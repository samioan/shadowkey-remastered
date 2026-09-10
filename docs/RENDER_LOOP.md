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

## Screen modes 1 and 5: which is which (M91)

`SetScreenMode(app, 1)` and `SetScreenMode(app, 5)` are the two modes the
game spends nearly all of its time in, and it is easy to guess them the
wrong way round. Three call sites settle it, and they only make sense one
way:

* **`FUN_100779b8`** — open-a-menu-by-name — ends with
  `if (appview->threadRunning == 0) SetScreenMode(app, 1)`. Opening a menu
  sets **1**.
* **Menu binding `0x32` `Quit`** — "close this screen and go back to the
  game" — ends with `SetScreenMode(app, 5)`. So do `0x35`
  `QuitAndDestroyOpener` and `0x36` `QueryDestroy`.
* **`FUN_10038674`** — the Player binding `SetCameraStart` — writes the
  player's position and all three orientation channels and then calls
  `SetScreenMode(app, 5)`. That is the last thing a level entry does.

And the app's own foreground handler confirms it from the other side:

```c
/* FUN_1002152c -- the app comes back to the front */
if (app->controller->mode == 5) {
    FUN_100779b8(app->controller->menuManager, "MainMenu", 1, 0);
}
```

"If the player was *playing* when the phone took the call, put the main
menu up over the game." That third argument is the menu manager's
`+0x49` — "this screen is open over a live session" — and it is the only
in-game main-menu entry point in the image.

So: **1 is a menu/UI screen, 5 is normal gameplay.** `port/src/engine/
screen_mode.h` had the pair transposed from M54 until M91.

## Screen mode 0x1f: quitting to the main menu (M54)

`ScreenModeController + 0x78` is the screen mode. M26 found that the
progress bar (`FUN_1002c010`, the controller's own vtable slot `+0x44`) is
drawn for four of them; M42 named three by walking `SetScreenMode`'s
(`FUN_1006a344`) call sites, and M44 ruled out the remaining lead by
showing that `0x1f` is not one of the fade-arming function's deferred
targets either. It is neither, because **nothing passes it to anything**:

```c
/* FUN_10026f40 -- ScreenModeController vtable slot +0x3c */
iVar1 = RThread::Create(&this->quitThread, name /* "nGEN_Quitting" */,
                        FUN_10027ce0, 0x6400, 0, &this->self, 0);
if (iVar1 == 0) {
    this->busy = 1;
    this->threadRunning = 1;
    *(int *)(this->engine->controller + 0x78) = 0x1f;   /* <- here */
    RThread::SetPriority(&this->quitThread, EPriorityMuchLess);
    RThread::Resume(&this->quitThread);
    ...
}
```

The mode is written **straight into the field**, beside the `RThread`
that does the work. That is why every search for it as an argument came
back empty, and why the caller search came back empty too: slot `+0x3c`
is a three-instruction thunk at `0x1006c268` that Ghidra never marked as
a function at all, so it appears in no call graph.

```asm
1006c268  LDR r3, [r0, #0x14]   ; controller->engine
1006c26c  LDR r0, [r3, #8]      ; engine->appview
1006c270  B   FUN_10026f40      ; spawn nGEN_Quitting
```

It is slot `+0x3c` in both the concrete `ScreenModeController` vtable
(`0x100fb908`) and its base (`0x100fe574`), one slot below the level
changer (`+0x2c`, `FUN_1006c31c`, mode **3**) and two below the bar draw
(`+0x44`).

### Two background threads, one progress bar

There are exactly two named threads in the whole image, and they are a
symmetric pair:

| | loading | quitting |
|---|---|---|
| thread name | `nGEN_Loading` | `nGEN_Quitting` |
| spawner | `FUN_10027d44` | `FUN_10026f40` |
| entry point | `FUN_10027d0c` | `FUN_10027ce0` |
| body | `GameEngine_InitLevel` | `FUN_1002707c` |
| closing log line | `"Loading thread closed"` | `"Quitting thread closed"` |
| screen mode | set by the caller (3 or 10) | set by the spawner (**0x1f**) |
| progress values | 0, 3, 5, …, 98, 100 | 4, 15, 22, 30, 40, 80, 100 |

Both write the same counter (`appview+0x408`, whose setter is
`FUN_10027e34`), and that counter is exactly what the bar samples — the
draw call in `FUN_10029cb0` passes it in as argument 6:

```c
if (mode == 10 || mode == 3 || mode == 0x1f || mode == 4) {
    Blit_RLESprite(engine, fb, 0, 0, engine->sprites[174], ...);   /* splash */
    (**(vt + 0x44))(ctrl, 0, 0, 0, 0, appview->progress, 0);        /* the bar */
    if (mode == 10 || mode == 3) { /* "Travel to: <zone>" */ }
    if (engine->inMultiplayer && engine->messagePending) { /* see below */ }
}
```

Two details worth keeping. The banner is nested *inside* the gate, under
`mode == 10 || mode == 3` only — the two that travel to a named zone — so
a save and a quit show the bar over the bare splash. And the sub-dispatch
one line above the gate is `if (mode < 0x25 && 0x1f < mode)
FUN_1002bdf4(...)`, the vignette's own per-frame tick: `0x1f` sits one
below that run and is deliberately excluded from it, which is why M44's
"the vignette run beginning one above it" lead went nowhere.

### What the quitting thread actually does

`FUN_1002707c`, in order, with its progress writes:

| progress | work |
|---|---|
| 4 | stop the level's audio (`FUN_100096b8(snd, 5)`, `FUN_100094ac(snd, 0)`) |
| 15 | engine-side teardown (`FUN_1000e3c4(engine, 0, 0)`, `FUN_1000e5c8`) |
| 22 | `FUN_10023910(this, 1)` — free every loaded asset; then queue the `menu` sound set (`FUN_100092f4(snd, "menu")`) |
| 30 | clear the multiplayer flags, stop the world object (`world->vtable[0x178](world, 0, 0)`) |
| 40 | `FUN_10024c8c(this, "menu")` — load the `menu` sprite manifest |
| 80 | `FUN_1001b180(engine, 70, 100, 0xff)` — start the front-end music |
| 100 | `FUN_100779b8(ctrl->interp, "MainMenu", 0, 0)` — open mainmenu.s |

then `if (mode != 0xb) mode = 1;`, logs `"Quitting thread closed"` and
kills its own `RThread`.

Three of those resolve things this port had only half-named:

- **`FUN_10023910` is `UnloadLevelAssets`.** It frees all 384 `global.spr`
  slots at `engine+0x4460`, all 256 model slots at `engine+0x6b38`, and
  several other caches — **except slots `0xcd` and `0xce`**, which it
  skips by index. `0x4460 + 205*4 = 0x4794` and `+ 206*4 = 0x4798` are
  exactly the two pointers `FUN_1002c010` reads. The progress bar's own
  art is the one thing a level unload is not allowed to throw away, which
  is what lets the bar keep drawing on a screen where nothing else is
  loaded any more.
- **`FUN_10024c8c` is the `<level>_sprites.txt` manifest loader** —
  `sprintf("%s\\%s_sprites.txt", dataDir, level)`, `strtok` on newlines,
  `atoi` each line, and pull that slot out of `global.spr` (whose per-slot
  size table it caches at `this+0x388` on first use). It has a dead
  `strcmp(level, "menu")` whose `CMP r0, #0` at `0x10024cf0` nothing
  consumes — a special case that was removed but not deleted.
- **`"menu"` is a real pseudo-level.** It ships `menu_sprites.txt`,
  `menu_models.txt` and `menu_sounds.txt` but no `.zon`, `.ent` or `.zmp`:
  a manifest set, not a place. Slot 70 in `menu_sounds.txt` is
  `battle3.ogg`, the only non-`NULL.wav` music entry in it — so the
  progress-80 `FUN_1001b180(engine, 0x46, 100, 0xff)` is the main menu's
  own theme starting up.

### Where it is called from, and what it is called

Slot `+0x3c` has three callers:

1. **`FUN_10078de4` case `0x33`** — the GameEngine root SimKin dispatcher.
   The registration pass names index `0x33` on the root trie (`0x14cf0`)
   **`QuitToMenu`**, and the three consecutive cases confirm each other:
   `0x32` `Quit` sets mode 5, `0x33` `QuitToMenu` calls this slot, `0x34`
   `QuitGame` ends the process. The case first clears `engine+0x14a81`,
   the pending-message flag below.
2. **`FUN_1004fc50`**, the screen fade, when its deferred target
   (`engine+0x5ac`) is `0xb` — a fade straight into a session end.
3. **`FUN_1003627c`**, twice, on the multiplayer paths that lose the
   session out from under the player.

Seven shipped scripts call `QuitToMenu()`: `deathmenu.s`'s
`DeathMenuBack` and `mpdeathmenu.s`'s `MPDeathMenuBack` (both with a
`//QuitGame();` left commented out on the line above the call that
replaced it), `gameended.s`'s and `mainmenu.s`'s `CancelEndGame`,
`saveconfirm.s`'s `MenuDoneSave`, and `savegamecorrupted.s` /
`savegamenospace.s`, which name it as a menu row's handler outright
(`AddMenuItem(3591, "QuitToMenu")` — string 3591 is "Exit").

Two of those call something else in the *same* handler right afterwards
(`QuitToMenu(); OnDisplay();` and `QuitToMenu(); Init();`), which only
works because the native does not block: it hands the teardown to the
thread and returns at once.

### The one thing the 0x1f screen draws that a zone load does not

```c
if (engine->inMultiplayer /* +0x5c0 */ && engine->messagePending /* +0x14a81 */) {
    DrawWrappedText(app, engine + 0x14a82, 0, 0x6e, 0xb0, 0x3c, white, ..., font 0x19);
}
```

`engine+0x14a82` is a wide-string buffer filled just before the quit is
triggered, and every writer of it is a multiplayer path. The two constant
messages come from the string table: `+0x3b8c` is id **3811**
`"Connection lost"` (Bluetooth dropped) and `+0x3fc4` is id **4081**
`"Game terminated by the host "`. So the quit screen doubles as the
"here is why your session just ended" screen — and only ever in
multiplayer.

> The string-table offsets are plain indices: `offset / 4`. M26 already
> knew id 3950 is `"Travel to: "`, and `0x3db8 / 4 == 3950` exactly, with
> `0x3fc4 / 4 == 4081` landing on the last of the table's 4082 entries.

### Implemented in the port

`port/src/engine/screen_mode.h` holds the recovered table (which modes
draw the bar, which of those draw the banner, both progress lists, the
reserved sprite slots, the front-end level name and music slot);
`MenuStack::RequestQuitToMenu()` and `MenuExecutable`'s `QuitToMenu` /
`QuitAfterSave` / `SetQuitAfterSave` handlers implement the natives; and
`main.cpp` renders the mode-0x1f screen on the quit stage list before
tearing the session down and reopening MainMenu. Smoke test
`src/tests/m54_quit_to_menu_smoke.cpp`.

## Open follow-ups

- ~~Explore the `0x1006Bxxx`–`0x1006Dxxx` cluster~~ — done, see
  [`WORLD_MODEL.md`](WORLD_MODEL.md). Turned out to be save/load and
  level-transition management, not rendering — but tracing
  `GameEngine_ctor` fully in the process found the actual World/Map
  object (`engine+0x618`, a 2D tile grid) and its central accessor,
  `Map_GetTileAt`, which has 30+ call sites across the binary. The
  rasterizer itself still isn't located — `WORLD_MODEL.md`'s follow-ups
  have the current best next-step ideas.
- Dump the ~50 SimKin script-binding `DAT_` string constants registered
  in `GameEngine_FirstTickBootstrap` to get real script-visible names.
- Identify the two entity pools' element structure (40×132B, 32×264B) —
  likely actors/objects and lights/effects respectively, unconfirmed.
