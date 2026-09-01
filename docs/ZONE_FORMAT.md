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

## What's still open

- The exact binary-search-tree *construction* for `engine+0xbe34` (where
  type descriptors and their model-archive-index field get populated from —
  presumably a global, not-per-zone, `.txt`/binary config listing every
  entity type in the game; not yet located).
- Precise field semantics of the `.ent` record's four `u16` fields at
  `0x0c` (rotation? scale? at least one, `local_75c`, is confirmed used as
  `object+0x5e`/"modelFlags").
- The `.sur` (surface) and `.pth` (AI spawn/patrol path) formats — only
  their outer count+record framing was traced, not decoded field-by-field.
- `.zon`'s 0x48-byte room record layout (only the count/stride was traced).
- `azra.sta`'s format (see `MODEL_FORMAT.md`) — still unpursued.
