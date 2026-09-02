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

This closed `PORT_ROADMAP.md`'s "Real font/glyph rendering" item as
far as *this game's own files* go — not by finding a decodable format
in `6r51.app` or the retail install image, but by confirming there
isn't one to find there. The real glyph bitmaps live in the Nokia
N-Gage's Symbian ROM (the `LegendFont`/`Swiss` system typefaces), not
in this game's data. The pixel-exact reproduction this doc originally
called "out of reach" (needing an actual N-Gage device ROM font dump)
turned out to be reachable after all — see below.

## The real Symbian `.gdr` font format -- decoded and wired into the port (follow-up session)

A dump of the real N-Gage QD (RH-29) ROM was available after all (an
EKA2L1 emulator profile), so the "font bitmaps live in device firmware,
out of reach" conclusion above got revisited. `Z:\System\Fonts\` on
that ROM has three real Symbian bitmap-font files (`.gdr` = Glyph Data
Resource): `Ceurope.gdr` (31389 bytes, the general system font set —
this is the one wired in), `Browsereur.gdr` (the WML browser's own,
larger set), `CalcEur.gdr` (a tiny calculator digit-only font).

**This is not a shadowkey-specific format** — `.gdr` is Symbian OS's
own standard font-store format, unrelated to any of the tries/dispatch
tables/RLE sprite formats elsewhere in this doc, so there was no
Ghidra/decompile angle to it. It's real, though: the open-source
EKA2L1 emulator (the same tool the ROM dump came from) ships its own
GPLv3 `.gdr` parser (`src/emu/loader/{gdr.h,gdr.cpp}`,
`src/emu/common/src/unicode.cpp`) — that parser's byte layout was
ported into this port (`port/src/assets/gdr_font.h/.cpp`), then
independently verified against the real `Ceurope.gdr`: the port's
parser consumes the **entire 31389-byte file with zero leftover
bytes** and recovers 8 real typefaces (`LatinBold12`/`13`/`17`/`19`,
`LatinPlain12`, plus three small digit-only faces) with correctly
shaped glyph bitmaps.

Format, in parse order — a header (9 fixed `uint32` fields, 3 of them
magic UIDs/checksum that identify the file as a font store at all, then
N copyright strings) → one `font_bitmap_header` per pixel-size/style
variant (uid, posture/stroke/proportional flags, cell height/ascent/max
width, then that variant's `code_section_header[]` — contiguous
Unicode codepoint ranges, each an offset into that variant's own
bit-packed glyph blob) → one `typeface_header` per human-readable name
(e.g. `"LatinPlain12"`) pointing at a `font_bitmap` by uid → then, per
`font_bitmap` in header order, a `character_metric[]` table (ascent/
height/left-bearing/advance/right-adjust per distinct glyph shape --
many codepoints share one entry, e.g. every blank cell) followed by,
per code_section, an offset table plus a bit-packed blob: each
character starts with a 7- or 15-bit index into `character_metric[]`
(the first bit picks which), then a run-length-coded 1bpp bitmap (a
repeat-line flag + 4-bit run count per encoded row, LSB-first
throughout — see `gdr_font.cpp`'s `ParseCodeSection` for the exact bit
math, transliterated from EKA2L1's C++).

**The one real trap**: the embedded strings (copyright text, typeface
names) are compressed with an SCSU-like scheme (static/dynamic 128-code
windows plus an explicit "quote raw UTF-16" escape) — and the on-disk
cardinality-prefixed length these strings carry is only a generous
*upper-bound buffer size* to decompress into, not the string's real
compressed size. The real per-string byte length only comes out the far
end of actually running the decompressor (`UnicodeExpander::Expand`
reports bytes consumed via its return value) — a naive "skip N bytes"
implementation (this session's first attempt) desyncs the whole rest of
the file after the very first string. Confirmed by brute-force: for the
real file's first (3-character) copyright string, the naive formula
computed a 22-byte skip; the correct decompressed skip is 7 bytes,
found by walking the SCSU state machine by hand against the raw bytes
and cross-checking against where the (independently very recognizable —
ascending `0x10005910..591c`-style UIDs, sane cell heights, real
codepoint ranges) `font_bitmap_header` table actually starts.

**Wired into the port**: `port/src/graphics/bitmap_font.h`'s
`BitmapFont::LoadRealFont(path, typefaceName)` loads a `GdrFont` and
`DrawString` now draws real glyph bitmaps (proper per-glyph advance/
left-bearing/baseline placement) wherever the loaded font covers a
codepoint, falling back to the old blocky 5x7 placeholder otherwise —
same "non-fatal missing optional asset" pattern as every other real
asset this port loads. `main.cpp` loads `"LatinPlain12"` (the one
non-bold variant, 12px cell height) from a configurable path (`argv[2]`,
default `port/assets/fonts/Ceurope.gdr`). **`Ceurope.gdr` itself is
Nokia device firmware, not this project's data or shadowkey's — like
the retail game install, it is never committed to this repo** (see
`.gitignore`); each dev copies their own extraction in (e.g. from an
EKA2L1 profile's `data/drives/z/<device>/System/Fonts/`).

Confirmed live: the main menu now renders real proportional mixed-case
Symbian UI glyphs ("New Game", "Load Game", "Delete Saved Game", ...)
in place of the old blocky upper-case-only placeholder.

Not pursued: `Browsereur.gdr`/`CalcEur.gdr` (no current use for either
in this port), and the bold/other-size typefaces in `Ceurope.gdr` (only
`LatinPlain12` is wired up — the other 7 decode fine too, just aren't
loaded by anything yet).

## Correction: the real ROM font above is NOT the menu font -- "Nokia Cellphone FC" is (same-day follow-up)

The `.gdr` decode above is real and correct (byte-exact, full-file
consumption, verified) — but wrong for this job. A user-provided real
screenshot of the main menu, compared glyph-by-glyph against every
single typeface in *both* real font files (`Ceurope.gdr`'s 8 plus
`Browsereur.gdr`'s 9 — all 17, every size, every bold/italic variant),
showed none of them match: the real menu text ("New Game", "Load
Game", ...) is a distinctly rounded, bubbly display face, while every
real N-Gage ROM font (both device profiles checked, RH-29 and NEM-4)
is a plain blocky sans. Neither device ships anything else that could
produce it (checked: the game's own retail files ship no font at all;
neither EKA2L1 profile on hand has a `.ttf` fallback or `font:` config
override in play).

The user identified the actual source by comparing against what EKA2L1
itself renders: **"Nokia Cellphone FC"**, a freeware TrueType lookalike
of classic Nokia phone displays (`nokiafc22.ttf`) — rendering "New
Game" in it lines up with the real screenshot almost exactly (same
rounded "G"/"a"/"m" shapes, same weight). *How* EKA2L1 arrives at this
specific font wasn't pinned down (checked its `font_store.cpp` matching
logic, `load_custom_fonts`'s user-font-folder mechanism, and the config
file's font-override field — none of them explain it on this machine);
this is a visual-comparison finding, not a decompile or a traced code
path. Given how it was found, don't extend it into a claim about how
the real hardware or EKA2L1 internally resolves this font — that part
stays an open question.

**Wired into the port** (`port/src/graphics/bitmap_font.h`/`.cpp`):
`BitmapFont::LoadRealFont(path, typefaceName)` now dispatches on file
extension. A `.ttf` goes through a new `TtfFont` class that rasterizes
via **Win32 GDI** (`AddFontResourceExA` + `CreateFontIndirectW` +
`ExtTextOutW` into an off-screen 32bpp DIB, then composited into the
backbuffer by per-pixel luminance blend against the requested color) —
this port already links GDI for presentation, and TrueType
rasterization is exactly what it's for, so there was no reason to
hand-parse glyph outlines. A `.gdr` still goes through the `GdrFont`
path above (kept working, just not the default any more). `main.cpp`
now loads `port/assets/fonts/nokiafc22.ttf` as `"Nokia Cellphone FC"`
by default. Sized at 8px (tried 12px first, matching `LatinPlain12`'s
cell height, then 10px — both visibly too large against the real
screenshot, e.g. "Multiplayer Menu" spanning most of the parchment
border instead of leaving real margin; 8px matches noticeably closer).
Like `Ceurope.gdr`, this TTF is third-party (not this project's or
shadowkey's own data) and never committed to the repo — see
`.gitignore`.

**Checked and ruled out: a hand-drawn glyph-sheet sprite.** Before
settling on the TTF match, checked whether the menu font might instead
be pre-rendered pixel art blitted from `global.spr` (plausible for a
2004 handset — a hand-pixeled display face wouldn't need a real
system/TrueType font at all, and the letterforms' geometric,
single-story-`a` character does read as intentionally hand-drawn, not
a stock face). Scanned all 384 `global.spr` slots (already fully
decoded, see above) for anything glyph-sheet-shaped: no cluster of
~20-40 small (roughly 6-16px) sprites with sequential slot ids, and no
single wide "strip" sprite beyond the compass tape/vitals bars already
identified. The one dense run of same-sized slots (181-204, 24 of them,
all 16x64) decodes to a torch/flame flicker-animation cycle, not
glyphs. Also checked `6r51.mbm` (a real, separate Symbian
multi-bitmap resource file this game ships) — only 1770 bytes, too
small for anything but the app's launcher icon. No other candidate
files exist in the retail install. This doesn't disprove a hand-drawn
font (it could still be compiled directly into `6r51.app`'s own
resources rather than a loose file — not checked), but rules out the
most likely loose-asset locations.

**Position was also wrong, separately from the font.** The main menu's
background (`global.spr` slot 69, `MenuBackground(69)` in
`mainmenu.s`) has "The Elder Scrolls Travels / SHADOWKEY" logo art
baked into its top ~50px — unlike every other menu background (20,
174), which are plain parchment. `mainmenu.s` sets no title row and no
native y-offset call exists for this, so the port's fixed `y = 8` item
list start (fine for every *other* menu) collided with the logo.
Fixed to `y = 50` specifically when `backgroundId() == 69`, measured
against the real screenshot (not a decompiled constant — nothing in
the script sets this, so whatever offset the real native menu-list
renderer uses natively isn't visible from the script side).

**Alignment was wrong too.** The real screenshot's menu items are
horizontally centered on screen, with no left-margin selection arrow
(selection is color-only, the row's text turning
`kSelectedTextColor`) — the port had both left-aligned text *and* a
`>` arrow glyph in front of the selected row, neither backed by
anything from the real screenshot. `BitmapFont::TextWidth(text)`
(changed from a fixed-pitch `TextWidth(charCount)`, unused elsewhere,
to a real GDI-measured width so centering tracks whatever font
`DrawString` actually draws with) now centers `RowKind::MenuItem`/
`StaticItem` labels in `RenderMenu`; the arrow draw was removed
(applied to every row kind, not just these — no evidence any real
screen uses it either).

Confirmed live: menu item text now matches the real screenshot's font,
size, position, and centered alignment — a "New Game"/"Load Game"/...
list that looks like the same typeface, sits below the logo instead of
through it, and is centered rather than left-hugging.

## Correction #2: the ROM font WAS right all along -- the "Nokia Cellphone FC" call above was a mistake (same-day follow-up)

The correction above was itself wrong. Two things forced a second look:
user feedback that the TTF still didn't look right, and a decompile of
shadowkey's *own* text-draw call chain — `DrawUIText`
(`FUN_1007f49c`) → `FUN_1008f8a4` → `FUN_10022b20` — which confirmed
`AddMenuItem`'s per-item widgets (the `Widget (UI base)` class,
`0x14d20`, whose `SetFontNum` field flows all the way down as
`FUN_10022b20`'s last parameter) hit the branch that calls genuine
`EIKCORE::LegendFont()` — the real N-Gage ROM's own system font —
whenever that field is anything but exactly `1` (the one case that
instead builds an explicit `"Swiss"`-family `TFontSpec`, used by
`mainmenu.s`'s separate `funtext` floating-text object via
`SetFontNum(1)`, not by ordinary `AddMenuItem` rows). This is a real,
decompiled finding — the earlier "rounded, therefore not this ROM
font" conclusion never accounted for what the game's own code actually
calls.

That sent the question back to *which* `Ceurope.gdr` typeface,
properly this time: downscaled the real screenshot back to native
176×208 with a box filter (undoing the ~4.4x upscale a video-capture
source applies) instead of comparing rendered glyphs against the
upscaled image directly. The result, extracted as raw on/off pixels
for "New Game" and compared letter-by-letter against every glyph
`Ceurope.gdr`'s `gdr_font.cpp` already decodes: **exact, bit-for-bit
matches against `LatinBold12`** — same 6px-wide "N"/"G"/"a"/"e" shapes,
same 10px-wide "m", same cell height, no discrepancy in a single pixel
checked. The "rounded" look that motivated switching to a TTF in the
first place was video-compression blur smoothing a blocky ~10-12px
bitmap font in the *upscaled* screenshot — comparing crisp
Python-rendered glyphs against a blurred, re-encoded video frame was
comparing the wrong things, not a real design difference.

**Also checked and ruled out again, more carefully this time**: a
hand-drawn glyph-sheet sprite (Correction #1's `global.spr`/`6r51.mbm`
scan already covered this and still stands — nothing glyph-shaped
exists in either file).

**Wired into the port**: `main.cpp` now loads `Ceurope.gdr` /
`"LatinBold12"` again (not `"LatinPlain12"` from the first `.gdr`
pass — bold is the correct weight, confirmed by the same pixel
comparison). `BitmapFont::TextWidth` was extended to sum real
per-glyph `.gdr` advances (it previously only handled the TTF case or
the flat-pitch placeholder, which would have made the centering added
in Correction #1 measure the wrong widths against this font). The TTF
path (`TtfFont`, Win32 GDI) is kept in place and working — genuinely
useful, exercised code — just not the default any more.

**Also fixed, from separate user feedback comparing the same
screenshot**: menu text colors. Sampled directly off the real
screenshot: unselected items are a dark red/maroon (`~(112,48,48)`),
the selected item is near-white (`~(216,216,216)`), not the port's
prior gold-select/light-gray-unselected scheme. Also gave the disabled/
static-item color (`kStaticTextColor`) its own distinct muted tone —
previously visually too close to the unselected color to read as a
real third state.

## The real gameplay HUD (compass banner + vitals bar) -- decoded (PC port session)

Following up on `PORT_ROADMAP.md`'s "HUD dragon-head/compass border art"
item: traced the actual native draw functions for the always-on
in-game HUD (not the menu/list-item icon path above).

**Finding the functions was the hard part.** The obvious path —
`GameTick_UpdateAndPresent`'s per-tick "screen mode" dispatcher
(`FUN_10068e0c`, `RENDER_LOOP.md`) — turned out to be a dead end for
this: its gameplay case (`this+0x78 == 5`) only calls `Render3DScene`,
which itself never touches the sprite cache at all (grepped its full
decompile for `Blit_RLESprite`/`0x4460` — zero hits). The real HUD
draw functions are called through **indirect vtable dispatch**
(`ScreenModeController`'s secondary vtable, a static array at
`0x100fb908` — confirmed by finding `FUN_10029cb0`'s and
`FUN_1002a6d4`'s own addresses stored at `+0x1c`/`+0x24` there), which
Ghidra's static "find callers" can't trace back to a concrete caller.
Found them instead by searching for their own address as a raw 32-bit
value elsewhere in the binary (`pyghidra_find_literal_pool_offset.py`
against each candidate function's own entry-point address) — e.g. the
vitals-bar function's address turns up at `0x100fb94c`, i.e. **the
same vtable, slot `+0x44`**.

A second wrinkle: these functions use a **compile-time-constant** slot
index into the 384-slot cache (`engine+0x4460+slot*4`), which the
compiler folds into one fixed address (`engine+0x4464` for slot 1,
etc.) — invisible to a plain `"0x4460"` text search across the whole
binary, unlike the *variable*-indexed lookups the menu/icon code above
uses. Found by computing the fixed addresses for the candidate slots
(from the real art already identified by eye, see below) and grepping
the whole decompiled program for those specific constants instead
(`pyghidra_grep_decompiled.py`, new this session).

**Compass banner** (`FUN_1002ba64`, decompiled in full):
- `global.spr` slot 0 (322×13 — wider than the 176px screen on
  purpose) is a strip reading `"...N...E...S...W..."`, meant to be
  scrolled. Slot 1 (176×31) is the dragon-head-flanked frame, with a
  transparent center window.
- Draws slot 0 at screen `(52,5)`, but only an **68px-wide window**
  (`Blit_RLESprite`'s `srcXOffset`/`clipRight` params) starting at a
  **heading-derived source X offset** — the real formula reads the
  *high byte* of a 16-bit heading field at `player+0xb6` as a signed
  value, wrapped into `[0,255]` and capped at `254`. Then draws slot 1
  on top at `(0,0)`, full screen width — its transparent gap is
  exactly where the scrolled tape shows through.
- Confirmed live: launched the built port, created a character, and
  screenshotted the compass banner mid-gameplay — the gold dragon-head
  frame renders with the `N` glyph visible in the window, matching the
  original screenshot description exactly.

**Vitals bar cluster -- corrected (post-M14 session)**: the original
pass above got this wrong. `FUN_1002c010` (single health-only bar) is
*not* the real always-on vitals HUD -- `FUN_1002ae88` is, and it draws
all three vitals together.

Found the real per-frame caller this time (rather than stopping at "no
static caller found"): `FUN_1002ae88` has exactly one direct (non-
vtable) caller, `FUN_10029cb0` -- which is itself vtable slot `+0x1c`
on this same `ScreenModeController` secondary vtable, confirmed called
*unconditionally every tick* from `GameTick_UpdateAndPresent`. Inside
it, the gameplay-screen-mode branch (`this+0x78 == 5`) calls exactly:

```
FUN_1002ae88(param_1);  // vitals bar cluster
FUN_1002ba64(param_1);  // compass
FUN_1002bb54(param_1);  // hand icons
```

every single frame, together. That settles it -- `FUN_1002ae88` is the
real vitals widget, not a "weapon condition" indicator as the
`player+0x3ac+0x24` field guess originally concluded.

`FUN_1002ae88` (816-byte decompile) draws **three** 39×5 bar-fill
sprites, each independently percentage-clipped by the identical
`(ratio * 0x2700 >> 8 + 0xff) >> 8` fixed-point formula (0x27 = 39, the
sprite width):
- `global.spr` slot 162 (red gradient) at `(10,182)`
- slot 160 (blue/white gradient) at `(10,190)`
- slot 161 (green gradient) at `(10,196)`

...then one shared frame, slot 180 (57×46, dragon-head/wing art, *not*
the 94×42 frame from the original pass) on top at `(0,162)`, masking
all three bars at once -- same fill-then-frame-mask technique as the
compass banner.

**Which bar is which stat**: the underlying stat struct's three "max"
fields sit at `+0x24` (red bar's max), `+0x26` (green bar's max), and
`+0x28` (blue bar's max) -- i.e. address order is red, green, blue, not
red, blue, green. Combined with the standard health=red/magicka=blue/
fatigue=green convention, that fixes the mapping as: **top bar (red,
y=182) = health, middle bar (blue, y=190) = magicka, bottom bar (green,
y=196) = fatigue.** Confirmed live: a user-provided screenshot from
real gameplay shows exactly this 3-bar cluster (red/pale-blue/green
stacked in one small dragon-head frame, bottom-left), matching this
decode pixel-for-pixel once scaled.

**Open item -- `FUN_1002c010`'s single big bar is real too, just not
explained yet.** A second user-provided real-gameplay screenshot shows
a *different*, larger single-bar widget: slot 205 (79×9 red gradient)
at `(44,182)` plus slot 206 (94×42 dragon-wing frame) at `(40,166)` --
exactly what the original (wrong) pass had implemented. This function
is real and does get used in actual play, evidently under some other
game state. Exhaustively re-searched this pass (whole-memory raw scans,
not just literal-pool scans, for both the function's own address and
its vtable's base address as 4-byte words): `FUN_1002c010`'s address
appears **exactly once** anywhere in the program -- its one static slot
at `ScreenModeController`+0x44. There is no second static reference to
chase; the real trigger is a fully dynamic/computed dispatch this pass
couldn't resolve. Left unimplemented in the port rather than guessing a
trigger condition. Leading (unconfirmed) guess if this is revisited: an
enemy lock-on/target health bar, given it's health-only with no
magicka/fatigue counterpart.

**Equipped-item icons** (`FUN_1002bb54`, decompiled in full): draws
the left/right hand's currently-equipped item icon (via `player+0x3ac
+0x48`/`+0x4c`, the same hand-slot fields `UpdateEquipStatus` already
identified) at `(5,5)`/`(139,5)` — flanking the compass banner in the
same top HUD row.

**Implemented in the port** (`port/src/main.cpp`'s `RenderHud`): the
compass and the real 3-bar vitals cluster (health/magicka/fatigue, slot
180 frame + slots 162/160/161 fills) draw the real assets at the real
positions; `Backbuffer::BlitRegion` (new, alongside the existing
`Blit`) supports both the compass's source-X-scroll and each bar's
destination-width clip. The port has no equivalent 16-bit fixed-point
heading field to replicate the exact byte-extraction formula, so
`RenderHud`'s heading-to-scroll-offset mapping is a documented,
unverified-direction best effort from `Camera::yaw` (a float radian),
not a decompiled formula — flagged in the port code itself. Equipped-
item icons use the existing per-zone icon-loading path this session's
earlier sprite work (see above) already established. `FUN_1002c010`'s
big single bar (see open item above) is not implemented.

## Open follow-ups

- `FUN_1006bee8`/`FUN_1006be58` (rect/outline fill primitives used
  throughout the UI code above) are still unidentified beyond "simple
  fill/line helpers" — low priority, their effect is already visible
  and reproducible without knowing their exact internals.
- The single-character category string comparison in `FUN_10024c8c`
  (`param_2` vs. `"m"`) and the two context-object fields noted above —
  not needed for the file-format question this pass answered.
