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
// `targetMagicResistance` the real monster's SetMagicResistance() (M12,
// stored but never read back until now). Deliberately from-scratch, same
// "no RE ground truth for the real formula" footing as RollDamage() --
// real DoAttackRoll(target, effectId) is a native call this port doesn't
// have decompiled, and its own effectId argument selects a real status
// effect (poison/paralyze/blind/fear/drain/...) this port doesn't model
// at all (a separate, much larger, mostly-native subsystem -- not
// attempted here, only the damage half).
int RollSpellDamage(int rating, int targetMagicResistance);

}  // namespace sk_bindings
