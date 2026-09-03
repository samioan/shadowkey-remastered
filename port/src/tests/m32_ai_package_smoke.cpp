// M32 smoke test: the real AI package state machine, the flee/Fear timed
// package, the paralysis lockout, and the attack cadence timer.
//
// Everything asserted here is decompiled ground truth -- see
// docs/WORLD_MODEL.md's "The monster AI" section:
//   * the package values (FUN_10082224 / FUN_10084924 / FUN_100815e0)
//   * the timed-package helper FUN_10086b98(actor, package, duration),
//     which stores `duration << 8` and restores the saved package when it
//     expires
//   * the cadence accumulator monster+0x2c4: act only once it passes
//     0x100, then reset it to `rand & 0x1f`
//   * FUN_100458e4's status-effect dispatch, which selects the effect from
//     the spell entity's own entities.txt typeId (4020 = spells\Fear.s,
//     4025 = spells\Paralyze.s)
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>

#include "assets/string_table.h"
#include "simkin_bindings/item_executable.h"
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

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%-72s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    if (!ok) ++g_failures;
}

std::unique_ptr<sk_bindings::MonsterExecutable> LoadMonster(const std::string& path,
                                                             skInterpreter& interp,
                                                             const sk::StringTable& strings,
                                                             sk_bindings::MenuStack& stack) {
    skExecutableContext loadCtxt(&interp);
    try {
        auto m = std::make_unique<sk_bindings::MonsterExecutable>(skString(path.c_str()), loadCtxt,
                                                                    &strings, stack.player(), stack);
        skRValueArray args;
        args.append(skRValue(0));
        skRValue ret;
        skExecutableContext callCtxt(&interp);
        m->method(skString("Init"), args, ret, callCtxt);
        return m;
    } catch (skParseException&) {
        return nullptr;
    } catch (skRuntimeException&) {
        return nullptr;
    }
}

}  // namespace

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    if (!strings.Load(std::string(scriptRoot) + "/stringtable.eng")) {
        std::printf("m32_ai_package_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }

    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);

    using Monster = sk_bindings::MonsterExecutable;

    // --- The real script-facing Ai* calls ---
    // arat.s's own Init() ends with AiDetect(), so a freshly loaded real
    // creature must come up in the idle/detect package rather than the
    // constructor's asleep default.
    auto rat = LoadMonster(std::string(scriptRoot) + "/arat.s", interpreter, strings, stack);
    if (!rat) {
        std::printf("m32_ai_package_smoke: FAILED to load arat.s\n");
        return 1;
    }
    Check(rat->aiPackage() == Monster::kAiIdle,
          "real arat.s Init()'s AiDetect() leaves the creature in package 2 (idle)");

    // AiSleep -> -1, and back.
    {
        skRValueArray args;
        skRValue ret;
        skExecutableContext ctxt(&interpreter);
        rat->method(skString("AiSleep"), args, ret, ctxt);
    }
    Check(rat->aiPackage() == Monster::kAiAsleep, "AiSleep() puts the creature in package -1");
    rat->SetAiPackage(Monster::kAiIdle);

    // --- The timed flee package (FUN_10086b98) ---
    // Duration is stored as `duration << 8`, and the package reverts to
    // idle -- not to whatever it was -- because the real helper writes 2
    // into the saved-package slot for the flee case specifically.
    {
        const int duration = 50;  // real Fear.s magnitude 10 * 5
        rat->SetAiPackage(Monster::kAiPursue);
        rat->SetAiPackageTimed(Monster::kAiFlee, duration);
        Check(rat->aiPackage() == Monster::kAiFlee, "SetAiPackageTimed(4) enters the flee package");

        // `duration << 8` units at the port's frame delta.
        int expectedTicks = (duration << 8) / sk_bindings::kAiFrameDeltaUnits;
        bool stillFleeing = true;
        for (int i = 0; i < expectedTicks - 1; ++i) {
            rat->TickAi(sk_bindings::kAiFrameDeltaUnits);
            if (rat->aiPackage() != Monster::kAiFlee) stillFleeing = false;
        }
        Check(stillFleeing, "the flee package survives for its whole `duration << 8` countdown");
        // A couple more ticks must expire it.
        rat->TickAi(sk_bindings::kAiFrameDeltaUnits);
        rat->TickAi(sk_bindings::kAiFrameDeltaUnits);
        Check(rat->aiPackage() == Monster::kAiIdle,
              "...then reverts to idle (package 2), the real saved-package value");
    }

    // --- Paralysis lockout (monster+0x294) ---
    {
        rat->SetParalyzed(3 * sk_bindings::kAiFrameDeltaUnits);
        Check(rat->paralyzed(), "SetParalyzed arms the action lockout");
        rat->TickAi(sk_bindings::kAiFrameDeltaUnits);
        rat->TickAi(sk_bindings::kAiFrameDeltaUnits);
        Check(rat->paralyzed(), "...which is still active partway through");
        rat->TickAi(sk_bindings::kAiFrameDeltaUnits);
        Check(!rat->paralyzed(), "...and clears when it runs out");
    }

    // --- The attack cadence (monster+0x2c4) ---
    {
        // Must NOT fire before the accumulator passes 0x100, and must fire
        // once it does. At the port's 40-unit frame delta that's 7 ticks
        // (6 * 40 = 240 <= 256 < 7 * 40 = 280).
        int ticksToFirstSwing = 0;
        for (int i = 0; i < 200; ++i) {
            ++ticksToFirstSwing;
            if (rat->ConsumeAttackCadence(sk_bindings::kAiFrameDeltaUnits)) break;
        }
        int expected = sk_bindings::kAiAttackCadenceThreshold / sk_bindings::kAiFrameDeltaUnits + 1;
        Check(ticksToFirstSwing == expected,
              "the first attack lands after 0x100 worth of frame delta, not sooner");

        // After firing, the accumulator resets to a small random jitter
        // (rand & 0x1f), so the next swing takes roughly the same again --
        // never immediately, and never wildly longer.
        bool cadenceSane = true;
        for (int swing = 0; swing < 20; ++swing) {
            int ticks = 0;
            for (int i = 0; i < 200; ++i) {
                ++ticks;
                if (rat->ConsumeAttackCadence(sk_bindings::kAiFrameDeltaUnits)) break;
            }
            // 0x100 with 0..31 already banked, over a 40-unit delta.
            if (ticks < 6 || ticks > 7) cadenceSane = false;
        }
        Check(cadenceSane,
              "subsequent swings stay on cadence, jittered by the real `rand & 0x1f` reset");
    }

    // --- The real status-effect selector ---
    // FUN_100458e4 keys the effect off the spell entity's entities.txt
    // typeId; this port resolves the same relation from the script path.
    {
        skExecutableContext loadCtxt(&interpreter);
        auto fear = std::make_unique<sk_bindings::ItemExecutable>(
            skString((std::string(scriptRoot) + "/spells/Fear.s").c_str()), loadCtxt, stack);
        auto blaze = std::make_unique<sk_bindings::ItemExecutable>(
            skString((std::string(scriptRoot) + "/blaze.s").c_str()), loadCtxt, stack);
        Check(fear->statusEffect() == sk_bindings::ItemExecutable::kEffectFear,
              "real spells/Fear.s resolves to the fear effect (typeId 4020's branch)");
        Check(blaze->statusEffect() == sk_bindings::ItemExecutable::kEffectNone,
              "real blaze.s has no status effect branch -- it stays plain damage");

        // Casting real Fear.s at a real creature must put it to flight
        // rather than damage it.
        auto victim = LoadMonster(std::string(scriptRoot) + "/arat.s", interpreter, strings, stack);
        if (victim) {
            int hpBefore = victim->currentHealth();
            skRValueArray args;
            args.append(skRValue(static_cast<skiExecutable*>(victim.get()), false));
            args.append(skRValue(10));  // real Fear.s passes DoAttackRoll(target, 10)
            skRValue ret;
            skExecutableContext ctxt(&interpreter);
            fear->method(skString("DoAttackRoll"), args, ret, ctxt);
            Check(victim->aiPackage() == Monster::kAiFlee,
                  "real Fear.s's DoAttackRoll(target,10) puts the target in the flee package");
            Check(victim->currentHealth() == hpBefore,
                  "...and deals no damage doing it -- fear is not a damage spell");
        }
    }

    // --- M33: the remaining real status effects ---
    //
    // Each is transcribed from FUN_100458e4's own branch; the parameters
    // are the decompiled ones. Magnitude is the spell's SetRating() -- see
    // the DoAttackRoll handler for why the real input is a caster stat
    // this port doesn't model.
    {
        using Item = sk_bindings::ItemExecutable;
        auto cast = [&](const char* relPath) -> std::unique_ptr<Item> {
            skExecutableContext loadCtxt(&interpreter);
            auto spell = std::make_unique<Item>(
                skString((std::string(scriptRoot) + "/" + relPath).c_str()), loadCtxt, stack);
            skRValueArray initArgs;
            initArgs.append(skRValue(0));
            skRValue initRet;
            skExecutableContext initCtxt(&interpreter);
            spell->method(skString("Init"), initArgs, initRet, initCtxt);
            return spell;
        };
        auto hit = [&](Item& spell, Monster& victim) {
            skRValueArray args;
            args.append(skRValue(static_cast<skiExecutable*>(&victim), false));
            skRValue ret;
            skExecutableContext ctxt(&interpreter);
            spell.method(skString("DoAttackRoll"), args, ret, ctxt);
        };

        // Drain: attack -10 for (magnitude + 8) seconds.
        {
            auto spell = cast("spells/Drain.s");
            auto v = LoadMonster(std::string(scriptRoot) + "/arat.s", interpreter, strings, stack);
            int before = v->attack();
            hit(*spell, *v);
            Check(spell->statusEffect() == Item::kEffectDrain &&
                      v->statModifier(Monster::kStatAttack) == -10 && v->baseAttack() == before,
                  "Drain applies -10 to attack, leaving the base stat untouched");
        }
        // Blind: attack -10 AND defense -10, plus the blind flag.
        {
            auto spell = cast("spells/Blind.s");
            auto v = LoadMonster(std::string(scriptRoot) + "/arat.s", interpreter, strings, stack);
            hit(*spell, *v);
            Check(spell->statusEffect() == Item::kEffectBlind && v->blinded() &&
                      v->statModifier(Monster::kStatAttack) == -10 &&
                      v->statModifier(Monster::kStatDefense) == -10,
                  "Blind applies -10 attack and -10 defense, and sets the blind flag");
        }
        // Disease: disease.s's rating is 5, so the magnitude<7 branch
        // (-2 attack) plus a flat -3 defense, both for a fixed 30s.
        {
            auto spell = cast("spells/Disease.s");
            auto v = LoadMonster(std::string(scriptRoot) + "/arat.s", interpreter, strings, stack);
            hit(*spell, *v);
            Check(spell->statusEffect() == Item::kEffectDisease &&
                      v->statModifier(Monster::kStatAttack) == -2 &&
                      v->statModifier(Monster::kStatDefense) == -3,
                  "Disease takes the magnitude<7 branch (-2 attack) plus a flat -3 defense");
            int ticks30 = 30 * 256 / sk_bindings::kAiFrameDeltaUnits;
            for (int i = 0; i < ticks30 - 1; ++i) v->TickAi(sk_bindings::kAiFrameDeltaUnits);
            bool stillOn = v->statModifier(Monster::kStatDefense) == -3;
            v->TickAi(sk_bindings::kAiFrameDeltaUnits);
            v->TickAi(sk_bindings::kAiFrameDeltaUnits);
            Check(stillOn && v->statModifier(Monster::kStatDefense) == 0,
                  "...for a fixed 30 seconds, after which the stats come back");
        }
        // Poison: 3 damage every 256 units, for `magnitude` seconds.
        {
            auto spell = cast("spells/Poison.s");
            auto v = LoadMonster(std::string(scriptRoot) + "/arat.s", interpreter, strings, stack);
            int hp0 = v->currentHealth();
            hit(*spell, *v);
            Check(spell->statusEffect() == Item::kEffectPoison && v->poisoned(),
                  "real Poison.s (which passes DoAttackRoll no magnitude at all) still poisons");
            int ticksPerSecond = 256 / sk_bindings::kAiFrameDeltaUnits + 1;
            for (int i = 0; i < ticksPerSecond; ++i) v->TickAi(sk_bindings::kAiFrameDeltaUnits);
            Check(v->currentHealth() == hp0 - 3,
                  "...dealing exactly 3 per tick -- the real +0x76 value, passed to DoDamage");
            for (int i = 0; i < 12 * ticksPerSecond; ++i) {
                v->TickAi(sk_bindings::kAiFrameDeltaUnits);
            }
            int settled = v->currentHealth();
            for (int i = 0; i < 40; ++i) v->TickAi(sk_bindings::kAiFrameDeltaUnits);
            Check(!v->poisoned() && v->currentHealth() == settled,
                  "...and expires after its duration instead of ticking forever");
        }
        // ---- M34: the two branches that carry their own damage ----
        //
        // Absorb: fixed damage of `magnitude + 12` (its min and max are
        // equal, so the roll is skipped), then the caster is healed by
        // `damage * magnitude / 25 + 6` -- clamped, so it cannot overheal.
        // Real absorb.s has SetRating(1), so magnitude is 1: 13 damage,
        // less the rat's real SetMagicResistance(3), is 10; and the
        // proportional term truncates to 0 at that magnitude, leaving the
        // flat +6. (A caster at the real 25-point cap would instead absorb
        // the damage in full -- see the DoAttackRoll handler's note on why
        // this port substitutes the spell's own rating for a caster stat.)
        {
            auto spell = cast("spells/Absorb.s");
            auto v = LoadMonster(std::string(scriptRoot) + "/arat.s", interpreter, strings, stack);
            // The heal is invisible at full health, by design -- the real
            // SetHealth clamps to maxHealth -- so open a wound first.
            stack.player().ApplyDamage(50);
            int casterBefore = stack.player().health();
            int hp0 = v->currentHealth();
            hit(*spell, *v);
            Check(spell->statusEffect() == Item::kEffectAbsorb &&
                      v->currentHealth() == hp0 - 10,
                  "Absorb deals its fixed magnitude+12 damage (13, less 3 resistance)");
            Check(stack.player().health() == casterBefore + 6,
                  "...and transfers it to the caster, who heals by the real flat +6");
            int full = stack.player().maxHealth();
            stack.player().SetHealth(full);
            hit(*spell, *v);
            Check(stack.player().health() == full,
                  "...but never past maxHealth -- the real SetHealth clamps");
        }
        // IgniteFoe: an opening damage roll, then the stats block's
        // *second* periodic channel -- 1 point a second for magnitude*2
        // seconds. Cast at Lakvan (200 health) rather than the rat, whose
        // real 12 wouldn't survive the opening hit to be burned at all.
        {
            auto spell = cast("spells/IgniteFoe.s");
            auto v = LoadMonster(std::string(scriptRoot) + "/monsters/lakvan.s", interpreter,
                                  strings, stack);
            int hp0 = v->currentHealth();
            hit(*spell, *v);
            int opening = hp0 - v->currentHealth();
            // ignitefoe.s has SetRating(19), so the real roll is
            // (19+1)*2 .. (19+1)*5, less Lakvan's SetMagicResistance(45),
            // floored at 1. Asserted as the range rather than a value --
            // this is the one status branch with genuine variance.
            int lo = (std::max)(1, (19 + 1) * 2 - v->magicResistance());
            int hi = (std::max)(1, (19 + 1) * 5 - v->magicResistance());
            Check(spell->statusEffect() == Item::kEffectIgniteFoe && opening >= lo &&
                      opening <= hi && v->burning(),
                  "IgniteFoe rolls its opening hit in the real range and sets the target alight");

            int ticksPerSecond = 256 / sk_bindings::kAiFrameDeltaUnits + 1;
            int hp1 = v->currentHealth();
            for (int i = 0; i < ticksPerSecond; ++i) v->TickAi(sk_bindings::kAiFrameDeltaUnits);
            Check(v->currentHealth() == hp1 - 1,
                  "...then burns for exactly 1 a second -- the real +0x76 value it wrote");

            // Duration is magnitude*2 == 38 seconds; run well past it.
            for (int i = 0; i < 45 * ticksPerSecond; ++i) {
                v->TickAi(sk_bindings::kAiFrameDeltaUnits);
            }
            int settled = v->currentHealth();
            for (int i = 0; i < 2 * ticksPerSecond; ++i) {
                v->TickAi(sk_bindings::kAiFrameDeltaUnits);
            }
            Check(!v->burning() && v->currentHealth() == settled,
                  "...and goes out after magnitude*2 seconds instead of burning forever");
        }
        // The shared +0x76 field. The burn channel reads the same short
        // poison writes, so poisoning a burning creature really does make
        // its flames tick for 3 instead of 1. Faithful, and easy to lose.
        {
            auto ignite = cast("spells/IgniteFoe.s");
            auto poison = cast("spells/Poison.s");
            auto v = LoadMonster(std::string(scriptRoot) + "/monsters/lakvan.s", interpreter,
                                  strings, stack);
            hit(*ignite, *v);
            hit(*poison, *v);
            int ticksPerSecond = 256 / sk_bindings::kAiFrameDeltaUnits + 1;
            int hp = v->currentHealth();
            for (int i = 0; i < ticksPerSecond; ++i) v->TickAi(sk_bindings::kAiFrameDeltaUnits);
            // Both channels fire on the same tick: poison's own 3 through
            // channel 1, plus the burn re-reading that same 3 -- 6 total.
            Check(v->burning() && v->poisoned() && v->currentHealth() == hp - 6,
                  "poison and burn share +0x76, so a poisoned burn ticks for 3, not 1");
        }
        // A burn can finish a creature off on its own -- the real kind-8
        // handler calls the actor's kill slot directly when health hits 0.
        // main.cpp keys its OnKilled()/loot handling off exactly this
        // transition happening inside TickAi(), so it is worth pinning.
        {
            auto v = LoadMonster(std::string(scriptRoot) + "/arat.s", interpreter, strings, stack);
            v->ApplyBurn(1, 60);  // the rat's real SetMaxHealth is 12
            bool aliveBefore = v->alive();
            int ticksPerSecond = 256 / sk_bindings::kAiFrameDeltaUnits + 1;
            for (int i = 0; i < 30 * ticksPerSecond; ++i) {
                v->TickAi(sk_bindings::kAiFrameDeltaUnits);
            }
            Check(aliveBefore && !v->alive() && v->currentHealth() == 0,
                  "a burn left to run kills the creature from inside the AI tick");
        }
        // FUN_1004aa28's mode-1 "strongest wins" rule.
        {
            auto v = LoadMonster(std::string(scriptRoot) + "/arat.s", interpreter, strings, stack);
            v->ApplyStatModifier(Monster::kStatAttack, -10, 30);
            v->ApplyStatModifier(Monster::kStatAttack, -2, 30);   // weaker: rejected
            bool keptStrong = v->statModifier(Monster::kStatAttack) == -10;
            v->ApplyStatModifier(Monster::kStatAttack, -15, 30);  // stronger: replaces
            Check(keptStrong && v->statModifier(Monster::kStatAttack) == -15,
                  "a weaker modifier on the same stat is rejected; a stronger one replaces it");
        }
    }

    std::printf("\nm32_ai_package_smoke: %s (%d failure(s))\n", g_failures == 0 ? "OK" : "FAILED",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
