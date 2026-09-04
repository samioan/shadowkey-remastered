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

    // `stats+0x34`. The magnitude behind every status-effect branch, but
    // only when the caster is the player -- see DoAttackRoll's magnitude
    // rule, which falls back to the spell's own SetLevel otherwise.
    virtual int actorLevel() const = 0;

    // FUN_1004bc60 / FUN_1004bbd0, both computed from the same stats-block
    // fields on either side (combat.h).
    virtual int spellToHit() const = 0;
    virtual int spellResistance() const = 0;

    // FUN_1004bb88 (clamped into [0, max]) and the DoDamage vtable slot.
    virtual int actorHealth() const = 0;
    virtual void SetActorHealth(int value) = 0;
    virtual void ApplyActorDamage(int amount) = 0;
    virtual bool actorAlive() const = 0;

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
