# The save system

Recovered this pass. The roadmap's long-standing "no on-disk save format
has been RE'd yet" is now narrower: the **container** is fully decoded and
implemented, and what remains is the serialization *inside* each of its
members.

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

## What is still open

**The member payloads.** This decode is the envelope, not the letter:
nothing here says how a player's stats, inventory, quest flags or a level's
entity state are laid out inside `character.dat` or a `<level>.dat` blob.
That is the larger remaining job.

**No real file to check against.** Saves are created at runtime on the
device and none ships on the install image, so the container decode has no
external verification. What `port/src/tests/m40_save_archive_smoke.cpp`
does instead is assert the encoding **field by field against the
decompiled writer** — sizes, the NUL-inclusive name length, the `+4`
offset bias — plus the two corruption cases the real open path checks,
rather than only round-tripping a reader against its own writer.

The container itself lives in
[`port/src/assets/save_archive.h`](../port/src/assets/save_archive.h).
