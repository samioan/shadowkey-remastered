#pragma once

#include <cstdint>
#include <map>
#include <string>

// M15: native binding for a real placed door's .s script (door.s/
// door02.s -- see docs/PORT_ROADMAP.md's "generic Action::Use interact
// binding" open item). Same shape as MonsterExecutable (a real script's
// Init() actually runs, backed by a real TreeNode, answering whatever
// subset of the real "Object/Entity (world base)" native class
// (shadowkey/simkin_native_bindings.json trie 0x14d08, docs/
// SIMKIN_NATIVE_API.md) a door script's Init()/OnUse() actually calls --
// this port doesn't reproduce the real 57-method class in full, same
// precedent ItemExecutable/MonsterExecutable already established for
// their own native classes.
//
// Real door.s/door02.s (read directly from the install image, both
// identical apart from door.s's extra PlaySound/bPlaySound bookkeeping):
//   Init(s) { SetUseText(941); SetMPUsable(true); }
//   OnUse(s) {
//     if (saved_Open==0) { SetPassable(true); AddRotationTurn(-64*256);
//                          saved_Open=1; DoorOpened(self,1,0);
//                          SetUseText(2989); }
//     else { SetPassable(true); AddRotationTurn(64*256); SetPassable(false);
//            saved_Open=0; DoorOpened(self,0,0); SetUseText(941); }
//   }
// GetPlayer().PlaySound(...) (door.s only) soft-fails cleanly through
// PlayerExecutable's NativeStubExecutable base -- this port has no audio
// (consistent with every prior milestone), nothing lost by leaving it
// unimplemented.
//
// SetPassable(bool) -- M15 stored it and left it inert, M55 gave it teeth
// in main.cpp's per-entity collision walk, and **M67 found the half that
// was actually keeping doors shut**. A door is 64x256 in
// `<zone>_models.txt`, i.e. wider than the half-tile the engine tests
// per-entity, so it is a *tile-stamped* entity: its footprint is baked
// into the tile grid and blocks like a wall (world/model_collision.h).
// The real native (Object/Entity dispatch case 0x17) is
//
//     entity->passable /* +0xd5 */ = arg;
//     if (entity->vtable[0xac]())            // is it tile-stamped?
//         arg ? FUN_1006640c(entity, 4, 1)   // opened -> clear the stamp
//             : FUN_10066204(entity, 4, 1);  // closed -> set it
//
// -- and this port did the assignment and none of the stamping, so every
// door in the game stayed impassable after it opened. See
// Zone::StampEntityBox and the TileStamp interface below.
//
// The stamp has to happen **inside** the call rather than being
// reconciled afterwards, because the footprint is walked at the entity's
// heading *at that moment* and `door.s` deliberately changes passability
// on both sides of its turn:
//
//     open:  SetPassable(true);  AddRotationTurn(-64*256)
//     close: SetPassable(true);  AddRotationTurn(64*256); SetPassable(false)
//
// Reading it after the fact would clear the closed footprint at the open
// heading and vice versa.
//
// AddRotationTurn(raw): raw units share the same 16-bit-wraparound fixed-
// point convention already confirmed for .ent's rotOrScale fields
// (docs/ZONE_FORMAT.md: 65536 raw units == one full turn) -- door.s's
// own -64*256 == -16384 == a quarter turn, a physically sensible "door
// swings open 90 degrees." yawRadians() converts the accumulated raw
// value for render3d/zone_renderer.cpp's new per-entity yaw parameter.

#include <string>

#include "simkin_bindings/entity_position_ref.h"
#include "skScriptedExecutable.h"

class skInterpreter;

namespace sk_bindings {

class PlayerExecutable;

class DoorExecutable : public skScriptedExecutable, public EntityPositionRef {
public:
    // M67: the tile grid, reached through an interface for the same
    // layering reason LevelExecutable::ZoneRegions exists -- `sk_bindings`
    // deliberately does not link `sk_world`. main.cpp implements it over
    // the live zone; a test can implement it over a counter.
    class TileStamp {
    public:
        virtual ~TileStamp() = default;
        // `Zone::StampEntityBox` -- FUN_10066204 / FUN_1006640c.
        virtual void StampEntityBox(int worldX, int worldY, int headingRaw, int halfExtentX,
                                     int halfExtentY, uint8_t mask, bool set) = 0;
    };

    // The half-extents come from `<zone>_models.txt` via
    // `sk::ModelCollision`, and `placementYawRaw` is the `.ent` record's
    // own heading -- together with the script's accumulated
    // AddRotationTurn() they make up `Entity+0xb6`, the single heading the
    // real stamp walk rotates by. Attach before Init() runs, so a script
    // whose Init() sets passability stamps against real geometry.
    //
    // Before this is attached (and in every test that does not care), the
    // native still records passability and simply stamps nothing -- the
    // same late-bound-dependency convention SetEntityTypes()/
    // SetZoneRegions() already use.
    void AttachTileStamp(TileStamp* stamp, int halfExtentX, int halfExtentY, int placementYawRaw);

    // `Entity+0x92`, via ModelCollision::tileStamped(): only an entity
    // wider than half a tile on either axis is baked into the grid at all.
    bool isTileStamped() const;

    // `Entity+0xb6` -- placement heading plus every AddRotationTurn() so
    // far, wrapped to the engine's 16 bits.
    int headingRaw() const;

    DoorExecutable(const skString& filename, skExecutableContext& ctxt, PlayerExecutable& player);

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

    // M38: keep an object assigned to a *pre-declared* script field --
    // see native_binding_common.h's StoreScriptObjectField() for the
    // vendored-Simkin behaviour this works around and the real script
    // (lothcav.s) that exposed it.
    bool setValue(const skString& fieldName, const skString& attribute,
                  const skRValue& value) override;
    bool getValue(const skString& fieldName, const skString& attribute,
                  skRValue& value) override;


    // M21: guaranteed-non-blank strValue() -- see ItemExecutable::
    // strValue()'s comment for the real bug this avoids (a found object's
    // default-empty strValue() could otherwise compare equal to the
    // blank `null` global via skRValue::operator=='s T_Object-vs-T_String
    // branch). azra.s's own `M1 = Level.GetEntity("m1"); if (M1 != null)`
    // pattern (a door/switch, category 11) is the real call shape this
    // protects, even though that script isn't loaded/run by this port
    // yet (docs/PORT_ROADMAP.md's M18 "Not attempted" note) -- fixed
    // proactively rather than waiting to hit it.
    skString strValue() const override { return skString("Door"); }

    // Real per-instance state, host-side accessors (same split
    // MonsterExecutable's stat accessors use) -- main.cpp's render loop
    // and interact-prompt text read these without going through the
    // interpreter.
    int useTextId() const { return m_UseTextId; }
    float yawRadians() const;
    bool passable() const { return m_Passable; }

    // Runs the real script's OnUse() handler -- same
    // skParseException/skRuntimeException-catching convention
    // MonsterExecutable::InvokeOnKilled() already uses for a host-
    // triggered call outside any live script call frame.
    void InvokeOnUse();

private:
    // M38: object-valued script fields -- see setValue() above.
    std::map<std::string, skRValue> m_ObjectFields;
    PlayerExecutable& m_Player;
    skInterpreter* m_Interpreter;

    // M67: applies the current passability to the tile grid, at the
    // heading the door is at right now. See SetPassable's comment above.
    void ApplyTileStamp();

    int m_UseTextId = -1;
    int m_RotationRaw = 0;  // accumulated AddRotationTurn() argument, raw units
    bool m_Passable = false;
    bool m_MpUsable = false;  // SetMPUsable() -- stored but inert, no multiplayer in this port

    // M67: the tile stamp -- see AttachTileStamp().
    TileStamp* m_TileStamp = nullptr;
    int m_HalfExtentX = 0;
    int m_HalfExtentY = 0;
    int m_PlacementYawRaw = 0;
};

}  // namespace sk_bindings
