// M97 smoke test: a kill pays its experience.
//
// The engine's chain, and nothing else in the image awards a kill:
//
//   FUN_10049e78  stats->vtable[0x10](stats, damage, attackerStats, p4, p5)
//                 -- at health <= 0: stats->vtable[0x28](stats, attacker, p4)
//   0x100a4314    the creature's slot 0x28: `this -= 0x224; b FUN_10083c04`
//   FUN_10083c04  if (attacker && monster->+0x2e8 == 0)
//                     attacker->vtable[0x20](attacker, stats->expWorth)
//
// So the port needed two things: the attacker carried into the damage
// entry point (killer() records the fatal blow's), and the award in the
// death handler (PayKillExperience). What this checks:
//
//   1. The killer record: only the fatal hit sets it, a corpse keeps it,
//      and every refusal (invulnerable, Sanctuary) leaves it unset.
//   2. The award: the player is paid expWorth as it stands at death, level
//      arithmetic and all; no killer, no payment.
//   3. A creature as the killer: FUN_1004a104's other arm, `(level+1)*1000`
//      and a no-op level-up.
//   4. Every damage source, through the port's real code, names the
//      attacker the engine's call site names -- including the three that
//      name nobody: a script's DoDamage, a poison tick and a burn.
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "simkin_bindings/actor_stats.h"
#include "simkin_bindings/arrow_projectile.h"
#include "simkin_bindings/character_progression.h"
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
    std::printf("  %-90s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    if (!ok) ++g_failures;
}

// FUN_1004a104's player arm, transcribed independently of the port's own
// AddExperience, so part 2's levels are predicted rather than read back.
struct Model {
    int classId = 0;
    int level = 1;
    int experience = 0;
    void Award(int amount) {
        const int delta = static_cast<int16_t>(amount);
        if (sk_b::ExperienceToLeaveLevel(classId, level) < experience + delta) ++level;
        experience += delta;
    }
};

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
        std::printf("m97_kill_experience_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(root)) {
        std::printf("m97_kill_experience_smoke: FAILED to load entities.txt\n");
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
    // A fresh azra rat: SetExpWorth(40), and SetMob(4)'s 23 health.
    auto rat = [&]() { return loadMonster("monsters/Azra_Rat.s"); };

    call(&player, "ChooseCharacter", one(skRValue(1)));  // Barbarian: first threshold 900

    // ---------------------------------------------------------------
    // Part 1: the killer record.
    // ---------------------------------------------------------------
    std::printf("\n== Part 1: who landed the fatal blow ==\n");
    {
        std::unique_ptr<Monster> a = rat();
        std::unique_ptr<Monster> archer = loadMonster("monsters/archer.s");
        Check(a && archer, "monsters/Azra_Rat.s and monsters/archer.s load");
        if (a && archer) {
            Check(a->expWorth() == 40 && a->actorHealth() == 23,
                  "the rat is worth 40 and has 23 health");
            a->ApplyDamage(10, &player);
            Check(a->alive() && a->killer() == nullptr,
                  "a hit that does not kill names no killer, whoever landed it");
            a->ApplyDamage(13, archer.get());
            Check(!a->alive() && a->killer() == archer.get(),
                  "the fatal hit's attacker is the killer -- not the one who did the most");
            a->ApplyDamage(50, &player);
            Check(a->killer() == archer.get(), "a corpse takes no more hits, so the killer stays");
        }

        std::unique_ptr<Monster> b = rat();
        if (b) {
            call(b.get(), "SetInvulnerable", one(skRValue(true)));
            b->ApplyDamage(1000, &player);
            Check(b->alive() && b->killer() == nullptr,
                  "an invulnerable creature is neither killed nor credited");
        }

        std::unique_ptr<Monster> c = rat();
        if (c) {
            c->actorStats().SetPeriodic(sk_b::ActorStats::kPeriodicSanctuaryTimer, 10 << 8);
            c->ApplyDamage(1000, &player);
            Check(c->alive() && c->killer() == nullptr,
                  "under Sanctuary (FUN_10049e78's first line) the hit never happens at all");
        }
    }

    // ---------------------------------------------------------------
    // Part 2: the award, to the player.
    // ---------------------------------------------------------------
    std::printf("\n== Part 2: FUN_10083c04 pays the player ==\n");
    {
        Model model;
        model.classId = player.characterClass();
        const int startXp = player.experience();
        bool agree = true;
        int kills = 0;
        // 23 rats at 40: the 23rd crosses the Barbarian's 900 by 20.
        for (int i = 0; i < 23; ++i) {
            std::unique_ptr<Monster> r = rat();
            if (!r) {
                agree = false;
                break;
            }
            r->ApplyDamage(r->actorHealth(), &player);
            r->PayKillExperience();
            model.Award(40);
            ++kills;
            if (player.experience() != model.experience || player.level() != model.level) {
                agree = false;
            }
        }
        std::printf("     %d kills -> experience %d, level %d, %d point(s)\n", kills,
                    player.experience(), player.level(), player.levelUpPoints());
        Check(agree && kills == 23, "every kill pays 40, and the level follows FUN_1004a104");
        Check(player.experience() - startXp == 920 && player.level() == 2 &&
                  player.levelUpPoints() == 1,
              "23 rats: 920 experience, level 2 on the 23rd kill, one point to spend");

        // expWorth is read at death, so a running ExpWorth effect is paid.
        std::unique_ptr<Monster> buffed = rat();
        if (buffed) {
            sk_b::AddEffect(*buffed, sk_b::kDurationTimed, sk_b::kEffectStatExpWorth,
                            sk_b::kOpIncrement, 25, 60);
            const int before = player.experience();
            buffed->ApplyDamage(1000, &player);
            buffed->PayKillExperience();
            Check(player.experience() - before == 65,
                  "a rat carrying a +25 ExpWorth effect pays 65 -- the worth as it stands at death");
        }

        // SetZone's share (M39) is an ordinary SetExpWorth, and is what pays.
        std::unique_ptr<Monster> share = rat();
        if (share) {
            // What main.cpp writes after a zone's Init(): azra.s's
            // SetZone(1, 2000), shared (for the sake of the number) over 11.
            share->SetExpWorth(2000 / 11);
            const int before = player.experience();
            share->ApplyDamage(1000, &player);
            share->PayKillExperience();
            Check(player.experience() - before == 181,
                  "a SetZone share replaces the script's own worth, and is what pays (181)");
        }

        std::unique_ptr<Monster> nobody = rat();
        if (nobody) {
            const int before = player.experience();
            nobody->ApplyDamage(1000, nullptr);
            nobody->PayKillExperience();
            Check(!nobody->alive() && player.experience() == before,
                  "a death with no attacker pays nobody");
        }
    }

    // ---------------------------------------------------------------
    // Part 3: a creature as the killer.
    // ---------------------------------------------------------------
    std::printf("\n== Part 3: FUN_1004a104's creature arm ==\n");
    {
        std::unique_ptr<Monster> archer = loadMonster("monsters/archer.s");
        if (archer) {
            const int startLevel = archer->level();
            std::unique_ptr<Monster> victim = rat();
            if (victim) {
                victim->ApplyDamage(1000, archer.get());
                victim->PayKillExperience();
            }
            Check(archer->experience() == 40 && archer->level() == startLevel,
                  "an archer that kills a rat banks the rat's 40 itself");
            // No class table for a creature: the threshold is the opening
            // `(level + 1) * 1000`.
            const int threshold = (startLevel + 1) * 1000;
            archer->AddActorExperience(threshold - archer->experience());
            Check(archer->level() == startLevel,
                  "exactly (level + 1) * 1000 = " + std::to_string(threshold) +
                      " does not level (strictly greater)");
            archer->AddActorExperience(5000);
            Check(archer->level() == startLevel + 1,
                  "5000 more crosses several thresholds and still gains one level");
            const int before = archer->experience();
            archer->AddActorExperience(40000);
            Check(archer->experience() == before - 25536, "the award is a short: 40000 banks -25536");
        }
    }

    // ---------------------------------------------------------------
    // Part 4: every source names the engine's attacker.
    // ---------------------------------------------------------------
    std::printf("\n== Part 4: the attacker at each damage site ==\n");
    {
        // DoAttackRoll (FUN_100458e4): the caster. blaze.s is typeId 50, a
        // damage branch; an unowned spell is the player's.
        std::unique_ptr<Item> blaze = loadItem("blaze.s");
        std::unique_ptr<Monster> r1 = rat();
        if (blaze && r1) {
            for (int i = 0; i < 500 && r1->alive(); ++i) blaze->InvokeHitTarget(r1.get());
            Check(!r1->alive() && r1->killer() == &player,
                  "a player's spell (DoAttackRoll) that kills names the player");
        }
        std::unique_ptr<Item> creatureBlaze = loadItem("blaze.s");
        std::unique_ptr<Monster> caster = loadMonster("monsters/archer.s");
        std::unique_ptr<Monster> r2 = rat();
        if (creatureBlaze && caster && r2) {
            creatureBlaze->SetSpellOwner(caster.get());
            for (int i = 0; i < 500 && r2->alive(); ++i) creatureBlaze->InvokeHitTarget(r2.get());
            Check(!r2->alive() && r2->killer() == caster.get(),
                  "a creature's spell that kills names the creature");
        }

        // FUN_1005f3c8's second half: the projectile's own flat damage.
        std::unique_ptr<Monster> r3 = rat();
        if (r3) {
            sk_b::SpellProjectile shot = sk_b::SpawnSpellProjectile(
                nullptr, &player, 0, 5000, 0, sk_b::EngineYawFromPortYaw(0.0f), 0, 2, 50);
            sk_b::ProjectileTarget target;
            target.actor = r3.get();
            target.script = r3.get();
            target.x = 256;
            target.y = 5000;
            target.halfWidth = sk_b::kProjectileRadius;
            target.halfDepth = sk_b::kProjectileRadius;
            std::vector<sk_b::ProjectileTarget> targets{target};
            auto never = [](int, int) { return false; };
            for (int i = 0; i < 20 && shot.alive; ++i) {
                sk_b::TickSpellProjectile(shot, 200, 200, never, targets);
            }
            Check(!r3->alive() && r3->killer() == &player,
                  "a greater blaze's 50-point impact that kills names its caster");
        }

        // FUN_10007214: both of the arrow's hit passes.
        auto openField = [](int, int, int) {
            sk_b::ArrowCell cell;
            cell.onMap = true;
            cell.floorHeight = 0;
            return cell;
        };
        std::unique_ptr<Monster> r4 = rat();
        if (r4) {
            for (int i = 0; i < 50 && r4->alive(); ++i) {
                sk_b::ArrowProjectile shot = sk_b::SpawnArrowProjectile(
                    &player, /*ownerIsPlayer=*/true, 0x80, 0x80, 0, 0x40, 0, 0,
                    sk_b::kBowProjectileTypeId, /*damage=*/30, /*attackSkill=*/100000);
                std::vector<sk_b::ArrowTarget> targets{{r4.get(), 0x80, 0x180}};
                sk_b::TickArrowProjectile(shot, openField, targets);
            }
            Check(!r4->alive() && r4->killer() == &player,
                  "the player's arrow (the same-tile pass) that kills names the player");
        }
        std::unique_ptr<Monster> archer = loadMonster("monsters/archer.s");
        std::unique_ptr<Monster> r5 = rat();
        if (archer && r5) {
            for (int i = 0; i < 50 && r5->alive(); ++i) {
                sk_b::ArrowProjectile shot = sk_b::SpawnArrowProjectile(
                    archer.get(), /*ownerIsPlayer=*/false, 0x80, 0x80, 0, 0, 0, 0,
                    sk_b::kBowProjectileTypeId, /*damage=*/30, /*attackSkill=*/100000);
                std::vector<sk_b::ArrowTarget> targets{{r5.get(), 0x80, 0x180}};
                for (int t = 0; t < 4 && shot.alive; ++t) {
                    sk_b::TickArrowProjectile(shot, openField, targets);
                }
            }
            Check(!r5->alive() && r5->killer() == archer.get(),
                  "an archer's arrow (the general sweep) that kills names the archer");
        }

        // The three unsourced deaths.
        std::unique_ptr<Monster> r6 = rat();
        if (r6) {
            call(r6.get(), "DoDamage", one(skRValue(1000)));
            Check(!r6->alive() && r6->killer() == nullptr,
                  "a script's DoDamage (`vt[0x10](stats, n, 0, 0, 0)`) names nobody");
        }
        // M98: the player's hits now carry its Strength term, so these two
        // set-up hits take it off to leave the health they always left.
        const int strengthTerm =
            sk_b::StrengthDamageTerm(player.EffectStatValue(sk_b::kEffectStatStrengthProper),
                                     player.EffectStatValue(sk_b::kEffectStatStrength));
        std::unique_ptr<Monster> r7 = rat();
        if (r7) {
            r7->ApplyDamage(20 - strengthTerm, &player);  // 3 health left, and the player's hit
            r7->actorStats().ApplyEffectFlag(sk_b::ActorStats::kEffectFlagPoison, 3, 10);
            for (int i = 0; i < 40 && r7->alive(); ++i) r7->TickAi(64);
            const int before = player.experience();
            r7->PayKillExperience();
            Check(!r7->alive() && r7->killer() == nullptr && player.experience() == before,
                  "poisoned to death after the player's hit: the tick passes 0, nobody is paid");
        }
        std::unique_ptr<Monster> r8 = rat();
        if (r8) {
            r8->ApplyDamage(21 - strengthTerm, &player);
            r8->actorStats().ApplyBurn(1, 10);
            for (int i = 0; i < 40 && r8->alive(); ++i) r8->TickAi(64);
            Check(!r8->alive() && r8->killer() == nullptr,
                  "burned to death (kind 8 calls the death slot with (stats, 0, 0)): nobody");
        }
    }

    std::printf("\nm97_kill_experience_smoke: %d/%d checks passed\n", g_checks - g_failures,
                g_checks);
    return g_failures == 0 ? 0 : 1;
}
