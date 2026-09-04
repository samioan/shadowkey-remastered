#include "simkin_bindings/spell_cast.h"

#include <algorithm>
#include <cstdint>

namespace sk_bindings {

namespace {

// FUN_10046764's branch tree, flattened. Every row is read straight off the
// decompiled comparison chain; see spell_cast.h for the three independent
// checks the whole table survives.
//
// The three cost shapes the tree produces are all here: `level + bonus` (the
// overwhelming majority), a fixed cost (4006 free, 4014/4015 free, 4026 a
// flat 12), and the default arm's `level + 6` for a typeId with no case.
constexpr SpellCastRule kRules[] = {
    // typeId  bonus  fixed  sound             sprite  impact
    {50, 6, false, kCastSoundFire, 2, 0, false},        // blaze.s
    {51, 10, false, kCastSoundHeal, 0, 0, false},       // HealWound.s
    {4002, 6, false, kCastSoundFire, 5, 0, false},      // spells\DeadToDust.s
    {4006, 0, true, kCastSoundFire, 2, 50, false},      // blaze.s, the greater one
    {4008, 7, false, kCastSoundFire, 5, 0, false},      // spells\Weakness.s
    {4009, 20, false, kCastSoundFire, 5, 0, false},     // spells\Absorb.s
    {4010, 8, false, kCastSoundFire, 2, 0, false},      // spells\Blind.s
    {4011, 14, false, kCastSoundPowerUp, 0, 0, false},  // spells\RaiseStrength.s
    {4012, 30, false, kCastSoundFire, 2, 0, false},     // spells\DoomHammer.s
    {4013, 3, false, kCastSoundPowerUp, 0, 0, false},   // spells\BodyToMind.s
    {4014, 0, true, kCastSoundPowerUp, 0, 0, false},    // spells\CureDisease.s
    {4015, 0, true, kCastSoundPowerUp, 0, 0, false},    // spells\CurePoison.s
    {4016, 18, false, kCastSoundFire, 0, 0, false},     // spells\DaedricWeapon.s
    {4017, 30, false, kCastSoundFire, 2, 0, false},     // spells\DoomHammer.s again
    {4018, 20, false, kCastSoundFire, 5, 0, false},     // spells\Drain.s
    {4019, 6, false, kCastSoundPowerUp, 0, 0, false},   // spells\Energize.s
    {4020, 15, false, kCastSoundFire, 5, 0, false},     // spells\Fear.s
    {4021, 5, false, kCastSoundFire, 5, 0, false},      // spells\FeebleBlade.s
    {4022, 16, false, kCastSoundPowerUp, 0, 0, false},  // spells\Frenzy.s
    {4023, 8, false, kCastSoundFire, 5, 0, false},      // spells\HarmArmor.s
    {4024, 8, false, kCastSoundFire, 5, 0, false},      // spells\IgniteFoe.s
    {4025, 19, false, kCastSoundFire, 5, 0, false},     // spells\Paralyze.s
    {4026, 12, true, kCastSoundPowerUp, 0, 0, false},   // spells\RemoveEnchantment.s
    {4027, 7, false, kCastSoundPowerUp, 0, 0, false},   // spells\Righteousness.s
    {4028, 2, false, kCastSoundPowerUp, 0, 0, false},   // spells\Sanctuary.s
    {4029, 6, false, kCastSoundPowerUp, 0, 0, false},   // spells\Shield.s
    {4033, 6, false, kCastSoundFire, 5, 0, false},      // spells\Disease.s
    {4034, 6, false, kCastSoundFire, 5, 0, false},      // spells\Poison.s
    {4035, 25, false, kCastSoundFire, 5, 0, false},     // spells\DeathHowl.s
    {4038, 20, false, kCastSoundFire, 0, 0, false},     // spells\AzraWrath.s
    {4039, 20, false, kCastSoundPowerUp, 0, 0, false},  // spells\AzraSustenance.s
};

}  // namespace

SpellCastRule SpellCastRuleFor(int typeId) {
    for (const SpellCastRule& rule : kRules) {
        if (rule.typeId == typeId) return rule;
    }
    // LAB_10046a58, the default arm: sprintf a warning, charge `level + 6`,
    // play the fire sound, spawn nothing.
    SpellCastRule unknown;
    unknown.typeId = typeId;
    unknown.unknown = true;
    return unknown;
}

bool SpellCastAllowed(const ItemExecutable& spell, const SpellCastCooldown& cooldown,
                      int nowUnits) {
    const int refire = spell.refireRate();
    if (cooldown.lastCastTime != 0 && nowUnits < cooldown.lastCastTime + refire) return false;
    if (spell.spellTypeId() == 4028 && nowUnits < cooldown.sanctuaryTime + refire) return false;
    return true;
}

void NoteSpellCast(SpellCastCooldown& cooldown, int nowUnits) { cooldown.lastCastTime = nowUnits; }

SpellCastResult CastSpell(ItemExecutable& spell, SpellActor& caster) {
    SpellCastResult out;
    const int typeId = spell.spellTypeId();
    const SpellCastRule rule = SpellCastRuleFor(typeId);
    const int level = caster.actorLevel();

    // The one write the real first switch makes, and it happens *before*
    // the affordability gate, so a failed AzraWrath still re-arms the
    // cooldown. The only place in the engine that sets a refire rate at
    // runtime; every other spell's is whatever its script declared.
    if (typeId == 4038) spell.SetRefireRate(400);

    // ---- the cost ----
    int cost = rule.costIsFixed ? rule.costBonus : level + rule.costBonus;
    // `if (spell->scroll) cost = 0;` -- a scroll carries its own power and
    // costs the reader nothing.
    if (spell.scroll()) cost = 0;
    // FUN_1003e6f4's discount. Unreachable in the retail data (see
    // kSpellCostDiscountTypeId), so this is the shape, gated off.
    if (caster.isPlayerActor() && caster.actorHasSpellCostDiscount()) {
        cost = (std::max)(0, cost - kSpellCostDiscount);
    }

    // ---- the magnitude every branch below scales with ----
    //
    //   if (!scroll) { if (casterIsMonster) { castByMonster = true;
    //                                        magnitude = spell->SetLevel; }
    //                  else                  magnitude = casterLevel; }
    //   else                                 magnitude = spell->SetLevel;
    //
    // the same rule the status-effect dispatcher uses on the other side of
    // the projectile (item_executable.h's spellLevel() comment).
    bool castByMonster = false;
    int magnitude;
    if (!spell.scroll() && !caster.isMonsterActor()) {
        magnitude = level;
    } else {
        castByMonster = !spell.scroll() && caster.isMonsterActor();
        magnitude = spell.spellLevel();
    }

    // ---- the gate, and the deduction ----
    //
    // Note what is *not* gated: a scroll and a creature both skip the
    // affordability test entirely, and both still run the deduction, which
    // clamps at zero. So a creature with no magicka casts freely, which is
    // exactly how the 32 shipped caster scripts work -- none of them sets a
    // magicka pool at all.
    const int magicka = caster.actorMagicka();
    if (magicka < cost && !spell.scroll() && !castByMonster) return out;
    caster.SetActorMagicka(magicka - cost);

    out.cast = true;
    out.magickaSpent = cost;
    out.soundId = rule.soundId;
    out.magnitude = magnitude;
    out.projectileSprite = rule.projectileSprite;
    out.impactDamage = rule.impactDamage;
    out.consumeScroll = spell.scroll();

    // ---- the self-targeted half, applied inline ----
    //
    // This is the whole second switch of FUN_10046764, and it is the half
    // this port previously had nothing for: fifteen shipped spell scripts
    // define no HitTarget() at all, because everything they do happens
    // right here.
    ActorStats& stats = caster.actorStats();
    switch (typeId) {
        case 51: {
            // HealWound: `SetHealth(health + 6 + magnitude * 2)`, which
            // FUN_1004bb88 then clamps to the maximum.
            //
            // The real branch adds one more term for a player whose class
            // (`player+0xf3c`) is 7, read through a vtable slot on the stats
            // block. This port has no class perk table and the slot is not
            // decompiled, so the bonus is left out rather than invented.
            caster.SetActorHealth(caster.actorHealth() + 6 + magnitude * 2);
            break;
        }
        case 4011:
            // RaiseStrength: stat 9 (strength -- outside the three the
            // status dispatcher touches, so this port stores it without a
            // consumer), +magnitude*5 for magnitude*10 seconds.
            stats.ApplyStatModifier(9, magnitude * 5, magnitude * 10);
            break;
        case 4013: {
            // BodyToMind: pour all remaining fatigue into magicka. The real
            // branch reads `+0x2c`, adds it to the *current* magicka, and
            // zeroes the fatigue -- with both writes clamped by their own
            // setters, so overflowing magicka is simply capped.
            const int fatigue = caster.actorFatigue();
            if (fatigue > 0) {
                caster.SetActorMagicka(caster.actorMagicka() + fatigue);
                caster.SetActorFatigue(0);
            }
            break;
        }
        case 4014:
            // CureDisease: remove the two modifiers Disease applies (named
            // "DISEASE" and "DISEASE_DEF" in the real call), then clear the
            // disease flag.
            stats.RemoveStatModifier(ActorStats::kStatAttack);
            stats.RemoveStatModifier(ActorStats::kStatDefense);
            stats.ClearEffectFlag(ActorStats::kEffectFlagDisease);
            break;
        case 4015:
            // CurePoison, whose entire body is this one line.
            stats.ClearEffectFlag(ActorStats::kEffectFlagPoison);
            break;
        case 4016:
            // DaedricWeapon: conjure entity 4037 into the weapon slot, but
            // only if the caster does not already carry one. The lookup and
            // the creation both need an inventory, so they are the caller's.
            out.conjureTypeId = kConjuredWeaponTypeId;
            break;
        case 4019:
            // Energize: fatigue regeneration for magnitude*10 seconds.
            stats.ArmPeriodic(ActorStats::kPeriodicFatigueRegen, 1,
                              static_cast<int16_t>(magnitude * 0xa00));
            break;
        case 4022:
            // Frenzy: +magnitude attack for magnitude*5 seconds.
            stats.ApplyStatModifier(ActorStats::kStatAttack, magnitude, magnitude * 5);
            break;
        case 4026:
            // RemoveEnchantment: strip the debuffs, then clear blindness
            // (FUN_1004ba84(stats, 0, 0)).
            stats.RemoveNegativeStatModifiers();
            stats.ClearEffectFlag(ActorStats::kEffectFlagBlind);
            break;
        case 4027:
            // Righteousness: +magnitude to attack *and* armour, both for
            // magnitude+5 seconds. The only branch that applies two.
            stats.ApplyStatModifier(ActorStats::kStatAttack, magnitude, magnitude + 5);
            stats.ApplyStatModifier(ActorStats::kStatArmor, magnitude, magnitude + 5);
            break;
        case 4028:
            // Sanctuary: a bare kind-4 duration, magnitude*5 seconds.
            stats.ArmPeriodic(ActorStats::kPeriodicSanctuaryTimer, 1,
                              static_cast<int16_t>(magnitude * 0x500));
            break;
        case 4029:
            // Shield: +magnitude*2 armour for magnitude*10 seconds.
            stats.ApplyStatModifier(ActorStats::kStatArmor, magnitude * 2, magnitude * 10);
            break;
        case 4038:
            // AzraWrath: magnitude*4 damage to every creature in the level
            // within kAreaSpellRange (its refire rate was armed above).
            out.areaDamage = magnitude * 4;
            break;
        case 4039:
            // AzraSustenance: health regeneration for magnitude+10 seconds.
            stats.ArmPeriodic(ActorStats::kPeriodicHealthRegen, 1,
                              static_cast<int16_t>((magnitude + 10) * 0x100));
            break;
        default:
            break;
    }
    return out;
}

}  // namespace sk_bindings
