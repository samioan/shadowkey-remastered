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

}  // namespace sk_bindings
