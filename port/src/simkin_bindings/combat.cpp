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

int SpellDamageAfterResistance(int rawDamage, int targetMagicResistance) {
    return (std::max)(1, rawDamage - targetMagicResistance);
}

int RollSpellDamage(int rating, int targetMagicResistance) {
    return SpellDamageAfterResistance(rating * 3, targetMagicResistance);
}

// M37: see combat.h -- these four are transcribed, not designed.
int SpellToHit(int spellcast, int willpower) { return spellcast + 2 * willpower; }

int SpellResistance(int magicResistance, int willpower) { return magicResistance + willpower / 5; }

int SpellHitChance(int casterPower, int targetResistance) {
    if (casterPower <= 0) return 0;
    return (casterPower << 16) / ((casterPower + targetResistance) * 0x100);
}

bool RollSpellHit(int casterPower, int targetResistance) {
    int chance = SpellHitChance(casterPower, targetResistance);
    if (chance == 0x100) return true;
    return (std::rand() % 0x101) < chance;
}

// M43: the real melee model, transcribed -- see combat.h.
int MeleeHitChance(int attackerAttack, int defenderDefense) {
    int total = attackerAttack + defenderDefense;
    if (total == 0) total = 1;
    // `if (total == attack) chance = 0x80` -- the zero-defense case, which
    // the real code deliberately caps at half rather than letting the
    // division produce a certainty.
    if (total == attackerAttack) return 0x80;
    return (attackerAttack << 16) / (total << 8);
}

bool RollMeleeHit(int attackerAttack, int defenderDefense) {
    return (std::rand() % 0x100) <= MeleeHitChance(attackerAttack, defenderDefense);
}

int RollDamage(int attackerAttack, int defenderDefense, int defenderArmor, int dmgMin, int dmgMax,
                bool halveRoll) {
    if (!RollMeleeHit(attackerAttack, defenderDefense)) return 0;  // miss

    int lo = (std::min)(dmgMin, dmgMax);
    int hi = (std::max)(dmgMin, dmgMax);
    // The real spread: `span = max - min; if (span < 1) span = 1;
    // roll = rand % span` -- exclusive at the top, so a 3..6 weapon rolls
    // 3, 4 or 5 and never 6. Transcribed rather than corrected.
    int span = hi - lo;
    if (span < 1) span = 1;
    int raw = lo + std::rand() % span;
    // M73: the exhaustion penalty lands here, on the roll itself, exactly
    // as FUN_100425bc applies it -- before mitigation, not after.
    if (halveRoll) raw >>= 1;
    // ...and the full armour rating comes off, not half of it, with a
    // blocked hit dealing literally nothing (`if (0 < damage) DoDamage`).
    int mitigated = raw - defenderArmor;
    return (std::max)(0, mitigated);
}

// M87: `FUN_100425bc`'s melee tail. See combat.h for the transcription and
// for why it is a separate function from RollDamage rather than a flag on
// it -- the roll itself is a different one.
PlayerMeleeResult RollPlayerMeleeDamage(int attackerAttack, int defenderDefense, int defenderArmor,
                                         int dmgMin, int dmgMax, bool halveRoll,
                                         bool targetIsSpider, int spiderBonusMin,
                                         int spiderBonusMax, bool weaponIsMagickaEdge,
                                         int magicka) {
    PlayerMeleeResult out;
    // The same gate the creature path uses -- `FUN_1004b71c` wraps
    // FUN_1004b620, and the extra dodge/block test inside it is the one
    // piece of the real to-hit this port still does not reproduce (see
    // RollMeleeHit).
    if (!RollMeleeHit(attackerAttack, defenderDefense)) return out;
    out.hit = true;

    const int lo = (std::min)(dmgMin, dmgMax);
    const int hi = (std::max)(dmgMin, dmgMax);
    // `FUN_100730c8(rng, min, max)` == `min + rand % (max - min + 1)`.
    // Inclusive, unlike the creature's own roll.
    int raw = lo + std::rand() % ((hi - lo) + 1);
    // The exhaustion halving, on the roll and before every bonus -- which
    // is where FUN_100425bc has it, one line after the roll.
    if (halveRoll) raw >>= 1;

    // The Spider Impaler. Note this bonus is *not* halved by exhaustion
    // and the base roll is: the order is roll, halve, then add.
    if (targetIsSpider) {
        const int span = spiderBonusMax - spiderBonusMin;
        if (span > 0) raw += spiderBonusMin + std::rand() % span;
    }

    // The Magicka Edge Axe. Both conditions are the real ones, including
    // the strict `10 <` on the pool -- at exactly ten magicka it does not
    // fire, and it never takes the player below zero because of it.
    if (weaponIsMagickaEdge && magicka > kMagickaEdgeProcCost &&
        (std::rand() % 101) < kMagickaEdgeProcChance) {
        raw += kMagickaEdgeProcDamage;
        out.magickaSpent = kMagickaEdgeProcCost;
    }

    out.damage = (std::max)(0, raw - defenderArmor);
    return out;
}

bool InInteractRange(float playerX, float playerY, float playerYaw, float targetX, float targetY,
                      float range, float minFacing) {
    float dx = targetX - playerX, dy = targetY - playerY;
    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist > range) return false;
    // Standing exactly on it counts -- there's no meaningful facing
    // direction at zero distance, and refusing here is how a player
    // standing on top of a small item ends up with no prompt at all.
    if (dist < 1.0f) return true;
    return (std::cos(playerYaw) * dx + std::sin(playerYaw) * dy) / dist >= minFacing;
}

}  // namespace sk_bindings
