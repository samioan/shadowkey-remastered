#pragma once

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
// SetPassable(bool): stored but currently inert -- this port has no
// per-entity collision at all yet (only Zone::CircleHitsWall's tile-grid
// test, see world/zone.h), so a closed door was never actually blocking
// movement to begin with. Not a regression this pass introduces, just an
// existing gap this class doesn't attempt to close.
//
// AddRotationTurn(raw): raw units share the same 16-bit-wraparound fixed-
// point convention already confirmed for .ent's rotOrScale fields
// (docs/ZONE_FORMAT.md: 65536 raw units == one full turn) -- door.s's
// own -64*256 == -16384 == a quarter turn, a physically sensible "door
// swings open 90 degrees." yawRadians() converts the accumulated raw
// value for render3d/zone_renderer.cpp's new per-entity yaw parameter.

#include <string>

#include "skScriptedExecutable.h"

class skInterpreter;

namespace sk_bindings {

class PlayerExecutable;

class DoorExecutable : public skScriptedExecutable {
public:
    DoorExecutable(const skString& filename, skExecutableContext& ctxt, PlayerExecutable& player);

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

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
    PlayerExecutable& m_Player;
    skInterpreter* m_Interpreter;

    int m_UseTextId = -1;
    int m_RotationRaw = 0;  // accumulated AddRotationTurn() argument, raw units
    bool m_Passable = false;
    bool m_MpUsable = false;  // SetMPUsable() -- stored but inert, no multiplayer in this port
};

}  // namespace sk_bindings
