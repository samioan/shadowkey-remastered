#pragma once

// M48: `FUN_10046764` -- what a real cast actually does.
//
// M43 decompiled this function and recorded its shape, but reproduced only
// its *effect*: this port called `HitTarget()` directly on both the player's
// and a creature's cast, so every spell was hitscan and the whole
// self-targeted half of the spell list did nothing at all. That is the gap
// docs/PORT_ROADMAP.md's "the rest of a real cast" bullet names, and this is
// the half of it that lives on the caster. The other half -- the projectile
// whose *impact* is what runs `HitTarget()` -- is spell_projectile.h.
//
// The real function is one 200-line branch tree over the spell entity's
// entities.txt typeId (`spell+0xc8`) that produces four things:
//
//   * the **magicka cost**, almost always `casterLevel + <per-spell bonus>`,
//   * the **sound slot** to play,
//   * whether to spawn a **projectile**, and with which art,
//   * and, for the spells that have no projectile, a **self-targeted
//     effect** applied inline right here.
//
// Reading a Ghidra branch tree that dense is exactly the kind of thing that
// silently goes wrong, so the table below is verified three independent
// ways against shipped game data, and all three agree:
//
//   1. **The `HitTarget` split is exact.** Every spell this table gives a
//      projectile has a `HitTarget[...]` handler in its shipped script, and
//      every spell it does not has none -- 15 scripts on each side, with
//      exactly one explained exception (`spells\AzraWrath.s`, whose damage
//      comes from the native area-of-effect branch instead, see
//      `SpellCastResult::areaDamage`).
//   2. **The sound slots name themselves.** Slot `0x54` is
//      `pl_cast_fire.wav` and slot `0x57` is `pl_cast_powerup.wav` in every
//      one of the 21 shipped per-zone sound manifests -- and this table
//      hands 0x54 to the offensive spells and 0x57 to the buffs and cures.
//      (`0x55`, which only HealWound uses, is `NULL.wav` in every zone.)
//   3. **Three typeIds are stated outright by scripts.**
//      `spells\U_Heal_Wound_Lvl10.s` calls `SetSpellType(51)`,
//      `spells\U_Frenzy_lvl10.s` `SetSpellType(4022)`, and
//      `spells\U_Blaze_lvl5.s` `SetSpellType(50)` -- three rows of the
//      branch tree read off independently, all matching.
//
// `CastSpell()` deliberately touches nothing outside the caster: it returns
// a description of what the *world* must then do (spawn a projectile, run
// an area effect, conjure an item, consume a scroll). That keeps the whole
// cast testable without a zone, and is why sk_bindings still does not link
// sk_world.

#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/spell_actor.h"

namespace sk_bindings {

// FUN_10046764's two sound slots, named by every shipped `*_sounds.txt`.
constexpr int kCastSoundFire = 0x54;     // pl_cast_fire.wav
constexpr int kCastSoundHeal = 0x55;     // NULL.wav in all 21 zones
constexpr int kCastSoundPowerUp = 0x57;  // pl_cast_powerup.wav

// `FUN_1003e6f4`: the player is wearing/carrying entity typeId 0x328, which
// takes 6 off every spell's cost. The real check walks the two equipped
// slots (`stats+0x48`, `stats+0x4c`) and the eight at `player+0xf8c`.
// 0x328 == 808 is not in the shipped entities.txt at all, so nothing in the
// retail game can satisfy it -- recorded, and the discount is exposed here
// as a caller-set flag rather than pretended to be reachable.
constexpr int kSpellCostDiscountTypeId = 0x328;
constexpr int kSpellCostDiscount = 6;

// FUN_1004720c's own literal: AzraWrath reaches every creature within 12000
// world units, i.e. about 47 tiles -- the whole level, in practice.
constexpr int kAreaSpellRange = 12000;

// `spells\DaedricWeapon.s` conjures entity 4037 (`weapons\DaedricSword.s`)
// into the caster's weapon slot, but only if they do not already have one.
constexpr int kConjuredWeaponTypeId = 4037;

// One row of the branch tree.
struct SpellCastRule {
    int typeId = 0;
    // Magicka cost. `casterLevel + costBonus`, unless `costIsFixed`.
    int costBonus = 6;
    bool costIsFixed = false;
    int soundId = kCastSoundFire;
    // `+0x134` on the projectile, and its whole single-frame animation
    // range. 0 means this spell spawns nothing. The two real values are 2
    // (blaze/Blind/DoomHammer) and 5 (everything else); what art they
    // select is *not* resolved -- they are not models.txt indices (2 is
    // lantern.bin, 5 is sbarrel.bin), so they index something else, and
    // this port carries the number without drawing it. See
    // spell_projectile.h.
    int projectileSprite = 0;
    // The projectile's `+0x182`: flat damage applied on impact *in addition*
    // to the script's own HitTarget(). Only 4006 sets it, to 50.
    int impactDamage = 0;
    // True when the typeId is not one of the branch tree's own cases -- the
    // real default arm sprintf's "Warning: entity %d does not define any
    // parameters." into a stack buffer and charges `level + 6` anyway.
    bool unknown = false;
};

// The table row for a typeId, including the default arm.
SpellCastRule SpellCastRuleFor(int typeId);

// FUN_10046680: the cast cooldown, which applies **only when the caster is
// the player**. Two timestamps on the player object, both in the engine's
// 1/256-second units:
//
//   +0xfc4  every successful cast stamps this
//   +0xfc8  stamped when a Sanctuary channel expires (actor_stats.h)
//
// and the gate is `now < stamp + spell->SetRefireRate()`. Only one shipped
// script sets a refire rate at all -- `spells\Sanctuary.s`'s
// `SetRefireRate(768); //3 secs`, whose comment is an independent
// confirmation of the 0x100-per-second time unit this port already uses
// everywhere. Every other spell leaves it 0, which makes the gate inert.
struct SpellCastCooldown {
    int lastCastTime = 0;   // player+0xfc4; 0 means "never cast", as in the real test
    int sanctuaryTime = 0;  // player+0xfc8
};
bool SpellCastAllowed(const ItemExecutable& spell, const SpellCastCooldown& cooldown, int nowUnits);
void NoteSpellCast(SpellCastCooldown& cooldown, int nowUnits);

// What the caller still has to do in the world.
struct SpellCastResult {
    bool cast = false;  // false = the caster could not afford it, or the gate refused
    int magickaSpent = 0;
    int soundId = 0;
    // The magnitude every branch scales with: the caster's level, or the
    // spell's own SetLevel for a scroll or a creature's spell.
    int magnitude = 0;
    // Spawn a projectile with this art index (0 = none), carrying this much
    // flat impact damage.
    int projectileSprite = 0;
    int impactDamage = 0;
    // FUN_1004720c (AzraWrath): this much damage to every creature within
    // kAreaSpellRange of the caster, the caster excluded.
    int areaDamage = 0;
    // FUN_10044e08 (DaedricWeapon): put this entity typeId in the caster's
    // weapon slot, unless they already own one.
    int conjureTypeId = 0;
    // The real tail: a scroll is destroyed after it is read.
    bool consumeScroll = false;
};

// FUN_10046764 itself. Deducts magicka, plays nothing (the caller owns
// audio), applies the self-targeted half inline, and describes the rest.
SpellCastResult CastSpell(ItemExecutable& spell, SpellActor& caster);

}  // namespace sk_bindings
