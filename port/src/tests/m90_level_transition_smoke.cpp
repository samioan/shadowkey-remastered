// M90 smoke test: the travel prompt, and the bare `LoadLevel` that never
// resolved.
//
// Reported from play: walking into a transition region either teleported
// the player with no warning (azra -> ghstpass) or did nothing at all, and
// the log said `ZoneScript: LoadLevel(broken1) -- not implemented`. Two
// symptoms, two separate defects, one screen missing between them.
//
//   Part 1  `levelconfirm.s` itself -- the shipped screen this port never
//           opened, and the five stringtable ids it is made of.
//   Part 2  The corpus census: which spelling of the transition call each
//           script uses, and the 41 `<ScriptName>Back` handlers that turn
//           out to be the engine's real back-key dispatch.
//   Part 3  The three transition bindings in isolation -- 0x17 prompts,
//           0x16/0x18 load, 0x15 restores.
//   Part 4  End to end through two real shipped scripts, one of each
//           spelling: `ghstpass.s`'s bare `LoadLevel("broken1")` and
//           `azra.s`'s qualified `Level.LoadLevel("GhstPass")`, each
//           driven into the real `levelconfirm.s` and out again through
//           its own Go / Don't Go / back-key rows.
//   Part 5  The rest of the bare Level vocabulary a zone root script uses
//           and this port used to soft-fail: UnlockZone, LockZone,
//           PlayAmbient.
//   Part 6  `GetNextLevelName()` -- the prompt's second row.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "assets/zone_display_names.h"
#include "engine/screen_mode.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/zone_script_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
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

// Split a script into its top-level handler names -- `Name[ (args)` at the
// start of a line, which is how every shipped `.s` declares one.
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

// Count `LoadLevel(` occurrences, split by whether a `Level.` immediately
// precedes them.
struct CallCensus {
    int bare = 0;
    int qualified = 0;
};

CallCensus CountCalls(const std::string& src, const std::string& method) {
    CallCensus out;
    const std::string needle = method + "(";
    size_t at = 0;
    while ((at = src.find(needle, at)) != std::string::npos) {
        size_t here = at;
        at += needle.size();
        // A longer identifier that merely ends with this one --
        // `ActuallyLoadLevel(` / `ForceLoadLevel(` when the needle is
        // `LoadLevel(`. Neither is this binding.
        if (here > 0) {
            char prev = src[here - 1];
            if (prev == '_' || std::isalnum(static_cast<unsigned char>(prev))) continue;
            if (prev == '.') {
                if (here >= 6 && src.compare(here - 6, 6, "Level.") == 0) ++out.qualified;
                // Any other object prefix is a different binding.
                continue;
            }
        }
        ++out.bare;
    }
    return out;
}

struct RecordingZoneRegions : sk_bindings::LevelExecutable::ZoneRegions {
    std::vector<std::string> locked;
    std::vector<std::string> unlocked;
    bool HasRegion(const std::string&) const override { return true; }
    int LockRegion(const std::string& name) override {
        locked.push_back(name);
        return 1;
    }
    int UnlockRegion(const std::string& name) override {
        unlocked.push_back(name);
        return 1;
    }
    void LightRect(int, int, int, int, int) override {}
};

// Loads one zone root script and attaches it to the Level object exactly
// as main.cpp's zone-load block does, so both spellings of a native call
// route the way they do in the running game.
std::unique_ptr<sk_bindings::ZoneScriptExecutable> LoadZoneScript(
    const std::string& root, const std::string& zoneName, skInterpreter& interpreter,
    sk_bindings::MenuStack& stack) {
    skExecutableContext loadCtxt(&interpreter);
    std::unique_ptr<sk_bindings::ZoneScriptExecutable> script;
    try {
        script = std::make_unique<sk_bindings::ZoneScriptExecutable>(
            skString((root + "/" + zoneName + ".s").c_str()), loadCtxt, stack);
    } catch (skParseException& e) {
        std::printf("   (PARSE ERROR loading %s.s: %s)\n", zoneName.c_str(), e.toString().ptr());
        return nullptr;
    } catch (skRuntimeException& e) {
        std::printf("   (RUNTIME ERROR loading %s.s: %s)\n", zoneName.c_str(), e.toString().ptr());
        return nullptr;
    }
    stack.level().AttachZoneScript(script.get());
    stack.SetCurrentLevelName(zoneName);
    return script;
}

void CallHandler(sk_bindings::ZoneScriptExecutable& script, skInterpreter& interpreter,
                 const std::string& handler, const std::string& arg) {
    skRValueArray args;
    args.append(skRValue(skString(arg.c_str())));
    skRValue ret;
    skExecutableContext ctxt(&interpreter);
    try {
        script.method(skString(handler.c_str()), args, ret, ctxt);
    } catch (skRuntimeException& e) {
        std::printf("   (RUNTIME ERROR in %s(\"%s\"): %s)\n", handler.c_str(), arg.c_str(),
                    e.toString().ptr());
    }
}

// The prompt's own two rows, by callback name -- 1-based, the way
// `selectedItem()` counts them.
int RowIndexOf(const sk_bindings::MenuExecutable& menu, const std::string& callback) {
    for (size_t i = 0; i < menu.rows().size(); ++i) {
        if (menu.rows()[i].callback == callback) return static_cast<int>(i) + 1;
    }
    return -1;
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
        std::printf("m90_level_transition_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }

    std::printf("=== M90: the travel prompt, and the bare LoadLevel ===\n");

    // ---------------------------------------------------------------
    // Part 1: levelconfirm.s, the shipped screen.
    //
    // Its Init is four rows and two softkey labels, and every one of them
    // is a stringtable id this port can resolve. If the ids drift, the
    // prompt stops reading as a sentence.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 1: levelconfirm.s ---\n");
    {
        const std::string src = ReadFile(root + "/levelconfirm.s");
        Check(!src.empty(), "levelconfirm.s ships with the game");

        std::set<std::string> handlers = HandlerNames(src);
        Check(handlers.count("Init") == 1, "levelconfirm.s defines Init");
        Check(handlers.count("Go") == 1, "levelconfirm.s defines Go");
        Check(handlers.count("DontGo") == 1, "levelconfirm.s defines DontGo");
        Check(handlers.count("LevelConfirmBack") == 1,
              "levelconfirm.s defines LevelConfirmBack -- the back-key handler");
        Check(handlers.size() == 4, "and nothing else -- four handlers, no more");

        Check(src.find("Level.ActuallyLoadLevel( Level.GetNextLevel(), Level.GetNextLevelX(), "
                       "Level.GetNextLevelY() )") != std::string::npos,
              "Go reads back exactly the three fields LoadLevel wrote");
        Check(src.find("Level.RestoreSaveLevel()") != std::string::npos,
              "Don't Go calls Level.RestoreSaveLevel()");
        Check(src.find("AddStaticItem(GetNextLevelName(),false)") != std::string::npos,
              "the destination row is GetNextLevelName(), not a stringtable id");

        // The five ids, resolved against a real stringtable.eng. Together
        // they are the whole screen: "Travel to: " / "<zone>" / "No" /
        // "Yes", with "Accept" and "Back" on the softkey line.
        Check(strings.Get(3950) == "Travel to: ", "3950 is \"Travel to: \" -- the prompt's header");
        Check(strings.Get(1389) == "No", "1389 is \"No\" -- AddMenuItem(1389, \"DontGo\")");
        Check(strings.Get(1375) == "Yes", "1375 is \"Yes\" -- AddMenuItem(1375, \"Go\")");
        Check(strings.Get(4077) == "Accept", "4077 is the left softkey label, \"Accept\"");
        Check(strings.Get(4078) == "Back", "4078 is the right softkey label, \"Back\"");

        // Order matters: "No" is added first, so the prompt opens with the
        // safe answer highlighted.
        size_t noAt = src.find("AddMenuItem(1389");
        size_t yesAt = src.find("AddMenuItem(1375");
        Check(noAt != std::string::npos && yesAt != std::string::npos && noAt < yesAt,
              "\"No\" is the first row -- the prompt opens on the safe answer");
    }

    // ---------------------------------------------------------------
    // Part 2: the corpus.
    //
    // Two censuses, both of them the evidence for a fix.
    //
    // (a) The transition call is spelled both ways, in the same handler
    //     kind, in files that sit on opposite sides of the same doorway.
    //     `azra.s` says `Level.LoadLevel("GhstPass")`; `ghstpass.s` says
    //     `LoadLevel("azra")`. Only one of those two used to work.
    // (b) 41 scripts define a `<ScriptName>Back` handler and no shipped
    //     script ever calls one. They are the engine's own right-softkey
    //     dispatch (`"%sBack"` at 0x100b32c8, sprintf'd from the screen's
    //     name at `menu+0xc` by `FUN_100768b4`), and this port had no
    //     caller for any of them.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 2: the shipped corpus ---\n");
    {
        static const char* kZoneScripts[] = {
            "azra",     "broken1",      "broken2", "crypt1",  "crypt2",   "crypt3",  "delfhide",
            "drgnfld",  "dstar_e",      "dstar_w", "erthcave", "fearfrst", "ffarena", "ghstpass",
            "glaciercrawl", "lakvan",   "lothcav", "raiders", "snowline", "stouttp", "twilite",
        };
        int bare = 0;
        int qualified = 0;
        int filesRead = 0;
        for (const char* zone : kZoneScripts) {
            const std::string src = ReadFile(root + "/" + zone + ".s");
            if (src.empty()) continue;
            ++filesRead;
            CallCensus c = CountCalls(src, "LoadLevel");
            bare += c.bare;
            qualified += c.qualified;
        }
        std::printf("   zone root scripts read: %d, LoadLevel bare: %d, Level.-qualified: %d\n",
                    filesRead, bare, qualified);
        Check(filesRead == 21, "all 21 zone root scripts read");
        Check(bare > 0 && qualified > 0,
              "both spellings are shipped -- the bare one is not a typo, it is a majority");
        Check(bare > qualified,
              "and the bare spelling is the more common of the two in zone root scripts");

        // The specific pair either side of one doorway.
        const std::string ghstpass = ReadFile(root + "/ghstpass.s");
        const std::string azra = ReadFile(root + "/azra.s");
        Check(ghstpass.find("\t\t\tLoadLevel(\"broken1\");") != std::string::npos,
              "ghstpass.s's EnterZone(\"BrokenWing\") is bare LoadLevel(\"broken1\")");
        Check(azra.find("Level.LoadLevel(\"GhstPass\");") != std::string::npos,
              "azra.s's EnterZone is Level.LoadLevel(\"GhstPass\") -- same binding, other spelling");

        // (b) The back handlers. One per screen, named after the screen.
        static const char* kBackHandlers[][2] = {
            {"levelconfirm", "LevelConfirmBack"},
            {"lootmenu", "LootMenuBack"},
            {"inventory", "InventoryBack"},
            {"statsscreen", "StatsScreenBack"},
            {"questlog", "QuestLogBack"},
            {"levelup", "LevelUpBack"},
            {"charactermanager", "CharacterManagerBack"},
            {"actionqueue", "ActionQueueBack"},
            {"options", "OptionsBack"},
            {"mainmenu", "MainMenuBack"},
            {"buysell", "BuySellBack"},
            {"configkeys", "ConfigKeysBack"},
            {"deathmenu", "DeathMenuBack"},
            {"menus/lockpickdoor", "LockPickDoorBack"},
            {"menus/doorlockedmenu", "DoorLockedMenuBack"},
            {"menus/dropgoldmenu", "DropGoldMenuBack"},
            {"menus/newgamemenu", "NewGameMenuBack"},
            {"menus/skeleton_key_menu", "skeleton_key_menuBack"},
            {"dstar_w/armor_convo", "armor_convoBack"},
        };
        int matched = 0;
        int calledByAnyone = 0;
        for (const auto& row : kBackHandlers) {
            const std::string src = ReadFile(root + "/" + row[0] + ".s");
            if (src.empty()) continue;
            if (HandlerNames(src).count(row[1]) == 1) ++matched;
            // A `<Name>Back()` call inside the same file would mean the
            // scripts drive these themselves. None of them do.
            if (src.find(std::string(row[1]) + "()") != std::string::npos) ++calledByAnyone;
        }
        std::printf("   back handlers found: %d of %d sampled\n", matched,
                    static_cast<int>(sizeof(kBackHandlers) / sizeof(kBackHandlers[0])));
        Check(matched == static_cast<int>(sizeof(kBackHandlers) / sizeof(kBackHandlers[0])),
              "every sampled screen names its back handler after its own open path, case and all");
        Check(calledByAnyone == 0,
              "and no script calls one -- they are the engine's right-softkey dispatch, nothing else");
    }

    // ---------------------------------------------------------------
    // Part 3: the three bindings, in isolation.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 3: bindings 0x15 / 0x16 / 0x17 / 0x18 ---\n");
    {
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(root, interpreter, &strings);
        stack.SetCurrentLevelName("ghstpass");
        skExecutableContext ctxt(&interpreter);
        skRValue ret;

        // 0x17 -- prompt, do not load.
        skRValueArray args;
        args.append(skRValue(skString("broken1")));
        args.append(skRValue(4096));
        args.append(skRValue(8192));
        bool handled = stack.level().method(skString("LoadLevel"), args, ret, ctxt);
        Check(handled, "Level.LoadLevel(name, x, y) is handled");
        Check(!stack.gameStartRequested(), "LoadLevel loaded nothing");
        Check(stack.currentMenu() != nullptr &&
                  stack.currentMenu()->scriptName() == sk::kLevelConfirmMenuName,
              "LoadLevel raised the LevelConfirm screen");
        Check(stack.currentLevelName() == "broken1",
              "the destination is in the current-name slot (app+0x28)");

        skRValueArray none;
        stack.level().method(skString("GetNextLevel"), none, ret, ctxt);
        Check(std::string(ret.str().ptr()) == "broken1", "GetNextLevel() reads it back");
        stack.level().method(skString("GetNextLevelX"), none, ret, ctxt);
        Check(ret.intValue() == 4096, "GetNextLevelX() is argument 2");
        stack.level().method(skString("GetNextLevelY"), none, ret, ctxt);
        Check(ret.intValue() == 8192, "GetNextLevelY() is argument 3");

        // 0x15 -- the decline.
        stack.ArmCameraStart(1, 2, 3, 0, 4, 0);
        stack.level().method(skString("RestoreSaveLevel"), none, ret, ctxt);
        Check(stack.currentLevelName() == "ghstpass",
              "RestoreSaveLevel put the saved level name back");
        Check(!stack.cameraStart().armed,
              "and disarmed the spawn override the script armed for a trip that did not happen");

        // 0x18 -- the accept. It must not push the (already destination)
        // current name over the saved one; the saved one is where the
        // player came from and a later decline still needs it.
        stack.level().method(skString("LoadLevel"), args, ret, ctxt);
        skRValueArray goArgs;
        goArgs.append(skRValue(skString("broken1")));
        goArgs.append(skRValue(4096));
        goArgs.append(skRValue(8192));
        stack.level().method(skString("ActuallyLoadLevel"), goArgs, ret, ctxt);
        Check(stack.gameStartRequested() && stack.requestedZone() == "broken1",
              "ActuallyLoadLevel asked for the load");
        stack.ClearGameStartRequest();
        stack.level().method(skString("RestoreSaveLevel"), none, ret, ctxt);
        Check(stack.currentLevelName() == "ghstpass",
              "ActuallyLoadLevel left the saved name alone -- a decline after it still works");

        // 0x16 -- ForceLoadLevel, the unconfirmed trip, which does take on
        // the save-level push itself.
        stack.SetCurrentLevelName("ghstpass");
        skRValueArray forceArgs;
        forceArgs.append(skRValue(skString("snowline")));
        stack.level().method(skString("ForceLoadLevel"), forceArgs, ret, ctxt);
        Check(stack.gameStartRequested() && stack.requestedZone() == "snowline",
              "ForceLoadLevel asked for the load without a prompt");
        stack.ClearGameStartRequest();
        stack.level().method(skString("RestoreSaveLevel"), none, ret, ctxt);
        Check(stack.currentLevelName() == "ghstpass",
              "and pushed the save level itself, so it is still recoverable");
    }

    // ---------------------------------------------------------------
    // Part 4: end to end, through two real shipped scripts.
    //
    // This is the reported bug, both halves of it. `ghstpass.s`'s
    // "BrokenWing" region is the bare spelling that used to soft-fail;
    // `azra.s`'s "GhastsPass" region is the qualified one that used to
    // teleport with no prompt.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 4: ghstpass.s -> broken1, the whole conversation ---\n");
    {
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(root, interpreter, &strings);
        auto script = LoadZoneScript(root, "ghstpass", interpreter, stack);
        Check(script != nullptr, "ghstpass.s loads");
        if (script) {
            CallHandler(*script, interpreter, "EnterZone", "BrokenWing");

            sk_bindings::MenuExecutable* prompt = stack.currentMenu();
            Check(prompt != nullptr && prompt->scriptName() == sk::kLevelConfirmMenuName,
                  "bare LoadLevel(\"broken1\") raised the prompt (it used to soft-fail)");
            Check(!stack.gameStartRequested(), "and nothing loaded behind it");
            Check(stack.cameraStart().armed && stack.cameraStart().x == 16768,
                  "the SetCameraStart on the next line armed the spawn override");

            if (prompt) {
                // The screen the player is looking at: "Travel to: " /
                // "Broken Wing I" / "No" / "Yes".
                std::vector<std::string> text;
                for (const auto& row : prompt->rows()) {
                    text.push_back(row.textId >= 0 ? strings.Get(row.textId) : row.literalText);
                }
                Check(text.size() == 4, "the prompt is four rows");
                if (text.size() == 4) {
                    Check(text[0] == "Travel to: ", "row 1 is \"Travel to: \"");
                    Check(text[1] == "Broken Wing I",
                          "row 2 is the destination's real display name, \"Broken Wing I\"");
                    Check(text[2] == "No" && text[3] == "Yes", "rows 3 and 4 are No / Yes");
                }
                Check(RowIndexOf(*prompt, "DontGo") == 3 && RowIndexOf(*prompt, "Go") == 4,
                      "and they carry the DontGo / Go callbacks");
                // And the prompt opens *on* "No" -- the row order is the
                // script's answer to "what happens if the player just
                // presses the confirm key", and the answer has to be
                // "nothing".
                prompt->EnsureValidSelection();
                std::printf("   selected row: %d of %zu\n", prompt->selectedItem(),
                            prompt->rows().size());
                Check(prompt->selectedItem() == RowIndexOf(*prompt, "DontGo"),
                      "the prompt opens with \"No\" selected, not \"Yes\"");

                // --- Don't Go ---
                Check(prompt->TryInvoke("DontGo"), "the \"No\" row's callback runs");
                Check(!stack.gameStartRequested(), "\"No\" did not load anything");
                Check(stack.currentLevelName() == "ghstpass",
                      "\"No\" put the level name back to ghstpass");
                Check(!stack.cameraStart().armed, "\"No\" disarmed the spawn override");
                Check(stack.closeMenuRequested(), "\"No\" closed the prompt");
                stack.ClearCloseMenuRequest();
            }

            // --- the back key, which has to do exactly what "No" does ---
            CallHandler(*script, interpreter, "EnterZone", "BrokenWing");
            sk_bindings::MenuExecutable* again = stack.currentMenu();
            Check(again != nullptr && stack.currentLevelName() == "broken1",
                  "the prompt comes back up on a second visit");
            if (again) {
                bool handled = again->GoBack();
                Check(handled, "the back key is handled -- LevelConfirmBack -> DontGo");
                Check(stack.currentLevelName() == "ghstpass",
                      "the back key restored the level name too");
                Check(!stack.gameStartRequested(), "and loaded nothing");
                stack.ClearCloseMenuRequest();
            }

            // --- Yes ---
            CallHandler(*script, interpreter, "EnterZone", "BrokenWing");
            sk_bindings::MenuExecutable* third = stack.currentMenu();
            Check(third != nullptr, "and a third time");
            if (third) {
                Check(third->TryInvoke("Go"), "the \"Yes\" row's callback runs");
                Check(stack.gameStartRequested() && stack.requestedZone() == "broken1",
                      "\"Yes\" asked for the load, and for the right zone");
                Check(stack.cameraStart().armed && stack.cameraStart().x == 16768,
                      "\"Yes\" left the spawn override armed for the zone that is about to load");
            }
        }
        stack.level().AttachZoneScript(nullptr);
    }

    std::printf("\n--- Part 4b: azra.s -> GhstPass, the qualified spelling ---\n");
    {
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(root, interpreter, &strings);
        auto script = LoadZoneScript(root, "azra", interpreter, stack);
        Check(script != nullptr, "azra.s loads");
        if (script) {
            // azra.s's own region tag for the ghstpass exit.
            const std::string src = ReadFile(root + "/azra.s");
            size_t at = src.find("Level.LoadLevel(\"GhstPass\");");
            size_t tagAt = src.rfind("if( s = \"", at);
            std::string tag;
            if (at != std::string::npos && tagAt != std::string::npos) {
                size_t open = tagAt + 9;
                size_t close = src.find('"', open);
                if (close != std::string::npos) tag = src.substr(open, close - open);
            }
            Check(!tag.empty(), "found azra.s's own region tag for the GhstPass exit");
            std::printf("   azra.s region tag: \"%s\"\n", tag.c_str());

            CallHandler(*script, interpreter, "EnterZone", tag);
            sk_bindings::MenuExecutable* prompt = stack.currentMenu();
            Check(prompt != nullptr && prompt->scriptName() == sk::kLevelConfirmMenuName,
                  "Level.LoadLevel(\"GhstPass\") raised the same prompt (it used to teleport)");
            Check(!stack.gameStartRequested(), "and nothing loaded behind it");
            if (prompt && prompt->rows().size() >= 2) {
                const auto& row = prompt->rows()[1];
                std::string name = row.textId >= 0 ? strings.Get(row.textId) : row.literalText;
                Check(name == "Ghast's Pass",
                      "the destination row says \"Ghast's Pass\", from the mixed-case zone name");
            }
        }
        stack.level().AttachZoneScript(nullptr);
    }

    // ---------------------------------------------------------------
    // Part 5: the rest of the bare vocabulary.
    //
    // `LoadLevel` was not the only Level native a zone root script reaches
    // without the prefix, and every one of them soft-failed. UnlockZone is
    // the loudest: 33 bare calls across the corpus, and an EnterZone arm
    // that cannot unlock the region it guards is a door that never opens.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 5: the other bare Level natives ---\n");
    {
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(root, interpreter, &strings);
        RecordingZoneRegions regions;
        stack.level().SetZoneRegions(&regions);
        skExecutableContext ctxt(&interpreter);
        skRValue ret;

        auto script = LoadZoneScript(root, "ghstpass", interpreter, stack);
        Check(script != nullptr, "ghstpass.s loads");
        if (script) {
            skRValueArray args;
            args.append(skRValue(skString("testregion")));
            bool handled = script->method(skString("UnlockZone"), args, ret, ctxt);
            Check(handled, "a bare UnlockZone(...) on the zone script resolves");
            Check(regions.unlocked.size() == 1 && regions.unlocked[0] == "testregion",
                  "and reaches the same host the Level-qualified one does");

            skRValueArray lockArgs;
            lockArgs.append(skRValue(skString("testregion")));
            script->method(skString("LockZone"), lockArgs, ret, ctxt);
            Check(regions.locked.size() == 1, "so does a bare LockZone(...)");

            skRValueArray ambientArgs;
            ambientArgs.append(skRValue(73));
            ambientArgs.append(skRValue(100));
            Check(script->method(skString("PlayAmbient"), ambientArgs, ret, ctxt),
                  "so does a bare PlayAmbient(id, volume)");

            // The other direction still works, and does not recurse:
            // `Level.<name>()` for a handler the zone script defines.
            skRValueArray none;
            Check(stack.level().method(skString("GPZombiesKilled"), none, ret, ctxt),
                  "and Level.<scriptHandler>() still reaches the zone script (no recursion)");
        }
        stack.level().SetZoneRegions(nullptr);
        stack.level().AttachZoneScript(nullptr);
    }

    // ---------------------------------------------------------------
    // Part 6: GetNextLevelName().
    // ---------------------------------------------------------------
    std::printf("\n--- Part 6: GetNextLevelName() ---\n");
    {
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(root, interpreter, &strings);
        stack.OpenMenu(sk::kLevelConfirmMenuName);
        sk_bindings::MenuExecutable* menu = stack.currentMenu();
        Check(menu != nullptr, "LevelConfirm opens by the engine's own spelling of its name");
        if (menu) {
            Check(menu->scriptName() == "LevelConfirm",
                  "and keeps that spelling, which is what LevelConfirmBack is built from");
            skRValueArray none;
            skRValue ret;
            skExecutableContext ctxt(&interpreter);

            struct Row {
                const char* internalName;
                const char* display;
            };
            static const Row kRows[] = {
                {"broken1", "Broken Wing I"},   {"GhstPass", "Ghast's Pass"},
                {"snowline", "Snowline"},        {"dstar_e", "Dragonstar East"},
                {"azra", "Azra's Crossing"},
            };
            bool allOk = true;
            for (const Row& row : kRows) {
                stack.SetCurrentLevelName(row.internalName);
                menu->method(skString("GetNextLevelName"), none, ret, ctxt);
                if (std::string(ret.str().ptr()) != row.display) {
                    std::printf("   %s -> \"%s\" (expected \"%s\")\n", row.internalName,
                                ret.str().ptr(), row.display);
                    allOk = false;
                }
            }
            Check(allOk, "GetNextLevelName() is the destination's display name, case-insensitively");

            // `ffarena` is the one zone with no entry in the 3821-3840 run.
            stack.SetCurrentLevelName("ffarena");
            menu->method(skString("GetNextLevelName"), none, ret, ctxt);
            Check(std::string(ret.str().ptr()) == "ffarena",
                  "a zone with no display-name entry falls back to its internal name");
            Check(sk::ZoneDisplayNameStringId("ffarena") == -1,
                  "-- which is exactly the one zone the shipped run is missing");
        }
    }

    std::printf("\nm90_level_transition_smoke: %s (%d checks)\n",
                g_failures == 0 ? "OK" : "FAILED", g_checks);
    return g_failures == 0 ? 0 : 1;
}
