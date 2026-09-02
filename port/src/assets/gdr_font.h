#pragma once

// Parser for Symbian OS's ".gdr" bitmap font format (Glyph Data
// Resource) -- the real font `docs/GRAPHICS_FORMAT.md`'s "real in-game
// font" section already traced the game's text-draw path to
// (CEikonEnv::LegendFont() / the "Swiss" TFontSpec, both Symbian OS
// system APIs -- the glyphs live in the N-Gage's own ROM, not any file
// this game ships. This is NOT that ROM's file format guessed at or
// decompiled from shadowkey itself -- .gdr is a real, independently
// documented-in-code Symbian OS format; the exact byte layout here is
// ported from EKA2L1 (the open-source Symbian/N-Gage emulator)'s own
// GPLv3 parser (src/emu/loader/{gdr.h,gdr.cpp}, src/emu/common/src/
// unicode.cpp for the embedded typeface-name compression), then
// independently verified against a real Nokia N-Gage QD (RH-29) ROM
// dump's Ceurope.gdr: this parser consumes the *entire* 31389-byte file
// with zero leftover bytes and recovers 8 real typefaces (LatinBold12/
// 13/17/19, LatinPlain12, three small digit-only fonts) with correctly
// shaped glyph bitmaps (checked by eye against ASCII-art renders).
//
// Format, in parse order:
//   header (9 fixed uint32 fields incl. 3 magic UIDs/checksum that
//     identify the file as a font store at all, then N copyright
//     strings)
//   -> font_bitmap_header[] (one per pixel-size/style variant: uid,
//     posture/stroke/proportional flags, cell height/ascent/max width,
//     then that variant's code_section_header[] -- contiguous Unicode
//     codepoint ranges, each an offset into that variant's own
//     bit-packed glyph blob)
//   -> typeface_header[] (human-readable name e.g. "LatinPlain12" +
//     style flags, each pointing at one font_bitmap by uid)
//   -> per font_bitmap, in the same order as its headers: a
//     character_metric[] table (ascent/height/left-bearing/advance/
//     right-adjust per distinct glyph shape -- many codepoints share
//     one entry, e.g. every blank cell) followed by, per code_section,
//     an offset table + a bit-packed blob: each character's bits start
//     with a 7- or 15-bit index into character_metric[] (flagged by the
//     first bit), then a run-length-coded 1bpp bitmap (a repeat-line
//     flag + 4-bit run count per encoded row).
// Text strings (copyright, typeface names) are compressed with a
// Unicode scheme (SCSU-like -- static/dynamic 128-code windows plus an
// explicit "quote raw UTF-16" escape) that must be *decoded*, not just
// measured, to find the real per-string byte length: the on-disk
// cardinality-prefixed length is only an upper-bound buffer size to
// decompress into, not the compressed size on disk.
//
// This differs from every other format in this repo: it's not
// shadowkey's own data, so there is deliberately no Ghidra/decompile
// angle here -- it's read the same way any Symbian OS `.gdr` would be
// read by anything else that opens one.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace sk {

struct GdrGlyph {
    int width = 0;
    int height = 0;
    int leftBearing = 0;   // pixels from pen position to the bitmap's left edge
    int advance = 0;       // pixels to move the pen after drawing this glyph
    int ascentAboveBaseline = 0;  // this glyph's own bitmap-top-to-baseline distance
    std::vector<uint8_t> bits;    // width*height, row-major, 1 = set pixel, 0 = clear
};

class GdrFont {
public:
    // Parses `path` and selects the font_bitmap belonging to the
    // typeface named `typefaceName` (exact match, e.g. "LatinPlain12").
    // Non-fatal on any failure (missing file, unrecognised format,
    // typeface not found) -- returns false, leaves the font empty so
    // callers can fall back to a stand-in.
    bool Load(const std::string& path, const std::string& typefaceName);

    bool IsLoaded() const { return loaded_; }
    int CellHeight() const { return cellHeight_; }
    int Ascent() const { return ascent_; }

    // Regression-test hook: the parser fully understands the format iff
    // these come back equal (see gdr_font.h's "consumed the entire
    // 31389-byte file with zero leftover bytes" verification note).
    size_t BytesConsumed() const { return bytesConsumed_; }
    size_t FileSize() const { return fileSize_; }

    // Returns nullptr if this font has no glyph for `code` (caller
    // should fall back to a placeholder glyph, not skip the character).
    const GdrGlyph* GetGlyph(char32_t code) const;

private:
    bool loaded_ = false;
    int cellHeight_ = 0;
    int ascent_ = 0;
    size_t bytesConsumed_ = 0;
    size_t fileSize_ = 0;
    std::map<char32_t, GdrGlyph> glyphs_;
};

}  // namespace sk
