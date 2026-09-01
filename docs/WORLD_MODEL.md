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
`sprintf`'d save-slot filename), a "load level or show an error dialog"
routine (`FUN_1006c31c`, which calls the earlier-identified crash/error
display function `FUN_10018e70` on failure), and a level-unload/cleanup
routine (`FUN_1006c240`). Correcting the previous doc's guess: this
isn't where the 3D/tile rendering code lives.

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

- Casts a **fan of rays** outward from the player/camera position — the
  ray count (150, 177, or 178) is picked by a 3-tier quality setting read
  from `engine+0x608` (the same field elsewhere used as a `0x100`=identity
  render-scale factor — so this looks like a detail/performance knob, not
  a fixed constant), using the *same* 2048-entry sin/cos LUT shape as
  `BuildRotationMatrix3x4`/the automap marker/`Bullseye_PropagateLight`.
- Each ray steps tile-by-tile (a DDA-style march) up to a quality-tiered
  max range (25, 93, or 172 tiles), stopping early if it reaches a tile
  whose `flags` byte has **bit1 set (wall/obstruction)** — the *same* bit
  `Bullseye_PropagateLight`'s light-bounce rays stop/reflect at, so one
  "this tile is solid" bit governs both light propagation and rendering
  visibility.
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
