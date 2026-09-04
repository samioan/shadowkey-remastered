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
- **12, 24, 54 or 66 bytes** (always a multiple of 6) in the other entries
  — all heavily multi-frame animated models.

### RESOLVED: it's the animation clip table

**Decoded (PC-port session), against the whole real archive.** Each 6-byte
record is:

```c
struct AnimationClip {   // 6 bytes
    uint16 startFrame;   // 0x00
    uint16 endFrame;     // 0x02, exclusive
    uint16 rate;         // 0x04, playback speed -- units unconfirmed
};
```

What pins this down is that the records **exactly partition** `[0, H1)`:
each one's `startFrame` is the previous record's `endFrame`, the first
starts at 0, and the last one's `endFrame` equals the header's own frame
count `H1`. That holds for **every** multi-frame entry in the archive (33
of the 226 non-empty resources, up to 200 frames and 11 clips), which a
coincidental misreading of unrelated bytes would not do. Real examples:

```
entry  18   57 frames,  4 clips: (0,11,3) (11,22,10) (22,42,10) (42,57,10)
entry  59  157 frames,  9 clips: (0,1,10) (1,19,10) (19,35,10) ... (135,157,1)
entry  20  144 frames, 11 clips: (0,1,10) (1,22,10) (22,31,10) ... (138,144,10)
```

This is also what the scripts have been indexing all along. Every monster
script calls `SetIdleAnimation(n)`/`SetWalkAnimation(n)`/
`SetSwingAnimation(n)`/`SetDeathAnimation(n)` (plus `PlayAnimation(n)` for
the starting pose), and those numbers are clip indices: `arat.s` names
clips 0-3 and its model carries 4 records; the creatures that name clip 8
resolve to models carrying 9 or 11. That correspondence was the original
reason to look at the trailer at all.

**`rate`'s units, resolved (M52).** The consumer is `FUN_100655a8`, the
engine's clip starter: it reads exactly this record out of the model and
converts the field before storing it on the object as
`internalRate = clipRate * 384` (with a shortcut branch setting `0xf00`
directly when `clipRate == 10`, which is the same value). The animation
tick `FUN_10065438` then advances an 8.8 fixed-point *frame* cursor by
`(dt * internalRate) >> 8`, where `dt` is the engine's frame delta in 8.8
fixed-point **seconds** (`elapsedMs * 256 / 1000`, clamped to `[4, 64]`,
i.e. 15.6..250 ms). One second therefore advances the cursor by exactly
`internalRate`, and a frame is 256 cursor units:

```
framesPerSecond = internalRate / 256 = clipRate * 1.5
```

So the field is **not frames per second**: it is frames per second in
units of 1.5. The archive's overwhelmingly common value of 10 means
**15 fps**, not 10, and the range 1..29 spans 1.5..43.5 fps. The PC port
read it as fps and was therefore a third too slow on every animation; see
`AnimationClip::fps()` and `kRateToFps` in
[`port/src/world/model_archive.h`](../port/src/world/model_archive.h),
asserted by `src/tests/m52_save_fields_smoke.cpp`.

The same function is where the *caller* can scale a clip: its last
argument is an override rate, defaulting to `0xf00`, applied as
`internalRate * override / 0xf00`.

Verified by `port/src/tests/m29_animation_smoke.cpp`, which walks the real
archive and asserts the partition property for every animated entry.

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
- The unaccounted texture-header halfword (`+3`). ~~The trailer's exact
  size formula for multi-frame (`H1>1`) models.~~ — **resolved**: the
  trailer is the animation clip table, one 6-byte record per clip, so its
  size tracks clip count (not frame count, which is why `H1 * 6` never
  fitted). See "The trailer" above.
- ~~`AnimationClip::rate`'s units.~~ — **resolved (M52)**: the field is
  frames per second in units of 1.5 (the engine multiplies it by 384 into
  an 8.8-per-second cursor rate), so the common value 10 is 15 fps. See
  "The trailer" above.
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

## A model's forward axis is its local +Z

Decompiled in M35, and needed by anything that has to point a model in a
direction (a creature facing the player, a door facing its wall).

`BuildRotationMatrix3x4` (`FUN_10073a70`) builds the actor transform from
three Euler angles. Zero the other two and keep only the third and it
reduces to

```
row0 = [ cos, 0, sin ]      -> out0 = screen right
row1 = [   0, 1,   0 ]      -> out1 = screen up
row2 = [-sin, 0, cos ]      -> out2 = camera depth
```

and `Actor3D_TransformAndSubmitModel`'s vertex loop (`FUN_10056eb0`,
around `0x10057400`) applies those rows to the vertex's three `int16`s in
**file order** — `out_i = row_i . (v1, v2, v3) + t_i`. So the heading angle
rotates file components **1 and 3** about component **2**:

- component 2 is **up** (untouched by heading),
- component 1 is the model's **right**,
- component 3 is the model's **forward** — at heading zero it maps
  straight to camera depth, i.e. the model faces directly away from a
  camera looking the same way.

The shipped geometry agrees independently. Measuring frame 0's bounding
box per axis:

| model | dX | dY | dZ | reading |
|---|---|---|---|---|
| Azra_Rat | 311 | 422 | **1182** | quadruped body runs along Z |
| Alpha_Wolf | 238 | 481 | **893** | same |
| Bandit_Thug | 218 | 668 | **120** | humanoid: shoulders across X, *depth* in Z |
| Skelos_Undriel | 217 | 668 | **123** | same |
| door.s | 519 | 1036 | **30** | a slab whose thin axis — its normal, i.e. its facing — is Z |

Every case puts the facing axis on Z, and every case puts height on Y.

**Porting note:** a renderer that rotates local +X to the heading instead
draws every model a quarter turn off. Creatures still turn to track a
target, but present their flank while doing it, which is easy to misread
as "they don't turn at all".
