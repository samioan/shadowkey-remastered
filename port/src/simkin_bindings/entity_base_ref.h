#pragma once

// The bindings every placed thing inherits -- SimKin class `0x14d08`,
// dispatcher `FUN_10061a60`, the **Object/Entity base** a player, a monster,
// a door, an item and a prop all derive from.
//
// M62 created this file for five of its bindings (the position group) and
// called it `EntityPositionRef`. M92 finished the job: every `0x14d08`
// binding a shipped script actually calls now lives here, on one class,
// mixed into each binding class that is a placed entity -- which is the
// whole point. The engine has *one* implementation of `ShowEntity`, so a
// port that implements it on Monster and not on Door has not implemented
// it; it has implemented a coincidence. See the M92 entry in
// docs/PORT_ROADMAP.md for the census that made that concrete (a soft-fail
// log with `Door.SetName` 23 times, `Monster.SetPassable` 12 and
// `Item.PlaySound` 7 -- three names this port already had, on the wrong
// receivers).
//
// ---- The bindings, by dispatcher case ---------------------------------
//
//   0x04  RunScript(name)                 -- swap this entity's script
//   0x0e  ShowEntity(visible)
//   0x11  GetID()                         -- the .ent placement name
//   0x13  SetID(name)
//   0x14  SetName(stringTableId)
//   0x17  SetPassable(passable)
//   0x18  SetRotationTurn(raw)
//   0x1b  AddRotationTurn(raw)
//   0x23  SetModel(modelsTxtIndex)
//   0x25  PlaySound(id[, volume[, directional[, repeats]]])
//   0x30  SetPosition(x, y[, z])
//   0x31  SetPositionMirror(x, y[, z])    -- the replicated twin
//   0x32  MirrorMethod(name)              -- multiplayer only, see below
//   0x34  GetPositionX()
//   0x35  GetPositionY()
//   0x36  GetPositionZ()
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
//    `engine+0x640`. A player and a monster are in it; a door, an item and
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
// monster teleport already used, and the convention `ShowEntity`,
// `SetModel` and `RunScript` below follow too.
//
// ---- `ShowEntity` (case 0x0e) -----------------------------------------
//
//     bool v = args[0].boolValue();
//     entity->vtable[0x58](entity, v ^ 1);
//
// -- so slot 0x58 is `SetHidden`, and `ShowEntity` is its inverse. What
// hidden *means* is pinned by the engine's other caller of the same slot,
// the monster tick's corpse-decay branch (`FUN_10082224`): when the
// lifespan at `+0x2ec` runs out it calls `vtable[0x58](this, 1)` and then
// walks the actor list clearing every creature whose current target
// (`+0x20c`) was this one. A hidden entity is therefore not drawn, not
// targeted and not interacted with -- exactly what this port's existing
// `destroyed()` already meant (main.cpp's M23 comment says so in those
// words), except reversible. `MonsterExecutable::outOfWorld()` is the two
// of them together, and it is what main.cpp now asks.
//
// The shipped corpus is unambiguous about the "not interacted with" half:
// `dstar_w.s` and `fearfrst.s` both pair every `ShowEntity` with the
// matching `SetPassable` on the same entity one line later --
// `Porliss.ShowEntity(false); Porliss.SetPassable(true);` -- because a
// vanished NPC you could still walk into would be a wall in an empty room.
//
// ---- `MirrorMethod` (case 0x32) is multiplayer-only -------------------
//
//     if (engine->inMultiplayer /* +0x5c0 */) {
//         session = engine->+0x5d0;
//         session->vtable[0x150](session, entity, methodNameString);
//     }
//
// The whole case is inside that test: it asks the Bluetooth session to
// invoke the named method on this entity's *mirror* on the other handset.
// In single player it does nothing at all -- and this port answers
// `IsMultiplayer()` false everywhere (M91), which is what routes every
// script down its single-player branch. So all 33 shipped call sites are
// correctly no-ops here, the same conclusion `DoorOpened`,
// `DestroyObjectMirror` and `SetPositionMirror` already reached. It is
// counted rather than ignored (`mirroredMethodCount()`) so a test can tell
// "deliberately does nothing" from "never ran".
//
// ---- `SetName` takes an ID, not a string (case 0x14) ------------------
//
// The case branches on the argument's type tag and, for a string, puts up a
// dialog reading **"SetName() invalid argument" / "You must pass in an ID#
// now"** rather than setting anything. Only the integer form reaches the
// setter (`vtable[0x88]`), and the integer is a `stringtable.*` id. This
// port says the same thing on the console instead of a dialog; no shipped
// script trips it, which is presumably why the studio left the check in.
//
// ---- `GetID` is the `.ent` placement name (case 0x11) -----------------
//
// The case reads the buffer at `entity+0xcb` and returns it as a string;
// `SetID` (0x13) is the matching setter. `lakvan/sdoor_trapa.s` settles
// what seeds it: one script is shared by four placements and tells them
// apart with `if (GetID() = "11s")`, `"gg5"`, `"sdoor1"`, `"hh6"` -- and
// each of those four strings occurs exactly once in `lakvan.ent`. So the
// placement name is the id, and the host seeds it before `Init()` the same
// way it seeds the position.
//
// ---- `SetModel` is a `models.txt` row (case 0x23) ---------------------
//
// Three live call sites, and the manifest reads them straight:
// `monsters/ivgrizt.s`'s `SetModel(61)` is row 61, `goblin.bin`;
// `twilite/volstok_convo.s`'s `Level.GetEntity("volstok").SetModel(23)` is
// `male_short_tunic.bin`; and `twilite/volstok_violet.s`'s `SetModel(69)`
// is `zombie.bin` -- Volstok turns into a zombie mid-conversation. The
// twenty-odd commented-out `//SetModel(202)` lines in the monster scripts
// are a copy-paste header the studio never enabled.
//
// ---- `RunScript` reloads the script and re-runs `Init` (case 0x04) ----
//
// The case reads the name, builds a path the same way every other script
// load does, calls `vtable[0xa4]` (load a script file into this object) and
// then `vtable[0xa8]` with the literal at `0x100b153c` -- which is the
// UTF-16 string **"Init"**. So it is a genuine hot-swap: the object keeps
// its identity and every field the old `Init()` already set, and gains the
// new script's methods.
//
// All seven shipped sites are the same shape -- an upgraded spell variant
// that sets what differs and then defers to the base spell:
//
//     spells/u_blaze_lvl10.s:
//         SetLevel(10); SetSpellType(50); SetCost(756);
//         SetMarketValue(265); RunScript("Blaze");
//
// -- and `blaze.s`'s own `Init()` then supplies the name, icon, use text,
// rating and `HitTarget` behaviour the variant never mentions. (It also
// re-sets the market value, so the variant's 265 does not survive. That is
// the shipped game's behaviour, not a port artefact.)
//
// **The swap is deferred by one call.** `skTreeNodeObject::setNode()`
// deletes both the old tree and the method cache, and `RunScript` is called
// from inside the very `Init()` that lives in that tree -- doing it on the
// spot would free the parse tree the interpreter is walking. The engine
// gets away with it because its Simkin is modified; here the request is
// recorded and `RunEntityInit()` below applies it after `Init()` returns,
// which is also the only moment any shipped script needs.

#include <string>

#include "skRValue.h"
#include "skRValueArray.h"
#include "skString.h"

class skExecutableContext;
class skTreeNodeObject;

namespace sk {
class AudioEngine;
class SoundArchive;
}  // namespace sk

namespace sk_bindings {

class EntityBaseRef {
public:
    virtual ~EntityBaseRef() = default;

    // ---- position (M62) ------------------------------------------------

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

    // M100: how far above the surface an actor's pending move lands, and
    // the one other move that asks for a different height. SetPosition's
    // snap adds 0x80; FindPathNode's (`vtable[0x14](x, y, 0)`, the same
    // FUN_100686e0 snap, then `z += 300`) adds 300. Read with the pending
    // position and reset to 0x80 when taken.
    void RequestSurfaceMove(int x, int y, int lift) {
        m_X = x;
        m_Y = y;
        m_Z = 0;
        m_PositionDirty = true;
        m_PendingSurfaceLift = lift;
    }
    int TakePendingSurfaceLift() {
        const int lift = m_PendingSurfaceLift;
        m_PendingSurfaceLift = kSetPositionSurfaceLift;
        return lift;
    }
    static constexpr int kSetPositionSurfaceLift = 0x80;

    // `vtable[0xc8]`: is this entity a member of the engine's actor list,
    // and therefore floor-snapped on a teleport? See the header comment.
    virtual bool isActorForPositioning() const { return false; }

    // ---- identity (M92) ------------------------------------------------

    // Seeded by the host from the `.ent` placement name, before Init() --
    // see the `GetID` section of the header comment for the four-placement
    // script that proves that is what it is.
    void SetEntityId(std::string id) { m_Id = std::move(id); }
    const std::string& entityId() const { return m_Id; }

    // `SetName`'s `stringtable.*` id, or -1 if the script never set one.
    int entityNameId() const { return m_NameId; }

    // ---- visibility (M92) ----------------------------------------------

    // `ShowEntity(false)`: not drawn, not targetable, not usable. Starts
    // false -- a placement is visible until a script says otherwise.
    bool entityHidden() const { return m_Hidden; }

    // ---- passability (M92) ---------------------------------------------

    // `entity+0xd5`. False (solid) until a script says otherwise, which is
    // what every closed door and every standing creature relies on.
    bool entityPassable() const { return m_Passable; }

    // ---- rotation (M92) ------------------------------------------------

    // `entity+0xb6`, raw 16-bit turn units. `SetRotationTurn` assigns it
    // and `AddRotationTurn` accumulates into it -- a door's swing and a
    // creature's facing are the same field.
    int rotationRaw() const { return m_RotationRaw; }

    // Drained by the host for an entity whose facing is otherwise its own
    // business -- a creature's, which the AI tick writes every frame and
    // would otherwise overwrite the script's answer immediately. A door
    // reads `rotationRaw()` directly instead (its heading is nothing but
    // the accumulated turn) and never drains this, which is harmless: the
    // flag simply stays raised.
    bool TakePendingRotation(int& raw) {
        if (!m_RotationDirty) return false;
        m_RotationDirty = false;
        raw = m_RotationRaw;
        return true;
    }

    // ---- model (M92) ---------------------------------------------------

    // The `models.txt` row `SetModel` asked for, or -1 if it was never
    // called and the placement's own entities.txt model stands.
    int modelOverride() const { return m_ModelOverride; }

    // ---- MirrorMethod (M92) --------------------------------------------

    // How many times a script asked for a method to be mirrored onto the
    // other handset. Always a no-op here (single player); counted so a test
    // can distinguish "ran and correctly did nothing" from "never ran".
    int mirroredMethodCount() const { return m_MirroredMethods; }

    // ---- RunScript (M92) -----------------------------------------------

    // The SimKin path a script asked to switch to, if any. Drained by
    // RunEntityInit() below rather than by a caller.
    bool TakePendingScript(std::string& simkinPath) {
        if (m_PendingScript.empty()) return false;
        simkinPath = std::move(m_PendingScript);
        m_PendingScript.clear();
        return true;
    }

    // The skTreeNodeObject half of this same object, for the script swap.
    // Null for an entity that is not script-backed (the player), in which
    // case RunScript() is accepted and recorded but nothing can apply it.
    skTreeNodeObject* entityScriptNode() const { return m_ScriptNode; }

protected:
    // Returns false if `methodName` is none of the base's, so each class can
    // carry on to its own methods and then soft-fail -- the same "try, then
    // fall through" shape RowOwnerRef::HandleSharedWidgetNative() uses.
    bool HandleEntityBaseNative(const skString& methodName, skRValueArray& args,
                                skRValue& returnValue);

    // Called by every class's constructor that has a script file behind it,
    // so RunScript() can swap that file out. See entityScriptNode().
    void AttachEntityScript(skTreeNodeObject* node) { m_ScriptNode = node; }

    // A door is the only entity whose passability is also stamped into the
    // tile grid; it overrides this to restamp. Everything else answers
    // `entityPassable()` per frame and needs no hook.
    virtual void OnPassableChanged() {}

    // `PlaySound` needs the zone's sound manifest and the mixer. Every
    // entity class can reach both; the base cannot, so it asks.
    virtual sk::SoundArchive* entitySounds() const { return nullptr; }
    virtual sk::AudioEngine* entityAudio() const { return nullptr; }

private:
    int m_X = 0;
    int m_Y = 0;
    int m_Z = 0;
    bool m_PositionDirty = false;
    int m_PendingSurfaceLift = kSetPositionSurfaceLift;  // M100

    std::string m_Id;
    int m_NameId = -1;
    bool m_Hidden = false;
    bool m_Passable = false;
    int m_RotationRaw = 0;
    bool m_RotationDirty = false;
    int m_ModelOverride = -1;
    int m_MirroredMethods = 0;
    std::string m_PendingScript;
    skTreeNodeObject* m_ScriptNode = nullptr;
};

// Runs a freshly built entity's `Init()`, then honours any `RunScript()` it
// asked for -- reload the named script into this same object and run the new
// `Init()` -- until the script stops asking. Every site that builds an entity
// calls this instead of `method("Init", ...)` directly, so a spell bought in
// a store, restored from a save, dropped in the world or handed over by a
// debug command all get the same object the engine would have built.
//
// `scriptRoot` is MenuStack::scriptRoot(); the chain is capped (see the .cpp)
// because a script could name itself.
void RunEntityInit(EntityBaseRef& entity, const std::string& scriptRoot,
                   skExecutableContext& ctxt);

}  // namespace sk_bindings
