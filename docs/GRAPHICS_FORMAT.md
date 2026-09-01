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

## Open follow-ups

- The actual first-person 3D wall/floor/ceiling renderer is still not
  located. Best remaining ideas: identify `FUN_1006bee8`/`FUN_1006be58`
  (used here for rects/outlines — likely simple fill/line primitives,
  worth checking if a *textured* variant of one of them exists and is
  called with per-column width/height derived from a projected
  distance); or search for division/reciprocal operations (perspective
  projection needs a divide-by-depth somewhere) combined with
  `Map_GetTileAt`-style tile access.
- What the 384-slot image cache (`engine+0x4460`/`+0x4a60`) actually
  holds — probably all loaded sprite/icon bitmaps, decoded from some
  on-disk asset format into this in-RAM RLE representation. Where that
  decode happens (turning a `.ztx`/similar asset file into this
  in-memory layout) is unconfirmed.
