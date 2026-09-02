// Combat vertical-slice smoke test: proves MonsterExecutable against a
// real placed monster, not a synthetic fixture -- loads the real azra
// zone, resolves one of its 35 real typeId-202 (.ent -> entities.txt)
// placements to monsters/Azra_Rat.s, actually runs that script's real
// Init() through the vendored interpreter, and checks the loaded stats
// match the script's own literal values. Then exercises the
// ApplyDamage/InvokeOnKilled/RollDamage host-side API headlessly (no
// windowing -- main.cpp's tick loop is the only thing that actually
// drives combat during real play). See docs/PORT_ROADMAP.md's combat
// vertical-slice entry.
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>

#include "assets/string_table.h"
#include "simkin_bindings/combat.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"
#include "world/zone.h"

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";
    const char* zoneName = argc > 2 ? argv[2] : "azra";

    sk::StringTable strings;
    if (!strings.Load(std::string(scriptRoot) + "/stringtable.eng")) {
        std::printf("m12_combat_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }

    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot)) {
        std::printf("m12_combat_smoke: FAILED to load entities.txt\n");
        return 1;
    }

    sk::Zone zone;
    if (!zone.Load(scriptRoot, zoneName)) {
        std::printf("m12_combat_smoke: FAILED to load zone %s\n", zoneName);
        return 1;
    }

    // 1. Find a real typeId-202 (azra_rat) placement -- azra places 35
    // of them, any one proves the resolution chain.
    const sk::Zone::EntPlacement* ratPlacement = nullptr;
    for (const auto& e : zone.entities()) {
        if (e.typeId == 202) {
            ratPlacement = &e;
            break;
        }
    }
    if (!ratPlacement) {
        std::printf("m12_combat_smoke: FAILED -- no typeId=202 (azra_rat) placement in %s\n",
                    zoneName);
        return 1;
    }

    // 2. Resolve it through entities.txt -> a real script path.
    const sk::EntityTypeDescriptor* desc = entityTypes.Lookup(ratPlacement->typeId);
    if (!desc) {
        std::printf("m12_combat_smoke: FAILED -- typeId 202 has no entities.txt entry\n");
        return 1;
    }
    if (desc->category != 2) {
        std::printf("m12_combat_smoke: FAILED -- typeId 202 category=%d, expected 2 (monster)\n",
                    desc->category);
        return 1;
    }
    std::string relPath = desc->name;
    std::replace(relPath.begin(), relPath.end(), '\\', '/');
    std::string fullPath = std::string(scriptRoot) + "/" + relPath;
    std::printf("resolved typeId=202 -> \"%s\" (category=%d)\n", relPath.c_str(), desc->category);

    // 3. Actually run the real script's Init() -- same
    // skExecutableContext/try-catch pattern PlayerExecutable::
    // LoadStartingInventory already establishes for a real .s file.
    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);

    bool ok = true;
    skExecutableContext loadCtxt(&interpreter);
    std::unique_ptr<sk_bindings::MonsterExecutable> rat;
    try {
        rat = std::make_unique<sk_bindings::MonsterExecutable>(skString(fullPath.c_str()),
                                                                 loadCtxt, &strings, stack.player());
        skRValueArray args;
        args.append(skRValue(0));  // placeholder for Init's "(s)" parameter
        skRValue ret;
        skExecutableContext callCtxt(&interpreter);
        rat->method(skString("Init"), args, ret, callCtxt);
    } catch (skParseException& e) {
        std::printf("m12_combat_smoke: FAILED -- PARSE ERROR loading %s: %s\n", fullPath.c_str(),
                    e.toString().ptr());
        return 1;
    } catch (skRuntimeException& e) {
        std::printf("m12_combat_smoke: FAILED -- RUNTIME ERROR loading %s: %s\n", fullPath.c_str(),
                    e.toString().ptr());
        return 1;
    }

    // 4. Real literal values from monsters/azra_rat.s's own Init() body.
    auto check = [&](const char* label, int actual, int expected) {
        bool pass = actual == expected;
        std::printf("%s: %d (expected %d) %s\n", label, actual, expected, pass ? "OK" : "FAILED");
        if (!pass) ok = false;
    };
    check("attack", rat->attack(), 3);
    check("defense", rat->defense(), 4);
    check("damageMin", rat->damageMin(), 3);
    check("damageMax", rat->damageMax(), 6);
    check("armorValue", rat->armorValue(), 2);
    check("maxHealth", rat->maxHealth(), 12);
    check("currentHealth (starts full)", rat->currentHealth(), 12);
    check("chaseRadius", static_cast<int>(rat->chaseRadius()), 18000);
    check("aggressive", rat->aggressive() ? 1 : 0, 1);

    std::string expectedName = strings.Get(2030);
    std::printf("name: \"%s\" (expected \"%s\") %s\n", rat->name().c_str(), expectedName.c_str(),
                rat->name() == expectedName ? "OK" : "FAILED");
    if (rat->name() != expectedName) ok = false;

    // 5. Damage sequence: apply less than maxHealth, confirm still
    // alive; apply the rest, confirm death; confirm OnKilled() runs
    // without throwing (it soft-fails cleanly through unimplemented
    // quest-tracking calls -- see monster_executable.cpp's comment).
    rat->ApplyDamage(5);
    if (!rat->alive() || rat->currentHealth() != 7) {
        std::printf("m12_combat_smoke: FAILED -- after 5 damage, health=%d alive=%s\n",
                    rat->currentHealth(), rat->alive() ? "true" : "false");
        ok = false;
    }
    rat->ApplyDamage(7);
    if (rat->alive() || rat->currentHealth() != 0) {
        std::printf("m12_combat_smoke: FAILED -- after lethal damage, health=%d alive=%s\n",
                    rat->currentHealth(), rat->alive() ? "true" : "false");
        ok = false;
    }
    rat->InvokeOnKilled();  // must not throw/crash -- see its own comment
    std::printf("InvokeOnKilled() returned normally: OK\n");

    // 6. RollDamage bounds sanity -- not a statistical test (avoids
    // flakiness), just confirms the formula never produces a
    // nonsensical result across many rolls.
    bool rollsOk = true;
    for (int i = 0; i < 500; ++i) {
        int dmg = sk_bindings::RollDamage(/*attackerAttack=*/10, /*defenderDefense=*/2,
                                           /*defenderArmor=*/4, /*dmgMin=*/3, /*dmgMax=*/6);
        if (dmg < 0 || dmg > 6) rollsOk = false;
    }
    std::printf("RollDamage bounds (500 rolls, attacker favored): %s\n",
                rollsOk ? "OK" : "FAILED");
    if (!rollsOk) ok = false;

    std::printf("\nm12_combat_smoke: %s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
