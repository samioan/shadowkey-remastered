# The world's level data is a 2D tile grid

**Correction (2026-09-01): this does NOT mean the game is rendered in
2D.** Shadowkey is a real-time first-person 3D dungeon crawler — the
finding below is about how *level layout/collision data* is stored
(a 2D grid of cells), which is completely normal even for fully
3D-rendered games in this genre (Ultima Underworld, Eye of the
Beholder, etc. all use a 2D grid for level data while rendering true
3D walls/floors/ceilings/lighting from it). The actual first-person 3D
rasterizer is a separate, still-unlocated piece of code that almost
certainly *consumes* this tile grid (extruding cells into 3D wall/floor/
ceiling geometry or texture-mapped surfaces per frame) rather than
being contradicted by it. Originally this doc overclaimed "not open 3D
geometry" — struck through below, kept for the record.

Following up on `RENDER_LOOP.md`'s open lead (the `0x1006Bxxx`–`0x1006Dxxx`
code cluster) turned up something more useful than expected: not the
rasterizer, but confirmation of how the game world itself is represented.

## What the `0x1006Bxxx` cluster actually is

Chasing `ScreenModeController`'s remaining vtable slots (`+0x2c`
`FUN_1006c31c`, `+0x30`/`+0x34` `FUN_1006be18`/`FUN_1006bdc8`) showed
this cluster is **save/load and level-transition management**, not
rendering: file-existence checks (`ESTLIB::fopen`/`fclose` probes on a
`sprintf`'d save-slot filename), a level-change routine (`FUN_1006c31c`),
and a level-unload/cleanup routine (`FUN_1006c240`). Correcting the
previous doc's guess: this isn't where the 3D/tile rendering code lives.

> **Correction (M42).** This paragraph used to describe `FUN_1006c31c` as
> "load level or show an error dialog", calling "the earlier-identified
> crash/error display function `FUN_10018e70` on failure".
> `FUN_10018e70` is not an error display — it is the **save-game writer**
> (see [`SAVE_FORMAT.md`](SAVE_FORMAT.md)), and `FUN_1006c31c` calls it
> *first*, unconditionally, to autosave `current.sav` before changing
> level, aborting the change if the save fails. What gave the wrong
> impression is that it does show a message on failure and returns a
> non-zero code — but the code is `3`, "not enough free disk space", and
> the caller that reads it is `ActuallySaveGame`.

## `engine+0x5cc` is GAMECOMMS (networking), not the world

While tracing `GameEngine_ctor` (0x1000fa7c) fully, found it constructs
a ~2.2KB (`0x8bc`) object at `engine+0x5cc` via `FUN_1000bc30`, whose
body is entirely `GAMECOMMS_ord1/20/27/28/30/37/41` calls — this is the
Bluetooth multiplayer subsystem, already deprioritized per
`ROADMAP.md`'s Phase 3 prioritization. Noting this so it isn't
re-investigated as a world/rendering candidate later.

## `engine+0x618` is the actual World/Map object

`GameEngine_ctor` also loads a data file early on: formats a filename
(languge/save-slot dependent), reads it, and if present, parses it via
`FUN_10015878` (a generic array-of-variable-length-`ushort`-arrays
deserializer — most likely the localized string/font-glyph table
feeding the giant 121-case string dispatcher noted in `RENDER_LOOP.md`,
not world geometry). If loading fails, or as part of the same sequence,
`engine+0x618` — zero-initialized earlier in the same constructor — gets
built via `FUN_1000e3c4`/`FUN_1000e5c8` when no save data is present.

`engine+0x618` (call it **`CMap`**) is referenced constantly throughout
gameplay code as the tile-grid manager:

- `map+0x2c` = grid width (in tiles)
- `map+0x30` = grid height (in tiles)
- `map+0x6908` = pointer to the per-cell tile array, **8 bytes per tile**
- `map+0x690c` = pointer to a shared tile-*type* definition table,
  **36 bytes (0x24) per entry**, indexed by a type ID stored in each
  cell
- `map+0x618` = a further sub-object holding the player/camera's current
  position and orientation, e.g. `+0x94`/`+0x9c` (position, each an 8.8
  fixed-point value, `>>8` recovers the tile-integer coordinate); see the
  object-identity correction right below for how this was pinned down
  precisely.

**Object-identity correction/unification (found while tracing
`RENDERER_3D.md`'s tile-grid wall/surface-face renderer)**: this `CMap`
object turns out to be **the exact same object every other doc in this
project calls "`engine`"** — the one with `+0x480` (framebuffer),
`+0x5b4`/`+0x5b8` (composite/stencil buffers), `+0xbe34` (entity BST),
`+0x6b38` (model cache), `+0x62c` (room-render state), etc. (see
`RENDERER_3D.md`, `ZONE_FORMAT.md`, `MODEL_FORMAT.md`). There is no
separate wrapper object in between — `GameEngine_InitLevel`,
`Bullseye_Init`/`Bullseye_InitMap`/`Bullseye_LoadZmpCells`/
`Bullseye_BakeLighting`, `Render3DScene`, and `SurfaceFace_*` are all
effectively **methods of this one `CMap`/"engine" object**, called with
it directly as their own `param_1` — not reached through a `+0x618`
indirection from some other object. `map+0x618` (this doc's original
finding) is instead **`CMap`'s own field**, pointing to a smaller
player/camera sub-object (position `+0x94`/`+0x9c`, plus orientation
fields referenced throughout `RENDERER_3D.md`'s actor/transform code).
Verified directly: `TileGrid_RaycastVisibility` (`FUN_1000f694`, see
"Per-frame tile-visibility raycasting" below) reaches this object both
as its own `param_1` *and* passes that same `param_1` into
`Map_GetTileAt` as the `CMap*` argument — proving they're identical.
(`GameEngine_ctor` constructing this object at `+0x618` of *its own*
`this` still stands — that `this` is the actual Symbian application
object, one level further out than anything else in this project has
needed to name.)

**`Map_GetTileAt(CMap*, TInt xFixed8_8, TInt yFixed8_8)`** (renamed from
`FUN_1001b004`) is the accessor: bounds-checks x/y against width/height,
computes a row-major index (`width*y_tile + x_tile`), and returns
`tileArray + index*8` (or `0` out of bounds / unloaded). It has **30+
call sites spread across dozens of functions all over the binary** —
this is the engine's single, ubiquitous "what's at this world position"
primitive. Nothing else comes close to that call-site count in this
binary, which is strong evidence this is genuinely central to how the
game represents and queries its world.

**This confirms the world's *level data* is a simple 2D tile grid** —
consistent with "The Elder Scrolls Travels" being a portable-hardware
dungeon crawler that (like Ultima Underworld/EOB before it) likely
lays out levels on a grid for authoring/collision/pathing, then renders
a true first-person 3D view *from* that grid each frame. ~~not open 3D
geometry~~ (struck through — overclaimed; see correction note at top
of this file). What this actually changes for the porting goal: wall/
floor/ceiling geometry is probably *generated per-cell from the grid*
(each cell's type table entry likely encodes texture IDs, wall
presence, height, etc.) rather than being arbitrary authored 3D meshes
— which is still very relevant to a port (it constrains what "the
renderer" needs to reproduce), just not "the game is 2D."

## What was found along the way, per-tick tile scan

`ScreenModeController`'s `+0x1c` per-tick method (`FUN_10029cb0`) calls
`FUN_1002b430` once per tick (in at least one of its mode branches).
`FUN_1002b430`:

1. Draws some text via the font-drawing helper also used for the debug
   FPS overlay (`FUN_1008f97c`) — likely a location/HUD label.
2. Reads the player's tile position from `CMap`'s player/camera
   sub-object (`*(map+0x618)+0x94`/`+0x9c` — corrects an earlier version
   of this note that wrote `map+0x94`/`+0x9c` directly; see the
   object-identity correction above).
3. Scans a 65×65-tile window (`-0x20..+0x20` in both axes) centered on
   the player via repeated `Map_GetTileAt` calls, and for each
   neighboring pair of tiles, looks up their tile-type's height field
   (in the 36-byte type table at `map+0x690c`) and computes the
   absolute height difference between them.

Not fully understood yet. A 65×65-tile radius is large for anything
evaluated inside the real-time first-person view's per-frame draw
(typical dungeon-crawler render/visibility distance is a handful of
cells), so — now correctly framing this as *not* evidence the game is
2D — the better-supported guess is that this is the **automap/map
screen** (consistent with the text label drawn first, e.g. a location
name) rather than the first-person renderer itself. Left as
`FUN_1002b430` (un-renamed, plate-comment only) pending more evidence
either way.

## Per-frame tile-visibility raycasting: how `Render3DScene` picks which faces to draw

Found while resolving `RENDERER_3D.md`'s open item on the tile-grid
wall/surface-face renderer's traversal. `Render3DScene` calls
**`TileGrid_RaycastVisibility`** (renamed from `FUN_1000f694`, 0x1000f694)
once per frame, before its face-drawing loop:

- Casts a **fan of rays** outward from the player/camera position, using
  the *same* 2048-entry sin/cos LUT shape as
  `BuildRotationMatrix3x4`/the automap marker/`Bullseye_PropagateLight`.
  Three tiers are selected from `engine+0x608`:

  | `engine+0x608` | rays | max steps | angle step | **total arc** |
  |---|---|---|---|---|
  | `< 0x101` | 150 | 25 | 170 | 140° |
  | `< 0x201` | 177 | 93 | 96 | 93° |
  | otherwise | 178 | 172 | 52 | 51° |

  > **Correction.** An earlier pass read this as a 3-tier *quality*
  > setting. It is a **zoom**: multiplying `rays × angleStep` in the
  > engine's own 65536-per-turn angle unit gives the fan's total arc, and
  > the three tiers narrow the arc (140° → 93° → 51°) while pushing the
  > range out (25 → 93 → 172 steps). A detail level would not change the
  > field of view; a zoom factor does exactly this — and `engine+0x608`
  > is the same field used elsewhere as a `0x100`-is-identity render-scale
  > factor. Note the boundary is `< 0x101`, so identity itself takes the
  > *widest* tier.
- **Every ray starts four steps behind the camera** (`x = camX - 4·dx`),
  which is how the tile the player is standing on, and the ones just
  behind them, get into the set at all — the fan itself never points
  backwards.
- Each ray then steps by a fixed one-tile increment (the LUT's amplitude
  is `0x200` and the code takes `value >> 1`, giving exactly `0x100` per
  step in the 8.8 world coordinates), stopping when it reaches a tile
  whose flags satisfy **`(flags & 0b1010) == 0b0010`** — bit1 (wall) set
  *and* bit3 (force-draw) clear, so a force-draw cell does not block even
  though it is a wall. Bit1 is the *same* bit
  `Bullseye_PropagateLight`'s light-bounce rays stop at, so one "this tile
  is solid" bit governs both light propagation and rendering visibility.
- **Walls are ignored for the first five steps** (`if (step > 4)`), which
  is what keeps the tiles immediately around the camera in the set
  regardless of what they are.
- Every tile a ray passes through gets appended to a per-frame visible-
  tile list (max 512 entries), **deduplicated** via a newly-decoded tile
  record field: **byte 6**, a rotating 0–3 "last visible frame" stamp
  compared against a frame counter at `engine+0x478` — extending the
  8-byte tile record (previously flags/unknown0/lightLevel/typeId, see
  `ZONE_FORMAT.md`'s `Bullseye_LoadZmpCells` finding) to 7 of its 8 bytes
  decoded; only byte 7 remains unknown.
- Also opportunistically sets bits in a packed bitmap reached through
  `*(engine+0x470)+0x45c` (the SimKin object registry) — looks like a
  "tiles the player has explored" bitmap feeding an automap reveal-as-
  you-go mechanic, not confirmed in depth.

`Render3DScene`'s main loop then walks this visible-tile list (not a
fixed-radius or full-grid scan) and, for each tile, checks each cardinal
neighbor's **type-table entry** (the 36-byte `.zcp`-loaded record, see
`ZONE_FORMAT.md`) for a per-direction `.sur` index byte — **every
direction now mapped to a specific byte offset**, including a second
"upper band" per wall direction and two ceiling bands (see
`ZONE_FORMAT.md`'s `ZcpEntry` struct). A value of `0xff` means "no
face here" (open space); otherwise a face gets drawn via
`SurfaceFace_BuildAndProject` (`RENDERER_3D.md`), gated additionally by
either the camera being on the correct side of that tile boundary (an
implicit backface/already-passed cull) **or** the current tile's `flags`
bit3 being set (forces the draw regardless — exact intended use, e.g.
"always double-sided," not confirmed).

This answers this doc's own long-standing open question ("the actual
first-person rasterizer/raycaster still not located" below): it's not a
single dedicated raycaster — it's `TileGrid_RaycastVisibility` (what's
*visible*) feeding `SurfaceFace_BuildAndProject`/`SurfaceFace_
ClipAndDispatch`/`SurfaceFace_RasterizeTextured_v0..v3` (how it gets
*drawn*), on top of the `.zsk`-baked whole-room mesh for the parts that
don't need per-tile dynamic faces. See `RENDERER_3D.md`'s "The tile-grid
wall/surface-face renderer" section for the drawing half of this.

## Labels applied

- `Map_GetTileAt` (0x1001b004)
- `TileGrid_RaycastVisibility` (0x1000f694)
- Addendum to `GameEngine_ctor`'s existing plate comment noting
  `engine+0x618`'s role.
- Addendum to `Map_GetTileAt`'s comment documenting the CMap/"engine"
  object-identity unification above.
- Addendum to `Bullseye_LoadZmpCells`'s comment documenting the extended
  tile-record layout (byte 6).

New reusable script: `shadowkey/ghidra/scripts/pyghidra_find_reads.py
<hex-offset>` — like `pyghidra_find_field_writes.py` but for `LDR`
instead of `STR`, used to find every function that *consumes* a known
object field (here, `engine+0x480`, the cached raw framebuffer
pointer) rather than every function that sets it. This is how
`Map_GetTileAt`'s huge call-site count, and `FUN_1002b430`, were found.

## Open follow-ups

- ~~What `FUN_1002b430`'s tile-height-difference scan actually
  produces~~ — still genuinely unconfirmed (automap vs. something else),
  but now better-contextualized: it's clearly **not** the first-person
  visibility system, since that's `TileGrid_RaycastVisibility` (resolved
  above) — a 150-178-ray fan out to 25-172 tiles, structurally nothing
  like `FUN_1002b430`'s 65×65 full-window scan.
- ~~The actual first-person rasterizer/raycaster still not located~~ —
  **resolved**, see "Per-frame tile-visibility raycasting" above and
  `RENDERER_3D.md`'s tile-grid wall/surface-face renderer section.
- ~~What reads a tile's *texture*/*wall* fields... out of the 36-byte
  type-table~~ — **resolved**: `Render3DScene`'s traversal reads a
  per-direction `.sur`-index byte for every wall direction (×2 height
  bands each), both ceiling bands, and floor — every relevant byte from
  `0x16` to `0x20` mapped, see `ZONE_FORMAT.md`'s `ZcpEntry` struct. The
  *height* field this doc originally found is now understood to be (at
  least) two `u16`s at `0x04`/`0x14` (`heightA`/`heightB`), used both for
  wall-band selection and per-vertex UV math — not broken down to
  individual corner/sub-field precision.
- The 36-byte tile-type table's (`.zcp`, see `ZONE_FORMAT.md`) and 8-byte
  per-cell tile's field layouts are now decoded for everything this
  project's traced code actually reads: the tile record is
  flags(bit0=light source/bit1=wall/bit2=?/bit3=force-draw-face)/
  unknown0/lightLevel/typeId/visibleFrameStamp — 7 of 8 bytes; the
  36-byte type entry has lightDelta(0x00), two height fields
  (0x04/0x14), 11 per-direction `.sur`-index bytes (0x16-0x20, every
  wall/ceiling/floor face mapped), and 3 trailing unknown bytes
  (0x21-0x23). Remaining unknowns: tile byte 7, the height fields'
  finer sub-structure (individual corners?), and those 3 trailing
  bytes.

## The monster AI: packages, distances, and what the script values mean

Decompiled this session (PC-port work), resolving what earlier passes of
this project recorded as "the real AI state machine is opaque/native".
Functions:

- **`FUN_10082224`** — the per-tick AI update (target acquisition, pursuit,
  stand-off, attack cadence, give-up).
- **`FUN_100683d4`** — the actor-to-actor "distance" the AI compares
  against, reached through actor vtable slot `+0x5c`
  (vtable `0x100fe2b8`).
- **`FUN_10082004`** — a 3D line-of-sight/reach raycast from the attacker's
  eye toward the target, stepping a caller-supplied budget.
- **`FUN_100815e0`** — the actor constructor, and therefore the source of
  every default below.
- **`FUN_10084924`** — the Monster class's Simkin dispatcher.
- `FUN_10086454`/`FUN_10086514` are the AI block's save/load
  serialisation; `FUN_100866c0`/`FUN_10086f9c` are two constructors.

### The AI package field (`monster+0x2a8`)

| value | meaning | set by |
|-------|---------|--------|
| `-1`  | asleep / no package | the actor constructor, and `AiSleep` |
| `2`   | idle — look for a target | default after spawn; `AiDetect` |
| `3`   | pursue/attack a target (`monster+0x20c`) | the tick, on acquiring a target; `AiAttack(target)` |
| `4`   | flee | `AiFlee(target)` |
| `6`   | spell-assist a target | `AiSpellAssistTarget(target)` |

### **The distance unit — the key finding**

`FUN_100683d4` is:

```c
int distance(self, other) {
    int dy = other->y - self->y;
    int dx = other->x - self->x;
    return (dy*dy >> 8) + (dx*dx >> 8);     // == (dx^2 + dy^2) / 256
}
```

So every AI distance threshold a script sets is a **scaled squared**
distance, not a linear radius. The real separation a value stands for is:

```
worldUnits = sqrt(value * 256) = 16 * sqrt(value)
```

This matters a great deal, because read linearly the shipped values look
absurd — and a port that reads them that way gives monsters map-wide
aggro:

| script call | occurrences | linear (wrong) | real |
|-------------|-------------|----------------|------|
| `SetChaseRadius(18000)` | 222 | 70 tiles | **8.4 tiles** (2147 units) |
| `SetChaseRadius(10000)` | 31 | 39 tiles | 6.3 tiles |
| `SetChaseRadius(50000)` | 2 | 195 tiles | 14.0 tiles |
| `SetChaseRadius(1)` / `(15)` | 3 | — | ~0 — "never chase" sentinels |
| `SetAttackRange(12000)` | 20 | 47 tiles | **6.9 tiles** |
| `SetAttackRange(3000)` | 4 | 12 tiles | 3.4 tiles |
| default `+0x2dc` = `0x6a4` | — | — | 2.6 tiles (660 units) |
| default `+0x2b8` = `0x7fff` | — | — | 11.3 tiles |

Note "tiles" is a fine-grained unit here: a real door model is ~1036 raw
units tall (~4 tiles) and a person ~800, so the default 660-unit stand-off
is roughly arm's length, not a room away.

### Which field each script call writes

The Monster dispatcher's `switch` cases run **3 below** the binding-table
indices in `shadowkey/simkin_native_bindings.json`. That offset is pinned
down by a Set/Get pair landing on one field — case `0x26` writes
`monster+0x2b8` and case `0x21` reads it back, and `SetChaseRadius`/
`GetChaseRadius` are table indices `0x29`/`0x24` — and corroborated by
case `0x24` writing `+0x2b2`, the same field the death handler
(`FUN_10083c04`) hands to a positional `PlaySound`, i.e. `SetDeathNoise`.

| field | written by | role in the tick |
|-------|-----------|------------------|
| `+0x2b8` (u32) | `SetChaseRadius` | give-up distance: `if (chase < dist)` → drop target, back to package 2 |
| `+0x2dc` (u16) | `SetAttackRange` | stand-off: `if (dist < attackRange)` → zero velocity, stop pathing, swing. Also supplies the LOS raycast's step budget as `>> 8` (i.e. the same threshold expressed in tiles²) |
| `+0x2be`..`+0x2c1` (bytes) | the four `Set*Animation` calls | animation indices passed to vtable `+0x148` |
| `+0x2b0`/`+0x2b2`/`+0x2b4` (u16) | `SetAttackNoise`/`SetDeathNoise`/`SetIsHitNoise` | sound ids |
| — | `SetMeleeAttackRange` | **a genuine no-op** — its case falls straight through to the shared `break` and stores nothing (only one script in the corpus calls it). Same for `AiActivate` and `AiWounded`. |

### Aggro is gated on line of sight, not distance alone

In single-player the only candidate target is `engine+0x618` (the player).
Acquiring it does **not** rest on the chase radius alone: the tick calls
actor vtable `+0x21c` (`FUN_10004d70`) and `FUN_10082004`, a 3D raycast
from the attacker's eye toward the target with a step budget of
`attackRange >> 8`. The generous radii above only make sense alongside
that check — which is why a port that skips it, however it scales the
radius, ends up with the whole level converging on the player.

### The flee package, the attack cadence, and the status-effect system

Second AI pass (PC-port session), covering what M31 left open.

**The attack cadence (`monster+0x2c4`)** is the only thing pacing a
creature's swings. The tick adds the engine's per-frame delta
(`FUN_1001afa4`, a field the engine writes each frame) to it every frame,
gates the whole attack block on `0x100 < accumulator`, and afterwards
resets it to `rand & 0x1f` — a small random jitter so a pack doesn't swing
in lockstep. Nothing else throttles attacking: `monster+0x294` looks like a
cooldown but is never written by the attack, only by the dispatcher (see
below).

**`monster+0x294` is the paralysis/stun lockout**, not an attack cooldown.
The attack function's (`FUN_100835b8`) very first test is
`monster+0x294 < 1`, and the tick only steers toward a target while the
value is exactly `0`; the tick counts it down by the same per-frame delta
and, on reaching zero, releases the animation lock and returns the creature
to its idle pose. It is written from exactly one place: the dispatcher case
that corresponds to `SetParalyzed`.

**Timed AI packages** come from `FUN_10086b98(actor, package, duration)`:

```c
actor->aiPackage   = package;          // +0x2a8
actor->packageTimer = duration << 8;   // +0x300
if (package == 4 && actor->target) {   // flee only
    actor->hasMoveGoal  = 1;           // +0x1b9
    actor->goalX = (target->x - actor->x) * 0x14;   // +0x1bc
    actor->goalY = (target->y - actor->y) * 0x14;   // +0x1c0
    actor->target = 0;                 // +0x20c  -- forget the target
    actor->savedPackage = 2;           // +0x2fc  -- come back as idle
}
```

The tick counts `+0x300` down and, when it expires, restores `+0x2fc`.

**Package 4 (flee) has no per-tick behaviour** — the AI tick contains no
`package == 4` branch at all. Its entire implementation is the one-shot
move goal above plus the countdown. Note the goal expression
`(target - self) * 0x14` produces an absolute world position that is not
generally *away* from the threat; whether that is intended or an original
bug is not determinable from the code alone.

**Package 6 (spell-assist) is never read anywhere in the binary.** A
whole-program decompiled grep for `0x2a8) == 6` returns nothing, and a
creature left in package 6 matches neither branch of the tick's
`if (package == 3 ...) else if (package == 2 || ...)` structure — so it
simply stops acting. `AiSpellAssistTarget` is effectively a no-op in the
shipped game. (No script in the corpus calls it, or `AiFlee`, either — only
`AiDetect` and `AiSleep`.)

**`SetWimpy` does not drive fleeing.** `monster+0x2ae` has a getter and a
setter in the dispatcher and no other reference in the program; the AI
never reads it.

#### RESOLVED: the status-effect system

`FUN_100458e4` is the status-effect dispatcher, and it selects the effect
from **the spell entity's own `entities.txt` typeId** (`actor+0xc8`) — not
from `DoAttackRoll`'s second argument, which is a *magnitude*:

| typeId | script | branch |
|--------|--------|--------|
| 4009 | `spells\Absorb.s` | — |
| 4010 | `spells\Blind.s` | — |
| 4018 | `spells\Drain.s` | — |
| **4020** | **`spells\Fear.s`** | `FUN_10086b98(target, 4, magnitude * 5)` — the flee package |
| 4023 | `spells\HarmArmor.s` | — |
| 4024 | `spells\IgniteFoe.s` | — |
| **4025** | **`spells\Paralyze.s`** | arms the target's `+0x294` lockout |
| 4033 | `spells\Disease.s` | — |
| 4034 | `spells\Poison.s` | — |

This closes what `PORT_ROADMAP.md` had recorded as "no scripted table has
been found anywhere to decode `DoAttackRoll`'s `effectId` from": the table
is `entities.txt`, keyed by typeId, and the argument scripts pass is the
magnitude (real `Fear.s` passes `DoAttackRoll(target, 10)`, real `blaze.s`
passes `1`).

#### The status-effect primitives, decoded

`FUN_100458e4`'s branches are all built from two shared primitives on the
actor's stats block (`actor+0x224`).

**`FUN_1004aa28(stats, mode, statIndex, kind, delta, durationSeconds, extra, name)`
— a timed stat modifier.** Mode 1 allocates a 0x18-byte record
`{statIndex, kind, delta, expiry = now + duration * 0x100, extra, name}`
and links it onto the list at `stats+0x58`. **Strongest wins**: if that
stat already carries a modifier, the incoming one is rejected outright
unless its absolute delta is strictly greater, in which case the weaker
record is unlinked first — so there is never more than one modifier per
stat.

**`FUN_1004bae8(stats, flagBit, durationSeconds)` — a timed effect flag.**

```c
stats->effectFlags /* +0x44 */ |= flagBit;
stats->dotKind     /* +0x76 */  = (flagBit == 8) ? 3 : 0;
stats->effectTimer /* +0x72 */  = duration << 8;
stats->dotAccum    /* +0x74 */  = 0;
```

**The damage-over-time tick is `FUN_10049780`.** While `+0x72 > 0` and
`+0x76 > 0` it accumulates the frame delta into `+0x74` and, each time the
accumulator reaches 256, zeroes it and calls the stats vtable's
`DoDamage(+0x76)`. Note that `+0x76` is **both the effect kind and the
damage dealt** — it is passed straight through — so poison's `3` means
three points every 256 delta units, the same "one second" every other
engine timer counts in. (A second, independent periodic channel lives at
`+0x78`/`+0x7a`/`+0x7c`; `+0x7c == 7` regenerates health by `+0x34` and
`+0x7c == 8` drains it by `+0x76`. IgniteFoe uses that one.)

#### The stats block's own layout

Recovered in full by cross-referencing two independent switches:
`FUN_1004ad40`, which maps a *stat index* to a halfword (`kind` 1 adds to
the current value, 3 subtracts, anything else assigns), and the
character-stats dispatcher `FUN_10048244`, whose 85 named bindings read
and write those same halfwords by name. Where the two agree, the field is
named by the shipped engine itself, not inferred:

| offset | field | named by | | offset | field | named by |
|--------|-------|----------|-|--------|-------|----------|
| `+0x00` | attack | `SetAttack` / index 1 | | `+0x1a` | **willpower** | `GetWill`/`GetWil`/`GetWillpower` / index 0xc |
| `+0x02` | defense | `SetDefense` / index 2 | | `+0x1c` | speed | `GetSpeed` / index 0xd |
| `+0x04` | **spellcast** | `GetSpellcast` / index 3 | | `+0x1e` | endurance | `GetEndurance` / index 0xe |
| `+0x06` | **magic resistance** | `GetMagicResistance` / index 4 | | `+0x24`–`+0x28` | max health / fatigue / magicka | indices 0x11–0x13 |
| `+0x08`/`+0x0a` | damage min / max | `GetDamageMin`/`Max` / indices 5, 6 | | `+0x2a`–`+0x2e` | current health / fatigue / magicka | indices 0x14–0x16, each clamped to its max and to >= 0 |
| `+0x0c` | armor value | `SetArmorValue` / index 7 | | `+0x30` | experience (32-bit) | `GetExperience` / index 0x17 |
| `+0x0e` | exp worth | `GetExpWorth` / index 8 | | `+0x34` | **character level** | `GetLevel`/`SetLevel` / index 0x18 |
| `+0x14` | strength | `GetStrength` | | `+0x38` | gold (32-bit) | `GetGold`/`SetGold` / index 0x19 |
| `+0x16` | intelligence | `GetIntelligence` / index 0xa | | — | — | index 0x20 sets/clears effect flag bit 4 (blind) |
| `+0x18` | agility | `GetAgility` / index 0xb | | | | |

Note that indices 0xa–0xc and 0xe–0x10 — the attribute block — are the
only ones whose write path also calls `FUN_10049698`, i.e. a recompute of
whatever is derived from them. Strength (`+0x14`) has no index at all and
can only be set by name.

> **Correction.** `+0x34` was previously recorded here as "spell power".
> It is the **character level**: `FUN_10048244`'s `GetLevel` (binding
> index 0x37) reads exactly that halfword and `SetLevel` writes it. Every
> statement below that used to say "spell power" means the caster's level,
> and the 25 that caps it is the level cap.

#### The magic to-hit model

The dispatcher does not subtract resistance from spell damage. It resolves
resistance once, as a **hit chance in front of the whole effect** — damage
and status alike are applied inside that branch, so a resisted Poison
applies no poison at all rather than a weaker one:

```c
power  = casterStats ? GetSpellToHit(casterStats, caster) : 100;
resist = targetStats ? GetSpellResistance(targetStats, caster) : 0;
chance = power > 0 ? (power << 16) / ((power + resist) * 0x100) : 0;
if (chance == 0x100 || rand(0, 0x100) < chance) { /* everything */ }
```

The `chance == 0x100` short-circuit is load-bearing: an unresisting target
yields exactly 0x100, which the inclusive `rand(0, 0x100)` would otherwise
still miss 1 time in 257.

Both terms have a standalone helper *and* a script-callable binding that
computes the same thing inline — two independent transcriptions of each:

| | helper | binding | formula |
|---|---|---|---|
| caster | `FUN_1004bc60` | `GetSpellToHit` (index 3) | `spellcast + 2 × willpower` |
| target | `FUN_1004bbd0` | `GetSpellResistance` (index 4) | `magicResistance + willpower / 5` |

The `/5` is a `0x66666667` magic multiply with a 33-bit shift, not a
written division. Both helpers take a second argument (the caster entity)
and add a further enchantment term for an equipped item whose enchantment
type is 4 (to-hit) or 7 (resistance) — a bonus, not part of the base
formula.

**Every branch, in the engine's own parameters.** The dispatcher is really
two switches on the same typeId: one picks a `min`/`max` damage pair
(`max` first, then `min`, then `if (max <= min) max = min`; the actual
value is `rand(min, max)` **only when `min != max`**, and the `DoDamage`
call is guarded by `if (0 < max)`), and a second applies the status. Four
typeIds are in the first switch only, five in the second only, and two
(Absorb, IgniteFoe) are in both:

| effect | typeId | script | damage pair | status |
|--------|--------|--------|-------------|--------|
| Blaze | 50 | `blaze.s` | `3 .. magnitude*3+3` (`6` with no caster stats) | — |
| Blaze (greater) | 4006 | `blaze.s` | `45 .. 50` — the one branch with no magnitude term | — |
| DeadToDust | 4002 | `spells\DeadToDust.s` | `(magnitude+1)*2 .. (magnitude+1)*5` | — (returns immediately unless the target is undead) |
| DoomHammer | 4012, 4017 | `spells\DoomHammer.s` | `r = rand(1,10); magnitude*r + r`, fixed | — |
| DeathHowl | 4035 | `spells\DeathHowl.s` | `r = rand(1,10); r*8 + 1 + magnitude`, fixed | — |
| Absorb | 4009 | `spells\Absorb.s` | `magnitude + 12`, fixed | heals the caster by `damage * magnitude / 25 + 6` (`FUN_1004bb88`) |
| IgniteFoe | 4024 | `spells\IgniteFoe.s` | `(magnitude+1)*2 .. (magnitude+1)*5` | second periodic channel: `+0x7c = 8`, `+0x78 = magnitude << 9`, `+0x76 = 1` — 1 damage a second for `magnitude * 2`s |
| Blind | 4010 | `spells\Blind.s` | — | attack −10 **and** defense −10 for `magnitude + 5`s; plus effect flag 4, applied only when the target is not the player |
| Drain | 4018 | `spells\Drain.s` | — | attack −10 for `magnitude + 8`s |
| Fear | 4020 | `spells\Fear.s` | — | `FUN_10086b98(target, 4, magnitude * 5)` — the flee package |
| HarmArmor | 4023 | `spells\HarmArmor.s` | — | armor −`magnitude` for `magnitude + 8`s |
| Paralyze | 4025 | `spells\Paralyze.s` | — | arms the target's `+0x294` action lockout via `(magnitude + 4) * 0x100` |
| Disease | 4033 | `spells\Disease.s` | — | attack −2 (when `magnitude < 7`) else −3, **and** defense −3, both for a fixed 30s — the only effect whose duration ignores magnitude |
| Poison | 4034 | `spells\Poison.s` | — | effect flag 8, DoT kind 3 → 3 damage per 256 units for `magnitude`s |

The script column is `entities.txt`, which is also what makes the two
duplicate pairs visible: 50 and 4006 both point at `blaze.s`, and 4012 and
4017 both at `DoomHammer.s`, so the typeId — not the script — is what
selects the branch. (4012 draws its `r` twice and throws the first away;
4017 draws once. Same distribution.)

**DeadToDust's target test.** `FUN_10086f88(target)` is `monster+0x2d0 ==
2`, and `monster+0x2d0` is a single creature-kind field: `SetSpider`
writes 1, `SetUndead` writes 2, and the `IsUndead` binding is literally
`field == 2`.

#### `magnitude` is the caster's level

It is **not** the script's argument — the dispatcher never looks at what
`DoAttackRoll` was passed. The proof is in the scripts: real `Poison.s` and
`Disease.s` call `DoAttackRoll(target)` with no second argument at all, yet
both apply fully-parameterised effects.

What it *is*:

```c
if (!spell->scroll /* +0x1d4 */ && (caster == null || casterIsCharacter))
     magnitude = casterStats->level;   // stats+0x34
else magnitude = spell->level;         // spell+0x1d0, the Spell SetLevel
if (0x18 < magnitude) magnitude = 0x19;   // clamped to the level cap, 25
```

The scroll fallback is corroborated straight out of the shipped corpus:
the *only* spell scripts that call the Spell class's own `SetLevel` are
the scroll and "unique" wrappers — `spells\IgniteScroll.s` (`SetLevel(8)`),
`spells\U_Blaze_lvl5.s`, `U_Blaze_lvl10.s`, `U_DoomHammer_lvl10.s`,
`U_Frenzy_lvl10.s`, `U_Heal_Wound_lvl10.s`, `U_Ignite_Foe_8_lvl8.s` — each
a three-line `SetLevel(N); SetSpellType(typeId); RunScript(realSpell)`
wrapper whose level matches its own filename. No plain `spells\*.s` calls
it. A found scroll casts at the scroll's power; a memorised spell casts at
yours.

> **`SetRating` is not this.** Every plain spell script calls
> `SetRating(N)`, and the values across the whole `spells/` directory are a
> dense near-alphabetical run with duplicates — absorb 1, blind 3,
> bodytomind 5, disease 5, curedisease 6, curepoison 7, daedricweapon 8,
> poison 8, deadtodust 9, … azrawrath 27, azrasustenance 28 — and are
> absent from the scroll/unique variants entirely. It is a per-spell
> ordinal, not a power.

#### The two damage-carrying branches: Absorb and IgniteFoe

The four typeIds above them in the table are damage-*only*, and the five
below are status-only: for those, the shared damage pair (`min`/`max`,
local `local_250`/`iVar11`) stays at 0/0, so the `if (0 < max)` that guards
the `DoDamage` call never fires and only the effect lands. Absorb and
IgniteFoe are the two branches that appear in both switches — they deal
damage *and* do something further with it.

**Absorb's heal.** After the damage call, the caster's stats block is
handed to `FUN_1004bb88` — a three-instruction `SetHealth` that clamps
into `[0, maxHealth]`, so absorbing at full health does nothing:

```c
casterHealth + ((damage * 0x100 * ((magnitude << 16) / 6400)) >> 16) + 6
```

That divisor is not written as a divisor. The binary has a 64-bit multiply
by `DAT_10045e54` followed by `>> 0x2b`, and `DAT_10045e54` reads
`0x51EB851F` — the standard magic constant for signed division by 25,
whose own shift is 3, plus 8 more for the 256. So the divisor is
`25 * 256 = 6400`, and 25 is exactly the cap applied to `magnitude` at the
top of the function. The whole expression collapses to:

> **heal = damage × magnitude / 25 + 6**

i.e. absorb a share of the damage proportional to how far the caster's
**level** has come toward the level cap, plus a flat 6. At the cap the
caster recovers the full hit and then some.

**IgniteFoe and the second periodic channel.** `FUN_10049780` runs *two*
independent periodic channels, not one. The first (`+0x72`/`+0x74`/`+0x76`)
is the effect-flag channel poison uses. The second
(`+0x78`/`+0x7a`/`+0x7c`) has its own timer, its own accumulator and a
`kind` selector, and the tick implements three kinds:

| kind | per second |
|------|-----------|
| 6 | `+0x2c` (current magicka) += `+0x34` (the caster's level) |
| 7 | current health += `+0x34`, clamped to max and to >= 0 |
| 8 | current health −= **`+0x76`**, and on reaching 0 calls the actor's kill vtable slot (`+0x28`) directly |

IgniteFoe is the *only* site in the whole status-effect dispatcher that
arms this channel, and it arms kind 8. Kinds 6 and 7 are regeneration and
are set somewhere else (not yet found).

Two details worth keeping:

- **Kind 8 writes health directly** rather than going through `DoDamage`
  the way poison does, so a burn is reduced by nothing — no armour, no
  resistance. It is the flat 1/second it says it is, and it kills through
  the kill slot rather than the damage path.
- **`+0x76` is shared between the two channels.** Poison writes 3 there as
  its kind-and-damage; IgniteFoe writes 1 there as its per-tick damage.
  They are the same short. So poisoning a burning creature really does
  make its flames tick for 3 instead of 1. Relatedly, channel 1's own
  expiry is not a clear-to-zero — it assigns `flags = 2` and
  **`+0x76 = 3`** — so a poison *wearing off* while a creature burns also
  triples the burn. Both are reproduced in the port.

**The flame itself.** The branch also calls
`FUN_10067f84(target, 0x3e, 0x44, magnitude * 2)`, which allocates a
0x170-byte emitter, copies the target's own `x`/`y`/`z` (`+0x94`/`+0x9c`/
`+0xa4`), stores sprite range 62..68, registers it with the level, and
gives it a lifetime of `magnitude * 2 << 8` — exactly the burn duration,
from a completely independent write. That is the corroboration for the
duration reading.

**The resistance gate.** See "The magic to-hit model" above for the full
treatment — it sits above every branch here, wrapping the entire effect
(status included, not just damage) in a single hit roll. `casterPower`
comes from `FUN_1004bc60`, defaulting to 100 when there is no caster stats
block, and `targetResistance` from `FUN_1004bbd0`. Implemented in the
port as of M37, replacing the flat resistance-subtraction model it used
before, which had no basis in the decompile.

---

## Creature casting, `SetMob`, and the melee model (M43)

The port's last implementable roadmap item was "monsters as spell casters".
Chasing it turned into the largest single correction pass so far, because
the answer was not one function but a chain of five — and because two of
them overturn readings recorded above.

### `FUN_1002fd30`: the two vtable predicates, finally pinned

Everything else here follows from three lines:

```c
int FUN_1002fd30(actor) {
    if (actor->vtable[0xe4]())       return actor + 0x224;   // a monster
    else if (actor->vtable[0xcc]())  return actor + 0x3ac;   // the player
    else                             return 0;
}
```

So **`vtable+0xe4` is "is a monster"** and **`vtable+0xcc` is "is the
player"**, and a monster and the player own *the same stats-block layout*
at two different offsets. The status-effect dispatcher's first two lines
call this on the spell's owner and on the target, which is what makes the
whole system symmetric.

Three branches read the wrong way round until this was pinned:

| branch | real guard | what it means |
|---|---|---|
| **Blind** (4010) | `if (!target->vtable[0xe4]()) applyFlag(4)` | the blindness *flag* lands on the **player only**; a blinded creature takes the two -10 stat penalties and nothing more |
| **Fear** (4020) | `if (!target->vtable[0xe4]()) return 1` | fear works on a **creature only** — the player has no AI package to flee with |
| **DeadToDust** (4002) | `if (target->vtable[0xe4]() && !isUndead(target)) return 1` | a *living creature* is immune; the player is not |

### The caster is the spell's owner, not the player

`spell+0x170` is the spell's **owner**, and the magnitude rule reads:

```c
if (!spell->scroll && (owner == 0 || owner->vtable[0xcc]()))
     magnitude = casterStats->level;   // stats+0x34 -- the player's level
else magnitude = spell->level;         // spell+0x1d0 -- the Spell's SetLevel
```

With `0xcc` pinned, the second arm is **every creature-cast spell**. That
is why all 32 shipped caster scripts pair each `AddSpell` with an explicit
`Item.SetLevel(n)`, and why those levels differ from the creature's own
`SetLevel` (`monsters/tunnel_wight.s` is level 9 and gives its Absorb 7).

### `AddSpell` / `SetMeleeRoll` — the creature spell table

Dispatcher `0x10084924` case 8 (and the identical standalone
`FUN_10086ab0`) fills a fixed **four-slot** table on the monster:

```c
struct { u8 chance; Spell* spell; } slots[4];   // +0x30c, 8 bytes apart
int  blindSlot;                                 // +0x32c, 0xff for none
```

- The slot search takes the first slot that is empty **or already holds the
  same spell typeId**, so re-adding replaces; a fifth distinct spell is
  dropped with a debug log.
- `AddSpell(Item)` stores **100**; `AddSpell(Item, n)` stores `n`.
- `chance` is a **cumulative** threshold over `rand(0, 100)`, matching the
  scripts' own comments: `tunnel_wight.s` adds three at 30 / 70 / 100 and
  annotates them "30%", "40% of the time", "30%".
- A Blind spell (typeId 4010) has its slot index cached in `+0x32c`.

**`FUN_1008457c` — the chooser.** Walks the table until a slot's threshold
reaches the roll, returning nothing if it runs off the end. Ahead of that
sits one special case: a creature that knows Blind and whose current target
already carries effect flag 4 skips the roll entirely and casts its first
non-Blind spell instead, rather than wasting the turn.

**`FUN_100835b8` — melee or spell.** `SetMeleeRoll` writes `monster+0x304`,
and the branch is:

```c
if (slots[0].spell == 0 || (meleeRoll != 0 && meleeRoll <= rand(0,100)))
     ... melee ...
else ... cast ...
```

Note the direction, which the name does not suggest: a creature melees only
when the roll **reaches** the value, so a **higher** `SetMeleeRoll` means
**less** melee, and the default of 0 means *never* melee. That default is
load-bearing — `bandit_mage.s`, `highwaymage.s` and `yelnicin.s` call
`AddSpell` and never `SetMeleeRoll`, and so cast on every single attack.

`FUN_10046680`'s per-spell cooldown only applies when the owner is the
player (`vtable+0xcc`), so creature spells are never gated by it.

`SetPlaySpellCasting` writes `monster+0x2bd`, which the cast branch reads
to decide whether to play sound id 6. Every shipped script that sets it is
a caster.

### `SetMob` is a stat template, and it is what makes creature casting work

An earlier pass read `SetMob`'s first line (`monster+0x2ef = 1`, the
zone-XP opt-in) and stopped there. The rest of dispatcher case 0 is a
nested `switch (tier)` that **overwrites most of what the script just set
by hand**, scaled by the zone the creature is standing in:

```
L = ZoneDifficulty(<current level name>) + <optional 2nd argument>
H = L >> 1
```

`ZoneDifficulty` is `FUN_1008467c`, a flat `strcasecmp` chain over 21 zone
names returning 1..21 — in the game's own progression order, which is
itself a useful artifact:

> azra 1, delfhide 2, erthcave 3, ghstpass 4, twilite 5, snowline 6,
> fearfrst 7, ffarena 8, drgnfld 9, raiders 10, dstar_w 11, dstar_e 12,
> LothCav 13, lakvan 14, broken1 15, broken2 16, stouttp 17,
> GlacierCrawl 18, crypt1 19, crypt2 20, crypt3 21

Anything unlisted is 1. The four templates:

| field | tier 1 | tier 2 | tier 3 | tier 4 |
|---|---|---|---|---|
| attack (`+0x00`) | `H*8+8` | `1` | `H*6+6` | `H*8+8` |
| defense (`+0x02`) | `L*2+60` | `H*2+40` | `L*2+50` | `L*2+60` |
| magic resistance (`+0x06`) | `L*3+20` | `H*2+20` | `L*3+20` | `L*2+20` |
| damage min (`+0x08`) | `H*3+6` | `1` | `H*2+5` | `H*3+6` |
| damage max (`+0x0a`) | `H*4+8` | `1` | `H*3+6` | `H*4+8` |
| armour (`+0x0c`) | `1` | `1` | `1` | `1` |
| **willpower (`+0x1a`)** | `1` | `H*3+15` | `(L/3)*4+15` | `1` |
| max health (`+0x24`) | `L*10+20` | `L*7+15` | `L*8+15` | `L*8+15` |

followed by `SetHealth(maxHealth)`. Tier 2 is the caster tier — attack and
both damage bounds pinned to 1, and by far the largest willpower term.

**Why this is a prerequisite for creature casting.** The magic to-hit gate
is `spellcast + 2 * willpower`, and the chance is 0 whenever that is
`<= 0`. **21 of the 32 shipped caster scripts call `SetSpellcast(0)`** — a
creature with no willpower could never land a spell. All 21 call `SetMob`;
all 11 that do not call `SetMob` set a real `SetSpellcast` instead. The
split is exact, with no exceptions in either direction across the whole
corpus, and it is what pins the reading.

The consequence for the rest of this document: **a creature's script
literals are mostly dead weight**. `monsters/Azra_Rat.s` says
`SetAttack(3) SetDefense(4) SetDamageMin(3) SetDamageMax(6)
SetArmorValue(2) SetMaxHealth(12)`, then ends with `SetMob(4)` — so in azra
it actually fights with attack 8, defense 62, damage 6..8, armour 1 and 23
hit points.

### The melee model, recovered

That last fact is what forced the next one. `combat.h` had carried a
from-scratch `clamp(50 + (attack - defense) * 5, 10, 95)` since the combat
slice, on the documented grounds that no ground truth existed. Feeding a
real defense of 62 into a linear formula produces nonsense, so the real one
had to be found. It is two functions.

**`FUN_1004b620` — the to-hit gate:**

```c
total  = attack + defense;  if (total == 0) total = 1;
chance = (total == attack) ? 0x80 : (attack << 16) / (total << 8);
hit    = (rand % 0x100) <= chance;
```

i.e. `attack * 256 / (attack + defense)` — the same ratio model as the
magic gate. The zero-defense case is the interesting one and is the
**opposite** of the magic gate's: where a zero-resistance target is a
guaranteed magical hit (`chance == 0x100`), a zero-defense target is capped
at `0x80`, half. The division would have produced exactly `0x100` there, so
that is a deliberate substitution rather than an overflow guard.

A successful roll is followed by a second dodge/block test through the
defender's stats vtable (`+0x14`), which is not decompiled.

**The damage tail of `FUN_100835b8`:**

```c
span   = damageMax - damageMin;  if (span < 1) span = 1;
damage = damageMin + rand % span - defenderArmorRating;
if (damage > 0) DoDamage(damage, attackerStats, 0, 0);
```

The spread is **exclusive** at the top — a 3..6 weapon rolls 3, 4 or 5 —
and the defender's **full** armour rating is subtracted, with a
fully-absorbed hit dealing literally nothing rather than a courtesy 1.

`FUN_10047f64` and `FUN_1004801c` are `GetAttack`/`GetDefense`: base
`stats+0x00` / `stats+0x02` plus a bonus derived from `+0x14` / `+0x18` and,
for the player only, a class perk.

### Two more status branches

Asking which spells *creatures* cast turned up the dispatcher's last two
unimplemented arms, both absent from the player's own spell list:

| typeId | script | branch | cast by |
|---|---|---|---|
| 4008 | `spells\Weakness.s` | -10 attack for `magnitude + 8` | `tunnel_wight.s` only |
| 4021 | `spells\FeebleBlade.s` | -10 attack for `magnitude + 5` | `highwaymage.s`, `highwaymage_cskye.s`, `shadow_tentacle.s` |

Weakness is byte-for-byte identical to Drain (4018).

### Correction: Paralyze's duration

The Paralyze branch is `(**(stats+0x80 vtable + 0x44))(stats, (magnitude
+ 4) * 0x100)` — **`magnitude + 4` seconds**. The port had `magnitude * 5`,
which its own comment admitted was a shape rather than a recovered value.
At the shipped creature levels that is the difference between a raider's
Paralyze (`SetLevel(5)`) locking the player for 9 seconds and for 25.

### `FUN_10046764` — what a real cast does

Not reproduced in the port (there is no projectile system here, and the
player's own casting already takes the same shortcut), but recorded: the
real cast deducts magicka against the caster's stats, applies any
self-targeted half of the spell inline — heals, cures, buffs, the
regeneration kinds 6/7 of the second periodic channel — and for the
offensive spells allocates a 0x198-byte projectile whose impact is what
eventually calls the status dispatcher. This port substitutes a direct
`HitTarget()` call on both sides instead.

---

## Entering a named region, and the zone effects (M44)

The regions themselves are `.zon`'s room records — see
[`ZONE_FORMAT.md`](ZONE_FORMAT.md). This is what the engine does when the
player walks into one.

### `FUN_1002ef44(level, room)` — the entry handler

```c
uint OnEnterRoom(level, room) {
    name = room + 0x40;
    // 1. an encounter that names this region takes it, and stops here
    for (node = *(level + 0x44c); node; node = node[1], ++index) {
        if (FUN_1008af9c(node[0], name) >= 0) {
            FUN_1008a76c(node[0], room, index);   // spawn
            return 1;
        }
    }
    // 2. otherwise the level script's own handler
    result = FUN_10070534(level, room);           // EnterZone(name)
    // 3. then a trigger walk -- see below
    for (node = *(level + 0x420); node; node = node[1]) { ... }
    return result & 0xff;
}
```

**Correction: `level+0x44c` is the encounter list, not the region list.**
An earlier note had it as the source of the region names, which is why the
search for them went to `.ent`. It holds registered `Encounter` objects,
each of which *names* regions; `FUN_1008af9c` asks whether one is bound to
this room and returns its index.

`FUN_10070534` is where the binary's UTF-16 `"EnterZone"` literal is used:
it wraps the name in a one-element SimKin argument array and calls the
level script through the standard scripted-call vtable slot. It also
strcmp's the room name against two engine-hardcoded constants first —
`"Minefield"`, which calls `FUN_100730f0` (an **empty function** in the
shipped build), and `"FinalEscape"`, whose strcasecmp result is
**discarded**. Neither name appears in any shipped `.zon`. Both are dead.

**Step 3 selects triggers it cannot fire.** The walk picks triggers with
flag `0x08` whose name matches the region, then calls
`FUN_10090818(trigger, 1, *(engine+0x618))` — mode 1, with the **world
object** as the notifying entity (`ldr r3,[r10,#0x34]; ldr r2,[r3,#0x618];
movne r1,#1`). Inside, mode 1 requires `entity == trigger->doorObject`
(SetDoor's own argument) and the trap branch requires the entity to match
the trigger's AddEntity list. The world object is neither, for any trigger
any shipped script builds. So the region-entry trigger notification is
effectively dead code, and the port does not reproduce it.

### `LockZone` / `UnlockZone`

Covered in [`ZONE_FORMAT.md`](ZONE_FORMAT.md) — a named region's tiles get
their cell's second byte written, bit 2 meaning "blocked". 30-odd real call
sites, all `UnlockZone("swdoor")`-shaped, after a key or a lever.

### The zone-effects dispatcher (`FUN_1002f074`)

Eight bindings, of which the shipped scripts use **two**:

| index | binding | call sites | what it does |
|---|---|---|---|
| 0 | `SetZone` | 20 | the zone XP budget (see M39) |
| 1 | `LightRect` | 8 | `for (y = y0; y < y1) for (x = x0; x < x1) tile(x,y)->light = level << 8` — half-open, and **not** Y-flipped, unlike `.zon`'s own rectangles. Every call site passes 64. |
| 2 | `Vignette` | 6 | a full-screen slideshow, below |
| 3 | `SpawnWithinRadius` | **0** | — |
| 4 | `AddEncounters` | several | the encounter factory (M38) |
| 5 | `TickZones` | **0** | — |
| 6 | `AddInterestPoint` | **0** | — |
| 7 | `ClearInterestPoints` | **0** | — |

Four of the eight have **zero call sites anywhere in the shipped script
corpus**, so there is nothing to be faithful to and no way to check an
implementation. Recorded rather than guessed at.

### `Vignette(n)` is a screen mode, not an effect

`FUN_1002bc6c` stops the player dead (zeroing `+0x98`/`+0xa0` on the world
object), preloads a contiguous run of sprite slots, and arms a fade to
screen mode **0x20**. Its per-frame tick `FUN_1002bdf4` then advances one
screen mode every `0x700` engine time units (7 seconds on the 1/256s
clock), drawing sprite `firstSprite + (mode - 0x20)` at (0xe, 5) with
caption string `firstTextId + (mode - 0x20)` under it at y=0x73. Any key
skips straight to mode 5.

The six that ship, from the switch, verbatim:

| n | sprites | first caption id | used by |
|---|---|---|---|
| 0 | 0xe0..0xe3 | 0xf39 | `delfhide.s` |
| 1 | 0xdc..0xdf | 0xf49 | `crypt1.s` |
| 2 | 0xe4..0xe7 | 0xf41 | `drgnfld.s` |
| 3 | 0xe8..0xeb | 0xf3d | `ghstpass.s` |
| 4 | 0xec..0xef | 0xf45 | `glaciercrawl.s` |
| 5 | 0xf0..0xf4 | 700 | `azra.s` |

Case 5 is the only five-slide one, and the only one whose caption base is
not in the 0xf3x/0xf4x run.

Every one of the six is called from an `EnterZone` handler, and four of
them (`crypt1`, `delfhide`, `ghstpass`, `glaciercrawl`) from a region
named `vig` that contains the zone's own spawn tile — i.e. they are
arrival cutscenes.

### `FUN_1001b788` is the fade-arming function

This is the "whoever arms that fade" M42's screen-mode pass was looking
for:

```c
void ArmFadeToMode(engine, mode, flag) {
    engine[0x5a8] = 0x100;   // the fade counter FUN_1004fc50 counts down
    engine[0x5ac] = mode;    // the deferred SetScreenMode target
    engine[0x5b0] = flag;
}
```

Its complete call-site set, across the whole binary: literal `1`, `5`,
`0xc`, `0x20`, plus two computed runs — the attract-mode slideshow's
`mode + 1` wrapping at `0x10` back to `1`, and the vignette's `mode + 1`
running from `0x20` to `0x20 + slides - 1` and then falling to `5`.

**0x1f appears in none of them**, so `FUN_1002c010`'s fourth gating mode
stays unidentified — but the search space is now closed on this side: it
is not any fade target. Worth noting that the vignette run starts at
`0x20`, one above it.

---

## The encounter spawner (M45)

The last piece of the Zone/Level bullet. M38 recovered the Encounter
object model, M44 gave its regions real rectangles; this is what happens
between "the player entered a region an encounter claims" and "a creature
is standing there".

### The object layout

`AddEncounters` allocates a 0x58-byte encounter (`FUN_1008b074`) and
appends it to `level+0x44c`; each argument becomes a 0x18-byte region
record (`FUN_1008b000`) on the encounter's own list at `+0x0c`.

```c
struct Encounter {              // 0x58 bytes
    /* 0x0c */ List<Region> regions;      // one per AddEncounters argument
    /* 0x14 */ int   regionCount;
    /* 0x18 */ List<Set> sets;            // one per AddRandomSets call
    /* 0x20 */ int   setCount;
    /* 0x24 */ Level* level;
    /* 0x28 */ bool  active;              // SetActive, defaults to 1
    /* 0x2c */ int   respawnSeconds;      // SetRespawnSeconds, defaults to 0
    /* 0x30 */ int   respawnTimer;
};

struct Region {                 // 0x18 bytes
    /* 0x08 */ wchar name[];              // matched against a .zon room name
    /* 0x14 */ int16 live;                // creatures alive from this region
    /* 0x16 */ int16 limit;               // SetLimit, defaults to 99
};

struct Set    { /* 0x08 */ List<Entry> entries; /* 0x10 */ int entryCount; };
struct Entry  { /* 0x08 */ int16 typeId;        /* 0x0a */ int16 count; };
```

### `FUN_1008a76c(encounter, room, regionIndex)` — the spawn

```c
region = encounter->regions[regionIndex];           // bounds-checked; -1 -> region 0

if (!((region->live < 1 || encounter->respawnSeconds != 0)
      && encounter->active
      && region->live <= region->limit - 1)) return;

setIndex = (encounter->setCount == 1) ? 0 : rand() % encounter->setCount;
for (entry : encounter->sets[setIndex]) {
    for (n = 0; n < entry->count; ++n) {
        actor = CreateEntityByTypeId(level, entry->typeId);   // FUN_100715a8
        if (!actor) continue;
        actor->encounterRegion /* +0x2e4 */ = region;
        region->live++;
        cell = FindSpawnPosition(encounter, room, actor, &x, &y);   // FUN_1008ae94
        if (cell) { actor->SetPosition(x, y, z); SnapToFloor(actor, cell); }
        if (region->live == region->limit) return;
    }
}
```

Two things worth keeping. The live count is incremented **before** the
placement search, so a creature that cannot be placed still consumes a
slot. And the first clause of the gate means that with `respawnSeconds` at
its default 0 — which is every encounter in the game — a region will not
top itself up while anything it spawned is still alive.

`actor+0x2e4` is the backlink that makes that work: the death routine
(`FUN_10083c04`) calls `FUN_1008b118(actor->encounterRegion)`, which is a
one-line `region->live--`.

### `FUN_1008ae94` — the placement search

```c
attempts = x1 - x0;                        // the region's *width*
for (i = 0; i < attempts; ++i) {
    tx = rand(x0, x1);  ty = rand(y0, y1); // inclusive, FUN_100730c8
    if (occupancyGrid[ty * width + tx] == 0) return tileAt(tx, ty);
}
return 0;
```

The retry budget is the rectangle's width and has nothing to do with its
area, so a long thin region gets very few tries. The draw is inclusive at
both ends, which lines up with the inclusive containment M44 established
from a completely different direction.

`engine+0x6904` is a **per-tile array of 4-byte entity pointers**,
separate from the 8-byte cell array at `engine+0x6908`; a tile is free
when its pointer is null. This is the first use found for that array.

### `FUN_1008ae00` — the respawn tick, which never runs

```c
if (encounter->active && (encounter->respawnTimer += delta) > encounter->respawnSeconds) {
    encounter->respawnTimer = 0;
    for (index = 0, region : encounter->regions) {
        room = LookupRoomByName(level, region->name);
        if (room) SpawnEncounter(encounter, room, index);
    }
}
```

Called from the level tick, but only when `level+0x459` is set — and that
flag is written by exactly one thing, the `TickZones(bool)` binding, which
**no shipped script calls**. So the periodic top-up is dead in the shipped
game.

Recorded for whoever wires it up: `delta` is the engine's per-frame delta,
256 units to the second, while `SetRespawnSeconds(n)` stores `n` **raw**.
So `SetRespawnSeconds(60)` would fire after 60/256 of a second rather than
60 seconds. Since neither binding is ever called the mismatch is
unobservable, and it is not clear which side is the bug.

### What the shipped game actually does with any of this

Three `AddEncounters` call sites exist in the entire corpus.

| script | encounter | regions | in the zone's `.zon`? |
|---|---|---|---|
| `ghstpass.s` | `fight12` | `fight12`, `fight12W`, `fight12S` | **none of them** |
| `ghstpass.s` | `fight13` | `fight13`, `fight13C`, `fight13W` | **none of them** |
| `lothcav.s` | `fight41` | `battle41` | yes |

`ghstpass.zon`'s regions are `witchtree`, `wolves`, `snowline`,
`Twilight`, `BrokenWing`, `Azra`, `tosnowline`, `Stouts`, `vig`,
`startOutTwilite`, `start` — so both of ghstpass's encounters, and the ten
`AddRandomSets` calls behind them, can never resolve a region and are
dead.

That leaves one, and it is a **quest reward rather than an ambush**:

```
lothcav.s:
    fight41 = AddEncounters("battle41");
    fight41.AddRandomSets(272, 1);        // monsters\Tunnel_Wight.s x1
    fight41.SetActive(false);
    fight41.SetLimit(0,1);

lothna/pilgrim_remains.s, the "CreateWight" menu option:
    Level.fight41.SetActive(true);
    Level.fight41.SpawnEncounter("battle41");
```

Built dormant, capped at one creature, and switched on only when the
player uses the pilgrim's remains — once, behind a `saved_madewight`
flag. One Tunnel Wight, one time, in the whole game. Until then, walking
into `battle41` *does* reach the encounter (the region walk matches by
name, not by state) and the gate refuses — which also means `EnterZone`
never fires for that region, since the encounter claimed it.

---

## The attached weapon (M46)

`SetAttachedWeapon` was the roadmap's last "curiosity" item, carried with
two claims attached. Both were wrong. M39 read the field as
`monster+0x304`; M43 showed that offset belongs to `SetMeleeRoll`, which
left the field unknown again. And the entry said no shipped script calls
it — the corpus has **92 calls across 86 creature scripts**.

### The field

Monster binding index 12, dispatcher case `0xc`:

```c
case 0xc:                                        // SetAttachedWeapon(n)
    monster->attachedWeapon /* +0x2c2 */ = AtomToInt(args[0]);
```

A **signed 16-bit `models.idx` archive index**, initialised to `-1` by the
constructor (`FUN_100815e0`: `*(u16*)(this + 0x2c2) = 0xffff`). `-1` rather
than `0` matters, because `0` is a valid archive slot.

Why it stayed unknown so long is worth recording: every consumer loads it
with `LDRSH`, and `pyghidra_find_reads.py` only matches `LDR` with an
immediate offset. That is the same blind spot that produced M39's wrong
answer. Grepping the *decompiled* corpus instead
(`pyghidra_grep_decompiled.py 0x2c2`) finds exactly five sites in 2006
functions: the constructor, the dispatcher's write, and three consumers.

### Consumer 1 — `FUN_10083490`, the monster's render override

```c
void MonsterActor::Render(actor) {
    if (actor->attachedWeapon != -1) {
        model = engine->modelCache[0x6b38][actor->attachedWeapon];
        if (model) {
            t = Transform();                        // FUN_1006807c
            t.pos    = actor->+0x94, +0x9c;
            t.orient = actor->+0xa4, +0xa8, +0xb2, +0xb6;
            t.scale  = actor->+0x5e;                // SetScale
            t.anim   = actor->+0x64 .. +0x80;       // incl. frame index +0x70
            Actor3D_TransformAndSubmitModel(engine, t, actor->+0x2d4, 0, -1);
        }
    }
    thunk_FUN_10064ffc(actor);                      // then draw the body
}
```

A second whole model, welded to the body, drawn *before* the ordinary
actor draw and sharing the actor's position, orientation, scale and
**absolute animation frame**.

Copying the whole animation block instead of applying a bone offset is not
a shortcut — the weapon models are **frame-aligned exports of the humanoid
rig**. Checked against the real archive:

| slot | file | frames | clips | skins |
|---|---|---|---|---|
| 20–23 | `female_long_tunic` / `female_short_tunic` / `male_long_tunic` / `male_short_tunic` | 144 | 11 | 8 / 16 / 19 / 13 |
| 53 | `delfran.bin` | 144 | 11 | 4 |
| 222–226 | `sword` / `mace` / `dagger` / `bow` / `ax` | 144 | 11 | 1 |
| 18 | `rat.bin` (for contrast) | 57 | 4 | 4 |

The body models' large skin counts are what `SetSkin()` picks from — one
rig dressed as a guard, a bandit, a soldier. The weapons have exactly one
skin each, which is why the render override never needs to carry the
body's skin across.

All five weapons carry a **byte-identical clip table** to the humanoid
bodies — `(0,1,10) (1,22,10) (22,31,10) (31,42,10) (42,66,10) (66,98,10)
(98,106,10) (106,116,3) (116,127,3) (127,138,10) (138,144,10)` — and those
eleven models (plus one duplicate dagger at slot 25) are the *only*
144-frame entries in the 226-entry archive. The hand's motion is baked
into the weapon's own vertices; handing it the body's frame index is the
entire attachment mechanism.

This is a *different* mechanism from `FUN_10065f7c`, the rotation-matrix
attachment path `RENDERER_3D.md` describes. That one computes a child
transform from a parent's; this one does not.

`actor+0x2d4`, passed through as the submit call's third argument, is
**not** a skin index — it is the actor's slot in a registry at
`engine+0x14620` (`registry[slot] = actor`, written by `FUN_100866c0` /
`FUN_10086f9c` and cleared by the death routine). The body's own draw
passes the same value; only the mode byte differs (`0` for the weapon, `2`
for the body).

### Consumer 2 — `FUN_10082224`, the AI's ranged test

The bow's archive index is **hardcoded in the AI**:

```c
bool ranged = (stats->equipped /* +0x48 */ && stats->equipped->isRanged /* +0x1a7 */)
           || actor->spellSlot0 /* +0x310 */ != 0
           || actor->attachedWeapon /* +0x2c2 */ == 225;      // bow.bin
```

A ranged creature skips the facing-cone test (`|yawDelta| < 0x200`) and
skips the melee reach/LOS raycast (`FUN_10082004`) entirely — it attacks
anything inside `SetAttackRange` outright. A melee creature must pass the
raycast, retried once at a fixed reach of `0xc` if the first attempt fails
without setting flag bit 1.

The corpus corroborates the `== 225` clause exactly. The five values any
script ever passes are:

| value | model | calls | who passes it |
|---|---|---|---|
| 222 | `sword.bin` | 29 | guards, soldiers, swordsmen |
| 223 | `mace.bin` | 6 | brawlers |
| 224 | `dagger.bin` | 10 | thugs, named NPCs |
| 225 | `bow.bin` | 27 | **every archer, and nothing else** |
| 226 | `ax.bin` | 20 | raiders, bandits |

All 24 distinct scripts passing 225 are archers (`archer_guard`,
`elite_bowman`, `deadeye`, `arrow_shade`, `ace_archer`, `icebowman`, …),
and no script passing any other value is. No shipped script both attaches
a weapon and calls `AddSpell`, so in practice the two implemented clauses
of the test are disjoint.

The five indices hold the same five models in **all 21 real zones'**
`<zone>_models.txt`; only `menu_models.txt` (the main-menu pseudo-zone)
leaves them NULL.

### Consumer 3 — `FUN_1003c1c0`, multiplayer replication

```c
actor = LookupEntityById(engine->entityRegistry /* +0x5cc */, msg->entityId /* +4 */);
if (!actor) { sprintf(scratch, "..."); User::After(10000); }
else actor->attachedWeapon = msg->weaponModel /* +6 */;
```

Not called directly from anywhere — its address appears once in the
binary, as one slot of a **48-entry handler table at `0x100fdf34`** whose
every entry lies in the `0x10038000`–`0x1003c000` networking block. So the
attached weapon is replicated over the wire, alongside the same block's
`engine+0x5c0` ("session active") and `engine+0x5d0` (outbound sender)
that `ReplicateTeleport` and the attack path also gate on. The `else`
branch is a debug leftover: it formats a message into a discarded stack
buffer and sleeps 10 ms without retrying.

---

## The weapon swing (M47)

M25 decompiled the first-person viewmodel's draw function in full but
could not find what starts a swing, and recorded that as an exhaustive
search that came up empty: "the exact native call that writes
`+0x230`/`+0x234`/`+0x238` wasn't found despite tracing every reachable
write site from the Weapon class dispatcher."

The search was exhaustive in the wrong direction. Grepping the decompiled
corpus for the *write text* — `pyghidra_grep_decompiled.py "0x234) ="` and
friends, the same technique that cracked M46 — finds every writer of all
five fields in one pass over 2006 functions. The state lives on the
**player object** (`engine+0x618`), not in a separate view struct, which is
why walking outward from the Weapon dispatcher never reached it.

### The fields

| field | meaning |
|---|---|
| `player+0x204` | the active weapon `Item*` |
| `player+0x22c` | its `SetWeaponSprite()` slot, cached |
| `player+0x230` | the **previous** weapon's sprite slot |
| `player+0x234` | the swing accumulator; counts down, non-zero == swinging |
| `player+0x238` | the weapon-**swap** transition timer |
| `player+0x244` | walk-bob phase |
| `player+0xf48` | the player's attack cadence |
| `item+0x17f` | swing variant, re-rolled per swing: 0, 5 or 10 |
| `item+0x180` | `SetAnimationFrames` |
| `item+0x184` | `SetReloadSpeed`, an 8.8 scale on the decay rate |
| `item+0x19c` | `SetWeaponSprite` |
| `item+0x179` | `SetThrowingWeapon` |
| `item+0x17a` | `SetBow` — and `SetCrossbow`, one field for both |
| `item+0x1a0` / `+0x1a7` | `SetRange`, and "uses the ranged attack path" = `range > 0x400` |

**Two of M25's readings were wrong.** `+0x230` is not an "alternate/swing
sprite" and `+0x238` is not a "post-swing hold": they are the previous
weapon's sprite and a weapon-swap timer. A swing has no hold phase at all —
it ends when its accumulator runs out.

### `FUN_100425bc(player)` — the player's attack function, and the trigger

Not reachable by a callers-of search (it is dispatched virtually), which is
the other reason M25 missed it.

```c
if (player->attackCooldown /* +0xf48 */ > 0) {      // the player's attack cadence,
    player->attackCooldown -= frameDelta();          // the analogue of monster+0x2c4
    return;
}
player->attackCooldown = stats->attackSpeed /* +0x1c, i16 */;
if (player->weapon && player->weapon->usesRangedPath /* +0x1a7 */)
    player->attackCooldown *= 6;                     // ranged fires six times slower

if (player->swapTimer  > 0) return;                  // mid weapon-swap
if (player->swingAccum > 0) return;                  // already swinging
player->fatigue /* +0x3d8 */ -= 4;  if (< 0) = 0;    // a swing costs 4 fatigue

weapon = player->weapon;
if (weapon && weapon->frames != 0) {
    if (!weapon->throwingWeapon && !weapon->bow) {
        // three swing variants: 0 half the time, 10 a quarter, 5 a quarter
        weapon->variant = (rand()&1) ? 0 : ((rand()&1) ? 10 : 5);
        player->swingAccum = (weapon->frames + 2) << 8;
    } else {
        player->swingAccum = weapon->frames << 8;    // no variant, no +2
    }
}
player->swayPhase = 0;
```

Two more entries share the swing half: `FUN_10042394(player, item)` — "use
item at target", which starts a swing after a cast succeeds — and
`FUN_1001d880(player)`, player vtable **+0x190**, a bare "start a swing"
with the same gate but no `+2`.

### The sprite arithmetic, and why a weapon owns 16 sprite slots

The draw function walks the accumulator down:

```c
delta = frameDelta * 4;                                   // 40 at the 25Hz tick
if (weapon->reloadSpeed /* +0x184 */ != 0x100)
    delta = weapon->reloadSpeed * delta >> 8;
if (acc < 0x200) drawNothing();
else slot = weapon->baseSprite
          + (((frames + variant + 1) * 0x100 - (acc - 0x200)) >> 8);
acc -= delta;
```

For `weapons/club.s` (`SetWeaponSprite(88)`, `SetAnimationFrames(5)`) that
produces, exactly:

| variant | slots drawn |
|---|---|
| 0 | 89, 90, 91, 92, 93 |
| 5 | 94, 95, 96, 97, 98 |
| 10 | 99, 100, 101, 102, 103 |

Three five-frame swings, filling 88–103 after the idle pose at 88 — which
is what the **16-slot spacing between weapon sprite bases** (72, 88, 104,
120, 136) is for. Decoding those sixteen real sprites confirms it: each run
is a visibly different swing (an overhead chop, a low sweep, a backhand),
and 88 is the weapon simply held.

A swing therefore lasts 32 ticks, about 1.3 seconds.

Two details reproduced rather than tidied:

- **The accumulator lands on exactly `0x200` on the final tick**, which the
  `< 0x200` test lets through, so one extra frame at one slot past the
  animation is drawn. Real, and one frame long.
- **A ranged weapon starts two frames in.** Without the `+2`,
  `weapons/bandit_longbow.s` (base 175, 4 frames) draws only 178 and 179 —
  the last two slots of its five-slot strip. Whether that is a bug or a
  deliberate "no windup, straight to the release" is not something the code
  says. Nine scripts call `SetThrowingWeapon`, five `SetBow`, two
  `SetCrossbow`; every other weapon in the game gets the three variants.

`SetReloadSpeed` (`item+0x184`) has **zero call sites** in the corpus, so it
always holds its constructed value. It has to be `0x100`, since any other
default would rescale — or, at 0, freeze — every swing in the game; that is
inference from the game working, not a read of the constructor.

`SetAnimationFrames`, `SetReloadFrames`, `SetNumClips`, `SetClipSize`,
`SetIsAutomatic`, `SetHasZoom`, `SetScoped`: this Item class is a **leftover
FPS weapon class** the engine was reused with. Only the handful Shadowkey's
own scripts call are wired to anything.

### `FUN_1001d778(player, force)` — the weapon swap (vtable +0x194)

```c
if (force || (player->swingAccum < 1 && ...three more busy flags...)) {
    player->prevSprite /* +0x230 */ = player->currentSprite /* +0x22c */;
    ...
    vtable[0x24c](player);                       // re-resolve currentSprite
    if (player->currentSprite != player->prevSprite) {
        player->swapTimer = (player->swapTimer == 0 && player->prevSprite != -1)
                            ? 0x800 : 0x400;
        player->swayPhase = 0;
    }
}
```

and in the draw:

```c
if (player->swapTimer < 0x401) y = 0x7c;         // the NEW weapon, 124px low
else                          slot = player->prevSprite;   // the OLD one, at y=0
player->swapTimer -= 1;
```

So a swap shows the outgoing weapon, then the incoming one raised from
below. **The decrement is 1 per call**, which against the real 25 Hz tick
is 82 seconds for `0x800` — impossible as an intended duration. Either the
draw hook runs far more often than the game tick, or the seed is in the
engine's usual 0x100-per-second units and the decrement should be the frame
delta. Unresolved; recorded, and the port keeps the real seeds with its own
decrement.

### `FUN_1001f230(player, speed)` — the walk bob

```c
if (player->swingAccum == 0 && player->+0x1b4 == 0 && player->swapTimer == 0) {
    phase = player->swayPhase - ((frameDelta * (speed & 0xffff)) >> 8);
    if (phase > 0) { player->swayPhase = phase; return; }
    phase += 0x1400;                      // one whole cycle
} else phase = 0;                         // a swing or swap cancels the bob
player->swayPhase = phase;
```

The draw turns that phase into a blit offset:

- **x** is a triangle wave — `phase >> 8`, mirrored as `0x14 - that` above
  `0xa00` — so 0…10 px and back.
- **y** is `|sin(phase) * 5 >> 7|`, also 0…10 px, read from a real sine
  table at **`0x100f4954`**: 2048 `int32` entries at stride 4, 8.8 fixed
  point. Verified against the bytes — `k=0` → 0, `k=256` → 181 (0.7071×256),
  `k=512` → 256, `k=1536` → −256.

The index arithmetic that maps the phase into that table
(`((((n - (n >> 0x10)) * 0x100 & 0xffff0000) + 0x40000000) >> 0x10) + 0x4000
>> 3 & 0x1ffc`) is **not** reproduced literally: evaluated by hand across
the phase's actual range it lands within a few entries of the table's zero
crossings for every input, i.e. no bob at all, which cannot be the intent.
The table and the `* 5 >> 7` scale are certain; the mapping is the one
interpreted part. The `speed` argument's units are also unrecovered — the
call site was not identified.

### The viewmodel art is full-screen

Every weapon viewmodel slot in `global.spr` is a **176×208 frame**, and the
real blit puts a swing frame at `(0, 0)` with the sprite's full width and
height. These are full-screen overlays of a hand holding the weapon. The
only non-zero positions the function ever produces are the 0…10 px bob and
the 124 px drop during a swap.

### Noted in passing: the ranged attack spawns a projectile

`FUN_100425bc`'s ranged branch, after starting the swing:

```c
if (weapon->usesRangedPath /* +0x1a7 */) {
    PlaySound(engine, 1, player->x, player->y, 100, ...);
    spread = rand() % weapon->+0x1ce;                  // i16, a per-weapon spread
    ... target picked by five FUN_1001afb0 passes at 0x68/0x7c/0x90/0xa4/0xb8,
        rejected if the yaw difference exceeds 0x180 ...
    skill = FUN_10047f64(player->stats);
    id    = weapon->bow ? 599 : 598;
    FUN_10005730(player, id, id, spread, skill);
    return;
}
```

Two consecutive entity type ids, 598 for a thrown weapon and 599 for a bow.
This is
the same projectile machinery `PORT_ROADMAP.md`'s open "rest of a real cast"
bullet needs, reached from the weapon side rather than the spell side —
recorded here as a pointer, not implemented.
