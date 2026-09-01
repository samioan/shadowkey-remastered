# The 3D model resource format

`RENDERER_3D.md` established that actors and rooms share one generic 3D
model format, read from `actor+0x54` / `room+0x54`. This thread first
reconstructed that format's layout by reading every byte-level field access
in the two functions that consume it — `Actor3D_TransformAndSubmitModel`
(0x10056eb0) and `RoomGeometry_TransformAndSort` (0x10057890) — then found
the actual on-disk resource archive and **verified the reconstruction
byte-for-byte against 226 real model entries**.

## The resource archive: `models.idx` + `models.huge`

Found in the game install tree, not inside `6r51.app` itself:
`system/apps/6r51/models.idx` (1900 bytes) and `system/apps/6r51/models.huge`
(4,907,880 bytes). This is the actual on-disk container for every 3D model
in the game.

`models.idx` is a flat index: a little-endian `u32` count (`237` in the
retail build), followed by that many `(startOffset: u32, size: u32)` pairs
slicing directly into `models.huge`. Verified exhaustive and gap-free: every
entry's `startOffset` equals the previous entry's `startOffset+size`, and
the final entry's end (`4,907,880`) equals `models.huge`'s exact file size.
Entry `236` (the last) is a zero-size sentinel. `azra.sta` (a small file
only present for the `azra` zone, `system/apps/6r51/azra.sta`) is a
*different* format — short repeated ~32-byte records, not a 3D model and
(per `ZONE_FORMAT.md`) not entity placement either, since that's `.ent`'s
job — still unidentified, noted but not pursued here.

Each slice of `models.huge` selected by an index entry is one complete model
resource, in exactly the format inferred from the code — **confirmed** by
decoding all 226 non-empty entries and checking every derived offset and
index against real bytes (script: see below).

## Header (14 bytes, 7 halfwords)

| Offset | Field | Meaning |
|--------|-------|---------|
| `0x00` | `H0` (i16) | Vertex table base, in **halfwords** (2-byte units) from the resource start |
| `0x02` | `H1` (i16) | Animation frame count |
| `0x04` | `H2` (i16) | Vertex count per frame |
| `0x06` | `H3` (i16) | UV-coordinate table entry count |
| `0x08` | `H4` (i16) | Face (triangle) count |
| `0x0a` | `H5` (i16) | Halfwords per animation frame's vertex block |
| `0x0c` | `H6` (i16) | Constant `1` in every one of the 226 entries checked — a format/version tag, not a count (see "Skin count", below, for the field this is *not*) |

`H5 == H2*3` in **every single entry checked** (3 halfwords/vertex × vertex
count) — this was a prediction from the code, now a confirmed invariant, not
just a guess.

Everything past the 14-byte header is a sequence of tables, each immediately
following the previous one with **zero padding**:

```
vertex table    (H1 frames x H2 vertices x 6 bytes)   starts at halfword 7 (byte 14)
UV table        (H3 entries x 4 bytes)                starts at halfword H0 + H1*H5
face table      (H4 faces x 12 bytes)                  right after the UV table
texture header  (4 halfwords)                          right after the face table
texture pixels  (skinCount x width*height x 2 bytes)   4 halfwords after the texture header
[trailer]                                              see "The trailer", below
```

Note `H0` is always `7` in every checked entry — i.e. the vertex table
always starts immediately after the 7-halfword header, with `H0` apparently
present as an explicit (if redundant, given it's a format constant)
self-describing offset rather than a hardcoded `7` in the reader.

## Vertex table

`H1` consecutive frames, each `H2` vertices, each vertex **6 bytes**: three
little-endian `int16`s, `(x, y, z)` in the model's local space. Every
vertex-index reference in every checked face table lands in
`[0, H2)` — confirmed exhaustively (max referenced index == `H2-1` for all
226 entries).

The active frame is selected per-instance, not baked into the model: both
consumer functions compute the frame's byte offset as
`(frameIndex * H5 + H0) * 2`, where `frameIndex` comes from
`*(int*)(actor+0x70) >> 8` for actors (an 8.8 fixed-point per-instance
animation-blend counter — distinct from the higher-level per-*pose*-object
model-swapping table at `engine+0x6b38` documented in `RENDERER_3D.md`) and
is implicitly frame 0 for rooms. This is a classic **vertex-animation**
format (comparable to Quake's MD2): face/UV topology is shared across all
frames, only vertex positions vary. `H1` ranges from `1` (static
props/furniture/room geometry) up to `157` in the retail data — clearly full
multi-frame animation cycles for creature/NPC models.

## UV table

`H3` entries, each **4 bytes**: `U` (u16) then `V` (u16), unsigned. Every
UV-index reference in every checked face table lands in `[0, H3)` —
confirmed exhaustively, same as the vertex indices above.

The fixed-point scale is **not fully pinned down**: `U` values top out
around `240–248` per texel-width-unit (e.g. a 32px-wide texture's `U` values
reach up to ~7936, close to but under `32*256`), consistent with an 8.8
fixed-point pixel coordinate — but some entries' `V` values *exceed*
`height*256` for that entry's texture height, so either `V` isn't scaled by
height the same simple way, textures wrap/tile vertically, or there's a
detail this pass didn't resolve. Left as an open follow-up.

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

Each face is a **textured triangle**: 3 vertex indices into the current
frame's vertex table, plus 3 independent UV-table indices (one per corner),
so UV coordinates are per-face-corner, not per-vertex (allows texture
seams). All 226 entries' face tables were decoded in full and every vertex-
and UV-index in every face landed in bounds.

## Texture header + pixel data

Immediately after the face table, a 4-halfword (8-byte) header:

| Halfword | Field |
|----------|-------|
| `+0` | **skin/color-variant count** — confirmed: values `1` through `7` seen in the retail data (a plain monster has 1; some creature models have up to 7 distinct recolors sharing one mesh) |
| `+1` | texture width, in pixels — always an exact power of two, and always **equal to the height** (every one of 226 entries has a square texture: `16x16`, `32x32`, or `64x64` observed) |
| `+2` | texture height, in pixels |
| `+3` | unaccounted for — neither traced consumer reads it; real values here vary per-model with no obvious pattern (not a constant, not obviously another count) |

`Actor3D_TransformAndSubmitModel` maps the width field to a "size class"
1–8 (`log2(width)`) and passes that into `Poly3D_ClipAndDispatch` as a
parameter — almost certainly a shift amount so the rasterizer can address
texture rows with a shift instead of a multiply.

Raw pixel data (16bpp, uncompressed — **not** the RLE/colorkeyed sprite
format from `GRAPHICS_FORMAT.md`) starts 4 halfwords after the texture
header base, exactly matching the traced code's `pbVar7 + (skinIndex*height*width + textureHeaderBase + 4) * 2`
formula — **confirmed**: for skin index 0, `textureHeaderBase+4` (halfwords)
lands exactly on the start of a `width*height*2`-byte block, verified across
226 entries. Actors index into it by a **per-instance skin/variant byte**
at `actor+0xca`; rooms always read variant `0` (no per-instance selector is
added in `RoomGeometry_TransformAndSort`'s call).

## The trailer

After `skinCount` back-to-back `width*height*2`-byte pixel blocks, the
resource has a small trailer before the next entry begins:

- **6 bytes** in 209 of 226 entries (92.5%) — all single-animation-frame
  (`H1==1`) models: static props, furniture, room geometry.
- **12, 24, or 54 bytes** (always a multiple of 6) in the other 17 entries
  — all heavily multi-frame animated models (`H1` from 57 up to 157).

The trailer's purpose (and why it scales with frame count for animated
models but isn't simply `H1 * 6` or `skinCount * 6`) is unresolved — flagged
as an open follow-up. It's clearly real structure, not slack/padding: the
`models.idx` size fields account for it exactly, with zero slop, in every
one of the 226 entries checked.

## What this confirms from the earlier (code-only) pass

Everything inferred purely from `Actor3D_TransformAndSubmitModel`'s and
`RoomGeometry_TransformAndSort`'s decompiled control flow held up against
real data: header field roles and offsets, `H5==H2*3`, the UV/face table
strides, every index bound, the texture header's width/height fields and
their power-of-two/square shape, and the exact texture-pixel-data offset
formula. The two functions computing the identical texture-offset formula
from the same header byte (`RENDERER_3D.md`'s original cross-validation
argument) is now doubly confirmed by real bytes agreeing with both.

## Open follow-ups

- The UV table's exact fixed-point scale (some `V` values exceed a simple
  `height*256` bound).
- The unaccounted texture-header halfword (`+3`) and the trailer's exact
  size formula for multi-frame (`H1>1`) models.
- `azra.sta`'s format is still unresolved — but the "per-zone entity/actor
  placement" guess below was **wrong**: that role is `.ent` (see
  `ZONE_FORMAT.md`), and `.sta` isn't even one-per-zone (only `azra.sta`
  exists, 21 zones exist) — its short repeated ~32-byte records are
  something `azra`-specific instead (starting zone? save data?), not
  pursued further.
- ~~Whether the `.zon`/`.zmp`/`.pal`/etc. per-level files reference
  `models.huge` entries by the same index used here, and how a room
  object's `+0x54` model pointer gets populated from one of those files on
  zone load~~ — **resolved**, see
  [`ZONE_FORMAT.md`](ZONE_FORMAT.md#the-model-index-cache-engine0x6b38--engine0x6f38):
  a per-zone `<zone>_models.txt` text file lists which `models.idx` archive
  indices that zone uses, loaded into a flat 256-slot `engine+0x6b38` cache
  keyed directly by archive index; placed objects from `<zone>.ent` get
  their `+0x54` model pointer as a cache read from that array, keyed by an
  index carried on a per-entity-type descriptor (`engine+0xbe34`, a BST
  keyed by type ID).
