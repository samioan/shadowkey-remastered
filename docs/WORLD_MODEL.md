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
- `map+0x94` / `map+0x9c` = the player's current position, each an 8.8
  fixed-point value (`>>8` recovers the tile-integer coordinate)

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
2. Reads the player's tile position from `CMap` (`map+0x94`/`+0x9c`).
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

## Labels applied

- `Map_GetTileAt` (0x1001b004)
- Addendum to `GameEngine_ctor`'s existing plate comment noting
  `engine+0x618`'s role.

New reusable script: `shadowkey/ghidra/scripts/pyghidra_find_reads.py
<hex-offset>` — like `pyghidra_find_field_writes.py` but for `LDR`
instead of `STR`, used to find every function that *consumes* a known
object field (here, `engine+0x480`, the cached raw framebuffer
pointer) rather than every function that sets it. This is how
`Map_GetTileAt`'s huge call-site count, and `FUN_1002b430`, were found.

## Open follow-ups

- What `FUN_1002b430`'s tile-height-difference scan actually produces
  (automap vs. visibility) — not confirmed.
- The actual first-person rasterizer/raycaster still not located. Given
  the world is now confirmed tile-based, worth searching next from a
  different angle: what reads a tile's *texture*/*wall* fields (as
  opposed to the height field already seen) out of the 36-byte
  type-table, or what iterates `Map_GetTileAt` results into actual
  pixel writes into the backbuffer.
- The 36-byte tile-type table's and 8-byte per-cell tile's exact field
  layouts are unconfirmed beyond the one height field found so far.
