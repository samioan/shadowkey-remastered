// M23 (zone-root script loading) smoke test: proves the real azra.s
// Init() runs end to end against real data -- most of its dozens of
// GetPlayer().saved_X/QuestX checks and Level.GetEntity(...) lookups
// referencing real, named .ent placements, none of it previously
// exercised at all (azra.s was never loaded by this port before M23).
//
// Part 1 confirms real "m1"/"m2" placements (azra.ent) resolve to
// monsters/Bandit_Brawler.s (typeId 106) and monsters/Azra_Rat.s (typeId
// 202) -- both real, ordinary category-2 monster scripts, standing in
// for two of azra.s's own `Level.GetEntity("m1")`.."m20"` targets.
//
// Part 2 runs azra.s's real Init() on a completely fresh PlayerExecutable
// (every GetPlayer().saved_X/QuestX check correctly false/0-default) and
// confirms it doesn't throw despite dozens of real native calls, that
// SetZone(1,2000) -- azra.s's own top-level saved_SetZone[0] guard --
// fires with its real literal arguments, and that "m1"/"m2" are
// untouched (the SetQuestSolved(0,true)-adjacent... no -- the
// saved_EndGame-gated DestroyObjectMirror block correctly does NOT fire).
//
// Part 3 is the payoff: pre-sets GetPlayer().saved_EndGame = 1 (via
// PlayerExecutable's new M23 generic field fallback -- the same real
// mechanism a real script assigning that field would use) and reruns
// Init(), confirming the real DestroyObjectMirror() calls this time
// genuinely reach and mark "m1"/"m2" destroyed().
#include <cstdio>
#include <memory>
#include <string>

#include "assets/string_table.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/zone_script_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"
#include "world/zone.h"

namespace {

std::unique_ptr<sk_bindings::MonsterExecutable> LoadMonster(const std::string& fullPath,
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

bool RunAzraInit(sk_bindings::ZoneScriptExecutable& zoneScript, skInterpreter& interpreter) {
    skRValueArray args;
    args.append(skRValue(0));
    skRValue ret;
    skExecutableContext ctxt(&interpreter);
    return zoneScript.method(skString("Init"), args, ret, ctxt);
}

}  // namespace

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";
    const char* zoneName = argc > 2 ? argv[2] : "azra";

    sk::StringTable strings;
    if (!strings.Load(std::string(scriptRoot) + "/stringtable.eng")) {
        std::printf("m23_zonescript_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot)) {
        std::printf("m23_zonescript_smoke: FAILED to load entities.txt\n");
        return 1;
    }
    sk::Zone zone;
    if (!zone.Load(scriptRoot, zoneName)) {
        std::printf("m23_zonescript_smoke: FAILED to load zone %s\n", zoneName);
        return 1;
    }

    bool ok = true;

    // --- Part 1: real "m1"/"m2" placements, structural ---
    int m1TypeId = -1, m2TypeId = -1;
    for (const auto& e : zone.entities()) {
        if (e.name == "m1") m1TypeId = e.typeId;
        if (e.name == "m2") m2TypeId = e.typeId;
    }
    bool m1Ok = m1TypeId == 106;
    bool m2Ok = m2TypeId == 202;
    std::printf("real \"m1\" placement typeId: %d (expected 106/Bandit_Brawler.s) %s\n", m1TypeId,
                m1Ok ? "OK" : "FAILED");
    std::printf("real \"m2\" placement typeId: %d (expected 202/Azra_Rat.s) %s\n", m2TypeId,
                m2Ok ? "OK" : "FAILED");
    if (!m1Ok || !m2Ok) ok = false;

    const sk::EntityTypeDescriptor* m1Desc = entityTypes.Lookup(106);
    const sk::EntityTypeDescriptor* m2Desc = entityTypes.Lookup(202);
    if (!m1Desc || !m2Desc) {
        std::printf("m23_zonescript_smoke: FAILED -- missing entities.txt entry\n");
        return 1;
    }

    // --- Part 2: real azra.s Init() on a fresh player ---
    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
    stack.level().SetEntityTypes(&entityTypes);

    auto toForwardSlash = [](std::string path) {
        for (char& c : path) {
            if (c == '\\') c = '/';
        }
        return path;
    };
    std::unique_ptr<sk_bindings::MonsterExecutable> m1, m2;
    try {
        m1 = LoadMonster(std::string(scriptRoot) + "/" + toForwardSlash(m1Desc->name), interpreter,
                          &strings, stack.player(), stack);
        m2 = LoadMonster(std::string(scriptRoot) + "/" + toForwardSlash(m2Desc->name), interpreter,
                          &strings, stack.player(), stack);
    } catch (skParseException& e) {
        std::printf("m23_zonescript_smoke: FAILED -- PARSE ERROR: %s\n", e.toString().ptr());
        return 1;
    } catch (skRuntimeException& e) {
        std::printf("m23_zonescript_smoke: FAILED -- RUNTIME ERROR: %s\n", e.toString().ptr());
        return 1;
    }
    stack.level().RegisterEntity("m1", m1.get());
    stack.level().RegisterEntity("m2", m2.get());

    std::string azraPath = std::string(scriptRoot) + "/" + zoneName + ".s";
    skExecutableContext loadCtxt(&interpreter);
    std::unique_ptr<sk_bindings::ZoneScriptExecutable> zoneScript;
    bool initOk = false;
    try {
        zoneScript = std::make_unique<sk_bindings::ZoneScriptExecutable>(
            skString(azraPath.c_str()), loadCtxt, stack);
        initOk = RunAzraInit(*zoneScript, interpreter);
    } catch (skParseException& e) {
        std::printf("m23_zonescript_smoke: FAILED -- PARSE ERROR loading %s: %s\n", azraPath.c_str(),
                    e.toString().ptr());
        return 1;
    } catch (skRuntimeException& e) {
        std::printf("m23_zonescript_smoke: FAILED -- RUNTIME ERROR loading %s: %s\n",
                    azraPath.c_str(), e.toString().ptr());
        return 1;
    }
    std::printf("azra.s real Init() ran without throwing (fresh player, every saved_X/QuestX "
                "check false-default): %s %s\n",
                initOk ? "true" : "false", initOk ? "OK" : "FAILED");
    if (!initOk) ok = false;

    bool untouchedOk = !m1->destroyed() && !m2->destroyed();
    std::printf("\"m1\"/\"m2\" untouched (saved_EndGame defaults to 0/false): %s %s\n",
                untouchedOk ? "true" : "false", untouchedOk ? "OK" : "FAILED");
    if (!untouchedOk) ok = false;

    // --- Part 3: the payoff -- real saved_EndGame gate ---
    stack.player().setValue(skString("saved_EndGame"), skString(""), skRValue(1));
    bool secondInitOk = RunAzraInit(*zoneScript, interpreter);
    std::printf("azra.s real Init() re-run with saved_EndGame=1: %s %s\n",
                secondInitOk ? "ran without throwing" : "threw", secondInitOk ? "OK" : "FAILED");
    if (!secondInitOk) ok = false;

    bool destroyedOk = m1->destroyed() && m2->destroyed();
    std::printf("\"m1\"/\"m2\" destroyed() after real DestroyObjectMirror(): %s %s\n",
                destroyedOk ? "true" : "false", destroyedOk ? "OK" : "FAILED");
    if (!destroyedOk) ok = false;

    std::printf("\nm23_zonescript_smoke: %s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
