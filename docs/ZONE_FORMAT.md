# Zone loading and the model-index resolution chain

This documents the per-zone level-loading pipeline in `6r51.app`, traced from
the single monolithic loader function `FUN_10024dec` (renamed
`GameEngine_InitLevel`, 0x10024dec — found via debug strings it references:
`"InitLevel Pre load_models"`, `"InitLevel Post load_models"`,
`"InitLevel Post Zones"`, `"InitLevel Pre Fog"`, `"InitLevel Pre Entities"`).
It answers the open question from `MODEL_FORMAT.md`/`RENDERER_3D.md`: **how
does a room/entity object's `+0x54` model pointer actually get populated on
zone load**, and confirms `engine+0x6b38` (previously described in
`RENDERER_3D.md` as indexed by a mysterious "frame index") is in fact
indexed by the model's **global `models.idx` archive index**.

Each zone (`azra`, `broken1`, `crypt1`, … — 21 total, see
[`MODEL_FORMAT.md`](MODEL_FORMAT.md)) is a matched set of files under
`system/apps/6r51/`, all opened by `sprintf`-ing `"z:\system\apps\6R51\" +
"<zone>" + "<ext>"` (with a same-zone `'e'`-prefix retry on open failure,
seen in `ModelArchive_Open`/`FUN_10068948` — likely a per-locale variant
fallback). `GameEngine_InitLevel` loads them in this order:

1. **`<zone>_models.txt`** (ASCII text, one line per model slot) — the list
   of which of the 237 `models.idx` entries this zone actually uses. Loaded
   by `ZoneModelList_Load` (`FUN_10060ac0`, 0x10060ac0).
2. **`<zone>.sur`** — small `u8` count + count×8-byte records. **Fully
   decoded** (see "The tile-grid wall/surface-face renderer" below,
   `RENDERER_3D.md`): each record describes one selectable wall-face
   "surface" — UV scale (as bit-shifts), UV origin offset, a flags byte
   (flip U/flip V/disable), and a clamped texture-slot index into `.ztx`.
   **Byte offsets confirmed** (PC-port session, decompiling
   `SurfaceFace_BuildAndProject`, 0x1005d784):
   `[0]`=U bit-shift, `[1]`=V bit-shift, `[2..3]`=signed-16 U offset,
   `[4..5]`=signed-16 V offset, `[6]`=flags (`bit0`=flip V, `bit1`=flip U,
   `bit5`=**disable this face entirely** — `BuildAndProject`'s very first
   check, no vertices built, no draw call at all when set), `[7]`=texture
   index (clamped into `[0, 0xfe]`, or `0xff` if the raw value overflows
   that).

   **The UV formula these drive, and its fixed-point scale — resolved**
   (PC-port session; previously "decoded but not yet ported", with the port
   substituting a flat 0..1 UV per quad). `BuildAndProject`'s tail computes
   each vertex's UV straight from its **world position**:

   ```c
   switch (orientCode) {                       // param_5
     case 0: u = (worldY + uOffset) << uShift; break;
     case 1: u = (uOffset - worldY) << uShift; break;
     case 2: u = (worldX + uOffset) << uShift; break;
     case 3: u = (uOffset - worldX) << uShift; break;
     case 4: case 5: u = (worldX + uOffset) << uShift; vAxis = worldY; break;
   }
   v = (vAxis + vOffset) << vShift;            // vAxis = worldZ for cases 0-3
   if (flags & 2) u = -u;                      // flip U
   if (flags & 1) v = -v;                      // flip V
   ```

   `Render3DScene` picks `orientCode` per direction: the `±Y` blocks pass a
   literal `2`/`3`, floor passes `5` and ceiling `4` (the two fall through
   to identical code), and the `±X` blocks choose between `0` and `1` using
   the *main-band surface's own flip-U bit* (`+X`: `flipU ? 0 : 1`; `-X`:
   `flipU ? 1 : 0`) — so for those two directions the flip bit ends up
   changing the sign of the U *offset* rather than of the U axis.

   The fixed-point scale is pinned by the consumer:
   `SurfaceFace_RasterizeTextured_v3` (0x1005bbc8) fetches
   `texture[(mask & (u >> 8)) + ((mask & (v >> 8)) << widthShift)]`, with
   `SurfaceFace_ClipAndDispatch` passing `widthShift = 7` (128-wide) and
   `mask` = `0x7f`/`0x7e`/`0x7c`/`0x78` chosen by the triangle's average
   view depth against `0x400`/`0x800`/`0xc00` raw units — a real
   distance-driven detail reduction, not just a wrap. So **one texel =
   2^(8 − shift) raw world units**: the dominant real shift of 6 gives 64
   texels per 256-unit tile (one 128×128 texture per 2×2 tiles), 7 gives one
   texture per tile, and 4 (real mural surfaces, e.g. `azra.sur` records
   14/15) stretches one texture across 8 tiles. Note the addressing is a
   **wrap** (`&`), so textures genuinely tile across a face.
3. **`<zone>.zon`** — room definitions: `u16` count → `engine+0x5460`, then
   count × 0x48-byte (72-byte) room records into `engine+0x5464`, stride
   0x84 (132 bytes) per room slot. **Record layout now fully decoded** (see
   below): four `u16` header fields at `+0x00/+0x02/+0x04/+0x06` followed by
   a 64-byte name at `+0x08`.
4. **`<zone>.pth`** — AI monster spawn + patrol-path data: `u16` count, then
   per-monster a 0x44-byte (68-byte) header record (creates an object via
   `FUN_1001ae8c`) followed by a variable-length list of 8-byte waypoint
   entries (count read from the header, consumed via `FUN_1001ad2c`).
5. **`<zone>.ent`** — static/dynamic **entity placement**: `u32` count, then
   count × 0x48-byte (72-byte) records. **This is where a placed object's
   model gets assigned** (see below).
6. **`<zone>.stn`** — loaded *conditionally* (only when `GameEngine_InitLevel`'s
   third parameter is set — likely "reloading a zone visited earlier this
   session" rather than a fresh first visit) during the "Init scripts" /
   "Done init scripts" phase. Fully decoded and verified against real data
   — see its own section below: it's a small table binding specific named
   zone objects (mostly locked doors/containers) into a global SimKin
   `resistDisarm[]` array, i.e. **per-instance lockpick/trap difficulty**.

`GameEngine_InitLevel` also loads six more per-zone files interleaved with
the above (`.ztx`, `.zmp`, `.zlu`, `.zfg`, `.zcp`, `.zsk`) through a second,
**zlib-compressed** loader — see "Compressed per-zone files" below, which
covers the single most important one for rendering: `.zsk`, the zone's
actual walkable dungeon-geometry model.

## The model-index cache: `engine+0x6b38` / `engine+0x6f38`

`ZoneModelList_Load` (`FUN_10060ac0`) opens `<zone>_models.txt`, and for
each line — parsed with `sscanf(line, "%d %d %d %d %s", &archiveIndex,
&flag1, &flag2, &flag3, name)` — unless `name == "NULL.bin"` (the empty-slot
sentinel):

- calls `ModelArchive_LoadByIndex(archiveObj, archiveIndex)`
  (`FUN_10068b68`, 0x10068b68) and stores the returned pointer at
  `engine+0x6b38[archiveIndex]` (a flat **256-slot** cache, `TAny*[0x100]`,
  large enough for every one of the 237 real `models.idx` entries),
- stores `flag1` (1 byte), `flag2`, `flag3` (each `u16`) into a parallel
  8-byte-stride array at `engine+0x6f38[archiveIndex]`.

> **M55: those three are not flags — they are the model's collision box.**
> `Entity::Init` (`FUN_100610e4`) looks the placement's descriptor up and
> copies all three onto the entity through vtable slots `+0x4c`, `+0x50`
> and `+0x54`:
>
> | column | table field | entity field | meaning |
> |---|---|---|---|
> | 2 | `+0` (u8) | `Entity+0x8c` | **solid** — 0 or 2, never 1 |
> | 3 | `+2` (u16) | `Entity+0x8e` | **half-extent X**, 8.8 units (`SetRadius`) |
> | 4 | `+4` (u16) | `Entity+0x90` | **half-extent Y** (`SetRadius2`) |
>
> Real rows read exactly as that: `bottle 2 64 64`, `door 2 64 256`
> (thin one way, wide the other), `rail 2 64 512` (a fence run),
> `table 2 256 256`, `roof 0 1500 1500` (huge, deliberately not solid),
> `dagger 0 0 0`. Columns 3 and 4 are also written for a `NULL.bin` row —
> the name check gates only the model load, not the table write. Full
> writeup in [`WORLD_MODEL.md`](WORLD_MODEL.md) ("Entity collision").

`archiveIndex` is used **directly and unchanged** as both the zone-local
cache slot and the `models.idx` index — the two are the same number. So
`<zone>_models.txt` isn't a name→ID lookup table at all; it's simply "load
these specific global model IDs into the cache before this zone starts",
letting the engine keep only the current zone's models resident instead of
all 237 at once. `ModelArchive_LoadByIndex` is exactly the
`models.idx`/`models.huge` chain independently reverse-engineered and
verified in `MODEL_FORMAT.md`:

```c
// FUN_10068b68, param_1 = archive object (opened by ModelArchive_Open),
// param_2 = archiveIndex (0..236)
undefined4 FUN_10068b68(int param_1,int param_2) {
  if (*(char *)(param_1 + 0x1c) == '\0') return 0;   // not ready
  offset = *(int *)(*(int *)(param_1 + 0xc) + param_2 * 8);      // models.idx[i].offset
  size   = *(int *)(*(int *)(param_1 + 0xc) + param_2 * 8 + 4);  // models.idx[i].size
  Seek(param_1 + 0x14, offset);        // the open models.huge RFile
  buf = new byte[size];
  Read(param_1 + 0x14, buf, size);
  return buf;
}
```

`ModelArchive_Open` (`FUN_10068948`, 0x10068948) is the constructor:
`sprintf("%s.idx", "z:\system\apps\6R51\models")`, reads a `u32` count, then
allocates `count*32` bytes (an in-memory array with room for cache/refcount
fields per entry beyond the 8 on-disk bytes) and reads `count*8` bytes of
raw `(offset:u32, size:u32)` pairs into it — this is `models.idx` itself,
independently confirming the format already verified byte-for-byte in
`MODEL_FORMAT.md`. Then opens `models.huge` for the `Seek`+`Read` calls
above.

## Entity placement (`.ent`) → the type-descriptor tree → the model pointer

Each `.ent` record (0x48/72 bytes) is:

```c
struct EntPlacement {      // offset  size
    int32  x;               // 0x00    4    world position (8.8 fixed?)
    int32  y;                // 0x04    4
    int32  z;                // 0x08    4
    uint16 rotOrScale[4];     // 0x0c    8   NOT 4 uniform rotation-or-scale
                                //             values, and NOT all 4 consumed --
                                //             see the note below the struct:
                                //             only idx0 (-> object +0xb2) and
                                //             idx2 (-> object +0xa8) are real,
                                //             used angle data; idx1 and idx3
                                //             are both boolean-flag-shaped
                                //             (0/0xFFFF) but neither is ever
                                //             read by GameEngine_InitLevel --
                                //             genuinely dead in this build
    int32  unkA;                // 0x14    4   low u16 -> object +0xb6, the 3rd
                                 //             orientation channel (NOT
                                 //             rotOrScale[3] as an earlier pass
                                 //             of this doc wrongly assumed --
                                 //             see below). High u16 round-trips
                                 //             into azra.sta unchanged (below)
    int32  unkB;                  // 0x18    4   a packed pair, NOT a plain int32 --
                                   //             high u16 is always the constant
                                   //             0xCCCC, low u16 is a per-instance
                                   //             value (see azra.sta below: this is
                                   //             exactly .sta's separate flags/marker
                                   //             u16 pair, just still packed into one
                                   //             field here instead of split)
    int32  typeId;                  // 0x1c    4   entity/object type ID
    char   name[8];                   // 0x20    8   object/instance name
    char   script[32];                  // 0x28   32   per-placement script path
};                                             // total 0x48 = 72
```

**Correction (M35): the tail is TWO strings, not one 40-byte name.** An
earlier pass had `char name[40]` at `0x20`, which silently concatenated
the two whenever the name filled its field. The tell is in the data: real
container placements came back named `"ContaineRaiders\RT_A.s"`, which is
`"Containe"` — a `"Container"` tag truncated into an 8-byte field, so no
terminator — immediately followed by `"Raiders\RT_A.s"`. Several records
also carry stale editor bytes *after* their terminator (azra's Old Trinket
placement reads `"monsters\Old_Trinket.s\0Azra.s"`), which only happens
when a shorter string is written into a reused fixed buffer. The `\xCC`
filler padding both fields is the same uninitialised-memory marker as
`unkB`'s constant `0xCCCC` high half.

**`script[32]` is the real per-placement script path, and it overrides
`entities.txt`.** Parsed across all 21 shipped zones and checked against
the script tree:

| category | placements with a tag | tag resolves to a real `.s` |
|---|---|---|
| 2 (monsters) | 1532 | **1530** |
| 11 (doors) | 559 | 442 |
| 8 (containers) | 570 | **393** |
| 12 (trapped) | 11 | 11 |
| 7 (merchants) | 9 | 9 |
| 10 (traps) | 39 | 34 |
| 1 (scenery) | 5506 | 9 |

For scenery it is only a label (`"footlocker"`, `"rock"`), which is why a
consumer has to check the path resolves rather than trusting it. For
everything else it is the script to run, and it is routinely a *different*
script than the typeId's own: azra's `"tanyin"` placement is typeId 166,
whose `entities.txt` entry is `monsters\Tanyin_Aldwyr.s`, but which names
`monsters\Tanyin_Aldwyr_Azra.s` — the zone-specific variant — for itself.

**This is how a chest gets its contents.** Every container in the game is
an `entities.txt` `!label` with no script (`!chest_loot`, `!footlocker`,
`!barrel_loot`, ...); the loot script is named per placement, and is
typically one of the randomised `RT_A.s`/`RT_B.s` chest scripts
(`SetUsable(true)`, a `Random(1,100)` chain of
`Level.CreateEntity(id); AddObject(Item);`, and an `OnUse` that opens the
loot menu on itself). A port that resolves scripts only through
`entities.txt` therefore renders every chest in the game as inert scenery.

**Corrects an earlier version of this struct** (`typeId` at `0x14`, `pad[6]`
at `0x18`, `name` at `0x1e`) — that was wrong by 8 bytes on `typeId`/`name`
and missed two whole `int32` fields entirely. Found and fixed by directly
parsing a real `azra.ent` against `entities.txt`'s known `typeId` range
(`< 7000`) while investigating `azra.sta` below: only offset `0x1c` gives
small, sensible `typeId`-shaped values across all 282 real records (`0x14`
gives large sign-extended-looking values instead — see `azra.sta`'s
`unkA` field, which is exactly this). Tool:
`tools/parse_zone_placement.py`.

**`rotOrScale[4]` is two real angles plus two dead flag-shaped fields,
not four uniform values.** Ran `parse_zone_placement.py` against a real
`azra.ent` (282 records) and checked each of the 4 indices' value
distribution: `idx0` (17 distinct values) and `idx2` (24 distinct
values) both span wide ranges in multiples of 128 (e.g. 256, 384, 768,
1152, and near-65536 wraparound values like 63872/64640) — the
signature of real angle/heading data, consistent with the compass's
confirmed `player+0xb6` heading format (`GRAPHICS_FORMAT.md`). `idx1`
and `idx3`, by contrast, each take only **2 distinct values (0 or
0xFFFF)** — flag-shaped, not angle-shaped.

**Corrects the previous pass's destination guess.** Directly tracing
`GameEngine_InitLevel`'s raw ARM disassembly (both the player-start and
generic-entity branches, which use an identical offset pattern —
verified structurally against each other) instead of inferring offsets
from the decompiler's local-variable naming order gives the *real*
per-field mapping:

| record offset | field | destination |
|---|---|---|
| `0x0c` | `rotOrScale[0]` | object `+0xb2` (orientation channel) |
| `0x0e` | `rotOrScale[1]` | **never read — dead** |
| `0x10` | `rotOrScale[2]` | object `+0xa8` (orientation channel) |
| `0x12` | `rotOrScale[3]` | **never read — dead** |
| `0x14` | `unkA` (low u16) | object `+0xb6` (orientation channel) |
| `0x18` | `unkB` (low u16) | object `+0x5e`, the model **scale** (see the M52 note below) |

> **Later correction (M52).** The destination `+0x5e` is not
> "`modelFlags`" — it is the model's **scale**, in the engine's 8.8 fixed
> point. Its script binding is `SetScale`, and the shipped scripts call it
> with 192, 256, 352 and 512, where 256 is 1.0; `SetActive`'s reset path
> and the Drawable save record treat it as one halfword beside the
> orientation channels. So `unkB`'s low halfword is a per-placement size
> multiplier, which is also why `rotOrScale`'s name was half right: two
> rotations, and the scale lives one field along. See
> `SAVE_FORMAT.md`'s "What the scalars are".

> **How the three channels are actually consumed (M71).**
> `Actor3D_TransformAndSubmitModel` passes them to
> `BuildRotationMatrix3x4` as `(actor[0xa8], actor[0xb2], actor[0xb6] +
> 0x8000)` — note the **half turn** added to the heading, and note that the
> heading is the *third* matrix argument, not the first. Counted across all
> 21 shipped zones: 203 placements carry a non-zero `+0xa8` and 232 a
> non-zero `+0xb2`, out of 8,216 — so a consumer that implements only the
> heading is right for ~97% of the world and visibly wrong for the rest.
> `RENDERER_3D.md`'s "The actor placement transform, in full" has the whole
> call site, including the `-0x40` vertical draw offset that goes with it.

So only 2 of the 4 `rotOrScale` fields are used at all — `idx0`/`idx2`,
both real angle data, feeding 2 of the object's 3 orientation channels.
The 3rd orientation channel (`+0xb6`) is fed by `unkA`'s low 16 bits, a
field entirely outside `rotOrScale`, not by `rotOrScale[3]` as an
earlier pass of this doc claimed (that pass had inferred the mapping
from the decompiler's `local_75c`-style variable names rather than the
verified raw offsets, and got both `modelFlags`' source and the
orientation-channel order wrong — see the code excerpt below, now
fixed). `idx1` and `idx3` are genuinely dead: grepping every write to
object offsets `+0xa8/+0xb2/+0xb6/+0x5e` across the whole function finds
no other occurrence besides the ones tabulated above. Whether some
function *outside* `GameEngine_InitLevel` reads either dead field wasn't
checked.

For each record:

- **`typeId == 1`**: special-cased as the **player start position** — writes
  `x`/`y`/`z` and the rotation fields straight into the player object at
  `engine+0x618` (a `CBase`-derived object, offsets `+0x94/+0x9c/+0xa4` for
  position, `+0xa8/+0xb2/+0xb6` for orientation) instead of creating a new
  entity, but **only if `GameEngine_InitLevel`'s `param_3` is non-zero**
  (`if (param_3 != 0) { ...write position... }`, otherwise this whole block
  is skipped and the player keeps whatever position they already had).

  > **The record is not the only source (M61).** That whole branch is the
  > `else` half of a test on `engine+0x14a20`. When a script has armed
  > `GetPlayer().SetCameraStart(...)` (Player binding 0x39, 49 shipped call
  > sites — see `SIMKIN_NATIVE_API.md`), the six values at
  > `engine+0x14a24..+0x14a38` are written onto the player *instead of* the
  > record's, and the engine's own struct is laid out in this record's
  > field order — `x, y, z, roll, pitch, yaw` at `+0x14a24`, `+0x14a28`,
  > `+0x14a2c`, `+0x14a30`, `+0x14a34`, `+0x14a38` — which is an
  > independent confirmation of the destination table above, arrived at
  > from a completely different function.
  >
  > Two asymmetries between the two branches are real and worth recording:
  >
  > * the record branch computes the eye height,
  >   `player+0x224 = player+0xa4 + CMap+0x1a`, and **the override branch
  >   does not**. A scripted arrival keeps whatever eye height the previous
  >   level left behind. (If `CMap+0x1a` really is 0 — the standing
  >   hypothesis, see `RENDERER_3D.md` — the two agree by accident and the
  >   omission is invisible.)
  > * the record branch is gated on `param_3`; **the override branch is
  >   not**. A scripted spawn places the player even in the mode where an
  >   ordinary entry would leave them alone.
  >
  > The flag is cleared later in the same function (twice, on the two paths
  > out of the entity pass) and by `Level.RestoreSaveLevel()`, so an
  > override applies to exactly one arrival.
- **`typeId > 1`**: looks up a **type descriptor** for `typeId` in a binary
  search tree rooted at `engine+0xbe34` via `EntityTypeDescriptor_Lookup`
  (`FUN_1008c1cc`, 0x1008c1cc — a plain BST: node = `{..,key@+8,
  data@+0xc,left@+0x10,right@+0x14}`, returns `node->data` for the first key
  `<= typeId`... actually exact match only, see code), then:
  1. Calls a factory virtual method to allocate the actual game-object
     instance for that type.
  2. Sets its position (`vtable+0x18`) from `x/y/z`, and orientation fields
     `+0xa8/+0xb2/+0xb6` from the record's u16 fields (`rotOrScale[2]`,
     `rotOrScale[0]`, and `unkA`'s low 16 bits respectively — see the
     corrected table above; `rotOrScale[1]`/`[3]` are dead).
  3. Copies the `name` field to two string slots (`+0xcb`, `+0xe2`).
  4. Calls an `Init(engine)` virtual method (`vtable+0x10`).
  5. **Looks up the type descriptor again** and does:
     ```c
     object->modelPtr /* +0x54 */ =
         engine->modelCache /* +0x6b38 */ [ typeDescriptor->modelArchiveIndex /* +0xc */ ];
     object->modelFlags /* +0x5e (u16) */ = record.unkB /* low u16 */;
     ```
     **Corrected**: `modelFlags` is `unkB`'s low 16 bits, not
     `rotOrScale[3]` as an earlier pass of this doc claimed (traced from
     the decompiler's `local_75c`-style variable naming rather than
     verified raw offsets — the raw-disassembly retrace above found the
     real source). This is the line that finally resolves the open question: the type
     descriptor found via the `engine+0xbe34` BST carries, at its own
     `+0xc`, the `models.idx` archive index for that entity type, and the
     object's model pointer is just a cache read from the already-populated
     `engine+0x6b38[archiveIndex]` slot — nothing is loaded from disk at
     entity-creation time, only at zone-init time via `_models.txt`.

This also **corrects/refines** a `RENDERER_3D.md` note: the animated-actor
model lookup there (`engine+0x6b38 + frameIndex*4`, indexed by `actor+0x2c2`,
described as a "16-bit current frame field") is the *same* `engine+0x6b38`
cache — `actor+0x2c2` is not a literal animation-frame counter, it's a
**model archive index**. The per-model *animation* frame (for the MD2-style
vertex table within one model resource) is the unrelated `actor+0x70` field
documented in `MODEL_FORMAT.md`.

> **Later correction (M46).** The second half of that sentence was wrong
> and has been withdrawn: `actor+0x2c2` is *not* the entity's own model
> index and is *not* set at placement time from the type descriptor. It is
> the **attached weapon** index, written only by the Monster binding
> `SetAttachedWeapon(n)` and defaulting to `-1`, and `FUN_10083490` draws
> it as a second model on top of the body. The chain traced above — type
> descriptor `+0xc` → `engine+0x6b38[archiveIndex]` → the object's model
> pointer at `actor+0x54` — is unaffected and still correct; only the
> field it was attributed to was. See `WORLD_MODEL.md`'s "The attached
> weapon".

## Where the type-descriptor tree itself comes from: `entities.txt`

`engine+0xbe34`'s BST isn't per-zone data — it's built **once, at game
startup**, from a single global file: `z:\system\apps\6R51\entities.txt`.
Loaded by `EntityTypeConfig_Load` (`FUN_10068854`, 0x10068854), whose
**only caller is `GameEngine_FirstTickBootstrap`** (the one-time engine
init function documented in `RENDER_LOOP.md`) — confirming it's global,
not reloaded on zone transitions.

`EntityTypeConfig_Load` reads the whole file into memory, splits it on
`'\n'`, and for each non-empty line parses `sscanf(line, "%d %d %d %254s",
&typeId, &modelArchiveIndex, &thirdField, name)`. For every `typeId < 7000`
it allocates a 148-byte (`0x94`) descriptor:

```c
struct EntityTypeDescriptor {       // offset  size
    // +0..+7: unused/vtable-ish (not set here)
    int32 typeId;                    // 0x08    4   == the sscanf'd key
    int32 modelArchiveIndex;          // 0x0c    4   == models.idx index (see above)
    int32 category;                     // 0x10    4   entity class enum (1=prop,2=monster,
                                         //             3=misc loot,4=weapon,5=spell,6=armor,
                                         //             7=merchant,8=container,9=consumable,
                                         //             10=trap,11=door,12=trapped,14=scroll,
                                         //             15=shield,16=unique) -- see below
    char  name[128];                    // 0x14  128  (strncpy'd, max 0x7f bytes + NUL)
};                                             // struct itself is 0x94 = 148 bytes
```

...then calls `EntityTypeDescriptor_Insert` (`FUN_1008c22c`, 0x1008c22c) to
insert it into the `engine+0xbe34` BST, keyed by `typeId`. That insert
function's node layout (`EUSER____builtin_new(0x18)`, node = `{tag@+4,
key@+8, data@+0xc, left@+0x10, right@+0x14}`, ordered insert comparing
`node->key`) is the exact BST `EntityTypeDescriptor_Lookup` walks — this is
the definitive confirmation, not just a plausible match.

So the full chain, start to finish, is: `entities.txt` (global, ~7000
possible type IDs, loaded once at boot) → `engine+0xbe34` BST → per-zone
`<zone>.ent` records reference a `typeId` → `EntityTypeDescriptor_Lookup`
→ descriptor's `modelArchiveIndex` → `engine+0x6b38[modelArchiveIndex]`
(populated per-zone from `<zone>_models.txt`, itself backed by
`models.idx`/`models.huge`) → the object's `+0x54` model pointer.

### `thirdField` resolved: it's an entity category enum

Traced all 17 callers of `EntityTypeDescriptor_Lookup` and grepped for
reads of the returned descriptor's `+0x10` field, then cross-checked the
handful of hits **directly against the real `entities.txt` game data**
(readable text, not just code inference) by tallying every line's third
column and sampling the names in each bucket:

| value | count | sample names |
|-------|-------|---------------|
| 1  | 130 | generic scenery/props (`!bottle`, `!barrel`, `!pinetree`, `!lantern`) |
| 2  | 176 | monsters (`monsters\Azra.s`, `monsters\Bandit_Thug.s`, ...) |
| 3  | 52  | misc loot/quest objects (`gold.s`, `bag.s`, `!shadowkey`) |
| 4  | 83  | weapons (`dagger.s`, `weapons\iron_broadsword.s`, ...) |
| 5  | 31  | spells, cast directly (`blaze.s`, `HealWound.s`, `spells\Weakness.s`) |
| 6  | 89  | armor pieces (`armor\iron_cuirass.s`, ...) |
| 7  | 9   | merchant NPCs (`monsters\Eranthos_Merchant.s`, ...) |
| **8**  | 22  | **containers** (`!footlocker`, `!crystal`, `!bag_loot`, `!chest_loot`, `!crate_loot`, `!sack_loot`, `!jar_loot`, `!barrel_loot`) |
| 9  | 58  | consumables (`items\bread.s`, `items\healing_potion.s`, ...) |
| 10 | 3   | mechanical traps (`ceilingmasher.s`, `steamtrap.s`, `spiketrap.s`) |
| 11 | 60  | doors (`door.s`, `!gatedoor`, `!ironcage`, `!twilite_3door`, ...) |
| 12 | 4   | trapped variants of 8/11 (`!trapped_door`, `!trapped_chest`, `!crate_lootTrapped`) |
| 14 | 7   | spell **scrolls** (`spells\IgniteScroll.s`, `spells\U_Blaze_lvl10.s`, ...) |
| 15 | 10  | shields (`armor\Iron_Shield.s`, ...) |
| 16 | 1   | one unique weapon (`weapons\DaedricSword.s`) |

This lines up exactly with how the code uses it:
- `FUN_1003f130` (item/inventory search, `FUN_10035a48`) matches a wanted
  category `iVar9` against `descriptor+0x10`, **with an explicit special
  case: `iVar9==5` (spell) also matches `descriptor+0x10==0xe` (14,
  scroll)** — i.e. spell scrolls count as castable spells for inventory
  lookups. Exactly what the category table above would predict.
- `FUN_1002c3a8` and `FUN_10084438` both spawn a container and both check
  `descriptor+0x10 == 8` before flagging the result as one
  (`+0x180`/`+0x181` = 1) — i.e. the code re-derives "is this really a
  container?" from the category field rather than assuming it. **M81
  corrects which is which.** The first pass called `FUN_1002c3a8` "a
  monster's on-death handler"; it is not.

  - **`FUN_1002c3a8` is `DropObject(item)`** — the *inventory* drop. It
    spawns a fixed `typeId 300` (`entities.txt` line 212, `300 30 8
    !bag_loot`) at `item->owner`'s position minus half a tile on each
    axis, then `FUN_10028f98` appends the item to the new bag's contents
    list (`bag+0x164`) and backlinks it (`item+0x88 = bag`). Its callers
    are the player dispatcher's drop-gold case (spawn typeId 0x34,
    `SetQuantity`, drop) and the item dispatcher's drop case — never a
    death.
  - **`FUN_10084438` is the on-death loot drop**, called from the real
    creature death handler `FUN_10083c04` and gated on `monster+0x2f0 !=
    0`. `+0x2f0`/`+0x2f4` are the typeId and script-name pair the
    creature's own `SetLoot(300, "Loot_ratseye", 1, 8)` left on it
    (dispatcher `0x10084924` case `0xd`), and the spawn passes the name to
    `FUN_100715a8` as a **script override** for the typeId — which is what
    makes one container type carry 54 different bags. It positions at the
    creature's own x/y, z + 300, floor-snaps (`FUN_100686e0`) and lifts by
    `0x80`.

  Both bags are drawn: `FUN_100715a8` sets the spawned object's `+0x54`
  model pointer from `engine+0x6b38[descriptor->modelArchiveIndex]`
  whichever way the script was chosen, and typeId 300's model index 30 is
  `bag_dropped.bin` in every playable zone's `<zone>_models.txt`. The `!`
  in `!bag_loot` marks the *name* column as a label rather than a script
  path; it says nothing about the model column.

  A third path spawns the same containers from script rather than from
  the engine: `Level.CreateEntityScript(typeId, script, x, y, z)` (Level
  dispatcher case `0x21`), which `monsters/arat.s` and
  `monsters/spiderqueen.s` call from their own `OnKilled()` handlers after
  rolling their own `Random(1,12)=12`, and `crypt1/shadowkeygate.s` uses
  for three `typeId 301` (`301 15 8 !chest_loot`) chests.
- `FUN_10005320`/`FUN_1003893c`/`FUN_1003b85c` all pass `descriptor+0x10`
  straight through as an argument to a virtual call at `vtable+0x18` on
  some other object (an equip/pickup dispatcher, not traced further) —
  consistent with using category to pick equip-slot/pickup behavior
  (weapon vs. armor vs. shield vs. consumable).

So `EntityTypeDescriptor::thirdField` (renamed **`category`**) is confirmed
as a coarse entity-class enum, not a numeric stat. `13` is unused in the
real data (no gap in the enum's *meaning*, just no entities assigned it).

### The category → C++ class table, and the per-model creature height (M72)

The category is consumed one more way, and it is the most literal one:
it *is* the class selector. `GameEngine_InitLevel` hands
`descriptor+0x10` to a virtual call on the engine's entity factory
(`engine+0x28`, `vtable+0x18`), which lands on **`FUN_1002aa14`** — a bare
`switch (category)` where every arm allocates a fixed size and runs one
constructor:

| category | `operator new` size | constructor | vtable |
|---------:|--------------------:|-------------|--------|
| default | 300 (0x12c) | `FUN_10060d54` (the base entity) | `0x100ff938` |
| 1 | 0x160 | `FUN_10067998` | |
| **2** | **0x330** | **`FUN_100815e0`** | **`0x100fe2b8`** |
| 3 | 0x1cc | `FUN_1002eeb8` | |
| 4 | 600 (0x258) | `FUN_1002ce9c` | |
| 5 | 0x1d8 | `FUN_10047740` | |
| 6 | 0x1d8 | `FUN_1002e954` | |
| 7 | 0x364 | `FUN_10086754` | `0x100ffaf4` |
| 8 | 0x184 | `FUN_10029044` | |
| 9 | 0x1cc | `FUN_1002e78c` | |
| 10 | 0x26c | `FUN_1009037c` | |
| 11 | 0x160 | `FUN_1002e708` | |
| 12 | 0x194 | `FUN_1002e678` | |
| 13 | 0x334 | `FUN_10086660` | |
| 14 | 0x1d8 | `FUN_1002e5ac` | |
| 15 | 0x1d8 | `FUN_1002e844` | |
| 16 | 0x25c | `FUN_10047500` | |

(Category 13 *does* have a factory arm even though no shipped entity uses
it — so the gap really is in the data, not the enum, confirming the note
above from the other direction.)

Every creature in the game is category 2, so they all share one class and
one vtable. That matters because of what that class overrides. Entity
`Height()` (vtable slot `0x108`, the `z .. z + Height()` band the engine's
vertical hit tests use) has three implementations in the whole binary:

- `0x100a1534` — the base entity: `mov r0, #0; bx lr`, i.e. no height.
- `0x10006230` — the **player-side actor** classes (the player's own class
  `FUN_1003d670` among them): a switch on the actor's stance byte
  `+0x1e1` returning one of three *globals*, `CMap+0x18` / `+0x1a` /
  `+0x1c`. `+0x1a` is the standing one — the same field
  `GameEngine_InitLevel` adds to the player's feet Z to get its eye Z
  (`player+0x224 = player+0xa4 + CMap+0x1a`), i.e. this is a single
  global human height, not a per-entity one.
- `0x1008679c` — the **creature** class. This one is per-creature, and
  the key it switches on is the creature's own **`entities.txt` model
  index** (`EntityTypeDescriptor_Lookup(...)->modelArchiveIndex`):

```
1008679c  ldr  r3, [r0, #0x44]          ; the CMap
100867a4  ldrb r2, [r0, #0xc8]          ; this creature's typeId
100867ac  add  r0, r3, #0xbe00
100867b0  add  r0, r0, #0x34            ; CMap+0xbe34, the entity-type BST
100867bc  bl   #0x1008c1cc              ; EntityTypeDescriptor_Lookup
100867c8  ldr  r3, [r0, #0xc]           ; -> modelArchiveIndex
100867cc  sub  r3, r3, #0x12            ; 51 cases, model indices 18..68
100867d0  cmp  r3, #0x32
100867d4  ldrls pc, [pc, r3, lsl #2]
...
100868a8  mov  r0, #0x100               ; 256 -- models 18, 55, 56, 66, 68
100868b4  mov  r0, #0x200               ; 512 -- the other 46 cases + default
100868c0  mov  r0, #0x200               ; 512 -- descriptor not found
```

So there are exactly **two creature sizes in the game**, and the five
short-model entries are, read straight off `entities.txt`:

| model | height | creatures |
|------:|-------:|-----------|
| 18 | 0x100 | every rat (`Azra_Rat`, `Assault_Rat`, `Field_Rat`, `Mountain_Rat`, `Umbric_Rats`, `ratherb`) |
| 55 | 0x100 | every spider (`Cave_Spider`, `spider_savager`, `Spider_Guardian`) |
| 56 | 0x100 | the wormmouths (`wormmouth`, `Horrid_Wormmouth`, `Nubbed_Wormmouth`, `shriek_tentacles`) |
| 66 | 0x100 | the stingers/rays (`Cavern_Stinger`, `Shadowray`, `Snowray`) |
| 68 | 0x100 | every wolf (`Alpha_Wolf`, `Dusk_Wolves`, `Twilight_Wolf`, `Dire_Wolf`, `shardwolf`) |
| everything else | 0x200 | bandits (22/23), mages, skeletons, and every other humanoid |

Of the 176 category-2 rows, **26 are short and 150 are tall**. This is a
collision height, not the drawn model's extent — it is the only
per-creature size the engine carries, and it is what the automatic
aim-assist pitch gates on (docs/RENDERER_3D.md). Transcribed in the port
as `sk::MonsterCollisionHeight` (`port/src/world/entity_types.h`).

## Compressed per-zone files, and where the actual room geometry comes from

A second per-zone file loader exists alongside the streaming
open+`fread`-style one (`FUN_1009eb68`/`FUN_1009ec80`) used for `.ent`/
`.pth`/`.sur`/`.zon` above: `WholeFile_Load` (`FUN_1002778c`, 0x1002778c).
Its on-disk format, read directly from its decompiled logic: the first
**4 bytes are the decompressed size** (`u32` LE), and everything after
that is a **raw zlib stream** (`EZLIB__uncompress`, i.e. zlib's
`uncompress()` — the standard 2-byte zlib header + deflate + Adler-32
trailer, not raw deflate/gzip). **Verified against a real file**:
`azra.zsk`'s first 4 bytes decode to `132624`, and
`zlib.decompress(data[4:])` in Python produces exactly `132624` bytes.

Six per-zone extensions go through this loader (found by resolving each
call site's `sprintf` format-string operand — some of GCC's local-variable
reuse in `GameEngine_InitLevel` initially led to a wrong extension for one
of these, caught by checking the *last* assignment before each use, not
just the first):

| ext     | dest field(s)                | role |
|---------|-------------------------------|------|
| `.ztx`  | `engine+0x364` (+ `engine+0x360` = first byte); forwarded to `engine+0x6b1c`/`+0x6b20` | the wall-texture atlas — **decoded**: a flat array of `0x4000`-byte (16384-byte), **8bpp-palettized** texture slots, one per `.sur` surface index. See "The tile-grid wall/surface-face renderer", `RENDERER_3D.md` |
| `.zmp`  | `engine+0x328`                | zone metadata — **header decoded, bulk content decoded** (a `field80`×`zmpTotal` light/nav grid, see "The 'bullseye' subsystem" below) |
| `.zlu`  | `engine+0x36c/0x370/0x374/0x378` (4×512-byte chunks of one 2048-byte blob); forwarded to `engine+0x6b24..+0x6b30` | **decoded**: 4 selectable 256-color palettes (512 bytes = 256×2-byte entries) that convert `.ztx`'s indexed wall texels into real 16bpp color, selected per-face by 2 bits of a material byte. **This is what the `"InitLevel Pre/Post LUA"` debug markers actually bracket** — "LUA" is short for `.zlu`, not the Lua scripting language (see correction below). See "The tile-grid wall/surface-face renderer", `RENDERER_3D.md`. **Two corrections (port scaffold session)**: (1) a real `azra.zlu` decompresses to 131072 bytes = **64** 2048-byte blobs, not one — `Bullseye_Init`'s 4 forwarded chunk pointers are one fixed set stashed once at zone load (matching this doc's description), but the port's own renderer, needing a *per-surface* palette instead, empirically picks the blob at `(surfaceTextureIndex % 64) * 2048` and always its chunk 0; this works well against real data (see below) but isn't independently confirmed to be the original's exact per-face selection rule. (2) each 16-bit palette entry is **4-bit-per-channel** (`0x0RGB`, matching `GRAPHICS_FORMAT.md`'s framebuffer format exactly — confirmed by real `.zlu` bytes: `0xfff`/white, `0xf0f`/magenta chroma-key literal present in every chunk), not RGB565 as this doc's "256×2-byte entries" phrasing could be misread to imply — an RGB565 read of real data produces garish cyan/blue nonsense, an RGB444 read of the exact same bytes against a real `.ztx` texture produces an unmistakable, correctly-shaded wood-plank floor texture. |
| `.zfg`  | `engine+0x5c4`                | the fog/fade lookup table `CompositeSceneBufferToScreen` reads when `engine+0xbe0f` is set (`RENDERER_3D.md`'s fade-LUT open item) — "zfg" = "zone fog" |
| `.zcp`  | `engine+0x32c`                | a small indexed table of per-cell light-level deltas for the lighting bake — **decoded**, see "The 'bullseye' subsystem" below. **Correction (port scaffold session)**: its entry count is a `u32`, not the `u8` originally guessed — see the full note where `ZcpFile`/`ZcpEntry` are defined below. |
| **`.zsk`** | **`(*(engine+0x62c))+0x54`** | **the zone's skybox mesh — see below. ("zone sky", and the engine's own word: `engine+0x62c` is its Skybox object.)** |

### `.zon`'s room record, fully decoded

`GameEngine_InitLevel`'s per-room read (`FUN_1009ec80(&local_724, 0x48, 1,
handle)`, one 0x48/72-byte record per room) fills three stack locals whose
declared types pin down the exact byte layout: `uint local_724` (4 bytes),
`undefined2 local_720` (2 bytes), `ushort uStack_71e` (2 bytes) — i.e. the
record's first 8 bytes are **four consecutive `u16` fields**, followed by a
64-byte name (`strcpy`'d straight into the room slot, already known):

```c
struct ZonRoomRecord {          // offset  size
    uint16 fieldA;               // 0x00    2   -> room slot +0x30, stored as-is
    uint16 fieldB;                // 0x02    2   -> room slot +0x34, stored as (zmpTotal - fieldB)
    uint16 fieldC;                 // 0x04    2   -> room slot +0x38, stored as-is
    uint16 fieldD;                  // 0x06    2   -> room slot +0x3c, stored as (zmpTotal - fieldD)
    char   name[64];                  // 0x08   64   room name (strcpy'd)
};                                             // 0x48 = 72 bytes total
```

`zmpTotal` is a `u16` read out of the *already-loaded* `.zmp` file's header
(decompressed offset `0x82`, see the `.zmp` header layout right below) —
i.e. two of the four per-room fields aren't stored as absolute values on
disk, they're stored as "distance from a shared total", and get converted
back to absolute indices at load time using a count that lives in a
*different* file. That's a strong signal `fieldB`/`fieldD` index into some
zone-wide array sized by `zmpTotal` (portal/neighbor list? texture-atlas
range? light index?) allocated from the end, while `fieldA`/`fieldC` are
plain forward indices/counts into something else — consistent with the
struct but the arrays being indexed aren't identified yet.

### `.zmp`'s header, partially decoded

`.zmp` (compressed, loaded right after `.ztx`, bracketed by debug markers
`"InitLevel Post Textures"`/`"InitLevel Pre Zones"`) turned out not to be
fully opaque — its first bytes are read into 5 chunks immediately after
load:

```c
struct ZmpHeader {              // decompressed offset  size
    char   scriptName[32];       // 0x00                  32   see below
    uint8  unknown1[32];          // 0x20                  32   not decoded
    uint8  unknown2[64];           // 0x40                  64   not decoded
    uint16 field80;                 // 0x80                   2  passed to Bullseye_InitMap
    uint16 zmpTotal;                 // 0x82                   2  passed to Bullseye_InitMap
                                      //                          AND used by .zon's fieldB/fieldD above
};                                                // 0x84 = 132 bytes header (rest of file unexamined)
```

**`scriptName` is the big find**: those first 32 bytes get passed straight
into an `sprintf("%s\%s.s", zonePath, scriptName)` call whose result is
handed to `SIMKIN_ord59` (a SimKin engine ordinal — "load/run script" by
context) a few lines later. So **`.zmp`'s header names the zone's SimKin
level script** (its `.s` file, part of the SimKin scripting layer already
known from `.s` files in the install tree) — the very first concrete link
found between the compiled binary's zone-loading code and which specific
script file gets run for a given zone, beyond just "scripts exist".
`field80`/`zmpTotal` are consumed by `Bullseye_Init`/`Bullseye_InitMap`
(the AI-navigation-looking subsystem, see below) as well as by `.zon`'s
record conversion above — so whatever `zmpTotal` counts, it's shared
between the pathfinding system and the room list.

### `.zsk` is the zone's **skybox**, in `MODEL_FORMAT.md`'s format

**Two corrections live in this section.** The first version of this doc
(and the matching `RENDERER_3D.md` note) said the `engine+0x62c` object's
fields get set from `<zone>.zon`. That was wrong — caught by re-tracing
which `sprintf`-built path actually feeds the `FUN_1002778c` call whose
result lands in `(*(engine+0x62c))+0x54`. It's `.zsk`, not `.zon`. `.zon`
only ever populates the separate `engine+0x5464` room-*list* array
(positions/names for up to 40 room slots, see below).

The second correction is what that mesh **is**. This section previously
read it as the zone's own baked room/dungeon geometry, verified against
`azra` alone. It is the zone's **skybox**, and `engine+0x62c` is a Skybox
object.

**The engine's own name for it.** Three debug strings, all present in
`6r51.app`:

- `"InitLevel Pre skybox load"` / `"InitLevel Post skybox load"` — the
  marker pair bracketing exactly this file's `WholeFile_Load` call in
  `GameEngine_InitLevel` (`0x10024dec`; literal-pool references at
  `0x10026618`/`0x10026620`).
- `"Bullseye constructer Post newing Skybox"` — the marker at the line
  that constructs the object, and the string `"Skybox"` on its own appears
  dozens of times more in the binary's debug/assert text.

So `.zsk` is "zone sky", the same `z`-prefixed naming as `.ztx` (zone
texture), `.zlu` (zone LUT), `.zfg` (zone fog).

**The format is still an ordinary model resource.** Decompressing
`azra.zsk` (4-byte size header + zlib, per above) and reading its first 14
bytes as `MODEL_FORMAT.md`'s 7×`int16` header gives
`(7, 1, 30, 168, 56, 90, 1)` — `H0=7` ✓, `H6=1` ✓ (both format constants),
and `H5==H2*3` (`90==30*3`) ✓, exactly the invariant verified across all
226 `models.idx` entries. `H1=1` (one static frame) is right for a sky.
It is zlib-compressed and loaded per-zone rather than referenced through
the `models.idx` archive, unlike every placed `.ent` object.

**What the 21 shipped files actually contain** (census:
`port/src/tests/m70_skybox_smoke.cpp`, run over the real files):

- **Three distinct meshes, six distinct skins.** Eleven zones share a
  30-vertex/56-face mesh, nine share a 98-vertex/192-face one, `raiders`
  has a variant of the first. What varies per zone is the *picture*, not
  the geometry — which is the shape of a shared sky asset, and not of
  per-zone room geometry. (This is the observation that reopened the
  question: nine zones cannot each have the other's room geometry.)
- **The 30-vertex mesh is a dome, and its skin is a fisheye sky.** Four
  rings of eight/eight/eight/four vertices whose radius shrinks as local
  `+Y` grows (254 → 236 → 181 → 91), closed by a single apex vertex at
  the top and one more underneath. The apex's UV is texel **(127, 129)**
  — the exact centre of the 256×256 skin. So the skin is an azimuthal
  projection with the zenith at the middle, and the eight zones that carry
  a real picture are unmistakable when dumped: `azra` is a night sky with
  Masser and Secunda, `drgnfld`/`ghstpass`/`snowline`/`stouttp` share a
  blue day sky, `glaciercrawl` a grey blizzard one, `dstar_e`/`dstar_w` a
  hazy overcast.
- **Thirteen zones' skins are black**, and the split against the shipped
  display names is exact: every open-air zone has a picture, every
  interior (Crypt of Hearts I–III, Earthtear/Fearfrost/Loth'Na Caverns,
  Broken Wing I–II, Twilight Temple, Delfran's Hideout, Lakvan's
  Stronghold, Raider's Nest, the arena) has black.
- **The nine-zone file's texture header is nonsense and the engine does
  not care.** It reads `skinCount=256, width=256, height=0` — zero pixels
  by its own arithmetic — and its trailer is 4 bytes where a well-formed
  clip record is 6. The 131072 texel bytes are present in the file all the
  same, all zero. See the addressing note below for why none of that
  reaches the renderer.

**How the skybox is drawn** (full trace in `RENDERER_3D.md`): it is not
one more thing rendered into the scene, it is *the alternative to clearing
the frame*. `Render3DScene` runs, before any wall or actor:

```c
if (engine[0xbe0e] == 0 || ((Skybox*)engine[0x62c])->model == 0) {
    for (i = 0; i < 0x8f00; ++i)             // 176*208, the whole scene buffer
        sceneBuffer[i] = engine[0x630] | 0x7fff0000;   // colour | far depth
} else {
    RoomGeometry_TransformAndSort(engine);   // i.e. draw the skybox
}
```

and `RoomFace_RasterizeTextured`, the one rasterizer that path uses,
writes `texel | 0x7fff0000` per pixel — the same far-depth word, with no
depth test, no light term and no chroma-key cutout.

**Its texture addressing is hardcoded, not read from the header.** That
rasterizer indexes its texel as

```c
texel = *(u16*)(pixels + (((v & 0xff00) + ((u >> 8) & 0xff)) * 2));
```

— a fixed 256-wide row stride and an 8-bit row mask, where the *actor*
pipeline derives a shift from the texture header's width
(`Actor3D_TransformAndSubmitModel`'s `log2(width)` size class,
`MODEL_FORMAT.md`). So a `.zsk` always yields one 256×256 skin at the
fixed offset after the 8-byte header, whatever the header says, which is
exactly why the nine interior zones' impossible header does no harm on
real hardware: they draw a black dome, which is what an interior wants.

**Load-time state** (`GameEngine_InitLevel`, the `else` branch after a
successful `WholeFile_Load`), which is where the placement constants come
from:

| write | meaning |
|---|---|
| `skybox+0x54 = model` | the loaded `.zsk` resource |
| `skybox+0x5e = 0x200` | scale, 8.8 → **2×**, every zone, every load |
| `skybox+0xa8 = 0`, `+0xb2 = 0`, `+0xb6 = 0` | pitch/roll/yaw all zeroed |
| `engine+0xbe0e = 1` | "there is a skybox" — cleared instead if the load fails, which is what selects the flat fill above |

### The "bullseye" subsystem: a load-time light-propagation bake, not AI pathfinding

Debug-string brackets `"InitLevel Pre bullseye init"` → `Bullseye_Init`
(`0x1001b8d4`) → `"...init_map"` → `Bullseye_InitMap` (`0x1000e840`) →
`"...load_map"` (loads `<zone>.zcp` via `WholeFile_Load`, stored at
`engine+0x32c`) → `"...calc_lights"` (`Bullseye_LoadZmpCells`,
`0x1001b964`) trace a subsystem originally guessed to be AI
navigation/pathfinding. **Fully decoding `.zmp`'s bulk content and `.zcp`
corrects that guess**: this is a one-shot **per-zone lighting bake**
(every function below has exactly one caller — `GameEngine_InitLevel` or
each other — none run per-tick), not pathfinding. "Bullseye" may still be
this subsystem's internal name (unconfirmed), but its job is torch-light
propagation with wall-bounce, computed once at zone load.

**`Bullseye_Init`** (`Bullseye_Init(engine, ztxFirstByte, ztxBuffer+1,
surCount, surBuffer, zluChunk1, zluChunk2, zluChunk3, zluChunk4)` — 9
arguments; the raw call-site disassembly had to be read directly because
the decompiler only showed 4 of them, the same "extra args silently
dropped from the C view" quirk hit earlier with `SimKinObject_FindByName`)
just stashes all 8 loaded per-zone buffers/counts into fields on the
engine object (`+0x6914`/`+0x6918` for `.sur`, `+0x6b1c`/`+0x6b20` for
`.ztx`, `+0x6b24..+0x6b30` for `.zlu`'s 4 chunks) for later use. **All
three are consumed by the tile-grid wall/surface-face renderer** — see
[`RENDERER_3D.md`](RENDERER_3D.md#the-tile-grid-wallsurface-face-renderer-a-third-pipeline)
for the full decode (`.sur`'s record layout, `.ztx` as a palettized
texture atlas, `.zlu` as 4 selectable palettes).

**`Bullseye_InitMap`** (`Bullseye_InitMap(engine, field80, zmpTotal,
&progressLog)`) takes `.zmp` header's `field80`/`zmpTotal` fields as a **2D
grid's width/height** and allocates two arrays sized by `width*height`: a
4-byte-per-cell array at `engine+0x6904` (zeroed here; its first consumer
turned out to be `SurfaceFace_BuildAndProject`, part of the tile-grid
wall/surface-face renderer — see `RENDERER_3D.md`) and an **8-byte-per-cell
light/nav grid** at
`engine+0x6908`, whose defaults it seeds (byte offset `+2`=0, `+3`=0x3f,
and byte `+0` bit 0 set on every 16th cell as a placeholder waypoint
marker — all overwritten by real data next).

**`Bullseye_LoadZmpCells`** (was `FUN_1001b964`, matches the
`"calc_lights"` debug marker) resolves **the bulk of `.zmp`'s content**:
everything from decompressed offset `0x84` onward (right after the
132-byte `ZmpHeader` — this is the answer to "what's the rest of `.zmp`
for") is a **`field80` × `zmpTotal` grid of 6-byte cell records**, copied
byte-for-byte into the first 6 bytes of each 8-byte `engine+0x6908` cell:

```c
struct ZmpCell {           // 6 bytes on disk, per grid cell; the in-memory
                             // engine+0x6908 record is 8 bytes -- the last 2
                             // (not present on disk) are runtime-only scratch
    uint8  flags;            // bit0 = light source; bit1 = wall/obstruction
                              //   (blocks + bounces Bullseye_PropagateLight's
                              //   rays AND TileGrid_RaycastVisibility's rays,
                              //   see RENDERER_3D.md); bit3 = force-draw a
                              //   surface face regardless of camera side
    uint8  unknown0;          // not decoded
    uint16 lightLevel;         // baseline light level, recalculated below
    uint16 zcpIndex;            // index into .zcp's 36-byte type-table (this
                                  // doc's ZcpEntry below, and WORLD_MODEL.md's
                                  // "36-byte tile-type table" -- same thing)
    uint8  visibleFrameStamp;      // runtime-only (not loaded from .zmp): last
                                     // frame (mod 4) TileGrid_RaycastVisibility
                                     // marked this cell visible, deduplicates
                                     // its per-frame output list
    uint8  unknown7;                 // runtime-only, CONFIRMED DEAD, not just
                                       // undecoded. `TileGrid_RaycastVisibility`
                                       // (`FUN_1000f694`) writes byte +6
                                       // (`visibleFrameStamp`) every frame but
                                       // never touches +7. Broadened the
                                       // search past that one function: a
                                       // whole-binary scan for any `STRB
                                       // <reg>, [<reg>, #7]` (byte-store at a
                                       // constant +7 offset, any base
                                       // register/struct) found **zero hits
                                       // anywhere in the entire binary** --
                                       // nothing writes a byte-7 field via any
                                       // standard addressing mode, full stop.
                                       // The 29 `LDRB ...,[..,#7]` reads that
                                       // do exist all belong to an unrelated
                                       // struct (the player/camera
                                       // orientation field's `byte@+6 |
                                       // byte@+7<<24>>16` heading-decode
                                       // idiom, not this tile array). Combined
                                       // with `Bullseye_LoadZmpCells` only
                                       // ever populating the first 6 of 8
                                       // bytes from disk, this is a reserved/
                                       // always-zero scratch byte with no
                                       // functional consumer in this shipped
                                       // build -- not a cut feature, not an
                                       // unfound reader, genuinely dead.
};
```

**`Bullseye_BakeLighting`** (was `FUN_1000f130`) does the actual bake, in
three passes over the `engine+0x6908` grid: (1) zero every cell's
`lightLevel`; (2) for every cell with `flags` bit0 set (a light source),
call `Bullseye_PropagateLight` at that cell's world position
(`col*0x100+0x80`, `row*0x100+0x80` — the same 256-unit-per-tile 8.8
fixed-point scale as the actor-collision/position code in
`GRAPHICS_FORMAT.md`); (3) for every cell, look up `.zcp`'s per-cell
entry at `zcpIndex`, add its signed delta byte (× `0x100`) to
`lightLevel`, clamped to `[0, 0x3eff]`/saturating at `0x3f00`.

**`Bullseye_PropagateLight`** (was `FUN_1000ef74`) is a real **2D ray-cast
light-propagation-with-wall-bounce** simulation: casts rays in 256
directions from a light-source cell using the *same* 2048-entry sin/cos
LUT shape as `BuildRotationMatrix3x4`/the automap's rotated player marker
(`RENDERER_3D.md`), stepping cell by cell along each ray and adding a flat
`+0x40` to every cell's `lightLevel` it crosses (same clamp as above). A
cell with `flags` bit1 set (wall) makes the ray **bounce** — its step
direction sign-flips on that axis — rather than pass through; each ray
stops once it has bounced on both axes or left the grid. This is a
lightmap bake: torches/light sources spread illumination outward,
reflecting off walls, computed once per zone load rather than every frame.

**`.zcp`'s format, fully decoded from `Bullseye_BakeLighting`'s lookup**:

**Correction (port scaffold session, `port/src/world/zone.cpp`)**: `entryCount`
is a **`u32`** at offset 0, not the `u8` this doc originally guessed from a
partial decompile. Verified directly against real `azra.zcp`: the file's
decompressed size is exactly `4 + 36*11828` bytes, and its `.zmp` cells'
`zcpIndex` values go up to `11827` — both consistent only with a 4-byte
count (`azra.zcp`'s first 4 bytes decode to `11828` as a `u32 LE`; reading
only the first byte gives a nonsensical `52`, `1876` bytes short of the
real file). So `.zcp` isn't a small deduplicated "tile type palette" as
the entry-count-52 reading implied — it's a much larger per-placed-tile
(or similar granularity) table, one entry per `zcpIndex` a `.zmp` cell can
reference, not one per distinct *type*. `pad[3]` doesn't exist either;
those 3 bytes are the rest of the same `u32`.

```c
struct ZcpFile {
    uint32 entryCount;              // offset 0x00
    ZcpEntry entries[entryCount]; // offset 0x04, stride 0x24 (36 bytes)
};
struct ZcpEntry {                  // 36 bytes -- now fully mapped
    int8   lightDelta;               // 0x00: signed, applied as delta*0x100
    uint8  unknown0;                   // 0x01, not decoded
    int16  floorBandThreshold;           // 0x02: **identified this session** (was
                                           // part of "unknown0[3]"). The FLOOR's own
                                           // eye-height gate: `Render3DScene`'s
                                           // floor-draw block is
                                           //   (flags & 2) == 0 &&
                                           //   (*(int16*)(entry+2) < cameraEyeHeight
                                           //    || (flags & 4))
                                           // -- i.e. the floor draws only when the
                                           // camera is above this scalar, or tile
                                           // `flags` bit2 forces it. An earlier pass
                                           // of this doc quoted that gate as reading
                                           // `ceilingBandThreshold`; it does not, it
                                           // reads offset **2**, and the two are
                                           // genuinely different values (in real
                                           // azra data they differ for the large
                                           // majority of tiles). Reading +4 here
                                           // instead gates the floor on the ceiling's
                                           // height and makes floors vanish.
    int16  ceilingBandThreshold;         // 0x04 (was "heightA"): a standalone
                                           // scalar, NOT part of either 4-corner
                                           // array below -- read only as the
                                           // *default* comparison threshold
                                           // (tile `flags` bit6 clear) against
                                           // the camera's eye-height field
                                           // (`*(camera+0x618)+0x224`) to pick
                                           // ceiling band A (surIndexCeilingA,
                                           // 0x1e) vs. B (surIndexCeilingB,
                                           // 0x1f). See below for the bit6-set
                                           // case.
    int16  floorHeight[4];                 // 0x06/0x08/0x0a/0x0c: the tile's 4
                                           // corner floor heights. Read
                                           // directly (unconditionally) as the
                                           // floor quad's 4 corners in
                                           // `Render3DScene`'s floor-draw
                                           // block; also read cross-tile
                                           // (current vs. the relevant
                                           // neighbor) to size a wall's
                                           // "lower band" vertical extent.
    int16  ceilingHeight[4];               // 0x0e/0x10/0x12/0x14: the tile's 4
                                           // corner ceiling heights. Read
                                           // directly as the ceiling quad's 4
                                           // corners for *both* ceiling band A
                                           // and B (same 4 values either way --
                                           // only the winding order and which
                                           // `.sur` index get used differ), and
                                           // cross-tile to size a wall's "upper
                                           // band" extent. **`ceilingHeight[3]`
                                           // (offset 0x14, was documented as its
                                           // own field "heightB") turns out not
                                           // to be independent data at all** --
                                           // when tile `flags` bit6 is *set*,
                                           // this same corner value is reused
                                           // as the ceiling-band comparison
                                           // scalar in place of
                                           // `ceilingBandThreshold` above. So
                                           // there's really one dedicated
                                           // threshold field plus one reused
                                           // corner height, not two independent
                                           // "heightA/heightB" scalars as
                                           // originally guessed. Confirmed by
                                           // decompiling `SurfaceFace_
                                           // BuildAndProject` (0x1005d784) and
                                           // `Render3DScene`'s (0x100166c8)
                                           // floor/ceiling/wall-band traversal
                                           // blocks in full and matching every
                                           // 16-bit read against these offsets.
    // IMPORTANT, and easy to get backwards -- see "Which tile owns a wall
    // face's material" below. These bytes are read from the tile on the
    // **far side** of the boundary being drawn, and the `_lo`/`_hi` names
    // are the opposite way round from the bands' actual vertical order:
    // the `_lo` family (0x16-0x19) is the MAIN/upper band and the `_hi`
    // family (0x1a-0x1d) is the LOWER floor-step band.
    uint8  surIndexE_lo;                       // 0x16: +X boundary, MAIN band --
                                                 // also the "is there a face on
                                                 // this boundary at all" gate for
                                                 // the whole direction (0xff = no
                                                 // face, and the step band is
                                                 // skipped with it)
    uint8  surIndexW_lo;                        // 0x17: -X boundary, main band
    uint8  surIndexS_lo;                        // 0x18: +Y boundary, main band
    uint8  surIndexN_lo;                        // 0x19: -Y boundary, main band
    uint8  surIndexE_hi;                        // 0x1a: +X boundary, floor-step band
    uint8  surIndexW_hi;                        // 0x1b: -X boundary, floor-step band
    uint8  surIndexS_hi;                        // 0x1c: +Y boundary, floor-step band
    uint8  surIndexN_hi;                        // 0x1d: -Y boundary, floor-step band
    uint8  surIndexCeilingA;                    // 0x1e: ceiling, band A
    uint8  surIndexCeilingB;                    // 0x1f: ceiling, band B
    uint8  surIndexFloor;                       // 0x20: floor
    uint8  unknown1;                            // 0x21: near-constant in real
                                                  // data (99.98% zero across
                                                  // azra.zcp's 11,828 entries,
                                                  // 2 outliers at 254) -- reads
                                                  // as padding, no consumer
                                                  // found. Not decoded.
    uint8  unknown2;                            // 0x22: genuinely varying
                                                  // real per-tile-type data
                                                  // (no dominant value across
                                                  // azra.zcp), but no consumer
                                                  // found in any cached
                                                  // decompile. Not decoded.
    uint8  cornerShape;                         // 0x23: DECODED. A per-tile-
                                                  // type diagonal/wedge-corner
                                                  // selector, read in
                                                  // `SurfaceFace_
                                                  // BuildAndProject`
                                                  // (0x1005d784)'s per-vertex
                                                  // quad-building loop: an
                                                  // 8-case switch nudges that
                                                  // corner's world x/z by
                                                  // +-0x80 (half a tile) in
                                                  // the 4 diagonal + 4
                                                  // axis-aligned combinations
                                                  // -- the classic dungeon-
                                                  // crawler "diagonal tile"
                                                  // trick (a la Eye of the
                                                  // Beholder/Ultima
                                                  // Underworld), beveling a
                                                  // face's quad corners
                                                  // instead of leaving every
                                                  // tile axis-aligned.
                                                  // Cross-checked against real
                                                  // azra.zcp (11,828 entries):
                                                  // distribution is
                                                  // {0: 10846 (92%), 1: 254,
                                                  // 2: 195, 3: 127, 4: 143,
                                                  // 5: 85, 6: 81, 7: 70,
                                                  // 8: 27} -- 0/default is an
                                                  // ordinary square tile,
                                                  // 1-8 real but rare wedge
                                                  // usage, matching the
                                                  // switch's cases exactly.
};
```

**Every face-direction `.sur`-index byte pinned down**, resolved by fully
reading `Render3DScene`'s tile-grid traversal (`RENDERER_3D.md`'s "The
traversal" section) rather than sampling isolated snippets as an earlier
pass of this doc did. The traversal is five near-identical blocks, one
per direction, each reading its `.sur` index from the *neighboring*
tile's type entry (current tile's own entry for floor/ceiling), gated by
either a camera-position-vs-tile-edge check or the tile's `flags` bit3
override, exactly as described in `RENDERER_3D.md`. Each wall direction
actually fires up to **two** draws ("lower band" using `floorHeight[4]`,
"upper band" using `ceilingHeight[4]` — a stepped-height wall segment
when the neighbor's floor/ceiling height differs from the current tile's,
now confirmed field-by-field above), selected by comparing the relevant
corner-height array between the two tiles; ceiling similarly picks
between two texture indices based on comparing `ceilingBandThreshold`
(or, if `flags` bit6 is set, `ceilingHeight[3]` instead) against the
camera's eye-height field (`*(camera+0x618)+0x224`, itself
`playerZ + a fixed eye-height offset`, set in `GameEngine_InitLevel`'s
player-start code). Floor has just one texture index and one call, using
`floorHeight[4]` directly as its quad's 4 corners. Tile `flags` bit6
(`0x40`) selects which of the two ceiling-comparison scalars above to
use — a fifth `flags` bit now identified, alongside bit0 (light source),
bit1 (wall), bit3 (force-draw), and bit2 (used in the floor gate,
`flags & 4`, role not pinned down beyond "also forces a draw").
**Confirmed directly against `Render3DScene`'s (0x100166c8) exact floor-
draw gate** (line ~531-549):
```c
if ((*pbVar36 & 2) == 0 &&                                    // not a wall
    (floorBandThreshold < cameraEyeHeight || (*pbVar36 & 4) != 0))
```
bit2 unconditionally forces the OR to true, making the floor face draw
regardless of the height comparison — mechanically identical in spirit
to bit3's wall force-draw. The *why* (which real tile situations set bit2
in shipped zone data) still isn't recoverable from code alone.

**Correction (PC-port session):** an earlier version of this note named
`ceilingBandThreshold` (offset `0x04`) in that gate. The real instruction
reads `*(short *)(iVar24 + 2)` — offset **`0x02`**, the separate
`floorBandThreshold` field now documented in the struct above. The two are
genuinely different values in shipped data, so the mix-up isn't cosmetic:
gating the floor on the *ceiling's* threshold makes floors disappear
across most of a zone.

### Which tile owns a wall face's material, and how the two bands work

Also fully pinned down this session, by reading all four of
`Render3DScene`'s wall blocks (and correcting a long-standing
approximation in the PC port):

- **The `.sur` index for a boundary comes from the tile on the far side of
  it, never from the tile being iterated.** In the `+X` block the entry
  pointer is rebuilt as `zcpBase + *(u16 *)(pbVar36 + 0xc) * 0x24` — that
  is the *next* cell's `zcpIndex` (runtime cells are 8 bytes; `+0xc` is
  `(cell+8)+4`) — and the `-X` block mirrors it with `pbVar36 - 4`. The
  two `±Y` blocks do the same through row-stride cell indices.
  Measured against shipped data, "current tile's index" and "neighbour's
  index" disagree for **100%** of azra's wall boundaries, 98% of
  snowline's, 92% of ghstpass's and 12% of crypt1's — so this is not a
  subtle distinction, it decides most walls' material outright.
- **Faces are not gated on "is the neighbour a wall".** Any boundary can
  carry a face; `0xff` in the neighbour's main-band byte means "no face
  here" and skips the direction entirely. What actually makes a flat
  corridor draw nothing is that both bands come out with zero vertical
  extent.
- **Two bands per direction**, in this vertical order:
  - the **floor-step** band (index from the `0x1a`-`0x1d` family), drawn
    when `currentFloor < neighbourFloor` at either edge endpoint, spanning
    `currentFloor → neighbourFloor`;
  - the **main** band (index from the `0x16`-`0x19` family), spanning
    `max(currentFloor, neighbourCeiling) → currentCeiling`, skipped only
    when that range is empty at *both* endpoints.
  A solid neighbour (floor raised, ceiling dropped below it) makes the two
  together span the full opening — that is where an ordinary wall comes
  from. Note this makes the naming in the struct above misleading: the
  `_lo` family is the upper/main band.
- **Wall tiles are iterated too** — only their floor/ceiling draw is
  suppressed (`(*pbVar36 & 2) == 0`). The height comparisons keep this
  self-consistent instead of double-drawing each boundary: on the solid
  side of an open/solid boundary, both bands evaluate degenerate.
- **A face draws only while the camera is still on the iterated tile's own
  side of that boundary** (`camera.x < x1` for `+X`, `x0 < camera.x` for
  `-X`, `camera.y < y1` for `+Y`, `y0 < camera.y` for `-Y`), unless the
  tile's `flags` bit3 forces it.

### The corner-index convention (0-3) — decoded

`floorHeight[4]`/`ceilingHeight[4]`'s indices map to tile corners as:

| index | corner position |
|-------|-----------------|
| 0     | `(x0, y1)`      |
| 1     | `(x1, y1)`      |
| 2     | `(x1, y0)`      |
| 3     | `(x0, y0)`      |

Read straight off the floor-draw block, which assigns vertex X from
`x0,x1,x1,x0` and vertex Y from `y1,y1,y0,y0` alongside
`floorHeight[0..3]`, and confirmed independently by all four wall blocks
(e.g. `+X` pairs this tile's `floor[2]`/`floor[1]` against the
neighbour's `floor[3]`/`floor[0]` — exactly the two points `(x1,y0)` and
`(x1,y1)` the two tiles share). This is the **reverse** of the
`0=NW,1=NE,2=SE,3=SW` order the PC port had assumed; the difference is
invisible on the flat tiles that make up most of a zone but mirrors every
sloped tile about its diagonal.

**Bits 4-5 identified** (PC-port session, chasing a real-vs-port
screenshot mismatch): the tile-grid wall/surface renderer's `.zlu`
palette selection (`SurfaceFace_ClipAndDispatch`'s 2-bit selector,
`RENDERER_3D.md`) reads `ZmpCell::flags` bits 4-5, not anything from
`.sur` — a per-face color "family" is therefore a property of *which
tile* (specifically: the blocking neighbor, for a wall face; the current
tile itself, for floor/ceiling), not of which `.sur` material index is
in use. `flags`' only remaining undecoded bit is bit7.

**This is also `WORLD_MODEL.md`'s "36-byte tile-type table at `map+0x690c`"**
— found independently in a much earlier round of this project, before
`.zcp` itself was identified. `.zcp`'s entries array *is* that table;
each per-cell tile record's `typeId`/`zcpIndex` field (see
`Bullseye_LoadZmpCells` above) is the index into it. See `WORLD_MODEL.md`
for the object-identity correction this unification came with, and
`RENDERER_3D.md`'s tile-grid wall/surface-face renderer for how
`surIndexA`/`B`/`C` get used to pick per-direction wall textures.

This also resolves what `azra.sta` isn't related to: an earlier pass of
this note guessed a link to "bullseye pathfinding" — there's no pathfinding
here at all, just a static lighting bake, so that guess is moot (see
`azra.sta`'s own entry below for what *is* known).

## `.stn`: per-instance trap/lockpick difficulty bindings

An **eighth per-zone file**, missed in earlier passes because it's loaded
*conditionally* (only when `GameEngine_InitLevel`'s third parameter is
non-zero — most likely "this zone was already visited earlier this play
session" rather than a first-time load) rather than unconditionally like
the other seven. Found via its debug-marker bracket, `"InitLevel Pre
skTreeNodes"` — "sk" = SimKin, so "SimKin Tree Nodes" — right before the
`"Init scripts"`/`"Done init scripts"` phase.

On-disk format (plain, uncompressed, streaming-read via
`FUN_1009eb68`/`FUN_1009ec80` like `.sur`/`.zon`/`.pth`/`.ent`): a `u16`
record count, then per record two Pascal-style strings (`u16` length +
that many bytes, no NUL on disk):

```c
struct StnRecord {
    uint16 varRefLen; char varRef[varRefLen];   // e.g. "resistDisarm[20]"
    uint16 nameLen;   char name[nameLen];        // e.g. "Chest02"
};
```

For each record, the game looks up a SimKin object by `name`
(`SimKinObject_FindByName`, 0x100732c8, on `engine+0x470`, the same
named-object registry used elsewhere in this function) and, if found,
replaces that object's `+0x3c` field with a copy of `varRef` (logging
`"Could not find %s"` if the lookup fails).

**Verified by directly parsing 4 real `.stn` files** (`dstar_e.stn`,
`dstar_w.stn`, `crypt1.stn`, `lakvan.stn` — `azra.stn`/`twilite.stn` are
empty, count 0): every single record's `varRef` is `"resistDisarm[N]"` for
some integer `N`, and every `name` is a short object identifier that's
either a door (`door1`..`door5`, `sdoor1`, `lakdoor`) or matches the
`Chest0N` naming used by `entities.txt`'s container-category objects (see
`thirdField`/category above). E.g. `crypt1.stn`: 5 records, all
`"resistDisarm[28]" -> doorN`; `dstar_e.stn`: 17 records, mostly
`"resistDisarm[17]"` against various short names plus 3 chests at
`resistDisarm[16]/[20]/[25]`.

So `.stn` binds specific named lockable objects (doors, trapped/lockable
containers — the same `category` values 8/11/12 identified above) to a
slot in a shared, global SimKin array called `resistDisarm` — i.e. **each
door/chest instance's lockpicking/trap-disarm difficulty is looked up
through this per-instance indirection** rather than being a fixed property
of the entity type. Not yet traced: what non-zero values of
`GameEngine_InitLevel`'s `param_3` actually correspond to at the call site
level (save-game load vs. same-session zone re-entry).

**Where `object+0x3c` is read, closing the loop (found directly in the
readable SimKin scripts, no further native RE needed)**: every SimKin class
for a lockable object (e.g. `chest_trapa.s`) declares its own field
`resistDisarm[N]` with a small constant default (e.g. `resistDisarm[5]`).
The lockpicking minigame's script, `menus\usepicks.s`, calls
`GetPlayer().CanDisarmTrap(GetOpener().resistDisarm)` — i.e. it reads the
difficulty straight off the opened object's own `resistDisarm` field. What
`.stn` does at zone-load time (confirmed by re-reading
`SimKinObject_FindByName`'s disassembly: its second parameter, the object
`name`, is not dropped — it's silently forwarded through an untouched
register into `FUN_1008fc04`, the real consumer) is overwrite that
per-object field with
a **reference** to a shared global array slot (`"resistDisarm[28]"`, etc.)
instead of the class's own constant. That's why `crypt1.stn` binds all 5
of its objects to the same `resistDisarm[28]` — multiple doors sharing one
externally-tunable difficulty value, rather than each carrying its own
fixed constant.

### `GameEngine_InitLevel`'s `param_3`: full entry vs. partial reload

Traced its sole call chain end to end:
`GameEngine_InitLevel` ← `FUN_10027d0c` (new thread's entry point) ←
`FUN_10027d44` (spawns that thread) ← two call sites,
`FUN_10019780` (passes literal `1`, itself gated by its own caller's flag)
and `FUN_10069cac` — a per-tick state-machine function (`switch` on a
`LoadState`-looking field) whose `case 3` (`case 10` always uses `0`) picks
`1` or `0` depending on a busy-check (`FUN_1000db04`) against another
subsystem. The exact trigger for choosing `0` vs `1` at that call site
wasn't pinned down further — but **what the two values *do* inside
`GameEngine_InitLevel` is unambiguous**, since both of `param_3`'s uses are
inside `GameEngine_InitLevel` itself, not caller-side:

- `param_3 != 0` → the `.ent` player-start record (`typeId == 1`) writes
  the player's position/orientation (see above) **and** `.stn` loads at
  all (rebinding lock/trap difficulty overrides).
- `param_3 == 0` → both are skipped: the player keeps their current
  position, and no `.stn` overrides are (re)applied.

So `param_3` distinguishes **a full/fresh zone entry** (place the player
at the zone's designated start point, apply per-instance lock-difficulty
bindings) from **a lighter reload of the same zone's data** where the
player's live position and any already-applied `.stn` overrides should be
left alone — consistent with, e.g., reloading a save vs. some other
same-session re-init path that doesn't want to teleport the player.

## `azra.sta`: a leftover level-editor "staging" file, not a game format

Resolved not through binary RE (nothing in `6r51.app` reads `.sta` files —
confirmed again this pass, no `"%s\%s.sta"` format string anywhere) but by
directly parsing the real file and comparing it against `azra.ent`'s
already-decoded entity placements — the same technique already used
elsewhere in this project for readable SimKin scripts, applied here to a
binary asset instead. Tool: `tools/parse_zone_placement.py`.

**On-disk format**: 201 fixed 32-byte records, no header, followed by a
4-byte trailer (`01 00 00 00` — likely a version/format tag, not a count):

```c
struct StaRecord {          // 32 bytes
    int32  typeId;            // 0x00 -- matches .ent's typeId field
    int32  x;                  // 0x04 -- matches .ent's x
    int32  y;                   // 0x08 -- matches .ent's y
    int32  z;                    // 0x0c -- matches .ent's z
    uint16 rot[4];                 // 0x10 -- matches .ent's rotOrScale[4] exactly
    int32  unkA;                    // 0x18 -- matches .ent's unkA field exactly
    uint16 flags;                     // 0x1c -- a per-instance value, see below
    uint16 marker;                      // 0x1e -- always 0xCCCC (every one of 201
                                          //         records)
};                                                 // 32 bytes total
```

**Verified against real `azra.ent` data**: of `.sta`'s 201 records, **188
(93.5%)** have a position that appears somewhere in `azra.ent`'s 282
records — and of those 188 matches, **100% match on both the rotation
quad and the `unkA` field** (`typeId` matches 143/188, the shortfall
fully explained by multiple `.ent` entities sharing the exact same
position, which the position-only lookup can't disambiguate). This is
airtight: `.sta`'s per-record fields are a strict subset of `.ent`'s own
fields (same `x`/`y`/`z`/`rot`/`unkA`/`typeId`), for the overwhelming
majority of records.

**`flags`/`marker` resolved: they're not missing from `.ent` at all — `.ent`'s
`unkB` field (the one this doc previously called "not decoded, near-constant
filler") is `.sta`'s `flags`/`marker` pair, still packed together.**
Checked bit-for-bit against all 188 position+`unkA`-disambiguated matches:
`.ent`'s `unkB` as a raw `uint32` is **exactly `(0xCCCC << 16) | flags`** —
its high 16 bits are `.sta`'s `marker` constant, its low 16 bits are
`.sta`'s `flags` value, **188/188 exact matches, zero exceptions**. So
`.ent`'s struct comment above is corrected to describe `unkB` as this same
packed pair rather than a plain, mostly-constant int32 — the "near-constant"
read of an earlier pass came from only sampling `unkB`'s high half, which
genuinely is constant (`0xCCCC`), while its low half is exactly as variable
as `.sta`'s `flags`. Tool: `tools/analyze_sta_fields.py` (new, extends
`tools/parse_zone_placement.py`).

**`flags`'s own semantic meaning is still open**, though now much better
characterized: always a multiple of 16 (13 distinct values across `azra.sta`'s
201 records, range 80–288 i.e. `16×5`–`16×18`), **256 (`16×16`) by far the
default** (179/201 = 89%), with the remaining 22 records spread across the
other 12 values. Ruled out: it is **not** `entities.txt`'s entity-category
enum (`ZONE_FORMAT.md`'s `thirdField`, [[shadowkey-zone-format]]) scaled by
16 — checked `flags/16` against the real category for every record's typeId
(both `.sta`'s own `typeId` field and, separately, the matched `.ent`
record's `typeId`) and got **0 matches either way**, not even a majority —
firmly rules that hypothesis out, it's not a scaled copy of the category. It
is also **not** a function of `typeId` alone (55%, 10 of 18 distinct
`typeId`s in `azra.sta` take more than one `flags` value across their
instances — e.g. typeId 10, `!roof`, takes 7 different values), so it's
genuine per-placed-instance data, not type-level metadata. The records with
non-default `flags` skew toward small decorative props placed repeatedly
with variation (`!barrel`, `!crate`, `!grainbag`, `!candelabra`,
`!tradinggate`, `!gryphon`, `!roof`) — consistent with, but not proof of, a
level-editor-only per-instance visual variation dial (e.g. a scale or
size-class slider) that the shipped game's renderer never reads (confirmed
separately, again, that no code in `6r51.app` opens any `.sta` file at all).
Given `.sta` is dev-tool-only staging data with zero runtime consumers, this
may not be resolvable further than "characterized precisely, not identified
semantically" without an actual level-editor build to compare against.

**Final pass, closing this out.** Four more angles were tried, specifically
to check whether the "zero runtime consumer" wall could be worked around
some other way:

- **A second `.sta` file to cross-check against?** No — a full search of
  the extracted install image found exactly one `.sta` file total,
  `azra.sta`. Every other zone (21 total, each with a `.ent`) has none.
  Previously assumed from absence of evidence; now confirmed by direct
  search.
- **Level-editor tool strings in the binary?** No — dumped all 3501
  ASCII strings ≥4 chars from `6r51.app` and grepped for
  editor/tool/staging/export/author/source-control keywords: zero hits.
  The binary's own `"%s\%s.<ext>"` load-pattern strings cover every real
  zone format (`.ent`, `.zmp`, `.sur`, `.ztx`, `.zon`, `.pth`, `.zfg`,
  `.zlu`, `.zcp`, `.zsk`, `.stn`, `.s`) but conspicuously never `.sta` —
  one more independent confirmation nothing loads it.
- **Correlate against script-authored data instead of `.ent`'s own
  fields?** This is the one real finding. Split `.ent`'s 282 records by
  whether they carry a real authored `name` (63 do — NPCs, markers,
  doors) vs. a placeholder (`none`/`noname`, 219 do — generic/decorative
  props). Result is a clean, exceptionless partition: **all 63 named
  records have `flags == 256` (the default), 0/63 deviate.** Every one
  of the 22 non-default-`flags` records in the zone is an unnamed prop.
  This sharpens the existing "skews toward decorative props" observation
  from a statistical lean into a hard 100%/0% split: whatever `flags`
  drives, it is provably never applied to a named/scripted entity.
  Those same 22 records are also spatially clustered into one small
  sub-region of the map (`x∈[24002,31460]`, `z∈[-3340,-1686]`, vs. the
  full zone's `x∈[1744,31872]`, `z∈[-7700,-1680]`) — one localized area,
  not a zone-wide mechanic.
- **Ordering/index correlation?** Weak, not proof: no long runs of a
  repeated `flags` value in record order (max run length 2, rules out
  "last N objects placed got the same tag"), but the 22 non-default
  records fall into two index bands (2-35 and 177-194) that echo the
  spatial clustering above rather than adding an independent lead —
  consistent with two separate editing sessions, not demonstrated.

**Verdict: closing this as characterized as precisely as static analysis
allows.** The named/unnamed partition and spatial clustering are real,
verified, new evidence — `flags` is confirmed to be a decorative-prop-only,
single-room-localized variation, strengthening (not just "consistent
with") the level-editor visual-variation-dial hypothesis. But its *exact*
meaning (what `flags=128` looks like vs. `240`) has no further lever to
pull: no second `.sta` file, no tool strings, and — as already
established — no runtime consumer at all, so there is nothing left to
trace it against short of an actual level-editor build or source. This
field is not going to resolve further within this project.

`unkA` (already known to round-trip byte-for-byte between `.sta` and `.ent`)
is also now more precisely characterized rather than left as a bare
"not decoded": across all 282 `.ent` records, 190 (67%) are exactly `0`
(the default), the 92 nonzero values range symmetrically from -32384 to
32384 with a GCD of only 2 (i.e. not a clean multiple of the game's usual
16 or 256 fixed-point units the way `flags` is), and it is **not** a
function of `typeId` (16 of 34 distinct `typeId`s take more than one
`unkA` value) and does **not** equal any of the same record's own `rot[4]`
values. The same named/unnamed split used for `flags` above shows `unkA`
runs on **independent, opposite logic**: nonzero on 54/63 (86%) of named
records vs. only 38/219 (17%) of unnamed ones, and spread across the
*entire* zone rather than one localized area — confirming `flags` and
`unkA` are governed by unrelated mechanisms, and hinting `unkA` may be
something like a scripting/dialogue/spawn-condition id tied mostly to
named entities, though no value-level correlation to any specific script
hook was found (not attempted in depth this pass). No further semantic
lead beyond that — still genuinely undecoded, just numerically bounded
and now behaviorally distinguished from `flags`.

**Conclusion**: `azra.sta` is a **leftover development-tool export of
(a slightly earlier version of) the same entity-placement data now
shipped as `azra.ent`** — not a distinct format the game ever reads. The
13 unmatched records (6.5%) are consistent with entities added, moved, or
removed in `.ent` after `.sta` was last saved. The `.sta` extension itself
now reads naturally as "staging" — an internal level-editor working file
saved alongside the final exported `.ent`, accidentally left in the
shipped install image for `azra` (the first/tutorial zone) and never
cleaned up, matching the earlier finding that no other zone has a `.sta`
file and no code in the binary ever opens one. The 5 unresolved literal
`"azra"` strings in the binary remain a separate, still-unexplained loose
end (not shown to be related to `.sta` — no code path connects them).

## What's still open

- Precise field semantics of the `.ent` record's four `u16` fields at
  `0x0c` (rotation? scale? at least one, `local_75c`, is confirmed used as
  `object+0x5e`/"modelFlags").
- The `.sur` (surface) and `.pth` (AI spawn/patrol path) formats — only
  their outer count+record framing was traced, not decoded field-by-field.
- `.zon`'s room record byte layout is now fully decoded (4×`u16` at
  `0x00/0x02/0x04/0x06` + 64-byte name, see above) but the **semantic**
  meaning of the 4 fields is still open (room connectivity/neighbor
  indices? light or texture references?) — two of them are computed as
  `zmpTotal - rawField`, a reverse/from-the-end index into something sized
  by `.zmp`'s header count, itself also not identified.
- ~~`.zcp`'s contents, and the bulk of `.zmp`'s content~~ — **resolved**,
  see "The 'bullseye' subsystem" above: `.zmp`'s post-header bytes are a
  `field80`×`zmpTotal` grid of 6-byte light/nav cells, `.zcp` is a small
  indexed table of per-cell light deltas, and together with
  `Bullseye_PropagateLight`'s ray-cast they form a one-shot per-zone
  lighting bake. `.zmp`'s `unknown1`/`unknown2` header fields (64 of its
  132 header bytes) are still undecoded, as is most of each `ZmpCell`
  (`unknown0`) and `ZcpEntry` (35 of 36 bytes).
- ~~`.ztx`'s and `.zlu`'s decompressed contents~~ / ~~`.sur`'s per-field
  byte meaning~~ — **resolved**: all three turned out to belong to a
  previously-unknown **third 3D rendering pipeline** (a tile-grid
  wall/surface-face renderer, parallel to the actor pipeline and the
  `.zsk`-baked room mesh), found by tracing `Bullseye_Init`'s forwarded
  fields to their actual consumer. `.sur` is a per-face material record
  (UV scale/offset, flags, a texture index); `.ztx` is a flat, 8bpp-
  palettized wall-texture atlas indexed by that texture index; `.zlu` is
  4 selectable 256-color palettes that convert `.ztx`'s indices to real
  color. Full writeup, including why the earlier offset-grep tooling
  missed the consumer (the offset is built via a split rotated-immediate
  ADD, not a literal-pool constant or a single 12-bit LDR immediate —
  needed a new search tool, `pyghidra_find_split_offset.py`):
  [`RENDERER_3D.md`](RENDERER_3D.md#the-tile-grid-wallsurface-face-renderer-a-third-pipeline).
- ~~`azra.sta`'s format~~ — **resolved**, see its own section above: a
  leftover level-editor staging export of `azra.ent`'s entity placements,
  never read by the shipped game. The 5 literal `"azra"` strings in the
  binary remain unexplained (not shown related to `.sta`).
- ~~`.sta`'s `flags`/`unkA`/`unkB` fields~~ — **closed, as far as static
  analysis can take it**: `unkB` is fully resolved (it's not a separate
  `.ent` field at all, it's `.sta`'s own `flags`/`marker` u16 pair, still
  packed into one `.ent` int32, `(0xCCCC<<16)|flags`, verified 188/188
  exact matches). `flags` and `unkA` are precisely characterized —
  value distributions, ruled out as a scaled copy of `entities.txt`'s
  category enum or a function of `typeId`, and (final pass) `flags`
  proven decorative-prop-only/single-room-localized via a clean 0/63
  named-vs-22/22-unnamed partition while `unkA` runs on independent
  logic skewed toward named entities (86% vs. 17% nonzero) — but their
  exact semantic meaning is unrecoverable: confirmed only one `.sta`
  file ever shipped (no cross-zone comparison possible), no
  level-editor tool strings exist anywhere in the binary, and — as
  already established — nothing in `6r51.app` ever reads a `.sta` file
  at all, so there is no runtime consumer to trace meaning from and no
  further data-comparison lever left to pull short of an actual
  level-editor build or source. See the `azra.sta` section above and
  `tools/analyze_sta_fields.py`.

---

## `.zon` is the named-region list (M44)

`.zon`'s room record was decoded byte-for-byte above without recognising
what it was **for**. It is the answer to the roadmap's longest-standing
open question, "where does a named zone region live?" — the tags
`EnterZone(s)` receives (`"Skelos_Dead"`, `"YouSure"`, `"ghasts"`) and the
Encounter spawner's regions (`"battle41"`, `"fight12W"`). A previous pass
ruled out `.ent` placements by dumping every named one in three zones; the
names are in `.zon`, and a plain `grep` for `battle41` across the install
tree finds `lothcav.s` and `lothcav.zon` and nothing else.

So the record reads:

```c
struct ZonRegionRecord {        // offset  size
    uint16 x0;                   // 0x00    2   left edge, tile space
    uint16 yFlippedBottom;        // 0x02    2   -> y1 = gridHeight - this
    uint16 x1;                     // 0x04    2   right edge
    uint16 yFlippedTop;             // 0x06    2   -> y0 = gridHeight - this
    char   name[64];                 // 0x08   64  the region tag
};                                          // 0x48 = 72 bytes
```

**The Y flip resolves the old open item.** This doc previously noted that
two of the four fields are stored as `zmpTotal - field` and guessed they
"index into some zone-wide array sized by zmpTotal (portal/neighbor list?
texture-atlas range? light index?)". They are **coordinates in a flipped
axis**: `zmpTotal` is the `.zmp` header's grid height (this doc's own
`ReadU16(&zmp[0x82])`), and the conversion turns a bottom-up Y into a
top-down one. `d` is the top edge and `b` the bottom, so they swap places.

Three independent confirmations:

- azra's region named `start` decodes to x 118..121, y 42..46, and azra's
  `.ent` player-start tile — from a completely different file — is
  (118, 46).
- Every record in all 21 zones has `x0 < x1` and, after the flip,
  `y0 < y1`.
- `LockZone`/`UnlockZone` iterate `for (x = x0; x < x1; ++x) for (y = y0;
  y < y1; ++y)` straight over the cell grid. Only a rectangle type-checks.

### The cell record's second byte

`.zmp`'s 6-byte on-disk cell is `{u8 flags, u8 blockFlags, u16 light, u16
zcpIndex}` — the second byte had no known meaning and was skipped by the
port. It is what `LockZone`/`UnlockZone` write, bit 2 (`0x04`) being
"locked". The runtime cell is 8 bytes (`engine+0x6908`, indexed
`(width*y + x) * 8`), with the same first six.

> **M57: the byte is authored on disk, and it has a second known bit.**
> Found by reading the map (`WORLD_MODEL.md`), whose "draw this tile
> solid" test is `cellU16 & 0x2402` — one mask over the first *two*
> bytes, so `flags` bit 1 (wall) plus `blockFlags` bits 2 and 5. Counted
> across all 21 shipped zones:
>
> | bit | cells on disk | zones | on a wall cell |
> |---|---|---|---|
> | `0x04` (bit 2) | 40,517 | all 21 | 4,185 of them |
> | `0x20` (bit 5) | 13,064 | 6 | **never** |
>
> So bit 2 is not only a runtime `LockZone` flag: it is authored
> blocking, and 36,332 of its cells are not walls. It is also the bit
> M55's entity tile-stamp ORs in, so authored blocking, a locked gate and
> an oversized prop all land on one flag.
>
> Bit 5 appears only in fearfrst (9859), erthcave (1681), broken2 (588),
> lothcav (433), delfhide (324) and ffarena (179), in compact regions and
> never on a wall. Its only other reader is `FUN_100001ac`, which tests
> it during movement and makes a virtual call when the actor's Z is at or
> below that cell's floor height — a surface you sink into, not geometry
> you collide with. Water or a hazard pool fits both the code and the
> distribution; not named here on that evidence alone.
>
> Note `LockZone`'s `cellByte1 = 4` **assignment** (see the asymmetry
> table below) therefore destroys any authored bit 5 inside a locked
> rectangle — whether that ever happens in the shipped content was not
> checked.

> **M67: bit 2's second writer, quantified — and it is a writer that also
> erases.** The paragraph above is right that the entity tile stamp ORs
> into this bit; M67 measured how much of the on-disk population that
> accounts for, and found the erase half. Recomputing `FUN_10066204`'s
> footprint for every solid tile-stamped placement and re-stamping it onto
> the shipped grid: **2,069 placements across the 21 zones set 2
> previously-clear cells**, so the recomputed footprints are a subset of
> the baked bits to within two cells, and 19 of the 21 zones are exactly
> clean. Those footprints are **12,403 of the 40,517 set cells**, so
> roughly two thirds of the bit's on-disk population is authored blocking
> that no placement explains — the barriers those 30 `UnlockZone` sites
> open, plus whatever else. Naming that remainder is still open.
>
> The erase is `FUN_1006640c`, the same walk with `&= ~mask`, and its
> caller is the `SetPassable(bool)` native (Object/Entity dispatch case
> `0x17`): assign `Entity+0xd5`, then, if the entity is tile-stamped,
> stamp when it became solid and unstamp when it became passable. **A
> closed door's footprint is part of the shipped `.zmp`**, and
> `SetPassable` is the only thing that takes it out again — which is why
> a port that stored passability without stamping left every door in the
> game impassable. See `PORT_ROADMAP.md`'s M67 entry and
> `port/src/world/zone.h`'s `StampEntityBox`.
>
> Two properties of the walk worth recording as format facts: it steps
> **half a tile (128 units)** and is **rotated by the entity's current
> heading**, so a door's footprint moves when it swings; and the bit is
> one bit with **no reference count**, so overlapping stamps do not
> survive one of them clearing.

### Inclusive containment, half-open iteration

The two disagree in the shipped engine, and the port reproduces both.

`LockZone` (`FUN_10073470`) and `UnlockZone` (`FUN_10073380`) both run
half-open loops, so locking a region misses its last row and column.
Containment, though, has to be **inclusive**: checking all 21 zones' spawn
tiles against their own rectangles, 12 land inside a region, every one of
those regions is named like an arrival point (`start`, `entry`,
`Entrance`, `Enter`, `Exit`, `Zvoldoor`, `vig`), and four of the twelve
hold only under inclusive bounds because the spawn sits exactly on the far
edge — azra's `start`, broken1's `Exit`, crypt3's `Entrance`, fearfrst's
`Exit`. The `vig` hits corroborate: four zone scripts contain
`if (zone = "vig") Level.Vignette(n)`, an arrival cutscene.

### `LockZone` and `UnlockZone` are deliberately asymmetric

| | `LockZone` (`FUN_10073470`) | `UnlockZone` (`FUN_10073380`) |
|---|---|---|
| lookup | the name→room **map** (`FUN_100731b4`, a wide-string binary tree at `levelObj+0x84`) | a linear walk of all **40** room slots with `strcmp` |
| affects | one rectangle | every rectangle sharing the name |
| write | `cellByte1 = 4` — an **assignment**, wiping the rest of the byte | `cellByte1 &= 0xfb` |

Names are not unique — azra has four rectangles called `YouSure` and three
called `queue` — so the asymmetry is observable: locking `YouSure` blocks
one doorway, unlocking it opens all four.

### The per-level tile-change journal

Every Lock/Unlock routes its write through `FUN_1006d7bc(level, x, y)`,
which maintains a fixed **100-entry** array at `level+0x100` (count at
`level+0xfc`) of `{i16 x, i16 y, u16 savedByte}`. An existing entry for
the same tile is overwritten rather than appended, so the array is exactly
"the cells this level has diverged from its `.zmp` in".

> **Correction (M50).** This paragraph used to call the journal "the
> level-state half of a save file". It is not saved at all — a
> `<level>.dat` holds entity records and one trailing byte, with no tile
> data anywhere. The divergence comes back because each saved entity is
> recreated with its SimKin variables restored and then called by name:
> `Init`. A door whose `saved_Open` reads back as 1 re-applies its own
> `UnlockZone` from inside `Init`, and the tile bytes follow. See
> [`SAVE_FORMAT.md`](SAVE_FORMAT.md).

### The room list in memory

`GameEngine_InitLevel` expands each 72-byte record into a **0x84-byte**
room slot in a fixed 40-slot array at `engine+0x5464` (count at
`engine+0x5460`):

```
slot + 0x30 : x0      slot + 0x38 : x1
slot + 0x34 : y1      slot + 0x3c : y0
slot + 0x40 : char name[64]
```

and registers it in the by-name map with `FUN_10073210(levelObj, name,
slot)`.

## `products.dat`: the merchant product database (M59)

Not zone data, but it lives here for the same reason `entities.txt` does:
this doc is where the shipped **flat data files** are decoded, and this is
the last one the port had never opened.

`FUN_10035634` reads it with plain `fopen`/`fread` from
`e:\system\apps\6R51\products.dat`, and it is built lazily — the first
`AddProduct` call in the session triggers it, and the catalogue is then
shared by every merchant in the game.

```
u16 version                (36 in the shipped file)
u16 count                  (279)
count x {
    u16 templateId          -> record +0x00
    u16 category            -> +0x14   (the IPT_* enum)
    u32 price               -> +0x04, and copied to +0x08 as the base
    u16 rating              -> +0x0c
    u16 descriptionStringId -> +0x0e
    u8  armorSlot           -> +0x12
    u16 nameStringId        -> +0x10
    u16 classFlagCount      (9 in every shipped row)
    u8  classFlags[count]   -> +0x18..
}
```

Two things about that layout are worth stating, because a reader who
guesses will get both wrong:

- **The read order is not the offset order.** The one-byte armour slot at
  `+0x12` is read *before* the name string id at `+0x10`. A "fields in
  offset order" parse desynchronises on the first record; this one
  consumes the shipped file's 7258 bytes exactly, 279 records with nothing
  left over.
- **The version field is the record stride.** The guard is
  `if (version < 0x24) reject`, with the message *"This product database
  file is obsolete- version #%d"*, and `0x24` is 36 — the size of one
  record. So "version 36" means "records are 36 bytes".

### Why the file matters: the shop scripts lie

A merchant stocks itself from its own `.s`:

```
ClearProducts();
AddProduct(662, "Shadow Band", 4444, 3, IPT_Armor);
AddProduct(678, "Steel Pauldron", 111, 10, IPT_Armor);
```

and the handler reads **the first argument and the fourth**. The name,
the price and the category are looked up in `products.dat` instead — and
the two disagree. Across the corpus's **363** `AddProduct` calls in ten
merchant scripts, every template id resolves in the file, and **65 of them
quote a price the file contradicts**: a Steel Pauldron is 1111, not 111; a
Vicar Herb 45, not 30. The literals in the scripts are stale
documentation, and this file is what the game charges.

### Every field is named from a real reader

| field | named by |
|---|---|
| `templateId` | matched against `AddProduct`'s first argument; handed to the entity factory by `BuyItem` |
| `category` | indexes the store's per-category counters, which are what `GetProductCount(IPT_Weapon)` answers |
| `price` / base | `BuyItem` charges `count * price`; `ReducePrices(pct)` recomputes price from the base |
| `rating` | passed to the bought item's own `SetRating`; `buysell.s`'s table has two "rating" columns |
| `nameStringId` | the **lookup key** — the store's find-by-name compares it against the selected row's text, which is how `BuyItem(itemText, n)` resolves at all |
| `descriptionStringId` | what `GetProduct` and `GetItemDescription` return ("Long blade, damage 4 - 12") |
| `armorSlot` | 0 on everything that is not armour; on the 99 armour rows it partitions them exactly as their descriptions read |
| `classFlags` | nine booleans, one per character class — the same 0..8 `ChooseCharacter` clamps into; `buysell.s` asks before a purchase and offers "Buy Anyway" |

The armour slot's partition is clean enough to be self-verifying: 19 body,
9 legs, 12 feet, 15 head, 11 hands, 14 shield, 9 worn, and the remaining
10 armour rows on slot 0 are exactly the arm pieces.
