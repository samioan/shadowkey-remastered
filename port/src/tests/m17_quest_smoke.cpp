// M17 (quest-state tracking) smoke test: proves PlayerExecutable's new
// QuestAssigned/QuestSolved/QuestCompleted/AddMonsterKilled state
// against real script data across repeated interactions, not a
// synthetic fixture.
//
// Part 1 walks the real snowline/tanyinconvo.s dialogue tree
// (Speech1->Speech2->Speech2a->Speech3->Accept, real handler bodies)
// through MonsterExecutable::InvokeOnUse() twice, confirming the
// SECOND conversation's opening line actually reflects the quest state
// the FIRST conversation wrote -- this is MenuStack::ReopenMenu()'s
// whole reason to exist (see its own comment): plain OpenMenu() only
// ever reruns a menu's Init() once per path, which would freeze
// Tanyin's dialogue at her very first line forever.
//
// Part 2 runs real monsters/Azra_Rat.s's OnKilled() 8 times (8 separate
// instances sharing one PlayerExecutable, matching the real
// AddMonsterKilled(203)/MonstersKilled(203)>=8 kill-count gate) and
// checks the counter reaches 8 -- but does NOT expect quest id 0 to end
// up solved, because the real script's own execution order calls
// Level.GetEntity("trthgar") (a still-unimplemented global, see
// monster_executable.cpp's OnKilled comment) BEFORE SetQuestSolved(0,
// true), so the real caught exception aborts the branch before that
// line runs. Asserting this explicitly, rather than silently assuming
// it works, matches this project's "verify against real data" standard.
#include <memory>
#include <string>
#include <vector>

#include "assets/string_table.h"
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

namespace {

std::unique_ptr<sk_bindings::MonsterExecutable> LoadAndInit(const std::string& fullPath,
                                                              skInterpreter& interpreter,
                                                              const sk::StringTable* strings,
                                                              sk_bindings::PlayerExecutable& player,
                                                              sk_bindings::MenuStack& stack) {
    skExecutableContext loadCtxt(&interpreter);
    auto obj = std::make_unique<sk_bindings::MonsterExecutable>(skString(fullPath.c_str()), loadCtxt,
                                                                  strings, player, stack);
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
        std::printf("m17_quest_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }

    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
    bool ok = true;

    // --- Part 1: real dialogue-tree quest progression, two visits ---
    std::string tanyinPath = std::string(scriptRoot) + "/monsters/Tanyin_Aldwyr.s";
    std::unique_ptr<sk_bindings::MonsterExecutable> tanyin;
    try {
        tanyin = LoadAndInit(tanyinPath, interpreter, &strings, stack.player(), stack);
    } catch (skParseException& e) {
        std::printf("m17_quest_smoke: FAILED -- PARSE ERROR loading %s: %s\n", tanyinPath.c_str(),
                    e.toString().ptr());
        return 1;
    } catch (skRuntimeException& e) {
        std::printf("m17_quest_smoke: FAILED -- RUNTIME ERROR loading %s: %s\n", tanyinPath.c_str(),
                    e.toString().ptr());
        return 1;
    }

    tanyin->InvokeOnUse();  // 1st conversation
    sk_bindings::MenuExecutable* firstMenu = stack.currentMenu();
    bool firstOk = firstMenu && !firstMenu->rows().empty() && firstMenu->rows()[0].textId == 2370;
    std::printf("1st conversation opening textId: %d (expected 2370, fresh/no quest state) %s\n",
                firstMenu && !firstMenu->rows().empty() ? firstMenu->rows()[0].textId : -1,
                firstOk ? "OK" : "FAILED");
    if (!firstOk) ok = false;

    // Real handler chain a player clicking through the conversation would
    // trigger -- Speech1 -> Speech2 -> Speech2a -> Speech3 -> Accept.
    // Accept's real body: `GetPlayer().SetQuestAssigned(26,false);
    // GetPlayer().SetQuestAssigned(25); GetPlayer().SetQuestCompleted(6);
    // Quit();`.
    if (firstMenu) {
        for (const char* handler : {"Speech1", "Speech2", "Speech2a", "Speech3", "Accept"}) {
            firstMenu->TryInvoke(handler);
        }
    }

    auto checkBool = [&](const char* label, bool actual, bool expected) {
        bool pass = actual == expected;
        std::printf("%s: %s (expected %s) %s\n", label, actual ? "true" : "false",
                    expected ? "true" : "false", pass ? "OK" : "FAILED");
        if (!pass) ok = false;
    };
    checkBool("player.questAssigned(25) after Accept", stack.player().questAssigned(25), true);
    checkBool("player.questAssigned(26) after Accept (explicitly retracted)",
              stack.player().questAssigned(26), false);
    checkBool("player.questCompleted(6) after Accept", stack.player().questCompleted(6), true);

    // 2nd conversation -- must be a genuinely different MenuExecutable
    // instance (ReopenMenu(), not OpenMenu()) whose freshly-rerun Init()
    // reflects quest 25 now being assigned: real branch is
    // `ClearMenu(); AddStaticItem(2368); AddStaticItem(179,false);
    // AddMenuItem(2314,"Quit");`.
    tanyin->InvokeOnUse();
    sk_bindings::MenuExecutable* secondMenu = stack.currentMenu();
    // NOTE: not asserting secondMenu != firstMenu by pointer -- the first
    // instance was freed by ReopenMenu()'s erase() just before the second
    // was allocated, so the allocator legitimately may (and in practice
    // does) hand back the same address; pointer identity isn't a
    // meaningful signal here. The real proof ReopenMenu() worked is the
    // content check below: a rebuilt Init() reading updated quest state.
    bool secondOk =
        secondMenu && !secondMenu->rows().empty() && secondMenu->rows()[0].textId == 2368;
    std::printf(
        "2nd conversation opening textId: %d (expected 2368, quest 25 assigned/not solved) %s\n",
        secondMenu && !secondMenu->rows().empty() ? secondMenu->rows()[0].textId : -1,
        secondOk ? "OK" : "FAILED");
    if (!secondOk) ok = false;

    // --- Part 2: real kill-count gate, 8 separate azra_rat instances ---
    std::string ratPath = std::string(scriptRoot) + "/monsters/Azra_Rat.s";
    for (int i = 0; i < 8; ++i) {
        std::unique_ptr<sk_bindings::MonsterExecutable> rat;
        try {
            rat = LoadAndInit(ratPath, interpreter, &strings, stack.player(), stack);
        } catch (skParseException& e) {
            std::printf("m17_quest_smoke: FAILED -- PARSE ERROR loading %s: %s\n", ratPath.c_str(),
                        e.toString().ptr());
            return 1;
        } catch (skRuntimeException& e) {
            std::printf("m17_quest_smoke: FAILED -- RUNTIME ERROR loading %s: %s\n", ratPath.c_str(),
                        e.toString().ptr());
            return 1;
        }
        rat->ApplyDamage(rat->maxHealth());
        rat->InvokeOnKilled();  // must not crash even where it hits the Level gap (see above)
    }
    bool killCountOk = stack.player().monstersKilled(203) == 8;
    std::printf("player.monstersKilled(203) after 8 kills: %d (expected 8) %s\n",
                stack.player().monstersKilled(203), killCountOk ? "OK" : "FAILED");
    if (!killCountOk) ok = false;
    // Documented, expected gap (not a bug) -- see this file's header
    // comment and monster_executable.cpp's OnKilled comment: the real
    // script's own SetQuestSolved(0,true) sits after an unreachable
    // Level.GetEntity(...) call, so it never actually runs yet.
    bool questZeroStillOpen = !stack.player().questSolved(0);
    std::printf(
        "player.questSolved(0) after 8th kill: %s (expected false -- Level global gap, see "
        "comment) %s\n",
        stack.player().questSolved(0) ? "true" : "false", questZeroStillOpen ? "OK" : "FAILED");
    if (!questZeroStillOpen) ok = false;

    std::printf("\nm17_quest_smoke: %s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
