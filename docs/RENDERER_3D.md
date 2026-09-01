# The actor/entity 3D polygon renderer

Following `GRAPHICS_FORMAT.md`'s open lead (search for a division/reciprocal
operation combined with tile access — perspective projection needs a
divide-by-depth somewhere), this thread found a genuine **perspective-correct
textured 3D polygon pipeline**. This is the strongest evidence yet that
Shadowkey renders true 3D geometry, not sprites — directly confirming the
mid-project correction in `WORLD_MODEL.md` (level *data* is a 2D grid; the
*rendering* is first-person 3D).

**Update**: the follow-up question below — do static walls/floors/ceilings use
this same pipeline — is now answered. See "The room/wall geometry renderer"
section near the bottom: rooms share the core clip/perspective/rasterize
machinery but go through their own top-level entry point and rasterizer, not
`Actor3D_TransformAndSubmitModel`/`Poly3D_ClipAndDispatch`. The scope caveat
below describes the actor-only pipeline as it was understood before that
follow-up.

## The pipeline, top to bottom

1. **`Actor3D_TransformAndSubmitModel`** (renamed from `FUN_10056eb0`,
   0x10056eb0) — given an actor object (`param_2`) and its 3D model resource
   (`param_2+0x54`: vertex list + face list + a texture-atlas-cell index
   table), picks a texture-atlas cell size class from a field near the end of
   the model header, computes the actor's position relative to the camera
   (`actor+0x94/+0x9c - camera+0x94/+0x9c`, the same 8.8 fixed-point position
   fields already known from the actor-collision code in
   `GRAPHICS_FORMAT.md`), builds a combined rotation+translation transform via
   `BuildRotationMatrix3x4`/`ComposeTransform3x4` (below), then walks the
   model's vertex list applying that transform per-vertex before handing each
   face off to `Poly3D_ClipAndDispatch`.

2. **`BuildRotationMatrix3x4`** (renamed from `FUN_10073a70`, 0x10073a70) —
   `BuildRotationMatrix3x4(dst12, yaw, pitch, roll, tx, ty, tz)`. Builds a
   **3×3 Euler rotation matrix + translation column** (a 3×4 affine transform,
   12 `int`s: `dst[0,1,2]`/`[4,5,6]`/`[8,9,10]` are the rotation rows,
   `dst[3]/[7]/[0xb]` are `tx/ty/tz`) from three angle inputs, using a
   **2048-entry sin/cos lookup table** (`DAT_10073bc8`, indexed
   `(angle+0x4000>>5) & 0x7ff` for cos / `(angle>>5) & 0x7ff` for sin — i.e.
   angles are a 0–0x2000 (13-bit) circle, matching the automap's rotated
   player-marker code in `WORLD_MODEL.md`/`GRAPHICS_FORMAT.md`, which indexes
   the *same-shaped* table `DAT_1002b6c4`). All arithmetic is 8.8-ish
   fixed-point (`>>8` after each multiply).

3. **`ComposeTransform3x4`** (renamed from `FUN_100738c4`, 0x100738c4) —
   `ComposeTransform3x4(dst, matA, matB)`. Matrix-multiplies two 3×4
   transforms together (3 rows × 3 columns, ignoring the translation column
   in the multiply itself) — used to combine an actor's local model-space
   rotation with its parent/attachment transform (e.g. a weapon attached to a
   hand bone, per `FUN_10065f7c`'s caller below).

4. **`Poly3D_ClipAndDispatch`** (renamed from `FUN_10056324`, 0x10056324) —
   `Dispatch(engine, vertices[], vertexCount, fbPtr, ..., flags, mode)`. Runs
   the polygon through `Poly3D_ClipAgainstPlane` up to 4 times (near-plane
   clip, detected from a per-vertex Z value against a near-clip threshold at
   `engine+0xbe10`, plus further clip passes), does a **2D cross-product
   backface/winding test** on the clipped result, repacks each surviving
   vertex's screen X/Y (`+0x14/+0x18`), texture U/V (`+0x1c/+0x20`), Z
   (`+0x08`, sign-extended from the source vertex's 16-bit `+4` field) into
   a local per-vertex record (stride `0x2c`), then dispatches to **one of
   10 specialized rasterizer functions**, `Poly3D_RasterizeTextured_v0`
   through `_v9` — now all individually identified, see "The 10
   `Poly3D_RasterizeTextured` variants" below.

5. **`Poly3D_ClipAgainstPlane`** (renamed from `FUN_1005c8b4`, 0x1005c8b4) —
   a Sutherland–Hodgman-style single-plane polygon clip: walks the vertex
   ring, and for each edge that crosses the plane (sign-bit flip on a `+8`
   field), **interpolates a new vertex** (position, U, V) at the crossing
   point using a reciprocal-based interpolation factor from `DAT_1005c968`/
   `DAT_1005cb94`. Newly-created vertices are allocated from a per-frame pool
   at `engine+0xc5c` (a bump-allocated counter) into scratch space at
   `engine+0xc60`. When the "camera-space" scale factor is exactly `0x100`
   (i.e. `1.0` in 8.8 fixed point — a special-cased identity/no-extra-scale
   path), it also does the actual **perspective divide**:
   `screenX = 0x5800/z * x + 0x5800`, `screenY = 0x6800 - 0x6800/z * y`, using
   the real ARM division routine `EUSER____divsi3` (no hardware divider on
   ARM7TDMI/ARM920T). `0x5800` = `0x58*0x100` = 88·256 and `0x6800` =
   `0x68*0x100` = 104·256 — half of 176 and half of 208, the screen center in
   8.8 fixed point. This is the textbook `screen = center + (world * scale) /
   depth` perspective projection.

6. **`Poly3D_RasterizeTextured` variant 0** (renamed from `FUN_100509a4`,
   0x100509a4; one of the ~10 dispatch targets above, all presumably
   siblings) — a classic **scanline edge-walking polygon fill**: two edge
   walkers (`local_2c`/`local_44`, one per side) step around the clipped
   polygon's vertex ring as the scanline `y` advances from the top vertex to
   `min(bottom vertex, 0xcf)` (207 — the screen's last row, confirming this
   writes directly into the 176×208 framebuffer described in
   `GRAPHICS_FORMAT.md`). Per scanline, edge deltas are computed using a
   **reciprocal-of-height lookup table** (`DAT_10050e34`) instead of a divide
   (classic technique to avoid the ARM's slow division routine per-scanline).
   Per span, the **U/V texture coordinates are perspective-corrected** via a
   **1/z reciprocal table split into two precision bands** (`DAT_10051038`
   for near/large-1-over-z values, `DAT_10050f54` for far/small ones, selected
   by comparing the interpolated depth against `0x800000`/`0x8000000`) — the
   standard software-renderer trick for affine-per-scanline,
   perspective-correct-per-pixel-or-per-span texture mapping without a
   hardware FPU or divider. Output row stride into the target buffer is
   `0x2c0` (704) bytes — **not** the 352-byte/176px screen stride from
   `GRAPHICS_FORMAT.md`, and it reads from `engine+0x5b4` rather than the
   `engine+0x480` framebuffer pointer passed in as an argument — this is
   the shared 176×208-at-4-bytes/pixel intermediate buffer, confirmed and
   explained fully by `CompositeSceneBufferToScreen` below. Each drawn
   pixel is gated by a **real per-pixel depth test** against that buffer's
   existing contents (`if (newZ*0x10000 < *existingWord) { draw }`) using
   an interpolated Z value carried in the packed vertex's `+8` field, then
   `color | (newZ*0x10000)` is stored back — i.e. the high 16 bits of every
   texel in this buffer are a genuine same-buffer occlusion depth, not
   padding. Texel value `0x0f0f` is a chroma-key: skip the pixel entirely
   (texture cutouts, e.g. foliage/grates). See "The 10
   `Poly3D_RasterizeTextured` variants" below for how the other 9 build on
   this.

## Where this is called from (all actor/entity rendering)

- `FUN_10064ffc` (called via `thunk_FUN_10064ffc`) — picks a static/idle
  "pose" model or an animated frame via a virtual call
  (`actor->vtable[+0x170]`), then calls `Actor3D_TransformAndSubmitModel`.
  Looks like the per-actor "draw my current pose" entry point.
- `FUN_10065f7c` — computes a rotation matrix for an **attachment offset**
  (`FUN_1000336c` returns 6 shorts consumed as 3 axis vectors), applies it to
  write a *child* object's position/orientation (`actor+0x138`, e.g. a
  weapon or shield attached to a hand/bone) from the *parent* actor's
  transform, then renders that child via `Actor3D_TransformAndSubmitModel`.
  Only reachable via virtual dispatch (no direct callers found) — an
  `Attach::Render`-style virtual method.
- `FUN_10083490` → `FUN_10086280` (virtual-dispatch only) — looks up a
  **model pointer** from a per-engine cache table (`engine+0x6b38 +
  modelIndex*4`, indexed by `actor+0x2c2`), copies the actor's full
  transform block (`actor+0x94..0x80`, i.e. position/orientation/scale),
  then renders that model. **Correction** (see `ZONE_FORMAT.md`):
  `actor+0x2c2` isn't a literal animation-frame counter as first guessed —
  `engine+0x6b38` is the global `models.idx`-archive-index-keyed model
  cache populated per-zone from `<zone>_models.txt`, so `actor+0x2c2` is
  the entity's assigned **model archive index**, set once when the entity
  is placed from `<zone>.ent`. The per-model vertex-animation *frame* (the
  MD2-style keyframe within one model resource) is the separate `actor+0x70`
  field documented in `MODEL_FORMAT.md`.

All three confirm actors are driven by a **real 3D model + keyframe animation
system**: model resources are unpacked per current frame index, transformed
by a full Euler rotation, and rasterized with perspective-correct texturing.
None of them touch `Map_GetTileAt` or any tile-grid state — this pipeline
looks dedicated to *actors*, not to the static dungeon geometry.

## The 10 `Poly3D_RasterizeTextured` variants

`Poly3D_ClipAndDispatch` picks one of 10 near-identical rasterizers per
polygon, keyed off three independent conditions, fully resolved by
decompiling and diffing all 10 against each other:

- **`bVar1`** ("near"): true if any vertex's clip-space Z was below the
  near-clip threshold at `engine+0xbe10` — i.e. this polygon actually got
  clipped by `Poly3D_ClipAgainstPlane`. Near variants are consistently
  **~2.5× the code size** of their non-near counterparts (extra per-edge
  bookkeeping for clipped polygons); the color/depth/texture logic itself is
  otherwise identical.
- **fade**: `*(char *)(engine+0xbe0f) != 0`. Fade variants additionally
  compute, per vertex, `intensity = clamp(vertex[+8] * engine[+0x5c8] >> 8,
  0, 0xffff)` — i.e. **depth scaled by a global factor** — interpolate it
  across the polygon, and OR its top nibble into the output color word
  before writing. `CompositeSceneBufferToScreen` then runs the *entire*
  16-bit color+nibble word through the `engine+0x5c4` lookup table — a
  torchlight/distance-fog effect keyed by depth, not a flat filter.
- **stencil**: selected by `Poly3D_ClipAndDispatch`'s mode-word bit 1. The
  mode-word parameter is repurposed from a bitmask into a literal **byte**
  value (`param_6`), stamped into a *second* 176×208 8bpp framebuffer plane
  at `engine+0x5b8` (stride `0xb0`) for every pixel actually drawn — an
  object-ID/picking buffer, not alpha blending. (Renaming this the "blend"
  family, as originally guessed, would have been wrong.)

| Variant | Address | near | stencil | fade | Notes |
|---|---|---|---|---|---|
| `_v0` | 0x100509a4 | Y | N | N | baseline near-clip case |
| `_v1` | 0x10051460 | Y | N | Y | |
| `_v2` | 0x10052044 | N | N | N | **the baseline** — plain scanline/1-over-z/chroma-key/depth-test rasterizer everything else builds on |
| `_v3` | 0x10052504 | N | N | Y | |
| `_v4` | 0x100536f8 | Y | Y | N | |
| `_v5` | 0x10052ab8 | Y | Y | Y | |
| `_v6` | 0x1005420c | N | Y | N | |
| `_v7` | 0x10054704 | N | Y | Y | |
| `_v8` | 0x10054d04 | Y | Y | N | plus an extra forwarded parameter (`Poly3D_ClipAndDispatch`'s own `param_7`, passed when it's `!= -1`) not yet deciphered |
| `_v9` | 0x10055a4c | — | — | — | selected by mode-word bit 0, orthogonal to the other three. Structurally distinct: writes straight into the **real** 16bpp screen buffer (`engine+0x480`, 0x160-byte stride) with an unconditional store — no depth test, no OR'd high bits — bypassing the shared padded-buffer scheme entirely. Also unconditionally stores a raw interpolated accumulator into `engine+0x5b4` at the same slot (purpose not pinned down). Likely an always-on-top / no-occlusion draw path (candidates: player's held weapon, a UI element routed through the 3D pipeline). |

Every variant (`_v9` included) treats texel value `0x0f0f` as a **chroma
key**: skip the pixel, don't draw — texture cutouts (foliage, grates, etc.),
not real alpha blending. None of the 10 do per-pixel alpha compositing
anywhere — "blended" was the wrong guess for what distinguishes them.

Remaining open items: `_v8`'s extra parameter, and `_v9`'s second
(unconditional, untested) write into `engine+0x5b4` — both low priority,
the dispatch/behavior split above is otherwise fully resolved.

## Labels applied

- `Actor3D_TransformAndSubmitModel` (0x10056eb0)
- `Poly3D_ClipAndDispatch` (0x10056324)
- `Poly3D_ClipAgainstPlane` (0x1005c8b4)
- `Poly3D_RasterizeTextured_v0` through `_v9` (0x100509a4, 0x10051460,
  0x10052044, 0x10052504, 0x100536f8, 0x10052ab8, 0x1005420c, 0x10054704,
  0x10054d04, 0x10055a4c)
- `BuildRotationMatrix3x4` (0x10073a70)
- `ComposeTransform3x4` (0x100738c4)

## The room/wall geometry renderer

Answering the open question above: static dungeon geometry (walls, floors,
ceilings) is **not** rendered through `Actor3D_TransformAndSubmitModel`/
`Poly3D_ClipAndDispatch` — every caller of those two is unambiguously
actor-shaped (confirmed by re-checking: `Poly3D_ClipAndDispatch` has exactly
one caller, `Actor3D_TransformAndSubmitModel`, and each of its ~10 rasterizer
targets has exactly one caller, `Poly3D_ClipAndDispatch`). Instead, rooms have
their **own top-level render path** that reuses the same underlying
primitives (rotation matrix, perspective divide, Sutherland-Hodgman clip) but
with a different structure — found by tracing `BuildRotationMatrix3x4`'s
other caller, `FUN_10057890`.

- **`RoomGeometry_TransformAndSort`** (renamed from `FUN_10057890`,
  0x10057890) — given the current room object (`engine+0x62c`) and its 3D
  model resource at `room+0x54` (**the exact same field offset and format** —
  vertex list + face list + texture-atlas-cell table — as an actor's
  `actor+0x54`, confirming rooms and actors share one generic 3D model
  format), transforms and perspective-projects **every vertex of the whole
  room model in one pass** (same `0x5800`/`0x6800` + `EUSER____divsi3`
  projection formula as `Poly3D_ClipAgainstPlane`) into a fixed per-engine
  vertex buffer (`engine+0x7738`). It then **depth-sorts every face into 64
  Z-buckets** (`engine+0xbd88`, bucketed by `-z>>5`, clamped `0..0x3f`) and
  walks the buckets in order, calling `RoomFace_ClipAndDispatch` once per
  face — a classic **painter's-algorithm bucket sort**, architecturally
  distinct from the actor pipeline's immediate per-face clip-and-dispatch
  with no global sort (actors instead rely on the near-clip/backface tests
  alone, since there are far fewer polygons per actor and overlap is rare).

- **`RoomFace_ClipAndDispatch`** (renamed from `FUN_10056aa0`, 0x10056aa0) —
  the room-geometry counterpart to `Poly3D_ClipAndDispatch`: clips one room
  face against up to 4 planes by **calling `Poly3D_ClipAgainstPlane`
  directly** (reusing the actor pipeline's clip routine verbatim — this is
  the strongest piece of evidence the two renderers are siblings, not
  independent implementations), repacks the surviving vertices, then always
  calls a single dedicated rasterizer (no ~10-way variant dispatch — rooms
  don't need the actor pipeline's blend-mode variants).

- **`RoomFace_RasterizeTextured`** (renamed from `FUN_10055f38`, 0x10055f38)
  — a dedicated scanline rasterizer, structurally similar to
  `Poly3D_RasterizeTextured_v0` (same reciprocal-of-height-LUT edge-walking
  technique, own copy of the LUT at `DAT_10056310`) but **simpler**: plain
  affine per-scanline UV stepping, no 1/z perspective-correction pass —
  because room faces were already perspective-projected per-vertex by
  `RoomGeometry_TransformAndSort` before clipping, unlike actors whose
  rasterizer perspective-corrects U/V per-span. Writes into `engine+0x5b4`
  with the same `0x2c0` (704-byte) row stride as every actor rasterizer
  variant — **confirming rooms and actors render into the same intermediate
  buffer**, which resolves the `engine+0x5b4` question below.

- **`Render3DScene`** (renamed from `FUN_100166c8`, 0x100166c8) — the
  per-frame master entry point, called once per frame from a screen-state
  switch statement (`FUN_10068e0c`, case `5`). Sequence: (1) if the current
  room has a model (`engine+0x62c` → `+0x54 != 0`), calls
  `RoomGeometry_TransformAndSort`; otherwise flat-fills `engine+0x5b4` (no
  room loaded — a void/black screen case). (2) Renders actors via **virtual
  dispatch through vtable offset `0x170`** — the exact same virtual slot
  `Actor3D_TransformAndSubmitModel`'s caller `FUN_10064ffc` reads to pick a
  pose/animation-frame model — confirming actor rendering also happens
  inside this same per-frame function, just reached through a polymorphic
  per-entity call rather than a direct call Ghidra's static analysis can see.
  (3) Calls `CompositeSceneBufferToScreen` to finish the frame.

- **`CompositeSceneBufferToScreen`** (renamed from `FUN_1005dfe0`, 0x1005dfe0)
  — answers the `engine+0x5b4` question directly. The buffer is **176×208 at
  4 bytes/pixel** (not double-width as first guessed — `0x2c0` = 704 bytes /
  4 = 176, matching screen width exactly): the low 16 bits hold the real
  16bpp color, the high 16 bits hold the per-pixel interpolated **depth**
  value every rasterizer wrote for its same-buffer occlusion test (see the
  variant table below — **corrects** an earlier claim in this doc that those
  bits were a meaningless constant `0x7fff`; no `0x7fff` literal exists
  anywhere in the rasterizer code, and the depth interpretation is confirmed
  by tracing the packed vertex's `+8` field back to
  `Poly3D_ClipAndDispatch`'s clip-space Z). This function masks each 32-bit
  source word with `& 0xffff` — discarding that depth data, its job is done —
  and packs two adjacent 4-byte source pixels into one 4-byte destination
  write, converting the padded intermediate buffer down into the real 16bpp
  `engine+0x480` screen buffer — i.e. `engine+0x5b4` exists so the software
  rasterizers can write full 32-bit-aligned pixels (faster on ARMv4T than
  unaligned 16-bit stores) *and* get a free per-pixel depth test almost for
  free, then get packed to real 16bpp only once, at the end of the frame.
  When a flag (`engine+0xbe0f`) is set, composite instead runs the full
  16-bit color word (color + a fog nibble some rasterizer variants OR into
  its top bits — see below) through a 2-byte-stride lookup table at
  `engine+0x5c4`, indexed by that entire 16-bit word (`index = word*2`,
  table size/exact indexing range not pinned down) — confirming the
  torchlight/distance-fog guess.

**Bottom line**: Shadowkey has **one 3D model format and one clip/perspective
core** (`BuildRotationMatrix3x4`, `ComposeTransform3x4`, `Poly3D_ClipAgainstPlane`)
shared by three renderers built on top of it — an actor renderer (immediate
per-face dispatch, 10 rasterizer variants split by near-clip/fog/stencil-ID
needs), a room renderer (whole-model transform + depth-bucket sort, one
rasterizer variant), and a tile-grid wall/surface-face renderer (see below,
4 more rasterizer variants) — all writing into one shared intermediate
buffer that gets composited to the screen once per frame.

## Labels applied (room/wall renderer)

- `Render3DScene` (0x100166c8)
- `RoomGeometry_TransformAndSort` (0x10057890)
- `RoomFace_ClipAndDispatch` (0x10056aa0)
- `RoomFace_RasterizeTextured` (0x10055f38)
- `CompositeSceneBufferToScreen` (0x1005dfe0)

## The tile-grid wall/surface-face renderer: a third pipeline

Found while chasing what `.ztx`/`.zlu` (`ZONE_FORMAT.md`) actually contain.
`Render3DScene` has a large (~500-line) block, not traced in full here,
that walks the level's tile grid (the same 2D grid `Map_GetTileAt`
indexes, per `WORLD_MODEL.md`) looking for tile-adjacent faces that need
their own draw call — distinct from both the actor pipeline above and the
`.zsk`-baked whole-room mesh (`RoomGeometry_TransformAndSort`). Each such
face is drawn by a **third, parallel rasterizer family** that reuses the
same clip core but has its own dispatcher and its own texture/color
scheme:

- **`SurfaceFace_BuildAndProject`** (renamed from `FUN_1005d784`,
  0x1005d784) — called once per exposed tile face from `Render3DScene`'s
  traversal. Takes a `.sur` record index (the per-face "surface" — see
  `ZONE_FORMAT.md` for the now-fully-decoded 8-byte record: U/V bit-shift
  scale, U/V offset, a flags byte, a clamped surface/texture index),
  builds a quad's 4 corner UVs from those fields (also sampling the
  `engine+0x6904` grid `Bullseye_InitMap` allocates — the first consumer
  found for that array, previously an open item), perspective-projects
  them with the **exact same formula and per-frame scratch pool**
  (`engine+0xc5c`/`+0xc60`) as `Poly3D_ClipAgainstPlane`, then calls
  `SurfaceFace_ClipAndDispatch` once per triangle.
- **`SurfaceFace_ClipAndDispatch`** (renamed from `FUN_1005d074`,
  0x1005d074) — clips via `Poly3D_ClipAgainstPlane` (the *third* caller of
  that routine, alongside the actor and room pipelines — strong evidence
  all three are siblings on one shared core), then dispatches to one of 4
  rasterizer variants by (near vs. far vertex depth) × (the same
  `engine+0xbe0f` fade flag the actor pipeline uses). Passes
  `*(engine+0x6b20) + surfaceIndex*0x4000` as the texture pointer — this
  is **`.ztx`'s role, fully resolved**: a flat wall-texture atlas, one
  `0x4000`-byte (16384-byte) slot per surface index. It also forwards one
  of **`.zlu`'s 4 selected 512-byte chunks** (`engine+0x6b24 +
  2-bit-selector*4`, the selector taken from bits 4-5 of the caller's
  per-face material byte) as an extra pointer argument.
- **`SurfaceFace_RasterizeTextured_v0`..`_v3`** (renamed from
  `FUN_1005a9e0`/`FUN_10059970`/`FUN_1005c1a4`/`FUN_1005bbc8`) — the 4
  dispatch targets (near+fade, near+no-fade, far+fade, far+no-fade). All
  4 now traced. `_v3` (0x1005bbc8, far+no-fade) **resolves `.zlu`'s role**:
  it reads a `.ztx` texel as a single **byte** (not raw 16bpp — `.ztx` is
  **8bpp palettized**, unlike the actor/room pipelines' raw-16bpp
  textures), doubles it as an index, then reads the *final* 16bpp color
  out of the forwarded `.zlu` chunk at that index. So **`.zlu` is 4
  selectable 256-color palettes** (512 bytes = 256×2-byte RGB565-ish
  entries each) that convert a wall texture's indexed texels into real
  color — a classic same-texture-different-palette trick for varying a
  wall's lit/dark/themed look without duplicating texture data, selected
  per-face by 2 bits of a material byte. `_v2` (far+fade) is the same
  core plus the actor pipeline's `engine+0x5c8` fog-nibble scheme
  (Round E) OR'd into the color word, exactly like `Poly3D_
  RasterizeTextured`'s fade variants.

  **The near/far split here is not just clip-bookkeeping size** (unlike
  the actor pipeline, where `_v0`'s near variant differs from `_v2`'s
  far variant only by extra edge cases): `_v0`/`_v1` (near) write every
  pixel of a fixed 8-wide interpolation batch **unconditionally** — no
  chroma-key (`0x0f0f`) transparency check, no per-pixel depth test
  against `engine+0x5b4` — while `_v2`/`_v3` (far) gate every pixel on
  both, same as every actor/room rasterizer. Near wall segments are
  close enough to the camera that they're presumably always the
  frontmost thing drawn there, so the engine skips both checks for
  speed. Near variants are still ~3.5× the far variants' code size
  (23-25KB vs. 7-8KB decompiled) — that size comes entirely from
  near-clip edge-case bookkeeping, not from doing more per-pixel work; if
  anything they do *less* per pixel than their far counterparts. A
  secondary per-scanline/per-pixel offset into adjacent `0x200`-byte
  blocks within the `.zlu` chunk (seen in all 4 variants) is **now
  decoded** — driven by the same per-vertex light/fog scalar the primary
  `.zlu` lookup already uses, confirming the "further distance-driven
  palette blend, analogous to the actor pipeline's fade LUT" guess — see
  Open follow-ups for the full mechanism.

This resolves `ZONE_FORMAT.md`'s last two open per-zone-file items
(`.ztx`, `.zlu`) and, as a side effect, fully decodes `.sur`'s 8-byte
record (previously only its count+stride framing was known) and finds the
first consumer of the `engine+0x6904` array `Bullseye_InitMap` allocates.

### The traversal: what decides which faces get a dynamic draw

Full writeup and the tile-record/type-table field layout live in
`WORLD_MODEL.md`'s "Per-frame tile-visibility raycasting" section
(this is fundamentally a tile-grid question, and that doc already owns
`Map_GetTileAt`/the tile record/the type table) — summary:

1. **`TileGrid_RaycastVisibility`** (renamed from `FUN_1000f694`,
   0x1000f694), called once per frame from `Render3DScene` before its
   face-drawing loop, casts a fan of rays (150/177/178, by a quality
   setting) from the camera using the same sin/cos LUT as
   `BuildRotationMatrix3x4`, stepping tile by tile until a wall tile
   (`flags` bit1 — the *same* bit `Bullseye_PropagateLight`'s light rays
   stop at) blocks it or it runs out of range (25/93/172 tiles,
   quality-tiered). Every tile crossed gets deduplicated (a newly-decoded
   per-tile "last visible frame" byte) into a per-frame visible-tile list.
2. `Render3DScene`'s main loop walks *that list* (not a fixed-radius or
   full-grid scan) and, per tile, checks the relevant neighbor's 36-byte
   `.zcp` type-table entry for a per-direction `.sur`-index byte —
   **every direction now mapped**, see `ZONE_FORMAT.md`'s `ZcpEntry`
   struct: `0x16`-`0x19` = east/west/south/north wall, lower height band;
   `0x1a`-`0x1d` = the same four directions' upper band (walls can draw
   up to two stacked segments, e.g. for a stepped floor/ceiling height
   difference with a neighbor); `0x1e`/`0x1f` = two ceiling bands
   (selected by a height comparison against the camera's eye-height field
   and a `flags` bit); `0x20` = floor (single band). `0xff` in any of
   these means no face there. When a face is defined, it still only draws
   if the camera is on the correct side of that tile boundary (an
   implicit backface/already-passed cull) **or** the tile's `flags` bit3
   forces it regardless (exact intended use not confirmed — plausibly
   "always double-sided").

So the wall/surface pipeline isn't drawing every tile boundary every
frame — it's gated by (a) a genuine visibility raycast and (b) a
per-direction, per-height-band "does this tile-type even have a face
here" check baked into the shared `.zcp` type table, with a small
explicit override for tiles that need a face regardless of camera
position.

## Labels applied (surface/wall-face renderer)

- `SurfaceFace_BuildAndProject` (0x1005d784)
- `SurfaceFace_ClipAndDispatch` (0x1005d074)
- `SurfaceFace_RasterizeTextured_v0` (0x1005a9e0)
- `SurfaceFace_RasterizeTextured_v1` (0x10059970)
- `SurfaceFace_RasterizeTextured_v2` (0x1005c1a4)
- `SurfaceFace_RasterizeTextured_v3` (0x1005bbc8)

## Open follow-ups

- ~~The other ~9 `Poly3D_RasterizeTextured` variants~~ — **resolved**, see
  "The 10 `Poly3D_RasterizeTextured` variants" above: near-clip, a
  depth-driven fog LUT, and a stencil/object-ID buffer, not blend modes.
  Two small leftovers: `_v8`'s extra forwarded parameter, and `_v9`'s
  second, unconditional write into `engine+0x5b4`.
- ~~What sets `engine+0x62c` (current room pointer) on room/level
  transitions~~ — **resolved**: nothing "sets" it on transitions at all.
  `engine+0x62c` is a single 0x160/352-byte room-render-state object,
  allocated exactly **once** in `GameEngine_ctor` (`new(0x160)` +
  constructor call at 0x10067898) and never reassigned again. Room
  transitions instead **overwrite its fields in place** — `GameEngine_
  InitLevel` sets `(*(engine+0x62c))+0x54` (the model pointer) straight
  from the freshly-loaded **`<zone>.zsk`** file every level load. **This
  corrects an earlier version of this note that named `.zon` here — it's
  wrong, `.zon` populates the separate `engine+0x5464` room-*list* array
  only; `.zsk` is what actually feeds the model pointer that
  `RoomGeometry_TransformAndSort` renders.** See `ZONE_FORMAT.md`'s new
  "Compressed per-zone files" section for the full correction and how this
  was caught. So there's exactly one live "current room" render object per
  engine instance, reused for whichever room is active — its geometry
  comes straight from `.zsk`, decompressed and parsed as an ordinary
  `MODEL_FORMAT.md`-format resource (**verified**: decompressing a real
  `azra.zsk` yields a header that decodes exactly per that spec, including
  the `H5==H2*3` invariant).
- What `FUN_10068e0c`'s other switch cases are (it's a general screen-state
  machine; case `5` is confirmed as "render the 3D game view", cases `1` and
  `6` look like menu/list UI — not traced in this pass).
- The `engine+0xbe0f`/`engine+0x5c4` fade/lighting lookup-table path is now
  understood structurally (a depth-driven color-remap table indexed by the
  full color+fog-nibble word, fed by the fade-family rasterizer variants —
  see above) but its actual **contents** (what darkness/color curve it
  encodes) haven't been dumped from a real binary/asset — worth doing if a
  PC port wants to preserve the torchlight falloff look.
- ~~The tile-grid traversal inside `Render3DScene` that decides *which*
  faces get a dynamic draw~~ / ~~exactly which `.zcp` byte maps to which
  face direction~~ — **both resolved**, see "The traversal: what decides
  which faces get a dynamic draw" above and `ZONE_FORMAT.md`'s `ZcpEntry`
  struct: every wall direction (×2 height bands), both ceiling bands, and
  floor now mapped to a specific byte offset. `flags` bit0/bit1/bit3/bit6
  are now known (light source / wall / force-draw / ceiling-band select);
  bit2's exact role (also forces a floor draw, per the traversal, but not
  otherwise pinned down), byte 7 of the tile record, and 3 trailing bytes
  of the 36-byte type table remain undecoded.
- ~~The `heightA`/`heightB` fields' precise sub-structure~~ — **resolved**,
  by decompiling `SurfaceFace_BuildAndProject` (0x1005d784) and
  `Render3DScene`'s (0x100166c8) floor/ceiling/wall-band blocks in full and
  matching every 16-bit read in that byte range against a specific offset.
  What looked like "heightA + 14 undecoded bytes + heightB" is actually
  **9 separate `int16` fields**: one standalone scalar (`0x04`, the default
  ceiling-band comparison threshold) plus two clean 4-element corner-height
  arrays — `floorHeight[4]` (`0x06`/`0x08`/`0x0a`/`0x0c`, the floor quad's 4
  corners) and `ceilingHeight[4]` (`0x0e`/`0x10`/`0x12`/`0x14`, the ceiling
  quad's 4 corners, used unchanged for both ceiling bands). The field
  previously documented as an independent "`heightB`" turned out to just be
  `ceilingHeight[3]` (offset `0x14`) doing double duty as the
  ceiling-band-selection scalar when `flags` bit6 is set — not separate
  data. Wall lower/upper bands read the relevant array cross-tile (current
  vs. neighbor) to size their vertical extent. Full struct + prose in
  `ZONE_FORMAT.md`'s `ZcpEntry`.
- ~~`SurfaceFace_RasterizeTextured_v0`/`_v1`/`_v2` weren't traced~~ —
  **resolved**, see above: all 4 confirmed (`_v2` matches `_v3` plus the
  fade LUT; `_v0`/`_v1`'s near variants skip the chroma-key/depth-test
  checks entirely, a genuine behavioral split, not just extra clip-edge
  bookkeeping like the actor pipeline's near variants).
- ~~The secondary `param_8 + N*0x200`-style offset `SurfaceFace_
  RasterizeTextured_v3` adds before indexing into the `.zlu` palette
  chunk~~ — **resolved, and the original guess was right**: it's driven by
  the *same per-vertex light/fog scalar* already established elsewhere in
  this pipeline (the byte pair computed and clamped to `[0x400, 0x3f00]` in
  `SurfaceFace_BuildAndProject`, at vertex offset `+6`), reached here after
  `SurfaceFace_ClipAndDispatch` (0x1005d074) repacks each clipped vertex
  into the rasterizer's wider interpolation record. Confirmed by tracing
  `ClipAndDispatch`'s repacking writes (screen Y → record offset `0`,
  screen X → `+4`, a transformed depth term → `+8`) against two of the
  rasterizer's *other* field reads (`+0x1c`, `+0x20`) that land exactly on
  `ClipAndDispatch`'s otherwise-unaccounted writes of the vertex's UV pair
  (from vertex offsets `+0xc`/`+0x10`) once a consistent `+0x18` gap
  between the record's two stack halves is assumed — the same gap places
  the light/fog byte pair (vertex offset `+6`) exactly at the record offset
  (`+0x18`) `_v3` reads to drive this logic, which is strong corroborating
  evidence, not proof from a single read. Mechanism: `_v3` compares the
  light value at the scanline's left vs. right edge; if they're close
  (within roughly ±8.0 in the 8.8-ish fixed format), it takes a fast path —
  one extra `0x200`-block offset for the *whole scanline*, from their
  average masked down to the nearest `0x200` boundary; if they differ more,
  it falls back to recomputing that offset **per pixel** by linearly
  interpolating the light value across the scanline. Either way the result
  is added on top of the per-face-selected `.zlu` palette pointer — i.e.
  this is a second, finer-grained light-driven blend across the *already*
  per-face-selected palette, on top of (not instead of) the 4-way per-face
  palette selection documented above. Not independently re-verified by
  decompiling `_v0`/`_v1`/`_v2` for the identical pattern this pass (all 3
  are structurally close enough to `_v3` per the existing writeup that it's
  a safe bet, but that's an assumption, not a separate confirmation).
