// M73 smoke test: the vitals economy -- what spends health, fatigue and
// magicka, and what puts them back.
//
// The port had one fatigue cost (the jump) and no regeneration at all, so
// the three HUD bars only ever went down. Everything asserted here is
// transcribed from the shipped binary; simkin_bindings/vitals.h carries the
// derivation. What each part checks, and why it is the check that could
// fail:
//
//   1. The four action costs, as constants, against the functions they came
//      out of.
//   2. `FUN_10045228`, the movement drain: its period, the fact that it
//      *resets* rather than subtracts (so a long frame cannot bank ticks),
//      the strict `>` on the period, and the per-direction doubling that
//      falls out of the hook being called from each move slot.
//   3. The two regeneration divisors, checked by reproducing the shipped
//      magic-number division sequences instruction for instruction and
//      comparing them against `/25` and `/15` across the whole plausible
//      attribute range. A wrong divisor is the single most likely way to
//      get this milestone silently wrong -- `0x51eb851f` reads like 100 at
//      a glance and is 25 -- so it is checked arithmetically, not asserted.
//   4. The three regen amounts against the **real** race/sex attribute
//      table, all sixteen rows, so the numbers are grounded in shipped data
//      rather than in a round 50.
//   5. The two racial modifiers and the one item modifier, including that
//      each applies to exactly one race and that the Wood Elf's compiled-
//      away health bonus really is absent.
//   6. `items\azras_bandage.s` against the real entities.txt.
//   7. A real PlayerExecutable ticking: the schedule, the clamp at maximum,
//      the "only below max" gate, and the equipped-bandage bonus through a
//      real created item.
//   8. The two exhaustion penalties, including that RollDamage halves the
//      roll before mitigation and not after.
//   9. The whole economy end to end at the real 25Hz tick: how long a
//      shipped character can walk before collapsing, and how long standing
//      still takes to undo it. This is the check that the recovered numbers
//      are playable rather than degenerate.
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "simkin_bindings/character_progression.h"
#include "simkin_bindings/combat.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/vitals.h"
#include "skInterpreter.h"
#include "world/entity_types.h"

namespace sk_b = sk_bindings;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%-86s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    if (!ok) ++g_failures;
}

// The shipped `/25`, instruction for instruction (0x10049c28):
//
//     ldr   r3, [pc, #0x238]       ; 0x51eb851f
//     smull r7, r2, r3, r5         ; r2 = high 32 of (int64)0x51eb851f * x
//     asr   r3, r5, #0x1f
//     rsb   r5, r3, r2, asr #3     ; (r2 >> 3) - (x >> 31)
int ShippedDivide25(int x) {
    const int hi = static_cast<int>((static_cast<long long>(0x51eb851f) * x) >> 32);
    return (hi >> 3) - (x >> 31);
}

// The shipped `/15` (0x10049d34 and again at 0x10049e1c). Same shape with
// the extra `add r2, r2, r5` the 0x88888889 multiplier needs -- and note the
// multiplier is used *signed*, i.e. -2004318071.
int ShippedDivide15(int x) {
    const int m = static_cast<int>(0x88888889u);
    const int hi = static_cast<int>((static_cast<long long>(m) * x) >> 32);
    return ((hi + x) >> 3) - (x >> 31);
}

const char* kRaceNames[8] = {"Argonian", "Breton",   "Dark Elf", "High Elf",
                             "Khajiit",  "Nord",     "Redguard", "Wood Elf"};

}  // namespace

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";

    // ---------------------------------------------------------------
    std::printf("\n== 1. what an action costs ==\n");
    // Four functions, four literals. Each is the whole reason its action
    // now touches the pool at all.
    Check(sk_b::kJumpFatigueCost == 5, "FUN_10044400: a jump spends 5 (and is refused at 5 or below)");
    Check(sk_b::kAttackFatigueCost == 4, "FUN_100425bc: an attack spends 4, before it even aims");
    Check(sk_b::kCastFatigueCost == 3, "FUN_10042394 case 2: a cast that happened spends 3");
    Check(sk_b::kMoveFatigueCost == 2 && sk_b::kMoveFatiguePeriod == 0xc4,
          "FUN_10045228: moving spends 2 every 0xc4 delta units");

    // ---------------------------------------------------------------
    std::printf("\n== 2. the movement drain (FUN_10045228) ==\n");
    constexpr int kTick = sk_b::kAiFrameDeltaUnits;  // 10 units, the real 25Hz frame
    {
        int accum = 0;
        int drainedAt = -1;
        int total = 0;
        for (int tick = 1; tick <= 40; ++tick) {
            const int d = sk_b::MovementFatigueDrain(accum, kTick);
            if (d > 0 && drainedAt < 0) drainedAt = tick;
            total += d;
        }
        // 0xc4 == 196, so with a 10-unit tick the accumulator passes it on
        // the 20th call (200 > 196), not the 19th (190).
        Check(drainedAt == 20, "the first drain lands on the 20th move call, at 200 > 196 units");
        Check(total == 4, "and 40 calls drain twice, not three times -- the reset is to 0");
    }
    {
        // The strictness of the comparison, on its own: an accumulator
        // sitting exactly on the period does not fire. The shipped test is
        // `cmp r1, #0xc4 / ble`, and getting it backwards would shift every
        // drain one frame earlier forever.
        int accum = sk_b::kMoveFatiguePeriod - 1;
        Check(sk_b::MovementFatigueDrain(accum, 1) == 0 && accum == sk_b::kMoveFatiguePeriod,
              "landing exactly on the period does not drain -- the compare is strict");
        Check(sk_b::MovementFatigueDrain(accum, 1) == sk_b::kMoveFatigueCost && accum == 0,
              "and the next unit does, resetting the accumulator to zero");
    }
    {
        // The per-direction doubling. The hook is called by each of the
        // four base move functions, so a frame that moves forward *and*
        // strafes calls it twice -- which is not an approximation here, it
        // is what the four `FUN_1001f2b4`..`FUN_1001f338` overrides do.
        int oneWay = 0, twoWays = 0;
        int oneTotal = 0, twoTotal = 0;
        for (int tick = 0; tick < 60; ++tick) {
            oneTotal += sk_b::MovementFatigueDrain(oneWay, kTick);
            twoTotal += sk_b::MovementFatigueDrain(twoWays, kTick);
            twoTotal += sk_b::MovementFatigueDrain(twoWays, kTick);
        }
        Check(twoTotal == 2 * oneTotal && oneTotal > 0,
              "moving diagonally drains exactly twice as fast, because the hook fires twice");
    }
    {
        // What that costs in seconds of walking, which is the number a
        // player actually feels. 25 ticks a second, 10 units each.
        int accum = 0, spent = 0, ticks = 0;
        while (spent < 100) {
            spent += sk_b::MovementFatigueDrain(accum, kTick);
            ++ticks;
        }
        const double seconds = ticks / 25.0;
        std::printf("   walking flat out spends 100 fatigue in %d ticks (%.1f s)\n", ticks,
                    seconds);
        Check(seconds > 20.0 && seconds < 60.0,
              "a 100-point pool takes tens of seconds of walking to empty, not one or one hundred");
    }

    // ---------------------------------------------------------------
    std::printf("\n== 3. the two regeneration divisors ==\n");
    {
        // The check that matters most in this milestone. Both divisors are
        // magic-number reciprocals in the shipped code, and both are easy
        // to misread by an order of magnitude; reproducing the exact
        // instruction sequence and comparing it against the plain division
        // settles it without trusting the decompiler.
        bool ok25 = true, ok15 = true;
        for (int x = -2000; x <= 5000; ++x) {
            if (ShippedDivide25(x) != x / 25) ok25 = false;
            if (ShippedDivide15(x) != x / 15) ok15 = false;
        }
        Check(ok25, "0x51eb851f with `asr #3` after the high word is /25, over -2000..5000");
        Check(ok15, "0x88888889 with the add-then-`asr #3` correction is /15, over the same range");
        // And the amounts actually use them.
        Check(sk_b::HealthRegenAmount(100, false) == ShippedDivide25(100) &&
                  sk_b::HealthRegenAmount(37, false) == ShippedDivide25(37),
              "HealthRegenAmount matches the shipped /25 sequence");
        Check(sk_b::MagickaRegenAmount(100, 0, 0) == ShippedDivide15(100) &&
                  sk_b::FatigueRegenAmount(60, 40, 0, 0) == ShippedDivide15(100),
              "both pool amounts match the shipped /15 sequence");
        // The periods, which decide how often any of it happens.
        Check(sk_b::kHealthRegenPeriod == 0x400 && sk_b::kMagickaRegenPeriod == 0x200 &&
                  sk_b::kFatigueRegenPeriod == 0x200,
              "health regenerates on a 4s period, magicka and fatigue on a 2s one");
    }

    // ---------------------------------------------------------------
    std::printf("\n== 4. the amounts, against the real race/sex table ==\n");
    {
        // Grounding the divisors in shipped data: FUN_1001fc24's own
        // sixteen attribute rows, run through the three amounts. If a
        // divisor were wrong by 4x (25 vs 100) every one of these would
        // come out zero, which is what the assertion below catches.
        int rows = 0, healthZero = 0, magickaZero = 0, fatigueZero = 0;
        for (int race = 0; race < sk_b::kRaceCount; ++race) {
            for (int sex = 0; sex < 2; ++sex) {
                const sk_b::BaseAttributes a = sk_b::RaceBaseAttributes(race, sex == 1);
                const int h = sk_b::HealthRegenAmount(a.endurance, false);
                const int m = sk_b::MagickaRegenAmount(a.willpower, race, 1);
                const int f = sk_b::FatigueRegenAmount(a.strength, a.willpower, race, 1);
                if (sex == 1) {
                    std::printf("   %-9s male   end %2d -> %d hp/4s   wil %2d -> %d mp/2s   "
                                "str %2d -> %d fp/2s\n",
                                kRaceNames[race], a.endurance, h, a.willpower, m, a.strength, f);
                }
                ++rows;
                if (h == 0) ++healthZero;
                if (m == 0) ++magickaZero;
                if (f == 0) ++fatigueZero;
            }
        }
        Check(rows == 16, "all sixteen shipped race/sex rows exercised");
        Check(healthZero == 0 && magickaZero == 0 && fatigueZero == 0,
              "every shipped starting character regenerates a non-zero amount in all three pools");
    }

    // ---------------------------------------------------------------
    std::printf("\n== 5. the racial and item modifiers ==\n");
    {
        // Each bonus belongs to exactly one race, and both are added to the
        // attribute term *before* the divide -- which is why a rank only
        // starts paying once it crosses a multiple of 15/5.
        int magickaBonusRaces = 0, fatigueBonusRaces = 0;
        for (int race = 0; race < sk_b::kRaceCount; ++race) {
            if (sk_b::MagickaRegenAmount(50, race, 3) != sk_b::MagickaRegenAmount(50, race, 0)) {
                ++magickaBonusRaces;
                Check(race == sk_b::kRaceHighElf, "the magicka bonus belongs to the High Elf");
            }
            if (sk_b::FatigueRegenAmount(40, 40, race, 3) !=
                sk_b::FatigueRegenAmount(40, 40, race, 0)) {
                ++fatigueBonusRaces;
                Check(race == sk_b::kRaceBreton, "the fatigue bonus belongs to the Breton");
            }
        }
        Check(magickaBonusRaces == 1 && fatigueBonusRaces == 1,
              "and to exactly one race each -- no other race has an arm");
        Check(sk_b::MagickaRegenAmount(50, sk_b::kRaceHighElf, 3) == (50 + 15) / 15,
              "a rank-3 High Elf adds 3*5 to willpower before the divide");
        Check(sk_b::FatigueRegenAmount(40, 40, sk_b::kRaceBreton, 3) == (80 + 15) / 15,
              "a rank-3 Breton adds the same to strength+willpower");
        // The one that was written and compiled away: at 0x10049c1c the
        // health branch loads the owner's race, compares it with 7, and
        // never reads the flags. There is no Wood Elf health bonus in the
        // shipped game and there must not be one here.
        bool woodElfNeutral = true;
        for (int rank = 0; rank < 20; ++rank) {
            if (sk_b::HealthRegenAmount(50, false) != 50 / 25) woodElfNeutral = false;
        }
        Check(woodElfNeutral && sk_b::HealthRegenAmount(50, false) == 2,
              "health regen takes no race argument at all -- the Wood Elf arm was compiled away");
    }

    // ---------------------------------------------------------------
    std::printf("\n== 6. items\\azras_bandage.s ==\n");
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot)) {
        std::printf("m73_vitals_smoke: FAILED to load entities.txt\n");
        return 1;
    }
    {
        const sk::EntityTypeDescriptor* desc = entityTypes.Lookup(sk_b::kAzraBandageTypeId);
        Check(desc != nullptr, "typeId 4702 is a real entities.txt row");
        if (desc) {
            std::printf("   4702 -> \"%s\" (category %d)\n", desc->name.c_str(), desc->category);
            Check(desc->name.find("azras_bandage") != std::string::npos,
                  "and it is the bandage FUN_10049b64's health branch tests for");
        }
        Check(sk_b::HealthRegenAmount(50, true) - sk_b::HealthRegenAmount(50, false) == 3,
              "carrying it adds a flat 3 to every health tick, on top of endurance/25");
    }

    // ---------------------------------------------------------------
    std::printf("\n== 7. a real player ticking ==\n");
    sk::StringTable strings;
    if (!strings.Load(std::string(scriptRoot) + "/stringtable.eng")) {
        std::printf("m73_vitals_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }
    skInterpreter interpreter;
    sk_b::MenuStack stack(scriptRoot, interpreter, &strings);
    stack.level().SetEntityTypes(&entityTypes);
    sk_b::PlayerExecutable& player = stack.player();
    // The three accumulators are ordinary per-actor state (stats +0x3c/
    // +0x3e/+0x40), so a schedule assertion only means what it says if it
    // starts from a known phase.
    auto resetRegenPhase = [&](sk_b::PlayerExecutable& p) {
        p.actorStats().healthRegenAccum() = 0;
        p.actorStats().magickaRegenAccum() = 0;
        p.actorStats().fatigueRegenAccum() = 0;
    };
    {
        // The defaults a PlayerExecutable starts at: 100/100 health,
        // 50/50 magicka, 100/100 fatigue, every attribute 50.
        const int enduranceAmount = 50 / 25;  // 2
        const int poolAmount = (50 + 50) / 15;  // 6 fatigue
        const int magickaAmount = 50 / 15;      // 3

        // A full pool does not move, however long it ticks.
        resetRegenPhase(player);
        for (int tick = 0; tick < 250; ++tick) player.TickVitalRegeneration(kTick);
        Check(player.health() == player.maxHealth() && player.magicka() == player.maxMagicka() &&
                  player.fatigue() == player.maxFatigue(),
              "ten seconds of ticking on a full character changes nothing");

        // Now hurt all three and watch the schedule. The health period is
        // twice the other two, so four seconds is one health tick and two
        // of each pool tick.
        resetRegenPhase(player);
        player.SetHealth(10);
        player.SetActorMagicka(0);
        player.SetActorFatigue(0);
        for (int tick = 0; tick < 105; ++tick) player.TickVitalRegeneration(kTick);  // 4.2s
        std::printf("   after 4.2s wounded: %d hp, %d mp, %d fp\n", player.health(),
                    player.magicka(), player.fatigue());
        Check(player.health() == 10 + enduranceAmount,
              "one health period elapsed is exactly one health tick (endurance/25)");
        Check(player.magicka() == 2 * magickaAmount && player.fatigue() == 2 * poolAmount,
              "and exactly two magicka and two fatigue ticks -- half the period, twice the ticks");

        // The clamp. A pool one point short of full takes the whole tick
        // and stops at the maximum rather than overshooting.
        resetRegenPhase(player);
        player.SetActorFatigue(player.maxFatigue() - 1);
        for (int tick = 0; tick < 60; ++tick) player.TickVitalRegeneration(kTick);
        Check(player.fatigue() == player.maxFatigue(),
              "a nearly-full pool tops out at its maximum instead of overshooting");
    }
    {
        // The bandage, through a real created item in a real hand.
        player.SetHealth(10);
        std::unique_ptr<sk_b::ItemExecutable> bandage =
            stack.level().CreateItem(sk_b::kAzraBandageTypeId, /*requireItemCategory=*/false);
        Check(bandage != nullptr, "the bandage's own script loads and runs through the interpreter");
        if (bandage) {
            Check(!player.HasEquippedTemplate(sk_b::kAzraBandageTypeId),
                  "an unowned bandage is not equipped");
            bandage->SetEquipped(true);
            player.AddItem(std::move(bandage));
            Check(player.HasEquippedTemplate(sk_b::kAzraBandageTypeId),
                  "and a worn one is found by the FUN_10045474 walk");
            resetRegenPhase(player);
            const int before = player.health();
            for (int tick = 0; tick < 105; ++tick) player.TickVitalRegeneration(kTick);
            Check(player.health() == before + 50 / 25 + 3,
                  "so the next health tick is endurance/25 plus the bandage's 3");
        }
    }

    // ---------------------------------------------------------------
    std::printf("\n== 8. what an empty pool costs ==\n");
    {
        Check(sk_b::ExhaustedSpeedScale(1) == 1.0f && sk_b::ExhaustedSpeedScale(0) == 0.5f,
              "FUN_100445d4's extra shift: an empty pool halves movement speed");
        Check(!sk_b::ExhaustedMeleeHalvesDamage(1) && sk_b::ExhaustedMeleeHalvesDamage(0),
              "and the melee penalty uses the same `fatigue < 1` test");
        // RollDamage, deterministically: a hopeless defence pins the
        // to-hit chance at 255 (and 1000 keeps `attack << 16` inside a
        // 32-bit int, which is the trap here), and min == max collapses the
        // spread to a single value -- so the only variable left is the
        // halving.
        Check(sk_b::MeleeHitChance(1000, 1) == 255, "the test's own matchup is a guaranteed hit");
        const int full = sk_b::RollDamage(1000, 1, 0, 40, 40, false);
        const int tired = sk_b::RollDamage(1000, 1, 0, 40, 40, true);
        Check(full == 40 && tired == 20, "an exhausted swing rolls half the damage");
        // ...and it halves the roll *before* mitigation, not the result
        // after. With 10 points of armour the two orders differ: 40/2-10
        // is 10, but (40-10)/2 would be 15.
        Check(sk_b::RollDamage(1000, 1, 10, 40, 40, true) == 10,
              "the halving lands on the roll, ahead of the armour subtraction");
    }

    // ---------------------------------------------------------------
    std::printf("\n== 9. the economy end to end ==\n");
    {
        // The whole loop at the real tick rate, on a real character, with
        // the drain and the regeneration running against each other -- the
        // only way to see where the recovered numbers actually settle.
        //
        // They settle somewhere specific, and it is the most surprising
        // result in this milestone. Both rates are fixed by the periods:
        //
        //   drain, straight line   2 per 20 ticks   = 2.50 fatigue/s
        //   drain, diagonally      4 per 20 ticks   = 5.00 fatigue/s
        //   regen, 50 str / 50 wil 6 per 52 ticks   = 2.88 fatigue/s
        //
        // so a starting character **cannot walk themselves tired in a
        // straight line** -- regeneration is very slightly ahead. Holding a
        // strafe as well doubles the drain and the pool empties in well
        // under a minute. That is not a rounding artefact of this port: the
        // engine's own periods are 0xc4 and 0x200, its own costs 2 and the
        // divisor 15, and the hook genuinely fires once per direction.
        // Fatigue in this game is spent by fighting and jumping; walking
        // only bleeds it when you are also sidestepping, or when strength
        // and willpower are low.
        auto walkUntilEmpty = [&](int drainsPerFrame) {
            sk_b::PlayerExecutable& p = stack.player();
            resetRegenPhase(p);
            p.SetActorFatigue(p.maxFatigue());
            int accum = 0, ticks = 0;
            const int cap = 25 * 600;  // ten minutes of game time
            while (p.actorFatigue() > 0 && ticks < cap) {
                for (int i = 0; i < drainsPerFrame; ++i) {
                    const int d = sk_b::MovementFatigueDrain(accum, kTick);
                    if (d > 0) p.SetActorFatigue(p.actorFatigue() - d);
                }
                p.TickVitalRegeneration(kTick);
                ++ticks;
            }
            return ticks;
        };
        sk_b::PlayerExecutable& p = stack.player();
        std::printf("   character: %d strength, %d willpower -> %d fatigue every 2s\n",
                    p.strength(), p.willpower(),
                    sk_b::FatigueRegenAmount(p.strength(), p.willpower(), p.race(),
                                              p.raceAbility()));
        const int straight = walkUntilEmpty(1);
        Check(straight == 25 * 600,
              "a straight walk never exhausts a 50/50 character -- regen is 2.88/s against 2.50/s");

        const int diagonal = walkUntilEmpty(2);
        std::printf("   walking diagonally exhausts them in %.1f s\n", diagonal / 25.0);
        Check(diagonal < 25 * 600, "holding a strafe as well does exhaust them -- 5.00/s against 2.88/s");
        Check(diagonal / 25.0 > 20.0 && diagonal / 25.0 < 120.0,
              "and it takes tens of seconds, not one and not ten minutes");

        // ...and standing still puts it back, at the regen rate alone.
        resetRegenPhase(p);
        int restTicks = 0;
        while (p.actorFatigue() < p.maxFatigue() && restTicks < 25 * 600) {
            p.TickVitalRegeneration(kTick);
            ++restTicks;
        }
        std::printf("   standing still refills the pool in %.1f s\n", restTicks / 25.0);
        Check(restTicks > 0 && restTicks < 25 * 600, "standing still brings the whole pool back");
        Check(restTicks < diagonal, "faster than the walk that spent it, since nothing opposes it");

        // The other two pools recover on the same 2s/4s schedule with
        // nothing spending them, so a fight's cost is paid back by walking
        // to the next one -- which is what makes the bars readable.
        resetRegenPhase(p);
        p.SetHealth(1);
        p.SetActorMagicka(0);
        int healTicks = 0;
        while ((p.health() < p.maxHealth() || p.magicka() < p.maxMagicka()) &&
               healTicks < 25 * 3600) {
            p.TickVitalRegeneration(kTick);
            ++healTicks;
        }
        // Note this character is still wearing the bandage from part 7, so
        // health is coming back at 2+3 rather than 2 -- which is most of
        // why the number below is 82 seconds and not 204.
        std::printf("   1 hp / 0 mp back to full in %.0f s (with the bandage still worn)\n",
                    healTicks / 25.0);
        Check(healTicks < 25 * 3600, "health and magicka both refill unaided too");
        Check(healTicks / 25.0 > 60.0,
              "and health takes over a minute -- a few points every 4 seconds is a rest, not a heal");
    }

    std::printf("\nm73_vitals_smoke: %s (%d checks)\n",
                g_failures == 0 ? "PASSED (all checks)" : "FAILED", g_checks);
    return g_failures == 0 ? 0 : 1;
}
