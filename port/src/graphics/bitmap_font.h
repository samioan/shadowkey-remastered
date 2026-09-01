#pragma once

// Placeholder bitmap font -- NOT the game's real glyph format, which was
// never reverse-engineered (see the port scaffold plan's known-stubs
// list: RENDER_LOOP.md only names a debug-overlay font cluster in
// passing). Upper-case-only 5x7 block glyphs so menu screens show real
// string-table text (see assets/string_table.h) instead of placeholder
// strings, clearly marked as a stand-in pending that RE pass.

#include <cstdint>
#include <string_view>

#include "graphics/backbuffer.h"

namespace sk {

class BitmapFont {
public:
    static constexpr int kGlyphWidth = 5;
    static constexpr int kGlyphHeight = 7;
    static constexpr int kAdvance = kGlyphWidth + 1;  // 1px letter-spacing

    // Draws text with (x,y) as the top-left corner of the first glyph.
    // Lowercase is folded to upper; unsupported characters render as a
    // blank cell rather than being skipped, so column alignment holds.
    // Named DrawString, not DrawText -- <windows.h> #defines DrawText to
    // DrawTextW/A depending on UNICODE, and since not every translation
    // unit that sees this header also includes windows.h first, the two
    // would disagree on the mangled name and fail to link.
    static void DrawString(Backbuffer& bb, int x, int y, std::string_view text, uint16_t color);

    static int TextWidth(size_t charCount) { return static_cast<int>(charCount) * kAdvance; }
};

}  // namespace sk
