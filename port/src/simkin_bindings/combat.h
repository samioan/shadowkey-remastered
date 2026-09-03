#pragma once

// Combat vertical-slice: shared damage-roll formula for both attack
// directions (player-on-monster, monster-on-player). Deliberately
// from-scratch -- no RE ground truth exists for the real
// attack/defense-to-damage formula (docs/WORLD_MODEL.md notes the real
// actor combat/stats system was never traced past the ~250-method
// native surface itself; the Character-stats class's own DoDamage/
// GetAttack/GetDefense/TestStrength were never decompiled), same
// footing as render3d/camera.h's kEyeHeightOffset or main.cpp's gravity
// constants -- a documented design choice, not a recovered constant.

namespace sk_bindings {

// hitChance% = clamp(50 + (attackerAttack - defenderDefense) * 5, 10, 95);
// on a hit, damage = max(1, RandomInRange(dmgMin, dmgMax) - defenderArmor / 2);
// a miss deals 0. Simple and transparent on purpose -- easy to retune
// once real playtesting (this port can't self-verify "does this feel
// right") says otherwise.
int RollDamage(int attackerAttack, int defenderDefense, int defenderArmor, int dmgMin, int dmgMax);

// M20 (ranged weapons): true if (targetX,targetY) is within `range` world
// units of (attackerX,attackerY) and inside a 60-degree forward-facing
// cone given the attacker's yaw (radians) -- the same nearest-in-cone
// shape main.cpp's tryAttack/findNearby* lambdas already use inline,
// factored out here so the real per-weapon range values
// (ItemExecutable::range(), e.g. a real bow script's SetRange(16384))
// are verifiable without needing the full windowed game loop. No line-
// of-sight/wall check -- same "no RE ground truth, from-scratch and
// deliberately simple" footing as RollDamage() above.
bool InAttackRange(float attackerX, float attackerY, float attackerYaw, float targetX,
                    float targetY, float range);

// M30: the same nearest-in-cone rule for the **Use** action (doors, NPCs,
// world items and containers), with the cone width made explicit --
// `minFacing` is the minimum cosine of the angle between the player's
// facing and the direction to the target, so 0.5 is a 60-degree cone and
// 0.30 about 72.
//
// Factored out of main.cpp's three near-identical findNearby* lambdas so
// the numbers are verifiable against real placement data without the
// windowed game loop. The reach this is called with (384 raw world units)
// is the game's own melee reach -- every real melee weapon in the corpus
// calls SetRange(384) -- rather than an invented value; the previous 140
// was barely half a tile, which is why interact prompts almost never
// appeared.
bool InInteractRange(float playerX, float playerY, float playerYaw, float targetX, float targetY,
                      float range, float minFacing);

// M22 (spellcasting): a spell's damage roll -- `rating` is the casting
// item's own real SetRating() value (e.g. blaze.s's SetRating(2)),
// `targetMagicResistance` the real monster's SetMagicResistance() (M12).
// Deliberately from-scratch, same "no RE ground truth for the real
// formula" footing as RollDamage().
//
// M33/M34 update: the *effect* half is no longer unmodelled -- the real
// selector (FUN_100458e4) is decompiled and nine of its branches are
// implemented (ItemExecutable::statusEffect()), several with their exact
// shipped constants. Two of those branches, Absorb and IgniteFoe, carry
// their own real damage formula and so bypass this function entirely,
// using only the resistance step below.
int RollSpellDamage(int rating, int targetMagicResistance);

// M34: the resistance step on its own, factored out of RollSpellDamage()
// (which is now exactly `SpellDamageAfterResistance(rating * 3, ...)`, an
// identity refactor -- its behaviour is unchanged).
//
// M37 retired this from the status-effect dispatcher: the real engine does
// not subtract resistance from spell damage at all, it resolves resistance
// as a *hit chance* gate in front of the whole effect (RollSpellHit()
// below). Kept only for RollSpellDamage()'s own from-scratch path, which
// covers spells that are not in the real dispatcher's typeId table.
int SpellDamageAfterResistance(int rawDamage, int targetMagicResistance);

// ---------------------------------------------------------------------
// M37: the real magic to-hit model. Unlike everything above, none of this
// is from-scratch -- all four functions are transcriptions of decompiled
// code, and the two stat formulas are each confirmed twice over (once in
// the standalone helper the dispatcher calls, once in the Character-stats
// native binding a script can call directly, which compute the same thing
// inline).
//
// The stats block's own layout was recovered the same pass, from
// FUN_1004ad40's index switch cross-checked against FUN_10048244's
// getters: `+0x04` spellcast, `+0x06` magic resistance, `+0x1a` willpower,
// `+0x34` character level.

// `GetSpellToHit` (Character-stats index 3) / FUN_1004bc60 -- how hard
// this caster's magic lands: `spellcast + 2 * willpower`.
//
// Both real functions add a further enchantment term for an equipped item
// whose enchantment type is 4 (to-hit) or 7 (resistance, in the function
// below); this port has no enchantment objects to read one from, and the
// omission is a missing bonus rather than a wrong formula.
int SpellToHit(int spellcast, int willpower);

// `GetSpellResistance` (Character-stats index 4) / FUN_1004bbd0 -- how
// hard it is to land magic on this target: `magicResistance +
// willpower / 5`. The /5 is a magic-multiply in the binary (0x66666667
// with a 33-bit shift), not a written division.
int SpellResistance(int magicResistance, int willpower);

// The gate itself, from FUN_100458e4:
//
//   chance = (power << 16) / ((power + resistance) * 0x100)
//
// i.e. `power * 256 / (power + resistance)`, in 0..0x100 -- kept in the
// real integer order so the truncation matches step for step. A caster
// with no power at all (`power <= 0`) gets 0 and can never land anything.
int SpellHitChance(int casterPower, int targetResistance);

// `chance == 0x100 || rand(0, 0x100) < chance`. The `== 0x100` special
// case is the engine's own and is load-bearing: a zero-resistance target
// yields exactly 0x100, which a `< chance` roll over [0, 0x100] would
// still miss 1 time in 257.
//
// This gates the **entire** effect, status included -- the real dispatcher
// does all its damage and status work inside this branch, so a resisted
// Poison applies no poison at all rather than a weakened one.
bool RollSpellHit(int casterPower, int targetResistance);

}  // namespace sk_bindings
