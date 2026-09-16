// M56 smoke test: the Player object's script API.
//
// A per-receiver coverage audit of the shipped corpus against
// docs/SIMKIN_NATIVE_API.md's 702-binding table found that `GetPlayer()`
// was the port's worst-covered receiver by a wide margin -- 2207 real
// call sites, only 55% of them landing on an implemented native. This
// milestone closes the inventory and menu half of that gap. Every
// expectation below is either decompiled from the real dispatchers
// (`FUN_1003f130` for the Player class, `FUN_10003810` for the Actor
// class it composites) or read straight out of shipped data.
//
//   OpenMenu(name)      332 sites, the single largest unhandled native.
//   FindInventory(id)   198 sites, with a "frozen_key" special case in
//                       front of the search that returns a bare int.
//   RemoveItem(x[,f])    71 sites; `x` is either an item object or that
//                       same int, and the two arms differ.
//   GetCharacter()       57 sites, a clamped read of the field
//                       ChooseCharacter() sets.
//   GiveItem(typeId)     29 sites, entities.txt -> inventory.
//   HasItem(id)          18 sites, an Actor binding reached through the
//                       player's composite.
//   HasAmulet(name)      10 sites -- and it never touches the inventory.
#include <cstdio>
#include <memory>
#include <string>

#include "assets/string_table.h"
#include "simkin_bindings/amulet_flags.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/player_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "world/entity_types.h"

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%-74s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    if (!ok) ++g_failures;
}

}  // namespace

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    strings.Load(std::string(scriptRoot) + "/stringtable.eng");
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot)) {
        std::printf("m56_player_api_smoke: FAILED to load entities.txt\n");
        return 1;
    }

    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
    stack.level().SetEntityTypes(&entityTypes);
    sk_bindings::PlayerExecutable& player = stack.player();

    auto call = [&](auto* obj, const char* name, skRValueArray& args) {
        skRValue r;
        skExecutableContext c(&interpreter);
        obj->method(skString(name), args, r, c);
        return r;
    };
    auto call0 = [&](auto* obj, const char* name) {
        skRValueArray none;
        return call(obj, name, none);
    };
    auto callStr = [&](auto* obj, const char* name, const std::string& s) {
        skRValueArray a;
        a.append(skRValue(skString(s.c_str())));
        return call(obj, name, a);
    };
    auto callInt = [&](auto* obj, const char* name, int v) {
        skRValueArray a;
        a.append(skRValue(v));
        return call(obj, name, a);
    };

    // ---- 1. the four key-item template ids, straight out of entities.txt ----
    {
        struct Row {
            int typeId;
            const char* script;
            const char* setId;
        };
        // The four ids the real HasAmulet/GiveItem code hardcodes. If any
        // of these rows ever stops matching, the bit table below is wrong.
        const Row rows[] = {
            {sk_bindings::kTemplateRedAmulet, "raiders\\RedAmulet.s", "redam"},
            {sk_bindings::kTemplateBlueAmulet, "raiders\\BlueAmulet.s", "blueam"},
            {sk_bindings::kTemplateGoldAmulet, "raiders\\GoldAmulet.s", "goldam"},
            {sk_bindings::kTemplateFrozenKey, "items\\frozen_key.s", "frozen_key"},
        };
        for (const Row& r : rows) {
            const sk::EntityTypeDescriptor* d = entityTypes.Lookup(r.typeId);
            Check(d != nullptr && d->name == r.script,
                  std::string("entities.txt ") + std::to_string(r.typeId) + " is " + r.script);
        }
        // And the name the native's wcscmp chain tests is the same string
        // the script's own SetID() sets -- which is the corroboration that
        // the bit table is a key-*item* table and not four unrelated flags.
        for (const Row& r : rows) {
            bool found = false;
            for (const sk_bindings::KeyItemFlag& f : sk_bindings::kKeyItemFlags) {
                if (f.templateId == r.typeId) found = f.scriptName == std::string(r.setId);
            }
            Check(found, std::string("HasAmulet(\"") + r.setId + "\") names template " +
                              std::to_string(r.typeId));
        }
    }

    // ---- 2. GetCharacter(), including the clamp ----
    {
        callInt(&player, "ChooseCharacter", 3);
        Check(call0(&player, "GetCharacter").intValue() == 3,
              "GetCharacter() reads back what ChooseCharacter() chose");
        // FUN_100207ec maps 0..8 to themselves and everything else to 0.
        callInt(&player, "ChooseCharacter", 8);
        Check(call0(&player, "GetCharacter").intValue() == 8, "...8 is the last valid class");
        callInt(&player, "ChooseCharacter", 9);
        Check(call0(&player, "GetCharacter").intValue() == 0, "...9 clamps to 0");
        callInt(&player, "ChooseCharacter", -1);
        Check(call0(&player, "GetCharacter").intValue() == 0, "...and so does a negative");
        callInt(&player, "ChooseCharacter", 2);
    }

    // ---- 3. FindInventory / HasItem, on a real three-item fixture ----
    {
        // M107: an explicit fixture -- a New Game grants no inventory.
        player.LoadItemScripts(stack, {"weapons/club.s", "armor/chain_coif.s", "items/bread.s"});
        // items/bread.s is the one fixture item with a SetID.
        skRValue bread = callStr(&player, "FindInventory", "bread");
        Check(bread.type() == skRValue::T_Object && bread.obj() != nullptr,
              "FindInventory(\"bread\") returns the real item object");
        Check(callStr(&player, "FindInventory", "stooth").obj() == nullptr,
              "FindInventory() of something not held returns null");
        Check(callStr(&player, "HasItem", "bread").boolValue(), "HasItem(\"bread\") is true");
        Check(!callStr(&player, "HasItem", "stooth").boolValue(),
              "HasItem() of something not held is false");
    }

    // ---- 4. GiveItem, and the flag word it feeds ----
    {
        Check(!callStr(&player, "HasAmulet", "blueam").boolValue(),
              "HasAmulet(\"blueam\") starts false");
        // raiders\BlueAmulet.s, category 3.
        callInt(&player, "GiveItem", sk_bindings::kTemplateBlueAmulet);
        Check(callStr(&player, "FindInventory", "blueam").obj() != nullptr,
              "GiveItem(718) puts a real raiders\\BlueAmulet.s in the inventory");
        Check(callStr(&player, "HasAmulet", "blueam").boolValue(),
              "...and acquiring it sets the blue bit (FUN_1003d8e0)");
        Check(!callStr(&player, "HasAmulet", "redam").boolValue() &&
                  !callStr(&player, "HasAmulet", "goldam").boolValue(),
              "...and only the blue bit -- red and gold stay clear");
        Check(!callStr(&player, "HasAmulet", "cellkey").boolValue(),
              "HasAmulet() of a name outside the four-way chain is false, not an error");
        // An ordinary item must not disturb the word.
        unsigned before = player.keyItemFlags().word();
        callInt(&player, "GiveItem", 714);  // items\startooth.s
        Check(callStr(&player, "HasItem", "stooth").boolValue() &&
                  player.keyItemFlags().word() == before,
              "GiveItem() of an ordinary item touches no flag bit");
        // A typeId with no item-shaped entities.txt row is a clean no-op.
        int held = static_cast<int>(player.inventory().size());
        callInt(&player, "GiveItem", 274);  // a creature row
        Check(static_cast<int>(player.inventory().size()) == held,
              "GiveItem() of a non-item typeId adds nothing");
    }

    // ---- 5. RemoveItem's two arms ----
    {
        // (a) the object arm -- what every `RemoveItem(Key, false)` site
        // reaches when the real item is still held.
        skRValue tooth = callStr(&player, "FindInventory", "stooth");
        skRValueArray a;
        a.append(tooth);
        a.append(skRValue(false));
        call(&player, "RemoveItem", a);
        player.PurgeRemovedItems();
        Check(!callStr(&player, "HasItem", "stooth").boolValue(),
              "RemoveItem(item, false) removes the item it was handed");

        // (b) the integer arm, and the frozen key's counter. Acquiring the
        // key sets the bit and counts 1.
        callInt(&player, "GiveItem", sk_bindings::kTemplateFrozenKey);
        Check(callStr(&player, "HasAmulet", "frozen_key").boolValue() &&
                  player.keyItemFlags().frozenKeyCount() == 1,
              "GiveItem(805) sets the frozen-key bit and counts one");
        // The special case is tested *before* the search, so from the
        // moment the key is acquired FindInventory("frozen_key") returns
        // the stand-in int and never the object -- meaning the corpus's
        // `RemoveItem(Key, false)` sites for this one item only ever
        // reach RemoveItem's integer arm. That is what the integer arm is
        // for, and it is not obvious from either side alone.
        skRValue key = callStr(&player, "FindInventory", "frozen_key");
        Check(key.type() != skRValue::T_Object &&
                  key.intValue() == sk_bindings::kTemplateFrozenKey,
              "FindInventory(\"frozen_key\") short-circuits to the int even while held");

        callInt(&player, "RemoveItem", sk_bindings::kFrozenKeyStandIn);
        player.PurgeRemovedItems();
        Check(!callStr(&player, "HasItem", "frozen_key").boolValue(),
              "RemoveItem(805) removes the key object from the inventory");
        // The real handler tests the counter *before* decrementing it, so
        // the flag survives the first removal.
        Check(callStr(&player, "HasAmulet", "frozen_key").boolValue() &&
                  player.keyItemFlags().frozenKeyCount() == 0,
              "...but the flag survives it -- the test precedes the decrement");
        skRValue standIn = callStr(&player, "FindInventory", "frozen_key");
        Check(standIn.type() != skRValue::T_Object &&
                  standIn.intValue() == sk_bindings::kTemplateFrozenKey,
              "...so FindInventory() now hands back the bare int 0x325 instead");

        // Second use: the counter is 0, so this one does clear the bit,
        // and the search that follows finds nothing and returns quietly.
        callInt(&player, "RemoveItem", sk_bindings::kFrozenKeyStandIn);
        Check(!callStr(&player, "HasAmulet", "frozen_key").boolValue() &&
                  player.keyItemFlags().frozenKeyCount() == -1,
              "the second RemoveItem(805) clears the bit, counter goes to -1");
        Check(callStr(&player, "FindInventory", "frozen_key").obj() == nullptr,
              "...and FindInventory() is back to returning null");
    }

    // ---- 6. OpenMenu, against the real scripts that call it ----
    {
        // crypt1.s and erthcave.s both do GetPlayer().OpenMenu("TrapMenu")
        // when the player triggers a trap; trapmenu.s is a real shipped
        // file. Before this milestone the call soft-failed and the screen
        // never appeared -- 62 of the suite's soft-fail lines were this
        // one call.
        callStr(&player, "OpenMenu", "TrapMenu");
        sk_bindings::MenuExecutable* current = stack.currentMenu();
        Check(current != nullptr, "GetPlayer().OpenMenu(\"TrapMenu\") opens trapmenu.s");
        Check(current != nullptr &&
                  call0(current, "GetOpener").obj() == static_cast<skiExecutable*>(&player),
              "...with the player as the new menu's opener");
        // A backslash-escaped subdirectory path, the other shape real
        // scripts use -- broken1/cage_doora.s does
        // `GetPlayer().OpenMenu("broken1\\\\cagedoor")`.
        callStr(&player, "OpenMenu", "broken1\\cagedoor");
        Check(stack.currentMenu() != nullptr && stack.currentMenu() != current,
              "...and a \"Dir\\\\Menu\" path resolves to the real subdirectory script");
        // A menu that does not exist must leave the current one alone
        // rather than blanking the screen.
        sk_bindings::MenuExecutable* held = stack.currentMenu();
        callStr(&player, "OpenMenu", "NoSuchMenuAtAll");
        Check(stack.currentMenu() == held, "OpenMenu() of a missing script keeps the current menu");

        // And a real script whose Init() *throws*. glcrcrwl/gate1.s opens
        // with `Gate = Level.GetEntity("box1"); if (Gate.saved_Gate = 0)`,
        // so with no zone loaded the interpreter raises "Cannot get field
        // saved_Gate from a non-object" out of Init(). That exception used
        // to escape into the host and abort the process -- which is the
        // sort of thing making 332 new script bodies reachable turns up.
        callStr(&player, "OpenMenu", "GlcrCrwl\\Gate1");
        Check(stack.currentMenu() == held,
              "OpenMenu() of a script whose Init() throws does not take the host down");
    }

    std::printf("\nm56_player_api_smoke: %s (%d checks)\n",
                g_failures ? "FAILED" : "PASSED (all checks)", 36);
    return g_failures ? 1 : 0;
}
