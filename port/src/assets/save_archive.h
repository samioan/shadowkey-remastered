#pragma once

// M40: the real on-disk save container.
//
// Until now the roadmap read "no on-disk save format has been RE'd yet"
// and M5's four save slots were simulated in memory. The container is now
// decoded, from one small class in the binary (constructor 0x10009b2c,
// 0x1924 bytes) that every save-shaped file in the game goes through:
//
//   c:\system\apps\6R51\game00.sav .. game03.sav   the four save slots
//   c:\system\apps\6R51\current.sav                the in-progress game
//   character.dat                                  the player's own record
//   <level>.dat                                    per-level state
//                                                  ("Using %s to save our
//                                                   previous level")
//
// It is a **named-blob archive**, not a flat struct dump -- a table of
// contents of up to 256 entries, each a name plus an (offset, length) pair
// into the same file:
//
//   u32 fileSize        // patched on close by seeking back to 0 and
//                       // writing the real size; a mismatch against the
//                       // actual size is exactly how the game detects a
//                       // truncated save and resets it (the "SaveCorrupted"
//                       // string sits next to this code)
//   u32 recordCount     // <= 256, the class's fixed table size
//   repeat recordCount:
//       u16 nameLength  // strlen(name) + 1 -- the NUL is on disk too
//       u8  name[nameLength]
//       u32 dataOffset  // see kDataOffsetBias below
//       u32 dataLength
//   ... payload blobs, at their own offsets ...
//
// Sources, all decompiled this pass:
//   FUN_10009bc4  open-for-read: reads fileSize, compares it against the
//                 real size, reads recordCount, then the TOC loop above
//   FUN_10009f2c  create-for-write: writes the 4-byte size placeholder
//   FUN_1000a09c  writes the TOC (count, then name/offset/length per entry)
//   FUN_1000b2bc  close: seeks to 0 and patches the real size
//   FUN_1000b100  lookup by name -- **case-insensitive** (strcasecmp)
//   FUN_1000b378  read a named blob: reads `dataOffset + 4` for
//                 `dataLength` bytes
//   FUN_1000b49c  delete a save file by name
//
// **Not decoded**: what is *inside* each blob. That is the actual
// serialization of player stats, inventory, quest flags and level state,
// and it is a separate (larger) job -- this is the envelope, not the
// letter. Nor is any of it verified against a real file: save files are
// created at runtime on the device and none ships on the install image, so
// the only available check is that a reader transcribed from the game's
// own read path accepts what a writer transcribed from its own write path
// produces, which is what save_archive_smoke does.

#include <cstdint>
#include <string>
#include <vector>

namespace sk {

class SaveArchive {
public:
    struct Record {
        std::string name;
        std::vector<uint8_t> data;
    };

    // The real read path fetches a blob at `dataOffset + 4`
    // (FUN_1000b378), i.e. the stored offset is relative to just past the
    // leading fileSize field rather than to the start of the file.
    // Reproduced rather than normalised, because it is what a real file
    // written by the device would contain.
    static constexpr uint32_t kDataOffsetBias = 4;

    // The class's own table is a fixed 256 entries (its constructor loops
    // 0x100 times), so a longer archive is not representable.
    static constexpr size_t kMaxRecords = 256;

    // Parses `bytes` as the container above. Returns false (and leaves
    // `out` untouched) on anything the real open path would reject: a
    // fileSize that disagrees with the actual size, a count past the
    // table, or a truncated TOC/blob. The real code's response to those is
    // to wipe and restart the archive, which is the caller's choice here.
    static bool Parse(const std::vector<uint8_t>& bytes, std::vector<Record>& out);

    // Produces bytes the reader above (and the game's own) accepts:
    // placeholder size, count, TOC, blobs, then the size patched in.
    static std::vector<uint8_t> Serialize(const std::vector<Record>& records);

    // FUN_1000b100's lookup, including its case-insensitivity -- returns
    // nullptr when absent.
    static const Record* Find(const std::vector<Record>& records, const std::string& name);
};

}  // namespace sk
