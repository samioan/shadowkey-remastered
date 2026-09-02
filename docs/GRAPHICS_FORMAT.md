# Low-level 2D graphics primitives

Following up on `WORLD_MODEL.md`'s open lead (find what turns tile/
entity data into pixels), this thread didn't reach the first-person 3D
wall renderer itself, but it did find the actual low-level primitives
everything on screen is built from — screen format, sprite format, and
the core blit function. Reached via a different path than expected: not
`Map_GetTileAt`'s widest callers (those turned out to be actor
movement/collision against the tile grid, or huge SimKin native-function
dispatch tables — see below), but via `engine+0x480` (the cached raw
framebuffer pointer)'s read sites.

## Screen/framebuffer format

- 176×208 pixels (`0xb0`×`0xd0` — the N-Gage's native portrait
  resolution), confirmed by a hard width clamp (`if (0xb0 <
  param_8-param_6) param_8 = param_6+0xb0`) and a height clip (`0xcf` =
  207, i.e. `0xd0-1`) inside the blit function below.
- 16 bits per pixel, row stride **0x160 (352) bytes** — exactly
  `176*2`, confirming a flat, non-padded 16bpp buffer.
- Colors are 4-bit-per-channel (RGB444-ish): palette constants seen so
  far are `0xfff` (white), `0xf00` (red), `0x0f0`-shaped values, and a
  dedicated **transparency colorkey `0x0f0f`** (a magenta-family value)
  — matches `ScreenModeController_ctor`'s palette setup in
  `RENDER_LOOP.md`.

## Sprite/image format (paletted, segment/RLE-encoded)

`Blit_RLESprite` (renamed from `FUN_1006a488`, 0x1006a488) is the core
compositing primitive:

```
Blit_RLESprite(engine, fbPtr, dstX, dstY, imageData,
               srcXOffset, rowStart, rowEnd, clipRight, blendMode)
```

Image layout (`imageData`):

- `+0x0`: `ushort` width, `+0x2`: `ushort` height (its own tiny header)
- `+0x4..`: a 16-bit-per-entry color palette
- `+0x204..`: per-row data — for each row, a `(startX, endX)` `ushort`
  pair followed by `endX-startX` bytes, each a palette index into the
  table at `+4`. Rows are visited sequentially by walking forward
  through this stream (no fixed per-row size — genuinely variable-length
  run encoding, presumably to skip fully-transparent leading/trailing
  pixels per row cheaply on a slow ARM920T with no hardware blitter).

Per pixel: skip if the palette-resolved color equals the colorkey
`0x0f0f`; otherwise write it, either as a straight copy (`blendMode
== 0`) or as a **50% average blend** with whatever's already in the
framebuffer (`blendMode == 1`: `((src & 0xeee) + (dst & 0xeee)) >> 1`)
— the latter is presumably used for translucency/shadow effects.

## Where this gets used (and where it doesn't)

Traced two call sites, both **UI widget rendering, not the 3D view**:

- `DrawEntitySpriteWithOutline` (0x1007ec80) — draws the sprite for a
  cached image index (`entity+0x98`, looked up in a **384-slot loaded-
  image-pointer cache at `engine+0x4460`**, zeroed out in
  `GameEngine_ctor` alongside a parallel array at `engine+0x4a60`) at a
  screen position (`entity+0x78`/`+0x7c`). If a highlight flag is set,
  also draws an 8-direction 1px-offset outline of the same sprite —
  a selection/highlight effect.
- `DrawListItemIconAndLabel` (0x1007f050) — draws either the icon above
  (via `DrawEntitySpriteWithOutline`) or, if no valid image is pending
  (`entity+0x9c == -1`), a plain colored placeholder box, then always
  draws a text label. Reads like an inventory/menu/dialogue-choice list
  entry, not the world view.

So this specific call path is HUD/menu rendering. The actual first-
person wall/floor/ceiling rendering is presumably built on the *same*
`Blit_RLESprite` primitive (or a sibling — `FUN_1006bee8`/`FUN_1006be58`,
used here for outline/placeholder boxes, are still unidentified rect/
line-fill helpers worth checking too) but from a different call site
not yet found.

## Corrected dead ends from this pass

Widening the `Map_GetTileAt` caller search (per `WORLD_MODEL.md`'s
follow-up) mostly hit two things that are **not** the renderer:

- `FUN_1003f130`/`FUN_1006dbec`/`FUN_10061a60`/`FUN_10084924` (the
  binary's largest functions, 5.5–12KB) are all **huge SimKin
  native-function dispatch tables** (40–100+ `case`s, each calling
  `SIMKIN_ord*` to marshal script-visible native functions) — not
  rendering code. Confirms the roadmap's expectation that SimKin drives
  a lot of game logic via a real native-function bridge.
- `FUN_100017c8` is **actor movement/collision against the tile grid**:
  reads an actor's fixed-point position/velocity/bounding-radius fields
  (`entity+0x94`/`+0x9c` position, `+0x8e`/`+0xa0` radius), checks the 4
  cardinal-neighbor tiles via `Map_GetTileAt` + a per-tile-type
  collision callback (vtable `+0x1e8`), and corrects position on
  collision. Confirms actors have fixed-point 8.8 positions and
  bounding radii, and that tile-type entries carry per-type collision
  behavior — useful for a port's physics code, not rendering.

## Labels applied

- `Blit_RLESprite` (0x1006a488)
- `DrawEntitySpriteWithOutline` (0x1007ec80)
- `DrawListItemIconAndLabel` (0x1007f050)
- `SpriteCache_LoadCategory` (0x10024c8c) — the lazy-load-and-cache
  function this session decoded (`global.spr` + `<category>_sprites.txt`)
- `ReadFileRange` (0x100275e4) — generic `RFile::Open`/`Seek`/`Read`
  helper, drive-letter-retry aware
- `DrawUIText` (0x1007f49c) — the real per-widget text-label draw entry
  point, routes to Symbian GDI (see the font section above)

## Open follow-ups

- The actual first-person 3D wall/floor/ceiling renderer is still not
  located via *this* thread's path — since resolved via a different
  route, see `RENDERER_3D.md` (the tile-grid `SurfaceFace_*` pipeline).
  `FUN_1006bee8`/`FUN_1006be58` (used here for rects/outlines) remain
  unidentified beyond "simple fill/line primitives" -- not chased
  further since the 3D renderer question is independently closed.

## The 384-slot image cache's real source format -- decoded (PC port session)

Traced `engine+0x4460`'s writers (not just readers) by decompiling
every function in the binary and grepping for the offset, since it's
built via multi-instruction ARM arithmetic and doesn't show up as a
literal-pool constant (`shadowkey/ghidra/scripts/
pyghidra_grep_decompiled.py`, new this session — decompiles the whole
~2000-function program once and greps the C text, for exactly this
"offset built via arithmetic, not a loadable literal" case). Found the
real lazy-load-and-cache function, `FUN_10024c8c`, and traced its two
string-format arguments to real, readable strings in the binary:

```
"%s\global.spr"          -- the sprite data file (DAT_10024dd0)
"%s\%s_sprites.txt"      -- per-category index manifest (DAT_10024dd8)
```

**`global.spr`** (1 file, global — not per-zone) is:

- A **384-entry `uint32` little-endian size table** (1536 bytes,
  `0x600` — matches the cache's own 384 slots exactly), each entry the
  byte length of that slot's sprite blob (0 = unused slot; 253 of 384
  are populated in the real file).
- Followed immediately by the sprite blobs themselves, **concatenated
  back-to-back in slot order** with no padding or per-blob header
  beyond what's already inside each blob — slot `i`'s byte offset is
  `1536 + sum(sizeTable[0..i-1])`. Verified exactly against the real
  file: summing all 384 real size-table entries plus the 1536-byte
  header lands on `global.spr`'s real file size (1,646,707 bytes) to
  the byte.
- **Each blob is already the exact in-memory RLE sprite layout this
  doc's "Sprite/image format" section above already fully decoded**
  (`ushort` width, `ushort` height, a 256-entry 16-bit palette, then
  per-row `(startX,endX)` + palette-index-byte runs) — there is no
  separate decode/transcode step. `FUN_100275e4` (the file-range
  reader `FUN_10024c8c` calls) is a generic
  `RFile::Open`/`Seek`/`Read` helper (with a drive-letter retry
  sequence — the path's first character is tried as given, then
  forced to `'e'`, then `'z'`, matching EKA1's ROM-vs-installed-drive
  convention) that just heap-allocates and reads N raw bytes at a
  byte offset — it doesn't parse anything. Confirmed by writing a
  parser against the real file: for every sprite checked, the parser
  consumes **exactly** the blob's byte length with zero slack, across
  wildly different blob sizes (from 731 bytes to 37,956 bytes) — the
  strongest evidence a format guess can get short of a screenshot
  match.
- **Visually confirmed** by rendering real slots to images: slot 20
  (176×208, i.e. exactly full-screen) is a parchment-and-vine-border
  menu background; slot 30 (32×32) is a dagger icon; slot 45 (64×64)
  is a character portrait (an elf face — the exact asset M5's
  character-creation portrait picker needs, see
  `FloatingSpriteExecutable`'s header comment); slot 160 (39×5) is a
  blue-to-white gradient strip (a UI highlight/progress-bar piece).

**`<category>_sprites.txt`** (one per zone, e.g. `azra_sprites.txt`,
plus a non-zone `menu_sprites.txt`) is a **plain-text, newline-
separated list of decimal slot indices** — which of `global.spr`'s 384
slots that context should have loaded and ready. `menu_sprites.txt`
(60 entries: 20-61 contiguous, then a scattered set — 69, 160-162, 174,
205-208, 213-217, 245-247) is the menu/HUD icon set, including
multiple full-screen (176×208) background variants (20, 69, 174,
245-247 — likely one per distinct menu backdrop) and several small
border/strip pieces (160 etc.) consistent with the "ornate gold
dragon-head/wing border" HUD art `PORT_ROADMAP.md` describes as still
missing. This resolves `PORT_ROADMAP.md`'s "Real menu background
images / HUD iconography" item's blocking question — a real,
structurally- and visually-verified format, not a guess.

Same manifest convention already existed for `<zone>_models.txt`/
`<zone>_sounds.txt` (used by M8's model-archive loading) — this is
just the third leg of the same per-zone-asset-list pattern, previously
unnoticed because nothing had gone looking for a `_sprites.txt` yet.

**Not chased further**: whether/how a *category* string other than
plain zone-name or `"menu"` is used (`FUN_10024c8c`'s `param_2` is
`strcmp`'d against a single-character string `"m"` for something not
traced), and the exact meaning of the two full-`param_1` context
fields (`+0x261` the base path, `+0x388` the cached size-table
pointer) beyond what's needed to read the format — this doc only
needed the file format itself, not every caller's context object
layout.

## The real in-game font -- confirmed to be the Symbian OS's own system font, not a game asset

Traced the actual UI text-draw call `DrawListItemIconAndLabel` (this
doc's own labeled function) makes, `FUN_1007f49c` ->
`FUN_1008f8a4` -> `FUN_10022b20`/`FUN_10022d48`/`FUN_10022f88`
(mode-selected by a parameter). All three of those bottom functions
are genuine Symbian EIKON/GDI calls, not a custom in-house renderer:

- The default path calls `CEikonEnv::LegendFont()` (`EIKCORE__
  LegendFont__C9CEikonEnv`) — Symbian's standard built-in small UI
  font, part of every Symbian device's ROM, not a file this game ships.
- A secondary path (used for some specific text, not traced further)
  explicitly builds a `TFontSpec` for family name **`"Swiss"`** at
  design height `0xd5` (213 twips) with optional bold/italic
  (`GDI::SetStrokeWeight`/`SetPosture`) — `"Swiss"` is itself a
  standard stock Symbian typeface name (a Helvetica/Arial-alike
  bundled with the OS), still not a custom game asset.
- Either way, the font object is handed to the graphics context via
  `UseFont()` and text is drawn via a real `CGraphicsContext::
  DrawText(...)` vtable call (offset `+0xa8`), then released via
  `DiscardFont()`/`ReleaseFont()` — completely standard Symbian GDI
  text rendering, no custom glyph atlas or bitmap-font asset anywhere
  in this call chain.

**This closes `PORT_ROADMAP.md`'s "Real font/glyph rendering" item —
not by finding a decodable format, but by confirming there isn't one
to find.** The real glyph bitmaps live in the Nokia N-Gage's Symbian
ROM (the `LegendFont`/`Swiss` system typefaces), not in `6r51.app` or
any file in this game's own install image — there is nothing left to
extract from the assets this project has access to. Pixel-exact
reproduction would require a dump of the actual N-Gage device ROM's
font bitmaps, out of reach here. The practical port-side conclusion:
the existing stand-in bitmap font (`port/src/graphics/bitmap_font.h`)
isn't missing a decode step, it's a permanent, deliberate substitute —
worth upgrading for *visual* quality (e.g. rendering through a real
sans-serif TrueType font via the Win32 platform layer's own GDI, which
this port already links) but not worth further RE effort chasing an
"original format" that isn't a game asset in the first place.

## Open follow-ups

- `FUN_1006bee8`/`FUN_1006be58` (rect/outline fill primitives used
  throughout the UI code above) are still unidentified beyond "simple
  fill/line helpers" — low priority, their effect is already visible
  and reproducible without knowing their exact internals.
- The single-character category string comparison in `FUN_10024c8c`
  (`param_2` vs. `"m"`) and the two context-object fields noted above —
  not needed for the file-format question this pass answered.
