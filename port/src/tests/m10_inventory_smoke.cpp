// M10 smoke test: proves the real inventory/character-manager chain end
// to end against real game data -- checks that a New Game grants no
// inventory (M107), loads a curated three-item fixture by actually running
// real armor/weapon/item .s files through the vendored interpreter
// (PlayerExecutable::LoadItemScripts), opens the real
// charactermanager.s -> inventory.s screens (the same MenuStack::OpenMenu
// path M4/M5 already proved for the main menu chain), switches inventory
// categories via the real ArmorMenu()/WeaponsMenu() script callbacks, and
// exercises a real equip (armor rating changes) and a real consumable use
// (fatigue rises, item leaves the inventory) -- all through the same
// native call paths the actual UI drives, not host-side shortcuts.
#include <cstdio>

#include "assets/string_table.h"
#include "simkin_bindings/character_progression.h"
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
        // MenuStack::RequestGameStart()).
        stack.RequestGameStart("azra");

        const auto& inv = stack.player().inventory();
        // M107: a New Game grants **nothing**. This used to be where the
        // port handed out a club, a chain coif and a loaf of bread -- M10's
        // real-data fixture, mistaken for the game's own starting kit and
        // shipped to the player for 97 milestones. See
        // player_executable.h for the evidence that the original grants no
        // inventory at all.
        if (!inv.empty()) {
            std::printf("m10_inventory_smoke: FAILED a New Game must grant no items, got %zu\n",
                        inv.size());
            for (const auto& item : inv) std::printf("  unexpected: \"%s\"\n", item->name().c_str());
            return 1;
        }
        std::printf("New Game grants 0 items  OK\n");

        // The rest of this test needs items to work on, so load the same
        // three real scripts explicitly -- one weapon, one armour, one
        // consumable, which is all the fixture was ever for.
        stack.player().LoadItemScripts(
            stack, {"weapons/club.s", "armor/chain_coif.s", "items/bread.s"});
        std::printf("fixture inventory: %zu item(s)\n", inv.size());
        for (const auto& item : inv) {
            std::printf("  \"%s\" type=%d\n", item->name().c_str(), item->itemType());
        }
        if (inv.size() != 3) {
            std::printf("m10_inventory_smoke: FAILED expected 3 fixture items, got %zu\n",
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

        // --- M25: real absolute layout -- charactermanager.s's own real
        // AddFloatingSprite/AddButton/AddItemButton x/y/w/h/ShowBorder
        // arguments (read directly off the install image), previously
        // discarded entirely by RenderMenu()'s plain vertical-list
        // rendering. Confirms the real 2x2 grid + portrait layout data is
        // actually captured now, not just "doesn't crash". ---
        {
            using RowKind = sk_bindings::MenuExecutable::RowKind;
            auto findRowAt = [&](RowKind kind, int x, int y) -> const sk_bindings::MenuExecutable::MenuRow* {
                for (const auto& row : charMgr->rows()) {
                    if (row.kind == kind && row.x == x && row.y == y) return &row;
                }
                return nullptr;
            };
            bool layoutOk = true;
            auto check = [&](const char* label, const sk_bindings::MenuExecutable::MenuRow* row) {
                std::printf("  %s: %s\n", label, row ? "found at real position OK" : "FAILED, not found");
                if (!row) layoutOk = false;
                return row;
            };
            // portrait=AddFloatingSprite(0, "", 109, 0, false);
            check("portrait (109,0)", findRowAt(RowKind::FloatingSprite, 109, 0));
            // healthItem = AddFloatingText(healthText, "", 8, 10, false);
            check("health text (8,10)", findRowAt(RowKind::StaticItem, 8, 10));
            // statsButton = AddButton(3043,"CMStatsScreen",10,100); w=80 h=23 ShowBorder(true)
            const auto* statsRow = check("Stats button (10,100)", findRowAt(RowKind::MenuItem, 10, 100));
            if (statsRow && (statsRow->w != 80 || statsRow->h != 23 || !statsRow->showBorder)) {
                std::printf("  FAILED Stats button real w/h/showBorder mismatch (w=%d h=%d "
                            "showBorder=%s)\n",
                            statsRow->w, statsRow->h, statsRow->showBorder ? "true" : "false");
                layoutOk = false;
            }
            // equipButton = AddButton(3774,"CMEquipScreen",90,100); w=80 h=23 ShowBorder(true)
            check("Equip button (90,100)", findRowAt(RowKind::MenuItem, 90, 100));
            // leftActionItem=AddItemButton(0,"LeftQueueSelected",10,123,80,60);
            const auto* leftHand =
                check("left-hand ItemButton (10,123)", findRowAt(RowKind::ItemButton, 10, 123));
            if (leftHand && (leftHand->w != 80 || leftHand->h != 60)) {
                std::printf("  FAILED left-hand ItemButton real w/h mismatch (w=%d h=%d)\n",
                            leftHand->w, leftHand->h);
                layoutOk = false;
            }
            // rightActionItem=AddItemButton(0,"RightQueueSelected",90,123,80,60);
            check("right-hand ItemButton (90,123)", findRowAt(RowKind::ItemButton, 90, 123));
            // questButton = AddButton(3773,"CMQuestLog",68,178); w=54 h=13 ShowBorder(true)
            check("Quest button (68,178)", findRowAt(RowKind::MenuItem, 68, 178));
            if (!layoutOk) {
                std::printf(
                    "m10_inventory_smoke: FAILED charactermanager.s's real layout data missing\n");
                return 1;
            }
        }

        // --- Post-M25 fix: GetMalePortrait(race)/GetFemalePortrait(race)
        // (any real menu script's real natives, per menus/
        // chooseportraitmenu.s -- exercised here through charMgr since the
        // handler lives on MenuExecutable::method() generically) must
        // return the real per-race/sex slot from the 16-slot global.spr
        // table (31-46, docs/GRAPHICS_FORMAT.md), not one shared slot.
        // race=0 is Argonian (chooseracemenu.s's own combo-box order,
        // also GetRace()'s real default), race=7 is Wood Elf. ---
        {
            auto getPortrait = [&](const char* method, int race) -> int {
                skRValueArray args;
                args.append(skRValue(race));
                skRValue ret;
                skExecutableContext ctxt(&interpreter);
                charMgr->method(skString(method), args, ret, ctxt);
                return ret.intValue();
            };
            bool portraitOk = true;
            auto checkSlot = [&](const char* label, int actual, int expected) {
                std::printf("  %s: %d (expected %d) %s\n", label, actual, expected,
                            actual == expected ? "OK" : "FAILED");
                if (actual != expected) portraitOk = false;
            };
            checkSlot("GetMalePortrait(0) Argonian", getPortrait("GetMalePortrait", 0), 31);
            checkSlot("GetFemalePortrait(0) Argonian", getPortrait("GetFemalePortrait", 0), 32);
            checkSlot("GetMalePortrait(7) Wood Elf", getPortrait("GetMalePortrait", 7), 45);
            checkSlot("GetFemalePortrait(7) Wood Elf", getPortrait("GetFemalePortrait", 7), 46);
            if (!portraitOk) {
                std::printf(
                    "m10_inventory_smoke: FAILED real per-race/sex portrait table mismatch\n");
                return 1;
            }
        }

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
        //
        // M74: every equip now goes through the real class gate first
        // (`FUN_1001f82c`), and `armor/chain_coif.s`'s own
        // `SetArmorConstraint(AR_Medium)` is not something every class may
        // wear. A default player object is a Battlemage, whose class row
        // allows AR_Light only, so it is *correctly* refused -- check that
        // first, then switch to a Knight (the row with all three weight
        // bits) for the rest of the equip exercise.
        {
            sk_bindings::ItemExecutable* coif = nullptr;
            for (const auto& item : stack.player().inventory()) {
                if (item->itemType() == sk_bindings::kItemTypeArmor) coif = item.get();
            }
            const int refused = coif ? stack.player().UpdateEquipStatus(coif, true) : -1;
            std::printf("\na Battlemage equipping AR_Medium armour: ret=%d (expect 3) %s\n",
                        refused, refused == 3 ? "OK" : "FAILED");
            if (refused != 3) {
                std::printf("m10_inventory_smoke: FAILED the class armour gate did not refuse\n");
                return 1;
            }
            skRValueArray classArgs;
            classArgs.append(skRValue(sk_bindings::kClassKnight));
            skRValue classRet;
            skExecutableContext classCtxt(&interpreter);
            stack.player().method(skString("ChooseCharacter"), classArgs, classRet, classCtxt);
        }
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
            // M74: the real success code is 1, not 0 -- see
            // PlayerExecutable::UpdateEquipStatus()'s own comment.
            if (ret2 != 1 || ret.intValue() != armor->armorValue()) {
                std::printf("m10_inventory_smoke: FAILED equip didn't change armor rating\n");
                return 1;
            }
        }

        // --- Real equip: the starting weapon (weapons/club.s), through
        // the same UpdateEquipStatus() entry point -- unlike armor
        // (armorRating() only), a weapon equip should be independently
        // observable two ways: it lands in leftItem()/rightItem() (the
        // real empty-hand-first auto-fill UpdateEquipStatus()'s own
        // comment documents, decompiled from FUN_10033660 case 1), and
        // GetAttack() picks up its real damage. Never exercised before --
        // only armor was, even though this exact code path is what
        // main.cpp's live melee combat (tryAttack) depends on every tick. ---
        sk_bindings::ItemExecutable* weapon = nullptr;
        for (const auto& item : stack.player().inventory()) {
            if (item->itemType() == sk_bindings::kItemTypeWeapon) weapon = item.get();
        }
        if (!weapon) {
            std::printf("m10_inventory_smoke: FAILED no weapon item in inventory\n");
            return 1;
        }
        bool handEmptyBefore = stack.player().leftItem() != weapon && stack.player().rightItem() != weapon;
        int attackBefore = stack.player().baseAttack();
        {
            skRValueArray args;
            skRValue ret;
            skExecutableContext ctxt(&interpreter);
            stack.player().method(skString("GetAttack"), args, ret, ctxt);
            attackBefore = ret.intValue();
        }
        int ret3 = stack.player().UpdateEquipStatus(weapon, true);
        bool handFilledAfter =
            stack.player().leftItem() == weapon || stack.player().rightItem() == weapon;
        int attackAfter = 0;
        {
            skRValueArray args;
            skRValue ret;
            skExecutableContext ctxt(&interpreter);
            stack.player().method(skString("GetAttack"), args, ret, ctxt);
            attackAfter = ret.intValue();
        }
        std::printf(
            "\nweapon equip: UpdateEquipStatus()=%d, hand empty before=%s, hand filled after=%s, "
            "GetAttack() %d -> %d\n",
            ret3, handEmptyBefore ? "true" : "false", handFilledAfter ? "true" : "false",
            attackBefore, attackAfter);
        // M74: 1 is the real "ok", and the hand a weapon lands in is its
        // own `+0x1c0` -- the right one -- not "whichever was free".
        if (ret3 != 1 || !handEmptyBefore || !handFilledAfter ||
            stack.player().rightItem() != weapon || attackAfter <= attackBefore) {
            std::printf(
                "m10_inventory_smoke: FAILED weapon equip didn't fill a hand or change GetAttack()"
                "\n");
            return 1;
        }
        bool equippedFlagOk = weapon->equipped();
        std::printf("weapon.equipped() after UpdateEquipStatus(): %s %s\n",
                    equippedFlagOk ? "true" : "false", equippedFlagOk ? "OK" : "FAILED");
        if (!equippedFlagOk) {
            std::printf("m10_inventory_smoke: FAILED weapon.equipped() still false\n");
            return 1;
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
