// M106 smoke test: the popup selectable flag, and two claims that were
// simply false.
//
//   FUN_100875e8  the popup item constructor -- `item+0x5c = 1`,
//                 unconditionally, callback or no callback
//   FUN_10087a60  popup case 3: SetSelectable(idx, flag), the only thing
//                 that clears that byte
//   FUN_10088a40  activation: fires the callback only when `item+0x5c`
//                 is set *and* `item+0x54` (the callback) is non-null
//   FUN_10088664  move-down: the non-wrapping arm requires only that the
//                 item have text; the wrapping arm requires selectable too
//
// Part 1  the constructor default -- an item with no callback is still
//         `selectable`, which is what the port used to get backwards
// Part 2  SetSelectable really clears and really restores, and an
//         out-of-range index is ignored rather than fatal
// Part 3  the bug, through a real menu and a real handler: activation is
//         refused on a cleared row and allowed on the same row restored
// Part 4  buysell.s, the script that proves it matters -- one popup built
//         with three callbacks and reused as a message box
// Part 5  the SetAttackRange correction: 28 shipped scripts call it, and
//         the port marches each creature's own value
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/on_detect.h"
#include "simkin_bindings/popup_menu_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"

namespace sk_b = sk_bindings;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("  %-92s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    if (!ok) ++g_failures;
}

std::string ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Strip `//` comments the way the corpus tools do, so a commented-out call
// never counts as a call. (`perosius_temp.s`'s dead `AiAttack()` and the
// four commented `SaveGame()` lines are exactly why this matters.)
std::string StripLineComments(const std::string& src) {
    std::string out;
    bool inString = false;
    for (size_t i = 0; i < src.size(); ++i) {
        if (src[i] == '"') inString = !inString;
        if (!inString && src[i] == '/' && i + 1 < src.size() && src[i + 1] == '/') {
            while (i < src.size() && src[i] != '\n') ++i;
            if (i < src.size()) out.push_back('\n');
            continue;
        }
        out.push_back(src[i]);
    }
    return out;
}

int CountOccurrences(const std::string& hay, const std::string& needle) {
    int n = 0;
    for (size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + 1)) ++n;
    return n;
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string root =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    if (!strings.Load(root + "/stringtable.eng")) {
        std::printf("m106_popup_selectable_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }

    auto call = [](skInterpreter& interp, skiExecutable* obj, const char* name,
                   skRValueArray args) {
        skRValue ret;
        skExecutableContext ctxt(&interp);
        try {
            obj->method(skString(name), args, ret, ctxt);
        } catch (skRuntimeException& e) {
            std::printf("   (RUNTIME ERROR in %s: %s)\n", name, e.toString().ptr());
        }
        return ret;
    };
    auto args1 = [](int a) {
        skRValueArray v;
        v.append(skRValue(a));
        return v;
    };
    auto args2 = [](int a, bool b) {
        skRValueArray v;
        v.append(skRValue(a));
        v.append(skRValue(b));
        return v;
    };
    auto addItem = [&](skInterpreter& interp, sk_b::PopupMenuExecutable* p, int textId,
                       const char* cb) {
        skRValueArray v;
        v.append(skRValue(textId));
        if (cb) v.append(skRValue(skString(cb)));
        skRValue ret;
        skExecutableContext ctxt(&interp);
        p->method(skString("AddItem"), v, ret, ctxt);
    };

    // ---------------------------------------------------------------
    // Part 1: the constructor default
    // ---------------------------------------------------------------
    std::printf("\n== Part 1: every item is born selectable ==\n");
    {
        skInterpreter interpreter;
        sk_b::MenuStack stack(root, interpreter, &strings);
        sk_b::MenuExecutable* menu = stack.GetOrCreateMenu("SaveGameMenu");
        Check(menu != nullptr, "savegamemenu.s loads as an owning menu");
        if (menu) {
            auto popup = std::make_unique<sk_b::PopupMenuExecutable>(*menu, 0, 0, 100, 50);
            addItem(interpreter, popup.get(), 3297, nullptr);      // a bare message line
            addItem(interpreter, popup.get(), 3297, "DoneSave");   // a real choice
            Check(popup->items().size() == 2, "two items added");
            if (popup->items().size() == 2) {
                // FUN_100875e8 stores 1 into `item+0x5c` for both -- the
                // flag does not depend on the callback argument.
                Check(popup->items()[0].selectable,
                      "the item added WITHOUT a callback is still `selectable`");
                Check(popup->items()[1].selectable,
                      "the item added WITH a callback is `selectable`");
                // IsSelectable() is the conjunction the engine's activation
                // test uses: the flag AND a callback.
                Check(!sk_b::PopupMenuExecutable::IsSelectable(popup->items()[0]),
                      "but it is not activatable -- no callback, so `+0x54` is null");
                Check(sk_b::PopupMenuExecutable::IsSelectable(popup->items()[1]),
                      "and the one with a callback is");
            }
        }
    }

    // ---------------------------------------------------------------
    // Part 2: SetSelectable clears, restores, and bounds-checks
    // ---------------------------------------------------------------
    std::printf("\n== Part 2: SetSelectable(idx, flag) ==\n");
    {
        skInterpreter interpreter;
        sk_b::MenuStack stack(root, interpreter, &strings);
        sk_b::MenuExecutable* menu = stack.GetOrCreateMenu("SaveGameMenu");
        if (menu) {
            auto popup = std::make_unique<sk_b::PopupMenuExecutable>(*menu, 0, 0, 100, 50);
            addItem(interpreter, popup.get(), 3297, "DoneSave");
            addItem(interpreter, popup.get(), 3297, "DoneSave");
            call(interpreter, popup.get(), "SetSelectable", args2(0, false));
            Check(!popup->items()[0].selectable, "SetSelectable(0,false) clears item 0's flag");
            Check(popup->items()[1].selectable, "and leaves item 1 alone");
            Check(!sk_b::PopupMenuExecutable::IsSelectable(popup->items()[0]),
                  "item 0 is no longer activatable even though it still has its callback");
            // buysell.s's own `SetSelectable(0, true); //reset` -- the
            // engine never restores the flag implicitly, which is why the
            // script has to.
            call(interpreter, popup.get(), "SetSelectable", args2(0, true));
            Check(popup->items()[0].selectable, "SetSelectable(0,true) restores it");
            Check(sk_b::PopupMenuExecutable::IsSelectable(popup->items()[0]),
                  "and it is activatable again");
            // `FUN_10087a60`'s case 3 walks the item list and simply runs
            // off the end for an out-of-range index; it must not be fatal.
            call(interpreter, popup.get(), "SetSelectable", args2(99, false));
            call(interpreter, popup.get(), "SetSelectable", args2(-1, false));
            Check(popup->items().size() == 2 && popup->items()[0].selectable,
                  "an out-of-range index is ignored, not fatal, and changes nothing");
        }
    }

    // ---------------------------------------------------------------
    // Part 3: the bug itself -- activation on a cleared row
    // ---------------------------------------------------------------
    std::printf("\n== Part 3: activation is refused on a cleared row ==\n");
    {
        // savegamemenu.s's `DoneSave` opens the "Game Saved." popup on its
        // menu, so whether the callback ran is directly observable as
        // `menu->activePopup()`. That is the same handler M91 used to
        // prove the one-button popup closes.
        skInterpreter interpreter;
        sk_b::MenuStack stack(root, interpreter, &strings);
        stack.SetGameActive(true);
        sk_b::MenuExecutable* menu = stack.GetOrCreateMenu("SaveGameMenu");
        if (menu) {
            menu->RunOnDisplay();
            auto popup = std::make_unique<sk_b::PopupMenuExecutable>(*menu, 0, 0, 100, 50);
            addItem(interpreter, popup.get(), 3297, "DoneSave");
            // Script-facing SetSelectedItem is 0-based (M91).
            call(interpreter, popup.get(), "SetSelectedItem", args1(0));
            Check(popup->IsItemSelected(0), "the cursor is on item 0");

            call(interpreter, popup.get(), "SetSelectable", args2(0, false));
            popup->ActivateSelected();
            Check(menu->activePopup() == nullptr,
                  "activating a CLEARED row does nothing -- DoneSave never ran");

            call(interpreter, popup.get(), "SetSelectable", args2(0, true));
            popup->ActivateSelected();
            Check(menu->activePopup() != nullptr,
                  "the same row, restored, does run it -- so the guard is the flag, "
                  "not something else");
        }
    }

    // ---------------------------------------------------------------
    // Part 4: buysell.s, the script that makes this reachable
    // ---------------------------------------------------------------
    std::printf("\n== Part 4: buysell.s builds one popup and reuses it ==\n");
    {
        const std::string raw = ReadFile(root + "/buysell.s");
        Check(!raw.empty(), "buysell.s reads");
        const std::string src = StripLineComments(raw);

        // All three items are created WITH a callback -- which is exactly
        // why "has a callback" could not stand in for the flag.
        Check(src.find("msgPopup.AddItem(3297,\"CloseMsgPopup\")") != std::string::npos,
              "msgPopup item 0 is AddItem(3297,\"CloseMsgPopup\") -- it has a callback");
        Check(src.find("msgPopup.AddItem(3297,\"ForcePurchase\")") != std::string::npos,
              "msgPopup item 1 is AddItem(3297,\"ForcePurchase\") -- so does it");

        // ...and then both are repurposed as message lines and cleared.
        Check(src.find("msgPopup.SetSelectable(0, false)") != std::string::npos,
              "and ForcePurchase clears item 0");
        Check(src.find("msgPopup.SetSelectable(1, false)") != std::string::npos,
              "and item 1 -- the row that still carries `ForcePurchase`");
        Check(src.find("msgPopup.SetSelectable(0, true)") != std::string::npos,
              "with an explicit `//reset` back to true before the next use");

        // The corpus-wide shape, so a future change that re-derives the
        // flag fails here rather than in a shop.
        int twoArgFalse = 0, twoArgTrue = 0;
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
             !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            if (it->path().extension() != ".s") continue;
            const std::string body = StripLineComments(ReadFile(it->path().string()));
            twoArgFalse += CountOccurrences(body, ", false)");
            twoArgTrue += CountOccurrences(body, ", true)");
        }
        Check(twoArgFalse > 0 && twoArgTrue > 0,
              "the corpus uses both polarities of the two-argument form");
    }

    // ---------------------------------------------------------------
    // Part 5: the SetAttackRange correction
    // ---------------------------------------------------------------
    std::printf("\n== Part 5: 28 scripts DO call SetAttackRange ==\n");
    {
        int callers = 0;
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
             !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            if (it->path().extension() != ".s") continue;
            const std::string body = StripLineComments(ReadFile(it->path().string()));
            if (body.find("SetAttackRange(") != std::string::npos) ++callers;
        }
        // on_detect.h used to claim this was zero. It is 27 scripts, not
        // the 28 a plain grep reports: `monsters/yelnicin.s` carries the
        // call commented out, which is why StripLineComments() is here.
        Check(callers == 27,
              "27 shipped scripts call SetAttackRange (the comment said none did) -- got " +
                  std::to_string(callers));

        // And the port marches each creature's own value, not the default.
        skInterpreter interpreter;
        sk_b::MenuStack stack(root, interpreter, &strings);
        auto load = [&](const std::string& rel) -> std::unique_ptr<sk_b::MonsterExecutable> {
            try {
                skExecutableContext ctxt(&interpreter);
                const std::string path = root + "/" + rel;
                auto m = std::make_unique<sk_b::MonsterExecutable>(
                    skString(path.c_str()), ctxt, &strings, stack.player(), stack);
                call(interpreter, m.get(), "Init", args1(0));
                return m;
            } catch (skParseException& e) {
                std::printf("   (PARSE ERROR loading %s: %s)\n", rel.c_str(), e.toString().ptr());
            } catch (skRuntimeException& e) {
                std::printf("   (RUNTIME ERROR loading %s: %s)\n", rel.c_str(),
                            e.toString().ptr());
            }
            return nullptr;
        };

        Check(sk_b::SightRangeTiles(0x6a4) == 6,
              "the constructor default 0x6a4 still buys 6 tiles");
        std::unique_ptr<sk_b::MonsterExecutable> archer = load("monsters/archer.s");
        Check(archer != nullptr, "monsters/archer.s loads");
        if (archer) {
            Check(archer->attackRangeRaw() == 12000,
                  "archer.s's SetAttackRange(12000) is stored, not discarded");
            Check(sk_b::SightRangeTiles(archer->attackRangeRaw()) == 46,
                  "which is 46 tiles of sight -- not the melee default's 6");
        }
        std::unique_ptr<sk_b::MonsterExecutable> deadeye = load("lakvan/deadeye.s");
        Check(deadeye != nullptr, "lakvan/deadeye.s loads");
        if (deadeye) {
            Check(deadeye->attackRangeRaw() == 50000,
                  "and the corpus maximum, deadeye.s's SetAttackRange(50000), survives too");
        }
    }

    std::printf("\nm106_popup_selectable_smoke: %d/%d checks passed -- %s\n", g_checks - g_failures,
                g_checks, g_failures == 0 ? "OK" : "FAILURES");
    return g_failures == 0 ? 0 : 1;
}
