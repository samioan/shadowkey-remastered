#pragma once

// M73: the vitals economy -- what *spends* health, fatigue and magicka, and
// what puts them back.
//
// The port had exactly one of these: M51's jump cost. Nothing else in the
// game touched fatigue, and no pool ever refilled on its own, so the three
// HUD bars were a one-way ratchet. All of it is recovered here; none of it
// is invented.
//
// ---- Where the stats block keeps these ----
//
// Everything below indexes the shared stats block (actor_stats.h), whose
// field map is pinned by the Character-stats dispatcher `FUN_10048244` --
// its `param_1` is a `short*`, so a `param_1[n]` in the decompiler's C is
// byte offset `2n`:
//
//     +0x14 Strength   +0x16 Intelligence  +0x18 Agility   +0x1a Willpower
//     +0x1c Speed      +0x1e Endurance     +0x20 Personality  +0x22 Luck
//     +0x24 maxHealth  +0x26 maxFatigue    +0x28 maxMagicka
//     +0x2a health     +0x2c fatigue       +0x2e magicka      +0x34 level
//
// and, new this milestone, the three regeneration accumulators the engine
// keeps right after the level:
//
//     +0x3c health accumulator   +0x3e magicka accumulator
//     +0x40 fatigue accumulator
//
// The player object carries that block at `player+0x3ac`, which is why the
// spend sites below read `player+0x3d8` (fatigue) and `player+0x3da`
// (magicka) directly rather than going through a setter.
//
// ---- The four things that spend fatigue ----
//
//   * **Jumping**, `FUN_10044400` (player vtable +0x234). Refuses outright
//     unless fatigue is strictly above 5, then spends 5. M51 already had
//     this; the constant simply moves here from audio/sound_mixing.h, which
//     was never its home -- it lived there because the jump also picks a
//     sound.
//   * **Attacking**, `FUN_100425bc` (player vtable +0x288). Its third
//     instruction after the two cooldown gates is `fatigue -= 4`, floored
//     at 0. It happens *before* the target search, before the melee/ranged
//     split and before any to-hit roll, so a whiff and a bare-fisted swing
//     cost the same as a connecting sword blow.
//   * **Casting**, `FUN_10042394` case 2 (player vtable +0x280, the
//     use-item dispatcher, whose selector is the item's own `+0x16c` kind).
//     `fatigue -= 3`, and only when both the refire gate `FUN_10046680` and
//     the cast `FUN_10046764` succeeded -- so a cast refused for want of
//     magicka is also free of fatigue. This is on top of the magicka the
//     cast itself charges (spell_cast.h).
//   * **Moving**, `FUN_10045228` (player vtable +0x1dc). The interesting
//     one, because it is not a per-key cost at all:
//
//         player->moveAccum += frameDelta;
//         if (player->moveAccum > 0xc4) {
//             SetFatigue(fatigue - 2);
//             player->moveAccum = 0;
//         }
//
//     Slot +0x1dc is a hook the *base* actor movement calls: all four of
//     `FUN_100063f0` (forward), `FUN_10006480` (back), `FUN_10006510`
//     (strafe left) and `FUN_100065b4` (strafe right) end by invoking it,
//     and the player's own overrides (`FUN_1001f2b4`..`FUN_1001f338`) are
//     each two calls -- the base move, then a second hook. So the drain
//     fires **once per direction actually moved this frame**, which means
//     holding forward *and* a strafe genuinely drains twice as fast.
//     Reproduced, including that.
//
//     There is no walk/run distinction to key this off: the engine has one
//     movement speed (`FUN_100445d4`, below). The one thing that looks like
//     a sprint, `engine+0xbe0c`, doubles the step -- and is written to 0 in
//     the engine constructor and never written again, so it is dead.
//
// ---- The two things low fatigue costs you ----
//
//   * **Half movement speed.** `FUN_100445d4` (player vtable +0x1e4) is the
//     per-frame step length every move slot is handed:
//
//         step = Speed << 8;
//         step >>= (fatigue < 1) ? 2 : 1;
//         if (step > 0x3fff) step = 0x4000;
//
//     -- one extra bit of right shift, i.e. exactly half, whenever the pool
//     is empty. This port keeps its own from-scratch `kMoveSpeed` (the
//     engine's step is in a fixed-point unit this port's float movement
//     does not share), so only the *ratio* is taken.
//   * **Half melee damage.** In `FUN_100425bc`'s melee branch, immediately
//     after the weapon's own `RandomRange(damageMin, damageMax)` roll:
//     `if (fatigue < 1) damage >>= 1`. It lands on the raw roll, before the
//     defender's mitigation comes off. The ranged branch has already
//     returned by that point, so **an arrow is not weakened by exhaustion**
//     -- only a swing is.
//
// ---- Regeneration ----
//
// `FUN_10049b64`, called from the player's own per-frame tick
// (`FUN_10045294`) immediately after the status-effect tick `FUN_10049780`.
// It has exactly one call site in the whole binary and it is that one, so
// **regeneration is the player's alone** -- a wounded creature stays
// wounded, which is why nothing on the monster side needs this.
//
// The shape is three independent accumulators, each `+= frameDelta` every
// frame and each with its own period; when one passes its period it resets
// to 0 and, *only if that pool is below its own maximum*, adds an amount
// derived from the character's attributes:
//
//     health   every > 0x400 units (4 s):  += Endurance / 25
//     magicka  every > 0x200 units (2 s):  += Willpower / 15
//     fatigue  every > 0x200 units (2 s):  += (Strength + Willpower) / 15
//
// The divisors are magic-number divides in the shipped code rather than
// literals -- `0x51eb851f` with `smull ... asr #3` after a `>> 32` is the
// standard signed reciprocal for 25, and `0x88888889` with the add-then-
// `asr #3` correction is the standard one for 15. Both were read off the
// disassembly at 0x10049c28 and 0x10049d34 rather than trusted from the
// decompiler's C.
//
// Each add is written through the same clamp the ordinary setters use, so
// regeneration can never overfill a pool, and the `pool < max` test in
// front means a full pool does not even burn its accumulator's reset.
//
// Three modifiers ride on top, all of them player-only (the real code
// reaches back through `stats+0x50` to the owner and asks its `vtable+0xcc`
// "are you the player" predicate first):
//
//   * A **High Elf** (race 3) adds `raceAbility * 5` to the willpower term
//     before the divide, so their magicka comes back faster as that rank
//     grows.
//   * A **Breton** (race 1) adds the same `raceAbility * 5` to the
//     strength+willpower term, for fatigue.
//   * Carrying **`items\azras_bandage.s`** (entities.txt typeId 4702) adds
//     a flat +3 to the health tick, tested with `FUN_10045474`, which walks
//     the two hand slots and the eight equipment slots.
//
// And one that does not ride on top, deliberately: at 0x10049c1c the health
// branch loads the owner's race and compares it against 7 (Wood Elf) --
// and then **never uses the flags**. The next instruction is the literal
// pool load for the divide, and both sides of the preceding branch converge
// on it. So a Wood Elf health bonus was written and compiled away; the
// shipped game has none, and neither does this. Faithful, and only visible
// in the disassembly -- the decompiler renders the dead compare as a
// discarded call.

#include <algorithm>

namespace sk_bindings {

// ---- What an action costs ----

// `FUN_10044400`: refused unless `fatigue > kJumpFatigueCost`, then spent.
constexpr int kJumpFatigueCost = 5;
// `FUN_100425bc`: spent up front by every swing and every shot alike.
constexpr int kAttackFatigueCost = 4;
// `FUN_10042394` case 2: spent only by a cast that actually happened.
constexpr int kCastFatigueCost = 3;
// `FUN_10045228`: this much, every time the accumulator passes the period.
constexpr int kMoveFatigueCost = 2;
constexpr int kMoveFatiguePeriod = 0xc4;  // engine delta units, ~0.77 s

// `FUN_10045228` itself. Returns the fatigue to spend this call -- 0 on the
// frames the accumulator has not filled yet. Pass one accumulator per
// actor; call it once per direction actually moved, which is what the four
// engine move slots do.
inline int MovementFatigueDrain(int& accumulator, int deltaUnits) {
    accumulator += deltaUnits;
    if (accumulator <= kMoveFatiguePeriod) return 0;
    accumulator = 0;
    return kMoveFatigueCost;
}

// ---- What an empty pool costs ----

// `FUN_100445d4`'s extra shift, as a ratio (see the header comment for why
// only the ratio is taken).
inline float ExhaustedSpeedScale(int fatigue) { return fatigue < 1 ? 0.5f : 1.0f; }

// `FUN_100425bc`'s `damage >>= 1`. Melee only.
inline bool ExhaustedMeleeHalvesDamage(int fatigue) { return fatigue < 1; }

// ---- Regeneration ----

// The three periods, in engine delta units (0x100 == one second).
constexpr int kHealthRegenPeriod = 0x400;   // 4 s
constexpr int kMagickaRegenPeriod = 0x200;  // 2 s
constexpr int kFatigueRegenPeriod = 0x200;  // 2 s

// The two divisors, both read off the magic-number divides.
constexpr int kHealthRegenDivisor = 25;
constexpr int kPoolRegenDivisor = 15;

// `items\azras_bandage.s`, and the flat bonus holding it adds to each
// health tick.
constexpr int kAzraBandageTypeId = 4702;
constexpr int kAzraBandageHealthBonus = 3;

// The per-rank racial bonus added *before* the divide, for the one race
// each pool favours.
constexpr int kRaceAbilityRegenBonus = 5;
constexpr int kMagickaRegenRace = 3;  // High Elf
constexpr int kFatigueRegenRace = 1;  // Breton

// One accumulator step. True means "this pool ticks now"; the accumulator
// is reset in that case and left alone otherwise. Note the comparison is
// strict, matching the shipped `cmp / ble` pair -- a period lands on the
// frame that passes it, not the one that reaches it.
inline bool RegenPeriodElapsed(int& accumulator, int periodUnits, int deltaUnits) {
    accumulator += deltaUnits;
    if (accumulator <= periodUnits) return false;
    accumulator = 0;
    return true;
}

// The three amounts. Each is the value added to the pool on a tick, before
// the setter's own clamp.
inline int HealthRegenAmount(int endurance, bool hasAzraBandage) {
    int amount = endurance / kHealthRegenDivisor;
    if (hasAzraBandage) amount += kAzraBandageHealthBonus;
    return amount;
}

inline int MagickaRegenAmount(int willpower, int race, int raceAbility) {
    int term = willpower;
    if (race == kMagickaRegenRace) term += raceAbility * kRaceAbilityRegenBonus;
    return term / kPoolRegenDivisor;
}

inline int FatigueRegenAmount(int strength, int willpower, int race, int raceAbility) {
    int term = strength + willpower;
    if (race == kFatigueRegenRace) term += raceAbility * kRaceAbilityRegenBonus;
    return term / kPoolRegenDivisor;
}

}  // namespace sk_bindings
