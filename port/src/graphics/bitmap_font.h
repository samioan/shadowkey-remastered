#pragma once

// The real N-Gage UI font, plus a placeholder fallback.
//
// docs/GRAPHICS_FORMAT.md's "The real in-game font" section traced the
// real UI text draw path to genuine Symbian EIKON/GDI APIs
// (CEikonEnv::LegendFont() for ordinary menu text) -- pointing at the
// N-Gage's system ROM font. That ROM font (Ceurope.gdr, assets/
// gdr_font.h/.cpp, typeface "LatinBold12") is what `LoadRealFont()`
// loads by default and DrawString actually draws with -- confirmed
// pixel-for-pixel against a real screenshot (downscaled back to native
// 176x208 first; compared against its *upscaled* form, this same font
// wrongly looked like a completely different, rounder face -- video
// compression blur on an ~11px bitmap font, not a real difference; see
// bitmap_font.cpp's `LoadRealFont` comment for the full story).
// `LoadRealFont()` also accepts a `.ttf` path (Win32 GDI rendering,
// `TtfFont` in the .cpp) -- real, working, just not the default.
//
// The blocky upper-case-only 5x7 glyphs below remain as the fallback
// for whenever the real font can't be loaded (file missing) or a
// codepoint it doesn't cover is drawn -- this is what every screen
// rendered with before the real font was found, and is safe as the
// non-fatal-missing-asset default this port uses everywhere else.

#include <cstdint>
#include <string>
#include <string_view>

#include "graphics/backbuffer.h"

namespace sk {

class BitmapFont {
public:
    static constexpr int kGlyphWidth = 5;
    // Matches the real font's cell height (LatinPlain12, see
    // gdr_font.cpp) so every call site's line-height math already
    // works for the common case; only widens the placeholder's actual
    // 7px glyphs by a few px of harmless extra line gap when the real
    // font isn't loaded.
    static constexpr int kGlyphHeight = 12;
    static constexpr int kAdvance = kGlyphWidth + 1;  // 1px letter-spacing (placeholder only)

    // Parses `path` as a Symbian .gdr font store and selects the
    // typeface named `typefaceName` (e.g. "LatinPlain12") for
    // DrawString to use from now on. Non-fatal: on any failure (missing
    // file, unrecognised format, no such typeface) DrawString keeps
    // using the placeholder glyphs, exactly as if this were never
    // called.
    // M111: the two faces the game actually asks for. `FUN_10022b20`
    // gates on the widget's `SetFontNum`: anything but 1 takes
    // `EIKCORE::LegendFont()` (Ui below), and 1 builds a
    // `TFontSpec("Swiss", 0xd5)` with the caller's stroke weight and
    // posture, which for every shipped site is non-bold and non-italic.
    //
    // No N-Gage ROM font store contains a typeface called "Swiss" --
    // Ceurope.gdr, Browsereur.gdr and CalcEur.gdr between them hold
    // LatinBold12/13/17/19, LatinPlain12, Acb14, Acb30, Acp5, Alpi12,
    // Albi12, Alp13, Alpi13, Albi13, alp17, Alb17b, albi17b, alpi17,
    // Aco13, Aco21 and Acalc21 -- so the device resolved it through
    // Symbian's nearest-font matching and the name contributed nothing.
    // The **style** then settles it on its own: Acb14 (13 glyphs),
    // Acb30 (13) and Acp5 (10) are digit-only clock/battery fonts rather
    // than text faces, which leaves five real Latin text faces, and
    // exactly one of them -- LatinPlain12 -- is not bold. Its 12px cell
    // is also the only candidate that fits the captions' own y=195 on a
    // 208px screen (LatinBold19 would overrun by six).
    enum class Face { Ui, Small };

    static bool LoadRealFont(const std::string& path, const std::string& typefaceName,
                             Face face = Face::Ui);

    // Draws text with (x,y) as the top-left corner of the first glyph's
    // cell. Lowercase is folded to upper only for the placeholder path
    // (the real font has real lowercase glyphs); unsupported characters
    // render as a blank cell rather than being skipped, so column
    // alignment holds when falling back.
    // Named DrawString, not DrawText -- <windows.h> #defines DrawText to
    // DrawTextW/A depending on UNICODE, and since not every translation
    // unit that sees this header also includes windows.h first, the two
    // would disagree on the mangled name and fail to link.
    static void DrawString(Backbuffer& bb, int x, int y, std::string_view text, uint16_t color,
                           Face face = Face::Ui);

    // Real measured pixel width of `text` in whichever font DrawString
    // would actually use for it (the real TTF's per-glyph advances when
    // loaded, else the placeholder's fixed pitch) -- for centering menu
    // item labels, which the real game does (a fixed per-character
    // advance guess would drift from what DrawString actually draws).
    static int TextWidth(std::string_view text, Face face = Face::Ui);
    // Cell height of a loaded face, or kGlyphHeight when it isn't loaded.
    static int LineHeight(Face face = Face::Ui);
};

}  // namespace sk
