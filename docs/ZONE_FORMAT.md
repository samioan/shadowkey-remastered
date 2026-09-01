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
  entity, but **only if `GameEngine_InitLevel`'s `param_3` is non-zero**
  (`if (param_3 != 0) { ...write position... }`, otherwise this whole block
  is skipped and the player keeps whatever position they already had).
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
| `.ztx`  | `engine+0x364` (+ `engine+0x360` = first byte); forwarded to `engine+0x6b1c`/`+0x6b20` | the wall-texture atlas — **decoded**: a flat array of `0x4000`-byte (16384-byte), **8bpp-palettized** texture slots, one per `.sur` surface index. See "The tile-grid wall/surface-face renderer", `RENDERER_3D.md` |
| `.zmp`  | `engine+0x328`                | zone metadata — **header decoded, bulk content decoded** (a `field80`×`zmpTotal` light/nav grid, see "The 'bullseye' subsystem" below) |
| `.zlu`  | `engine+0x36c/0x370/0x374/0x378` (4×512-byte chunks of one 2048-byte blob); forwarded to `engine+0x6b24..+0x6b30` | **decoded**: 4 selectable 256-color palettes (512 bytes = 256×2-byte entries) that convert `.ztx`'s indexed wall texels into real 16bpp color, selected per-face by 2 bits of a material byte. **This is what the `"InitLevel Pre/Post LUA"` debug markers actually bracket** — "LUA" is short for `.zlu`, not the Lua scripting language (see correction below). See "The tile-grid wall/surface-face renderer", `RENDERER_3D.md` |
| `.zfg`  | `engine+0x5c4`                | the fog/fade lookup table `CompositeSceneBufferToScreen` reads when `engine+0xbe0f` is set (`RENDERER_3D.md`'s fade-LUT open item) — "zfg" = "zone fog" |
| `.zcp`  | `engine+0x32c`                | a small indexed table of per-cell light-level deltas for the lighting bake — **decoded**, see "The 'bullseye' subsystem" below |
| **`.zsk`** | **`(*(engine+0x62c))+0x54`** | **the current room's actual 3D model — see below, this is the important one** |

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
struct ZmpCell {           // 6 bytes on disk, per grid cell
    uint8  flags;            // bit0 = light source; bit1 = wall/obstruction
                              //   (blocks + bounces Bullseye_PropagateLight's rays)
    uint8  unknown0;          // not decoded
    uint16 lightLevel;         // baseline light level, recalculated below
    uint16 zcpIndex;            // index into .zcp's per-cell light-delta table
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

```c
struct ZcpFile {
    uint8  entryCount;        // offset 0x00 (only byte actually read)
    uint8  pad[3];              // offset 0x01..0x03, unread
    ZcpEntry entries[entryCount]; // offset 0x04, stride 0x24 (36 bytes)
};
struct ZcpEntry {              // 36 bytes; only the first is decoded
    int8  lightDelta;            // offset 0x00: signed, applied as delta*0x100
    uint8 unknown[35];            // offset 0x01..0x23, not decoded
};
```

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
- `azra.sta`'s format — still unidentified. Confirmed **not** related to
  the "bullseye" pathfinding chain (that's `.zcp`) and **not** loaded via
  either per-zone loader traced here (no `"%s\%s.sta"` format string
  exists anywhere in the binary) — it may be a level-editor-only artifact
  never read by the shipped game. Five literal (non-templated) `"azra"`
  strings exist in the binary with no resolvable references, an
  unexplained loose end.
