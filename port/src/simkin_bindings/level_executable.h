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
// (M21)/`LoadLevel` (M26) get real handlers, picked because they're what
// this port's own already-loaded real scripts actually call (monsters/
// azra_rat.s's `Level.GetEntity("trthgar")`, azra.s's `Level.
// GetEntity("m1".."m7")` and `Level.PlayAmbient(73,100)`, real loot-bag
// scripts' `Level.CreateEntity(typeId)` -- see M21's writeup below;
// cheatmenu.s's real `Level.LoadLevel("azra")` -- see M26's writeup).
// Everything else (AddTrigger, ...) soft-fails, same convention as every
// other binding class -- a full Zone/Level pass (AddTrigger's own
// "Door/trap trigger" return-type class alone is a further ~10-method
// surface) is future work, not this milestone.
//
// M26: `LoadLevel(name)` -- MenuStack::RequestZoneChange() (own comment
// has the full writeup), consumed by main.cpp's real loading-screen +
// zone-load block. Fires and returns immediately; the actual zone swap
// happens on a later tick once the loading screen's fixed run of frames
// finishes, same "the host polls a request flag" shape RequestGameStart()
// already established for the very first zone.
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
class MonsterExecutable;  // M44: CreateEntity's creature form

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

    // ---- M44: the named-region natives ----
    //
    // `LockZone(name)`/`UnlockZone(name)` (dispatcher 0x1006dbec cases
    // 0x29/0x28) make a named `.zon` region impassable or passable by
    // writing the second byte of every cell it covers -- see
    // world/zone.h's Region and ZmpCell::blockFlags. 30-odd real call
    // sites across the corpus, all of the shape `UnlockZone("swdoor")`
    // after a key is used or a lever pulled.
    //
    // Reached through this interface rather than through `sk::Zone`
    // directly, because `sk_bindings` deliberately does not link
    // `sk_world` -- the same layering that made `sk_entity_types` its own
    // library (see CMakeLists.txt). main.cpp implements it over the live
    // zone; a test can implement it over anything.
    //
    // Set by main.cpp's zone-load block; before it is set, all three
    // natives accept the call and do nothing, the same
    // late-bound-dependency convention SetEntityTypes() uses.
    class ZoneRegions {
    public:
        virtual ~ZoneRegions() = default;
        virtual bool HasRegion(const std::string& name) const = 0;
        // Both return the number of tiles changed.
        virtual int LockRegion(const std::string& name) = 0;
        virtual int UnlockRegion(const std::string& name) = 0;
        // M44: `LightRect(x0, y0, x1, y1, level)` -- zone-effects
        // dispatcher (FUN_1002f074) case 1. Writes the *baked* light level
        // of every cell in a half-open tile rectangle, as `level << 8`
        // (the same 8.8 scale Zone::PaletteColor's `lightLevel >> 8` rung
        // lookup already reads). All 8 real call sites pass 64, which the
        // rung clamp turns into "fully lit".
        virtual void LightRect(int x0, int y0, int x1, int y1, int level) = 0;
    };
    void SetZoneRegions(ZoneRegions* regions) { m_ZoneRegions = regions; }

    // ---- M44: `Vignette(n)` ----
    //
    // Zone-effects dispatcher case 2, whose whole implementation
    // (FUN_1002bc6c + its per-frame tick FUN_1002bdf4) is a **full-screen
    // slideshow**: it stops the player dead, preloads a contiguous run of
    // sprite slots, arms a fade to screen mode 0x20, and then advances one
    // mode per 0x700 time units, drawing sprite `first + (mode - 0x20)`
    // with caption string `text + (mode - 0x20)` under it. Any key skips
    // to the end. Six of them ship, one per zone that calls it, and each
    // is a story beat at a region boundary (azra's "Done", crypt1's
    // "vig", ...).
    //
    // The table is the switch in FUN_1002bc6c, verbatim.
    struct VignetteDefinition {
        int firstSprite = 0;
        int lastSprite = 0;   // inclusive
        int firstTextId = 0;
        int slides() const { return lastSprite - firstSprite + 1; }
    };
    static bool VignetteById(int id, VignetteDefinition& out);

    // Set by the Vignette handler, drained by main.cpp, which owns the
    // screen. Cleared on read.
    bool TakePendingVignette(VignetteDefinition& out) {
        if (!m_PendingVignettePending) return false;
        m_PendingVignettePending = false;
        out = m_PendingVignette;
        return true;
    }

    // M44: `CreateEntity(typeId, x, y, z)` -- the four-argument form.
    // crypt1.s's EnterZone handler spawns Umbra Keth with it, at typeId
    // 274 (a *monster*, not an item), so this is the non-item-shaped
    // category the roadmap's second sub-bullet asks about. The position
    // is handed back to the host through here rather than acted on
    // directly: creating a live creature needs a MonsterExecutable plus a
    // world instance, both of which live in main.cpp.
    struct PendingCreature {
        int typeId = 0;
        int x = 0, y = 0, z = 0;
        // Filled in by the host once it has built the real instance, so
        // that the script's own follow-up calls (crypt1.s immediately does
        // `UmbraKeth.SetCanTeleport(true)`) reach the right object.
        skiExecutable* object = nullptr;
    };
    // Hands the host both the request and the live script object it
    // already built, transferring ownership. Returns false when nothing is
    // waiting. Declared here, defined in the .cpp where MonsterExecutable
    // is complete.
    bool TakePendingCreature(PendingCreature& out, std::unique_ptr<MonsterExecutable>& script);

    // M45: resolve a typeId to a creature script and run its Init(), the
    // shared half of `FUN_100715a8` -- used by the four-argument
    // CreateEntity above and by the encounter spawner, which creates its
    // creatures the same way. Returns null for a typeId that is not a
    // real category-2 script. Does **not** place anything in the world;
    // that is the host's job either way.
    std::unique_ptr<MonsterExecutable> CreateCreature(int typeId);

private:
    MenuStack& m_Stack;
    std::map<std::string, skiExecutable*> m_Entities;
    const sk::EntityTypeTable* m_EntityTypes = nullptr;
    std::unique_ptr<ItemExecutable> m_PendingCreatedEntity;
    ZoneRegions* m_ZoneRegions = nullptr;  // M44: see SetZoneRegions()
    VignetteDefinition m_PendingVignette;
    bool m_PendingVignettePending = false;
    PendingCreature m_PendingCreature;
    std::unique_ptr<MonsterExecutable> m_PendingCreatureScript;
    bool m_PendingCreaturePending = false;
};

}  // namespace sk_bindings
