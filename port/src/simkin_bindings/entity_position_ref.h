#pragma once

// M62: the world-position bindings every placed thing inherits.
//
// SimKin class `0x14d08` (dispatcher `FUN_10061a60`) is the **Object/Entity
// base** -- the class a player, a monster, a door, an item and a prop all
// derive from. Five of its bindings are one small, self-contained feature:
//
//   0x30  SetPosition(x, y[, z])
//   0x31  SetPositionMirror(x, y[, z])     -- the replicated twin
//   0x34  GetPositionX()
//   0x35  GetPositionY()
//   0x36  GetPositionZ()
//
// 63 call sites across the shipped corpus, on four different receivers, and
// until now only `MonsterExecutable` implemented any of them. This is the
// same consolidation M60 did for the widget class (`row_owner_ref.h`): the
// engine has one implementation on one base class, so this port has one
// too, mixed into each binding class that is a placed entity.
//
// ---- What `SetPosition` actually does (case 0x30, decompiled) ----------
//
//     entity->vtable[0x14](x, y, argc == 3 ? z : 0);   // the plain move
//     entity->dx /* +0x98 */ = 0;                      // stop it dead
//     entity->dy /* +0xa0 */ = 0;
//     if (entity->vtable[0xc8]() && engine->tileGrid /* +0x6908 */ &&
//         Map_GetTileAt(engine, entity->x, entity->y)) {
//         FUN_100686e0(entity);                        // snap to the surface
//         entity->z += 0x80;
//     }
//
// Three things worth keeping out of that:
//
//  * **`z` is optional and defaults to 0.** The two-argument form is a real
//    branch in the case, not a convenience -- `SetPosition(x, y)` puts the
//    entity at height 0 and then (for an actor) lets the snap find the
//    real floor.
//
//  * **The motion deltas are zeroed.** A teleport cancels whatever the
//    entity was doing, so an actor mid-fall does not keep its velocity
//    across the jump. This port's player carries `gameVelZ`; main.cpp
//    zeroes it on the same event for the same reason.
//
//  * **The floor snap only happens for actors.** `vtable[0xc8]` is the
//    predicate that decides membership of the engine's *actor* list at
//    `engine+0x640` -- the list `SetZone`'s experience-budget loop walks,
//    filtering it further with the "is a monster" predicate at
//    `vtable[0xe4]`. A player and a monster are in it; a door, an item and
//    a prop are not.
//
//    That distinction is not academic: `gate.s` is a **portcullis**, and
//    its whole open/close mechanism is `z = GetPositionZ() +/- 1200;
//    SetPosition(x, y, z)`. Snapping that back to the floor would mean the
//    gate could never open. `isActorForPositioning()` below is that
//    predicate, and it is the one thing each mixing class has to answer.
//
// The snap itself is `Zone::SnapActorToGround()` (world/zone.h), because it
// needs the tile grid this binding layer deliberately does not have. Every
// class here therefore records the request and lets the host apply it at a
// safe point -- the same defer convention `PickupItem()` and M39's original
// monster teleport already used.

#include "skRValue.h"
#include "skRValueArray.h"
#include "skString.h"

namespace sk_bindings {

class EntityPositionRef {
public:
    virtual ~EntityPositionRef() = default;

    // Seeded by the host from the `.ent` placement at zone load, so a
    // script's `GetPositionX()` reads where the thing actually is rather
    // than 0 -- `gate.s` reads its own position before moving it, and would
    // otherwise teleport itself to the origin the first time it opened.
    void SetWorldPosition(int x, int y, int z) {
        m_X = x;
        m_Y = y;
        m_Z = z;
    }
    int positionX() const { return m_X; }
    int positionY() const { return m_Y; }
    int positionZ() const { return m_Z; }

    // Drained once per tick by the host, which mirrors the move onto the
    // live instance and applies the ground snap when
    // isActorForPositioning() says so. Returns false when nothing changed.
    bool TakePendingPosition(float& x, float& y, float& z) {
        if (!m_PositionDirty) return false;
        m_PositionDirty = false;
        x = static_cast<float>(m_X);
        y = static_cast<float>(m_Y);
        z = static_cast<float>(m_Z);
        return true;
    }

    // `vtable[0xc8]`: is this entity a member of the engine's actor list,
    // and therefore floor-snapped on a teleport? See the header comment.
    virtual bool isActorForPositioning() const { return false; }

protected:
    // Returns false if `methodName` is none of the five, so each class can
    // carry on to its own methods and then soft-fail -- the same "try, then
    // fall through" shape RowOwnerRef::HandleSharedWidgetNative() uses.
    bool HandleEntityPositionNative(const skString& methodName, skRValueArray& args,
                                    skRValue& returnValue);

private:
    int m_X = 0;
    int m_Y = 0;
    int m_Z = 0;
    bool m_PositionDirty = false;
};

}  // namespace sk_bindings
