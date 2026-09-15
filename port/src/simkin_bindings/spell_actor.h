#pragma once

// M43: what the spell dispatcher actually talks to.
//
// The long-standing shape of this port was that a spell is cast *by the
// player* *at a monster*. Neither half is true in the shipped game, and the
// status-effect dispatcher FUN_100458e4 is written to be symmetric about
// both:
//
//   * The caster is the spell's own **owner** (`spell+0x170`), not "the
//     player". The player's spells have the player as owner; a creature's
//     spells have the creature, set by the Monster class's AddSpell
//     binding (dispatcher 0x10084924 case 8, which calls
//     `FUN_1006d510(spell, monster)` to do exactly that). 32 shipped
//     creature scripts call AddSpell.
//   * The target is whatever was passed in. The dispatcher's first two
//     lines resolve *both* through the same `FUN_1002fd30`, so the player
//     is a legal target throughout, and several branches test which of the
//     two they got.
//
// `FUN_1002fd30` is worth quoting, because the whole of this interface
// falls out of it:
//
//     if (actor->vtable[0xe4]())        return actor + 0x224;   // a monster
//     else if (actor->vtable[0xcc]())   return actor + 0x3ac;   // the player
//     else                              return 0;
//
// Two vtable predicates and one shared stats-block layout. That pins the
// two predicates' meanings for good -- 0xe4 is "is a monster", 0xcc is "is
// the player" -- which in turn corrects three branch readings this port had
// backwards or unknown; see item_executable.cpp's DoAttackRoll.
//
// Implemented by MonsterExecutable and PlayerExecutable. ResolveSpellActor
// below is FUN_1002fd30 itself.

#include "simkin_bindings/actor_stats.h"

class skiExecutable;

namespace sk_bindings {

class SpellActor {
public:
    virtual ~SpellActor() = default;

    // The two real vtable predicates. Exactly one is true.
    virtual bool isPlayerActor() const = 0;   // vtable +0xcc
    virtual bool isMonsterActor() const = 0;  // vtable +0xe4

    // The stats block itself -- where every timed status effect lives.
    virtual ActorStats& actorStats() = 0;
    const ActorStats& actorStats() const {
        return const_cast<SpellActor*>(this)->actorStats();
    }

    // M58: FUN_1004ad40's field map, as one accessor instead of a 28-case
    // switch. The engine's stats block is one struct with every field at a
    // fixed offset, so the applier can just index it; this port keeps those
    // fields on the two owners (which is where the rest of the port already
    // reads them from), so the owner has to hand out the storage.
    //
    // Returns null when this actor has no field for that stat -- a creature
    // has no Personality, and the real engine's answer there is that the
    // creature's stats block has the field and nothing ever writes it. Null
    // makes the effect a no-op instead, which is the same observable.
    //
    // Health is the one stat this is *not* used to write: FUN_1004ad40's
    // health arm clamps against max and zero, which is exactly what the
    // owners' SetActorHealth already does (and only they know how to flip
    // an alive flag), so the applier reads through the slot and writes
    // through the setter. See ApplyEffectStat.
    virtual int* EffectStatSlot(int /*stat*/) { return nullptr; }
    const int* EffectStatSlot(int stat) const {
        return const_cast<SpellActor*>(this)->EffectStatSlot(stat);
    }
    // Convenience: the field's value, or 0 when this actor has no such field.
    int EffectStatValue(int stat) const {
        const int* slot = EffectStatSlot(stat);
        return slot ? *slot : 0;
    }

    // `stats+0x34`. The magnitude behind every status-effect branch, but
    // only when the caster is the player -- see DoAttackRoll's magnitude
    // rule, which falls back to the spell's own SetLevel otherwise.
    virtual int actorLevel() const = 0;

    // FUN_1004bc60 / FUN_1004bbd0, both computed from the same stats-block
    // fields on either side (combat.h).
    virtual int spellToHit() const = 0;
    virtual int spellResistance() const = 0;

    // M48: the two remaining pools on the shared stats block, which the
    // real cast (spell_cast.h) reads and writes on whoever is casting:
    //
    //   +0x2c  fatigue, clamped against +0x26 by FUN_1004bb54
    //   +0x2e  magicka, clamped against +0x28 by FUN_1004bb20
    //
    // A creature has both in the real engine for exactly the reason it has
    // the rest of this block -- FUN_10046764 deducts a creature's magicka
    // the same way it deducts the player's; it just does not *gate* on it
    // (the `bVar3` branch), so a creature can always cast. No shipped
    // creature script sets either pool, so a monster's start at 0 and the
    // deduction clamps straight back to 0, which is what the engine does.
    virtual int actorMagicka() const { return 0; }
    virtual int actorMaxMagicka() const { return 0; }
    virtual void SetActorMagicka(int /*value*/) {}
    virtual int actorFatigue() const { return 0; }
    virtual int actorMaxFatigue() const { return 0; }
    virtual void SetActorFatigue(int /*value*/) {}

    // ---- M64: the two things the derived-stat recompute's *player* arm
    // needs, and the reason M58 could not take that arm at all. ----
    //
    // FUN_10049698 branches on `vtable[0xcc]` -- the player predicate
    // isPlayerActor() already answers -- and then does two things a
    // creature's arm does not: it looks the character's class row up
    // (`FUN_100450d4`) and **returns without writing max magicka at all**
    // if that row says the class has none, and it adds two per-character
    // bonus words (player +0xfb8 and +0xfba) on top of intelligence.
    //
    // Both defaults here are the creature's answer, so a monster keeps the
    // arm it already had: max magicka is its intelligence, unconditionally.
    virtual bool actorClassHasMagic() const { return true; }
    virtual int actorMagickaBonus() const { return 0; }

    // M48: FUN_1003e6f4 -- "is the player carrying entity typeId 0x328",
    // which takes 6 off every spell's cost. Only the player half of the
    // cast consults it. 808 is not a typeId the shipped entities.txt
    // defines, so nothing in the retail game answers true; the hook is here
    // because the branch is, not because it fires.
    virtual bool actorHasSpellCostDiscount() const { return false; }

    // M49: the two ranged-combat ratings a projectile impact resolves with
    // -- FUN_10047f64 (attack) and FUN_1004801c (defense), the same pair the
    // melee path already goes through. An arrow needs both from either side,
    // since a creature's shot is resolved by exactly the same code as the
    // player's; the concrete classes already compute them for melee.
    virtual int actorAttackRating() const { return 0; }
    virtual int actorDefenseRating() const { return 0; }

    // FUN_1004bb88 (clamped into [0, max]) and the DoDamage vtable slot.
    //
    // M97: the slot's third argument, which this port dropped. The engine's
    // is `stats->vtable[0x10](stats, damage, attackerStats, p4, p5)`, and
    // FUN_10049e78 hands `attackerStats` on to the death slot (`vt[0x28]`)
    // when the hit is fatal -- that is the only way a killer is ever known,
    // and the killer is who the creature's expWorth is paid to. Null is a
    // real value, not a default: a script's DoDamage, a poison tick and a
    // burn all pass 0, and their deaths pay nobody.
    //
    // M98: and its fifth, p5 -- `ranged` here. 1 at the ranged sites (an
    // arrow's general sweep, DoAttackRoll, a projectile's impact, AzraWrath),
    // 0 everywhere else, and read only by the snowray gate. The attacker
    // itself now also feeds the damage: see stats_damage.h.
    virtual int actorHealth() const = 0;
    virtual void SetActorHealth(int value) = 0;
    virtual void ApplyActorDamage(int amount, SpellActor* attacker, bool ranged) = 0;
    virtual bool actorAlive() const = 0;

    // M98: the two per-character fields FUN_10049e78's class terms read,
    // `player+0xf38` (the class) and `player+0xfb0` (its ability rank). Both
    // are reached only after a `vtable[0xcc]` player test, so a creature's
    // answers never matter; -1 is simply no class at all.
    virtual int actorCharacterClass() const { return -1; }
    virtual int actorSpecialAbility() const { return 0; }

    // M98: `entity+0x1e2`, SetInvulnerable -- one of DoAttackRoll's three
    // entry refusals. Only a creature script ever sets it.
    virtual bool actorInvulnerable() const { return false; }

    // M97: FUN_1004a104, `stats->vtable[0x20]` -- the one AddExperience,
    // shared unoverridden by every stats vtable in the image. The player's
    // is PlayerExecutable::AddExperience; a creature's is the same function
    // taking its other arm (see MonsterExecutable::AddActorExperience).
    virtual void AddActorExperience(int amount) = 0;

    // The DeadToDust guard (`vtable+0xe4` then FUN_10086f88). Only a
    // creature can be undead, so the player's answer is always false.
    virtual bool actorUndead() const { return false; }

    // The Fear branch, which the real dispatcher applies only to a monster
    // target -- the player has no AI package to put into flee. Default
    // no-op so the player simply ignores it, as the engine does.
    virtual void SetActorAiPackageTimed(int /*package*/, int /*durationUnits*/) {}
};

// FUN_1002fd30: resolve a script-visible object to an actor, or null if it
// is neither a creature nor the player.
SpellActor* ResolveSpellActor(skiExecutable* obj);

}  // namespace sk_bindings
