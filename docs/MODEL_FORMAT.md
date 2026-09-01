# The 3D model resource format

`RENDERER_3D.md` established that actors and rooms share one generic 3D
model format, read from `actor+0x54` / `room+0x54`. This thread reconstructs
that format's exact on-disk (in-memory resource) layout by reading every
byte-level field access in the two functions that consume it —
`Actor3D_TransformAndSubmitModel` (0x10056eb0) and
`RoomGeometry_TransformAndSort` (0x10057890) — and cross-checking that both
independently agree on the same field roles and offsets. Where the two
functions compute the exact same formula from the same header bytes (the
texture-block offset, see below), that's strong corroborating evidence the
interpretation is right, not just self-consistent guesswork from one call
site.

All multi-byte fields are little-endian. Offsets below are from the start of
the model resource (the pointer stored at `actor+0x54`/`room+0x54`).

## Header (12 bytes)

| Offset | Field | Meaning |
|--------|-------|---------|
| `0x00` | `H0` (i16) | Vertex table base, in **halfwords** (2-byte units) from the resource start — start of frame 0's vertex data |
| `0x02` | `H1` (i16) | Animation frame count |
| `0x04` | `H2` (i16) | Vertex count per frame |
| `0x06` | `H3` (i16) | UV-coordinate table entry count |
| `0x08` | `H4` (i16) | Face (triangle) count |
| `0x0a` | `H5` (i16) | Halfwords per animation frame's vertex block (expected to equal `H2*3` — 3 halfwords/vertex — unverified against real resource bytes, since no resource file has been located yet; see Open follow-ups) |

Everything past the header is a sequence of tables, each immediately
following the previous one — no padding or alignment observed:

```
vertex table   (H1 frames x H2 vertices x 6 bytes)      starts at halfword H0
UV table       (H3 entries x 4 bytes)                   starts at halfword H0 + H1*H5
face table     (H4 faces x 12 bytes)                    starts at halfword (H0 + H1*H5) + H3*2
texture header (4 halfwords)                            starts at halfword (face table base) + H4*6
texture pixels (N skins x width*height x 2 bytes)       starts at halfword (texture header base) + 4
```

## Vertex table

`H1` consecutive frames, each `H2` vertices, each vertex **6 bytes**: three
little-endian `int16`s, `(x, y, z)` in the model's local space (same 8.8
fixed-point convention used everywhere else in the engine — unconfirmed for
model-local coordinates specifically, but consistent with every other
position field in this codebase).

The active frame is selected per-instance, not baked into the model: both
functions compute the frame's byte offset as
`(frameIndex * H5 + H0) * 2`, where `frameIndex` comes from
`*(int*)(actor+0x70) >> 8` for actors (an 8.8 fixed-point per-instance
animation-blend counter — distinct from the higher-level per-*pose*-object
model-swapping table at `engine+0x6b38` documented in `RENDERER_3D.md`; this
is finer-grained in-model frame interpolation, e.g. a walk cycle baked into
one model resource) and is implicitly frame 0 for rooms (static geometry
doesn't animate). This is a classic **vertex-animation** format (comparable
to Quake's MD2): the face/UV topology is shared across all frames, only
vertex positions vary frame-to-frame.

## UV table

`H3` entries, each **4 bytes**: `U` (u16) then `V` (u16), both unsigned
(read via a plain little-endian combine, not sign-extended, unlike every
other 16-bit field here — notable, and consistent between both call sites).
Likely 8.8 fixed-point normalized texture coordinates into the model's
texture (see below), though the exact fixed-point scale wasn't derived in
this pass.

## Face table

`H4` faces, each **12 bytes** — six little-endian `int16`s:

| Bytes | Field |
|-------|-------|
| `+0x0` | vertex index A |
| `+0x2` | vertex index B |
| `+0x4` | vertex index C |
| `+0x6` | UV table index for corner A |
| `+0x8` | UV table index for corner B |
| `+0xa` | UV table index for corner C |

Each face is a **triangle**: 3 vertex indices into the current frame's
vertex table, plus 3 independent UV-table indices (one per corner) — so UV
coordinates are per-face-corner, not per-vertex, allowing texture seams
(standard for a UV-mapped mesh, and confirmed by both `Actor3D_TransformAndSubmitModel`
and `RoomFace_ClipAndDispatch`'s callers reading exactly this 12-byte/6-`int16`
layout with identical field roles).

## Texture header + pixel data

Immediately after the face table, a 4-halfword (8-byte) header:

| Halfword | Field |
|----------|-------|
| `+0` | unknown/unused by either traced consumer — possibly a skin/variant count, unconfirmed |
| `+1` | texture width, in pixels — always an exact power of two, `2..256` in the values both functions' branch tables handle |
| `+2` | texture height, in pixels (inferred — see below) |
| `+3` | unknown/unaccounted gap — neither function reads this halfword |

`Actor3D_TransformAndSubmitModel` maps the width field to a "size class"
1–8 (`log2(width)`, since observed widths are exactly `2,4,8,16,32,64,128,256`)
and passes that size class into `Poly3D_ClipAndDispatch` as a parameter —
almost certainly a shift amount so the rasterizer can address texture rows
with a shift instead of a multiply (a standard ARM-without-fast-multiply
optimization, consistent with every other "avoid division/slow ops" trick
already found in this renderer).

Raw pixel data (16bpp, uncompressed — **not** the RLE/colorkeyed sprite
format from `GRAPHICS_FORMAT.md`) starts 4 halfwords after the texture
header base. Actors index into it by a **per-instance skin/variant byte**
at `actor+0xca` (letting multiple monsters share one model with different
color variants — e.g. a red vs. a green slime): pixel data offset (halfwords)
= `textureHeaderBase + 4 + skinByte * height * width`, i.e. each skin
variant is a full `width x height` raw pixel block stored back-to-back.
Rooms always read variant `0` (no per-instance selector byte is added in
`RoomGeometry_TransformAndSort`'s call — consistent with a room needing only
one texture, not multiple color variants).

## Cross-validation between the two call sites

The strongest evidence this reconstruction is correct rather than
overfit to one function: `Actor3D_TransformAndSubmitModel` and
`RoomGeometry_TransformAndSort` independently compute the **identical**
formula for the texture-header offset — `faceTableBase + faceCount*6 + 4`
(in halfwords) — from the same header field (`H4`, the face count, at byte
`0x08`), despite being otherwise structurally different functions (the actor
version dispatches per-face immediately; the room version depth-bucket-sorts
first). Two independently-written call sites agreeing on the same offset
arithmetic from the same header byte is a strong signal the header layout
above is right, not a coincidence of one function's specific control flow.

## Open follow-ups

- No on-disk resource **file** containing this format has been located yet
  — only the *consumer* code (in `6r51_code.bin`, the extracted code
  section). The actual model/texture assets presumably live in a resource
  area of `6r51.app` outside the code section, or in separate `.app`
  sub-resources; finding and parsing one would let every offset above be
  verified against real bytes instead of inferred from control flow.
- The unknown texture-header halfwords (`+0` and `+3`) — a skin/variant
  count and possibly a stride/pitch field are reasonable guesses but
  unconfirmed.
- The UV table's fixed-point scale (is `U`/`V` 8.8 like everything else, or
  a different scale relative to the texture's width/height?) wasn't derived.
- Whether `H5` really always equals `H2*3` (implied by the format but never
  directly cross-checked against real data).
