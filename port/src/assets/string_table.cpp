#include "assets/string_table.h"

#include <cstdint>
#include <cstdio>
#include <fstream>

namespace sk {

namespace {

// M99: Latin-1, not ASCII. The four other languages SetLanguage can load are
// full of accented letters (German's "Nächste Seite", French's "Français"),
// and every one of them is below U+0100. The text renderer already treats
// each byte as a codepoint into the real N-Gage font (bitmap_font.cpp), which
// carries the Latin-1 block, so keeping the byte is all it takes to draw them.
std::string Utf16LeToLatin1(const std::vector<uint16_t>& units) {
    std::string out;
    out.reserve(units.size());
    for (uint16_t u : units) {
        if (u == 0) break;  // trailing NUL
        out.push_back(u < 0x100 ? static_cast<char>(static_cast<unsigned char>(u)) : '?');
    }
    return out;
}

}  // namespace

bool StringTable::Load(const std::string& path) {
    entries_.clear();
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::printf("StringTable: could not open %s\n", path.c_str());
        return false;
    }

    uint32_t count = 0;
    f.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (!f) {
        std::printf("StringTable: %s too short for header\n", path.c_str());
        return false;
    }

    entries_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t charCount = 0;
        f.read(reinterpret_cast<char*>(&charCount), sizeof(charCount));
        if (!f) {
            std::printf("StringTable: %s truncated at entry %u\n", path.c_str(), i);
            return false;
        }
        std::vector<uint16_t> units(charCount);
        if (charCount > 0) {
            f.read(reinterpret_cast<char*>(units.data()),
                   static_cast<std::streamsize>(charCount) * 2);
            if (!f) {
                std::printf("StringTable: %s truncated in entry %u's text\n", path.c_str(), i);
                return false;
            }
        }
        entries_.push_back(Utf16LeToLatin1(units));
    }
    return true;
}

std::string StringTable::Get(int id) const {
    if (id < 0 || static_cast<size_t>(id) >= entries_.size()) return "?";
    return entries_[static_cast<size_t>(id)];
}

}  // namespace sk
