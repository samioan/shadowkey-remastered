# The actor/entity 3D polygon renderer

Following `GRAPHICS_FORMAT.md`'s open lead (search for a division/reciprocal
operation combined with tile access — perspective projection needs a
divide-by-depth somewhere), this thread found a genuine **perspective-correct
textured 3D polygon pipeline**. This is the strongest evidence yet that
Shadowkey renders true 3D geometry, not sprites — directly confirming the
mid-project correction in `WORLD_MODEL.md` (level *data* is a 2D grid; the
*rendering* is first-person 3D).

**Scope caveat**: every caller traced in this pass draws an **actor/entity's
3D model** (NPCs, monsters, held items — see "Where this is called from"
below). Whether the static dungeon walls/floors/ceilings go through this same
pipeline or a separate one is still unconfirmed — see Open follow-ups.

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
- `FUN_10083490` → `FUN_10086280` (virtual-dispatch only) — looks up an
  **animation-frame model pointer** from a per-engine table
  (`engine+0x6b38 + frameIndex*4`, indexed by `actor+0x2c2`, a 16-bit
  "current frame" field), copies the actor's full transform block
  (`actor+0x94..0x80`, i.e. position/orientation/scale), then renders that
  frame's model — the animated-model "draw my current keyframe" path.

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

## Open follow-ups

- **Does static dungeon geometry (walls/floors/ceilings) use this same
  pipeline?** No caller of `Poly3D_ClipAndDispatch`/`Actor3D_TransformAndSubmitModel`
  found so far touches `Map_GetTileAt` or tile data — all three callers are
  actor/entity-shaped. Either walls are built into per-cell "model" resources
  fed through the *same* `Actor3D_TransformAndSubmitModel`/vertex-list path
  (plausible — the model format already carries an arbitrary vertex+face
  list), or there's a still-unfound separate wall rasterizer. Worth checking:
  does the per-engine animation-frame table at `engine+0x6b38` or a sibling
  table hold per-*tile-type* wall models (indexed by `Map_GetTileAt`'s
  returned tile-type byte) rather than per-actor animation frames?
- The other ~9 `Poly3D_RasterizeTextured` variants (`FUN_10051460` etc.) are
  unexamined — likely: opaque vs. blended, "near" (partially-clipped) vs.
  normal, and a flat/unlit variant, based on the flag bits seen selecting
  between them in `Poly3D_ClipAndDispatch`.
- Confirm what `engine+0x5b4` (the render target `Poly3D_RasterizeTextured_v0`
  actually writes into, with a 704-byte row stride) is, and how/when it gets
  composited into the real `engine+0x480` 176×208 framebuffer.
- Identify the 3D model resource format read at `actor->model+0x54` (vertex
  list, face list, texture-atlas-cell index table) — presumably decoded from
  an on-disk asset alongside the sprite/icon format from `GRAPHICS_FORMAT.md`.
