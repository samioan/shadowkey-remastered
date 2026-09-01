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
   vertex's screen X/Y (`+0x14/+0x18`), texture U/V (`+0xc/+0x10`), into a
   local array, then dispatches to **one of ~10 specialized rasterizer
   functions** chosen by blend-mode/near-clip/shading flags (`FUN_100509a4`,
   `FUN_10051460`, `FUN_10052044`, `FUN_10052504`, `FUN_100536f8`,
   `FUN_10052ab8`, `FUN_1005420c`, `FUN_10054704`, `FUN_10054d04`,
   `FUN_10055a4c` — not individually renamed yet, see Open follow-ups).

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
   `engine+0x480` framebuffer pointer passed in as an argument — so this
   likely renders into a separate, wider (32bpp‑ish, or padded) intermediate
   render target that gets composited/converted afterward, not directly into
   the final 16bpp screen buffer. Unconfirmed; see Open follow-ups.

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

## Labels applied

- `Actor3D_TransformAndSubmitModel` (0x10056eb0)
- `Poly3D_ClipAndDispatch` (0x10056324)
- `Poly3D_ClipAgainstPlane` (0x1005c8b4)
- `Poly3D_RasterizeTextured_v0` (0x100509a4)
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
  16bpp color, the high 16 bits are a constant `0x7fff` every rasterizer ORs
  in (padding/alignment, not a meaningful flag — both the background-fill
  path and every draw path write the same constant). This function packs two
  adjacent 4-byte source pixels into one 4-byte destination write, converting
  the padded intermediate buffer down into the real 16bpp `engine+0x480`
  screen buffer — i.e. `engine+0x5b4` exists so the software rasterizers can
  write full 32-bit-aligned pixels (faster on ARMv4T than unaligned 16-bit
  stores) and get packed to real 16bpp only once, at the end of the frame.
  When a flag (`engine+0xbe0f`) is set, composite instead runs each channel
  through a lookup table at `engine+0x5c4` — likely a fade/lighting
  post-process (torchlight falloff, a level-transition fade, or similar).

**Bottom line**: Shadowkey has **one 3D model format and one clip/perspective
core** (`BuildRotationMatrix3x4`, `ComposeTransform3x4`, `Poly3D_ClipAgainstPlane`)
shared by two renderers built on top of it — an actor renderer (immediate
per-face dispatch, multiple blend-mode rasterizer variants) and a room
renderer (whole-model transform + depth-bucket sort, one rasterizer variant)
— both writing into one shared intermediate buffer that gets composited to
the screen once per frame.

## Labels applied (room/wall renderer)

- `Render3DScene` (0x100166c8)
- `RoomGeometry_TransformAndSort` (0x10057890)
- `RoomFace_ClipAndDispatch` (0x10056aa0)
- `RoomFace_RasterizeTextured` (0x10055f38)
- `CompositeSceneBufferToScreen` (0x1005dfe0)

## Open follow-ups

- The other ~9 `Poly3D_RasterizeTextured` variants (`FUN_10051460` etc.) are
  unexamined — likely: opaque vs. blended, "near" (partially-clipped) vs.
  normal, and a flat/unlit variant, based on the flag bits seen selecting
  between them in `Poly3D_ClipAndDispatch`.
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
- The `engine+0xbe0f`/`engine+0x5c4` fade/lighting lookup-table path in
  `CompositeSceneBufferToScreen` — not traced; likely relevant to any
  lighting/darkness effects worth preserving in a PC port.
