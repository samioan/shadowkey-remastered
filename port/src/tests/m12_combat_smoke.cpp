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
    // M43: SetMob's stat template scales with the difficulty of the zone
    // the creature is standing in (FUN_1008467c maps a level name to
    // 1..21), which the engine reads off the live level. main.cpp's
    // zone-load block sets this before any creature script runs; a test
    // that loads a zone directly has to say so itself.
    stack.RequestZoneChange(zoneName);

    bool ok = true;
    skExecutableContext loadCtxt(&interpreter);
    std::unique_ptr<sk_bindings::MonsterExecutable> rat;
    try {
        rat = std::make_unique<sk_bindings::MonsterExecutable>(skString(fullPath.c_str()), loadCtxt,
                                                                 &strings, stack.player(), stack);
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
    // M43 -- CORRECTED, and the correction is the point of this block now.
    // These are NOT azra_rat.s's own SetAttack(3)/SetDefense(4)/
    // SetDamageMin(3)/SetDamageMax(6)/SetArmorValue(2)/SetMaxHealth(12).
    // The last line of that Init() is `SetMob(4)`, and the real handler
    // (dispatcher 0x10084924 case 0) is a four-tier stat template scaled by
    // the difficulty of the zone the creature is in -- it overwrites every
    // one of the values the lines above it set. azra's difficulty is 1
    // (FUN_1008467c's table, in the game's own progression order), so tier
    // 4 gives:
    //
    //   attack    = (L>>1)*8 + 8  =  8      (script said 3)
    //   defense   = L*2 + 60      =  62     (script said 4)
    //   damageMin = (L>>1)*3 + 6  =  6      (script said 3)
    //   damageMax = (L>>1)*4 + 8  =  8      (script said 6)
    //   armour    = 1                       (script said 2)
    //   maxHealth = L*8 + 15      =  23     (script said 12)
    //
    // The script's own literals turn out to be dead weight for any creature
    // that ends with SetMob -- which is most of them. See
    // MonsterExecutable::ApplyMobTemplate for the full four tiers and for
    // why this got found (a creature's willpower, and therefore its ability
    // to land a spell at all, comes from here).
    check("attack (SetMob(4) template, not the script's 3)", rat->attack(), 8);
    check("defense (not the script's 4)", rat->defense(), 62);
    check("damageMin (not the script's 3)", rat->damageMin(), 6);
    check("damageMax (not the script's 6)", rat->damageMax(), 8);
    check("armorValue (every tier pins it to 1)", rat->armorValue(), 1);
    check("maxHealth (not the script's 12)", rat->maxHealth(), 23);
    check("currentHealth (SetMob ends with SetHealth(max))", rat->currentHealth(), 23);
    // ...and the fields SetMob does *not* touch still come from the script.
    check("expWorth (untouched by the template)", rat->expWorth(), 40);
    check("level (untouched -- azra_rat.s never calls SetLevel)", rat->level(), 0);
    // M31: chaseRadius() now returns real world units, not the raw script
    // value. SetChaseRadius/SetAttackRange are compared against
    // `(dx^2 + dy^2) / 256` (the decompiled FUN_100683d4), so the script's
    // 18000 stands for 16*sqrt(18000) == 2147 world units == 8.4 tiles --
    // not the 70 tiles a linear reading gives, which is what made aggro
    // look unlimited. arat.s/azra_rat.s never call SetAttackRange, so the
    // stand-off is the real constructor default 0x6a4 -> 659.7 units.
    // (Both checks truncate the float, hence 2146/659 not 2147/660.)
    check("chaseRadius (18000 -> world units)", static_cast<int>(rat->chaseRadius()), 2146);
    check("attackRange (default 0x6a4 -> world units)", static_cast<int>(rat->attackRange()), 659);
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
    if (!rat->alive() || rat->currentHealth() != 23 - 5) {
        std::printf("m12_combat_smoke: FAILED -- after 5 damage, health=%d alive=%s\n",
                    rat->currentHealth(), rat->alive() ? "true" : "false");
        ok = false;
    }
    rat->ApplyDamage(23 - 5);
    if (rat->alive() || rat->currentHealth() != 0) {
        std::printf("m12_combat_smoke: FAILED -- after lethal damage, health=%d alive=%s\n",
                    rat->currentHealth(), rat->alive() ? "true" : "false");
        ok = false;
    }
    rat->InvokeOnKilled();  // must not throw/crash -- see its own comment
    std::printf("InvokeOnKilled() returned normally: OK\n");

    // 6. The melee model itself. M43 replaced this port's invented
    // `clamp(50 + (attack - defense) * 5, 10, 95)` with the real gate,
    // FUN_1004b620:
    //
    //   total  = attack + defense;  if (total == 0) total = 1;
    //   chance = (total == attack) ? 0x80 : (attack << 16) / (total << 8);
    //   hit    = (rand % 0x100) <= chance;
    //
    // -- the same ratio model as the magic gate, and it had to be found
    // because SetMob's now-real defense values (62 for the first rat in the
    // game) are meaningless to a linear formula.
    {
        using sk_bindings::MeleeHitChance;
        check("hit chance is attack*256/(attack+defense) (10 vs 30)", MeleeHitChance(10, 30), 64);
        check("...and an even matchup is exactly half", MeleeHitChance(20, 20), 128);
        // The zero-defense case is the real function's own substitution:
        // the division would give 0x100 (a certainty) and it writes 0x80
        // instead. Deliberate, and the opposite of the magic gate's
        // zero-resistance certainty.
        check("a zero-defense target is capped at half, not made certain", MeleeHitChance(10, 0),
              0x80);
        check("...and two zeroes do not divide by zero", MeleeHitChance(0, 0), 0);
    }
    // Bounds sanity on the whole roll -- not a statistical test (avoids
    // flakiness), just that it never produces a nonsensical result. The
    // real spread is exclusive at the top (`rand % (max - min)`), so a 3..6
    // weapon never rolls 6, and the defender's full armour comes off.
    bool rollsOk = true;
    bool sawHit = false;
    for (int i = 0; i < 500; ++i) {
        int dmg = sk_bindings::RollDamage(/*attackerAttack=*/10, /*defenderDefense=*/2,
                                           /*defenderArmor=*/1, /*dmgMin=*/3, /*dmgMax=*/6);
        if (dmg < 0 || dmg > 5 - 1) rollsOk = false;
        if (dmg > 0) sawHit = true;
    }
    std::printf("RollDamage bounds (500 rolls, attacker favored): %s\n",
                rollsOk && sawHit ? "OK" : "FAILED");
    if (!rollsOk || !sawHit) ok = false;

    std::printf("\nm12_combat_smoke: %s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
