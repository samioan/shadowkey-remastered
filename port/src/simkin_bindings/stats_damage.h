#pragma once

// M98: the middle of the stats block's DoDamage, `FUN_10049e78`, which is
// where the *attacker* changes the number.
//
// Every damage site in the image ends in the same virtual call,
// `stats->vtable[0x10](victim, damage, attackerStats, p4, p5)`. Each owner's
// slot does its own business first (the player's `FUN_10044814` arms the hurt
// timer; the creature's `FUN_10081844` gates, aggroes, plays its hit noise
// and flashes red) and then both call FUN_10049e78, which in full is:
//
//     if (victim->+0x7c == 4) return 0;                   // Sanctuary
//     short dmg = damage;
//     victim->vtable[0x1c](victim, &dmg, attacker);       // the Knight
//     if (dmg < 0) dmg = 0;
//     if (attacker) {
//         if (attacker->Strength > 4)                     // +0x14
//             dmg += (Strength + StrengthBonus) / 5;      // +0x10
//         owner = attacker->+0x50;
//         if (owner->vtable[0xcc]() && owner->+0xf38 == 0)
//             FUN_10044910(owner, &dmg);                  // the Assassin
//     }
//     if (victim->+0x7c == 9 && owner && owner->vtable[0xe4]() &&
//         victim->+0x50 && p5 == 1) return 0;             // snowray
//     if (victim->health <= dmg) { health = 0; vtable[0x28](victim, attacker, p4); return 1; }
//     victim->health -= dmg;  return 0;
//
// ResolveStatsDamage is the part between the Sanctuary gate and the health
// write. The two owners' ApplyDamage keep the gate and the write, because
// only they know how to die.
//
// The terms, and what they do to play:
//
//   * **Strength.** Every hit from an attacker whose Strength is above 4 gets
//     a fifth of Strength plus the strength bonus, *after* the armour has
//     come off (the melee site subtracts armour before the call) and after
//     the clamp -- so a blow the armour fully absorbed still lands the
//     strength term. A creature's stats block has Strength 0 (no creature
//     native writes +0x14), so in the shipped game this is the player's:
//     about +10 on every swing, arrow and damaging spell a 50-Strength
//     character lands.
//   * **The Assassin** (class 0) multiplies the strength-inclusive figure by
//     `1 + (rank*13 + 25) / 256` -- 14.8% at rank 1, 19.9% at rank 2.
//   * **The Knight** (class 3) is the victim side, `vtable[0x1c]`, which is
//     a no-op (0x1004b770) in a creature's stats vtable. See
//     KnightHalvesAbove below for the roll, which reads backwards.
//   * **Snowray** (periodic kind 9) refuses the hit when the attacker is a
//     creature and the site passed p5 = 1. The sites that do are the
//     ranged ones: an arrow's general sweep, DoAttackRoll's damage, a spell
//     projectile's impact and AzraWrath. Melee and the player's same-tile
//     arrow pass pass 0 -- the second can never meet the gate anyway, its
//     attacker being the player.

#include <cstdint>

namespace sk_bindings {

class SpellActor;

struct StatsDamage {
    // What comes off health, as the engine's 16-bit local holds it. Never
    // negative after the clamp unless the strength term is -- which needs a
    // strength bonus below minus Strength, and nothing ships one.
    int damage = 0;
    // The snowray gate fired: health is not touched and there is no death.
    bool refused = false;
};

// `ranged` is the call site's p5 (see the list above).
StatsDamage ResolveStatsDamage(const SpellActor& victim, int damage, const SpellActor* attacker,
                               bool ranged);

// ---- the pieces, exposed for the smoke test ----

// `if (attacker->+0x14 > 4) dmg += (+0x14 + +0x10) / 5`. The division is
// `smull` by 0x66666667 with the sign correction, i.e. C's truncation.
int StrengthDamageTerm(int strength, int strengthBonus);

// FUN_10044910: `dmg += (s16)((dmg << 8) * (rank*13 + 25) >> 16)`.
int AssassinDamage(int damage, int rank);

// FUN_10044950's threshold, `((rank << 16) / ((rank + dmg) << 8)) * 100 >> 8`
// -- rank/(rank+damage) as a percentage, truncated twice. The hit is halved
// when `rand(0, 100)` (inclusive, 101 outcomes) is **greater** than this:
//
//     cmp r0, r4, asr #8 ; ble skip
//
// So the halving chance *falls* as the Knight's rank rises and *rises* with
// the size of the hit: a rank-1 Knight halves a 10-point hit 92 times in 101
// (threshold 8), a rank-10 one 50 in 101 (threshold 50). Transcribed, not
// corrected. Only called for damage > 0.
int KnightHalvesAbove(int rank, int damage);

// The halving itself: `dmg - (dmg >> 1)`, rounding *up* (7 -> 4), floored at
// zero through the sign bit.
int KnightHalvedDamage(int damage);

}  // namespace sk_bindings
