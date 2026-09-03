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
// scoping): only `GetEntity`/`GetPlayer`/`PlayAmbient`/`CreateEntity`
// (M21) get real handlers, picked because they're what this port's own
// already-loaded real scripts actually call (monsters/azra_rat.s's
// `Level.GetEntity("trthgar")`, azra.s's `Level.GetEntity("m1".."m7")`
// and `Level.PlayAmbient(73,100)`, real loot-bag scripts' `Level.
// CreateEntity(typeId)` -- see M21's writeup below). Everything else
// (AddTrigger, LoadLevel, ...) soft-fails, same convention as every other
// binding class -- a full Zone/Level pass (AddTrigger's own "Door/trap
// trigger" return-type class alone is a further ~10-method surface) is
// future work, not this milestone.
//
// M21: `CreateEntity(typeId)` -- confirmed real by decoding real loot-bag
// scripts (docs/PORT_ROADMAP.md's M21 entry has the full writeup):
// monsters/azra_rat.s's `SetLoot(300, "Loot_ratseye", 1, 8)` tag string
// is, lowercased, a real loadable script path (`loot_ratseye.s`, this
// port's filesystem being case-insensitive, same convention every other
// path lookup in this codebase already relies on) -- and that script's
// own `Init()` does `Item = Level.CreateEntity(704); AddObject(Item);`,
// where 704 is exactly `items/ratseye.s`'s real entities.txt typeId.
// Resolves typeId -> entities.txt (same EntityTypeTable chain `world/
// entity_types.h` already uses for placed entities) -> a real
// ItemExecutable, Init() actually run. Only item-shaped categories (3/4/
// 5/6/9 -- misc loot/weapon/spell/armor/consumable, docs/ZONE_FORMAT.md's
// category table) resolve; every real loot-bag script in the corpus only
// ever creates one of these. Needs `SetEntityTypes()` called once the
// host has loaded `entities.txt` (main.cpp) -- soft-fails to "not found"
// before then, same "optional asset not loaded yet" convention every
// other late-bound dependency in this port already uses.
//
// GetEntity(name)'s registry (RegisterEntity/ClearEntities) is populated
// by the host (main.cpp's zone-load block) from `Zone::EntPlacement::name`
// -- the .ent record's own real per-instance name field (confirmed against
// real azra.ent data: record 20, typeId 141, name "trthgar" -- exactly
// the identifier monsters/azra_rat.s's OnKilled() looks up on its 8th-kill
// branch). Not every live door/monster has one (most placements leave it
// blank); only the named subset registers.
//
// Real scripts test the result with `if (X != null)` (e.g. azra.s,
// lootmenu.s) -- SimKin's grammar has no `null` literal (confirmed:
// grepped the vendored parser), so this is an ordinary global identifier
// a script expects to already exist. Registered by game_constants.cpp's
// RegisterGameConstants() as a blank default skRValue(); GetEntity()
// returns that exact same blank default on a miss, so an unfound lookup
// compares equal to it (skRValue::operator=='s T_String branch, matching
// blank strings) while a found live object (T_Object) correctly never
// does -- as long as that found object's own strValue() is non-blank
// (see game_constants.cpp's `null` comment, and ItemExecutable/
// DoorExecutable/MonsterExecutable's own strValue() overrides, M21, for
// a real bug this exact reasoning missed the first time).

#include <map>
#include <memory>
#include <string>

#include "simkin_bindings/native_stub_executable.h"

namespace sk {
class EntityTypeTable;
}

namespace sk_bindings {

class ItemExecutable;
class MenuStack;

class LevelExecutable : public NativeStubExecutable {
public:
    // M21: takes `MenuStack&` (the object that owns this LevelExecutable
    // in the first place) instead of a bare `PlayerExecutable&` -- needed
    // now that `CreateEntity()` has to load a real script (scriptRoot(),
    // interpreter(), strings(), all already on MenuStack) in addition to
    // resolving `GetPlayer()` (player()). Constructed from within
    // MenuStack's own constructor, referencing the MenuStack still under
    // construction -- safe, since nothing here calls back into `stack`
    // until well after that constructor returns (matches MenuExecutable's
    // own existing MenuStack&-holding shape, just constructed one call
    // frame earlier).
    // Declared here, defined in the .cpp where ItemExecutable (held via
    // m_PendingCreatedEntity, see below) is a complete type -- an inline
    // constructor here would still implicitly need it too (exception-
    // unwind cleanup of already-constructed members), same reasoning as
    // the destructor -- same forward-declared-member shape MenuStack's
    // own `~MenuStack()` already uses.
    explicit LevelExecutable(MenuStack& stack);
    ~LevelExecutable();

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

    // M21: must be called once entities.txt is loaded (main.cpp) before
    // CreateEntity() can resolve anything -- see this class's comment.
    void SetEntityTypes(const sk::EntityTypeTable* entityTypes) { m_EntityTypes = entityTypes; }

    // M21: CreateEntity()'s handler allocates the real ItemExecutable and
    // holds it here (a one-slot pending holder, same shape
    // PlayerExecutable::TakePendingPickupItem() already established for a
    // structurally identical reason -- the script that just called
    // CreateEntity() still needs a live, valid reference to work with
    // before real ownership can transfer anywhere) until the calling
    // script's own AddObject(Item) hands it off for real (ItemExecutable::
    // method()'s AddObject handler). Clears on read; a second call
    // (nothing calls CreateEntity() twice without an AddObject() between,
    // in the real corpus) returns nullptr rather than a stale object.
    std::unique_ptr<ItemExecutable> TakePendingCreatedEntity();

private:
    MenuStack& m_Stack;
    std::map<std::string, skiExecutable*> m_Entities;
    const sk::EntityTypeTable* m_EntityTypes = nullptr;
    std::unique_ptr<ItemExecutable> m_PendingCreatedEntity;
};

}  // namespace sk_bindings
