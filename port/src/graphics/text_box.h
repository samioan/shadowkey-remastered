#pragma once

// M131: `FUN_100231b4`, the engine's free-standing text box -- what draws
// everything that is not a menu widget: the use prompt, a vignette's
// caption, the death and "press a key" screens, the multiplayer notice.
//
//     FUN_100231b4(ctx, text, x, y, right, _, r, g, b,
//                  bold, italic, legendFont, align, wrapChars)
//
// * The font is `CEikonEnv::LegendFont()` when `legendFont` is set, else
//   `TFontSpec("Swiss", 0xd5)` with the given weight and posture -- the
//   second face of graphics/bitmap_font.h (see its Face comment for what
//   the device resolved "Swiss" to), or its italic companion
//   (`Face::SmallItalic`, M132) when the caller asks for the posture.
// * Each line is drawn into a 13-pixel-high box starting at (x, y) and
//   reaching `right`, with `align` 0 = left, 1 = centred, and the box
//   moves down 13 for the next.
// * Lines break on spaces by **character count**, not width: at a space
//   (or the end) more than `wrapChars` past the line's start, the line is
//   cut at the space *before* it.
//
// The break loop is transcribed, quirks and all -- a first word longer
// than the limit yields an empty first line, and the text's end always
// emits one more (usually empty) line.

#include <string>
#include <vector>

#include "graphics/backbuffer.h"
#include "graphics/bitmap_font.h"

namespace sk {

constexpr int kTextBoxLinePitch = 0xd;

inline std::vector<std::string> WrapTextBox(const std::string& text, int wrapChars) {
    std::vector<std::string> lines;
    const int length = static_cast<int>(text.size());
    int lastSpace = 0;  // uVar10
    int lineStart = 0;  // uVar11
    for (int i = 0; i <= length; ++i) {
        const bool end = i == length;
        const bool space = !end && text[static_cast<size_t>(i)] == ' ';
        int nextSpace = lastSpace;
        int nextStart = lineStart;
        if (end || space) {
            nextSpace = i;
            if (wrapChars < i - lineStart || end) {
                nextStart = lastSpace;
                if (end && i - lineStart <= wrapChars) nextStart = i;
                int from = lineStart;
                if (from < length && text[static_cast<size_t>(from)] == ' ') ++from;
                lines.push_back(nextStart > from
                                    ? text.substr(static_cast<size_t>(from),
                                                  static_cast<size_t>(nextStart - from))
                                    : std::string());
                if (end) {
                    int tail = nextStart;
                    if (tail < length && text[static_cast<size_t>(tail)] == ' ') ++tail;
                    lines.push_back(text.substr(static_cast<size_t>(tail)));
                }
            }
        }
        lastSpace = nextSpace;
        lineStart = nextStart;
    }
    return lines;
}

enum class TextBoxAlign { Left = 0, Centre = 1 };

// Returns the y below the last line drawn.
inline int DrawTextBox(Backbuffer& bb, const std::string& text, int x, int y, int right,
                       uint16_t color, TextBoxAlign align, int wrapChars,
                       BitmapFont::Face face = BitmapFont::Face::Small) {
    // `baseline = ascent + (13 - fontHeight) / 2`: the cell is centred in
    // the line box.
    const int inset = (kTextBoxLinePitch - BitmapFont::LineHeight(face)) / 2;
    for (const std::string& line : WrapTextBox(text, wrapChars)) {
        if (!line.empty()) {
            int lineX = x;
            if (align == TextBoxAlign::Centre) {
                lineX = x + ((right - x) - BitmapFont::TextWidth(line, face)) / 2;
            }
            BitmapFont::DrawString(bb, lineX, y + inset, line, color, face);
        }
        y += kTextBoxLinePitch;
    }
    return y;
}

}  // namespace sk
