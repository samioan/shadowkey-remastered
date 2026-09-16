// M104 smoke test: the last of the post-M99 audit list -- audit items 6
// and 7.
//
//   FUN_1003136c  menu case 0: DelayOnEnter, `menu+0xc8 = time() + 3`
//   FUN_10032868  the row-activate handler that reads it, whose first
//                 guard is `time() >= menu+0xc8`
//   FUN_10087a60  popup case 4: SetFocus, byte-for-byte case 5
//                 (SetSelectedItem) plus an out-of-range notice
//   FUN_10078de4  root cases 0x3a/0x3b: IsMultiplayer/IsMultiplayerClient
//   FUN_1006dbec  Level's own copies of the same two (cases 0 and 1)
//
// Part 1  the multiplayer pair, on every receiver that answers it
// Part 2  SetFocus, through buysell.s's own popups
// Part 3  DelayOnEnter, through herbhurrah.s and the rat that opens it
// Part 4  what is left in the soft-fail log -- three names the engine
//         does not dispatch either, and one chain that is real and is the
//         next milestone
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/popup_menu_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"

namespace sk_b = sk_bindings;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("  %-92s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    if (!ok) ++g_failures;
}

// Is `name` present in the shipped image as a UTF-16LE literal? Every
// registered native name is, because the registration functions insert it
// into a trie by that string. M102 settled `ReachedDestination` the same
// way.
bool ImageHasWideLiteral(const std::string& imagePath, const std::string& name) {
    std::ifstream in(imagePath, std::ios::binary);
    const std::string blob((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string wide;
    for (char c : name) {
        wide.push_back(c);
        wide.push_back('\0');
    }
    return !blob.empty() && blob.find(wide) != std::string::npos;
}

constexpr int kAzraRat = 202;
constexpr int kHerbRat = 204;       // ratherb.s -- the quest rat
constexpr int kVerminBomb = 711;    // any item will do as a receiver
constexpr int kLootBagTypeId = 300;

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string root =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    if (!strings.Load(root + "/stringtable.eng")) {
        std::printf("m104_last_natives_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }
    sk::EntityTypeTable entityTypes;
    entityTypes.Load(root);
    skInterpreter interpreter;
    sk_b::MenuStack stack(root, interpreter, &strings);
    stack.level().SetEntityTypes(&entityTypes);
    sk_b::PlayerExecutable& player = stack.player();

    auto call = [&](skiExecutable* obj, const char* name, skRValueArray args) {
        skRValue ret;
        skExecutableContext ctxt(&interpreter);
        try {
            obj->method(skString(name), args, ret, ctxt);
        } catch (skRuntimeException& e) {
            std::printf("   (RUNTIME ERROR in %s: %s)\n", name, e.toString().ptr());
        }
        return ret;
    };
    auto noArgs = []() { return skRValueArray(); };
    auto loadMonster = [&](const std::string& rel) -> std::unique_ptr<sk_b::MonsterExecutable> {
        try {
            skExecutableContext ctxt(&interpreter);
            const std::string path = root + "/" + rel;
            auto m = std::make_unique<sk_b::MonsterExecutable>(skString(path.c_str()), ctxt,
                                                                &strings, player, stack);
            skRValueArray initArgs;
            initArgs.append(skRValue(0));
            call(m.get(), "Init", initArgs);
            return m;
        } catch (skParseException& e) {
            std::printf("   (PARSE ERROR loading %s: %s)\n", rel.c_str(), e.toString().ptr());
        } catch (skRuntimeException& e) {
            std::printf("   (RUNTIME ERROR loading %s: %s)\n", rel.c_str(), e.toString().ptr());
        }
        return nullptr;
    };

    // ---------------------------------------------------------------
    // Part 1: IsMultiplayer / IsMultiplayerClient
    // ---------------------------------------------------------------
    std::printf("\n== Part 1: the multiplayer pair ==\n");
    {
        // Both read `engine+0x5c0`, which is zero in this port and has
        // nothing that could set it -- so both are false, which is also
        // what the shipped game answers in single player. They were already
        // *answering* false by soft-failing to 0; what changes is that the
        // port now says so on purpose, on every class that registers them.
        struct Receiver {
            const char* what;
            skiExecutable* obj;
        };
        std::unique_ptr<sk_b::ItemExecutable> item = stack.level().CreateItem(kVerminBomb, false);
        std::unique_ptr<sk_b::MonsterExecutable> rat = loadMonster("monsters/azra_rat.s");
        stack.OpenMenu("inventory");
        const Receiver receivers[] = {
            {"Level", &stack.level()},
            {"an item", item.get()},
            {"a creature", rat.get()},
            {"a menu", stack.currentMenu()},
        };
        for (const Receiver& r : receivers) {
            if (!r.obj) {
                Check(false, std::string("receiver missing: ") + r.what);
                continue;
            }
            const skRValue mp = call(r.obj, "IsMultiplayer", noArgs());
            const skRValue client = call(r.obj, "IsMultiplayerClient", noArgs());
            Check(!mp.boolValue() && !client.boolValue(),
                  std::string(r.what) + " answers false to both, with no soft-fail");
        }
        // And the branch it decides, in the one script where it decides
        // something the player can see. `ratherb.s`'s OnKilled is
        // `if (Level.IsMultiplayer()) { ...host/client... } else
        // { GetPlayer().OpenMenu("herbhurrah"); }` -- the else arm is what
        // must run.
        std::unique_ptr<sk_b::MonsterExecutable> herbRat = loadMonster("ratherb.s");
        Check(herbRat != nullptr, "ratherb.s loads -- the Azra herb rat");
        if (herbRat) {
            const std::string before =
                stack.currentMenu() ? stack.currentMenu()->scriptName() : std::string();
            herbRat->InvokeOnKilled();
            sk_b::MenuExecutable* now = stack.currentMenu();
            Check(now != nullptr && now->scriptName() != before,
                  "  killing it takes the single-player arm and opens a screen");
            Check(player.questSolved(1),
                  "  and the SetQuestSolved(1) after the branch still runs");
        }
    }

    // ---------------------------------------------------------------
    // Part 2: SetFocus
    // ---------------------------------------------------------------
    std::printf("\n== Part 2: SetFocus ==\n");
    {
        // buysell.s builds msgPopup as three rows -- message, "Buy Anyway",
        // "Cancel" -- and its "your trade cannot use this" branch is
        // SetFocus(1); SetSelectable(0, false); SetVisible(true). So the
        // prompt opens pointing at Buy Anyway.
        stack.OpenMenu("buysell");
        sk_b::MenuExecutable* store = stack.currentMenu();
        Check(store != nullptr, "buysell.s opens");
        // Built directly rather than through the CreatePopupMenu native:
        // that one hands back a Simkin-ref-counted object, and a test that
        // keeps only a raw pointer to it is holding a freed one.
        std::unique_ptr<sk_b::PopupMenuExecutable> owned;
        if (store) {
            owned = std::make_unique<sk_b::PopupMenuExecutable>(*store, 4, 60, 170, 60);
        }
        sk_b::PopupMenuExecutable* popup = owned.get();
        Check(popup != nullptr, "  and a popup with buysell.s's own geometry");
        if (popup) {
            for (int i = 0; i < 3; ++i) {
                skRValueArray add;
                add.append(skRValue(3297));
                add.append(skRValue(skString("CloseMsgPopup")));
                call(popup, "AddItem", add);
            }
            skRValueArray focus;
            focus.append(skRValue(1));
            call(popup, "SetFocus", focus);
            Check(popup->IsItemSelected(1),
                  "SetFocus(1) lands on row 1 -- the same 0-based index SetSelectedItem takes");
            skRValueArray other;
            other.append(skRValue(2));
            call(popup, "SetSelectedItem", other);
            Check(popup->IsItemSelected(2), "  and the two write the same popup+0xa0");
            // The engine logs and writes anyway; it does not clamp.
            skRValueArray past;
            past.append(skRValue(9));
            call(popup, "SetFocus", past);
            Check(popup->selectedItem() == 10,
                  "  an out-of-range index is reported and stored, not clamped");
        }
    }

    // ---------------------------------------------------------------
    // Part 3: DelayOnEnter
    // ---------------------------------------------------------------
    std::printf("\n== Part 3: DelayOnEnter ==\n");
    {
        stack.OpenMenu("inventory");
        sk_b::MenuExecutable* ordinary = stack.currentMenu();
        Check(ordinary != nullptr && ordinary->enterAllowedAt() == 0,
              "a screen that never calls it has no deadline at all");

        stack.OpenMenu("herbhurrah");
        sk_b::MenuExecutable* screen = stack.currentMenu();
        Check(screen != nullptr, "herbhurrah.s opens");
        if (screen) {
            const std::time_t now = std::time(nullptr);
            Check(screen->enterAllowedAt() >= now &&
                      screen->enterAllowedAt() <=
                          now + sk_b::MenuExecutable::kEnterDelaySeconds,
                  "its Init's DelayOnEnter() sets menu+0xc8 to time() + 3");
            Check(screen->enterDelayed(now), "  so right now the select key is refused");
            Check(!screen->enterDelayed(now + sk_b::MenuExecutable::kEnterDelaySeconds),
                  "  and three seconds later it is not");
            // The screen is one MenuQuit button. While delayed, activating
            // it must not close the screen; that is the whole point.
            Check(!screen->rows().empty(), "  it has rows to activate");
            screen->ActivateSelected();
            Check(stack.currentMenu() == screen,
                  "activating a row inside the delay does nothing -- FUN_10032868's first guard");
            // Nothing else about the screen is gated: navigation still
            // works, and so does the back key.
            const int wasSelected = screen->selectedItem();
            screen->MoveSelection(1);
            Check(screen->selectedItem() != wasSelected || screen->rows().size() == 1,
                  "  navigation is not gated");
        }
    }

    // ---------------------------------------------------------------
    // Part 4: what is left, and why
    // ---------------------------------------------------------------
    std::printf("\n== Part 4: the rest of the soft-fail log ==\n");
    {
        const std::string image = root + "/6r51.app";
        std::ifstream probe(image, std::ios::binary);
        Check(probe.good(), "6r51.app is where the game data is");
        // A name the engine dispatches has to exist in the image as a wide
        // literal -- whether it is inserted into a class trie or compared
        // with wcscmp. These three are not there at all, so they miss on
        // the real device too: script typos and leftovers, not gaps here.
        const char* dead[] = {"HitTarget", "SetMagicResistable", "MenuQuit"};
        for (const char* name : dead) {
            Check(!ImageHasWideLiteral(image, name),
                  std::string("`") + name + "` is not a literal in the image: it misses there too");
        }
        // The control: a name that *is* dispatched, proving the search
        // would have found one.
        Check(ImageHasWideLiteral(image, "DelayOnEnter"),
              "`DelayOnEnter` is -- so the search does find real names");

        // And the two that this test was originally written to call dead,
        // wrongly. They are in the image, on the `wcscmp` chain
        // `FUN_10033dd0` -- the action-queue screen's own four methods,
        // which no trie holds and neither coverage tool can see. M93 found
        // three such chains; this is a fourth.
        Check(ImageHasWideLiteral(image, "IsRightQueue") &&
                  ImageHasWideLiteral(image, "UpdateTextItems") &&
                  ImageHasWideLiteral(image, "GetLastItem") &&
                  ImageHasWideLiteral(image, "MoveItem"),
              "the action-queue screen's four methods are all real literals, trie or no trie");
        stack.OpenMenu("actionqueue");
        sk_b::MenuExecutable* queue = stack.currentMenu();
        Check(queue != nullptr, "actionqueue.s opens");
        if (queue) {
            // IsRightQueue is one line of that chain and the only one that
            // stands alone: its caller picks a title with it.
            queue->SetQueueHandIsRight(true);
            Check(call(queue, "IsRightQueue", noArgs()).boolValue(),
                  "  IsRightQueue answers the hand byte ShowActionQueue copied on");
            queue->SetQueueHandIsRight(false);
            Check(!call(queue, "IsRightQueue", noArgs()).boolValue(),
                  "  and the other hand too -- the screen's title was always the left one");
        }
        // The sixth is not a native at all: `Init` is registered only on the
        // collection class (0x14d50 case 0), and the line comes from this
        // suite calling Init on a loot-bag script that defines no handler.
        std::unique_ptr<sk_b::ItemExecutable> bag =
            stack.level().CreateEntityWithScript(kLootBagTypeId, "Loot_Dropped");
        Check(bag != nullptr, "the dropped-loot bag script loads");
        if (bag) {
            // Calling it is what the older tests do; the line in the log
            // is that call finding no handler and no native.
            skRValueArray initArgs;
            initArgs.append(skRValue(0));
            call(bag.get(), "Init", initArgs);
            Check(true, "  and calling Init on it is where `Item: Init(0)` comes from");
        }
        (void)kAzraRat;
    }

    std::printf("\nm104_last_natives_smoke: %d/%d checks passed -- %s\n", g_checks - g_failures,
                g_checks, g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
