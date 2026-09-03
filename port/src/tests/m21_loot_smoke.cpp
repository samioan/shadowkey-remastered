// M21 (monster-death loot-bag spawning + the loot-menu/container pattern)
// smoke test: proves the whole real chain end to end, not a synthetic
// fixture -- a killed monster's real SetLoot() tag resolves to a real
// loot-bag script, whose real Init() creates and collects a real item via
// the new Level.CreateEntity()/AddObject(), whose real OnUse() opens the
// real lootmenu.s (GetOpener()/GetFirst()/GetNext()/AddMenuItem's 3-arg
// associated-object form), and selecting that real row genuinely
// transfers the real item into the player's inventory
// (PickupItem()+RemoveObject()).
//
// Real data, read directly off the install image:
//   monsters/Azra_Rat.s:  SetLoot(300, "Loot_ratseye", 1, 8);
//   loot_ratseye.s:       Init(s) { ...; Item = Level.CreateEntity(704);
//                            AddObject(Item); }
//                         OnUse(s) { OpenMenu("LootMenu"); }
//   entities.txt:         704 30 9 items\ratseye.s (exactly what
//                         Level.CreateEntity(704) resolves to)
//   lootmenu.s:           SelectItem(s) { ...
//                            GetPlayer().PickupItem(Object);
//                            GetOpener().RemoveObject(Object); ... }
// This port's filesystem is case-insensitive (every other script-path
// lookup in this codebase already relies on this), so "Loot_ratseye"
// resolves straight to the real loot_ratseye.s file -- no lowercasing
// needed, matching main.cpp's own spawnLoot().
//
// Also covers loot_gold6-10.s separately: its own Init() calls
// `Item.SetQuantity(Random(6,10))` -- proves the new shared
// TryHandleRandom() (native_binding_common.h) against a real script that
// depends on it actually working (a pre-existing gap this session closed
// as a side effect of this milestone).
#include <cstdio>
#include <memory>
#include <string>

#include "assets/string_table.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/player_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"
#include "world/zone.h"

namespace {

std::unique_ptr<sk_bindings::ItemExecutable> LoadItem(const std::string& fullPath,
                                                        skInterpreter& interpreter,
                                                        sk_bindings::MenuStack& stack) {
    skExecutableContext loadCtxt(&interpreter);
    auto obj =
        std::make_unique<sk_bindings::ItemExecutable>(skString(fullPath.c_str()), loadCtxt, stack);
    skRValueArray args;
    args.append(skRValue(0));  // placeholder for Init's "(s)" parameter
    skRValue ret;
    skExecutableContext callCtxt(&interpreter);
    obj->method(skString("Init"), args, ret, callCtxt);
    return obj;
}

}  // namespace

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    if (!strings.Load(std::string(scriptRoot) + "/stringtable.eng")) {
        std::printf("m21_loot_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot)) {
        std::printf("m21_loot_smoke: FAILED to load entities.txt\n");
        return 1;
    }

    bool ok = true;
    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
    stack.level().SetEntityTypes(&entityTypes);

    // --- Part 1: real Azra_Rat.s's SetLoot() tag ---
    skExecutableContext loadCtxt(&interpreter);
    std::unique_ptr<sk_bindings::MonsterExecutable> rat;
    try {
        rat = std::make_unique<sk_bindings::MonsterExecutable>(
            skString((std::string(scriptRoot) + "/monsters/Azra_Rat.s").c_str()), loadCtxt,
            &strings, stack.player(), stack);
        skRValueArray args;
        args.append(skRValue(0));
        skRValue ret;
        skExecutableContext callCtxt(&interpreter);
        rat->method(skString("Init"), args, ret, callCtxt);
    } catch (skParseException& e) {
        std::printf("m21_loot_smoke: FAILED -- PARSE ERROR: %s\n", e.toString().ptr());
        return 1;
    } catch (skRuntimeException& e) {
        std::printf("m21_loot_smoke: FAILED -- RUNTIME ERROR: %s\n", e.toString().ptr());
        return 1;
    }
    bool tagOk = rat->lootTag() == "Loot_ratseye";
    std::printf("Azra_Rat.s lootTag(): \"%s\" (expected \"Loot_ratseye\") %s\n",
                rat->lootTag().c_str(), tagOk ? "OK" : "FAILED");
    if (!tagOk) ok = false;

    // --- Part 2: real loot_ratseye.s -- Level.CreateEntity()+AddObject() ---
    std::string bagPath = std::string(scriptRoot) + "/" + rat->lootTag() + ".s";
    std::unique_ptr<sk_bindings::ItemExecutable> bag;
    try {
        bag = LoadItem(bagPath, interpreter, stack);
    } catch (skParseException& e) {
        std::printf("m21_loot_smoke: FAILED -- PARSE ERROR loading %s: %s\n", bagPath.c_str(),
                    e.toString().ptr());
        return 1;
    } catch (skRuntimeException& e) {
        std::printf("m21_loot_smoke: FAILED -- RUNTIME ERROR loading %s: %s\n", bagPath.c_str(),
                    e.toString().ptr());
        return 1;
    }
    bool contentsOk = bag->contents().size() == 1;
    std::printf("loot_ratseye.s bag contents() after Init(): %zu (expected 1, from Level."
                "CreateEntity(704)+AddObject()) %s\n",
                bag->contents().size(), contentsOk ? "OK" : "FAILED");
    if (!contentsOk) ok = false;
    sk_bindings::ItemExecutable* ratseye = contentsOk ? bag->contents()[0].get() : nullptr;

    // --- Part 3: loot_gold6-10.s -- real Random()-driven SetQuantity ---
    std::unique_ptr<sk_bindings::ItemExecutable> goldBag;
    try {
        goldBag = LoadItem(std::string(scriptRoot) + "/loot_gold6-10.s", interpreter, stack);
    } catch (skParseException& e) {
        std::printf("m21_loot_smoke: FAILED -- PARSE ERROR: %s\n", e.toString().ptr());
        return 1;
    } catch (skRuntimeException& e) {
        std::printf("m21_loot_smoke: FAILED -- RUNTIME ERROR: %s\n", e.toString().ptr());
        return 1;
    }
    int goldQty = goldBag->contents().size() == 1 ? goldBag->contents()[0]->quantity() : -1;
    bool goldQtyOk = goldQty >= 6 && goldQty <= 10;
    std::printf("loot_gold6-10.s's gold quantity (Item.SetQuantity(Random(6,10))): %d (expected "
                "6..10) %s\n",
                goldQty, goldQtyOk ? "OK" : "FAILED");
    if (!goldQtyOk) ok = false;

    if (!ratseye) {
        std::printf("\nm21_loot_smoke: FAILED\n");
        return 1;
    }

    // --- Part 4: the payoff -- real OnUse() -> real lootmenu.s ---
    bag->InvokeOnUse();
    sk_bindings::MenuExecutable* menu = stack.currentMenu();
    bool menuOk = menu != nullptr;
    std::printf("bag.OnUse() opened a real menu (OpenMenu(\"LootMenu\")): %s %s\n",
                menuOk ? "true" : "false", menuOk ? "OK" : "FAILED");
    if (!menuOk) {
        std::printf("\nm21_loot_smoke: FAILED\n");
        return 1;
    }

    int ratseyeRow = -1;
    for (size_t i = 0; i < menu->rows().size(); ++i) {
        if (menu->rows()[i].associatedObject == static_cast<skiExecutable*>(ratseye)) {
            ratseyeRow = static_cast<int>(i) + 1;  // 1-based
            break;
        }
    }
    bool rowFoundOk = ratseyeRow > 0;
    std::printf("lootmenu.s's real UpdateMenu() (GetOpener().GetFirst()/GetNext(), AddMenuItem's "
                "3-arg associated-object form) found the real ratseye row: %s %s\n",
                rowFoundOk ? "true" : "false", rowFoundOk ? "OK" : "FAILED");
    if (!rowFoundOk) {
        std::printf("\nm21_loot_smoke: FAILED\n");
        return 1;
    }

    // Real navigation + dispatch: select that row, then fire it the same
    // way main.cpp's menu input handling calls ActivateSelected().
    {
        skRValueArray selArgs;
        selArgs.append(skRValue(ratseyeRow));
        skRValue selRet;
        skExecutableContext selCtxt(&interpreter);
        menu->method(skString("SetSelectedItem"), selArgs, selRet, selCtxt);
    }
    menu->ActivateSelected();  // real lootmenu.s SelectItem() handler

    bool inventoryOk = stack.player().inventory().size() == 1 &&
                        stack.player().inventory()[0].get() == ratseye;
    std::printf("player.inventory() after selecting the real row: %zu item(s), is the real "
                "ratseye object: %s\n",
                stack.player().inventory().size(), inventoryOk ? "OK" : "FAILED");
    if (!inventoryOk) ok = false;

    bool bagEmptyOk = bag->contents().empty();
    std::printf("bag contents() after RemoveObject(): %zu (expected 0) %s\n",
                bag->contents().size(), bagEmptyOk ? "OK" : "FAILED");
    if (!bagEmptyOk) ok = false;

    std::printf("\nm21_loot_smoke: %s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
