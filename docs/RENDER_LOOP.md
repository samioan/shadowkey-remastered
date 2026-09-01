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

## Open follow-ups

- Identify the vtable/class behind the two per-tick subsystem calls in
  `GameTick_UpdateAndPresent` (offsets +0x24/+0x1c) — likely candidates
  for "the" `Update()`/`Render()` split relevant to decoupling tick rate
  from present rate.
- `FUN_10023ad0` (called once, guarded by a first-tick flag inside
  `GameTick_UpdateAndPresent`) looks like first-frame/one-time init, not
  yet decompiled in depth.
- Where does the *actual* 3D rasterization happen? Not yet located —
  likely inside whatever the +0x24/+0x1c vtable calls dispatch to.
