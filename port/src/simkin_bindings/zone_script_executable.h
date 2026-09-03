#pragma once

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
// "GPZombiesKilled");`, e.g. `ghstpass.s`/`lothcav.s`) -- only the
// latter is implemented (`TriggerExecutable` below): the former needs
// real trigger-volume/position data this port doesn't have (same gap
// `EnterZone`'s own trigger tags are blocked on), while the kill-count
// variant is purely data-driven and composes directly on top of
// already-real infrastructure (M12's combat loop, M17's quest state).

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
    TriggerExecutable() : NativeStubExecutable("Trigger") {}

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

    int entityId() const { return m_EntityId; }

    // Called by ZoneScriptExecutable::NotifyKilled() for every kill of a
    // monster whose real typeId matches entityId() -- returns true the
    // one time this call brings the running count up to limit() (never
    // again after, whether or not more kills follow), telling the caller
    // to actually fire the real script callback() this instant.
    bool NotifyKilled();
    const std::string& callback() const { return m_Callback; }

private:
    int m_EntityId = -1;
    int m_Limit = 0;
    std::string m_Callback;
    int m_KillCount = 0;
    bool m_Fired = false;
};

class ZoneScriptExecutable : public skScriptedExecutable {
public:
    ZoneScriptExecutable(const skString& filename, skExecutableContext& ctxt, MenuStack& stack);

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

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

private:
    MenuStack& m_Stack;
    std::vector<std::unique_ptr<TriggerExecutable>> m_Triggers;
};

}  // namespace sk_bindings
