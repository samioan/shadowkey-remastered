#pragma once

// Loader for the game's localized string table (docs/INPUT_HANDLING.md,
// verified byte-for-byte against a real stringtable.eng, 4082 entries).
// Every menu item/label textId (mainmenu.s's AddMenuItem(716, ...) etc.)
// is an index into this table -- see tools/parse_string_table.py for the
// reference implementation this mirrors.
//
//   u32 count
//   repeat count times: u32 charCount; charCount x uint16 (UTF-16LE,
//   charCount includes a trailing 0x0000)

#include <string>
#include <vector>

namespace sk {

class StringTable {
public:
    // Loads from a real stringtable.* file (e.g. ".../6r51/stringtable.eng").
    // Returns false (logged) on any read/format failure.
    bool Load(const std::string& path);

    // Returns the string at `id`, folded from UTF-16LE to ASCII (non-ASCII
    // codepoints become '?' -- the placeholder bitmap font is ASCII-only
    // anyway, see graphics/bitmap_font.h). Out-of-range ids return "?".
    std::string Get(int id) const;

    size_t count() const { return entries_.size(); }

private:
    std::vector<std::string> entries_;
};

}  // namespace sk
