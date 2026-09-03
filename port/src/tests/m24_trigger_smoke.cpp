// M24 (AddTrigger kill-count callbacks) smoke test: proves the real
// "zone-scoped kill-count trigger" pattern end to end against real
// script data -- killing real monsters of a tracked typeId genuinely
// fires a real zone-root script callback once the real limit is reached.
//
// Real data, read directly off the install image:
//   ghstpass.s's Init():
//     zombieTrigger = AddTrigger("zombiesKilled");
//     zombieTrigger.SetCallback("GPZombiesKilled");
//     zombieTrigger.SetEntityID(104);
//     zombieTrigger.SetLimit(2);
//     fight12 = AddEncounters("fight12","fight12W","fight12S");
//     fight12.AddRandomSets(205,2); ... (x5, unconditional, no if-guard --
//       this is why AddEncounters has to return a real (if inert) object,
//       not just soft-fail to an int: the very next .AddRandomSets() call
//       would otherwise throw and abort the rest of Init())
//   ghstpass.s's GPZombiesKilled(s):
//     Level.Log("GPZombiesKilled"); save_bZombiesKilled=true;
//     GetPlayer().SetQuestSolved(14);
//   entities.txt: 104 69 2 monsters\Azra_Zombie.s (real, ordinary
//     category-2 monster script).
#include <cstdio>
#include <memory>
#include <string>

#include "assets/string_table.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/zone_script_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    if (!strings.Load(std::string(scriptRoot) + "/stringtable.eng")) {
        std::printf("m24_trigger_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot)) {
        std::printf("m24_trigger_smoke: FAILED to load entities.txt\n");
        return 1;
    }

    bool ok = true;

    // --- Part 1: real typeId 104, structural ---
    const sk::EntityTypeDescriptor* desc = entityTypes.Lookup(104);
    if (!desc) {
        std::printf("m24_trigger_smoke: FAILED -- typeId 104 has no entities.txt entry\n");
        return 1;
    }
    bool descOk = desc->category == 2 && desc->name == "monsters\\Azra_Zombie.s";
    std::printf("typeId=104 real entities.txt entry: category=%d name=\"%s\" (expected "
                "category=2, \"monsters\\Azra_Zombie.s\") %s\n",
                desc->category, desc->name.c_str(), descOk ? "OK" : "FAILED");
    if (!descOk) ok = false;

    // --- Part 2: real ghstpass.s Init() ---
    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
    stack.level().SetEntityTypes(&entityTypes);

    std::string ghstpassPath = std::string(scriptRoot) + "/ghstpass.s";
    skExecutableContext loadCtxt(&interpreter);
    std::unique_ptr<sk_bindings::ZoneScriptExecutable> zoneScript;
    bool initOk = false;
    try {
        zoneScript = std::make_unique<sk_bindings::ZoneScriptExecutable>(
            skString(ghstpassPath.c_str()), loadCtxt, stack);
        skRValueArray args;
        args.append(skRValue(0));  // placeholder for Init's "(s)" parameter
        skRValue ret;
        skExecutableContext callCtxt(&interpreter);
        initOk = zoneScript->method(skString("Init"), args, ret, callCtxt);
    } catch (skParseException& e) {
        std::printf("m24_trigger_smoke: FAILED -- PARSE ERROR loading %s: %s\n",
                    ghstpassPath.c_str(), e.toString().ptr());
        return 1;
    } catch (skRuntimeException& e) {
        std::printf("m24_trigger_smoke: FAILED -- RUNTIME ERROR loading %s: %s\n",
                    ghstpassPath.c_str(), e.toString().ptr());
        return 1;
    }
    std::printf("ghstpass.s real Init() ran without throwing (zombieTrigger set up, both "
                "AddEncounters() calls' chained AddRandomSets() calls didn't crash it): %s %s\n",
                initOk ? "true" : "false", initOk ? "OK" : "FAILED");
    if (!initOk) ok = false;

    // --- Part 3: the payoff -- real kill-count gate ---
    bool solvedBeforeAny = stack.player().questSolved(14);
    std::printf("player.questSolved(14) before any kill: %s (expected false) %s\n",
                solvedBeforeAny ? "true" : "false", !solvedBeforeAny ? "OK" : "FAILED");
    if (solvedBeforeAny) ok = false;

    zoneScript->NotifyKilled(104);  // 1st real zombie kill -- limit is 2
    bool solvedAfterOne = stack.player().questSolved(14);
    std::printf("player.questSolved(14) after 1 kill (limit=2): %s (expected false) %s\n",
                solvedAfterOne ? "true" : "false", !solvedAfterOne ? "OK" : "FAILED");
    if (solvedAfterOne) ok = false;

    zoneScript->NotifyKilled(104);  // 2nd real zombie kill -- reaches the limit
    bool solvedAfterTwo = stack.player().questSolved(14);
    std::printf("player.questSolved(14) after 2 kills (real GPZombiesKilled() ran -> "
                "GetPlayer().SetQuestSolved(14)): %s (expected true) %s\n",
                solvedAfterTwo ? "true" : "false", solvedAfterTwo ? "OK" : "FAILED");
    if (!solvedAfterTwo) ok = false;

    // --- Part 4: robustness -- already-fired and unrelated typeIds ---
    zoneScript->NotifyKilled(104);  // must not crash or double-fire
    zoneScript->NotifyKilled(999);  // no trigger watches this typeId at all
    bool stillSolvedOk = stack.player().questSolved(14);
    std::printf("player.questSolved(14) after a 3rd/unrelated kill: %s (still expected true, no "
                "crash) %s\n",
                stillSolvedOk ? "true" : "false", stillSolvedOk ? "OK" : "FAILED");
    if (!stillSolvedOk) ok = false;

    std::printf("\nm24_trigger_smoke: %s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
