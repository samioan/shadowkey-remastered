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
        // once it does. Bounds are derived from the two real constants
        // rather than written out, so they stay honest if the frame delta
        // is ever re-derived again -- M35 corrected it from 40 to 0x100/25
        // (the engine's clock counts 1/256ths of a second), which turned
        // one swing every ~7 ticks into one every ~26, i.e. one a second.
        int ticksToFirstSwing = 0;
        for (int i = 0; i < 200; ++i) {
            ++ticksToFirstSwing;
            rat->TickAttackCadence(sk_bindings::kAiFrameDeltaUnits);
            if (rat->attackCadenceReady()) { rat->JitterAttackCadence(); break; }
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
                rat->TickAttackCadence(sk_bindings::kAiFrameDeltaUnits);
                if (rat->attackCadenceReady()) { rat->JitterAttackCadence(); break; }
            }
            // 0x100 to cover, with 0..31 of it already banked by the reset.
            int fastest = (sk_bindings::kAiAttackCadenceThreshold - 31) /
                              sk_bindings::kAiFrameDeltaUnits + 1;
            int slowest = expected;
            if (ticks < fastest || ticks > slowest) cadenceSane = false;
        }
        Check(cadenceSane,
              "subsequent swings stay on cadence, jittered by the real `rand & 0x1f` reset");

        // The whole point of the M35 correction: that cadence is about one
        // swing per second at the real 25Hz tick, not four.
        int ticksPerSwing = sk_bindings::kAiAttackCadenceThreshold /
                                sk_bindings::kAiFrameDeltaUnits + 1;
        Check(ticksPerSwing >= 24 && ticksPerSwing <= 27,
              "...which works out to roughly one swing a second, not four");
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
        // M37: blaze.s used to fall through to the port's own from-scratch
        // damage path. entities.txt gives it typeId 50, which FUN_100458e4
        // does have a branch for (a 3 .. level*3+3 damage roll), so it is
        // now a real dispatcher branch like every other spell -- just one
        // with no status half.
        Check(blaze->statusEffect() == sk_bindings::ItemExecutable::kEffectBlaze,
              "real blaze.s resolves to typeId 50's own damage-only branch");

        // Casting real Fear.s at a real creature must put it to flight
        // rather than damage it.
        auto victim = LoadMonster(std::string(scriptRoot) + "/arat.s", interpreter, strings, stack);
        if (victim) {
            // M37: zero the target's magic resistance so the real hit gate
            // is a certainty rather than a 97% chance -- see the
            // makeVulnerable() comment in the M33 block below.
            {
                skRValueArray zero;
                zero.append(skRValue(0));
                skRValue r;
                skExecutableContext c(&interpreter);
                victim->method(skString("SetMagicResistance"), zero, r, c);
            }
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
    // are the decompiled ones.
    //
    // M37: magnitude is the **caster's level**, not the spell's
    // SetRating() this used to substitute -- so every case below sets the
    // caster's level explicitly and asserts against that, which is both the
    // real input and a stronger test than a per-spell constant was.
    {
        using Item = sk_bindings::ItemExecutable;
        auto setCasterLevel = [&](int lvl) {
            skRValueArray a;
            a.append(skRValue(lvl));
            skRValue r;
            skExecutableContext c(&interpreter);
            stack.player().method(skString("SetLevel"), a, r, c);
        };
        // M37: the real resistance model is a *probability* gate in front
        // of the whole effect, so a cast at a resistant target lands only
        // most of the time. Zeroing the target's magic resistance makes the
        // chance exactly 0x100, which the engine's own `== 0x100` special
        // case turns into a guaranteed hit -- that is what keeps the
        // effect assertions below exact instead of flaky. The gate's own
        // arithmetic is pinned separately, on the creatures' real shipped
        // resistances, in m37_spellpower_smoke.
        auto makeVulnerable = [&](Monster& v) {
            skRValueArray a;
            a.append(skRValue(0));
            skRValue r;
            skExecutableContext c(&interpreter);
            v.method(skString("SetMagicResistance"), a, r, c);
        };
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
            setCasterLevel(8);
            auto spell = cast("spells/Drain.s");
            auto v = LoadMonster(std::string(scriptRoot) + "/arat.s", interpreter, strings, stack);
            makeVulnerable(*v);
            int before = v->attack();
            hit(*spell, *v);
            // M58: the effect is *written through* the attack field now, the
            // way the engine's own applier does -- so the check is that the
            // creature's attack really dropped by 10 and that the node
            // records where those 10 came from, rather than that a base
            // stat stayed put behind a modifier this port used to add on
            // read. Clamped at 0, hence the max.
            Check(spell->statusEffect() == Item::kEffectDrain &&
                      v->statModifier(Monster::kStatAttack) == -10 &&
                      v->attack() == (before - 10 < 0 ? 0 : before - 10),
                  "Drain applies -10 to attack, written through the field itself");
        }
        // Blind: attack -10 AND defense -10 -- and, M43-corrected, the
        // blind *flag* only when the target is not a creature. The real
        // guard is `if (!target->vtable[0xe4]())`, and M43 pinned
        // `vtable+0xe4` as the monster predicate (FUN_1002fd30 resolves a
        // monster through it and the player through 0xcc), which flips the
        // reading this assertion used to encode. See the player-target half
        // of the same effect in m43_monster_caster_smoke.cpp.
        {
            auto spell = cast("spells/Blind.s");
            auto v = LoadMonster(std::string(scriptRoot) + "/arat.s", interpreter, strings, stack);
            makeVulnerable(*v);
            hit(*spell, *v);
            Check(spell->statusEffect() == Item::kEffectBlind && !v->blinded() &&
                      v->statModifier(Monster::kStatAttack) == -10 &&
                      v->statModifier(Monster::kStatDefense) == -10,
                  "Blind applies -10 attack and -10 defense to a creature, but not the flag");
        }
        // Disease: the `magnitude < 7` branch is -2 attack, otherwise -3,
        // plus a flat -3 defense either way, both for a fixed 30s. M37
        // pins *both* sides of that branch, which was impossible while the
        // magnitude was a per-spell constant -- with the caster's level as
        // the input, a level-5 and a level-8 caster take different arms.
        {
            setCasterLevel(8);
            auto spell = cast("spells/Disease.s");
            auto high = LoadMonster(std::string(scriptRoot) + "/arat.s", interpreter, strings,
                                     stack);
            makeVulnerable(*high);
            hit(*spell, *high);
            bool highArm = high->statModifier(Monster::kStatAttack) == -3;

            setCasterLevel(5);
            auto v = LoadMonster(std::string(scriptRoot) + "/arat.s", interpreter, strings, stack);
            makeVulnerable(*v);
            hit(*spell, *v);
            Check(spell->statusEffect() == Item::kEffectDisease && highArm &&
                      v->statModifier(Monster::kStatAttack) == -2 &&
                      v->statModifier(Monster::kStatDefense) == -3,
                  "Disease's -2/-3 attack arms follow the caster's level across the <7 boundary");
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
            setCasterLevel(8);
            auto spell = cast("spells/Poison.s");
            auto v = LoadMonster(std::string(scriptRoot) + "/arat.s", interpreter, strings, stack);
            makeVulnerable(*v);
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
        //
        // M37: at the level cap the proportional term is the whole damage,
        // so a capped caster really does absorb the hit in full and then
        // some -- 37 damage dealt, 37 + 6 healed. That is the case worth
        // pinning, and it is only reachable now that the magnitude is a
        // caster stat: the old rating substitution fixed it at 1 forever,
        // where the term truncated to 0 and only the flat +6 ever showed.
        {
            setCasterLevel(25);
            auto spell = cast("spells/Absorb.s");
            auto v = LoadMonster(std::string(scriptRoot) + "/monsters/lakvan.s", interpreter,
                                  strings, stack);
            makeVulnerable(*v);
            // The heal is invisible at full health, by design -- the real
            // SetHealth clamps to maxHealth -- so open a wound first.
            stack.player().ApplyDamage(50);
            int casterBefore = stack.player().health();
            int hp0 = v->currentHealth();
            hit(*spell, *v);
            Check(spell->statusEffect() == Item::kEffectAbsorb &&
                      v->currentHealth() == hp0 - 37,
                  "Absorb deals its fixed magnitude+12 damage (25 + 12 = 37, no variance)");
            Check(stack.player().health() == casterBefore + 37 + 6,
                  "...and at the level cap transfers the whole hit, plus the real flat +6");
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
            const int kCasterLevel = 8;
            setCasterLevel(kCasterLevel);
            auto spell = cast("spells/IgniteFoe.s");
            auto v = LoadMonster(std::string(scriptRoot) + "/monsters/lakvan.s", interpreter,
                                  strings, stack);
            makeVulnerable(*v);
            int hp0 = v->currentHealth();
            hit(*spell, *v);
            int opening = hp0 - v->currentHealth();
            // The real roll is (magnitude+1)*2 .. (magnitude+1)*5, with no
            // resistance subtraction at all -- M37 retired that step, which
            // never existed in the decompile: resistance is the hit gate.
            // Asserted as the range rather than a value -- this is the one
            // status branch with genuine variance.
            int lo = (kCasterLevel + 1) * 2;
            int hi = (kCasterLevel + 1) * 5;
            Check(spell->statusEffect() == Item::kEffectIgniteFoe && opening >= lo &&
                      opening <= hi && v->burning(),
                  "IgniteFoe rolls its opening hit in the real range and sets the target alight");

            int ticksPerSecond = 256 / sk_bindings::kAiFrameDeltaUnits + 1;
            int hp1 = v->currentHealth();
            for (int i = 0; i < ticksPerSecond; ++i) v->TickAi(sk_bindings::kAiFrameDeltaUnits);
            Check(v->currentHealth() == hp1 - 1,
                  "...then burns for exactly 1 a second -- the real +0x76 value it wrote");

            // Duration is magnitude*2 == 16 seconds; run well past it.
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
            setCasterLevel(8);
            auto ignite = cast("spells/IgniteFoe.s");
            auto poison = cast("spells/Poison.s");
            auto v = LoadMonster(std::string(scriptRoot) + "/monsters/lakvan.s", interpreter,
                                  strings, stack);
            makeVulnerable(*v);
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
