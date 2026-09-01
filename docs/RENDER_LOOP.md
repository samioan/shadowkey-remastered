# Finding the main game loop

Phase 3's concrete starting point (per `ROADMAP.md`): identify the
render/game-loop entry point(s) by tracing outward from the labeled
`WS32`/`GDI`/`BITGDI`/`FBSCLI` import call sites. Done — the main loop
is found, and it directly explains the "low fps" symptom from the
original porting discussion.

## Method

`shadowkey/ghidra/scripts/pyghidra_find_render_loop.py` collects every
function labeled from `WS32`/`GDI`/`BITGDI`/`FBSCLI` imports (18 total),
finds every direct caller (17 functions), then every caller of *those*
(two levels of "who calls the thing that touches graphics"). Most of the
callers cluster into small, obviously-scoped helpers (font setup,
off-screen bitmap/graphics-context creation) — but one call chain stood
out for being reachable from a `CPeriodic` timer, Symbian's OS-level
repeating-timer active object.

## The chain

```
InitGameLoopTimer_CPeriodic (0x1002821c)
  EUSER::CPeriodic::Start(delay=1us, interval=40000us, TCallBack=GameTick_TCallBackTrampoline)
        |
        v
GameTick_TCallBackTrampoline (0x10028200)   -- TCallBack target, tiny trampoline
        |
        v
GameTick_UpdateAndPresent (0x1002256c)      -- fires every tick
  - FBSCLI::DataAddress -> raw backbuffer pixel pointer
  - fast path: two vtable calls on an engine subsystem object
    (offsets +0x24 and +0x1c -- shape matches Update()/Render())
  - calls PresentFrame_BlitAndComputeFPS
  - slow/startup path (first ~61 ticks, counter at engine+0x14a7c < 0x3d):
    memset's exactly 0x11e00 bytes (176*208*2 -- the N-Gage's exact
    screen buffer size, confirming this is the framebuffer) and draws
    through CCoeControl's window GC directly (splash/loading screen?)
        |
        v
PresentFrame_BlitAndComputeFPS (0x10022898)
  - CCoeControl::SystemGc() -> Activate(DrawableWindow())
  - computes elapsed time since last frame (TInt64 arithmetic), keeps a
    rolling 10-sample window (engine+0x330..), averages it into a
    smoothed FPS value stored at engine+0xbe20
  - if a flag is set (engine+0x38c > 0): draws a debug/FPS overlay via
    the GDI font-drawing cluster (FUN_1008f8a4 etc.)
  - CCoeControl::Rect() then a vtable call at +0xc0 on the SystemGc
    (shape matches CWindowGc::BitBlt) -- blits the backbuffer bitmap
    into the actual window
  - Deactivate()
```

## The headline finding: hardcoded 25Hz tick rate

`InitGameLoopTimer_CPeriodic` starts the timer with a **40,000
microsecond (40ms) interval — a fixed 25Hz tick rate**. Both simulation
update and rendering/present happen inside the same tick callback, so
this single constant caps the entire game (world update, animation,
input polling, and frame presentation) at 25Hz regardless of how fast
the hardware is.

This is a strong, concrete lead for the "the game feels low-fps" part of
the porting goal (see the original porting discussion this roadmap grew
out of) — it may well not be a raw software-rendering performance
ceiling that needs optimizing, but a **deliberate fixed-tick-rate
design decision** from the N-Gage's constrained hardware (ARM920T @
104MHz). For a PC port this means:

- Naively shrinking the 40000 constant would speed up gameplay
  simulation *and* rendering together, since they're coupled in one
  callback — likely breaks physics/animation timing tuned for 40ms
  steps (things like the "10 sample rolling average" and per-tick
  counters elsewhere in the engine may assume a fixed step).
- The real fix for a smooth port is almost certainly **decoupling**
  simulation tick rate (keep at a fixed step, or whatever the physics
  actually assumes) from presentation/render rate (uncapped, or matched
  to display refresh) — i.e. splitting `GameTick_UpdateAndPresent`'s
  two responsibilities apart, not just changing the timer interval.
  Confirming exactly which parts of the update logic are truly
  40ms-step-dependent (vs. incidentally always called together) is
  Phase 3 work, not yet done.

## Also found, and ruled out: not the loop

The single largest function in the binary (`0x10078de4`, 19,252 bytes —
the "one 19KB monster" noted in the Phase 2 scale rationale) was an
early candidate, since it's reachable from the same caller chain. Full
decompile (`shadowkey/extracted/decomp_0x10078de4.c`, regenerable via
`pyghidra_dump_full.py 0x10078de4`, gitignored) shows 121 `case` labels,
103 calls to `EUSER::Leave` (throw on invalid input — a guard pattern
typical of table lookups), and heavy `wcslen`/`wcscpy` (wide-char/
Unicode string handling). This is almost certainly a **localized
string-table lookup function** (string-ID -> localized text), which
fits the release's 5 bundled languages (EN/FR/DE/ES/IT per the install
image's directory name) — not game logic. Worth remembering so it isn't
re-investigated as a loop candidate later.

## Labels applied

Persisted as real function names + plate comments in the Ghidra project
(`shadowkey/ghidra/scripts/pyghidra_label_render_loop.py`, idempotent,
same pattern as `pyghidra_label_imports.py`):

- `InitGameLoopTimer_CPeriodic` (0x1002821c)
- `GameTick_TCallBackTrampoline` (0x10028200)
- `GameTick_UpdateAndPresent` (0x1002256c)
- `PresentFrame_BlitAndComputeFPS` (0x10022898)

## Follow-up: the engine object graph (update 2026-09-01)

Traced the two per-tick vtable calls all the way to their concrete
targets and constructors. Correction up front: the "likely
`Update()`/`Render()`" guess above turned out to be wrong — see below.

### The chain, one level deeper

`GameTick_UpdateAndPresent`'s fast-path vtable calls are:
`engine = *(appview+0x30)`, `sub = *(engine+0x28)`, then call through
`*(sub+4)` (the object's *secondary* vtable pointer — this class uses
GCC-style multiple inheritance, primary vtable at `sub+0`, secondary at
`sub+4`) at byte offsets `+0x1c` and `+0x24`.

`engine` (at `appview+0x30`) is the central engine/world object —
**`GameEngine_ctor`** (0x1000fa7c), a hefty ~85.5KB (`0x14e14`-byte)
allocation. It's `new`'d exactly once, in
**`GameEngine_FirstTickBootstrap`** (0x10023ad0, formerly `FUN_10023ad0`
— renamed, it's the "called once on the real first tick" function
already noted above). That same bootstrap function also:

- `new`s and binds a **SimKin interpreter instance** at `engine+0x3a0`
  (`SIMKIN_ord20`), then registers ~50 named script bindings
  (`SIMKIN_ord42`/`ord43`/`ord60` call triples, each passing a small
  index + a `DAT_` string constant) — this is concretely *how*
  `6r51.app` drives SimKin (an open question in `ROADMAP.md`). Worth
  dumping those ~50 `DAT_` string constants later to get the actual
  bound script global/function names.
- `new`s **`ScreenModeController`** (0x1002958c) at `engine+0x28` — this
  is `sub` above — and calls its one-time init (vtable slot `+0x10`,
  0x100298a4) if this really is a cold start.

`GameEngine_ctor` itself initializes two fixed-capacity entity arrays
inside the engine object: **40 slots × 132 bytes** at `engine+0x5464`
and **32 slots × 264 bytes** at `engine+0xbe54` (per-element
constructors `FUN_1001b3a8`/`FUN_1001b448`), plus **48 SimKin-visible
slots** at `engine+0xdfa4` (one `SIMKIN_ord22` call each). Reads like a
scene/actor pool and a light/effect pool, both individually exposed to
script — consistent with the roadmap's note that SimKin logic is
"mostly free" and drives a lot of game entity behavior.

### Correction: not a clean Update()/Render() split

`ScreenModeController`'s per-tick vtable methods (`+0x1c` →
`FUN_10029cb0`, `+0x24` → `FUN_1002a6d4`) are **not** a simple
update/render pair. Both are large state-machine dispatchers keyed off
an internal field at `this+0x78` (values like `1`, `5`, and a checked
range `0x1f..0x25` elsewhere), interleaved with SimKin calls
(`SIMKIN_ord43`) and further vtable dispatch through *other* objects.
This reads like **screen/menu/dialog mode handling** — "what is
currently shown and what should happen this tick given that mode" — not
3D world simulation or rasterization directly. `ScreenModeController`'s
constructor also sets up a 12-bit-RGB color palette and a 2KB scratch
buffer, more consistent with UI/overlay chrome than a 3D renderer.

A fourth vtable slot, `+0x38` (0x1006c274), is called conditionally from
`FUN_1002152c` on what look like pause/resume-style state transitions
(checks `engine+0x30 → +0x28`'s state field against range `0x14..0x1a`).

### Next concrete lead: the 0x1006Bxxx–0x1006Dxxx cluster

The `+0x38` slot (and several nearby, not-yet-called-from-here vtable
entries at `+0x2c`/`+0x30`/`+0x34`/`+0x40`, read directly out of
`ScreenModeController`'s vtable at `0x100fb908`) all live in a distinct
code region, `0x1006Bxxx`–`0x1006Dxxx`, separate from the
`0x1002xxxx`-range "app/UI" cluster everything above lives in. This is
also where the earlier crash/error-display function's callers
(`FUN_1006c31c`, `FUN_1006dbec`) live. **This cluster is the strongest
remaining lead for where the actual 3D rasterization / world-simulation
code is** — not yet explored in depth.

### Labels applied (update)

Added to the Ghidra project via the same
`pyghidra_label_render_loop.py` (now also handles comment-only, lower
confidence entries):

- `GameEngine_FirstTickBootstrap` (0x10023ad0)
- `GameEngine_ctor` (0x1000fa7c)
- `ScreenModeController_ctor` (0x1002958c)
- Plate comments (not renamed — not confident enough yet) on the four
  observed vtable call targets: 0x100298a4 (`+0x10`, one-time init),
  0x10029cb0 (`+0x1c`), 0x1002a6d4 (`+0x24`), 0x1006c274 (`+0x38`).

New script: `shadowkey/ghidra/scripts/pyghidra_read_vtable.py <DAT_addr>
[N]` — resolves a decompiler `DAT_x` literal-pool reference to the
actual vtable it points at and lists the first N entries as (offset,
target address, function name if Ghidra already knows it). This is how
`ScreenModeController`'s vtable at `0x100fb908` was found and read.

## Open follow-ups

- Explore the `0x1006Bxxx`–`0x1006Dxxx` cluster — best remaining lead
  for 3D rasterization / world-simulation code.
- Dump the ~50 SimKin script-binding `DAT_` string constants registered
  in `GameEngine_FirstTickBootstrap` to get real script-visible names.
- Identify the two entity pools' element structure (40×132B, 32×264B) —
  likely actors/objects and lights/effects respectively, unconfirmed.
