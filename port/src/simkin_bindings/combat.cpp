#include "simkin_bindings/combat.h"

#include <algorithm>
#include <cstdlib>

namespace sk_bindings {

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
