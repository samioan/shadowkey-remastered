// M137: the to-hit ratings and the dodge -- FUN_10047f64 (attack),
// FUN_1004801c (defense) and FUN_1004b620's second test through the
// defender's stats vtable +0x14 (FUN_1004b774, and the player's
// FUN_100449f4). See combat.h and player_executable.h.
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "assets/string_table.h"
#include "simkin_bindings/combat.h"
#include "simkin_bindings/effects.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/player_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "world/entity_types.h"
#include "world/zone.h"

namespace sk_b = sk_bindings;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    std::printf("  %-86s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    ++g_checks;
    if (!ok) ++g_failures;
}

void Call(skiExecutable* target, const char* name, int arg, skInterpreter& interpreter) {
    skRValueArray args;
    args.append(skRValue(arg));
    skRValue ret;
    skExecutableContext ctxt(&interpreter);
    target->method(skString(name), args, ret, ctxt);
}

int CallInt(skiExecutable* target, const char* name, skInterpreter& interpreter) {
    skRValueArray args;
    skRValue ret;
    skExecutableContext ctxt(&interpreter);
    target->method(skString(name), args, ret, ctxt);
    return ret.intValue();
}

double DodgeRate(const sk_b::DodgeStats& d, int rollPlusChance, int trials = 20000) {
    int dodged = 0;
    for (int i = 0; i < trials; ++i) dodged += sk_b::RollDodge(d, rollPlusChance) ? 1 : 0;
    return static_cast<double>(dodged) / trials;
}

}  // namespace

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";
    std::srand(1337);

    std::printf("\n-- 1. the dodge (FUN_1004b774 / FUN_100449f4) --\n");
    {
        sk_b::DodgeStats plain;  // Luck 40, not a Khajiit
        Check(DodgeRate(plain, 200) == 0.0, "Luck 40 never dodges -- the test is `luck > 40`");
        sk_b::DodgeStats lucky;
        lucky.luck = 50;
        const double r = DodgeRate(lucky, 200);
        Check(r > 0.004 && r < 0.02,
              "Luck 50 against roll+chance 200: percent rounds to 0, so only RandomRange(0,100) "
              "== 0 dodges (~1%), got " + std::to_string(r));
        sk_b::DodgeStats khajiit;
        khajiit.khajiitRank = 10;
        const double k = DodgeRate(khajiit, 200);
        Check(k > 0.08 && k < 0.12,
              "a Khajiit's race-ability rank is its evasion percent (rank 10 ~ 10%), got " +
                  std::to_string(k));
        khajiit.khajiitRank = 250;
        const double kCap = DodgeRate(khajiit, 200);
        Check(kCap > 0.97, "...capped at rank 100, got " + std::to_string(kCap));
    }

    sk::StringTable strings;
    if (!strings.Load(std::string(scriptRoot) + "/stringtable.eng")) {
        std::printf("m137_to_hit_smoke: no game data\n");
        return 1;
    }
    sk::EntityTypeTable entityTypes;
    entityTypes.Load(scriptRoot);
    skInterpreter interpreter;
    sk_b::MenuStack stack(scriptRoot, interpreter, &strings);
    stack.level().SetEntityTypes(&entityTypes);
    sk_b::PlayerExecutable& player = stack.player();

    std::printf("\n-- 2. the player's ratings (FUN_10047f64 / FUN_1004801c) --\n");
    {
        Check(player.baseAttack() == 0 && player.baseDefense() == 0,
              "a fresh player's base attack and defense are the stats constructor's 0");
        sk_b::ApplyEffectStat(player, sk_b::kEffectStatStrengthProper, sk_b::kOpSet, 40);
        sk_b::ApplyEffectStat(player, sk_b::kEffectStatAgility, sk_b::kOpSet, 50);
        Call(&player, "ChooseRace", 5, interpreter);  // Nord
        Call(&player, "ChooseCharacter", 2, interpreter);  // Battlemage
        Check(player.actorAttackRating() == 80, "attack = 0 + 2 * Strength 40 = 80");
        Check(player.actorDefenseRating() == 10, "defense = 0 + Agility 50 / 5 = 10");
        Check(CallInt(&player, "GetAttack", interpreter) == 80 &&
                  CallInt(&player, "GetDefense", interpreter) == 10,
              "GetAttack/GetDefense (stats binding 0x40/0x41) answer the same two numbers");

        Call(&player, "ChooseRace", 2, interpreter);  // Dark Elf
        Call(&player, "SetSpecialAbility", 8, interpreter);
        Check(player.actorAttackRating() == 82, "a Dark Elf adds specialAbility / 4 (8 -> +2)");
        Call(&player, "ChooseRace", 5, interpreter);

        Call(&player, "ChooseCharacter", 5, interpreter);  // Rogue
        Check(player.actorDefenseRating() == 18, "a Rogue adds specialAbility to defense (+8)");
        Call(&player, "ChooseCharacter", 2, interpreter);

        sk_b::ApplyEffectStat(player, sk_b::kEffectStatStrengthProper, sk_b::kOpSet, 0);
        Check(player.actorAttackRating() == 0, "no Strength, no bonus -- the term is `> 0` gated");
        sk_b::ApplyEffectStat(player, sk_b::kEffectStatStrengthProper, sk_b::kOpSet, 40);
        sk_b::ApplyEffectStat(player, sk_b::kEffectStatAttack, sk_b::kOpIncrement, -10);
        Check(player.actorAttackRating() == 70,
              "a -10 debuff on the 0 base still comes off the rating (80 -> 70)");
        sk_b::ApplyEffectStat(player, sk_b::kEffectStatAttack, sk_b::kOpSet, 0);
        sk_b::ApplyEffectStat(player, sk_b::kEffectStatStrengthProper, sk_b::kOpSet, 0);

        Call(&player, "ChooseRace", 4, interpreter);  // Khajiit
        Call(&player, "SetRaceAbility", 3, interpreter);
        Check(player.actorDodgeStats().khajiitRank == 3, "a Khajiit dodges on its race ability");
        Call(&player, "ChooseRace", 5, interpreter);
        Check(player.actorDodgeStats().khajiitRank == -1, "...and nobody else does");
    }

    std::printf("\n-- 3. a creature's ratings --\n");
    {
        skExecutableContext ctxt(&interpreter);
        std::string full = std::string(scriptRoot) + "/monsters/azra_rat.s";
        auto rat = std::make_unique<sk_b::MonsterExecutable>(skString(full.c_str()), ctxt,
                                                             &strings, player, stack);
        skRValueArray args;
        args.append(skRValue(0));
        skRValue ret;
        skExecutableContext c2(&interpreter);
        rat->method(skString("Init"), args, ret, c2);
        Check(rat->actorAttackRating() == rat->baseAttack() + 2,
              "attack + 2: the creature constructor's Strength is 1");
        Check(rat->actorDefenseRating() == rat->baseDefense(),
              "defense + 0: its Agility is 1, and 1 / 5 is 0");
        Check(rat->actorDodgeStats().luck == 40 && rat->actorDodgeStats().khajiitRank == -1,
              "Luck 40, so a creature never dodges");
    }

    std::printf("\n-- 4. the creature melee reach (FUN_10082004's geometry stop) --\n");
    {
        sk::Zone zone;
        if (!zone.Load(scriptRoot, "glaciercrawl")) {
            Check(false, "glaciercrawl loads");
        } else {
            // An open tile, a blocked-not-wall tile, an open tile, in a row.
            int found = 0, sawThrough = 0, reachedThrough = 0;
            for (int y = 1; y < zone.height() - 1; ++y) {
                for (int x = 1; x + 2 < zone.width() - 1; ++x) {
                    const sk::ZmpCell& a = zone.CellAt(x, y);
                    const sk::ZmpCell& m = zone.CellAt(x + 1, y);
                    const sk::ZmpCell& b = zone.CellAt(x + 2, y);
                    if (a.IsWall() || a.IsBlocked() || b.IsWall() || b.IsBlocked()) continue;
                    if (m.IsWall() || !m.IsBlocked()) continue;
                    ++found;
                    const float ax = (x + 0.5f) * sk::kTileScale;
                    const float ay = (y + 0.5f) * sk::kTileScale;
                    const float bx = (x + 2.5f) * sk::kTileScale;
                    if (zone.HasLineOfSight(ax, ay, bx, ay)) ++sawThrough;
                    if (zone.HasMeleeReach(ax, ay, bx, ay)) ++reachedThrough;
                }
            }
            Check(found > 0, "glaciercrawl has blocked-not-wall cells between two open ones: " +
                                 std::to_string(found));
            Check(sawThrough == found, "the old wall-only test saw through every one of them");
            Check(reachedThrough == 0, "the reach is stopped by every one of them");
            const float sx = static_cast<float>(zone.playerStartX);
            const float sy = static_cast<float>(zone.playerStartY);
            Check(zone.HasMeleeReach(sx, sy, sx, sy), "a creature in the target's own tile reaches");
        }
    }

    std::printf("\nm137_to_hit_smoke: %d/%d checks passed -- %s\n", g_checks - g_failures,
                g_checks, g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
