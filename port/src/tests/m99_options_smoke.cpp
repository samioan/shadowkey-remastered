// M99 smoke test: the Options screen's rows land on something.
//
// Four GameEngine natives the gaps tool listed as called and missing, and the
// machinery behind them:
//
//   SetLanguage (0x72)        options.s's language popup -- reload the string
//                             table (StringTable.eng/spa/ger/fre/ita/euk)
//   ConfigKeysMenu (0x12)     configkeys.s's pages, built natively, with the
//   ConfigKeySelected (0x13)  two row callbacks the builders wire in and the
//   Redefine (0x14)           menu tick's key-capture mode
//   ConfigKeysDefault (0x11)  the startup bindings, again
//   SaveConfig (0x71)         dragonstar.set, also written by Quit/QuitGame
//
// What this checks:
//
//   1. The language table and GetLanguageStr ("Language: English", which
//      means options.s's God Mode row never shows), SetLanguage's reload and
//      its refusal to lose the old table, and Latin-1 text.
//   2. configkeys.s end to end: the four pages, an action's screen, a key
//      captured and swapped, a cancelled capture, reset to defaults.
//   3. The settings file: SaveConfig, Quit, QuitGame and its failure screen,
//      and DeleteAllGames switching saving off.
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "assets/game_config.h"
#include "assets/gdr_font.h"
#include "assets/string_table.h"
#include "engine/input_state.h"
#include "simkin_bindings/language.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"

namespace sk_b = sk_bindings;
using Menu = sk_b::MenuExecutable;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("  %-92s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    if (!ok) ++g_failures;
}

std::string RowText(const Menu::MenuRow& row, const sk::StringTable& strings) {
    if (!row.literalText.empty()) return row.literalText;
    return row.textId >= 0 ? strings.Get(row.textId) : std::string();
}

std::vector<std::string> RowTexts(const Menu& menu, const sk::StringTable& strings) {
    std::vector<std::string> out;
    for (const Menu::MenuRow& row : menu.rows()) out.push_back(RowText(row, strings));
    return out;
}

std::string Join(const std::vector<std::string>& v) {
    std::string s;
    for (const std::string& t : v) s += "[" + t + "]";
    return s;
}

bool FileExists(const std::string& path) { return std::ifstream(path).good(); }

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string root =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    if (!strings.Load(root + "/stringtable.eng")) {
        std::printf("m99_options_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }
    skInterpreter interpreter;
    sk_b::MenuStack stack(root, interpreter, &strings);
    sk::InputState input;
    stack.SetInput(&input);

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
    auto none = []() { return skRValueArray(); };
    auto one = [](skRValue v) {
        skRValueArray a;
        a.append(v);
        return a;
    };

    // ---------------------------------------------------------------
    // Part 1: languages.
    // ---------------------------------------------------------------
    std::printf("\n== Part 1: SetLanguage and GetLanguageStr ==\n");
    {
        Check(std::string(sk_b::LanguageFileSuffix(0)) == "eng" &&
                  std::string(sk_b::LanguageFileSuffix(1)) == "spa" &&
                  std::string(sk_b::LanguageFileSuffix(2)) == "ger" &&
                  std::string(sk_b::LanguageFileSuffix(3)) == "fre" &&
                  std::string(sk_b::LanguageFileSuffix(4)) == "ita" &&
                  std::string(sk_b::LanguageFileSuffix(5)) == "euk" &&
                  std::string(sk_b::LanguageFileSuffix(6)) == "eng",
              "FUN_1001b708's suffixes, in options.s's popup order; out of range is eng");

        stack.OpenMenu("Options");
        Menu* options = stack.currentMenu();
        Check(options != nullptr, "options.s opens");
        if (options) {
            const std::string str = call(options, "GetLanguageStr", none()).str().ptr();
            Check(str == "Language: English",
                  "GetLanguageStr() is \"Language: English\" (was \"ENGLISH\")");
            bool godRow = false;
            for (const Menu::MenuRow& row : options->rows()) {
                if (row.callback == "GodModeON" || row.callback == "GodModeOFF") godRow = true;
            }
            Check(!godRow, "...so options.s's `langStr = \"ENGLISH\"` God Mode row is never built");

            // The popup's own handler, as the row a player picks runs it.
            options->InvokeCallback("SetGerman");
            Check(stack.language() == 2 && strings.Get(4032) == "N\xE4" "chste Seite",
                  "SetGerman -> SetLanguage(2): the table is German, umlaut intact (Latin-1)");
            const std::string german = call(options, "GetLanguageStr", none()).str().ptr();
            Check(german == "Sprache: Deutsch", "GetLanguageStr() then reads \"Sprache: Deutsch\"");
            bool relabelled = false;
            for (const Menu::MenuRow& row : stack.currentMenu()->rows()) {
                if (row.callback == "CustControlsMenu" && RowText(row, strings) == "Steuerung anpassen") {
                    relabelled = true;
                }
            }
            Check(relabelled, "options.s's ClearMenu(); Init(); redraws its rows from the new table");

            options->InvokeCallback("SetFrench");
            Check(sk_b::LanguageDisplayString(stack.language()) == "Langue: Fran\xE7" "ais" &&
                      strings.Get(4032) == "Page suivante",
                  "SetFrench: \"Langue: Fran\\xE7ais\", and French text");
            call(options, "SetLanguage", one(skRValue(9)));
            Check(stack.language() == 9 && strings.Get(4032) == "Next Page" &&
                      call(options, "GetLanguageStr", none()).str() == skString("English"),
                  "SetLanguage(9): no such language -- eng loads, and the prefix switch has no arm");
            call(options, "SetLanguage", none());
            Check(stack.language() == 0, "SetLanguage() with no argument is SetLanguage(0)");
        }

        // A table that is not there changes nothing, not even the index.
        sk::StringTable spare;
        spare.Load(root + "/stringtable.eng");
        sk_b::MenuStack orphan("no/such/root", interpreter, &spare);
        const bool loaded = orphan.SetLanguage(3);
        Check(!loaded && orphan.language() == 0 && spare.Get(4032) == "Next Page",
              "a missing StringTable.fre: language stays 0 and the English table survives");

        const std::string fontPath = "port/assets/fonts/Ceurope.gdr";
        if (FileExists(fontPath)) {
            sk::GdrFont font;
            const bool fontOk = font.Load(fontPath, "LatinBold12");
            Check(fontOk && font.GetGlyph(0xE4) && font.GetGlyph(0xE7) && font.GetGlyph(0xF1) &&
                      font.GetGlyph(0xFC),
                  "the menu font (LatinBold12) has glyphs for \\xE4 \\xE7 \\xF1 \\xFC");
        } else {
            std::printf("  (Ceurope.gdr not present -- font glyph check skipped)\n");
        }
    }

    // ---------------------------------------------------------------
    // Part 2: configkeys.s.
    // ---------------------------------------------------------------
    std::printf("\n== Part 2: the key-configuration screen ==\n");
    {
        stack.OpenMenu("ConfigKeys");
        Menu* keys = stack.currentMenu();
        Check(keys != nullptr, "configkeys.s opens");
        if (keys) {
            const std::vector<std::string> page1 = RowTexts(*keys, strings);
            std::printf("     page 1: %s\n", Join(page1).c_str());
            Check(page1 == std::vector<std::string>{"Move Forward", "Move Backward", "Turn Left",
                                                    "Turn Right", "Look Up", " ", "Next Page", " ",
                                                    "Reset to Defaults", "Back to Options"},
                  "OnDisplay's ConfigKeysMenu(1): actions 0-4, Next Page, no Previous Page");
            Check(keys->configKeysPage() == 1 && keys->selectedItem() == 1 &&
                      keys->rows()[0].callback == "ConfigKeySelected",
                  "each action row calls ConfigKeySelected; the highlight starts on the first");

            keys->InvokeCallback("NextPage");
            const std::vector<std::string> page2 = RowTexts(*keys, strings);
            Check(page2 == std::vector<std::string>{"Look Down", "Side Step Left", "Side Step Right",
                                                    "Jump", "Map Toggle", " ", "Next Page",
                                                    "Previous Page", "Reset to Defaults",
                                                    "Back to Options"},
                  "page 2 is actions 5-9 in the engine's own order (Look Down is 5, Jump 8)");
            keys->InvokeCallback("NextPage");
            keys->InvokeCallback("NextPage");
            const std::vector<std::string> page4 = RowTexts(*keys, strings);
            std::printf("     page 4: %s\n", Join(page4).c_str());
            Check(page4 == std::vector<std::string>{"Use Right Action", " ", " ", "Previous Page",
                                                    "Reset to Defaults", "Back to Options"},
                  "page 4 is action 15 alone, and has no Next Page (`if (last < 0xf)`)");

            // Back to page 2, highlight Side Step Right (action 7), select.
            keys->InvokeCallback("PrevPage");
            keys->InvokeCallback("PrevPage");
            call(keys, "SetSelectedItem", one(skRValue(3)));
            keys->ActivateSelected();
            const std::vector<std::string> detail = RowTexts(*keys, strings);
            std::printf("     action: %s\n", Join(detail).c_str());
            Check(keys->configKeysAction() == 7 &&
                      detail == std::vector<std::string>{"Side Step Right", " ",
                                                         "Current Definition:", "Key 6", " ", " ",
                                                         "Redefine", " ", "Back to Key Config"},
                  "ConfigKeySelected on page 2's third row: action 7, currently Key 6");

            keys->InvokeCallback("Redefine");
            const std::vector<std::string> redefine = RowTexts(*keys, strings);
            Check(keys->awaitingKey() &&
                      redefine == std::vector<std::string>{": Redefining :", "Side Step Right", " ",
                                                           " ", "Press a key to use",
                                                           "for this action.", " "},
                  "Redefine: the press-a-key screen, and the menu waits for a key");

            // The select key is still down from choosing Redefine.
            input.SetButton(sk::ButtonSlot::Key5, true);
            keys->TickKeyCapture(input);
            keys->TickKeyCapture(input);
            Check(keys->awaitingKey() && input.binding(sk::Action::SideStepRight) ==
                                             static_cast<int>(sk::ButtonSlot::Key6),
                  "while Key 5 is held nothing is taken -- not even Key 5 itself");
            input.SetButton(sk::ButtonSlot::Key5, false);
            keys->TickKeyCapture(input);
            input.SetButton(sk::ButtonSlot::LeftSelectionKey, true);
            keys->TickKeyCapture(input);
            Check(keys->awaitingKey(), "a held selection key is not bindable (its registration flag is 0)");
            input.SetButton(sk::ButtonSlot::LeftSelectionKey, false);
            input.SetButton(sk::ButtonSlot::Key3, true);
            keys->TickKeyCapture(input);
            input.SetButton(sk::ButtonSlot::Key3, false);
            Check(!keys->awaitingKey() &&
                      input.binding(sk::Action::SideStepRight) ==
                          static_cast<int>(sk::ButtonSlot::Key3) &&
                      input.binding(sk::Action::Use) == static_cast<int>(sk::ButtonSlot::Key6),
                  "Key 3 is taken: Side Step Right moves to it and Use, which had it, gets Key 6");
            Check(keys->configKeysPage() == 2 && RowTexts(*keys, strings) == page2,
                  "...and the script's BackToConfig puts page 2 back up");
            Check(!input.ConsumeJustPressed(sk::ButtonSlot::Key3),
                  "the captured press leaves no edge behind to move the new page's highlight");

            // A cancelled capture.
            call(keys, "SetSelectedItem", one(skRValue(1)));
            keys->ActivateSelected();  // Look Down, action 5
            keys->InvokeCallback("Redefine");
            keys->TickKeyCapture(input);  // Key 5 already up
            input.SetButton(sk::ButtonSlot::RightSelectionKey, true);
            keys->TickKeyCapture(input);
            input.SetButton(sk::ButtonSlot::RightSelectionKey, false);
            Check(!keys->awaitingKey() &&
                      input.binding(sk::Action::LookDown) == static_cast<int>(sk::ButtonSlot::Key8) &&
                      keys->configKeysPage() == 1,
                  "the right softkey cancels: no change, and ConfigKeysBack -> PrevPage lands on page 1");

            call(keys, "ConfigKeysDefault", none());
            const sk::InputState defaults;
            bool allDefault = true;
            for (int a = 0; a < 16; ++a) {
                if (input.binding(static_cast<sk::Action>(a)) !=
                    defaults.binding(static_cast<sk::Action>(a))) {
                    allDefault = false;
                }
            }
            Check(allDefault, "ConfigKeysDefault puts all sixteen back (Side Step Right is Key 6 again)");

            // Rebind once more for part 3 to find in the file.
            input.RedefineBinding(sk::Action::Jump, sk::ButtonSlot::KeyHash);
            Check(input.binding(sk::Action::Jump) == static_cast<int>(sk::ButtonSlot::KeyHash) &&
                      input.binding(sk::Action::CharacterManager) ==
                          static_cast<int>(sk::ButtonSlot::Key1),
                  "RedefineBinding swaps: Jump takes #, Character Manager takes Jump's Key 1");
        }
    }

    // ---------------------------------------------------------------
    // Part 3: dragonstar.set.
    // ---------------------------------------------------------------
    std::printf("\n== Part 3: SaveConfig, Quit, QuitGame, DeleteAllGames ==\n");
    {
        Check(stack.muteOnCall(), "mute-on-call starts true (the constructor's `+0x14a79 = 1`)");
        Menu* menu = stack.currentMenu();
        const std::string path = "m99_dragonstar_test.set";
        std::remove(path.c_str());
        if (menu) {
            stack.SetLanguage(4);
            stack.SetConfigPath(path);
            const bool saved = call(menu, "SaveConfig", none()).boolValue();
            sk::GameConfig back;
            const bool parsed = back.Load(path);
            Check(saved && parsed && back.language == 4 && back.muteOnCall &&
                      back.actionMap[8] == static_cast<int>(sk::ButtonSlot::KeyHash) &&
                      back.actionMap[12] == static_cast<int>(sk::ButtonSlot::Key1),
                  "SaveConfig() writes the rebinding, LANGUAGE 4 and MUTEONCALL 1");
            std::remove(path.c_str());

            call(menu, "Quit", none());
            Check(FileExists(path), "Quit() writes the file too (case 0x32's first line)");
            stack.ClearCloseMenuRequest();

            call(menu, "QuitGame", none());
            Check(stack.quitRequested(), "QuitGame(): saved, so the game quits");

            sk_b::MenuStack failing(root, interpreter, &strings);
            failing.SetConfigPath("no/such/directory/dragonstar.set");
            failing.OpenMenu("Options");
            if (Menu* m = failing.currentMenu()) {
                call(m, "QuitGame", none());
                Check(!failing.quitRequested() && failing.currentMenu() &&
                          failing.currentMenu()->scriptName() == "SaveConfigFailed",
                      "a save that fails stops the quit and opens SaveConfigFailed");
                Menu* retry = failing.currentMenu();
                retry->InvokeCallback("LocalQuitGame");
                Check(failing.quitRequested(),
                      "saveconfigfailed.s's other row, QuitGame(false), quits without saving");
            }

            call(menu, "DeleteAllGames", none());
            Check(!FileExists(path) && stack.configSaveDisabled(),
                  "DeleteAllGames unlinks dragonstar.set and switches saving off");
            const bool reported = call(menu, "SaveConfig", none()).boolValue();
            Check(reported && !FileExists(path),
                  "SaveConfig() then reports success and writes nothing (`+0x14a78` jumps to return 1)");
            std::remove(path.c_str());
        }
    }

    std::printf("\nm99_options_smoke: %d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
