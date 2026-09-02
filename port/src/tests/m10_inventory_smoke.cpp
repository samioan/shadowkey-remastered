// M10 smoke test: proves the real inventory/character-manager chain end
// to end against real game data -- loads a curated starting inventory by
// actually running real armor/weapon/item .s files through the vendored
// interpreter (PlayerExecutable::LoadStartingInventory), opens the real
// charactermanager.s -> inventory.s screens (the same MenuStack::OpenMenu
// path M4/M5 already proved for the main menu chain), switches inventory
// categories via the real ArmorMenu()/WeaponsMenu() script callbacks, and
// exercises a real equip (armor rating changes) and a real consumable use
// (fatigue rises, item leaves the inventory) -- all through the same
// native call paths the actual UI drives, not host-side shortcuts.
#include <cstdio>

#include "assets/string_table.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/table_executable.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"

namespace {

sk_bindings::TableExecutable* InventoryTable(sk_bindings::MenuExecutable& menu) {
    for (const auto& row : menu.rows()) {
        if (row.kind == sk_bindings::MenuExecutable::RowKind::Table) {
            return static_cast<sk_bindings::TableExecutable*>(row.widget.get());
        }
    }
    return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    if (!strings.Load(std::string(scriptRoot) + "/stringtable.eng")) {
        std::printf("m10_inventory_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }

    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);

    try {
        // Mirrors main.cpp's New Game path (MenuExecutable::NewGame() ->
        // MenuStack::RequestGameStart()) -- the first call also runs the
        // real starting-inventory item scripts.
        stack.RequestGameStart("azra");

        const auto& inv = stack.player().inventory();
        std::printf("starting inventory: %zu item(s)\n", inv.size());
        for (const auto& item : inv) {
            std::printf("  \"%s\" type=%d\n", item->name().c_str(), item->itemType());
        }
        if (inv.size() != 3) {
            std::printf("m10_inventory_smoke: FAILED expected 3 starting items, got %zu\n",
                        inv.size());
            return 1;
        }

        stack.OpenMenu("charactermanager");
        sk_bindings::MenuExecutable* charMgr = stack.currentMenu();
        if (!charMgr) {
            std::printf("m10_inventory_smoke: FAILED to open charactermanager.s\n");
            return 2;
        }
        std::printf("\ncharactermanager.s: %zu row(s) after OnDisplay()\n", charMgr->rows().size());

        stack.OpenMenu("inventory");
        sk_bindings::MenuExecutable* inventoryMenu = stack.currentMenu();
        if (!inventoryMenu) {
            std::printf("m10_inventory_smoke: FAILED to open inventory.s\n");
            return 2;
        }
        // inventory.s's own Init() calls self.WeaponsMenu() twice, so the
        // table should already be showing the one starting weapon.
        sk_bindings::TableExecutable* table = InventoryTable(*inventoryMenu);
        if (!table) {
            std::printf("m10_inventory_smoke: FAILED -- no Table row found on inventory.s\n");
            return 2;
        }
        std::printf("\ninventory.s weapons page: %d row(s), first = \"%s\"\n", table->rowCount(),
                    table->CellText(0, 0).c_str());
        if (table->rowCount() != 1) {
            std::printf("m10_inventory_smoke: FAILED expected 1 weapon row\n");
            return 1;
        }

        inventoryMenu->TryInvoke("ArmorMenu");
        std::printf("inventory.s armor page: %d row(s), first = \"%s\"\n", table->rowCount(),
                    table->CellText(0, 0).c_str());
        if (table->rowCount() != 1) {
            std::printf("m10_inventory_smoke: FAILED expected 1 armor row\n");
            return 1;
        }

        // --- Real equip: toggling the armor item through the same
        // UpdateEquipStatus() entry point inventory.s's
        // PerformEquipAction() calls should change GetArmorRating(). ---
        sk_bindings::ItemExecutable* armor = nullptr;
        for (const auto& item : stack.player().inventory()) {
            if (item->itemType() == sk_bindings::kItemTypeArmor) armor = item.get();
        }
        if (!armor) {
            std::printf("m10_inventory_smoke: FAILED no armor item in inventory\n");
            return 1;
        }
        {
            skRValueArray args;
            skRValue ret;
            skExecutableContext ctxt(&interpreter);
            stack.player().method(skString("GetArmorRating"), args, ret, ctxt);
            std::printf("\narmor rating before equip: %d\n", ret.intValue());
            if (ret.intValue() != 0) {
                std::printf("m10_inventory_smoke: FAILED expected 0 armor rating unequipped\n");
                return 1;
            }
        }
        int ret2 = stack.player().UpdateEquipStatus(armor, true);
        {
            skRValueArray args;
            skRValue ret;
            skExecutableContext ctxt(&interpreter);
            stack.player().method(skString("GetArmorRating"), args, ret, ctxt);
            std::printf("UpdateEquipStatus(equip)=%d, armor rating after equip: %d\n", ret2,
                        ret.intValue());
            if (ret2 != 0 || ret.intValue() != armor->armorValue()) {
                std::printf("m10_inventory_smoke: FAILED equip didn't change armor rating\n");
                return 1;
            }
        }

        // --- Real hand-selection flow (charactermanager.s ->
        // actionqueue.s): decompiled this session (FUN_10034d8c) --
        // RightQueueSelected() sets a hand flag on charactermanager.s's
        // own instance, then ShowActionQueue() copies it onto the
        // actionqueue.s instance and actually pushes it. Guards against
        // ShowActionQueue() regressing back to its old complete no-op. ---
        stack.OpenMenu("charactermanager");
        charMgr = stack.currentMenu();
        charMgr->TryInvoke("RightQueueSelected");
        sk_bindings::MenuExecutable* actionQueue = stack.currentMenu();
        std::printf("\nafter RightQueueSelected(): current menu is %s\n",
                    actionQueue == charMgr ? "still charactermanager (FAILED)" : "actionqueue");
        if (actionQueue == charMgr || !actionQueue->queueHandIsRight()) {
            std::printf(
                "m10_inventory_smoke: FAILED ShowActionQueue() didn't open actionqueue.s with "
                "the right-hand flag set\n");
            return 1;
        }

        // --- Real consumable use: OnUsedBy() should run the item's own
        // OnUse() handler (items/bread.s: GetOwner().SetFatigue(...)) and
        // mark itself for removal; PurgeRemovedItems() then drops it. ---
        sk_bindings::ItemExecutable* consumable = nullptr;
        for (const auto& item : stack.player().inventory()) {
            if (item->itemType() == sk_bindings::kItemTypeConsumable) consumable = item.get();
        }
        if (!consumable) {
            std::printf("m10_inventory_smoke: FAILED no consumable item in inventory\n");
            return 1;
        }
        {
            skRValueArray setFatigueArgs;
            setFatigueArgs.append(skRValue(50));
            skRValue ret;
            skExecutableContext ctxt(&interpreter);
            stack.player().method(skString("SetFatigue"), setFatigueArgs, ret, ctxt);
        }
        int fatigueBefore = stack.player().fatigue();
        {
            skRValueArray useArgs;
            useArgs.append(skRValue(static_cast<skiExecutable*>(&stack.player()), false));
            skRValue ret;
            skExecutableContext ctxt(&interpreter);
            consumable->method(skString("OnUsedBy"), useArgs, ret, ctxt);
        }
        std::printf("\nfatigue before use: %d, after: %d, marked for removal: %s\n", fatigueBefore,
                    stack.player().fatigue(), consumable->markedForRemoval() ? "true" : "false");
        if (stack.player().fatigue() <= fatigueBefore || !consumable->markedForRemoval()) {
            std::printf("m10_inventory_smoke: FAILED OnUsedBy() had no real effect\n");
            return 1;
        }
        stack.player().PurgeRemovedItems();
        std::printf("inventory after purge: %zu item(s)\n", stack.player().inventory().size());
        if (stack.player().inventory().size() != 2) {
            std::printf("m10_inventory_smoke: FAILED consumable wasn't purged\n");
            return 1;
        }

        std::printf("\nm10_inventory_smoke: OK\n");
        return 0;
    } catch (skParseException& e) {
        std::printf("m10_inventory_smoke: PARSE ERROR: %s\n", e.toString().ptr());
        return 2;
    } catch (skRuntimeException& e) {
        std::printf("m10_inventory_smoke: RUNTIME ERROR: %s\n", e.toString().ptr());
        return 2;
    }
}
