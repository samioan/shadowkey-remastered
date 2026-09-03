#pragma once

// M18: the bare global `Level` identifier every real script can reach
// (docs/SIMKIN_NATIVE_API.md's "Zone/Level" (0x14d38) + "Zone effects"
// (0x14df8), confirmed to be two chained facets of one object, spliced
// into every script's default reachable-class set -- never itself
// obtained via a factory call). A native-only singleton, same
// NativeStubExecutable-derived shape PlayerExecutable already
// established -- no `.s` file backs "Level" itself.
//
// Narrow first slice (41+8 real member names between the two classes,
// 18/41 attested in the corpus for Zone/Level alone -- comparable in
// spirit to M15/M16's "one real category, not the whole native surface"
// scoping): only `GetEntity`/`GetPlayer`/`PlayAmbient` get real handlers,
// picked because they're what this port's own already-loaded real
// scripts actually call (monsters/azra_rat.s's `Level.GetEntity(
// "trthgar")`, azra.s's `Level.GetEntity("m1".."m7")` and
// `Level.PlayAmbient(73,100)`). Everything else (AddTrigger, CreateEntity,
// LoadLevel, ...) soft-fails, same convention as every other binding
// class -- a full Zone/Level pass (AddTrigger's own "Door/trap trigger"
// return-type class alone is a further ~10-method surface) is future work,
// not this milestone.
//
// GetEntity(name)'s registry (RegisterEntity/ClearEntities) is populated
// by the host (main.cpp's zone-load block) from `Zone::EntPlacement::name`
// -- the .ent record's own real per-instance name field (confirmed against
// real azra.ent data: record 20, typeId 141, name "trthgar" -- exactly
// the identifier monsters/azra_rat.s's OnKilled() looks up on its 8th-kill
// branch). Not every live door/monster has one (most placements leave it
// blank); only the named subset registers.
//
// Real scripts test the result with `if (X != null)` (e.g. azra.s) --
// SimKin's grammar has no `null` literal (confirmed: grepped the vendored
// parser), so this is an ordinary global identifier a script expects to
// already exist. Registered by game_constants.cpp's
// RegisterGameConstants() as a blank default skRValue(); GetEntity()
// returns that exact same blank default on a miss, so an unfound lookup
// compares equal to it (skRValue::operator=='s T_String branch, matching
// blank strings) while a found live object (T_Object) never does
// (skRValue.cpp's T_Object/T_String cross-type case falls through to its
// initial `r=false`) -- read directly off the vendored interpreter
// source, not guessed.

#include <map>
#include <string>

#include "simkin_bindings/native_stub_executable.h"

namespace sk_bindings {

class PlayerExecutable;

class LevelExecutable : public NativeStubExecutable {
public:
    explicit LevelExecutable(PlayerExecutable& player)
        : NativeStubExecutable("Level"), m_Player(player) {}

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

    // Called by main.cpp's zone-load block for every live door/monster
    // whose real .ent `name` field is non-empty. `object` must outlive
    // every subsequent GetEntity() call that could return it -- callers
    // clear the registry (ClearEntities()) before a zone's live objects
    // are destroyed/rebuilt.
    void RegisterEntity(const std::string& name, skiExecutable* object) {
        if (!name.empty()) m_Entities[name] = object;
    }
    void ClearEntities() { m_Entities.clear(); }

private:
    PlayerExecutable& m_Player;
    std::map<std::string, skiExecutable*> m_Entities;
};

}  // namespace sk_bindings
