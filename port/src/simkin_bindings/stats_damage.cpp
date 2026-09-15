#include "simkin_bindings/stats_damage.h"

#include <cstdlib>

#include "simkin_bindings/actor_stats.h"
#include "simkin_bindings/character_progression.h"
#include "simkin_bindings/effects.h"
#include "simkin_bindings/spell_actor.h"

namespace sk_bindings {

int StrengthDamageTerm(int strength, int strengthBonus) {
    const int s = static_cast<int16_t>(strength);
    if (s <= 4) return 0;
    return (s + static_cast<int16_t>(strengthBonus)) / 5;
}

int AssassinDamage(int damage, int rank) {
    const int d = static_cast<int16_t>(damage);
    return static_cast<int16_t>(d + ((d * 256 * (rank * 13 + 25)) >> 16));
}

int KnightHalvesAbove(int rank, int damage) {
    const int divisor = (rank + damage) << 8;
    // `__divsi3` would fault on zero; rank + damage is positive on every
    // path that gets here (damage > 0, and the rank starts at 1 and only
    // rises).
    if (divisor == 0) return 100;
    return ((rank * 65536) / divisor) * 100 >> 8;
}

int KnightHalvedDamage(int damage) {
    const int halved = static_cast<int16_t>(damage - (static_cast<int16_t>(damage) >> 1));
    return halved < 0 ? 0 : halved;
}

StatsDamage ResolveStatsDamage(const SpellActor& victim, int damage, const SpellActor* attacker,
                               bool ranged) {
    StatsDamage out;
    int dmg = static_cast<int16_t>(damage);

    // `victim->vtable[0x1c](victim, &dmg, attacker)`. The player's slot is a
    // `this -= 0x3ac` thunk to FUN_10044950; the creature's is a bare return.
    if (victim.isPlayerActor() && victim.actorCharacterClass() == kClassKnight && dmg > 0) {
        const int rank = victim.actorSpecialAbility();
        const int roll = std::rand() % 101;  // FUN_100730c8(rng, 0, 100)
        if (roll > KnightHalvesAbove(rank, dmg)) dmg = KnightHalvedDamage(dmg);
    }
    if (dmg < 0) dmg = 0;

    if (attacker) {
        dmg = static_cast<int16_t>(
            dmg + StrengthDamageTerm(attacker->EffectStatValue(kEffectStatStrengthProper),
                                     attacker->EffectStatValue(kEffectStatStrength)));
        if (attacker->isPlayerActor() && attacker->actorCharacterClass() == kClassAssassin) {
            dmg = AssassinDamage(dmg, attacker->actorSpecialAbility());
        }
        if (ranged && attacker->isMonsterActor() &&
            victim.actorStats().periodicKind() == ActorStats::kPeriodicSnowrayWard) {
            out.refused = true;
            return out;
        }
    }
    out.damage = dmg;
    return out;
}

}  // namespace sk_bindings
