#pragma once

// M25: first-person weapon viewmodel state -- the real per-tick draw
// function (`FUN_1002b1b0`, decompiled in full by this session's own RE
// pass) is the sole callee of `FUN_10029cb0` (main.cpp's RenderHud comment
// -- same ScreenModeController secondary-vtable tick hook, gated on a real
// "active weapon" `Item*` at engine+0x618+0x204 being non-null and
// screenMode==5). The real struct at that engine offset ("WeaponViewState"):
// +0x230 alternate/"swing" global.spr slot id, +0x234 interpolation/
// progress accumulator (also the branch gate -- nonzero means an active
// swing), +0x238 post-swing hold countdown, +0x244/+0x248 idle-sway phase +
// gate. Three real branches, decompiled in full: idle sway (a fixed-point-
// trig-table offset -- the table itself, `DAT_1002b330`/`DAT_1002b420`,
// wasn't extracted), post-swing hold (draws the alternate/+0x230 sprite at
// a fixed offset instead of idle), active swing (interpolates using the
// equipped Item's own +0x180 frame count / +0x184 8.8-fixed scale and a
// per-tick delta). All three converge on the same Blit_RLESprite primitive
// main.cpp's RenderHud compass/vitals bars already use.
//
// **Unresolved** (docs/PORT_ROADMAP.md's M25 entry): the exact native
// call/site that actually starts a swing (writes +0x230/+0x234/+0x238)
// wasn't found despite tracing every reachable write site from the Weapon
// class dispatcher -- same footing as RenderHud's own still-open
// FUN_1002c010 mystery. This struct's fields are a port-only recreation of
// the real one's *shape*, not a byte-for-byte match; main.cpp's own trigger
// (the same UseLeftAction/UseRightAction press that already drives melee/
// ranged/spell resolution) is the best-evidenced substitute available, not
// a decompiled fact. Frame/hold tick counts and the idle-sway curve are
// likewise this port's own choices -- the real trig table and exact tick
// constants weren't recovered.

#include "simkin_bindings/item_executable.h"

namespace sk_bindings {

struct WeaponViewmodel {
    ItemExecutable* item = nullptr;  // whichever hand last swung; null = nothing drawn
    enum class Phase { Idle, Swinging, Hold } phase = Phase::Idle;
    int frame = 0;         // current frame index while Swinging (0..item->animationFrames()-1)
    int ticksInPhase = 0;  // countdown to the next frame/phase change
    int idleSwayTick = 0;  // free-running counter driving the idle bob
};

constexpr int kViewmodelFrameTicks = 3;  // ~120ms/frame at the real 40ms tick (docs/RENDER_LOOP.md)
constexpr int kViewmodelHoldTicks = 6;   // post-swing hold before the pose returns to idle

// Starts (or restarts) a swing for whichever item just attacked -- called
// from main.cpp's tryAttack for a real or a missed swing alike (the real
// three-branch state machine above has no separate hit/miss pose). A no-op
// for an item with no real weaponSprite() (bare fists, a spell --
// SetWeaponSprite is only ever called by a real weapon script).
void StartWeaponSwing(WeaponViewmodel& vm, ItemExecutable* item);

// Advances the state machine by one game tick -- called every tick the
// gameplay screen is active, mirroring the real per-tick draw hook's own
// unconditional-every-frame cadence.
void TickWeaponViewmodel(WeaponViewmodel& vm);

}  // namespace sk_bindings
