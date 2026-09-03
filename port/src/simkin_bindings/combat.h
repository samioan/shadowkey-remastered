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

}  // namespace sk_bindings
