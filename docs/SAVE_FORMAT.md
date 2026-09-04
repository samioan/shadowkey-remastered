# The save system

The whole save format, in two passes. **M40** decoded the *container* --
an archive of named blobs -- and **M50** the *members*: the byte stream
they are written through and the recursive record graph that stream
carries. Both are implemented in the port.

## The files

Straight out of the binary's own format strings (all under
`c:\system\apps\6R51\`):

| path | what it is |
|---|---|
| `game0%d.sav` | the four numbered save slots — `GameAvailableForLoad` probes `0..3` |
| `current.sav` | the in-progress game; `SaveGame` promotes it into a slot |
| `character.dat` | the player's own record |
| `%s.dat` | per-level state, one file per zone — the debug string next to it reads *"Using %s to save our previous level"* |
| `levelinfo.txt` | a two-line text file, `"%d\n%d\n"`, opened `rt`/`wt` |
| `dragonstar.cfg`, `dragonstar.set` | configuration, written by `SaveConfig` |

A UTF-16 `v1023_1.0` string sits in the same code — a save format version
tag — as does `SaveCorrupted`, the message shown when the integrity check
below fails.

## The container

One small class handles every file above: constructor `0x10009b2c`,
instance size `0x1924`, holding a fixed **256-entry** table of `0x18`-byte
records plus an `RFs`/`RFile` pair. It is a **named-blob archive** — a
table of contents of `(name, offset, length)` triples, followed by the
blobs:

```c
u32 fileSize;         // patched on close (see below)
u32 recordCount;      // <= 256
repeat recordCount {
    u16 nameLength;   // strlen(name) + 1 -- the NUL is on disk too
    u8  name[nameLength];
    u32 dataOffset;   // biased: the reader fetches at dataOffset + 4
    u32 dataLength;
}
// ... payload blobs ...
```

**`fileSize` is the integrity check.** The create path (`FUN_10009f2c`)
writes a 4-byte placeholder up front; the close path (`FUN_1000b2bc`)
seeks back to offset 0 and writes the file's real size. The open path
(`FUN_10009bc4`) reads that field, compares it against the actual size on
disk, and on a mismatch wipes the archive and starts over. So a save
interrupted mid-write is detected by construction, without a checksum —
which is what `SaveCorrupted` is for.

Two sizes are *not* errors: a 4-byte file (only the size field) and a
`recordCount` of zero are both legitimately-empty archives, and the real
open path returns cleanly for each.

**The `+ 4` offset bias.** `FUN_1000b378` reads a member at
`dataOffset + 4` for `dataLength` bytes, i.e. the stored offset is
relative to just past the leading `fileSize` field rather than to the
start of the file. Reproduced rather than normalised, since it is what a
real device-written file contains.

**Lookup is case-insensitive.** `FUN_1000b100` walks the table with
`strcasecmp`, so a member written as `Character.dat` is found by
`character.dat`.

| function | role |
|---|---|
| `FUN_10009b2c` | constructor — clears the 256-entry table |
| `FUN_10009bc4` | open for read: size check, count, TOC loop |
| `FUN_10009f2c` | create for write: writes the size placeholder |
| `FUN_1000a09c` | writes the TOC |
| `FUN_1000b2bc` | close: seeks to 0, patches the real size |
| `FUN_1000b100` | lookup by name (`strcasecmp`) |
| `FUN_1000b378` | read a named member |
| `FUN_1000b41c` | destructor |
| `FUN_1000b49c` | delete a save file |

## The writer

`FUN_10018e70(engine, filename, showProgress)` is the save routine.
(An earlier pass recorded it as a crash/error display function — see the
correction in [`WORLD_MODEL.md`](WORLD_MODEL.md).) In order:

1. Saves the current `ScreenModeController` mode and sets **mode 4**, the
   mode `FUN_1002c010`'s progress bar draws in. The progress values it
   feeds that bar are literal percentages: **3, 10, 30, 50, 70**.
2. **Checks free disk space** — the device's free bytes, less a 512000-byte
   headroom, plus whatever the existing file already occupies, against an
   estimate built from the serialized sizes. Too little, and it restores
   the previous mode and returns **3**, which is the value
   `ActuallySaveGame` checks before showing its own failure message.
3. Serializes the player/camera object through its vtable slot `+0x130`
   into a 0x14-byte byte-buffer object (`FUN_1008c06c`; `+8` is the data
   pointer, `+0xc` the length), plus `FUN_100187d0` for the rest of the
   game state.
4. Opens the archive and writes two members: `character.dat`, then
   `"%s.dat"` formatted with the current level name — the pair the debug
   string *"Using %s to save our previous level"* sits beside.
5. Restores the previous screen mode and returns 0.

Members are added or replaced through two separate functions, chosen by
whether the name is already in the table: `FUN_1000b100` looks it up,
`FUN_1000a21c(archive, name, data, length)` **appends a new member** and
`FUN_1000a9f4(...)` **replaces an existing one**.

**One behavioural detail worth keeping.** Before serializing, the routine
swaps the player/camera's live position and heading
(`+0x94`/`+0x9c`/`+0xa4`/`+0xb6`) for a stored set at
`+0x1068`/`+0x106c`/`+0x1070`/`+0x1074`, and swaps them back afterwards —
so a save records an entry/respawn point rather than exactly where the
player is standing. It is guarded by `engine+0x5c0` and a
`FUN_1000db04(engine+0x5cc)` check on the multiplayer session, i.e. it
applies to single-player saves.

**Autosave before a level change.** `FUN_1006c31c` (the level-change
entry point) calls this writer on `current.sav` *first*, and **aborts the
level change** if it fails — which is why `current.sav` and the
per-level `<level>.dat` members exist at all.

## The script-facing API

All on the GameEngine root class (trie `0x14cf0`, dispatcher
`FUN_10078de4`), plus three on Zone/Level:

| binding | index | notes |
|---|---|---|
| `CanSaveGame` | 0x00 | |
| `GetSavedName` | 0x0f | reads an in-memory slot table at `engine+0xdf74`, 8 bytes per slot |
| `GetSavedTimeStr` | 0x10 | |
| `GameAvailableForLoad` | 0x16 | with no argument, probes slots 0..3; with one, probes that slot |
| `NewGame` / `NewGameHook` / `ClearNewGameHook` | 0x2a / 0x29 / 0x28 | |
| `LoadGame` | 0x2b | |
| `DeleteGame` / `DeleteAllGames` | 0x2d / 0x2e | |
| `GetSaveSlot` | 0x2f | reads the current slot byte at `engine+0x14a71` |
| `SaveGame` / `ActuallySaveGame` | 0x30 / 0x31 | `SaveGame` records the slot and defers; `ActuallySaveGame` does the work |
| `QuitAfterSave` / `SetQuitAfterSave` | 0x37 / 0x38 | |
| `SaveConfig` | 0x71 | `dragonstar.cfg`/`.set` |
| `AutoSave` | Level 0x08 | |
| `SaveLevelState` | Level 0x0d | the `<level>.dat` half |
| `RestoreSaveLevel` | Level 0x15 | debug string: *"RestoreSaveLevel setting to %s"* |

## The members (M50)

M40's "what is still open" is now closed. Both members are built by the
same machinery: a **byte stream** class, and a `Save`/`Load` pair at
**vtable slots `+0x130` and `+0x134`** of every serializable class.

### The stream

One 0x14-byte class, constructor `FUN_1008c06c`:

```
+0x00  (unused)     +0x08  u8* buffer -- a single new[] 0x40000, 256 KB
+0x04  vtable       +0x0c  u32 write cursor
                    +0x10  u32 read cursor
```

Two cursors over one buffer and **no bounds checks anywhere**. A save is
built by writing from 0; a load memcpy's a blob out of the archive and
sets `+0xc = length, +0x10 = 0` (`FUN_100195b0` does exactly that).
`FUN_1008bfe4` zeroes both cursors without touching the bytes — which is
how the writer measures a blob twice for its disk-space estimate.

Its vtable (`0x101001d8`) is one large overload set — slots `+0x08`..
`+0x44` write, `+0x48`..`+0x80` read — and several slots are *byte-for-
byte identical code at different indices* (four separate "write one byte"
thunks, four "read one byte"), i.e. distinct C++ overloads that compiled
to the same body. The wire format therefore collapses to:

| write slot | read slot | primitive | bytes |
|---|---|---|---|
| `0x0c`/`0x10`/`0x14`/`0x38` | `0x48`/`0x4c`/`0x50`/`0x74` | u8 | 1 |
| `0x20` | `0x5c` | i16 | 2, LE |
| `0x24` | `0x60` | u16 | 2, LE |
| `0x18`/`0x34` | `0x54`/`0x70` | i32 | 4, LE |
| `0x1c` | `0x58` | u32 | 4, LE |
| `0x28` | `0x64` | u32 (memcpy) | 4, LE |
| `0x2c` | `0x68` | 8-bit string | 2 + n |
| `0x30`/`0x3c` | `0x6c`/`0x78` | wide string | 2 + 2n |
| `0x40` | `0x7c` | SimKin value | variable |
| `0x44` | `0x80` | i32 triple | 12 |

Two details worth keeping:

- **An i32 is written as two i16 halves**, low first (`FUN_1008beec`
  calls the i16 writer twice), while the read side assembles four bytes
  directly. On a little-endian target those agree exactly, which is why
  the game never noticed.
- **A string's length prefix is an i16 and the NUL is not written.**
  `FUN_1008bd54` writes `strlen(s)` and that many bytes; the reader
  appends its own terminator. That is the **opposite** of the container's
  TOC, whose name length *includes* the NUL. The two layers disagree, and
  both are reproduced as they are.

Slot `0x40`/`0x7c` serializes a **SimKin `skRValue`**: a type-tag byte,
then the value — tag 1 a wide string, 2 an i32 (`AtomToInt`), 4 a
character byte (`SIMKIN_ord89`), 5 a boolean byte (`SIMKIN_ord85`), 0
nothing. Tag 3 (float) and 6 (object) have no branch at all, which is
consistent with a fixed-point engine.

### The chain

Thirty-two vtables carry a `+0x130`/`+0x134` pair. Between them they name
thirteen distinct save functions, and those thirteen form **one
inheritance chain**, each writing its own fields and then tail-calling
its base's — so the wire order is most-derived first:

```
Entity              FUN_10066614 / FUN_10066cc4   (5 vtables)
 +- Drawable        FUN_10067db0 / FUN_10067ca0   (7)
     +- Item        FUN_1006d35c / FUN_1006d2ac   (1)
     |   +- Stackable   FUN_1002ed3c / FUN_1002ed04  (4)
     |   |   +- Wearable    FUN_10047584 / FUN_1004754c  (2)
     |   +- Weapon         FUN_1002eaa0 / FUN_1002ea0c  (2)
     +- Spellbook   FUN_10028a60 / FUN_10028be8   (2)
         +- InventoryHolder  FUN_10005164 / FUN_10005320  (2)
             +- Actor        FUN_10086514 / FUN_10086454  (3)
             +- LinkedActor  FUN_10006f90 / FUN_10006ee4  (1)
             +- Character    FUN_1001e754 / FUN_1001ea54  (1)
                 +- Player   FUN_10043308 / FUN_10043bb0  (1)
```

plus one non-virtual leaf, the **stats block** (`FUN_1004a284` /
`FUN_1004a6f4`), which Actor and Player each call on their own block
(`actor+0x224` / `player+0x3ac`).

**Nothing is version-tagged.** No magic, no field count, no per-record
length: a record is exactly as long as the class that wrote it, and a
reader that picks the wrong class desynchronises everything after it,
silently. Which class to use comes from the entity's **typeId**, written
immediately before each record by whoever owns it — an i16 in the level
file, an i32 inside an inventory holder — and resolved through
`entities.txt`.

**Fields are written twice.** `Drawable` re-writes seven fields
(`+0x6c`, `+0x6d`, `+0x70`..`+0x80`) that `Entity` also writes, and
`Actor` re-writes `+0x1e2` and `+0xd8`. Both copies are genuinely on the
wire, in both orders, and the loader reads both.

### `character.dat`

One Player record. `FUN_10018e70` writes it through the player's own
`+0x130`; `FUN_100195b0` reads it back through `+0x134`.

The head, before the stats block and the base chain:

| field | offset | what |
|---|---|---|
| wide string | `+0xfcc` | the character's name (`SetPlayerName`) |
| 256 × 3 bytes | `+0x430`/`+0x530`/`+0x630` | **the quest arrays**, interleaved one index at a time |
| 256 bytes | `+0xa30` | **the per-type kill counter** |
| i32 | `+0xf44` | level-up points |

The three quest arrays are named by the Player/GameState dispatcher's own
bindings (`FUN_1003f130`, cases `0x4c`..`0x53`), each a two-instruction
accessor whose `ADD` immediate gives the array away:

| array | binding pair |
|---|---|
| `+0x430` | `SetQuestAssigned` / `QuestAssigned` |
| `+0x530` | `SetQuestCompleted` / `QuestCompleted` |
| `+0x630` | `SetQuestSolved` / `QuestSolved` |
| `+0xa30` | `AddMonsterKilled` / `MonstersKilled` — a bare `+= 1` with no clamp, so it wraps at 256 |

Then the tail, after the base chain — the character-creation block, all
named the same way:

| field | offset | binding |
|---|---|---|
| 2 × 5 i16 | `+0xf4c` / `+0xf68` | the two **hand queues**, by item id; `-1` for an empty slot |
| 2 × i16 | — | which slot of each the stats block's `+0x48`/`+0x4c` point at, as an *index into the list above*, `-1` if none |
| u8 | `+0xf35` | `HasCreatedCharacter` |
| u8 | `+0xf38` | `ChooseCharacter` / `GetCharacter` — the class |
| u8 | `+0xf3c` | `ChooseRace` / `GetRace` |
| u16 | `+0xf40` | `GetPortraitID` / `SetPortraitID` |
| u8 | `+0xfac` | `SetSex` |
| i32 | `+0xfb0` | `Get`/`SetSpecialAbility` |
| i32 | `+0xfb4` | `Get`/`SetRaceAbility` |
| u16 | `+0xfc2` | `Get`/`SetManaReduction` |
| i16, u16 | `+0xfb8`, `+0xfba` | derived by `UpdateAttributes` (`FUN_1001fc24`); no binding, no name |
| 18 × i32 | `+0xfd8`..`+0x101c` | **the global story flags**, below |
| 8 × u16 | `+0xf8c` | the equipment slots, by typeId (0 = empty) |

Note that **class and race are 32-bit in memory but one byte each on
disk** — `ChooseCharacter`/`ChooseRace` store a full word, the save slot
for both is the u8 writer. With eight classes it never matters.

### The eighteen global story flags

`+0xfd8`..`+0x101c` is not an anonymous block. `FUN_1003e7a4` and
`FUN_1003ebd8` are the player object's own `getValue`/`setValue`, and
they match an incoming field name against eighteen hardcoded wide
strings, one per int, in this order:

```
saved_Guild       saved_NA_Crystal  saved_Business    saved_Birgidda
saved_Skelos      saved_Rescue1     saved_Rescue2     saved_Rescue3
saved_Rescue4     saved_Heather     saved_Dstar_Pass  saved_GoAway
saved_Goblin      saved_StarTooth   saved_Makor       saved_SkelosDead
saved_KethStatus  saved_EndGame
```

**Seventeen of the eighteen appear in the shipped scripts as
`GetPlayer().saved_X`** — `GetPlayer().saved_Guild = 3`,
`if(GetPlayer().saved_EndGame = 1)` — and the eighteenth,
`saved_NA_Crystal`, appears nowhere at all: a cut flag the format still
carries.

This is the architectural point the whole save format turns on. The
corpus has **309 files** using some `saved_*` name and hundreds of
distinct names — `Level.saved_Open` alone appears 134 times. Those are
ordinary SimKin instance variables; they ride along in *their own
entity's* record (below) and are scoped to that level. Only these
eighteen live on the player, and therefore in `character.dat`, and
therefore survive a zone change. `saved_Birgidda` here versus the
separate `Level.saved_Birgitta` the scripts set beside it is the clearest
illustration: two different stores, two different spellings, both live.

### Script state is persisted as text

The entity root's last act is to walk its script object's attribute list
and write each name/value pair as **two wide strings**, preceded by a `1`
tag, ending the list with `0xff` — but only for pairs whose *value* text
contains no `{`, i.e. skipping anything that looks like a nested
structure or a method body.

That is how a door remembers it was unlocked. The per-level tile-change
journal at `level+0xfc`/`+0x100` (M44) is **not serialized anywhere** —
`<level>.dat` contains no tile data at all. Instead the level's `.zmp` is
reloaded clean, each saved entity is recreated with its variables already
restored, and `FUN_100188dc` then calls every one of them by name:
`Init`. A door script whose `saved_Open` came back as 1 re-applies its
own `UnlockZone` from inside `Init`, and the tile bytes follow.

### `<level>.dat`

`FUN_100187d0` / `FUN_100188dc`, and much smaller than it sounds:

```c
repeat {
    u8  1;              // 0 ends the list
    i16 typeId;
    <that entity's own record, via its vtable +0x130>
}
u8 0;
u8 engine->+0xbe0e;
```

The writer skips the player (`engine+0x618`) and asks every other entity
in `engine+0x634` its own `vtable+0x104` — "are you saveable at all" —
before writing it.

The single trailing byte, `engine+0xbe0e`, is the **"this level is ready
to render" gate**: `GameEngine`'s constructor sets it to 1,
`GameEngine_InitLevel` clears and re-sets it around a level load, and
`Render3DScene` refuses to draw the 3D view while it is 0.

### The stats block, fully named

70 bytes, and **not in field order**: `+0x00`..`+0x0e`, then
`+0x14`..`+0x2e`, then the 32-bit `experience`/`level`/`gold` group, and
only *then* doubling back for `+0x10`/`+0x12`. M43's table named most of
these; this pass added four more from the same two-switch
cross-reference:

| offset | field | named by |
|---|---|---|
| `+0x10` | **strength bonus** | `GetStrengthBonus` (index 9) |
| `+0x12` | **health bonus** | `SetHealthBonus` / `ModHealthBonus` (no stat index) |
| `+0x20` | **personality** | `GetPersonality` (index 0xf) |
| `+0x22` | **luck** | `GetLuck` (index 0x10) |

The tail is `+0x44` (the effect-flag bitfield) and four halfwords, in the
order `+0x70`, `+0x78`, `+0x72`, `+0x76`. The loader zeroes
`+0x70`..`+0x7a` and `+0x7c` first and then fills only those four, so
three live fields are **not** restored: the two damage-over-time
accumulators (harmless — they re-fill) and **`+0x7c`, the second
periodic channel's *kind***, which `SetSpellEffect` writes. That third
one is not harmless: a regeneration or drain effect keeps its timer
(`+0x78`) across a save but loses the kind that made it do anything, so
it comes back ticking down inert. There is nowhere on the wire to put it.

### What the writer does around all this

`FUN_10018e70`'s disk-space estimate (M40, step 2) is now legible: it
serializes the player once to measure it, resets the stream, serializes
the level state to measure that, and requires
`free + existingFileSize - 512000 >= 3 * playerBytes + 2 * levelBytes`.

And `FUN_1001e754`'s first field — an 8-bit string written from
`engine[0x28]+0x28`, next to the debug string *"Saving %s as our current
level"* — is **the level name, not the character's**. It is the first
thing in `character.dat` and it is how a load knows what to load:
`FUN_1001ea54` `strcpy`s it straight back into the engine. (A
multiplayer session writes `player+0x1078` instead, next to *"Saving %s
as original level"*.)

## What is still open

**Nothing about the two shipped members' layout.** Both are decoded
field by field with their save/load pairs agreeing, and implemented in
[`port/src/assets/save_stream.h`](../port/src/assets/save_stream.h) and
[`save_records.h`](../port/src/assets/save_records.h).

What remains is smaller and mostly semantic:

- **Roughly forty unidentified scalars** across the Entity, Drawable and
  InventoryHolder layers. Their widths and positions are pinned by the
  save/load pair; what they *mean* is not, and nothing in this port
  needs them yet. They round-trip verbatim.
- **`levelinfo.txt`, `dragonstar.cfg` and `dragonstar.set`.** The first
  is two decimal integers (`"%d\n%d\n"`, opened `rt`/`wt`); the other two
  are whatever `SaveConfig` writes, which has not been read.
- **Still no real save file to check against.** Saves are created at
  runtime on the device and none ships on the install image, so the
  verification is the same shape as M40's: every field asserted against
  the *decompiled writer*, and the writer cross-checked against its own
  loader — which is a genuinely independent second source, since the two
  are separate functions that must agree byte for byte. Where the format
  names things the shipped data also names (the eighteen story flags, the
  quest arrays, the stats block), the two are checked against each other.

The container itself lives in
[`port/src/assets/save_archive.h`](../port/src/assets/save_archive.h), the
stream and the records beside it in
[`save_stream.h`](../port/src/assets/save_stream.h) and
[`save_records.h`](../port/src/assets/save_records.h), and the port's own
save slots are real files on disk (`port/src/simkin_bindings/menu_stack.cpp`,
`player_save.cpp`). Smoke tests: `m40_save_archive_smoke.cpp` (the
container) and `m50_save_records_smoke.cpp` (the members).
