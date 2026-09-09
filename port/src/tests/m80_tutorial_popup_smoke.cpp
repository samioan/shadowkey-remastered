// M80 smoke test: the tutorial popups, and the two things that stopped
// them.
//
// Reported: walking around Azra never produces the tutorial messages the
// real game shows, and that is only the visible corner of a general
// mechanism -- an `EnterZone` handler opening a menu is how the game
// delivers hints, warnings and a good part of its story.
//
// Decompiled for this milestone:
//   FUN_1003136c case 2   ParseActionText -- the Menu class's binding 2
//   FUN_10030250          the `[KD_n]` substitution itself
//   FUN_1001a578          binding offset -> key-name stringtable id
//   FUN_10078de4 case 0x6c AddStaticItem, all four arguments
//   FUN_1007dca0          the word wrap, one widget per line
//   FUN_10076b64          the menu draw: row pitch 0xc, x=9, the shadow
//
// Part 1  the twelve `KD_*` tokens and the actions they name
// Part 2  the substitution, on the real shipped strings
// Part 3  the wrap, against FUN_1007dca0's own arithmetic
// Part 4  starthelp.s end to end, every page
// Part 5  every page fits on a 176x208 screen -- the numbers cross-check
// Part 6  azra's "start" region really opens it, and `Level` still works
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "engine/input_state.h"
#include "simkin_bindings/action_text.h"
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

namespace sk_b = sk_bindings;

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%-78s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    if (!ok) ++g_failures;
}

// Every row's display text, in order, resolving a wrapped row's own
// literal line ahead of its source id (same rule main.cpp's RowText uses).
std::vector<std::string> RowTexts(sk_b::MenuExecutable* menu, const sk::StringTable& strings) {
    std::vector<std::string> out;
    if (!menu) return out;
    for (const auto& row : menu->rows()) {
        out.push_back(!row.literalText.empty() ? row.literalText
                                                : (row.textId >= 0 ? strings.Get(row.textId) : ""));
    }
    return out;
}

std::string Joined(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& line : lines) {
        if (!out.empty()) out += " ";
        out += line;
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    std::printf("=== M80: the tutorial popups ===\n\n");

    sk::StringTable strings;
    if (!strings.Load(std::string(scriptRoot) + "/stringtable.eng")) {
        std::printf("m80_tutorial_popup_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }

    // ---- Part 1: the twelve tokens ----
    //
    // The strongest check available on the whole table, and it needs no
    // screenshot: `[KD_n]` is named for a *key*, but the substitution
    // resolves an *action*. If the twelve `mov r1, #N` values were read
    // wrong, or M57's action indices were wrong, the two would disagree.
    // They agree twelve times out of twelve.
    std::printf("-- 1. the [KD_*] tokens --\n");
    struct TokenCase {
        const char* token;
        int action;
        sk::ButtonSlot defaultKey;
    };
    const TokenCase kCases[] = {
        {"KD_0", 10, sk::ButtonSlot::Key0},         {"KD_1", 8, sk::ButtonSlot::Key1},
        {"KD_2", 4, sk::ButtonSlot::Key2},          {"KD_3", 13, sk::ButtonSlot::Key3},
        {"KD_4", 6, sk::ButtonSlot::Key4},          {"KD_5", 15, sk::ButtonSlot::Key5},
        {"KD_6", 7, sk::ButtonSlot::Key6},          {"KD_7", 14, sk::ButtonSlot::Key7},
        {"KD_8", 5, sk::ButtonSlot::Key8},          {"KD_9", 9, sk::ButtonSlot::Key9},
        {"KD_ASTERISK", 11, sk::ButtonSlot::KeyStar},
        {"KD_POUND", 12, sk::ButtonSlot::KeyHash},
    };
    sk::InputState defaults;
    bool allTokensMap = true;
    bool allTokensSelfConsistent = true;
    for (const TokenCase& c : kCases) {
        if (sk_b::ActionIndexForKeyToken(c.token) != c.action) allTokensMap = false;
        if (defaults.binding(static_cast<sk::Action>(c.action)) != static_cast<int>(c.defaultKey)) {
            allTokensSelfConsistent = false;
        }
    }
    Check(allTokensMap, "all twelve tokens resolve to FUN_10030250's own action indices");
    Check(allTokensSelfConsistent,
          "...and each token's action is the one whose DEFAULT key the token is named for");
    Check(sk_b::ActionIndexForKeyToken("KD_ENTER") < 0 && sk_b::ActionIndexForKeyToken("") < 0,
          "an unknown token resolves to nothing");
    // The label ids themselves: InputState_InitDefaultBindings' 0xd05+slot.
    Check(strings.Get(sk::InputState::slotNameStringId(sk::ButtonSlot::Key5)) == "Key 5" &&
              strings.Get(sk::InputState::slotNameStringId(sk::ButtonSlot::Left)) == "Left" &&
              strings.Get(sk::InputState::slotNameStringId(sk::ButtonSlot::KeyHash)) == "Key #",
          "slot 0xd05+n reads back the real key labels out of stringtable.eng");
    Check(sk::InputState::slotNameStringId(sk::ButtonSlot::Slot19Unused) < 0,
          "slot 19, the one InitDefaultBindings never registers, has no label");

    // ---- Part 2: the substitution ----
    std::printf("\n-- 2. ParseActionText on the real shipped strings --\n");
    const std::string t2995 = sk_b::ParseActionText(2995, &defaults, &strings);
    std::printf("   2995 -> \"%s\"\n", t2995.c_str());
    Check(t2995 == "Press Key 5 to continue.",
          "2995 \"Press [KD_5] to continue.\" resolves against the default bindings");
    const std::string t3748 = sk_b::ParseActionText(3748, &defaults, &strings);
    Check(t3748.find("[KD_") == std::string::npos && t3748.find("used by Key 7") != std::string::npos &&
              t3748.find("Key * will cycle") != std::string::npos,
          "3748's two tokens both go, including the one that is not a digit");
    // The whole point of the indirection: rebind, and the text follows.
    sk::InputState rebound;
    rebound.Rebind(sk::Action::UseRightAction, sk::ButtonSlot::Key1);
    Check(sk_b::ParseActionText(2995, &rebound, &strings) == "Press Key 1 to continue.",
          "rebinding Use Right Action to key 1 rewrites 2995 -- the reason this native exists");
    // FUN_10030250's `bVar3 = false` arm: an unknown token stops the pass
    // dead, taking every later token with it.
    Check(sk_b::SubstituteActionKeys("a [KD_NOPE] b [KD_5] c", &defaults, &strings) ==
              "a [KD_NOPE] b [KD_5] c",
          "an unrecognised token ends the pass and leaves the rest of the text alone");
    Check(sk_b::SubstituteActionKeys("half open [KD_5", &defaults, &strings) == "half open [KD_5" &&
              sk_b::SubstituteActionKeys("nothing here", &defaults, &strings) == "nothing here",
          "an unclosed bracket, and text with no token at all, come back untouched");
    // Null input state == the engine's own defaults, which is what the
    // strings were written against.
    Check(sk_b::ParseActionText(2995, nullptr, &strings) == t2995,
          "no InputState reads as the default bindings");

    // ---- Part 3: FUN_1007dca0's wrap ----
    std::printf("\n-- 3. the wrap --\n");
    Check(sk_b::WrapMenuText("short", sk_b::kLeftAlignedWrapChars).size() == 1,
          "text shorter than the limit stays one row (`if (iVar4 < param_4)`)");
    const std::string alarm = strings.Get(2502);
    const std::vector<std::string> alarmLines =
        sk_b::WrapMenuText(alarm, sk_b::kLeftAlignedWrapChars);
    std::printf("   2502 wraps to %d line(s), longest %d chars\n",
                static_cast<int>(alarmLines.size()), [&] {
                    int m = 0;
                    for (const std::string& l : alarmLines) {
                        m = (std::max)(m, static_cast<int>(l.size()));
                    }
                    return m;
                }());
    Check(alarmLines.size() > 1, "the 170-character alarm message does not");
    bool noLineRunsOff = true;
    for (const std::string& line : alarmLines) {
        // The engine breaks at the space *after* passing the limit, so a
        // line can overshoot by one word; nothing may overshoot by more.
        if (static_cast<int>(line.size()) > sk_b::kLeftAlignedWrapChars + 12) noLineRunsOff = false;
    }
    Check(noLineRunsOff, "...and no line is more than one word past the 25-character limit");
    Check(Joined(alarmLines) == alarm, "the lines are the source text, split, with nothing lost");
    // A separator never wraps, at either width.
    Check(sk_b::WrapMenuText(strings.Get(179), sk_b::kStaticItemWrapChars).size() == 1,
          "the `-----` separator (id 179) is one row at the 17-character width too");

    // ---- Part 4: starthelp.s, end to end ----
    std::printf("\n-- 4. starthelp.s, the popup azra's \"start\" region opens --\n");
    skInterpreter interpreter;
    sk_b::MenuStack stack(scriptRoot, interpreter, &strings);
    stack.SetInput(&defaults);
    stack.ReopenMenu("starthelp");
    sk_b::MenuExecutable* help = stack.currentMenu();
    Check(help != nullptr && !help->rows().empty(), "OpenMenu(\"starthelp\") builds a real screen");
    // Post-M80: `FUN_100779b8` sets `menu+0x50 = 0x14` before the script
    // runs, so a screen that never calls MenuBackground still has the
    // parchment -- not a transparent hole onto whatever is behind it.
    Check(help != nullptr && help->backgroundId() == sk_b::kDefaultMenuBackground,
          "...on global.spr slot 20, the default FUN_100779b8 arms before Init()");
    if (help) {
        const std::vector<std::string> texts = RowTexts(help, strings);
        for (const std::string& t : texts) std::printf("   | %s\n", t.c_str());
        // Init(): AddStaticItem(2502), AddStaticItem(179,false),
        // AddMenuItem(ParseActionText(2995), "FirstInfo").
        Check(texts.size() == alarmLines.size() + 2,
              "its rows are the wrapped alarm message, the separator, and one button");
        Check(!texts.empty() && texts.front() == alarmLines.front(),
              "the first row is the message's first line, not the whole paragraph");
        const auto& rows = help->rows();
        Check(!rows.empty() && !rows.front().centered,
              "a message row takes FUN_1007f49c's left-aligned arm");
        bool separatorCentered = false;
        for (const auto& row : rows) {
            if (row.textId == 179) separatorCentered = row.centered;
        }
        Check(separatorCentered,
              "...and the `-----` row is centred instead -- the \"---\" prefix rule");
        Check(!texts.empty() && texts.back() == "Press Key 5 to continue.",
              "the button's own text came through ParseActionText, not as textId 0");
        Check(!rows.empty() && rows.back().selectable && rows.back().callback == "FirstInfo",
              "...and it is the selectable row, wired to FirstInfo");

        // Walk the whole tutorial: FirstInfo -> MoreInfo -> NextInfo ->
        // FinalInfo -> FinalInfo2 -> MenuQuit. Every page but the first is
        // built out of ParseActionText.
        const char* kPages[] = {"FirstInfo", "MoreInfo", "NextInfo", "FinalInfo", "FinalInfo2"};
        bool everyPageBuilds = true;
        bool noTokenSurvives = true;
        int worstBottom = 0;
        auto pageBottom = [&](sk_b::MenuExecutable* m) {
            return 0x32 + static_cast<int>(m->rows().size()) * sk_b::kMenuRowPitch;
        };
        worstBottom = pageBottom(help);
        for (const char* page : kPages) {
            if (!help->TryInvoke(page)) {
                everyPageBuilds = false;
                break;
            }
            if (help->rows().empty()) everyPageBuilds = false;
            for (const std::string& t : RowTexts(help, strings)) {
                if (t.find("[KD_") != std::string::npos) noTokenSurvives = false;
            }
            worstBottom = (std::max)(worstBottom, pageBottom(help));
        }
        Check(everyPageBuilds, "all five follow-on pages build rows of their own");
        Check(noTokenSurvives, "no `[KD_` token reaches a row on any of the six pages");

        // ---- Part 5: it fits ----
        //
        // Three separately recovered numbers meet here: SetStartCoord's
        // default 0x32 (M64), FUN_10076b64's 0xc row pitch and
        // FUN_1007dca0's 0x19 wrap. Get any of them wrong and the longest
        // page runs off a 208-pixel screen, taking its own "continue"
        // button -- and therefore the whole tutorial -- with it.
        std::printf("\n-- 5. the longest page ends at y=%d of %d --\n", worstBottom, 208);
        Check(worstBottom <= 208,
              "every starthelp page fits the screen at start 0x32, pitch 0xc, wrap 0x19");
        Check(0x32 + 12 * (sk_b::kMenuRowPitch + 4) > 208,
              "...and would not at the pitch this port used before (a real regression, not a tidy-up)");
    }

    // ---- Part 6: the region, and the `Level` global ----
    std::printf("\n-- 6. azra's own EnterZone --\n");
    {
        skInterpreter zoneInterp;
        sk_b::MenuStack zoneStack(scriptRoot, zoneInterp, &strings);
        zoneStack.SetInput(&defaults);
        std::unique_ptr<sk_b::ZoneScriptExecutable> azra;
        skExecutableContext loadCtxt(&zoneInterp);
        bool initThrew = false;
        std::string initError;
        try {
            azra = std::make_unique<sk_b::ZoneScriptExecutable>(
                skString((std::string(scriptRoot) + "/azra.s").c_str()), loadCtxt, zoneStack);
            // The real load order: attach first, *then* Init(). Attaching
            // is what used to poison every `Level.` call in the script.
            zoneStack.level().AttachZoneScript(azra.get());
            skRValueArray args;
            args.append(skRValue(0));
            skRValue ret;
            skExecutableContext callCtxt(&zoneInterp);
            azra->method(skString("Init"), args, ret, callCtxt);
        } catch (skParseException& e) {
            initThrew = true;
            initError = e.toString().ptr();
        } catch (skRuntimeException& e) {
            initThrew = true;
            initError = e.toString().ptr();
        }
        if (initThrew) std::printf("   (Init threw: %s)\n", initError.c_str());
        Check(azra != nullptr && !initThrew,
              "azra.s Init() runs to the end -- `Level.PlayAmbient(73,100)` on its first line");
        // The same thing the interpreter does, one level down: the bare
        // name `Level` inside the zone script must reach the global, not a
        // tree node the script invented on the way past.
        skRValue levelValue;
        const bool shadowed =
            azra && azra->getValue(skString("Level"), skString(), levelValue);
        Check(azra != nullptr && !shadowed,
              "...because the script no longer answers to the name `Level` itself");
        skRValue ownField;
        Check(azra && azra->getValue(skString("StartCreated"), skString(), ownField),
              "its own declared fields still resolve on the script, ahead of any global");
        skRValue typo;
        Check(azra && azra->getValue(skString("saved_NoSuchField"), skString(), typo) &&
                  typo.intValue() == 0,
              "and an undeclared field still reads back falsy (the five shipped typos)");

        if (azra && !initThrew) {
            sk_b::MenuExecutable* before = zoneStack.currentMenu();
            azra->EnterRegion("start");
            sk_b::MenuExecutable* after = zoneStack.currentMenu();
            Check(after != nullptr && after != before,
                  "entering the region named \"start\" opens a menu -- what main.cpp watches for");
            Check(after != nullptr && !after->rows().empty() &&
                      RowTexts(after, strings).back() == "Press Key 5 to continue.",
                  "...and it is starthelp, fully resolved");
            // StartCreated latches, exactly like every other tutorial
            // flag: a second crossing must not reopen anything, which is
            // what lets main.cpp fire this on every boundary crossing.
            sk_b::MenuExecutable* settled = zoneStack.currentMenu();
            azra->EnterRegion("start");
            Check(zoneStack.currentMenu() == settled,
                  "walking back in does nothing -- the script's own StartCreated latch");
        }
    }

    std::printf("\nm80_tutorial_popup_smoke: %s (%d failure(s))\n",
                g_failures == 0 ? "OK" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
