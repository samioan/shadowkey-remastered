#include "assets/save_archive.h"

#include <cctype>
#include <cstring>

namespace sk {
namespace {

bool ReadU16(const std::vector<uint8_t>& b, size_t& pos, uint32_t& out) {
    if (pos + 2 > b.size()) return false;
    out = static_cast<uint32_t>(b[pos]) | (static_cast<uint32_t>(b[pos + 1]) << 8);
    pos += 2;
    return true;
}

bool ReadU32(const std::vector<uint8_t>& b, size_t& pos, uint32_t& out) {
    if (pos + 4 > b.size()) return false;
    out = static_cast<uint32_t>(b[pos]) | (static_cast<uint32_t>(b[pos + 1]) << 8) |
          (static_cast<uint32_t>(b[pos + 2]) << 16) | (static_cast<uint32_t>(b[pos + 3]) << 24);
    pos += 4;
    return true;
}

void AppendU16(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xff));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
}

void AppendU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xff));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
}

void PatchU32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    b[at + 0] = static_cast<uint8_t>(v & 0xff);
    b[at + 1] = static_cast<uint8_t>((v >> 8) & 0xff);
    b[at + 2] = static_cast<uint8_t>((v >> 16) & 0xff);
    b[at + 3] = static_cast<uint8_t>((v >> 24) & 0xff);
}

}  // namespace

bool SaveArchive::Parse(const std::vector<uint8_t>& bytes, std::vector<Record>& out) {
    size_t pos = 0;
    uint32_t declaredSize = 0;
    if (!ReadU32(bytes, pos, declaredSize)) return false;
    // FUN_10009bc4's own first check: `if (local_540 != local_53c)` --
    // the declared size against the file's real size. This is the
    // corruption test, and it is the whole reason the size is patched in
    // on close rather than written up front.
    if (declaredSize != bytes.size()) return false;
    // `if (local_540 < 5) return` -- a file with only the size field in it
    // is a legitimately empty archive, not an error.
    if (declaredSize < 5) {
        out.clear();
        return true;
    }

    uint32_t count = 0;
    if (!ReadU32(bytes, pos, count)) return false;
    // `if (local_54c < 1) return` -- likewise legitimate.
    if (count == 0) {
        out.clear();
        return true;
    }
    if (count > kMaxRecords) return false;

    struct Entry {
        std::string name;
        uint32_t offset = 0;
        uint32_t length = 0;
    };
    std::vector<Entry> entries;
    entries.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t nameLen = 0;
        if (!ReadU16(bytes, pos, nameLen)) return false;
        if (nameLen == 0 || pos + nameLen > bytes.size()) return false;
        // The stored length includes the terminating NUL (the writer uses
        // `strlen(name) + 1`), so the name itself is one shorter.
        Entry e;
        e.name.assign(reinterpret_cast<const char*>(&bytes[pos]), nameLen - 1);
        pos += nameLen;
        if (!ReadU32(bytes, pos, e.offset)) return false;
        if (!ReadU32(bytes, pos, e.length)) return false;
        entries.push_back(std::move(e));
    }

    std::vector<Record> parsed;
    parsed.reserve(entries.size());
    for (const Entry& e : entries) {
        const uint64_t start = static_cast<uint64_t>(e.offset) + kDataOffsetBias;
        if (start + e.length > bytes.size()) return false;
        Record r;
        r.name = e.name;
        r.data.assign(bytes.begin() + static_cast<size_t>(start),
                       bytes.begin() + static_cast<size_t>(start + e.length));
        parsed.push_back(std::move(r));
    }
    out.swap(parsed);
    return true;
}

std::vector<uint8_t> SaveArchive::Serialize(const std::vector<Record>& records) {
    std::vector<uint8_t> out;
    // FUN_10009f2c writes a 4-byte placeholder first; FUN_1000b2bc
    // overwrites it with the real size on close.
    AppendU32(out, 0);
    AppendU32(out, static_cast<uint32_t>(records.size()));

    // The TOC is fixed-width per entry only once the names are known, so
    // its size has to be computed before the blobs can be placed.
    size_t tocBytes = 0;
    for (const Record& r : records) {
        tocBytes += 2 + (r.name.size() + 1) + 4 + 4;
    }
    uint32_t cursor = static_cast<uint32_t>(4 + 4 + tocBytes);

    std::vector<uint32_t> offsets;
    offsets.reserve(records.size());
    for (const Record& r : records) {
        offsets.push_back(cursor - kDataOffsetBias);  // see kDataOffsetBias
        cursor += static_cast<uint32_t>(r.data.size());
    }

    for (size_t i = 0; i < records.size(); ++i) {
        const Record& r = records[i];
        AppendU16(out, static_cast<uint32_t>(r.name.size() + 1));
        out.insert(out.end(), r.name.begin(), r.name.end());
        out.push_back(0);  // the NUL is on disk
        AppendU32(out, offsets[i]);
        AppendU32(out, static_cast<uint32_t>(r.data.size()));
    }
    for (const Record& r : records) {
        out.insert(out.end(), r.data.begin(), r.data.end());
    }

    PatchU32(out, 0, static_cast<uint32_t>(out.size()));
    return out;
}

const SaveArchive::Record* SaveArchive::Find(const std::vector<Record>& records,
                                              const std::string& name) {
    for (const Record& r : records) {
        if (r.name.size() != name.size()) continue;
        bool same = true;
        for (size_t i = 0; i < name.size(); ++i) {
            // strcasecmp, which is what FUN_1000b100 uses -- a save
            // written as "Character" is found by "character".
            if (std::tolower(static_cast<unsigned char>(r.name[i])) !=
                std::tolower(static_cast<unsigned char>(name[i]))) {
                same = false;
                break;
            }
        }
        if (same) return &r;
    }
    return nullptr;
}

}  // namespace sk
