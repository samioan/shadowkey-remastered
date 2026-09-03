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

#include "skScriptedExecutable.h"

namespace sk_bindings {

class MenuStack;

class ZoneScriptExecutable : public skScriptedExecutable {
public:
    ZoneScriptExecutable(const skString& filename, skExecutableContext& ctxt, MenuStack& stack);

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

private:
    MenuStack& m_Stack;
};

}  // namespace sk_bindings
