#include "simkin_bindings/combat.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace sk_bindings {

bool InAttackRange(float attackerX, float attackerY, float attackerYaw, float targetX,
                    float targetY, float range) {
    float dx = targetX - attackerX, dy = targetY - attackerY;
    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist > range || dist < 1.0f) return false;
    float fwdX = std::cos(attackerYaw), fwdY = std::sin(attackerYaw);
    float facing = (fwdX * dx + fwdY * dy) / dist;
    return facing >= 0.5f;  // ~60 degree forward cone
}

int RollDamage(int attackerAttack, int defenderDefense, int defenderArmor, int dmgMin,
                int dmgMax) {
    int hitChance = 50 + (attackerAttack - defenderDefense) * 5;
    hitChance = (std::max)(10, (std::min)(95, hitChance));
    if ((std::rand() % 100) >= hitChance) return 0;  // miss

    int lo = (std::min)(dmgMin, dmgMax);
    int hi = (std::max)(dmgMin, dmgMax);
    int raw = hi > lo ? lo + std::rand() % (hi - lo + 1) : lo;
    int mitigated = raw - defenderArmor / 2;
    return (std::max)(1, mitigated);
}

}  // namespace sk_bindings
