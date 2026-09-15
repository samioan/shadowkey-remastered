// M98 smoke test: the stats DoDamage's attacker terms.
//
// FUN_10049e78, between its Sanctuary gate and its health write:
//
//   victim->vtable[0x1c](victim, &dmg, attacker)   the Knight (FUN_10044950)
//   if (dmg < 0) dmg = 0
//   if (attacker->Strength > 4) dmg += (Strength + StrengthBonus) / 5
//   if (owner is the player && class == 0) FUN_10044910   the Assassin
//   if (victim kind 9 && owner is a creature && p5 == 1) return   snowray
//
// plus the two gates this milestone found beside them: FUN_100458e4's entry
// refusal (invulnerable / kind 4 / kind 9 refuse a whole spell) and
// FUN_1005f3c8's creature arm, which damages creatures only. What this
// checks:
//
//   1. The arithmetic of each piece, against hand-worked values.
//   2. Strength and the Assassin through ApplyDamage, including their order
//      and the absorbed swing that still lands the strength term.
//   3. The Knight's roll, by its distribution.
//   4. Snowray: the stats gate's four conditions, then the real sites -- an
//      archer's arrow, a creature's spell, and DoAttackRoll's refusal of a
//      status effect.
//   5. The two smaller gates: DoAttackRoll's entry, the creature impact.
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "simkin_bindings/actor_stats.h"
#include "simkin_bindings/arrow_projectile.h"
#include "simkin_bindings/character_progression.h"
#include "simkin_bindings/combat.h"
#include "simkin_bindings/effects.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/spell_projectile.h"
#include "simkin_bindings/stats_damage.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"

namespace sk_b = sk_bindings;
using Monster = sk_b::MonsterExecutable;
using Item = sk_b::ItemExecutable;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("  %-92s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    if (!ok) ++g_failures;
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::srand(98);
    const char* scriptRoot =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";
    const std::string root = scriptRoot;

    sk::StringTable strings;
    if (!strings.Load(root + "/stringtable.eng")) {
        std::printf("m98_attacker_terms_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(root)) {
        std::printf("m98_attacker_terms_smoke: FAILED to load entities.txt\n");
        return 1;
    }

    skInterpreter interpreter;
    sk_b::MenuStack stack(root, interpreter, &strings);
    stack.level().SetEntityTypes(&entityTypes);
    sk_b::PlayerExecutable& player = stack.player();

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
    auto one = [](skRValue v) {
        skRValueArray a;
        a.append(v);
        return a;
    };
    auto loadMonster = [&](const std::string& rel) -> std::unique_ptr<Monster> {
        skExecutableContext loadCtxt(&interpreter);
        try {
            auto m = std::make_unique<Monster>(skString((root + "/" + rel).c_str()), loadCtxt,
                                               &strings, player, stack);
            call(m.get(), "Init", one(skRValue(0)));
            return m;
        } catch (skParseException& e) {
            std::printf("   (PARSE ERROR loading %s: %s)\n", rel.c_str(), e.toString().ptr());
        } catch (skRuntimeException& e) {
            std::printf("   (RUNTIME ERROR loading %s: %s)\n", rel.c_str(), e.toString().ptr());
        }
        return nullptr;
    };
    auto loadItem = [&](const std::string& rel) -> std::unique_ptr<Item> {
        skExecutableContext loadCtxt(&interpreter);
        try {
            auto item = std::make_unique<Item>(skString((root + "/" + rel).c_str()), loadCtxt, stack);
            call(item.get(), "Init", one(skRValue(0)));
            return item;
        } catch (skParseException& e) {
            std::printf("   (PARSE ERROR loading %s: %s)\n", rel.c_str(), e.toString().ptr());
        } catch (skRuntimeException& e) {
            std::printf("   (RUNTIME ERROR loading %s: %s)\n", rel.c_str(), e.toString().ptr());
        }
        return nullptr;
    };
    // A rat with 1000 health, so a hit's size can be read off what it lost.
    auto sandbag = [&]() {
        std::unique_ptr<Monster> m = loadMonster("monsters/Azra_Rat.s");
        if (m) call(m.get(), "SetHealth", one(skRValue(1000)));
        return m;
    };
    auto lost = [](Monster& m, int amount, sk_b::SpellActor* attacker, bool ranged) {
        const int before = m.actorHealth();
        m.ApplyDamage(amount, attacker, ranged);
        return before - m.actorHealth();
    };
    auto setStrength = [&](int strength, int bonus) {
        call(&player, "SetStrength", one(skRValue(strength)));
        *player.EffectStatSlot(sk_b::kEffectStatStrength) = bonus;
    };
    auto healPlayer = [&]() {
        *player.EffectStatSlot(sk_b::kEffectStatMaxHealth) = 1000;
        player.SetActorHealth(1000);
    };

    // ---------------------------------------------------------------
    // Part 1: the arithmetic.
    // ---------------------------------------------------------------
    std::printf("\n== Part 1: each term's arithmetic ==\n");
    Check(sk_b::StrengthDamageTerm(4, 100) == 0 && sk_b::StrengthDamageTerm(5, 0) == 1,
          "Strength 4 adds nothing whatever the bonus; Strength 5 adds 1 (`4 < Strength`)");
    Check(sk_b::StrengthDamageTerm(50, 0) == 10 && sk_b::StrengthDamageTerm(50, 4) == 10 &&
              sk_b::StrengthDamageTerm(50, 5) == 11,
          "50 adds 10; the bonus is summed before the division (50+4 -> 10, 50+5 -> 11)");
    Check(sk_b::StrengthDamageTerm(5, -20) == -3 && sk_b::StrengthDamageTerm(6, -10) == 0,
          "the division truncates toward zero: -15/5 = -3, -4/5 = 0 (not -1)");
    Check(sk_b::AssassinDamage(100, 1) == 114 && sk_b::AssassinDamage(100, 2) == 119 &&
              sk_b::AssassinDamage(10, 1) == 11 && sk_b::AssassinDamage(0, 9) == 0,
          "Assassin: 100 -> 114 at rank 1 (38/256), 119 at rank 2 (51/256); 10 -> 11");
    Check(sk_b::KnightHalvesAbove(1, 10) == 8 && sk_b::KnightHalvesAbove(10, 10) == 50,
          "Knight threshold: rank 1 vs 10 damage = 8, rank 10 vs 10 = 50");
    Check(sk_b::KnightHalvedDamage(10) == 5 && sk_b::KnightHalvedDamage(7) == 4 &&
              sk_b::KnightHalvedDamage(1) == 1,
          "the halving rounds up: 10 -> 5, 7 -> 4, 1 -> 1");

    // ---------------------------------------------------------------
    // Part 2: Strength and the Assassin, through ApplyDamage.
    // ---------------------------------------------------------------
    std::printf("\n== Part 2: the attacker's own terms ==\n");
    {
        call(&player, "ChooseCharacter", one(skRValue(1)));  // Barbarian: no class term
        std::unique_ptr<Monster> bag = sandbag();
        std::unique_ptr<Monster> archer = loadMonster("monsters/archer.s");
        Check(bag && archer && bag->actorHealth() == 1000, "a 1000-health rat and an archer load");
        if (bag && archer) {
            setStrength(50, 0);
            Check(lost(*bag, 7, &player, false) == 17,
                  "the player at Strength 50 hits for 7 + 10 = 17");
            setStrength(4, 0);
            Check(lost(*bag, 7, &player, false) == 7, "at Strength 4, exactly 7");
            setStrength(50, 5);
            Check(lost(*bag, 7, &player, false) == 18,
                  "a +5 strength bonus (what an AddEffect Strength moves) counts: 7 + 11");
            setStrength(50, 0);
            Check(lost(*bag, 0, &player, false) == 10,
                  "a swing the armour fully absorbed (0) still lands the strength term");
            Check(lost(*bag, -6, &player, false) == 10,
                  "a negative remainder clamps to 0 *before* the term, not after");
            Check(lost(*bag, 7, archer.get(), false) == 7,
                  "a creature's Strength is 0: an archer's 7 is 7");
            Check(lost(*bag, 7, nullptr, false) == 7, "an unsourced 7 is 7");

            call(&player, "ChooseCharacter", one(skRValue(sk_b::kClassAssassin)));
            call(&player, "SetSpecialAbility", one(skRValue(1)));
            setStrength(4, 0);
            Check(lost(*bag, 10, &player, false) == 11, "an Assassin at rank 1, no Strength: 10 -> 11");
            setStrength(50, 0);
            Check(lost(*bag, 10, &player, false) == 22,
                  "with Strength 50: (10 + 10) * 38/256 -> 22 -- Strength first, then the bonus");
            call(&player, "SetSpecialAbility", one(skRValue(2)));
            Check(sk_b::ResolveStatsDamage(*bag, 90, &player, false).damage == 119,
                  "rank 2 on a 90-point hit: (90 + 10) -> 119");
            call(&player, "SetSpecialAbility", one(skRValue(1)));
        }
    }

    // ---------------------------------------------------------------
    // Part 3: the Knight.
    // ---------------------------------------------------------------
    std::printf("\n== Part 3: FUN_10044950, the Knight's roll ==\n");
    {
        std::unique_ptr<Monster> archer = loadMonster("monsters/archer.s");
        if (archer) {
            auto halvedShare = [&](int rank, int damage, int trials) {
                call(&player, "SetSpecialAbility", one(skRValue(rank)));
                int halved = 0;
                for (int i = 0; i < trials; ++i) {
                    const int d = sk_b::ResolveStatsDamage(player, damage, archer.get(), false).damage;
                    if (d == sk_b::KnightHalvedDamage(damage)) ++halved;
                }
                return static_cast<double>(halved) / trials;
            };
            call(&player, "ChooseCharacter", one(skRValue(1)));
            const double barbarian = halvedShare(1, 10, 2000);
            Check(barbarian == 0.0, "a Barbarian never halves");

            call(&player, "ChooseCharacter", one(skRValue(sk_b::kClassKnight)));
            const double rank1 = halvedShare(1, 10, 20200);
            const double rank10 = halvedShare(10, 10, 20200);
            std::printf("     rank 1: %.3f halved (92/101 = 0.911), rank 10: %.3f (50/101 = 0.495)\n",
                        rank1, rank10);
            Check(rank1 > 0.89 && rank1 < 0.93,
                  "a rank-1 Knight halves a 10-point hit when rand(0,100) > 8: 92 in 101");
            Check(rank10 > 0.47 && rank10 < 0.52,
                  "a rank-10 Knight only 50 in 101 -- the chance falls as the rank rises");

            call(&player, "SetSpecialAbility", one(skRValue(1)));
            bool zeroStays = true;
            for (int i = 0; i < 200; ++i) {
                if (sk_b::ResolveStatsDamage(player, 0, archer.get(), false).damage != 0) {
                    zeroStays = false;
                }
            }
            Check(zeroStays, "a zero hit is not rolled for");

            // The Knight's slot runs before the strength term. A player
            // victim of its own Strength is not a thing the game does, but it
            // is the one way to put both terms on one hit.
            setStrength(50, 0);
            bool ordered = true;
            for (int i = 0; i < 500; ++i) {
                const int d = sk_b::ResolveStatsDamage(player, 10, &player, false).damage;
                if (d != 15 && d != 20) ordered = false;
            }
            Check(ordered, "halve first, then add Strength: a 10 is 15 or 20, never 10");

            healPlayer();
            player.TickHurtTimer(1000);
            const int before = player.health();
            player.ApplyDamage(10, archer.get(), false);
            const int took = before - player.health();
            Check((took == 5 || took == 10) && player.hurtTimer() > 0,
                  "through ApplyDamage: the Knight takes 5 or 10, and the frame flashes either way");
            call(&player, "ChooseCharacter", one(skRValue(1)));
        }
    }

    // ---------------------------------------------------------------
    // Part 4: snowray.
    // ---------------------------------------------------------------
    std::printf("\n== Part 4: snowray (periodic kind 9) ==\n");
    {
        std::unique_ptr<Monster> archer = loadMonster("monsters/archer.s");
        if (archer) {
            setStrength(50, 0);
            healPlayer();
            skRValueArray ward;
            ward.append(skRValue(9));
            ward.append(skRValue(7680));
            call(&player, "SetSpellEffect", ward);  // snowray_powder.s's OnUse
            Check(player.actorStats().periodicKind() == sk_b::ActorStats::kPeriodicSnowrayWard,
                  "SetSpellEffect(9, 7680) arms kind 9");

            int before = player.health();
            player.TickHurtTimer(1000);
            player.ApplyDamage(12, archer.get(), /*ranged=*/true);
            Check(player.health() == before && player.hurtTimer() > 0,
                  "a creature's ranged hit is refused -- after FUN_10044814 has flashed the frame");
            player.ApplyDamage(12, archer.get(), /*ranged=*/false);
            Check(before - player.health() == 12, "a creature's melee (p5 = 0) lands in full");
            before = player.health();
            player.ApplyDamage(12, nullptr, /*ranged=*/true);
            Check(before - player.health() == 12, "an unsourced ranged hit has no owner to test: lands");
            Check(!sk_b::ResolveStatsDamage(player, 12, &player, true).refused,
                  "a player source is not a creature: not refused");

            // The real sites. An archer's arrow, the general sweep (p5 = 1).
            auto openField = [](int, int, int) {
                sk_b::ArrowCell cell;
                cell.onMap = true;
                cell.floorHeight = 0;
                return cell;
            };
            auto volley = [&]() {
                healPlayer();
                int landed = 0;
                for (int i = 0; i < 50; ++i) {
                    sk_b::ArrowProjectile shot = sk_b::SpawnArrowProjectile(
                        archer.get(), /*ownerIsPlayer=*/false, 0x80, 0x80, 0, 0, 0, 0,
                        sk_b::kBowProjectileTypeId, /*damage=*/5, /*attackSkill=*/100000);
                    std::vector<sk_b::ArrowTarget> targets{{&player, 0x80, 0x180}};
                    for (int t = 0; t < 4 && shot.alive; ++t) {
                        const sk_b::ArrowImpact hit = sk_b::TickArrowProjectile(shot, openField, targets);
                        if (hit.hit) ++landed;
                    }
                }
                return landed;
            };
            int landed = volley();
            Check(landed > 0 && player.health() == 1000,
                  "an archer's arrows that land (" + std::to_string(landed) +
                      ") take nothing from a warded player");
            player.actorStats().SetPeriodic(sk_b::ActorStats::kPeriodicNone, 0);
            landed = volley();
            Check(landed > 0 && player.health() < 1000,
                  "the same volley unwarded does damage (control)");

            // A creature's spell: DoAttackRoll refuses it outright, so the
            // poison is not applied either -- a status the stats gate alone
            // would have let through.
            std::unique_ptr<Item> poison = loadItem("spells/poison.s");
            if (poison) {
                poison->SetSpellOwner(archer.get());
                bool poisonedUnwarded = false;
                for (int i = 0; i < 300 && !poisonedUnwarded; ++i) {
                    player.actorStats().SetEffectFlagsRaw(0);
                    poison->InvokeHitTarget(&player);
                    if (player.actorStats().effectFlags() & sk_b::ActorStats::kEffectFlagPoison) {
                        poisonedUnwarded = true;
                    }
                }
                player.actorStats().SetEffectFlagsRaw(0);
                player.actorStats().SetPeriodic(sk_b::ActorStats::kPeriodicSnowrayWard, 7680);
                bool poisonedWarded = false;
                for (int i = 0; i < 300; ++i) {
                    poison->InvokeHitTarget(&player);
                    if (player.actorStats().effectFlags() & sk_b::ActorStats::kEffectFlagPoison) {
                        poisonedWarded = true;
                    }
                }
                Check(poisonedUnwarded && !poisonedWarded,
                      "a creature's Poison lands unwarded, and never through the ward");
                player.actorStats().SetPeriodic(sk_b::ActorStats::kPeriodicNone, 0);
            }
            // Whoever casts: the entry gate has no source test.
            std::unique_ptr<Item> blaze = loadItem("blaze.s");
            std::unique_ptr<Monster> bag = sandbag();
            if (blaze && bag) {
                bag->actorStats().SetPeriodic(sk_b::ActorStats::kPeriodicSnowrayWard, 7680);
                for (int i = 0; i < 100; ++i) blaze->InvokeHitTarget(bag.get());
                Check(bag->actorHealth() == 1000,
                      "DoAttackRoll refuses the player's own Blaze at a warded creature too");
            }
        }
    }

    // ---------------------------------------------------------------
    // Part 5: the two smaller gates.
    // ---------------------------------------------------------------
    std::printf("\n== Part 5: DoAttackRoll's entry, and the creature impact ==\n");
    {
        std::unique_ptr<Item> poison = loadItem("spells/poison.s");
        std::unique_ptr<Monster> control = sandbag();
        std::unique_ptr<Monster> sanctuary = sandbag();
        std::unique_ptr<Monster> essential = sandbag();
        if (poison && control && sanctuary && essential) {
            sanctuary->actorStats().SetPeriodic(sk_b::ActorStats::kPeriodicSanctuaryTimer, 7680);
            call(essential.get(), "SetInvulnerable", one(skRValue(true)));
            auto poisoned = [](Monster& m) {
                return (m.actorStats().effectFlags() & sk_b::ActorStats::kEffectFlagPoison) != 0;
            };
            for (int i = 0; i < 300; ++i) {
                poison->InvokeHitTarget(control.get());
                poison->InvokeHitTarget(sanctuary.get());
                poison->InvokeHitTarget(essential.get());
            }
            Check(poisoned(*control), "the player's Poison takes on an ordinary rat (control)");
            Check(!poisoned(*sanctuary), "never on a rat under Sanctuary (`+0x7c == 4`)");
            Check(!poisoned(*essential), "never on an invulnerable one (`+0x1e2`)");
        }

        // FUN_1005f3c8: the creature arm damages creatures only.
        std::unique_ptr<Monster> caster = loadMonster("monsters/archer.s");
        std::unique_ptr<Monster> bag = sandbag();
        if (caster && bag) {
            auto impact = [&](sk_b::SpellActor* owner, sk_b::SpellActor* actor, skiExecutable* script) {
                sk_b::SpellProjectile shot = sk_b::SpawnSpellProjectile(
                    nullptr, owner, 0, 5000, 0, sk_b::EngineYawFromPortYaw(0.0f), 0, 2, 50);
                sk_b::ProjectileTarget target;
                target.actor = actor;
                target.script = script;
                target.x = 256;
                target.y = 5000;
                target.halfWidth = sk_b::kProjectileRadius;
                target.halfDepth = sk_b::kProjectileRadius;
                std::vector<sk_b::ProjectileTarget> targets{target};
                auto never = [](int, int) { return false; };
                bool hit = false;
                for (int i = 0; i < 20 && shot.alive; ++i) {
                    if (sk_b::TickSpellProjectile(shot, 200, 200, never, targets).hit) hit = true;
                }
                return hit;
            };
            healPlayer();
            const bool struckPlayer = impact(caster.get(), &player, &player);
            Check(struckPlayer && player.health() == 1000,
                  "a creature's 50-point impact reaches the player and does nothing to it");
            const bool struckBag = impact(caster.get(), bag.get(), bag.get());
            Check(struckBag && bag->actorHealth() == 950, "the same impact on a creature takes 50");
            std::unique_ptr<Monster> bag2 = sandbag();
            setStrength(50, 0);
            if (bag2 && impact(&player, bag2.get(), bag2.get())) {
                Check(bag2->actorHealth() == 940, "the player's impact takes 50 + its Strength 10");
            }
        }

        // The melee roll now says whether it connected.
        const sk_b::PlayerMeleeResult absorbed =
            sk_b::RollPlayerMeleeDamage(1000, 1, 1000, 5, 9, false, false, 0, 0, false, 0);
        Check(absorbed.hit && absorbed.damage == 0,
              "a certain hit (255/256 <= roll) into 1000 armour: hit, 0 damage -- still a call");
    }

    std::printf("\nm98_attacker_terms_smoke: %d/%d checks passed\n", g_checks - g_failures,
                g_checks);
    return g_failures == 0 ? 0 : 1;
}
