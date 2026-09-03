#pragma once

#include <map>
#include <string>

// M23: native binding for a real zone-root script (e.g. `azra.s`) --
// distinct from every per-entity script (Item/Door/Monster) this port
// already runs: it isn't placed in the world or tied to any model, and
// its `Init()` fires once at zone load, not per-instance. Same
// `skScriptedExecutable`-backed shape as those classes though (a real
// script's `Init()` actually runs, backed by a real TreeNode), answering
// whatever subset of native calls a zone-root script's `Init()`/
// `EnterZone()` actually use.
//
// Confirmed real and worth loading: `azra.s`'s own `Init()` references
// `Level.GetEntity("m1")`..`("m20")`, `("trinket")`, `("birg")`,
// `("skelos")`, `("azra")`, `("vil1")`..`("vil4")`, `("heather")`,
// `("tanyin")` -- every one of these is a real, named `.ent` placement in
// `azra.ent` (confirmed this session by dumping every placement's real
// 40-byte name field, docs/PORT_ROADMAP.md's M23 entry), not dead code
// referencing nonexistent objects.
//
// `EnterZone(s)` (trigger-volume-driven, e.g. `s == "Skelos_Dead"`/
// `"YouSure"`/zone-transition triggers like `"ghasts"`) is NOT invoked by
// this port -- no real trigger-volume data source has been traced yet
// (a separate, larger investigation: is it `.ent`-based, `.zcp`-cell-
// based, or something else entirely -- docs/PORT_ROADMAP.md's "Not
// attempted" note). Only `Init()` runs, once, right after a zone's
// doors/monsters/pickups are loaded and registered into the `Level`
// global (so its many `Level.GetEntity(...)` calls can actually resolve).
//
// M24: `AddTrigger(name)` -- confirmed real (docs/SIMKIN_NATIVE_API.md's
// "Door/trap trigger" class research) to cover two genuinely distinct
// real usage shapes sharing one factory: a physical, position-based trap
// (`spikeTrap.AddEntity(1013); spikeTrap.SetTrap(8,16,10);
// spikeTrap.RemainActive();`, e.g. `broken1.s`/`lothcav.s`) and a
// zone-scoped *kill-count* trigger (`zombieTrigger.SetEntityID(104);
// zombieTrigger.SetLimit(2); zombieTrigger.SetCallback(
// "GPZombiesKilled");`, e.g. `ghstpass.s`/`lothcav.s`).
//
// M38: both are implemented now, and the "real trigger-volume/position
// data this port doesn't have" that blocked the first one turns out not
// to exist. Decompiling the trigger's own fire routine (`FUN_10090818`)
// and the list walk in front of it (`FUN_1007307c`, which iterates the
// zone's trigger list at `level+0x420`) shows there is no volume: a
// trigger is notified about an **entity**, with a mode saying what
// happened to it, and matches that entity in one of two ways --
//
//   * against its own `AddEntity(...)` list, which holds either integer
//     `entities.txt` typeIds or entity *names* (`FUN_1009138c` compares
//     `entity+0xc8` for the int case and strcmps `entity+0xcb` for the
//     string case), or
//   * against the trigger's **own name**, compared to the entity's name.
//
// The second is what makes `crypt1.s`'s five `AddTrigger("door1")` ...
// `AddTrigger("door5")` work: no AddEntity, no SetDoor, just a trap
// whose name matches the real `.ent` placement it guards -- and
// `crypt1.stn` binds those exact five names (`door1`..`door5`) to a
// shared `resistDisarm[28]` slot, which is independent corroboration
// from a completely different file that these names identify real,
// individually-trapped doors.
//
// The three notification modes, from the three real call sites of
// `FUN_1007307c`: **0** an entity was killed (the kill-count variant),
// **1** a door was opened (`FUN_1002e6cc`, which notifies before running
// the door's own open), **2** a trap entity's own periodic proximity
// check (`FUN_1008ff14`, the trap entity's tick, which builds a +/-0x100
// box around itself and the player's own box from the player's
// SetRadius/SetRadius2 half-extents).
//
// So everything a physical trap needs is placement data this port
// already parses. What is *not* reproduced is the pair of saving throws
// in front of the damage -- see TriggerExecutable::FireResult.

#include <memory>
#include <string>
#include <vector>

#include "simkin_bindings/native_stub_executable.h"
#include "skScriptedExecutable.h"

namespace sk_bindings {

class MenuStack;

// M24: `AddTrigger(name)`'s real return value, kill-count-callback slice
// only -- see zone_script_executable.h's class comment. Owned by the
// `ZoneScriptExecutable` that created it (same lifetime as the zone
// itself); scripts only hold a non-owning reference, same convention
// every other factory-returned native handle in this port already uses
// (MenuItemHandle, ComboBoxExecutable, ...).
class TriggerExecutable : public NativeStubExecutable {
public:
    // M38: the real notification modes -- see the class comment above for
    // where each one is sent from.
    enum NotifyMode {
        kNotifyEntityKilled = 0,
        kNotifyDoorOpened = 1,
        kNotifyTrapProximity = 2,
    };

    // M38: `trigger+0x24`'s real flag bits, in the order the dispatcher
    // sets them. The constructor (FUN_100914a0) starts at exactly
    // `kFlagOneShot`, which is why RemainActive() *clears* a bit rather
    // than setting one.
    enum Flags {
        kFlagLimit = 0x01,     // SetLimit
        kFlagOneShot = 0x02,   // cleared by RemainActive()
        kFlagCallback = 0x04,  // SetCallback
        kFlagDoor = 0x08,      // SetDoor
        kFlagTrap = 0x10,      // SetTrap
        kFlagDoorArg = 0x20,   // SetDoor(door) -- with an argument
    };

    explicit TriggerExecutable(std::string name = std::string())
        : NativeStubExecutable("Trigger"), m_Name(std::move(name)) {}

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

    int entityId() const { return m_EntityId; }
    const std::string& name() const { return m_Name; }
    const std::string& callback() const { return m_Callback; }
    bool trap() const { return (m_Flags & kFlagTrap) != 0; }
    bool active() const { return m_Active; }

    // True if `typeId`/`name` is an entity this trigger cares about --
    // either it is in the AddEntity list, or its name is the trigger's own
    // name. Exposed so the host can pre-filter the zone's placements once
    // at load instead of testing every prop against every trigger on every
    // tick; the real engine has no such filter (it walks the whole trigger
    // list per notification), so this is an optimisation, not a rule.
    bool WatchesEntity(int typeId, const std::string& entityName) const;

    struct FireResult {
        // The trigger's SetCallback() handler should run now.
        bool runCallback = false;
        // Rolled trap damage to apply to the player, 0 for none. The real
        // roll is `rand(dmgMin, dmgMax)` with no mitigation of any kind --
        // it goes straight into the player's stats-block DoDamage.
        int damage = 0;
        // The trap fired at all (so a "you were hit by a trap" message is
        // due, when showDamageMessage() is set).
        bool trapFired = false;
    };

    // M38: FUN_10090818, transcribed.
    //
    // **Not reproduced**: the two saving throws the real code puts in
    // front of the damage, both keyed on SetTrap's third argument (10 in
    // lothcav.s's three traps, 25 in crypt1.s's five doors).
    // `FUN_1003e438` is a full avoid -- it reads the player's stance
    // (`+0xf38`), an 8-slot equipped-effect array (`+0xf8c`, scanned for
    // typeIds 618 and 4500), both weapon hands and a stat at `+0x3c4`, and
    // rolls against `rand(0, 0x100)`; `FUN_10044e58` is a second, cheaper
    // roll that blocks the damage without cancelling the trap. Neither has
    // an equivalent here (this port has no stance model and no equipped-
    // effect array), so a trap in this port always connects. That is a
    // missing *mitigation*, not a wrong formula -- and it is why the
    // damage numbers may feel heavier here than on the device.
    FireResult Fire(int mode, int typeId, const std::string& entityName);

    // The kill-count variant, kept as its own entry point because
    // main.cpp's combat loop already calls it and it needs no entity name.
    bool NotifyKilled();

    bool showDamageMessage() const { return m_ShowDamageMessage; }

private:
    std::string m_Name;
    int m_EntityId = -1;
    int m_Limit = 0;
    std::string m_Callback;
    int m_KillCount = 0;
    bool m_Fired = false;

    // M38: the physical-trap half.
    unsigned m_Flags = kFlagOneShot;  // the real constructor's own value
    bool m_Active = true;             // trigger+0x28, likewise
    int m_TrapDamageMin = 0;          // +0x34
    int m_TrapDamageMax = 0;          // +0x36
    int m_TrapSaveId = 0;             // +0x38
    bool m_ShowDamageMessage = false; // +0x6c
    std::vector<int> m_EntityTypeIds;
    std::vector<std::string> m_EntityNames;
};


// M38: `AddEncounters(regionName, ...)`'s real return value -- the
// "Encounter spawner" class (docs/SIMKIN_NATIVE_API.md's 0x14d98,
// dispatcher FUN_1008aa30), which until now was an InertHandleExecutable
// whose every method soft-failed.
//
// The object model is straight out of that dispatcher:
//
//   AddEncounters(a, b, c)   -- one *region* per argument (ghstpass.s's
//                               `AddEncounters("fight12","fight12W",
//                               "fight12S")`), kept as a list the other
//                               calls index into.
//   AddRandomSets(t1, c1, t2, c2, ...)
//                            -- reads its arguments in **pairs**
//                               (typeId, count) and appends the whole
//                               group as *one* set. Calling it five times
//                               (ghstpass.s does exactly that) gives five
//                               alternative sets, which is what makes them
//                               "random" sets.
//   SetLimit(index, count)   -- indexes the *region* list, not the set
//                               list, and writes a per-region cap.
//   SetActive(bool)          -- gates SpawnEncounter entirely.
//   SetRespawnSeconds(n)     -- a cooldown before the region can spawn
//                               again.
//   SpawnEncounter(region)   -- returns early when inactive; otherwise
//                               looks the named region up in the level's
//                               own named-object registry and spawns.
//
// **Not attempted**: where a region's position comes from. The one real
// caller (`lothna/pilgrim_remains.s`'s `Level.fight41.SpawnEncounter(
// "battle41")`) names a region that is *not* an `.ent` placement -- this
// session dumped every named placement in lothcav.ent/ghstpass.ent and
// none of "battle41"/"fight12"/"fight12W"/"fight13C"/... appears, so
// encounter regions live in a per-zone data source this port has not
// identified yet (the real code looks them up through FUN_100731b4
// against a list at `level+0x44c`, which is also what
// `EnterZone`'s own named triggers walk). Everything above the spawn
// *position* is real and stored; a spawn request is recorded and can be
// read back, but nothing places creatures in the world from it yet.
class EncounterExecutable : public NativeStubExecutable {
public:
    struct SpawnEntry {
        int typeId = 0;
        int count = 0;
    };

    EncounterExecutable() : NativeStubExecutable("Encounter") {}

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

    const std::vector<std::string>& regions() const { return m_Regions; }
    const std::vector<std::vector<SpawnEntry>>& sets() const { return m_Sets; }
    bool active() const { return m_Active; }
    int respawnSeconds() const { return m_RespawnSeconds; }
    int regionLimit(size_t index) const {
        return index < m_RegionLimits.size() ? m_RegionLimits[index] : 0;
    }
    // Called by the factory handler with AddEncounters()'s own arguments.
    void AddRegions(skRValueArray& args);

    // The last region SpawnEncounter() was asked for, empty if none --
    // the record a host would drain once encounter-region positions are
    // known. Cleared on read.
    std::string TakePendingSpawn() {
        std::string s;
        s.swap(m_PendingSpawn);
        return s;
    }

private:
    std::vector<std::string> m_Regions;
    std::vector<int> m_RegionLimits;
    std::vector<std::vector<SpawnEntry>> m_Sets;
    bool m_Active = true;
    int m_RespawnSeconds = 0;
    std::string m_PendingSpawn;
};

class ZoneScriptExecutable : public skScriptedExecutable {
public:
    ZoneScriptExecutable(const skString& filename, skExecutableContext& ctxt, MenuStack& stack);

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


    // M24: called by main.cpp for every real monster kill (any zone,
    // whether or not this zone-root script's own AddTrigger() calls set
    // up anything watching `typeId` -- a no-op if none did). Matches
    // against every registered trigger's real entityId() and, the one
    // time a match's running count reaches its real limit(), invokes its
    // real script callback -- same host-triggered-call convention
    // (placeholder "(s)" arg, catch+log skParseException/
    // skRuntimeException) every other Invoke*() in this codebase already
    // establishes.
    void NotifyKilled(int typeId);

    // M38: the other two real notification modes. `Notify` walks the same
    // trigger list the real FUN_1007307c does, fires each matching
    // trigger, applies whatever trap damage came back to the player, and
    // runs any callback that came due. Returns true if any trigger fired
    // (the real function's own return, which the trap entity's tick uses
    // to mark itself sprung).
    bool Notify(int mode, int typeId, const std::string& entityName,
                skiExecutable* entity = nullptr);

    // True if any registered trigger watches this entity -- lets main.cpp
    // build the per-zone trap candidate list once at load. See
    // TriggerExecutable::WatchesEntity().
    bool AnyTrapWatches(int typeId, const std::string& entityName) const;

private:
    // M38: object-valued script fields -- see setValue() above.
    std::map<std::string, skRValue> m_ObjectFields;
    // M38: a real trigger callback takes **two** parameters, not the
    // placeholder one every other host-triggered call in this port uses:
    // crypt1.s declares `OnOpenDoor[ (trigger, who) ]` and immediately
    // calls `trigger.IsActive()` and `who.OpenDoor("Open Door")`. So the
    // trigger itself and the entity it fired for both have to be passed
    // as real objects.
    void RunCallback(const std::string& callback, TriggerExecutable* trigger,
                     skiExecutable* entity);

    MenuStack& m_Stack;
    std::vector<std::unique_ptr<TriggerExecutable>> m_Triggers;
    std::vector<std::unique_ptr<EncounterExecutable>> m_Encounters;
};

}  // namespace sk_bindings
