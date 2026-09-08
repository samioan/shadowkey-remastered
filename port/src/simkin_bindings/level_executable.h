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
#include <vector>

#include "simkin_bindings/effect_entity.h"
#include "simkin_bindings/native_stub_executable.h"
#include "skRValue.h"

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
    // M74: an entity's `entities.txt` category, which is what decides its
    // C++ class and therefore its item type (game_constants.h). -1 when the
    // table is not loaded or the typeId is not in it. Used by the save
    // loader, which rebuilds inventory children from a stored script path
    // and so does not go through CreateItem().
    int EntityCategoryOf(int typeId) const;

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

    // M48: the item half of the same thing, factored out of the
    // one-argument CreateEntity handler so the real cast's conjure branch
    // (`spells\DaedricWeapon.s` -> entity 4037) builds its sword through
    // exactly the path a script would. `requireItemCategory` is the
    // handler's own category filter; the conjure bypasses it, because
    // 4037's entities.txt category is 16 rather than a weapon's usual 4
    // and the real FUN_10044e08 does no category check at all.
    std::unique_ptr<ItemExecutable> CreateItem(int typeId, bool requireItemCategory = true);

    // ---- M63: `CreateEffect(...)` ----
    //
    // The animated sprites a zone script places in its own Init()
    // (simkin_bindings/effect_entity.h has the whole writeup). They live
    // here rather than in main.cpp because nothing outside this port's
    // renderer needs them and the real ones are owned by the engine's own
    // entity list, which this class already stands in for; main.cpp ticks
    // and (eventually) draws them, and clears them on a zone change the
    // same way it clears the GetEntity() registry.
    const std::vector<EffectEntity>& effects() const { return m_Effects; }
    std::vector<EffectEntity>& effects() { return m_Effects; }
    void ClearEffects() { m_Effects.clear(); }
    // Runs every live effect's tick and drops the ones that expired. A
    // scripted effect is immortal, so in practice nothing is dropped --
    // the pruning is here for the engine-spawned forms the same struct
    // covers.
    void TickEffects(int frameDelta, int gravityUnits = 0);

    // ---- M75: `Level` is the zone-root script object ----
    //
    // This class was built (M18) on the assumption that `Level` is a
    // native-only singleton like `GetPlayer()`. It is half of one. The
    // corpus settles the other half, twice over:
    //
    //   * `crypt2/pedestal_entity.s` calls `Level.AddCrystal()` seven
    //     times, and `AddCrystal[()...]` is defined **in `crypt2.s`** --
    //     the zone-root script -- and nowhere else. It is not in any
    //     native binding trie.
    //   * Scripts read and write 168 distinct `Level.<field>` names across
    //     423 sites, and **163 of them are declared at the top level of
    //     the zone-root script of exactly the zone they are used in** --
    //     `EndGame_Trinket`/`EndGame_Skelos` in `azra.s`,
    //     `saved_Birgitta`/`saved_taker`/`saved_Given` in `delfhide.s`,
    //     `saved_Crys1`..`saved_Crys7` in `crypt2.s`, and so on for every
    //     zone. Inside `crypt2.s`'s own `AddCrystal` those same variables
    //     are read bare (`saved_Tele1 = 1`) alongside `Level.GetEntity(...)`
    //     and bare `GetEntity(...)`, used interchangeably.
    //
    // So `Level` and the zone-root script are one object: a TreeNode-backed
    // script executable whose `method()` also answers the Zone/Level
    // (0x14d38) and zone-effects (0x14df8) natives -- exactly the shape
    // MonsterExecutable/DoorExecutable already have. This port keeps them
    // as two C++ objects (the split is load-bearing elsewhere: `Level`
    // outlives any one zone and is reachable from menus with no zone
    // loaded at all) and bridges them here: field access and unresolved
    // method names forward to the attached zone script.
    //
    // Without this, `Level.<anything>` raised "Cannot get field", and
    // because that is a *runtime error* rather than a soft-fail it aborted
    // the whole handler -- which for a conversation menu means its `Init()`
    // never finishes and the menu never opens at all. Six real
    // conversations died that way on the first line of their `Init()`:
    // Old Trinket, Azra Skelos, delfhide's Chef and RescueConvo, crypt1's
    // final Azra conversation, and `Talker`.
    //
    // Set by main.cpp's zone-load block; cleared before the zone script is
    // destroyed. Same late-bound-dependency convention as SetEntityTypes()
    // and SetZoneRegions().
    void AttachZoneScript(class ZoneScriptExecutable* script);
    class ZoneScriptExecutable* zoneScript() const { return m_ZoneScript; }

    bool setValue(const skString& fieldName, const skString& attribute,
                  const skRValue& value) override;
    bool getValue(const skString& fieldName, const skString& attribute,
                  skRValue& value) override;

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
    std::vector<EffectEntity> m_Effects;  // M63

    // M75: see AttachZoneScript(). Non-owning -- main.cpp owns the zone
    // script and clears this before destroying it.
    class ZoneScriptExecutable* m_ZoneScript = nullptr;
    // Field storage for the case the engine does not have: a script
    // touching `Level.<field>` with no zone loaded (every menu screen
    // reachable from the main menu, and every smoke test that does not
    // build a zone). Reads default to 0 rather than erroring, the same
    // convention -- and for the same reason -- as
    // PlayerExecutable::getValue()'s own bucket.
    std::map<std::string, skRValue> m_Fields;
};

}  // namespace sk_bindings
