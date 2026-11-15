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

// M137: FUN_1004b774, and FUN_100449f4's Khajiit arm -- see combat.h.
// `FUN_100730c8(rng, lo, hi)` is `lo + rand % (hi - lo + 1)`.
bool RollDodge(const DodgeStats& defender, int rollPlusChance) {
    const int p = rollPlusChance;
    int percent = 0;  // the caller's local, zeroed before the call
    if (defender.luck > 0x28) {
        const int scaled = (defender.luck - 0x28) * 0x10000;
        const int v = static_cast<int>((static_cast<long long>(0x66666667) * scaled) >> 0x29) -
                      (scaled >> 31);
        const int denominator = v + p * 0x100;
        const int chance = denominator != 0 ? (v * 0x100) / denominator : 0;
        percent = chance * 100 >> 8;
        if (std::rand() % 101 <= percent) return true;
    }
    if (defender.khajiitRank >= 0) {
        const int rank = defender.khajiitRank < 100 ? defender.khajiitRank << 16 : 0x640000;
        const int fraction =
            static_cast<int>((static_cast<long long>(0x51eb851f) * rank) >> 0x2d) - (rank >> 31);
        percent += (p * 0x100 * fraction) >> 16;
        if (std::rand() % (p + 1) < percent) return true;
    }
    return false;
}

bool RollMeleeHit(int attackerAttack, int defenderDefense, const DodgeStats* dodge) {
    const int roll = std::rand() % 0x100;
    const int chance = MeleeHitChance(attackerAttack, defenderDefense);
    if (roll > chance) return false;
    // FUN_1004b620: `if (!defender->vtable[0x14](defender, roll + chance, ...)) hit`.
    return !(dodge && RollDodge(*dodge, roll + chance));
}

int RollDamage(int attackerAttack, int defenderDefense, int defenderArmor, int dmgMin, int dmgMax,
                bool halveRoll, const DodgeStats* dodge) {
    if (!RollMeleeHit(attackerAttack, defenderDefense, dodge)) return 0;  // miss

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
                                         int magicka, const DodgeStats* dodge) {
    PlayerMeleeResult out;
    // The same gate the creature path uses -- `FUN_1004b71c` wraps
    // FUN_1004b620, and the extra dodge/block test inside it is the one
    // piece of the real to-hit this port still does not reproduce (see
    // RollMeleeHit).
    if (!RollMeleeHit(attackerAttack, defenderDefense, dodge)) return out;
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
