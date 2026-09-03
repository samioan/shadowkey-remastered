// M40 smoke test: the real on-disk save container.
//
// See assets/save_archive.h for the decode and its sources. Nothing here
// can be checked against a real save file -- none ships on the install
// image, because saves are created at runtime on the device -- so the
// checks are of two kinds instead:
//
//   * **byte-layout** assertions against the decompiled writer, field by
//     field, so the encoding is pinned rather than merely round-tripping
//     with itself, and
//   * the corruption tests the game's own open path performs.
#include <cstdio>
#include <string>
#include <vector>

#include "assets/save_archive.h"

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%-74s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    if (!ok) ++g_failures;
}

uint32_t U32At(const std::vector<uint8_t>& b, size_t i) {
    return static_cast<uint32_t>(b[i]) | (static_cast<uint32_t>(b[i + 1]) << 8) |
           (static_cast<uint32_t>(b[i + 2]) << 16) | (static_cast<uint32_t>(b[i + 3]) << 24);
}

uint32_t U16At(const std::vector<uint8_t>& b, size_t i) {
    return static_cast<uint32_t>(b[i]) | (static_cast<uint32_t>(b[i + 1]) << 8);
}

}  // namespace

int main() {
    using sk::SaveArchive;

    std::printf("=== M40: the real save container ===\n\n");

    // The real file set this container is used for -- the names come
    // straight out of the binary's own format strings.
    std::vector<SaveArchive::Record> records;
    records.push_back({"character.dat", {1, 2, 3, 4, 5}});
    records.push_back({"azra.dat", {0xaa, 0xbb}});
    records.push_back({"crypt1.dat", {}});

    std::vector<uint8_t> bytes = SaveArchive::Serialize(records);

    // ---- byte layout, against the decompiled writer ----
    Check(U32At(bytes, 0) == bytes.size(),
          "the leading u32 is the real file size (patched on close, FUN_1000b2bc)");
    Check(U32At(bytes, 4) == 3, "the second u32 is the record count (FUN_1000a09c)");
    // First TOC entry: u16 strlen+1, then the name *including* its NUL.
    Check(U16At(bytes, 8) == std::string("character.dat").size() + 1,
          "a TOC name length counts the terminating NUL, exactly as the writer does");
    Check(std::string(reinterpret_cast<const char*>(&bytes[10]), 13) == "character.dat" &&
              bytes[10 + 13] == 0,
          "...and the name is written NUL-terminated on disk");
    {
        const size_t offsetField = 10 + 14;
        uint32_t storedOffset = U32At(bytes, offsetField);
        uint32_t storedLength = U32At(bytes, offsetField + 4);
        Check(storedLength == 5, "the entry's second u32 is its blob length");
        // FUN_1000b378 reads at dataOffset + 4, so the stored value is
        // biased by the leading size field.
        Check(bytes[storedOffset + SaveArchive::kDataOffsetBias] == 1,
              "the entry's first u32 is a blob offset biased by 4, as the real reader assumes");
    }

    // ---- round trip ----
    std::vector<SaveArchive::Record> parsed;
    Check(SaveArchive::Parse(bytes, parsed), "a written archive parses back");
    Check(parsed.size() == 3, "...with every record present");
    if (parsed.size() == 3) {
        Check(parsed[0].name == "character.dat" && parsed[0].data.size() == 5 &&
                  parsed[0].data[4] == 5,
              "...names and payloads survive intact");
        Check(parsed[2].name == "crypt1.dat" && parsed[2].data.empty(),
              "...including a zero-length record, which a per-level .dat can legitimately be");
    }

    // ---- the lookup is case-insensitive (strcasecmp) ----
    Check(SaveArchive::Find(parsed, "CHARACTER.DAT") != nullptr &&
              SaveArchive::Find(parsed, "character.dat") != nullptr &&
              SaveArchive::Find(parsed, "nosuch.dat") == nullptr,
          "lookup by name is case-insensitive, matching FUN_1000b100's strcasecmp");

    // ---- the corruption tests the game's own open path performs ----
    {
        std::vector<uint8_t> truncated = bytes;
        truncated.pop_back();
        std::vector<SaveArchive::Record> dummy;
        Check(!SaveArchive::Parse(truncated, dummy),
              "a truncated file is rejected -- the size field no longer matches the real size");

        std::vector<uint8_t> tooManyRecords = bytes;
        tooManyRecords[4] = 0x01;
        tooManyRecords[5] = 0x01;  // 257, one past the class's fixed table
        // Fix the size field so the *only* thing wrong is the count.
        Check(!SaveArchive::Parse(tooManyRecords, dummy),
              "a count past the class's fixed 256-entry table is rejected");

        std::vector<uint8_t> empty = {4, 0, 0, 0};
        std::vector<SaveArchive::Record> none;
        Check(SaveArchive::Parse(empty, none) && none.empty(),
              "a bare 4-byte file is a legitimately empty archive, not a corrupt one");
    }

    std::printf("\nm40_save_archive_smoke: %s\n", g_failures ? "FAILED" : "PASSED (all checks)");
    return g_failures ? 1 : 0;
}
