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
*drawn*), over the `.zsk` **skybox**, which is what the frame starts as
before any of those faces are drawn (M70 — this line previously called
that mesh "the zone's baked room geometry"; see `ZONE_FORMAT.md`'s `.zsk`
section). So the walls, floors and ceilings the player walks around are
entirely the tile-grid pipeline's; nothing else contributes world
geometry. See `RENDERER_3D.md`'s "The tile-grid wall/surface-face
renderer" section for the drawing half of this.

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
  produces~~ — **resolved in M57: it is the map.** See "`FUN_1002b430` is
  the map, and here is all of it" below for the whole feature. The
  earlier reasoning held up — it is clearly not the first-person
  visibility system, since that is `TileGrid_RaycastVisibility`, a
  150-178-ray fan structurally nothing like this 65×65 window — and the
  window turns out to be 64×64 tiles drawn 2×2 pixels each.
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
| `2`   | idle — look for a target | **entity vtable `+0x10`, on every placement** (M77, see the end of this file); `AiDetect` |
| `3`   | pursue/attack a target (`monster+0x20c`) | the tick, on acquiring a target; `AiAttack(target)` |
| `4`   | flee | `AiFlee(target)` |
| `5`   | pursue a target — **no arm in the tick reads it** | `AiPursue(target)` |
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

### SUPERSEDED (M77): "Aggro is gated on line of sight, not distance alone"

Wrong -- see the M77 section at the end of this file. Both raycasts named
below are real, but neither is in the acquire arm: `FUN_10082004` is the
melee reach test and `vtable[0x21c]` is the OnDetect sightline. Kept for
the function addresses only.

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
| 4 | — (M48: **nothing**; a bare duration, `spells\Sanctuary.s`'s) |
| 6 | `+0x2c` (current **fatigue**) += `+0x34` (the caster's level), *unclamped* |
| 7 | current health += `+0x34`, clamped to max and to >= 0 |
| 8 | current health −= **`+0x76`**, and on reaching 0 calls the actor's kill vtable slot (`+0x28`) directly |

IgniteFoe is the *only* site in the whole status-effect dispatcher that
arms this channel, and it arms kind 8. **M48 found where kinds 6 and 7 are
armed: the real cast** (below) — `spells\Energize.s` arms 6 and
`spells\AzraSustenance.s` arms 7 — along with a fourth kind, 4, which
the tick implements as nothing at all.

**Correction: kind 6 is fatigue, not magicka.** `+0x2c` is clamped by
`FUN_1004bb54` against `+0x26`; magicka is `+0x2e`, clamped by
`FUN_1004bb20` against `+0x28`. M47 landed on the same field independently
when it found a weapon swing costing 4 points of `player+0x3d8`, which is
`player+0x3ac` (the stats block) + 0x2c. And kind 6 has **no clamp at
all** — an Energize really can push fatigue past its own maximum.

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

Recorded by M43 as an outline; decompiled and implemented in full by M48 —
see "The rest of a real cast" below.

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
    damage = rand() % weapon->+0x1ce;                  // i16 -- SetDamageMax
    ... target picked by five FUN_1001afb0 passes at 0x68/0x7c/0x90/0xa4/0xb8,
        rejected if the yaw difference exceeds 0x180 ...
    skill = FUN_10047f64(player->stats);
    id    = weapon->+0x17a ? 599 : 598;
    FUN_10005730(player, id, id, damage, skill);
    return;
}
```

Two consecutive entity type ids, 598 for a thrown weapon and 599 for a bow.
Recorded here as a pointer, not implemented — and M48, which did implement
the spell side, found this is a **different class**: the arrow is 0x16c
bytes built by `FUN_10007da4`, the spell projectile 0x198 bytes built by
`FUN_1005f0b4`. They share the actor base's position/velocity layout and
nothing above it.

> **Two corrections, from M49, which implemented this.** The `rand() %
> weapon->+0x1ce` term was written here as "a per-weapon spread"; it is the
> arrow's **damage** — `+0x1ce` is `SetDamageMax`, `+0x1cc` is
> `SetDamageMin`, and the melee branch a few lines below rolls
> `RandomRange(+0x1cc, +0x1ce)` from the same pair. So a ranged attack
> ignores the weapon's minimum entirely. And `+0x1a7` is not set by
> `SetBow`: `SetRange` sets it, whenever the value exceeds `0x400`. See
> "The arrow" below.

---

## The rest of a real cast, and its projectile (M48)

M43 decompiled `FUN_10046764` and recorded its shape; this implements it,
along with the projectile it exists to launch. The gap it closes is the one
`PORT_ROADMAP.md` names: this port called `HitTarget()` straight at a
target on both the player's and a creature's cast, so **every spell was
hitscan and the whole self-targeted half of the spell list did nothing at
all**.

### The shape of a cast

```c
int Cast(Spell* spell) {                       // FUN_10046764
    stats = FUN_1002fd30(spell->owner);        // the *caster's* stats block
    level = stats->+0x34;                      // the caster's level
    <switch 1: cost, sound, projectile art, flat impact damage>
    if (spell->scroll) cost = 0;
    if (ownerIsPlayer && FUN_1003e6f4(owner)) cost = max(0, cost - 6);
    magnitude = (!scroll && ownerIsMonster) ? spell->SetLevel : level;
    if (stats->magicka < cost && !scroll && !castByMonster) return 0;   // the only gate
    SetMagicka(stats, stats->magicka - cost);
    PlaySound(engine, sound, 100, ...);
    <switch 2: the self-targeted effect, applied to `stats` right here>
    if (projectileArt) { p = new(0x198); FUN_1005f0b4(p, owner, ...); ...; Spawn(engine, p); }
    if (spell->scroll) { destroy the scroll }
    return 1;
}
```

Two things about the gate are worth keeping. A **scroll** skips the
affordability test *and* still runs the deduction — which clamps at zero,
so reading a scroll on an empty pool is free and harmless. And a
**creature** skips it too (the decompile's `bVar3`), which is what lets the
32 shipped caster scripts work while not one of them sets a magicka pool.

### The table

The whole first switch, flattened. `L` is the caster's level.

| typeId | script | cost | sound | projectile |
|---|---|---|---|---|
| 50 | `blaze.s` | L+6 | 0x54 | 2 |
| 51 | `HealWound.s` | L+10 | 0x55 | — |
| 4002 | `spells\DeadToDust.s` | L+6 | 0x54 | 5 |
| 4006 | `blaze.s` (greater) | **0** | 0x54 | 2, +50 flat |
| 4008 | `spells\Weakness.s` | L+7 | 0x54 | 5 |
| 4009 | `spells\Absorb.s` | L+20 | 0x54 | 5 |
| 4010 | `spells\Blind.s` | L+8 | 0x54 | 2 |
| 4011 | `spells\RaiseStrength.s` | L+14 | 0x57 | — |
| 4012 | `spells\DoomHammer.s` | L+30 | 0x54 | 2 |
| 4013 | `spells\BodyToMind.s` | L+3 | 0x57 | — |
| 4014 | `spells\CureDisease.s` | **0** | 0x57 | — |
| 4015 | `spells\CurePoison.s` | **0** | 0x57 | — |
| 4016 | `spells\DaedricWeapon.s` | L+18 | 0x54 | — |
| 4017 | `spells\DoomHammer.s` | L+30 | 0x54 | 2 |
| 4018 | `spells\Drain.s` | L+20 | 0x54 | 5 |
| 4019 | `spells\Energize.s` | L+6 | 0x57 | — |
| 4020 | `spells\Fear.s` | L+15 | 0x54 | 5 |
| 4021 | `spells\FeebleBlade.s` | L+5 | 0x54 | 5 |
| 4022 | `spells\Frenzy.s` | L+16 | 0x57 | — |
| 4023 | `spells\HarmArmor.s` | L+8 | 0x54 | 5 |
| 4024 | `spells\IgniteFoe.s` | L+8 | 0x54 | 5 |
| 4025 | `spells\Paralyze.s` | L+19 | 0x54 | 5 |
| 4026 | `spells\RemoveEnchantment.s` | **12 flat** | 0x57 | — |
| 4027 | `spells\Righteousness.s` | L+7 | 0x57 | — |
| 4028 | `spells\Sanctuary.s` | L+2 | 0x57 | — |
| 4029 | `spells\Shield.s` | L+6 | 0x57 | — |
| 4033 | `spells\Disease.s` | L+6 | 0x54 | 5 |
| 4034 | `spells\Poison.s` | L+6 | 0x54 | 5 |
| 4035 | `spells\DeathHowl.s` | L+25 | 0x54 | 5 |
| 4038 | `spells\AzraWrath.s` | L+20 | 0x54 | — (area) |
| 4039 | `spells\AzraSustenance.s` | L+20 | 0x57 | — |
| *anything else* | — | L+6 | 0x54 | — (sprintf's a warning) |

Reading a branch tree that dense is the kind of thing that silently goes
wrong, so the table is checked **three independent ways** against shipped
data, and all three agree.

1. **The `HitTarget` split is exact.** Fifteen shipped spell scripts define
   a `HitTarget[...]` handler and fourteen do not — and the fifteen are
   exactly the fifteen this table gives a projectile, with one explained
   exception (`spells\AzraWrath.s`, whose damage comes from the native area
   branch). That is the whole corpus, with no fudge.
2. **The sound slots name themselves.** In all 21 shipped `*_sounds.txt`,
   slot `0x54` is `pl_cast_fire.wav` and slot `0x57` is
   `pl_cast_powerup.wav` — and the table hands 0x54 to the offensive spells
   and 0x57 to the buffs and cures. (`0x55`, HealWound's alone, is
   `NULL.wav` everywhere: the heal is silent in the shipped game.)
3. **Three rows are stated outright by scripts.**
   `spells\U_Heal_Wound_Lvl10.s` calls `SetSpellType(51)`,
   `spells\U_Frenzy_lvl10.s` `SetSpellType(4022)` and
   `spells\U_Blaze_lvl5.s` `SetSpellType(50)`.

### The self-targeted half

The second switch, which is what the fourteen `HitTarget`-less scripts
exist for. `m` is the magnitude.

| typeId | what it does to the caster |
|---|---|
| 51 HealWound | `SetHealth(health + 6 + m*2)`, clamped |
| 4011 RaiseStrength | stat 9 +`m*5` for `m*10`s |
| 4013 BodyToMind | magicka += all fatigue; fatigue = 0 |
| 4014 CureDisease | remove the modifiers named `"DISEASE"` and `"DISEASE_DEF"`, clear flag 0x10 |
| 4015 CurePoison | clear flag 8 — its entire body |
| 4016 DaedricWeapon | conjure entity 4037 (`weapons\DaedricSword.s`) into the weapon slot, if absent |
| 4019 Energize | periodic kind **6** for `m*10`s |
| 4022 Frenzy | attack +`m` for `m*5`s |
| 4026 RemoveEnchantment | strip every modifier with a negative delta; clear blindness |
| 4027 Righteousness | attack **and** armour +`m`, both for `m+5`s |
| 4028 Sanctuary | periodic kind **4** for `m*5`s |
| 4029 Shield | armour +`m*2` for `m*10`s |
| 4038 AzraWrath | `m*4` damage to every creature within 12000 units (`FUN_1004720c`) — ~47 tiles, i.e. the level |
| 4039 AzraSustenance | periodic kind **7** for `m+10`s |

Three details reproduced rather than tidied:

- **The duration store is 16-bit.** `stats+0x78` takes a `short`, so
  Energize's `m * 0xa00` wraps negative at magnitude 13 and the effect
  simply never runs for a caster that high. A real bug, one branch wide.
- **`+0x76` is written to 1 by every one of the three periodic arms**, and
  that is the field poison and the burn *share*. So casting Energize while
  poisoned really does drop the poison from 3 points a second to 1 — the
  same shared-field quirk M34 found from the other direction.
- **Kind 6 has no clamp**, so fatigue can run past its own maximum.

### `FUN_10046680` — the cooldown is a Sanctuary rule

Called immediately before the cast at both call sites, and it returns 1 at
once unless the caster is the player. For the player:

```c
now = level->+0x460;                                   // the 1/256s clock
if (player->+0xfc4 != 0 && now < player->+0xfc4 + spell->SetRefireRate) return 0;
if (spell->typeId == 0xfbc && now < player->+0xfc8 + spell->SetRefireRate) return 0;
if (player->+0x428 == 4) return 0;                     // unidentified state
player->+0xfc4 = now;
```

`SetRefireRate` is the Spell class's `+0x1d2`, the sibling of `SetLevel`'s
`+0x1d0` in dispatcher `0x10046328`. **Exactly one shipped script sets
one** — `spells\Sanctuary.s`'s `SetRefireRate(768); //3 secs`, whose own
comment independently confirms the `seconds * 0x100` unit — so the gate is
inert for every other spell in the game. The only runtime writer is the
cast itself: AzraWrath sets 400 as it fires.

The two clauses are not symmetric, and the asymmetry is real: the general
one is guarded on "something has actually been cast", the Sanctuary one is
not. So for the first three seconds after a level's clock starts, Sanctuary
alone is refused.

`player+0xfc8`, the second stamp, is written at the tail of `FUN_10049780`
when a kind-4 periodic channel expires — i.e. when a Sanctuary runs out.

### The projectile

`FUN_1005f0b4` allocates 0x198 bytes and points it where the caster is
looking:

```c
p->x = caster->x;  p->y = caster->y;  p->z = caster->z + 0x20;
p->vx = sinTable[yaw >> 5];                       // 0x100f4954, M47's table
p->vy = sinTable[(yaw + 0x4000) >> 5 & 0x7ff];
p->vz = sinTable[(-pitch) >> 5 & 0x7ff] + 0x32;
p->radius /* +0x8e */ = 200;
p->+0x180 = 1;                                    // "run the script's HitTarget on impact"
p->+0x182 = flat impact damage;                   // 50 for the greater blaze, else 0
```

Note the engine's heading zero points along **+y** (`vx = sin`, `vy = cos`),
which is a quarter turn off this port's own camera convention.

The velocity comes off the sine table **unscaled**, so the speed is 0x100
units a tick — exactly one tile. `FUN_1005f928` then flies it:

```c
x += vx; y += vy; z += vz;  clamp x,y into the map;
if (++age >= 0xd) despawn;                        // twelve ticks of collision
tile = Map_GetTileAt(engine, x, y);
if (!tile) return;                                // off-map: keeps flying
wall = ((tile[1] & 0x1c) == 4) || (tile[0] & 2);
for (each entity overlapping the projectile's 400x400 AABB)
    if (it is a creature or the player, and is not the caster) { Impact(); despawn; }
if (wall) { one more creature-only sweep; despawn; }
```

So **a spell's range is twelve tiles and nothing else** — there is no
`SetRange` on the spell side at all. The impact test is purely 2D: an x/y
AABB, with no height test anywhere, which is also why the vertical velocity
cannot be checked against anything.

And `FUN_1005f3c8` is the impact, which is the whole point:

```c
if (p->+0x180) call p->spell's "HitTarget" through the scripted-call slot, with the target;
if (p->+0x182 > 0 && p->owner) target->stats->DoDamage(p->+0x182, ownerStats, 0, 1);
```

`"HitTarget"` is the UTF-16 literal at `0x100b105c`. **This is why
`blaze.s` has a `HitTarget[ (target) { DoAttackRoll(target, 1); } ]` at
all**: the projectile's arrival is what runs the status dispatcher.

The clincher is a commented-out line in that same script:

```
//Level.CreateEffect( 12, 19, 1, target.GetPositionX(), ... , 128, 16, 128, 128 );
```

against the tail of `FUN_1005f928`, for a spell whose typeId is 50 or 4006:

```c
FUN_10073528(level, 0xc, 0x13, 1, x, y, z, 0x80, 0x10, 0x80, 0x80, 0);
```

— the same call with the same twelve arguments, moved into the engine and
hardcoded to blaze. The script author commented theirs out because the
native projectile had taken it over.

### Not reproduced

**The art — identified in M63, see below; still not drawn.** `+0x134` is 2
for blaze/Blind/DoomHammer and 5 for the other twelve projectile spells,
and it seeds a one-frame animation range (`+0x140`/`+0x144`/`+0x14c` all
`art << 8`) as well as being stored directly. It is *not* a `models.txt`
index — 2 is `lantern.bin` and 5 is `sbarrel.bin`. It is a **`global.spr`
slot**, indexed into the engine's loaded-sprite table at `engine+0x4460`
by `FUN_1008b25c`; both slots are real 32×32 sprites (2 averages
RGB (182, 131, 72), a gold-orange fireball; 5 averages (173, 106, 46)).
The port still carries the number and simulates the projectile without
drawing it, because nothing here draws a billboard yet.

Also not reproduced: the multiplayer mirror of the spawn and the impact,
HealWound's extra term for a player whose class (`player+0xf3c`) is 7 (read
through an undecompiled stats-block vtable slot), and the monster-only
predicate at target vtable `+0x170` that the impact sweep rejects on.

---

## The arrow (M49)

M47 recorded the ranged branch as a pointer and M48 established that the
thing it spawns is a *different class* from the spell projectile it had
just implemented. This is that class: **0x16c bytes, constructed by
`FUN_10007da4`, spawned by `FUN_10005730`, flown by `FUN_10007214`**,
against the spell projectile's 0x198 / `FUN_1005f0b4` / `FUN_1005f928`.
The two share the actor base's position and velocity fields and nothing
above them, and — as it turns out — not even the same flight model.

### Which weapons take the ranged path, and it is not `SetBow`

The player's attack routine branches on `weapon+0x1a7`. The Weapon class's
dispatcher (`FUN_1006ca90`, trie `0x14d44`) writes that field in exactly
one place, and it is `SetRange`:

```c
case 2:                                   // SetRange
    weapon->+0x1a0 = value;
    if (value < 0x401) return 1;
    weapon->+0x1a7 = 1;
    return 1;
```

So "is this a ranged weapon" is a **range threshold**, not a flag. None of
`SetBow`, `SetCrossbow` or `SetThrowingWeapon` touches `+0x1a7` at all.

That is only safe because the shipped corpus is bimodal, which this port
already knew from the other direction (M20: "every melee weapon uses
exactly 384, every ranged one exactly 16384, no other value appears
anywhere"). Re-checked against the threshold: **all 16 weapons with
`SetRange(16384)` are precisely the 16 that call one of the three markers,
and all 65 with `SetRange(384)` call none of them.** M20's convenience
proxy turns out to be literally how the engine decides.

The three markers do matter, for a different question. Mapping the whole
dispatcher's 24 cases onto the trie's registered names gives:

| binding | field | binding | field |
|---|---|---|---|
| `MakeInfinitePickup` | `+0x1a5 = 1` | `SetWeaponDamage` | `+0x18a` (i16) |
| `SetPickupable` | `+0x1a4` | `SetShootSound` | `+0x188` (i16) |
| `SetRange` | `+0x1a0`, and `+0x1a7` | `SetNumClips` | `+0x190` (i16) |
| `SetWeaponSprite` | `+0x19c` | `SetClipSize` | `+0x18c`, `+0x18e` |
| `SetAnimationFrames` | `+0x180` | `SetIsThrown` | `+0x179` |
| `SetThrowingWeapon` | `+0x179` | `SetIsLaunched` | `+0x17a` |
| `SetBow` | `+0x17a` | `SetIsAutomatic` | `+0x17b` |
| `SetCrossbow` | `+0x17a` | `SetAllowReload` | `+0x17c` |
| `SetWeaponClass` | `+0x1a6` | `SetCanBash` | `+0x17d` |
| `SetReloadFrames` | `+0x180` | `SetHasZoom` | `+0x178` |
| `SetReloadSpeed` | `+0x184` | `SetScoped` | `+0x17e` |
| `SetFireRate` | `+0x194` | `SetWielderMovement` | `+0x198` |

Three names, two bytes: `SetBow`/`SetCrossbow`/`SetIsLaunched` all write
`+0x17a`, and `SetThrowingWeapon`/`SetIsThrown` write `+0x179`. (The
right-hand column is mostly M47's "leftover FPS weapon class" — and note
`SetAnimationFrames` and `SetReloadFrames` really do share `+0x180`.)

The ranged branch reads only `+0x17a`, to choose between two consecutive
entity type ids — and the shipped data names both of them:

```
598 176 1 !throwing        models.txt 176 = throw_dagger.bin
599 175 1 !arrow           models.txt 175 = arrow.bin
```

The 7 bows and crossbows land on 599 and the 9 darts and throwing knives
on 598, which is what the two model names say they should.

### A creature shoots when `SetProjectile` was called, and the value is dead

`FUN_100835b8`'s ranged branch:

```c
if (monster->+0x2d8 != -1) {                       // SetProjectile
    damage = (rand() % stats->+0xa) - stats->+0x8 + stats->+0x8;   // == rand % damageMax
    skill  = FUN_10047f64(monster->stats);
    FUN_10005730(monster, monster->+0x2d8, 599, damage, skill);
}
... swing animation, attack sound ...
if (monster->+0x2d8 == -1) { <the entire melee resolution> }
```

Two things. **An archer never melees** — the whole melee tail sits inside
the `== -1` branch. And the entity type is **hardcoded 599** here; the
script's own `SetProjectile` value goes into the spawn's *other* argument.

That argument is `+0xc6`, a per-entity draw parameter the entity's render
override (`FUN_10064ffc`) forwards into the rasterizer dispatch. The model
does not come from it: `FUN_10089820`, which runs during the spawn, looks
the entity up by `+0xc8` — the typeId — through `entities.txt` and stores
the resolved model in `+0x54`, which is what
`Actor3D_TransformAndSubmitModel` draws. So all twelve shipped archers
passing `SetProjectile(175)` — 175 being `models.txt`'s own index for
`arrow.bin` — is the script author writing a model index into a slot that
is not one, and it changes nothing: the arrow draws `arrow.bin` because
the hardcoded 599 says so. **`SetProjectile` is load-bearing only as a
flag.**

The twelve agree on more than that: every one also sets
`SetAttackRange(12000)` (except `lakvan/deadeye.s`, at 50000),
`SetAttachedWeapon(225)` — `models.txt` 225 is `bow.bin`, the visible bow
M46's field draws — and `SetAttackNoise(1)`. Slot 1 is
`barch_firebow.wav` in 21 of the 22 shipped `*_sounds.txt`, and it is the
same slot the *player's* ranged branch plays as a bare literal
(`FUN_1001b198(engine, 1, ...)`). None of the twelve calls `GiveWeapon`,
which is why the branch's `weapon == 0 || ...` gate passes for them.

### The spawn

```c
Arrow* Spawn(Actor* shooter, int art, int typeId, int damage, i16 skill) {   // FUN_10005730
    a = new(0x16c); FUN_10007da4(a, art, typeId);      // +0xc6 = art, +0xc8 = typeId
    a->vtable[0x10](a, shooter->engine);               // resolve the model from typeId
    a->owner = shooter;                                // +0x160
    a->damage = damage;                                // +0x164
    a->skill  = (i16)skill;                            // +0x168
    a->x = shooter->x;  a->y = shooter->y;             // verbatim -- no muzzle offset
    a->z = (shooter == engine->player) ? shooter->+0x224          // the eye-height field
                                      : shooter->z + 0x100;      // a whole tile up
    a->yaw = shooter->yaw; a->roll = shooter->+0xb2; a->pitch = shooter->pitch;
    a->vx = (sine[ yaw        >> 5] * 0xa00) >> 8;  a->vx >>= 3;
    a->vy = (sine[(yaw+0x4000)>> 5] * 0xa00) >> 8;  a->vy >>= 3;
    a->vz = (i16)((sine[(-pitch) >> 6] * 0xa00) >> 8);
    a->pitchRate >>= 3;                                // a no-op: it is zero
    a->vz >>= 3;
    AddToLevel(shooter->engine, a);
}
```

Three things worth keeping.

**Speed.** `sine * 0xa00 >> 11` against the table's 8.8 scale is
`256 * 1.25 = 320` raw units a tick — **1.25 tiles**, a quarter faster
than a fireball's flat one. And unlike the fireball, an arrow has **no
lifetime at all**: there is no age counter anywhere in the class. It flies
until geometry or a body stops it. The weapon's own `SetRange(16384)` is
consumed entirely by the `> 0x400` test; it is never a distance.

**Pitch is read at half scale.** The vertical term indexes the sine table
at `>> 6` where every other consumer in the engine — including the spell
projectile's structurally identical pitch term, which M48 read as `>> 5`
from the same shaped code — uses `>> 5`. Verified in the disassembly on
both sides (`and lr,lr,r3,asr #0x6` here against `and r12,r12,r3,asr #0x5`
at `0x1005f25c`). Left as found: it halves an arrow's elevation rather
than breaking it.

**The `pitchRate >>= 3`** reads and rewrites `+0xaa`, which the base
constructor (`FUN_10060d54`) zeroed and nothing since has written — so it
is a no-op, and looks like a copy-paste of the `vz >>= 3` beneath it. With
all three angular rates left at zero, an arrow flies dead straight.

### The flight, and what actually stops it

`FUN_10007214`, in order:

1. Integrate position and all three angles.
2. `Map_GetTileAt(engine, x, y)`; **if there is no tile, return** — off the
   map an arrow is neither tested nor despawned, it simply keeps going.
3. **The player's shot only**: walk the entity list of the arrow's own
   tile, take the first creature that is not the owner, roll to hit, apply
   damage — and despawn *whether or not the roll landed*, since the
   despawn sits outside the hit branch.
4. Four wall probes, at ±0x20 on each axis.
5. A general entity sweep over three columns.

The probes are the interesting part, because what they test is not a wall
flag:

```c
bool Blocked(Arrow* a, Sector* s, Tile* t) {          // FUN_1008977c, vtable +0x168
    FUN_1001bdf4(s, t, a->x, a->y, a->engine, a->z);
    return a->z < FUN_1001beac(s, t, a->x, a->y, a->engine, a->z);
}
```

`FUN_1001beac` is the **collision height** at a position:

```c
int CollisionHeight(Sector* s, Tile* t, int x, int y, Engine* e, int z) {
    solidCeiling = s->surIndexCeilingA != 0xff
                   && !(e->surfaces && (e->surfaces[s->surIndexCeilingA].flags & 0x20));
    if (!(t->byte1 & 2) || !solidCeiling || z < Ceiling(s, t, x, y))
        return Floor(s, t, x, y);                    // FUN_1001bd50
    return Ceiling(s, t, x, y);                      // FUN_1001bcac
}
```

and the two height lookups are conditionally interpolated:

```c
Floor(s, t, x, y)   = (t->byte0 & 0x04) ? bilinear(s->floorHeight[4],  x&0xff, y&0xff)
                                        : s->+0x02;   // the flat authored band value
Ceiling(s, t, x, y) = (t->byte0 & 0x40) ? bilinear(s->ceilingHeight[4], x&0xff, y&0xff)
                                        : s->+0x04;
```

Those two bits are the same ones the renderer already reads for exactly
this purpose (`RENDERER_3D.md`'s floor and ceiling band gates) — so **the
`.zmp` cell's flags bit 2 and bit 6 mean "this tile has per-corner
floor / ceiling heights"**, in collision as in drawing. The second cell
byte's bit 1 is new here: it marks a two-storey tile, whose ceiling
becomes the collision floor for anything already above it.

> **How much each branch is actually worth (M62).** Measured across all
> 21 shipped zones (331,776 tiles), comparing a plain bilinear blend of
> the four corners against the real `CollisionHeight` above:
>
> * **The flat-vs-corners branch changes nothing, anywhere.** 241,174
>   tiles have `flags & 0x04` clear, and on every one of them the four
>   stored corners already equal `ZcpEntry+2`. The bit is a storage and
>   authoring distinction — "this tile is sloped" — not a behavioural one,
>   and a renderer or a physics step that ignores it gets the same answer.
> * **The two-storey branch is entirely real and entirely local.** Exactly
>   **1,921** tiles carry it, and only two zones have any: `dstar_w`
>   (1,723, of which 1,644 have a standable ceiling surface) and
>   `glaciercrawl` (198/118). On 1,532 of them an actor above the ceiling
>   resolves to a ground up to **2,112 raw units** — eight tile-widths —
>   above what the corner blend returns. That is the difference between
>   standing on Dragonstar West's upper level and falling through it.
>
> So of the two things this function does that a naive floor lookup does
> not, one is free and the other is the whole of two zones' verticality.

### `SetPosition`'s floor snap is this same function (M62)

`FUN_100686e0` — the snap step of Entity binding `0x30` — is a one-line
wrapper around it:

```c
void SnapToSurface(Entity* e) {                       // FUN_100686e0
    e->z = CollisionHeight(&e->engine->zcp[e->tile->zcpIndex], e->tile,
                           e->x, e->y, e->engine,
                           (int16_t)e->z + 0x180);
}
```

and its one caller adds `0x80` afterwards. So a teleported actor ends up
at `CollisionHeight(x, y, requestedZ + 0x180) + 0x80`: the argument `z` is
**raised by 384 and used as the upper-vs-lower-storey probe**, then thrown
away, and the resolved surface is lifted by 128 so the actor stands on it
rather than in it. On an ordinary tile the requested z has no effect at
all; on one of those 1,921 two-storey tiles it is what chooses the floor.

The snap is gated on `vtable[0xc8]`, the predicate for membership of the
engine's **actor list** at `engine+0x640` (the list `SetZone`'s
experience-budget loop walks, narrowing it further with the "is a monster"
predicate at `vtable[0xe4]`). A player and a monster are in it; a door, an
item and a prop are not — which is what lets `gate.s`'s portcullis rise
1200 units and stay there. See `SIMKIN_NATIVE_API.md`.

The consequence is worth stating plainly. **An arrow is stopped by
geometry taller than the arrow, not by anything flagged as a wall.**
Checked against real azra data: all **725** of the zone's wall-flagged
tiles do stop a shot fired from the player's own eye height — and so do a
further **1858 tiles that are not flagged as walls at all**. A wall-flag
test, which is what the spell projectile uses, would fire an arrow through
every one of those. What is left is a single connected space 13695 tiles
across, which is the level.

A blocked probe does three things at once: pushes the arrow back out of
the cell (`x = (x + 0x100) - ((x - 0x20) & 0xff)` and its three
variants), **reflects that axis' velocity**, and despawns the arrow — and
then carries straight on into the next probe with the mutated position, so
a shot can be marked dead and still hit something in the same tick's
sweep.

One probe is transposed. The `-y` probe's "one frame back" lookup is

```c
Map_GetTileAt(engine, (y - vy) - 0x20, x - vx);   // the y expression in the x slot
```

against the correct order in the other three. Verified in the
disassembly, not a decompiler artifact. It only decides whether the probe
is vetoed for being off the map — `FUN_1008977c` reads only the *ahead*
pair, and the four "back" lookups exist purely so their being null can
cancel the probe — so its effect is that an arrow near the grid's edge
occasionally gets one probe waived on the wrong axis.

### The impact

Both paths roll through the same gate the melee code already uses,
`FUN_1004b620` — `attack * 256 / (attack + defense)` against
`rand % 0x100`. Two details of the calls:

* The caller computes the target's defense with `FUN_1004801c` and passes
  it in, and **`FUN_1004b620` ignores that argument** and recomputes it
  from the target's stats itself.
* The two paths disagree about the attacker's rating. The player's
  first-look pass uses the value stamped into the arrow at spawn time
  (`+0x168`); the general sweep re-reads the owner's live rating with
  `FUN_10047f64`. They differ only if the shooter's stats changed
  mid-flight.

And the damage:

```c
targetStats->vtable[0x10](targetStats, arrow->damage, attackerStats, 0, 1);
```

Note what is **not** there. The melee path fetches the target's armour
rating (`stats->vtable[0x3c]`) and subtracts it before this call; the
arrow's two impact paths pass the rolled damage straight through. **A
ranged hit ignores armour entirely.** Combined with `rand % damageMax`
ignoring the weapon's minimum, a bow's damage profile is genuinely
different from a sword's rather than a reskin of it.

The general sweep takes the entity list of the first of three columns that
has *any* entity in it — `(x, y)`, then `(x + 0x80, y)`, then
`(x - 0x80, y)` — and walks only that one. A miss despawns the arrow and
then **continues to the next entity in the same list**, rolling again.

### Also found

* The player's ranged branch sets its own refire cooldown to
  `attackSpeed * 6` (`player+0xf48`), against the melee path's bare
  `attackSpeed` — a bow is six times slower to ready than a sword. This
  port has no player attack-speed timer at all (one swing per keypress),
  so there is nothing to apply it to; recorded.
* `FUN_1003c45c` is the **multiplayer mirror**: it replays a remote
  player's shot as `FUN_10005730(remote, 599 or 598, same, 0, 0)` — zero
  damage, zero skill, purely visible art. Not implemented, same as M48's
  spell mirror.

### What this port does

`simkin_bindings/arrow_projectile.h` / `.cpp`, plus
`Zone::CollisionFloorHeightAt()` for the probe. Both sides shoot: the
player's ranged weapon and every archer creature. A bow can now miss by
the target stepping aside, an arrow can be stopped by a pillar, and — for
the first time in this port — a projectile is actually **visible**, since
unlike the spell projectile's unidentified `+0x134` art selector, an
arrow's model is completely pinned down (`599 -> 175 -> arrow.bin`,
`598 -> 176 -> throw_dagger.bin`).

Simplifications, all recorded at the code: the port's candidate list for
both sweeps is actors only, so an ordinary world object cannot shadow a
creature in the column sweep the way it can in the real engine; and the
monster-side predicate at target vtable `+0x170` that the first-look pass
rejects on is still unidentified, so "still alive" stands in for it, as it
does everywhere else here.

## The script timer, and the two "unplaceable" scripts (M53)

The roadmap carried `crypt2/controller.s` and `twilite/steamsound.s`
together as scripts "this port cannot place", on the theory that neither
one's placement mechanism was a category the zone-load block resolves.
That theory is wrong for both, in opposite directions -- and what was
actually missing is a mechanism neither the roadmap nor this document had
noticed.

### `Delay(seconds, tag)` -> `DelayReached(tag)`

Every entity carries a one-shot timer. M52 named its three fields from the
binding side (`Entity+0x10a` armed, `+0x10c` deadline, `+0x110` tag)
without knowing what fired it. The other half is **`FUN_1006410c`**, run
once per frame per entity:

```c
if (obj->armed && obj->deadline <= engine->clock) {
    obj->armed = 0;                       // cleared BEFORE dispatch
    /* call the script method named by the wide literal at 0x100b1634 */
    obj->script->method("DelayReached", (int)obj->tag);
}
```

and the arming side is the Entity dispatcher's own `Delay` case:

```c
obj->armed    = 1;
obj->deadline = engine->clock + seconds * 0x100;
obj->tag      = second argument, or 0 when called with one
```

`StopDelay` clears the armed flag and nothing else.

**The clock is not a wall clock.** It is `engine+0x470 -> +0x460`
(`FUN_1002f694`), a counter advanced every frame by the same delta the
animation player uses — 8.8 fixed-point *seconds*, `elapsedMs * 256 /
1000` clamped to `[4, 64]` — and zeroed on a level load (`FUN_1002fca4`).
So `seconds * 0x100` is simply "seconds, in 8.8".

Clearing the armed flag *before* the callback is what makes the shipped
scripts work at all: every chained sequence in the corpus ends each case
by arming the next one, and a dispatch order that cleared afterwards would
throw that away.

> **A real bug in the save format, and where the two halves meet.**
> `Entity`'s save writes `+0x10c` as `value - time(0)` and its loader adds
> its own `time(0)` back — the same treatment it gives `+0x118`. That is
> correct for `+0x118`, which really is `time(0) + n` (`FUN_10068480`,
> M52). It is wrong for `+0x10c`: subtracting and re-adding a wall clock
> leaves a **game** clock deadline shifted by however many real-world
> seconds passed between the save and the load, divided by 256. A
> `Delay(1, ...)` armed just before saving and reloaded a day later comes
> back as a deadline about five and a half minutes of game time away — and
> the game clock has been reset by the level load underneath it anyway.

### How much of the game runs on it

**Sixteen shipped scripts define `DelayReached`, across 59 placements**,
and every one of them is placed — in exactly two `entities.txt`
categories:

| script | placements | category |
|---|---|---|
| `monsters/azra_rat.s` | 35 (azra) | 2 |
| `crypt2/sarc_entity.s` | 11 (crypt2) | 8 |
| `crypt1/sarcophagus_*.s` (9 files) | 11 (crypt1) | 8 |
| `drgnfld/loot1.s`, `loot2.s` | 2 | 8 |
| `monsters/umbra_keth.s` | 1 (crypt3) | 2 |
| `ratherb.s` | 1 (azra) | 2 |
| `crypt2/controller.s` | 1 (crypt2) | 3 |

Categories 2, 3 and 8 are all categories this port already loads, which is
why the gap was invisible: the scripts were being read and `Init`-ed, and
then simply never ticked. Three consequences, in rough order of how
visible they are:

- **`azra_rat.s`** arms `Delay(2, 0)` on the eighth rat kill; its
  `DelayReached(0)` opens the `rathurrah` menu. That is the completion
  message for the game's first quest, in the zone this port starts in, and
  it has never appeared.
- **`umbra_keth.s`** uses the timer as the final boss's *phase* cycle: it
  vanishes, opens `umbra_disappear`, and arms `Delay(Random(8, 12), 1)` to
  come back. Without the timer the fight cannot progress.
- **The sarcophagi** in crypt1 and crypt2 stagger the creature climbing
  out of each one over a second.

### `crypt2/controller.s` was always placed

It is `crypt2.ent` record #166: typeId **6023**, which `entities.txt` maps
to category **3** and the label `!final_tp`, with the placement name
`"star"` and position `(4480, 7808, 70)`.

Its trigger is `crypt2.s`'s own `AddCrystal()`, on the seventh crystal:

```
Star = GetEntity("star");
if (Star != null) { Star.Delay(1, 0); }
```

and from there the script is a pure sequencer. `DelayReached(s)` for
`s = 0..6` lights one of the seven crystal alcoves (`Level.LightRect(...,
64)`), plays sound 83, and arms `Delay(1, s+1)`; `s = 7` calls
`Level.CreateEntity(274, 4480, 7808, 400)` — **the same spot the
sequencer itself stands on** — scales it, and mirrors
`ClientFightUmbra`. It is the Umbra's arrival cutscene, eight seconds
long.

Two things were missing rather than one. The timer, above; and
`GetEntity("star")` — pickups (categories 3 and 8) were the one loaded
category this port never entered into the `Level.GetEntity` registry, so
the lookup returned null and the trigger died on the first line.

### `twilite/steamsound.s` is cut content

Its whole body is `SetPassable(true); ShowEntity(false); PlaySound(65, 75,
1, 255);` — an invisible, walk-through ambient emitter, using the
four-argument `PlaySound` M51 decoded (a quieter, directional, endlessly
repeating steam hiss).

Nothing loads it. No `.ent` placement in any of the 21 zones names it, no
`entities.txt` row names it, and no other script mentions it. The steam
*regions* do exist — `twilite.zon` carries `Steam01`..`Steam05` and
`twilite.s`'s `EnterZone` opens `MistMenu` for each — but they are a
different mechanism with a different effect, and none of them reaches this
script.

And it is not a special case. Sweeping the whole corpus the same way,
**145 of the 1535 shipped scripts** are named by no placement, no
`entities.txt` row and no other script. (Some of those are opened by the
engine directly by name — `SaveConfirm`, `MPDeathMenu` and friends are
literals in the image — which narrows the set but does not touch
`steamsound.s`, an entity script with no native path to it.) So the
correct reading is not "this port cannot place it" but "the shipped game
cannot either": it is one leftover among many, and the roadmap bullet's
pairing of the two scripts was the mistake.

Implemented in
[`port/src/simkin_bindings/script_delay.h`](../port/src/simkin_bindings/script_delay.h);
smoke test `src/tests/m53_script_delay_smoke.cpp`.

## Entity collision, and the two scalars nothing named (M55)

M52 named every scalar in the save record but two: `Entity+0x8c` and
`Entity+0x92`. Both looked genuinely dead — zeroed by the constructor,
carried by the save format, and matched by no direct field access
anywhere in the image.

The direct-access part was true and the conclusion drawn from it was
wrong. **Both are reached only through vtable accessors**, four
three-instruction thunks Ghidra never marked as functions:

| slot | thunk | instruction |
|---|---|---|
| `+0x40` | `0x100a151c` | `LDRB r0, [r0, #0x8c]` |
| `+0x4c` | `0x100a212c` | `STRB r1, [r0, #0x8c]` |
| `+0x50` | `0x100a2180` | `STRH r1, [r0, #0x8e]` |
| `+0x54` | `0x100a2178` | `STRH r1, [r0, #0x90]` |
| `+0xac` | `0x100a2678` | `LDRB r0, [r0, #0x92]` |
| `+0xb0` | `0x100a2648` | `STRB r1, [r0, #0x92]` |

All **31 Entity-derived vtables carry the identical six**, with no
subclass overriding any of them, so a call through one of those slots on
an Entity is unambiguous. `+0x50` and `+0x54` are already known — they
are what the `SetRadius` and `SetRadius2` script bindings call — which is
what makes the neighbouring pair worth reading as part of the same thing.

### `Entity+0x8c` is "this entity is solid"

The value comes from the model, not the placement. `Entity::Init`
(`FUN_100610e4`) looks the placement's `entities.txt` descriptor up and
copies three fields off a per-model table at `engine+0x6f38`:

```c
setSolid  (this, *(u8  *)(engine + 0x6f38 + descriptor->modelIndex*8 + 0));  /* +0x4c -> 0x8c */
setRadius (this, *(u16 *)(engine + 0x6f38 + descriptor->modelIndex*8 + 2));  /* +0x50 -> 0x8e */
setRadius2(this, *(u16 *)(engine + 0x6f38 + descriptor->modelIndex*8 + 4));  /* +0x54 -> 0x90 */
```

and that table is filled by `ZoneModelList_Load` from columns 2, 3 and 4
of `<zone>_models.txt` — the three fields `ZONE_FORMAT.md` had as
"flag1/flag2/flag3". They are a **collision box**: a solid flag and two
half-extents in the usual 8.8 world units, 256 to a tile.

The shipped data reads unmistakably:

```
4  2   64   64 bottle.bin      a quarter-tile prop
5  2  128  128 sbarrel.bin
6  2  160  160 pinetree.bin
7  2   64  256 door.bin        thin one way, wide the other
8  2  256  256 table.bin
9  0 1500 1500 roof.bin        huge extents, deliberately NOT solid
12 2   64  512 rail.bin        a fence run
18 2  128  128 rat.bin         a creature has one too
25 0    0    0 dagger.bin      a pickup blocks nothing
```

Across all 21 zones column 2 is **only ever 0 or 2** — never 1 — and
every reader tests it for non-zero only. That is also why the save writes
this one-byte field through the stream's **i32** overload (write slot
`0x18`, `SAVE_FORMAT.md`): it is the only place in the entire image a
one-byte field does that, which means the member's declared type is
neither `TBool` nor `TUint8` nor `char` (each of which has its own
overload) but something that promotes to `TInt` — an enum whose second
constant is 2.

### The five readers are the whole of entity collision

Every non-wall collision path in the engine is gated on it:

- **`FUN_100017c8`** — movement. After pushing the mover out of wall
  tiles, it walks the entity list of the tile it stands on
  (`engine+0x6904`, linked through `Entity+0x40`) and for each candidate
  requires `solid && halfExtentX && halfExtentY && !passable`. The mover's
  own box uses `+0x8e` on **both** axes; only the candidate gets a
  separate X and Y.
- **`FUN_10001fd0`** — does a stance change still fit. Sets the actor's
  own `+0x8e` to `0x80` for every stance it accepts, then runs the same
  box test and reverts the stance on a hit. (So the player's half-extent
  is 128 — exactly half a tile.)
- **`FUN_10000ab8`** — is this tile occupied by something solid.
- **`FUN_10066204` / `FUN_1006640c`** — the tile stamp and its inverse,
  below.

The overlap predicate is `FUN_1001c48c`, `<=` on all four edges (so two
boxes that merely touch count), and the box slides with
`FUN_1001c458`. The resolution in `FUN_100017c8` is axis-separated:

```c
if (Overlap(mover, other)) {
    y -= dy;  x -= dx;  MoveBox(mover, -dx, -dy);   // back the whole move out
    x += dx;            MoveBox(mover,  dx, 0);      // X alone
    if (Overlap(mover, other)) { x -= dx; MoveBox(mover, -dx, 0); dx = 0; }
    y += dy;            MoveBox(mover, 0,  dy);      // then Y alone
    if (Overlap(mover, other)) { y -= dy; MoveBox(mover, 0, -dy); dy = 0; }
}
```

— which is what lets you slide along a table instead of sticking to it,
and is the same shape this port already used for walls.

### `Entity+0x92` is "wider than the tile it stands on"

`GameEngine_InitLevel`, once the level's entities exist:

```c
if (getSolid(e) && (getRadius(e) > 128 || getRadius2(e) > 128)) {
    setStamped(e, 1);
    FUN_1001c0f4(engine, e);        /* an empty function in this build */
}
if (getStamped(e)) FUN_10066204(e, 4, 0);
```

`FUN_10066204` walks the box *rotated by the entity's heading*
(`+0xb6`, through the same fixed-point trig table the renderer uses),
converts each step to tile coordinates and ORs bit 2 into the flags byte
of every tile cell it covers (`engine+0x6908`, byte `+1`).
`FUN_1006640c` is the same walk in reverse. So the engine splits its
entity collision two ways: anything up to half a tile is tested
per-entity from the tile's own list, and anything bigger is **baked into
the tile grid** and blocks like a wall. `+0x92` is the record of which
side of that split an entity fell on — and the save loader
(`FUN_100188dc`) recomputes it from the extents when it re-creates a
saved entity, which is why it round-trips.

It is also read defensively: `FUN_10005d60`, which deactivates an entity,
zeroes its whole motion and orientation set and its box and sets
`passable` — but **only when `+0x92` is clear**, because a stamped
entity's bit is already in the grid and clearing its box would strand it
there.

### Two smaller things that fell out

- **`+0x98`, `+0xa0` and `+0xa6` are the per-tick movement delta** for x,
  y and z — the partners of `+0x94`/`+0x9c`/`+0xa4`, which the
  constructor zeroes right beside them and which `FUN_100017c8` subtracts
  and re-adds one axis at a time. That closes the three gaps in the
  position block.
- **`FUN_1001c0f4` is an empty function**, so whatever the original
  wanted to do alongside stamping an entity into the grid was compiled
  out of this build.
- **Which way a heading points (M61, ~~corrected~~ in M71).** `FUN_100063f0`
  reads `+0xb6` and, against the 2048-entry sine table, adds

  ```c
  entity->dx /* +0x98 */ += speed * sin(heading + 0x4000);   /* == +cos(heading) */
  entity->dy /* +0xa0 */ += speed * sin(heading + 0x8000);   /* == -sin(heading) */
  ```

  M61 read this as "walk forward" and concluded the forward vector is
  `(+cos h, -sin h)`, hence `yaw = -heading` for the port's own
  `(cos yaw, sin yaw)` camera. **That function is `Strafe`, not `Walk`.**

  > **M71 correction.** The forward one is `FUN_100065b4`, which takes its
  > angle from the virtual getter at `vtable+0x11c` rather than the raw
  > field and adds
  >
  > ```c
  > entity->dx += speed * sin(heading);              /* == +sin(heading) */
  > entity->dy += speed * sin(heading + 0x4000);     /* == +cos(heading) */
  > ```
  >
  > so **forward is `(sin h, cos h)`** — a quarter turn off what M61 read.
  > `FUN_100063f0`'s `(cos h, -sin h)` is that vector turned a quarter turn
  > the other way, which is exactly what a sidestep is. The four movement
  > functions are two mirrored pairs, and they split on which vector they
  > use, not on sign alone:
  >
  > | function | delta | what it is |
  > |---|---|---|
  > | `FUN_100065b4` | `+(sin h, cos h)` | walk forward |
  > | `FUN_10006510` | `-(sin h, cos h)` | walk backward |
  > | `FUN_100063f0` | `+(cos h, -sin h)` | sidestep |
  > | `FUN_10006480` | `-(cos h, -sin h)` | sidestep, the other way |
  >
  > (The two walk functions take their angle from the virtual getter at
  > `vtable+0x11c`; the two sidesteps read `+0xb6` directly.)
  >
  > The **renderer agrees independently**, which is what settles it:
  > `Render3DScene` builds the camera matrix as
  > `FUN_10073760(engine+0x5d8, -roll, -pitch, -heading)` and that
  > function's depth row evaluates, for a level camera, to
  > `[sin h, 0, cos h]` against its `(worldX, up, worldY)` input — the same
  > forward vector. So the relation to this port's camera is
  >
  > ```
  > yaw = pi/2 - heading          (and, being a reflection, its own inverse)
  > ```
  >
  > which is the conversion `sk_bindings::PortYawFromEngineYaw` had already
  > derived in M48 from a completely different function (the spell spawn's
  > `vx = sin(yaw); vy = cos(yaw)`). Three derivations, one answer. The
  > practical consequence of the M61 version: the port spawned the player
  > facing ninety degrees away from where the level designer pointed them,
  > and — because `PlacementYawRadians` was fitted against the *same* wrong
  > relation — drew every placed prop mirrored about the world Y axis, i.e.
  > exactly backwards at headings 0 and 180 and correct at 90 and 270. See
  > `RENDERER_3D.md`'s "The actor placement transform, in full".
  >
  > Whether a heading runs clockwise or counter-clockwise is a statement
  > about the world axes' handedness and is unchanged; only the zero
  > reference moved.

### In the port

`port/src/world/model_collision.h` holds the table, the two predicates
(`blocks()` and `tileStamped()`) and the decompiled box overlap;
`main.cpp` loads it per zone beside the other manifests and tests the
player's move against every solid door, creature and prop.

Two departures, both deliberate. This port walks the live instance lists
rather than a per-tile list plus a stamped grid — the same set of
blockers, reached differently — and it uses the axis-aligned box for
everything, where the original's stamp uses the box rotated by the
entity's heading, so a rotated fence blocks a slightly different
footprint here.

What this changed in practice: doors and creatures previously used two
invented body radii (90 and 40), and **static props had no collision at
all** — every barrel, table, rock, tree and fence in the game was
walk-through. In azra alone, 266 of 280 placements are solid (104 pine
trees, 36 rats, 25 barrels, 16 chairs, 8 crates, 8 rocks, 7 tables, 7
doors, 5 fence rails, ...), and 141 of those are large enough that the
original bakes them into the tile grid.

Smoke test `src/tests/m55_model_collision_smoke.cpp`.

## `FUN_1002b430` is the map, and here is all of it (M57)

The hypothesis this doc has carried since the `Map_GetTileAt` pass —
"probably the automap, left un-renamed pending more evidence" — is
confirmed, and the whole feature is now recovered end to end. It is
**not a screen**: no screen mode, no `.s` script, no menu entry. It is a
HUD overlay drawn straight over the 3D view.

### The switch

One boolean on the player, `player+0x3a4`, and one three-line function
that flips it:

```c
void FUN_1001ee50(int player) {
    *(bool *)(player + 0x3a4) = *(char *)(player + 0x3a4) != '\x01';
}
```

`FUN_1001ee50` has exactly one caller — the player's per-frame input
poll `FUN_1001c9c0` — and it is edge-triggered on **logical action 9**:

```c
if (GetBoundButton(input, 9) && !GetBoundButtonPrev(input, 9))
    FUN_1001ee50(player);
```

Action 9 is `"Map Toggle"` (string `0xd04`), default key **Key 9** — see
[`INPUT_HANDLING.md`](INPUT_HANDLING.md), whose default-scheme table now
carries the real action-index column this milestone recovered from
`FUN_1001a220`.

The draw side is gated on the same flag, inside the in-game HUD pass of
`FUN_100149b8`, after the compass/vitals/hand-icon draws:

```c
if (*(char *)(*(int *)(engine + 0x618) + 0x3a4) != '\0') FUN_1002b430(controller);
```

Nothing pauses. The map is an overlay and the game keeps running under
it.

### The picture

- An optional full-screen backdrop from sprite slot **20**
  (`engine+0x44b0`; the slot array's base is `engine+0x4460`, pinned in
  M54 via slots 205/206, so `(0x44b0 - 0x4460)/4 = 20`). Every zone's
  `<zone>_sprites.txt` requests it.
- The **zone display name**, centred at y=10, white, with a one-pixel tan
  shadow. `FUN_100290a8` is the engine's own internal-name → display-string
  lookup, i.e. the same table M26 recovered for the loading screen.
- A **64×64-tile window centred on the player's tile**, drawn at 2×2
  pixels per tile from screen (24, 40) — so 128×128 pixels.
  **Larger world Y is up**: the tile loop counts world Y *down* from
  `py + 32` while the screen row counts up.
- Then the player marker.

The per-tile colour, with the `+ 5` every in-bounds arm adds folded in:

| condition | colour |
|---|---|
| off the grid, or never seen | `0x0ca5` |
| seen, and `cell.u16 & 0x2402` | `0x0868` |
| seen, open, no floor-height change | `0x0db5` |
| seen, open, floor height changes | `0x0a85` |
| the player marker | `0x0fff` |

Out-of-bounds and unexplored are deliberately the *same* colour — the
default is `0xca5` outright and the unexplored arm is `0xca0 + 5` — so
the edge of the world and the edge of your knowledge are indistinguishable
by design.

The height compared is the `.zcp` entry's signed 16-bit at byte 2 (this
port's `ZcpEntry::floorBandThreshold`), and it is compared against the
tile **diagonally ahead**, `(x+1, y+1)`, not a cardinal neighbour. The
engine bounds-checks the first lookup and not the second.

**These colour literals settle
[`GRAPHICS_FORMAT.md`](GRAPHICS_FORMAT.md)'s long-open question about the
exact 16-bit pixel format.** They are written *straight into the
framebuffer*, and they only read as sensible map colours in 0x0RGB
4-bit-per-channel (Symbian `EColor4K`); as RGB565 `0x0ca5` would be a dark
green. `FUN_1008f97c` confirms it independently by unpacking its own
colour argument as `R = ((c>>8)&0xf)<<4`, `G = ((c>>4)&0xf)<<4`,
`B = (c&0xf)<<4`.

### The blocking mask, and a bit it turned up

`0x2402` is the only occurrence of that literal in the image, and it is
applied to the cell's **first two bytes as one u16** — so bit 1 is
`ZmpCell::flags` bit 1 (wall) and bits 10 and 13 are `blockFlags` bits 2
and 5. Both of the latter are richly authored on disk, which a first
draft of this work assumed they were not:

- **bit 2** — 40,517 cells across all 21 zones, 36,332 of them *not*
  walls. The same bit M44 found `LockZone` assigning and M55 found the
  entity tile-stamp ORing, so authored blocking, a locked gate and a prop
  too wide for its own tile all converge on one flag.
- **bit 5** — 13,064 cells across exactly six zones (fearfrst 9859,
  erthcave 1681, broken2 588, lothcav 433, delfhide 324, ffarena 179),
  in compact regions, and **never once on a wall cell**. Its only other
  reader is `FUN_100001ac`, which tests it during movement and, when the
  actor's own Z is at or below that cell's floor height, makes a virtual
  call — a surface you sink into rather than geometry you collide with.
  Water or a hazard pool is the natural reading and the distribution fits
  (fearfrst is 60% of its grid); naming it properly is left open. What is
  certain is that the map paints it solid.

### The player marker

Three white lines, all from (0x58, 0x68) — the centre of the drawn area,
which is where the player's own tile lands — via the DDA line drawer
`FUN_1005e310` (8.8 fixed point, `max(|dx|,|dy|)` whole-pixel steps,
stride 0xb0 = 176, clipping only the bottom edge).

The angles come from a **2048-entry sine table at `0x100f4954` whose
every entry is exactly `round(256 · sin(2πi/2048))`** — checked against
all 2048, zero deviation. With `a = -0x8000 - heading` (the engine's
0x10000-per-turn unit, `Entity+0xb6`), indexed `a >> 5`:

- one leg at `a`, radius `sin >> 5` → 8 pixels;
- two legs at `a ± 45°`, radius `sin >> 6` → 4 pixels.

The decompile looks asymmetric because one subexpression is shared:
`sin(a+45)` is the first short leg's X *and* the second's Y. And the two
short legs are not exact mirrors — an arithmetic `>> 6` rounds a negative
away from zero and a positive toward it, so at heading 0 they land at
(91, 107) and (86, 107) around a long leg ending at (88, 112).

A second, multiplayer-only block draws up to two other players as 2×2
blocks, gated on `engine+0x5c0`.

### The explored bitmap: what makes it a map

`registry+0x45c`, allocated `width * height / 8` bytes and zeroed by
`FUN_1002f890`, indexed exactly:

```c
bitmap[(width >> 3) * y + (x >> 3)] & (1 << (x & 7))
```

**`TileGrid_RaycastVisibility` is what fills it in** — the same per-frame
fan of rays that decides which tile faces to draw ORs a bit in for every
tile a ray passes through, every frame, whether or not the map is open.
So the map shows precisely the set of tiles that have at some point been
rendered. That is the reveal-as-you-go mechanic this doc guessed at, now
confirmed from both ends.

Two details of the original worth recording:

- It is allocated **once** (`if (registry+0x45c == 0)`) and never
  resized, so a later, larger zone would index past a buffer sized for
  the first. This port sizes it per zone instead — the one deliberate
  departure.
- It is **saved and restored** (`FUN_1002fa5c` / `FUN_1002f934`): map
  width, map height, then `w*h/8` raw bytes — and immediately after them,
  in the same record, the u16 at `registry+0x468` and the u16 at
  `registry+0x478`. Those are M56's key-item flag word and its frozen-key
  counter, which is an independent confirmation of that milestone's
  reading, including the counter's width.

## The animated-sprite entity, and the world's second art source (M63)

`Level.CreateEffect(...)` builds one of these; so do the spell
projectile's tick (the blaze impact), `FUN_10081e5c` (the blood spurt) and
`FUN_10067f84` (the effect a live entity carries around). It is the
engine's **billboard**: a 0x160-byte actor that draws a `global.spr`
sprite rather than a `models.idx` mesh, and that registers through the
same `FUN_1001c080` and appears in the same entity lists as everything
else in a zone.

That is the thing worth recording here beyond the binding itself: this
world has **two** art sources, not one. Every entity this document has
described so far is a 3D model — a creature, a door, a prop. A billboard
entity is the other kind, and its selector (`+0x134`) is a plain index
into the loaded-slot pointer table at `engine+0x4460`, i.e. into the same
384-slot `global.spr` cache the menus draw from (`GRAPHICS_FORMAT.md` has
the table of world-facing slots).

**The layout** (constructor `FUN_10060744` over the sprite base
`FUN_1008b420`):

| Field | Meaning |
| --- | --- |
| `+0x134` | the `global.spr` slot drawn this frame |
| `+0x140` / `+0x144` | first / last slot, 8.8, inclusive |
| `+0x148` / `+0x14c` | animation rate / cursor, 8.8 |
| `+0x150` | gravity flag |
| `+0x151` | loop flag (constructor sets it; nothing clears it) |
| `+0x152` | "no lifetime" |
| `+0x13c` / `+0x15c` | lifetime remaining / initial |
| `+0x138` | scale, ramped `+0x154` → `+0x158` over the lifetime |
| `+0x12c` / `+0x130` | the two size scalars, multiplied by the sprite's own pixel dimensions |

**The tick** is `FUN_1005ef94`: count the lifetime down by the engine's
per-frame delta and destroy at zero (unless `+0x152`), advance the frame
(`FUN_100606d0`, which loops or destroys at the end of the range),
integrate `x`/`y`/`z` by `+0x98`/`+0xa0`/`+0xa6` with an optional gravity
term on `+0xa6`, then interpolate the scale. Everything else in the class
is inherited actor state.

**The draw** is `FUN_1008b25c`, and it is the first thing in this document
that reads the camera-space triple `+0xbc`/`+0xbe`/`+0xc0` for something
other than culling. `FUN_1001610c` fills those by rotating the entity's
offset from the player through the camera matrix at `engine+0x5d8..0x600`
and shifting down 8. The billboard spans `±halfWidth` horizontally about
`+0xbc` and runs *upward* from `+0xbe` by `2 * halfHeight`, so it is
**bottom-anchored** at the entity's own z — the same anchoring M62's floor
snap assumes for an actor.

## The vitals economy: what spends the three pools, and what refills them (M73)

The HUD's three bars have been drawn since M10 and the stats block behind
them decoded since M43, but nothing had traced what *moves* them outside a
spell. The answer is six small functions, five of them hanging off the
player's own vtable, and one of them the reason the bars are not a one-way
ratchet.

### The stats-block fields

Everything here indexes the shared stats block (`player+0x3ac`), whose
field map is pinned by the Character-stats dispatcher `FUN_10048244` — its
`param_1` is a `short*`, so `param_1[n]` in the decompiler's C is byte
offset `2n`, and the dispatcher's own `GetStrength`/`GetMaxHealth`/... arms
name each one:

| offset | field | | offset | field |
| --- | --- | --- | --- | --- |
| `+0x14` | Strength | | `+0x24` | max health |
| `+0x16` | Intelligence | | `+0x26` | max fatigue |
| `+0x18` | Agility | | `+0x28` | max magicka |
| `+0x1a` | Willpower | | `+0x2a` | health |
| `+0x1c` | Speed | | `+0x2c` | fatigue |
| `+0x1e` | Endurance | | `+0x2e` | magicka |
| `+0x20` | Personality | | `+0x34` | level |
| `+0x22` | Luck | | `+0x3c`/`+0x3e`/`+0x40` | the three regen accumulators |

The last row is new this milestone. `+0x2c` being fatigue and `+0x2e`
magicka was already established (M48); the three accumulators sit just past
the level and are touched by exactly one function.

### The four things that spend fatigue

| what | function | vtable slot | cost |
| --- | --- | --- | --- |
| jump | `FUN_10044400` | `+0x234` | 5, and refused unless `fatigue > 5` |
| attack | `FUN_100425bc` | `+0x288` | 4 |
| cast | `FUN_10042394` case 2 | `+0x280` | 3 |
| move | `FUN_10045228` | `+0x1dc` | 2 per `0xc4` delta units |

Three details are worth stating because each is visible in play.

**The attack's 4 is spent up front.** It is the first thing `FUN_100425bc`
does after its two cooldown gates — before the target search, before the
melee/ranged split, before any to-hit roll. A whiff, a bare-fisted swing
and a connecting sword blow all cost the same, and a bow costs it too.

**The cast's 3 is spent last.** `FUN_10042394`'s case 2 charges it only
after both the refire gate `FUN_10046680` and the cast `FUN_10046764` have
returned success, so a cast refused for want of magicka is free. Spells
never reach `FUN_100425bc` at all — the use-item dispatcher is a different
vtable slot — which is why the two costs do not stack.

**The move cost is a time drain on a hook, not a per-key charge.**
`FUN_10045228` is the whole of it:

```c
player->moveAccum += frameDelta;          // player+0xb30
if (player->moveAccum > 0xc4) {
    SetFatigue(fatigue - 2);
    player->moveAccum = 0;
}
```

Slot `+0x1dc` is a hook the *base* actor movement calls: `FUN_100063f0`
(forward), `FUN_10006480` (back), `FUN_10006510` (strafe left) and
`FUN_100065b4` (strafe right) each end by invoking it, and the player's own
overrides (`FUN_1001f2b4`..`FUN_1001f338`) are each two calls — the base
move, then a second hook. So the drain fires **once per direction actually
moved this frame**: holding forward and a strafe together genuinely drains
twice as fast. It also fires whether or not the step is then blocked,
because the base move writes the position unconditionally and collision
resolves afterwards in `FUN_100017c8`.

There is no walk/run distinction to key any of this off. The engine has one
movement speed, and the one thing that looks like a sprint —
`engine+0xbe0c`, which doubles the step — is written to 0 in the engine
constructor and never written again.

### The two things an empty pool costs

**Half movement speed.** `FUN_100445d4` (slot `+0x1e4`) is the per-frame
step length every move slot is handed:

```c
step = Speed << 8;
step >>= (fatigue < 1) ? 2 : 1;
if (step > 0x3fff) step = 0x4000;
```

— one extra bit of right shift, i.e. exactly half.

**Half melee damage.** In `FUN_100425bc`'s melee branch, immediately after
the weapon's own `RandomRange(damageMin, damageMax)`: `if (fatigue < 1)
damage >>= 1`. It lands on the raw roll, ahead of the defender's
mitigation. The ranged branch has already returned by that point, so **an
arrow is not weakened by exhaustion** — only a swing is.

### Regeneration

`FUN_10049b64`, called from the player's own per-frame tick
`FUN_10045294` immediately after the status-effect tick `FUN_10049780`. It
has **exactly one call site in the binary** and that is it, so regeneration
is the player's alone: a wounded creature stays wounded.

Three independent accumulators, each `+= frameDelta` every frame, each with
its own period; whichever passes its period resets to 0 and — only if that
pool is below its own maximum — adds an attribute-derived amount through
the same clamp the ordinary setters use:

| pool | accumulator | period | amount |
| --- | --- | --- | --- |
| health | `+0x3c` | `> 0x400` (4 s) | `Endurance / 25` |
| magicka | `+0x3e` | `> 0x200` (2 s) | `Willpower / 15` |
| fatigue | `+0x40` | `> 0x200` (2 s) | `(Strength + Willpower) / 15` |

Neither divisor is a literal in the shipped code. `/25` is
`0x51eb851f` with `smull` + `asr #3` on the high word (0x10049c28); `/15`
is `0x88888889` used *signed*, with the `add r2, r2, r5` correction before
the same `asr #3` (0x10049d34 and 0x10049e1c). Both were read off the
disassembly rather than trusted from the decompiler, because `0x51eb851f`
reads like the reciprocal of 100 at a glance and is not.

Three modifiers ride on top, all gated on the owner being the player (the
function reaches back through `stats+0x50` and calls its `vtable+0xcc`
predicate first):

* a **High Elf** (race 3) adds `raceAbility * 5` to the willpower term,
  before the divide;
* a **Breton** (race 1) adds the same to the strength+willpower term;
* carrying **`items\azras_bandage.s`** (entities.txt typeId 4702) adds a
  flat +3 to the health tick, tested with `FUN_10045474`, which walks the
  two hand slots (`stats+0x48`/`+0x4c`) and the eight equipment slots at
  `player+0xf8c`.

And one that does not ride on top. At 0x10049c1c the health branch loads
the owner's race and compares it against 7 (Wood Elf) — and then never
reads the flags:

```
10049c1c  ldr   r3, [r4, #0x50]
10049c20  ldr   r2, [r3, #0xf3c]
10049c24  cmp   r2, #7
10049c28  ldr   r3, [pc, #0x238]     ; the /25 multiplier -- both paths land here
```

A Wood Elf health bonus was written and compiled away. The shipped game has
none. It is invisible in the decompiler's C, which renders the dead compare
as a discarded call.

### What the numbers actually mean

Both rates are fixed by the periods, so at the engine's own 25 Hz they come
out as flat figures:

```
drain, walking straight    2 per 20 ticks   = 2.50 fatigue/s
drain, walking diagonally  4 per 20 ticks   = 5.00 fatigue/s
regen, 50 Str / 50 Wil     6 per 52 ticks   = 2.88 fatigue/s
```

which means **a starting character cannot walk themselves tired in a
straight line** — regeneration is very slightly ahead of the drain. Hold a
strafe as well and the pool empties in about 45 seconds. Fatigue in this
game is spent by fighting and jumping; walking only bleeds it when you are
also sidestepping, or when strength and willpower are low (a Dark Elf,
High Elf, Khajiit or Wood Elf starts at 4 per 2 s and loses ground even
walking straight).

Health is the slow one by design: `Endurance / 25` every four seconds is
1 or 2 points for every shipped starting character, so a full heal from
near death takes minutes of standing still. It is a rest, not a heal.

## The item type, the equip slot, and the class gate (M74)

Three things an inventory item carries that nothing had read, and all three
turn out to hang off one column of `entities.txt`.

### `GetItemType()` is a stored word, not a deduction

`FUN_1006d508` is the whole of it — `return entity->+0x16c` — and it is
what sorts an inventory. The five `Display*Page` natives (Store/shop
dispatcher `FUN_10033660`, cases 4..8) each pick a number and hand it to
`FUN_10032f78`, whose entire filter is one line:

```c
if (FUN_1006d508(item) == page) { ...add the row... }
```

| native | page |
| --- | ---: |
| `DisplayMiscItemsMenu` | 0 |
| `DisplayWeaponsPage` | 1 |
| `DisplaySpellsPage` | 2 |
| `DisplayArmorMenu` | 3 |
| `DisplayConsumablesMenu` | 4 |

`+0x16c` is a per-C++-class constant, written by the class's constructor,
and the class is chosen by the **`entities.txt` category** through the
factory `FUN_1002aa14` (the seventeen-arm table above). Eight of its arms
are item classes:

| cat | constructor | `+0x16c` | `+0x1c0` | what it is |
| ---: | --- | ---: | ---: | --- |
| 3 | `FUN_1002eeb8` | 0 misc | 2 none | keys, quest trinkets |
| 4 | `FUN_1002ce9c` | 1 weapon | 1 right | `weapons\*` |
| 5 | `FUN_10047740` | 2 spell | 1 right | `spells\*`, `blaze.s` |
| 6 | `FUN_1002e954` | 3 armor | 2 none | `armor\*` |
| 9 | `FUN_1002e78c` | 4 consumable | 0 left | `items\*` |
| 14 | `FUN_1002e5ac` | 2 spell | 1 right | scrolls |
| 15 | `FUN_1002e844` | 3 armor | 2 none | shields |
| 16 | `FUN_10047500` | 1 weapon | 1 right | the conjured sword |

Categories 14 and 16 are derived classes and inherit what they do not set:
14 runs `FUN_10047740` first and only adds the scroll flag `+0x1d4`, 16 runs
`FUN_1002ce9c`. The base item constructor `FUN_1006c960` writes `1`, which
is why every other arm overwrites it a few instructions later.

The shipped data agrees column for column: 80 of category 4's 83 rows are
under `weapons\`, 28 of category 5's 31 under `spells\`, 77 of category 6's
89 under `armor\`, 55 of category 9's 58 under `items\`, all 7 of category
14 under `spells\`, and 9 of category 15's 10 under `armor\`.

### `+0x1c0` is which hand the item wants

`FUN_1002ed74` returns it: **0 = left, 1 = right, 2 = neither**. Confirmed
by the two writers it feeds — `vtable+0x2c` and `vtable+0x30` on the stats
block, which the Character-stats dispatcher's own `SetLeftItem` (case 0x13,
`stats+0x48`) and `SetRightItem` (0x14, `stats+0x4c`) name.

So weapons and spells are right-hand items, consumables left-hand ones, and
armour and misc items name no hand at all.

### Picking something up equips it

The tail of the player's add-to-inventory slot, `vtable+0x164` =
`FUN_1003d8e0`:

```c
if (!IsItemEnabledFor(player, item)) return;
slot = item->+0x1c0;
if (slot == 1) { if (stats+0x4c) return; SetRightItem(item); q = player+0xf68; }
else if (slot == 0) { if (stats+0x48) return; SetLeftItem(item); q = player+0xf4c; }
else return;
appendToHandQueue(q, item);
```

An occupied hand means the item is simply not equipped — the function
returns before it even reaches the queue. Everything funnels through this
one slot: a loot menu's `PickupItem`, a purchase (`FUN_1003e030` →
`FUN_10045078`, a one-line forward), and `EquipItem`.

**`EquipItem(hand, item)` ignores its hand argument.** The shipped handler
(Player case 0x54, 0x10041e9c) reads it with `SIMKIN_AtomToInt` and drops
the result on the floor — the disassembly discards r0 immediately — then
walks the inventory and calls `+0x164` if the item is not already in it. So
the `0` that every one of the game's 34 `EquipItem(0, self)` call sites
writes is decoration, and a spell still arms the right hand.

### `UpdateEquipStatus`, and where the "3" comes from

`FUN_10033660` case 1, which `inventory.s`'s `PerformEquipAction()` calls:

```c
if (!IsItemEnabledFor(classRow, item)) return 3;
if (type == 3) { toggleWorn(item); }              // armour
else {
    if (leftHand  == item) { SetLeftItem(0);  dequeue; return 1; }
    if (rightHand == item) { SetRightItem(0); dequeue; return 1; }
    slot = item->+0x1c0;                          // its own hand, not a free one
    ...
}
return 1;
```

Two things worth stating plainly: the **3** is the class gate and nothing
else — no item type is ever "not equippable" — and the hand is the item's
own, not whichever is free. Return codes are 1 (ok), 2 (the hand queue is
full) and 3 (refused); 0 is never returned.

### `FUN_1001f82c` — may this character use this item?

The gate in front of all of it, and the one consumer of the three unnamed
mask words in the class table (`FUN_1001f9ac`, the nine 0x10-byte rows).
`param_1` is the class row as a `ushort*`, so `[0]` is `+0`, `[1]` `+2`,
`[2]` `+4`, `[3]` the `HasMagic` byte at `+6`, `[4]` the class id at `+8`.

```c
type = item->+0x16c;
if (type == 2) {                                    // spell
    if (!item->+0x1d4) {                            // not a scroll
        if (!row->hasMagic) return false;
        mask = item->+0x1cc;                        // RestrictUse's bits
        if (mask) return (mask & (2 << row->classId)) != 0;
    }
}
else if (type < 3) {                                // weapon (or misc)
    if (type == 1 && row->field2 != 2
        && !(weaponClass == 0x400 && row->hasMagic)
        && !(weaponClass == 0x800 && row->hasMagic)
        && weaponClass != 0
        && (row->field2 & weaponClass) == 0) return false;
}
else if (type == 3) {                               // armour
    constraint = item->+0x1d4;
    if (!item->isShield()) {                        // vtable+0x180
        if (item->+0x1d0 == 7) return true;         // the "no slot" armour type
        ok = row->field0 & constraint;
    } else {
        if (row->field4 == 4) return true;
        if ((row->field4 & 0x18) && constraint == AR_Light) return true;
        ok = (constraint == AR_Medium) ? ((row->field4 >> 4) & 1) : 0;
    }
    if (!ok) return false;
}
return true;
```

`RestrictUse(...)` (Spell dispatcher case 5) is what fills `+0x1cc`: one bit
per argument, `2 << classId` (`FUN_10020904` is `(0x20000 << c) >> 16`).
`blaze.s`'s own `RestrictUse(2, 4, 6, 7)` stores 0x1a8 — Battlemage,
Nightblade, Spellsword, Sorcerer, which is **exactly** the four classes the
class table independently marks `HasMagic`.

Read against the class table the three masks make immediate sense:

| class | `field0` armour | `field2` weapons | `field4` shields |
| --- | --- | --- | --- |
| Assassin | Light | (unrestricted) | none |
| Barbarian | Light, Medium | (unrestricted) | any |
| Battlemage | Light | (unrestricted) | Light, Medium |
| Knight | Light, Medium, Heavy | (unrestricted) | any |
| Nightblade | Light | a mask | Light |
| Rogue | Light, Medium, Heavy | a mask | Light, Medium |
| Spellsword | Light, Medium | (unrestricted) | Light, Medium |
| Sorcerer | Light, Medium | a mask | none |
| Thief | Light | a mask | Light |

`field2 == 2` means "no weapon restriction", which is five of the nine. The
two weapon bits exempt for any magical class are `WR_EnchantedBlade`
(0x400) and 0x800.

This is also what makes a spell script's two use texts a real fork rather
than decoration. Every spell in the game ends its `Init()` with

```
if (GetPlayer().IsItemEnabledFor(self) = true) SetUseText(404);
else                                           SetUseText(405);
```

and for `blaze.s` those two strings are **"Learn Blaze"** and **"Pickup
Blaze Scroll"**.

## Talking to people: one byte, and the object called `Level` (M75)

Reported from play: walking up to an NPC never produced the prompt that
starts a conversation, and the merchants never opened a shop. Three
independent defects sat behind that one symptom.

### `entity+0xd8` is the whole of "you can interact with this"

The engine has exactly one gate, and both halves of the interaction read
it. The search that decides what the prompt is *about* is `FUN_1001dd40`,
called once per frame out of `Render3DScene` (0x10017b60) and parked in
`engine+0x61c`. It fans **twelve rays** out of the player's heading —
`heading + 0x600` stepping down by `0x100` — marches each one up to twelve
half-steps, walks the entity list of every tile it crosses, and stops at
the first wall (`.zmp` blocking bit 2):

```
for (e = tile->firstEntity; e; e = e->next)
  if (e->+0xd8 != 0) {                              /* the gate */
    dz = |player->+0x224 - e->+0xa4|;               /* eye Z vs feet Z */
    if (dz <= (player->+0x1e1 == 3 ? 0xc0 : 0x300))
      return e;
  }
```

and the *action* is `FUN_100646a8`, which is

```
if (entity->+0xd8 == 0) return false;
entity->vtable[0xa8]("OnUse");
return true;
```

So "no prompt appears" and "pressing Use does nothing" are the same
condition, not two.

Three things write that byte:

- **The constructor.** The base entity (`FUN_10060d54`) zeroes it, and two
  of the seventeen factory arms write 1 back: category 4, weapons
  (`FUN_1002ce9c`, the store at 0x1002cf6c) and category 9, consumables
  (`FUN_1002e78c`, at 0x1002e7f8). Misc loot, armour, spells, doors,
  containers, creatures and merchants all start off.
- **`SetUsable(b)`** — entity binding 30, dispatcher case 0x1e — assigns
  it, and additionally clears `engine+0x61c` if the entity being switched
  off happens to be the current use target.
- **`SetUseText(id)`**, and this is the rule the port was missing. The
  handler is one store longer than its name suggests (0x10068418):

  ```
  str  r1, [r0, #0xdc]     ; useTextId  = id
  mov  r3, #1
  strb r3, [r0, #0xd9]     ; hasUseText = 1
  strb r3, [r0, #0xd8]     ; usable     = 1
  bx   lr
  ```

  That one function is `vtable+0x8c` in **31 of the game's entity
  vtables** — every class — so naming a use text *is* how a thing becomes
  usable, universally.

A fatal hit clears it again: the creature damage path (0x10083c04) ends
with `strb r3(0), [sl, #0xd8]` at 0x10083e2c, beside the `+0xd5` dead flag.

The reader is `0x1006842c` (`vtable+0x90`):

```
if (entity->+0xd9)  text = stringTable[entity->+0xdc];
else                text = stringTable[13];
```

and shipped string 13 is the word `"default"` — a developer placeholder,
which is the same statement from the other side: a usable entity is
expected to have called `SetUseText`. The ids the NPCs pass are simply
their names — 2063 "Gravel Trothgar", 2027 "Menlin", 177 "Almathea", 1199
"Refugee", 835 "Heather".

**Why it matters.** 54 of the 152 talkable creature/merchant placements in
the shipped game — 35% — call `SetUseText(...)` and never call
`SetUsable(true)`. Gravel Trothgar (azra's shop), Acolyte Menlin and
Priestess Almathea (azra's two starting quests), Old Trinket, the four Azra
villager prisoners, Heather, and all four of Dark Star West's merchants are
in that set.

The shipped data corroborates the model from the other direction. Applying
the rule to *every* category and then counting placements that have a real
script: all 404 containers come out usable, and 433 of the 434 doors do.
The single exception is `dstar_e/Cell_Door.s`, whose entire `Init()` is
`SetUsable(false); SetMPUsable(true);` — a prison door opened by a quest
event calling its own `OpenDoor` handler, never by a player walking up to
it. So the byte really is the one gate, and honouring it costs nothing.

### `Level` is the zone-root script object

The port had `Level` as a native-only singleton, like `GetPlayer()`. It is
only half of one, and the corpus settles the other half twice over:

- `crypt2/pedestal_entity.s` calls `Level.AddCrystal()` seven times.
  `AddCrystal[()...]` is defined in **`crypt2.s`** — the zone-root script —
  and in no other file, and it is not in any native binding trie.
- Scripts read and write **168 distinct `Level.<field>` names across 423
  sites**, and **163 of them are declared at the top level of the zone-root
  script of exactly the zone they are used in**: `EndGame_Trinket` and
  `EndGame_Skelos` in `azra.s`, `saved_Birgitta` / `saved_taker` /
  `saved_Given` in `delfhide.s`, `saved_Crys1[0]`..`saved_Crys7[0]` in
  `crypt2.s`, and so on for all 21 zones. Inside `crypt2.s`'s own
  `AddCrystal`, those same variables are written bare (`saved_Tele1 = 1`)
  next to `Level.GetEntity("tele1")` and bare `GetEntity("star")`, used
  interchangeably.

So `Level` is a TreeNode-backed script executable whose `method()` also
answers the Zone/Level (`0x14d38`) and zone-effects (`0x14df8`) natives —
the same shape `MonsterExecutable`/`DoorExecutable` already have. That is
consistent with `SAVE_FORMAT.md`'s account of these flags as "ordinary
SimKin instance variables… scoped to that level", as against the eighteen
hardcoded story flags that live on the player.

This is not a cosmetic distinction. An undeclared field read raises a
*runtime error*, not a soft-fail, so it aborts the whole handler — and for
a conversation menu that means its `Init()` never finishes and the menu
never opens. Six real conversations died on the first line of theirs:
`trinketconvo`, `AzraSkelosConvo`, `delfhide/Chef_convo`,
`delfhide/RescueConvo`, `crypt1/azra_final_convo`, and `Talker`.

Five of the 168 names are declared by no zone root at all, and each differs
from a declared name only by case or a digit — `saved_Openswdoor` for
`saved_OpenSwdoor`, `saved_Bread4` where `dstar_w` declares 1..3. They are
shipped typos, and each sits on the first line of a real conversation's
`Init()`. `twilite/azra_zombiedawn.s` has a second one of its own kind:
line 33 reads `Level.saved_Dawn` as a field and line 37 calls
`Level.saved_Dawn()` as a method.

### `GetOpener()` is the object that called `OpenMenu`

The entity classes' `OpenMenu` is `FUN_100779b8(menuManager, name, 1,
self)` — the caller is always the new menu's opener, and 210 corpus call
sites read it back. Two of this port's classes were not passing it:

- A creature's `OpenMenu` passed nothing, so `GetOpener()` was null.
  `fearfrst/Ivgrizt_convo2.s` opens with
  `if (GetOpener().saved_WeTalked = 0)`, where `saved_WeTalked[0]` is
  declared at the top of `fearfrst/Ivgrizt.s` itself — with no opener that
  raised "Cannot get field … from a non-object" and the conversation never
  opened. `ghstpass/Trailslag_convo.s`, `StoutTP/OldTrinketConvo3.s` and
  `erthcave/EC_Menu3.s` fail the same way.
- A door had no `OpenMenu` handler at all, which is why no lock in the game
  could be picked: `lockeddoor.s`'s `OnUse()` is
  `OpenMenu("Menus\\UsePicks")` and nothing else, and `Menus/UsePicks.s`'s
  `Pick` handler is `if (GetOpener() != null) { … GetPlayer().CanDisarmTrap(
  GetOpener().resistDisarm) … }`, reading the `resistDisarm[5]` declared at
  the top of that same door script and calling `GetOpener().LockPicked()`
  on success.

## The NPC that speaks first: `OnDetect` (M76)

M75's section above is the prompt you walk up to and press Use on. This is
the other half of the same conversation system, and it lives somewhere
completely different: inside the creature AI tick, `FUN_10082224`.

### It is the other arm of the aggro branch

The tick branches on the AI package (`monster+0x2a8`). The arm that
matters is "I am looking for something":

```
else if (package == 2 /*idle*/ ||
         (package == 3 /*pursue*/ && monster->+0x20c /*target*/ == 0))
```

and inside it, after the perception test below has passed:

```
if (monster->+0x2ac /*aggressive*/ == 0) {
    if (target == engine->player &&
        self->vtable[0x21c](self, target, self->+0x2dc >> 8, 1) &&
        (self->+0x306 || !multiplayer || isHost))
        if (self->+0x120 /*script*/)
            self->vtable[0xa8]("OnDetect", { target }, ...);   // 0x10083320
} else {
    ... acquire the target, package = 3, close and swing ...
}
```

So **`OnDetect` is what a non-aggressive creature does instead of
attacking** — same distance, same roll, same tick, one `if` apart. Three
consequences fall straight out of that shape, and all three are visible in
the shipped scripts:

- An NPC whose own `Init()` calls `SetAggressive(true)` takes the *other*
  arm forever, so its `OnDetect` is dead. `delfhide/dh_guard_talk.s` ships
  exactly that combination.
- A creature that never calls `AiDetect()` stays in the constructor's
  package −1 (`FUN_100815e0` writes `+0x2a8 = -1`) and the arm is never
  entered at all. `monsters/lakvan.s` and `twilite/pergan_asuul.s` are both
  in that state — pergan's `AiDetect()` is commented out in the shipped
  file — and both are reachable only through the M75 use prompt instead.
- Nothing latches "already detected". The engine re-runs the test every
  tick; the scripts do their own latching, with `AiSleep()` (package −1,
  which the arm no longer matches) or `SetAggressive(true)`. That is why
  `monsters/olpac_trailslag.s` guards on its own `done_menu` field *and*
  calls `AiSleep()`.

The handler receives one argument, the detected entity: the engine boxes
`target + 0x14` — the skiExecutable base sub-object — into an skRValue and
appends it (0x100831ac..0x10083208). Every shipped handler declares the
parameter and none of them reads it; they all call `GetPlayer()` instead.

### The perception test

Three conditions, in the engine's own order.

**Distance.** `vtable[0x5c]` is `FUN_100683d4`:

```
((dy*dy) >> 8) + ((dx*dx) >> 8)          i.e. (dx^2 + dy^2) / 256
```

— the same *scaled squared* form `SetChaseRadius` stores, so the two are
compared directly. The constructor default is `+0x2b8 = 0x7fff`.

**The roll.** Shared with the attack branch: a creature that fails it does
not aggro either.

```
detect = monster->+0x258;  if (detect == 0) detect = 1;
agi    = playerStats->vtable[0x40]();          // the stat at stats+0x18, / 5
r      = (detect << 16) / ((detect + agi) << 8);
if (rand(0, 100) < (r * 100 >> 8))  noticed = false;
```

`monster+0x258` is written to 1 by the constructor and by nothing else in
the binary — no binding, no script, no engine path — so the numerator is
always 1 and the whole thing is "the chance of going unnoticed this tick is
100/(1 + Agility/5) percent". `vtable[0x40]` on the stats block is
`FUN_1004b848` for a creature and `FUN_10044b8c` for the player; both
return the 16-bit field at `stats+0x18` divided by 5, and for the player
(`player+0x3ac+0x18` == `player+0x3c4`) that field is **Agility**. The
player version then adds two equipped-item bonuses (`+0xf38 == 8` adds
`+0xfb0`, `+0xf3c == 2` adds `+0xfb4`), which can only ever make the player
easier to notice. Running every tick is what makes the numbers behave: a
starting Agility of 40 is an 11% miss chance per frame, so detection
happens within a frame or two.

**Sight.** `vtable[0x21c]` is `FUN_10004d70`, and it is a real ray march —
worth spelling out, because this port had a hand-built equivalent
(`Zone::HasLineOfSight`) that had been documented as guesswork:

- start at the caller's *eye*, `(+0x94, +0x9c, +0xa4 + vtable[0x108]())`,
  and aim at the target's eye;
- normalise the direction to 0x100 (one tile) and halve it, so the march
  steps half a tile at a time;
- step through `FUN_100182d0` with flag mask 0xb, which reports a wall
  (`.zmp` blocking bit 2 — the same flag the port's DDA tests) and tracks
  the ray's own z against each tile's floor and ceiling;
- a wall ends it (`if (hit & 2) return 0`); an *entity* in the way does
  not — the caller walks that tile's entity list, returns 1 if the target
  is in it, and otherwise resumes the march past it.

The distance budget is `monster->+0x2dc >> 8` — the raw, *scaled-squared*
`SetAttackRange` value shifted right by 8 — and `FUN_100182d0` spends it in
**tiles** (`while (travelled < budget * 0x100)`, adding 0x80 per half-tile
step). Mixing a squared unit with a tile count is the engine's own
arithmetic, not a transcription slip; `+0x2dc` is compared as a squared
distance everywhere else in the same function. The constructor default
0x6a4 therefore buys 6 tiles of sight, and no shipped script calls
`SetAttackRange`, so that is the number for every creature in the game.

### What it is worth, in the shipped data

**M77 corrected these numbers.** They were computed with a creature that
never calls an `Ai*` binding sitting in the actor constructor's package
-1; placement puts every creature in package 2 before `Init()` runs, so
`AiDetect()` only ever re-states what is already true. The corrected
census -- same corpus, same method -- is **241 of the 1539 placements
non-aggressive and looking, 58 of them with an `OnDetect` the engine
reaches, and 4 with one it cannot**, across 20 distinct scripts rather
than six. What is left dead is one shape only: an `Init()` that calls
`SetAggressive(true)` and so takes the attack arm instead
(`delfhide/dh_guard_talk.s`). In particular `monsters/lakvan.s` and
`twilite/pergan_asuul.s` -- written up below as dead ends for never
calling `AiDetect()` (Pergan's is commented out in the shipped file) --
both work, as do the nine `raiders/*.s` arena creatures.

The six scripts M76 could already reach, which are still the most
interesting ones:

| script | what it does |
| --- | --- |
| `broken2/perosius_temp.s` | a boss: opens his conversation, then `SetAggressive(true)`, `SetUsable(false)`, `AiAttack()` |
| `dstar_e/dse_skyrim_soldier.s`, `dse_skyrim_archer.s` | city guards that turn hostile once `GetPlayer().saved_Dstar_Pass` says you trespassed |
| `erthcave/azra_zombie.s` | `SetAggressive(true)`, `EC_Menu7`, `AiSleep()` — both arms in one handler |
| `monsters/olpac_trailslag.s` | opens `ghstpass/GP_Menu3`, once per save |
| `raiders/raider_enter.s` | the arena doorman, opens `Raiders/enter` |

(M77: the "further 34 placements ship a handler the engine can never
reach" that stood here is superseded by the corrected count above -- 4,
all of them `delfhide/dh_guard_talk.s`.)

### `SetAlwaysOnDetect`, `DetectOnKilled`, and `MenuClosed`

`SetAlwaysOnDetect(b)` is Monster(AI) binding index 4 (dispatcher case 4),
`monster+0x306`, and it has exactly one reader in the whole binary: the
third clause of the guard above. In singleplayer `!multiplayer` already
satisfies that clause, so the flag changes nothing; it exists so a
multiplayer *client* still runs `OnDetect` locally. All ten scripts that
set it are `raiders/*.s`. `DetectOnKilled(b)` is binding 3,
`monster+0x307`, read once inside the death path (`FUN_10083c04`) and only
under `engine+0x5c0` — multiplayer-only in the same way.

`MenuClosed` is the third piece, and **it does not work in the shipped
game.** The notifier exists:

```
FUN_10064c60(entity):
  if (entity != engine->player && entity->+0x120 && !entity->+0x48)
      entity->vtable[0xa8]("MenuClosed", {}, ...);
```

and it sits at `vtable+0x3c` in all 31 entity vtables — every one of the 31
words holding 0x10064c60 is exactly 20 slots below that same vtable's
`SetUseText`. But nothing calls it. Those 31 vtable words are the *only*
references to its address anywhere in the image, and of the 18 sites in the
binary that dispatch through slot 0x3c, not one has an entity receiver:
they are the menu manager's and the app object's own slot 0x3c, and they
pass arguments `FUN_10064c60` does not take. So `monsters/bbrawler_talk.s`'s
"talk to him, then kill him" never completes on the device either — its
`MenuClosed` handler is what would re-arm the brawler. The same script is
not placed in any of the 21 zones (only the plain
`monsters/Bandit_Brawler.s` is), which is the same story from the other
side: that creature did not ship.

### A latent crash found on the way, not fixed

Three of the 1535 shipped `.s` files raise `skTreeNodeReaderException` from
`skScriptedExecutable`'s constructor — `crypt2/exit.s`,
`dstar_e/thief_convoasdf.s` and `raiders/blu_spider.s`, each of which has a
bare statement sitting outside any handler. This port catches
`skParseException` and `skRuntimeException` at every script-loading site but
not that one, so such a load aborts the process (in a Debug build, on
Windows, as a modal "abort() has been called" dialog that looks exactly
like a hang). None of the three is placed in any `.ent` or named by any
script, so no player can reach it; it is recorded here rather than fixed
because the fix is a sweep across ~25 catch sites, not part of this
milestone. A corpus brace/bracket census finds 12 unbalanced files in all,
of which only these three actually raise.

## The creature AI tick, transcribed -- and the word that turned it on (M77)

M31, M32 and M35 recovered the AI *parameters*; M76 recovered the OnDetect
arm. What none of them recovered is the single fact that decides whether
any of it ever runs on a real creature. This section supersedes the two
places above that got it wrong, and is the reference for the whole tick.

### A placed creature does not start asleep

`FUN_100815e0`, the actor constructor, sets `monster+0x2a8 = -1`. That is
not the last word. Entity vtable slot **`+0x10`** is the "attach to the
engine" virtual, and the creature classes override it:

| vtable | entities.txt category | slot `+0x10` |
|--------|----------------------|--------------|
| `0x100fe2b8` | 2 — every creature and NPC in the game | `FUN_10086f9c` |
| `0x100fee0c` | 13 — no shipped placement | `FUN_10086f9c` |
| `0x100ffaf4` | 7 | `FUN_100866c0` |

(Resolved the usual way: `vtable+0x3c` holds `FUN_10064c60` in all 31
entity vtables, which fixes each address point; only these three of the 31
name either function.) Both overrides end with the same five lines:

```c
engine->actorRegistry[self->slot] = self;      // engine+0x14620
self->+0x5e  = 0x100;
self->+0x2a8 = 2;                              // the AI package
self->+0x1d4 = 10;
```

and `GameEngine_InitLevel` calls it on **every** placement, one line after
the factory allocates the entity and *before* the placement record or the
entity's script are read:

```c
entity = engineFactory->vtable[0x18](factory, size, typeId);
entity->vtable[0x10](entity, engine);          // <-- package = 2
entity->vtable[0x134](entity, reader);         // the .ent record
```

So a script's own `Init()` always runs on a creature that is *already*
looking for a target, and `-1` is only ever seen by an actor that was
constructed and never placed.

That one word is the difference between a level full of monsters and a
level full of statues. Package 2 is the only state in which the tick
evaluates perception at all, and the shipped corpus leans on the default
completely:

| | scripts | placements (all 21 zones) |
|---|---:|---:|
| category-2/7 placements with a real script | — | 1539 |
| `SetAggressive(true)` | 318 | 1296 |
| ...of those, calling `AiDetect()` or `AiAttack()` | — | **47** |
| ...relying on the spawn default alone | — | **1249** |

`monsters/Azra_Rat.s` is one of the 47. That is the entire reason the Azra
rats were the only creatures in this port with any behaviour: they call a
binding that re-states what the placement already did.

### The tick's own shape

`FUN_10082224`, in its own order, with the parts earlier sections already
cover named rather than repeated:

```c
if (self->+0x48 /*destroyed*/)  return;
if (self->+0x1e4 /*dead*/)      { death clip; return; }
... the timed-effect list; the fear countdown (+0x300 -> restore +0x2fc);
    the lifespan countdown (+0x2ec, at 3x the frame delta); the paralysis
    countdown (+0x294) ...
if (self->+0x2ee /*SetCanTeleport*/) {
    if (!target && self->+0x266 /*SetBoss*/) target = engine->player;
    if (dist(self, target) > self->+0x2b8) FUN_10086a18(self);   // jump to it
}
if (target && !self->+0x2bc /*SetImmobile*/) moveGoal = target position;
if (!self->+0x2bc) vtable[0x1b4](self);
self->+0x2c4 += FUN_1001afa4(engine);          // the cadence, EVERY tick

if (package == 3 && target) {                  // ---- pursue/attack
    if (self->+0x294 == 0) vtable[0x208](self, target->x, target->y);
    d = vtable[0x5c](self, target);
    if (d < self->+0x2dc) { stop moving; return to the idle pose; }
    if (0x100 < self->+0x2c4) {                // <-- once a second
        dz = |self->z - target->z|;            // both from +0xa4, s16
        ranged = equippedWeapon->isLongRange    // +0x48, +0x1a7
              || self->+0x310                   // a spell in slot 0
              || self->+0x2c2 == 0xe1;          // SetAttachedWeapon(225), a bow
        if (d < self->+0x2dc && (dz < 0x200 || ranged)) {
            reach = ranged ? true
                           : FUN_10082004(self, target, +0x2dc >> 8, ..., 1);
            if (reach) {
                if (self->+0x266 /*boss*/) {           // the second throttle
                    now = engine->clock;
                    if (self->+0x2c8 == 0 ||
                        self->+0x2c8 + self->+0x2cc * 0x100 < now) self->+0x2c8 = now;
                    else                                            suppress;
                }
                vtable[0x240](self, target);           // FUN_100835b8, the swing
            }
        }
        else if (self->+0x2b8 < d) { target = 0; package = 2; idle pose; }
        else                       { self->+0x1b9 = 1;  /* keep closing */ }
        self->+0x2c4 = rand & 0x1f;
    }
}
else if (package == 2 || (package == 3 && !target)) {   // ---- look
    candidate = engine->player;                // the only one in singleplayer
    d = vtable[0x5c](self, candidate);
    if (package == 2) package = 3;             // target still 0 -- same arm
    noticed = true;
    if (d < self->+0x2b8) noticed = the perception roll (see the M76 section);
    if (d < self->+0x2b8 && noticed) {
        if (!self->+0x2ac) { ... OnDetect ... }
        else               { target = candidate; package = 3; walk clip; }
        self->+0x2c4 = 0;
    }
}
```

Packages **4** (`AiFlee`), **5** (`AiPursue`) and **6**
(`AiSpellAssistTarget`) have no arm at all, and neither does **-1**
(`AiSleep`). A creature left in any of them keeps its move goal and its
timers and does nothing else.

### CORRECTION: aggro is *not* gated on line of sight

The section "Aggro is gated on line of sight, not distance alone" above is
wrong, and this port followed it into inventing a vertical gate and a
lost-sight grace period as well. Both raycasts are real; neither is in the
acquire arm:

- `FUN_10082004` is the **melee reach** test, inside the attack decision.
- `vtable[0x21c]` (`FUN_10004d70`) is the **OnDetect sightline** — M76.

The aggressive acquire arm has neither. Its only gates are the
scaled-squared distance against `SetChaseRadius` and the perception roll,
and *giving up is the same distance and nothing else*:

```c
else if (self->+0x2b8 < d) { target = 0; package = 2; }
```

evaluated only on a cadence tick. So a creature inside 8.4 tiles (the
corpus's dominant `SetChaseRadius(18000)`) comes for you through a wall,
and stops the moment you are further away than that. The generous-looking
radii make sense on their own once they are read as squared: the whole
"port that skips the check ends up with the level converging on the
player" worry was an artefact of reading 18000 as linear, which M31 had
already fixed.

### CORRECTION: what the cadence gates

`monster+0x2c4` accumulates **above** the package branch, so it runs on
every tick in every package -- including while a creature is still walking
toward you, which is what makes the first swing land on arrival. And
`0x100 < +0x2c4` gates the whole approach/attack/give-up decision, not the
damage roll: on the ~25 ticks in between, a creature in range simply
stands in its idle pose.

The swing clip is played by the attack, once:

```c
vtable[0x148](self, self->+0x2be /*swing*/, 1, self->+0x2c1 /*idle*/, 0xf00);
```

Mode 1 is "play through once, then hand over to this other clip". A port
that re-asserts the swing clip every tick while in range shows a creature
apparently attacking without pause even though it lands one blow a second
-- which is exactly what this one did.

### The second throttle: `SetBoss` and `SetAttackSpeed`

`monster+0x266` (`SetBoss`, 15 scripts) arms an independent gate on the
game clock: a boss may swing only once every `monster+0x2cc` seconds. The
constructor sets that to 2, and **no shipped script calls
`SetAttackSpeed`**, so every boss in the game attacks at half the rate of
everything else. `SetBoss` has one other reader -- the `SetCanTeleport`
block takes the player as its target only for a boss.

### The rest of the Monster(AI) surface

`FUN_10012584` builds the class's trie; its 55 (name -> index) pairs are
its dispatcher's own case numbers. Resolving every one of them against
`FUN_10084924` fills in the last of the fields:

| binding | field | corpus | what it does |
|---------|-------|-------:|--------------|
| `SetHealth` | stats `+0x12`/`+0x24`/`+0x2a` | 30 | **byte-for-byte the same case as `SetMaxHealth`** -- a pure alias |
| `DoDamage(n)` | — | 20 | damages the creature **itself**, unsourced |
| `SetBoss(b)` | `+0x266` | 15 | the throttle above |
| `SetCanTeleport(b)` | `+0x2ee` | 4 | `FUN_10086a18`: find a path node beside the target and move onto it |
| `SetImmobile(b)` | `+0x2bc` | 3 | blocks the move goal and the movement step; the same byte `SetParalyzed`'s countdown clears |
| `StopAnimating(b)` | `+0x302` | 1 | every `PlayAnimation` in the tick and the attack is guarded by it |
| `ReplicateTeleport` | — | 1 | broadcasts `+0x2ee` over a multiplayer session; a real no-op otherwise |
| `FindPathNode(name)` | — | 1 | the same jump as `SetCanTeleport`, to a named `.pth` node |
| `SetEnemy(e)` / `Follow(e)` | `+0x20c` | 0 | set the target without touching the package |
| `GuardPlayer(e)` | `+0x2e8` | 0 | the "whose side am I on" pointer -- the only reader is the creature-vs-creature scan in the look arm |
| `SetLifespan(n)` | `+0x2ec` | 0 | `seconds << 8`, counted down at **3x** the frame delta |
| `SetItemRequiredToHit(n)` | `+0x2e0` | 0 | "only this item id can hurt me" |
| `SetAttackSpeed(n)` | `+0x2cc` | 0 | seconds between a boss's swings |
| `SetState(pkg, secs)` | `+0x2a8`/`+0x300` | 0 | the script-callable form of the Fear spell's timed package |
| `Aggressive` / `GetWimpy` / `GetChaseRadius` / `GetAttackNoise` / `GetDeathNoise` / `GetCurrentAIPackage` | — | 0 | plain getters |
| `AiActivate` / `AiWounded` / `SetMeleeAttackRange` | — | 0/0/1 | genuine no-ops: their cases fall straight through to the shared `break` |

One further correction: **`AiPursue` is not a no-op.** Case `0x2d` stores
package **5** and a target, exactly as `AiAttack`'s case `0x2a` stores 3.
What makes it look inert is the other end -- no arm of the tick reads
package 5. `GetCurrentAIPackage()` can tell the two apart.

### `vtable[0x1b4]` is not the movement step

Worth recording because it looks like one. `FUN_10006704` is eleven
instructions: it reads the signed 16-bit value at `actor+0xb8`, compares
it with `actor+0x1a4`, and either steps it an eighth of the way toward
that target or -- when the target is 0, which it always is -- decays it by
half. It is an angular-velocity damper, not locomotion. The actual
movement is driven by `+0x1b9`/`+0x1bc`/`+0x1c0` through the entity
physics update, and the pathing behind it uses the zone's `.pth` node
table, which is still undecoded past its header (see ZONE_FORMAT.md). The
port's own steer-and-slide chase is the one substantial part of this tick
that is not a transcription.

### In the port

`port/src/simkin_bindings/monster_ai.h` carries this derivation and the
three constants (`kSpawnAiPackage`, `kAttackVerticalLimitUnits`,
`kBowAttachedWeaponModel`) plus `BossAttackDue()`. The tick itself is
main.cpp's creature loop, rewritten to the shape above;
`MonsterExecutable::m_AiPackage` now defaults to `kSpawnAiPackage`.
`port/src/tests/m77_monster_ai_smoke.cpp` (40 checks) covers the spawn
package, the census, the cadence, the boss throttle, the fourteen bindings
that were soft-failing, and the vertical limit.
