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
2. **`<zone>.sur`** — small `u8` count + count×8-byte records (surface/
   material data, not pursued further here).
3. **`<zone>.zon`** — room definitions: `u16` count → `engine+0x5460`, then
   count × 0x48-byte (72-byte) room records into `engine+0x5464`, stride
   0x84 (132 bytes) per room slot.
4. **`<zone>.pth`** — AI monster spawn + patrol-path data: `u16` count, then
   per-monster a 0x44-byte (68-byte) header record (creates an object via
   `FUN_1001ae8c`) followed by a variable-length list of 8-byte waypoint
   entries (count read from the header, consumed via `FUN_1001ad2c`).
5. **`<zone>.ent`** — static/dynamic **entity placement**: `u32` count, then
   count × 0x48-byte (72-byte) records. **This is where a placed object's
   model gets assigned** (see below).

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
    uint16 rotOrScale[4];     // 0x0c    8   (four u16 fields, exact meaning TBD)
    int32  typeId;             // 0x14    4   entity/object type ID
    uint8  pad[6];               // 0x18    6
    char   name[40];               // 0x1e   40   object/instance name
};                                       // total 0x48 = 72
```

For each record:

- **`typeId == 1`**: special-cased as the **player start position** — writes
  `x`/`y`/`z` and the rotation fields straight into the player object at
  `engine+0x618` (a `CBase`-derived object, offsets `+0x94/+0x9c/+0xa4` for
  position, `+0xa8/+0xb2/+0xb6` for orientation) instead of creating a new
  entity.
- **`typeId > 1`**: looks up a **type descriptor** for `typeId` in a binary
  search tree rooted at `engine+0xbe34` via `EntityTypeDescriptor_Lookup`
  (`FUN_1008c1cc`, 0x1008c1cc — a plain BST: node = `{..,key@+8,
  data@+0xc,left@+0x10,right@+0x14}`, returns `node->data` for the first key
  `<= typeId`... actually exact match only, see code), then:
  1. Calls a factory virtual method to allocate the actual game-object
     instance for that type.
  2. Sets its position (`vtable+0x18`) from `x/y/z`, and orientation fields
     `+0xa8/+0xb2/+0xb6` from the record's u16 fields.
  3. Copies the `name` field to two string slots (`+0xcb`, `+0xe2`).
  4. Calls an `Init(engine)` virtual method (`vtable+0x10`).
  5. **Looks up the type descriptor again** and does:
     ```c
     object->modelPtr /* +0x54 */ =
         engine->modelCache /* +0x6b38 */ [ typeDescriptor->modelArchiveIndex /* +0xc */ ];
     object->modelFlags /* +0x5e (u16) */ = record.rotOrScale[3] /* local_75c */;
     ```
     This is the line that finally resolves the open question: the type
     descriptor found via the `engine+0xbe34` BST carries, at its own
     `+0xc`, the `models.idx` archive index for that entity type, and the
     object's model pointer is just a cache read from the already-populated
     `engine+0x6b38[archiveIndex]` slot — nothing is loaded from disk at
     entity-creation time, only at zone-init time via `_models.txt`.

This also **corrects/refines** a `RENDERER_3D.md` note: the animated-actor
model lookup there (`engine+0x6b38 + frameIndex*4`, indexed by `actor+0x2c2`,
described as a "16-bit current frame field") is the *same* `engine+0x6b38`
cache — `actor+0x2c2` is not a literal animation-frame counter, it's the
entity's assigned **model archive index**, set once at placement time from
the type descriptor exactly as traced here. The per-model *animation* frame
(for the MD2-style vertex table within one model resource) is the unrelated
`actor+0x70` field documented in `MODEL_FORMAT.md`.

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
- `FUN_1002c3a8` (a monster's on-death handler — spawns a fixed `typeId
  300`, which `entities.txt` line 212 confirms is `300 30 8 !bag_loot`,
  category 8/container, at the dying actor's position) and the more
  generic `FUN_10084438` (spawns whatever typeId is stored at the calling
  object's `+0x2f0`) both check `descriptor+0x10 == 8` before flagging the
  spawned object as a container (`+0x180`/`+0x181` = 1) — i.e. "loot bag
  drops on monster death" is implemented as: spawn typeId 300, and the
  code even re-derives "is this really a container?" from the category
  field rather than assuming it.
- `FUN_10005320`/`FUN_1003893c`/`FUN_1003b85c` all pass `descriptor+0x10`
  straight through as an argument to a virtual call at `vtable+0x18` on
  some other object (an equip/pickup dispatcher, not traced further) —
  consistent with using category to pick equip-slot/pickup behavior
  (weapon vs. armor vs. shield vs. consumable).

So `EntityTypeDescriptor::thirdField` (renamed **`category`**) is confirmed
as a coarse entity-class enum, not a numeric stat. `13` is unused in the
real data (no gap in the enum's *meaning*, just no entities assigned it).

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
| `.ztx`  | `engine+0x364` (+ `engine+0x360` = first byte) | zone texture archive (not decoded further) |
| `.zmp`  | `engine+0x328`                | unidentified blob (not decoded) |
| `.zlu`  | `engine+0x36c/0x370/0x374/0x378` (4×512-byte chunks of one 2048-byte blob) | unidentified 4-way LUT/table split; **this is what the `"InitLevel Pre/Post LUA"` debug markers actually bracket** — "LUA" is short for `.zlu`, not the Lua scripting language (see correction below) |
| `.zfg`  | `engine+0x5c4`                | the fog/fade lookup table `CompositeSceneBufferToScreen` reads when `engine+0xbe0f` is set (`RENDERER_3D.md`'s fade-LUT open item) — "zfg" = "zone fog" |
| `.zcp`  | `engine+0x32c`                | loaded by the "bullseye" subsystem's `load_map` step (see below) |
| **`.zsk`** | **`(*(engine+0x62c))+0x54`** | **the current room's actual 3D model — see below, this is the important one** |

### `.zsk` is the room/dungeon geometry itself, in `MODEL_FORMAT.md`'s format

**This corrects an error in the first version of this doc** (and the
matching `RENDERER_3D.md` note), which said the `engine+0x62c` room-render
object's fields get set from `<zone>.zon`. That was wrong — caught by
re-tracing which `sprintf`-built path actually feeds the `FUN_1002778c`
call whose result lands in `(*(engine+0x62c))+0x54`. It's `.zsk`, not
`.zon`. `.zon` only ever populates the separate `engine+0x5464` room-*list*
array (positions/names for up to 40 room slots, see below); `.zsk` is a
one-per-zone file that supplies the actual model `RoomGeometry_
TransformAndSort` renders.

**Verified directly against real game data**: decompressing `azra.zsk`
(4-byte size header + zlib, per above) and reading its first 14 bytes as
`MODEL_FORMAT.md`'s 7×`int16` header gives `(7, 1, 30, 168, 56, 90, 1)` —
`H0=7` ✓, `H6=1` ✓ (both format constants), and `H5==H2*3` (`90==30*3`) ✓,
exactly the invariant verified across all 226 `models.idx` entries. So the
zone's big walkable dungeon geometry is stored as an ordinary
`MODEL_FORMAT.md`-format model resource, just zlib-compressed and loaded
directly per-zone rather than referenced through the `models.idx` archive
— unlike every placed `.ent` object, which *does* go through the archive.
`H1=1` (one frame, i.e. static, no vertex animation) makes sense for room
geometry.

### The "bullseye" subsystem and `.zcp`

While tracing this, found debug-string brackets `"InitLevel Pre bullseye
init"` → `Bullseye_Init` (`FUN_1001b8d4`, uses the `.sur` data loaded
earlier) → `"...init_map"` → `Bullseye_InitMap` (`FUN_1000e840`, uses the
`.sur` header's `local_656`/`local_658` fields) → `"...load_map"` (loads
`<zone>.zcp` via `WholeFile_Load`, stored at `engine+0x32c`) →
`"...calc_lights"` (`FUN_1001b964`). "Bullseye" is presumably this game's
internal name for its AI navigation/pathfinding system (a `.zcp` "zone
collision/pathing" map feeding both AI movement and — per the debug
sequence ending in `calc_lights` — some lighting calculation, maybe
line-of-sight-based). Not traced further; noted here since it explains
what `.zcp` is for, correcting an earlier guess that `azra.sta` might be
related to it (it isn't, see below).

## What's still open

- Precise field semantics of the `.ent` record's four `u16` fields at
  `0x0c` (rotation? scale? at least one, `local_75c`, is confirmed used as
  `object+0x5e`/"modelFlags").
- The `.sur` (surface) and `.pth` (AI spawn/patrol path) formats — only
  their outer count+record framing was traced, not decoded field-by-field.
- `.zon`'s 0x48-byte room record layout — partially decoded: a
  `strcpy`-copied 64-byte name plus 4×`u16` header fields that land at the
  room slot's `+0x30/+0x34/+0x38/+0x3c` (two of them computed as `<a value
  from .sur's header> - <raw field>`, suggesting a reverse/from-the-end
  index rather than a direct one) — semantic meaning of those 4 fields not
  pinned down (room connectivity/neighbor indices? light or texture
  references?).
- `.ztx`/`.zmp`/`.zlu`/`.zcp`'s decompressed contents — only their loader
  and destination field are known, not decoded field-by-field.
- `azra.sta`'s format — still unidentified. Confirmed **not** related to
  the "bullseye" pathfinding chain (that's `.zcp`) and **not** loaded via
  either per-zone loader traced here (no `"%s\%s.sta"` format string
  exists anywhere in the binary) — it may be a level-editor-only artifact
  never read by the shipped game. Five literal (non-templated) `"azra"`
  strings exist in the binary with no resolvable references, an
  unexplained loose end.
