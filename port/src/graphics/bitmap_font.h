#pragma once

// Placeholder bitmap font -- confirmed (docs/GRAPHICS_FORMAT.md's "The
// real in-game font" section) to be a *permanent* stand-in, not a gap
// pending RE: the real UI text draw path calls genuine Symbian EIKON/
// GDI APIs (CEikonEnv::LegendFont(), or a TFontSpec for the stock
// "Swiss" family) -- the real glyphs are the Nokia N-Gage's own system
// font, living in the device ROM, not any file this game ships. There's
// nothing left to extract from this project's assets. Upper-case-only
// 5x7 block glyphs so menu screens show real string-table text (see
// assets/string_table.h) instead of placeholder strings.

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
