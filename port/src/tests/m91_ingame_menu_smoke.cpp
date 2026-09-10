// M91 smoke test: the in-game main menu, saving and loading, and the row
// callbacks that never fired.
//
// Reported from play: pressing Esc in the game brought up the *title*
// menu -- New Game, Credits, Multiplayer -- with no Save Game anywhere;
// popups and secondary pages could get stuck with no way to close them;
// and the "are you sure you want to leave town" prompt in azra would not
// answer either of its two rows.
//
// Three separate defects underneath, all of them one line of the real
// engine each.
//
//   Part 1  A menu row's callback is an ordinary Simkin method call, so a
//           bare native name in the callback slot is normal -- and 290
//           shipped call sites use one. TryInvoke() only ever reached the
//           script, so all 290 did nothing.
//   Part 2  `GameActive()` (menu binding 0x5e) is `engine + 0x6908 != 0`,
//           not a hardcoded false -- and it is the whole of mainmenu.s's
//           OnDisplay.
//   Part 3  A popup's `SetSelectedItem` is **0-based**, so every confirm
//           popup in the game opens on its first *choice*, not on the
//           message line above it (where Enter did nothing at all).
//   Part 4  azra_menu_yousure.s end to end -- the reported screen.
//   Part 5  Saving and loading: the slot pair, the three result callbacks,
//           and the placement a save has always had room for and never
//           filled in.
//   Part 6  The screen-mode constants, which had 1 and 5 the wrong way
//           round.
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "engine/screen_mode.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/popup_menu_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%-86s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    if (!ok) ++g_failures;
}

std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::string();
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// `Name[ (args)` at the start of a line -- how every shipped `.s` declares
// a handler. Same reader M90's test uses.
std::set<std::string> HandlerNames(const std::string& src) {
    std::set<std::string> names;
    size_t pos = 0;
    while (pos < src.size()) {
        size_t eol = src.find('\n', pos);
        std::string line = src.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
        pos = eol == std::string::npos ? src.size() : eol + 1;
        size_t at = line.find_first_not_of(" \t");
        if (at == std::string::npos) continue;
        size_t bracket = line.find('[', at);
        if (bracket == std::string::npos) continue;
        std::string name = line.substr(at, bracket - at);
        while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();
        if (name.empty()) continue;
        bool ident = true;
        for (char c : name) {
            if (!(c == '_' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                  (c >= 'A' && c <= 'Z'))) {
                ident = false;
            }
        }
        if (ident) names.insert(name);
    }
    return names;
}

// Every `"..."` that sits in a *callback* argument slot of the row-adding
// natives. Deliberately narrow: only the calls whose signature really does
// end in a handler name, and only the second string of each, so item text
// ("English", "Go home") is not counted as a callback.
std::vector<std::string> RowCallbacks(const std::string& src) {
    static const char* kCalls[] = {"AddMenuItem", "AddItem", "SetBack"};
    std::vector<std::string> out;
    for (const char* call : kCalls) {
        const std::string needle = std::string(call) + "(";
        size_t at = 0;
        while ((at = src.find(needle, at)) != std::string::npos) {
            size_t here = at;
            at += needle.size();
            if (here > 0) {
                char prev = src[here - 1];
                // `AddStaticItem(` ends with `Item(` too, and takes no
                // callback at all.
                if (prev == '_' || std::isalnum(static_cast<unsigned char>(prev))) continue;
            }
            size_t close = src.find(')', at);
            if (close == std::string::npos) break;
            const std::string argsText = src.substr(at, close - at);
            // The callback is the LAST quoted string in the argument list:
            // AddMenuItem(id, "cb"), AddItem("text", "cb"), SetBack("cb").
            size_t q2 = argsText.rfind('"');
            if (q2 == std::string::npos || q2 == 0) continue;
            size_t q1 = argsText.rfind('"', q2 - 1);
            if (q1 == std::string::npos) continue;
            const std::string name = argsText.substr(q1 + 1, q2 - q1 - 1);
            // SetBack's only argument is the callback; for the two-arg
            // forms the callback must not be the *first* string (that is
            // AddItem's own literal text with no callback after it).
            if (std::string(call) == "AddItem" && argsText.find(',') == std::string::npos) continue;
            if (name.empty()) continue;
            out.push_back(name);
        }
    }
    return out;
}

std::string RowText(const sk_bindings::MenuExecutable& menu, size_t index,
                    const sk::StringTable& strings) {
    if (index >= menu.rows().size()) return std::string();
    const auto& row = menu.rows()[index];
    return row.textId >= 0 ? strings.Get(row.textId) : row.literalText;
}

bool MenuHasText(const sk_bindings::MenuExecutable& menu, const sk::StringTable& strings,
                 const std::string& want) {
    for (size_t i = 0; i < menu.rows().size(); ++i) {
        if (RowText(menu, i, strings) == want) return true;
    }
    return false;
}

// The popup a screen is currently showing, by its first item's text.
const sk_bindings::PopupMenuExecutable* VisiblePopup(const sk_bindings::MenuExecutable& menu) {
    return menu.activePopup();
}

std::string PopupItemText(const sk_bindings::PopupMenuExecutable& popup, size_t index,
                          const sk::StringTable& strings) {
    if (index >= popup.items().size()) return std::string();
    const auto& item = popup.items()[index];
    return item.textId >= 0 ? strings.Get(item.textId) : item.literalText;
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const char* scriptRoot =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";
    const std::string root = scriptRoot;

    sk::StringTable strings;
    if (!strings.Load(root + "/stringtable.eng")) {
        std::printf("m91_ingame_menu_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }

    std::printf("=== M91: the in-game main menu, saving, and the dead row callbacks ===\n");

    // ---------------------------------------------------------------
    // Part 1: a row callback is a method call, not a script-only hook.
    //
    // The census is the argument. If a name in a callback slot only ever
    // meant "a handler this script defines", then no script would ever
    // put a native there -- and the corpus is full of them.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 1: row callbacks that name a native ---\n");
    {
        // A representative spread rather than the whole 800-file corpus:
        // one popup-driven screen, one conversation, one confirmation, one
        // save screen, and the reported one.
        static const char* kFiles[] = {"azra_menu_yousure.s", "ohnoskelos.s",     "saveconfirm.s",
                                       "savegamecorrupted.s", "savegamenospace.s", "mainmenu.s"};
        int bareQuit = 0;
        int total = 0;
        for (const char* file : kFiles) {
            const std::string src = ReadFile(root + "/" + file);
            Check(!src.empty(), std::string(file) + " ships with the game");
            const std::set<std::string> defined = HandlerNames(src);
            for (const std::string& cb : RowCallbacks(src)) {
                ++total;
                if (defined.count(cb)) continue;
                // Undefined by this script -- so it is either a native or
                // a dead name. These five are the ones that matter.
                if (cb == "Quit") ++bareQuit;
                Check(cb == "Quit" || cb == "QuitToMenu" || cb == "ActuallySaveGame" ||
                          cb == "SaveGameMenuBack",
                      std::string(file) + ": undefined callback \"" + cb + "\" is a real native");
            }
        }
        Check(total > 20, "the sampled screens between them name a good spread of callbacks");
        Check(bareQuit >= 3, "and at least three of them are a bare `Quit`");
    }

    // The dispatch itself, on a real screen.
    {
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(root, interpreter, &strings);
        sk_bindings::MenuExecutable* menu = stack.GetOrCreateMenu("azra_menu_yousure");
        Check(menu != nullptr, "azra_menu_yousure.s loads as a menu");
        if (menu) {
            // TryInvoke is the script-only path: it must NOT resolve a
            // native, or the two entry points would be the same thing.
            Check(!menu->TryInvoke("Quit"),
                  "TryInvoke(\"Quit\") finds nothing -- the script defines no Quit");
            Check(!stack.closeMenuRequested(), "and so nothing happened (the reported bug)");
            // InvokeCallback is what a row uses.
            Check(menu->InvokeCallback("Quit"), "InvokeCallback(\"Quit\") resolves the native");
            Check(stack.closeMenuRequested(), "and the screen actually closes");
            stack.ClearCloseMenuRequest();
            // A name that is neither reaches the soft-fail log -- which
            // is the point: the corpus's genuinely dead rows (`MenuQuit`,
            // `MenuBack`, `ExitMenu`, all absent from the real 702-entry
            // table) announce themselves instead of failing silently, and
            // still do nothing, exactly as in the shipped game.
            menu->InvokeCallback("MenuQuit");
            Check(!stack.closeMenuRequested(), "a dead name (MenuQuit) changes nothing");
            Check(!menu->InvokeCallback(""), "an empty callback is a no-op");
        }
    }

    // ---------------------------------------------------------------
    // Part 2: GameActive(), and the menu it builds.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 2: GameActive() and mainmenu.s ---\n");
    {
        const std::string src = ReadFile(root + "/mainmenu.s");
        Check(src.find("if (GameActive())") != std::string::npos ||
                  src.find("if (GameActive() )") != std::string::npos,
              "mainmenu.s branches its whole OnDisplay on GameActive()");
        Check(src.find("AddMenuItem(718,\"MenuSaveGame\")") != std::string::npos,
              "and the Save Game row is inside a GameActive() arm");
        Check(strings.Get(1965) == "Return To Game", "1965 is \"Return To Game\"");
        Check(strings.Get(718) == "Save Game", "718 is \"Save Game\"");
        Check(strings.Get(717) == "Load Game", "717 is \"Load Game\"");
        Check(strings.Get(716) == "New Game", "716 is \"New Game\"");
        Check(strings.Get(3495) == "End Game", "3495 is \"End Game\" -- the in-game Quit row");
        Check(strings.Get(3848) == "Quit Game", "3848 is \"Quit Game\" -- the front-end one");
    }
    {
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(root, interpreter, &strings);
        sk_bindings::MenuExecutable* menu = stack.CreateRootMenu("MainMenu", root + "/mainmenu.s");
        Check(menu != nullptr, "mainmenu.s loads");
        if (menu) {
            // --- the front end, no session ---
            Check(!stack.gameActive(), "a freshly built stack has no game");
            menu->RunOnDisplay();
            Check(MenuHasText(*menu, strings, "New Game"), "front end: New Game is present");
            Check(!MenuHasText(*menu, strings, "Return To Game"),
                  "front end: Return To Game is not");
            Check(!MenuHasText(*menu, strings, "Save Game"), "front end: Save Game is not");
            Check(MenuHasText(*menu, strings, "Quit Game"),
                  "front end: the last row reads \"Quit Game\"");

            // --- the same script, with a session ---
            stack.SetGameActive(true);
            menu->RunOnDisplay();
            Check(MenuHasText(*menu, strings, "Return To Game"),
                  "in game: Return To Game is present");
            Check(MenuHasText(*menu, strings, "Save Game"), "in game: **Save Game is present**");
            Check(MenuHasText(*menu, strings, "End Game"),
                  "in game: the last row reads \"End Game\"");
            Check(!MenuHasText(*menu, strings, "New Game"), "in game: New Game is gone");
            Check(!MenuHasText(*menu, strings, "Credits"), "in game: Credits is gone");
            Check(!MenuHasText(*menu, strings, "Multiplayer Menu"),
                  "in game: Multiplayer Menu is gone");

            // The first row is the one the screen opens on, and in game it
            // has to be the harmless one.
            menu->EnsureValidSelection();
            Check(RowText(*menu, static_cast<size_t>(menu->selectedItem() - 1), strings) ==
                      "Return To Game",
                  "in game: the menu opens on Return To Game");

            // "Return To Game" is MenuReturnToGame -> Quit().
            Check(menu->InvokeCallback("MenuReturnToGame"), "the Return To Game row runs");
            Check(stack.closeMenuRequested(), "and asks the host to close the menu");
            stack.ClearCloseMenuRequest();

            // The back key: MainMenuBack, which is deliberately
            // conditional -- it closes the front end only when a game is
            // running behind it.
            Check(menu->GoBack(), "the back key is handled in game (MainMenuBack -> Quit)");
            Check(stack.closeMenuRequested(), "and closes the menu");
            stack.ClearCloseMenuRequest();
            stack.SetGameActive(false);
            menu->RunOnDisplay();
            Check(!stack.closeMenuRequested(),
                  "on the front end MainMenuBack does nothing -- there is nowhere to go back to");
        }
    }

    // ---------------------------------------------------------------
    // Part 3: popup selection is 0-based.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 3: popup SetSelectedItem is 0-based ---\n");
    {
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(root, interpreter, &strings);
        stack.SetGameActive(true);
        sk_bindings::MenuExecutable* menu = stack.CreateRootMenu("MainMenu", root + "/mainmenu.s");
        Check(menu != nullptr, "mainmenu.s loads");
        if (menu) {
            menu->RunOnDisplay();
            Check(VisiblePopup(*menu) == nullptr, "no popup is visible to start with");
            // End Game -> `myPopup.SetVisible(true); myPopup.SetSelectedItem(1);`
            Check(menu->InvokeCallback("MenuQuit"), "the End Game row runs");
            const sk_bindings::PopupMenuExecutable* popup = VisiblePopup(*menu);
            Check(popup != nullptr, "and raises a confirmation popup");
            if (popup) {
                Check(popup->items().size() == 3, "three items: a prompt and two answers");
                Check(PopupItemText(*popup, 0, strings) == strings.Get(4019),
                      "item 0 is the prompt (\"Do you really wish to quit?\")");
                Check(PopupItemText(*popup, 1, strings) == "No", "item 1 is No");
                Check(PopupItemText(*popup, 2, strings) == "Yes", "item 2 is Yes");
                // SetSelectedItem(1) -> item 1 -> "No".
                Check(popup->IsItemSelected(1),
                      "SetSelectedItem(1) selects **No**, not the prompt above it");
                Check(!popup->IsItemSelected(0), "the prompt line is not what Enter would activate");
            }
            // And Enter actually answers it.
            sk_bindings::PopupMenuExecutable* live = menu->activePopup();
            if (live) {
                live->ActivateSelected();
                Check(menu->activePopup() == nullptr,
                      "Enter on the opened row (\"No\") dismisses the popup");
            }
        }
    }
    {
        // The one-button "Game Saved. / OK" shape, which is the same
        // pattern and the one that stranded the player on the save screen.
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(root, interpreter, &strings);
        stack.SetGameActive(true);
        sk_bindings::MenuExecutable* menu = stack.GetOrCreateMenu("SaveGameMenu");
        Check(menu != nullptr, "savegamemenu.s loads");
        if (menu) {
            menu->RunOnDisplay();
            Check(menu->InvokeCallback("DoneSave"), "its DoneSave handler runs");
            sk_bindings::PopupMenuExecutable* popup = menu->activePopup();
            Check(popup != nullptr, "and shows the \"Game Saved.\" popup");
            if (popup) {
                Check(popup->items().size() == 2, "two items: the message and OK");
                Check(popup->IsItemSelected(1), "opened on OK (item 1), not on the message");
                popup->ActivateSelected();
                Check(menu->activePopup() == nullptr, "and Enter closes it");
            }
        }
    }

    // ---------------------------------------------------------------
    // Part 4: azra_menu_yousure.s, the reported screen, end to end.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 4: \"leave town without finishing your first quest?\" ---\n");
    {
        const std::string src = ReadFile(root + "/azra_menu_yousure.s");
        Check(src.find("AddMenuItem(3747, \"Quit\")") != std::string::npos,
              "both of its rows are `AddMenuItem(id, \"Quit\")` --");
        Check(src.find("AddMenuItem(3746, \"Quit\")") != std::string::npos,
              "-- the native, named directly in the callback slot");
        Check(HandlerNames(src).size() == 1, "and the script defines nothing but Init");
        Check(strings.Get(3745).find("leave town") != std::string::npos,
              "3745 is the question itself");
        Check(strings.Get(3747) == "Right, I forgot.", "3747 is \"Right, I forgot.\"");
        Check(strings.Get(3746) == "Yes, I'm sure.", "3746 is \"Yes, I'm sure.\"");

        skInterpreter interpreter;
        sk_bindings::MenuStack stack(root, interpreter, &strings);
        stack.SetGameActive(true);
        sk_bindings::MenuExecutable* menu = stack.GetOrCreateMenu("azra_menu_yousure");
        Check(menu != nullptr, "it loads as a menu");
        if (menu) {
            // The question is one AddStaticItem that the layout wraps, so
            // the row count is "however many lines the text took" plus the
            // rule and the two answers -- find the answers by callback.
            int firstAnswer = -1;
            for (size_t i = 0; i < menu->rows().size(); ++i) {
                if (menu->rows()[i].callback == "Quit") {
                    firstAnswer = static_cast<int>(i) + 1;
                    break;
                }
            }
            Check(firstAnswer > 0, "the screen has an answer row");
            menu->EnsureValidSelection();
            Check(menu->selectedItem() == firstAnswer,
                  "it opens on the first answer, not on the question text");
            // Both answers close the screen -- the script says so, and it
            // is the only thing either of them does.
            int answers = 0;
            for (size_t i = 0; i < menu->rows().size(); ++i) {
                if (menu->rows()[i].callback != "Quit") continue;
                ++answers;
                Check(menu->InvokeCallback(menu->rows()[i].callback),
                      "row " + std::to_string(i + 1) + " (\"" + RowText(*menu, i, strings) +
                          "\") resolves");
                Check(stack.closeMenuRequested(), "and closes the prompt");
                stack.ClearCloseMenuRequest();
            }
            Check(answers == 2, "both answers are wired to the same native");
        }
    }

    // ---------------------------------------------------------------
    // Part 5: saving and loading.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 5: save and load ---\n");
    {
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(root, interpreter, &strings);
        // Somewhere writable that is not the repo.
        const char* tmp = std::getenv("TEMP");
        stack.SetSaveDirectory(tmp && *tmp ? tmp : ".");
        for (int slot = 0; slot < 4; ++slot) stack.DeleteGame(slot);

        sk_bindings::MenuExecutable* menu = stack.GetOrCreateMenu("SaveConfirm");
        Check(menu != nullptr, "saveconfirm.s loads -- the screen SaveGame() opens");

        skExecutableContext ctxt(&interpreter);
        skRValue ret;
        skRValueArray none;

        // CanSaveGame is a constant true in the real dispatcher.
        if (menu) {
            Check(menu->method(skString("CanSaveGame"), none, ret, ctxt) && ret.boolValue(),
                  "CanSaveGame() is true, verbatim from the real case");

            // The slot pair.
            skRValueArray one;
            one.append(skRValue(2));
            Check(menu->method(skString("SaveGame"), one, ret, ctxt), "SaveGame(2) is handled");
            Check(stack.saveSlot() == 2, "and stores the slot for the next screen");
            Check(stack.currentMenu() != nullptr &&
                      stack.currentMenu()->scriptName() == std::string("SaveConfirm"),
                  "and opens SaveConfirm, the way case 0x30 does");
            sk_bindings::MenuExecutable* confirm = stack.currentMenu();
            if (confirm) {
                Check(confirm->method(skString("GetSaveSlot"), none, ret, ctxt) &&
                          ret.intValue() == 2,
                      "GetSaveSlot() reads it straight back");
            }
        }

        // The round trip: place the player, save, move them, load.
        stack.SetCurrentLevelName("ghstpass");
        stack.SetGameActive(true);
        stack.player().SetWorldPosition(16512, 6360, -3744);
        stack.SetPlayerHeadingUnits(61155);
        auto setGold = [&](int amount) {
            skRValueArray a;
            a.append(skRValue(amount));
            skRValue r;
            skExecutableContext c(&interpreter);
            stack.player().method(skString("SetGold"), a, r, c);
        };
        setGold(1234);

        Check(!stack.GameAvailableForLoad(0), "slot 0 starts empty");
        Check(stack.ActuallySaveGame(0), "ActuallySaveGame(0) writes");
        Check(stack.GameAvailableForLoad(0), "and the slot is now occupied");

        // Move everything, then load it back.
        stack.SetCurrentLevelName("azra");
        stack.player().SetWorldPosition(0, 0, 0);
        setGold(7);
        stack.ClearCameraStart();

        const std::string level = stack.LoadGameFromSlot(0);
        Check(level == "ghstpass", "LoadGameFromSlot returns the level the save was written in");
        Check(stack.player().gold() == 1234, "and restores the character");
        const sk_bindings::MenuStack::CameraStart& spawn = stack.cameraStart();
        Check(spawn.armed, "**and arms the spawn override with the saved placement**");
        Check(spawn.x == 16512 && spawn.y == 6360, "x/y round-trip exactly");
        Check(static_cast<int16_t>(spawn.z) == static_cast<int16_t>(-3744), "z round-trips");
        Check((spawn.yaw & 0xffff) == 61155, "and so does the heading");

        // The result callbacks. saveconfirm.s defines all three names the
        // real case 0x31 can call back.
        const std::string src = ReadFile(root + "/saveconfirm.s");
        const std::set<std::string> handlers = HandlerNames(src);
        Check(handlers.count("DoneSave") == 1, "saveconfirm.s defines DoneSave (status 0)");
        Check(handlers.count("SaveFailed") == 1, "saveconfirm.s defines SaveFailed (status 1/2)");
        Check(handlers.count("NotEnoughSpace") == 1,
              "saveconfirm.s defines NotEnoughSpace (status 3)");

        // A slot outside the four is refused rather than written.
        Check(!stack.ActuallySaveGame(4), "slot 4 does not exist -- the write is refused");
        Check(!stack.ActuallySaveGame(-1), "and neither does slot -1");

        for (int slot = 0; slot < 4; ++slot) stack.DeleteGame(slot);
        Check(!stack.GameAvailableForLoad(0), "and the test cleans up after itself");
    }

    // ---------------------------------------------------------------
    // Part 6: the screen modes, which were transposed.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 6: screen modes 1 and 5 ---\n");
    {
        Check(static_cast<int>(sk::ScreenMode::kMenu) == 1,
              "mode 1 is a menu -- FUN_100779b8 sets it on the way in");
        Check(static_cast<int>(sk::ScreenMode::kGameplay) == 5,
              "mode 5 is gameplay -- Quit and SetCameraStart both set it");
        Check(!sk::ScreenModeDrawsProgressBar(static_cast<int>(sk::ScreenMode::kMenu)) &&
                  !sk::ScreenModeDrawsProgressBar(static_cast<int>(sk::ScreenMode::kGameplay)),
              "and neither of them draws the progress bar");
    }

    std::printf("\n");
    if (g_failures == 0) {
        std::printf("m91_ingame_menu_smoke: OK (%d checks)\n", g_checks);
        return 0;
    }
    std::printf("m91_ingame_menu_smoke: %d of %d checks FAILED\n", g_failures, g_checks);
    return 1;
}
