#include "graphics/bitmap_font.h"

#include <cctype>
#include <cstring>

#include <windows.h>

#include "assets/gdr_font.h"

namespace sk {

namespace {

// M111: two faces, not one -- see bitmap_font.h's Face comment.
// FontSlot() is the storage (what LoadRealFont writes into); RealFont()
// is what drawing should use, which falls back to the Ui face when the
// Small one was never loaded. So a caller that never mentions a face,
// and a build with no Small font available, both behave exactly as
// before.
GdrFont& FontSlot(BitmapFont::Face face) {
    static GdrFont ui;
    static GdrFont small;
    return face == BitmapFont::Face::Small ? small : ui;
}

GdrFont& RealFont(BitmapFont::Face face = BitmapFont::Face::Ui) {
    GdrFont& slot = FontSlot(face);
    if (slot.IsLoaded()) return slot;
    return FontSlot(BitmapFont::Face::Ui);
}

// A TrueType path for DrawString, NOT currently used by default --
// GdrFont (Ceurope.gdr, the real N-Gage ROM font) turned out to be
// right after all; see this file's LoadRealFont for the full story of
// how a mid-session detour through this class (loading a freeware
// "Nokia Cellphone FC" TTF) got corrected. Kept working/available
// (rendered through Win32 GDI -- this port already links it for
// presentation, so this is genuine, exercised code, not dead weight)
// in case a real TrueType asset is ever actually the right answer for
// something else.
class TtfFont {
public:
    bool Load(const std::string& path, const std::string& familyName, int pixelHeight) {
        if (!AddFontResourceExA(path.c_str(), FR_PRIVATE, nullptr)) return false;

        std::wstring wfamily(familyName.begin(), familyName.end());
        LOGFONTW lf = {};
        lf.lfHeight = -pixelHeight;
        lf.lfWeight = FW_BOLD;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfOutPrecision = OUT_TT_PRECIS;
        lf.lfQuality = ANTIALIASED_QUALITY;  // force grayscale AA, not ClearType's
                                              // per-channel color fringing -- this
                                              // port blends by luminance below and
                                              // needs R==G==B out of GDI.
        wcsncpy_s(lf.lfFaceName, wfamily.c_str(), LF_FACESIZE - 1);

        font_ = CreateFontIndirectW(&lf);
        if (!font_) return false;

        HDC probe = CreateCompatibleDC(nullptr);
        HGDIOBJ old = SelectObject(probe, font_);
        TEXTMETRICW tm;
        GetTextMetricsW(probe, &tm);
        ascent_ = tm.tmAscent;
        SelectObject(probe, old);
        DeleteDC(probe);

        loaded_ = true;
        return true;
    }

    bool IsLoaded() const { return loaded_; }
    int Ascent() const { return ascent_; }

    int MeasureWidth(std::string_view text) const {
        if (!loaded_ || text.empty()) return 0;
        std::wstring wtext(text.begin(), text.end());
        HDC memDC = CreateCompatibleDC(nullptr);
        HGDIOBJ oldFont = SelectObject(memDC, font_);
        SIZE sz;
        GetTextExtentPoint32W(memDC, wtext.c_str(), static_cast<int>(wtext.size()), &sz);
        SelectObject(memDC, oldFont);
        DeleteDC(memDC);
        return sz.cx;
    }

    void DrawString(Backbuffer& bb, int x, int y, std::string_view text, uint16_t color) {
        if (!loaded_ || text.empty()) return;
        std::wstring wtext(text.begin(), text.end());  // stringtable text is plain ASCII

        HDC memDC = CreateCompatibleDC(nullptr);
        HGDIOBJ oldFont = SelectObject(memDC, font_);

        SIZE sz;
        GetTextExtentPoint32W(memDC, wtext.c_str(), static_cast<int>(wtext.size()), &sz);
        int w = sz.cx + 2, h = sz.cy + 2;

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;  // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bmp) {
            SelectObject(memDC, oldFont);
            DeleteDC(memDC);
            return;
        }
        HGDIOBJ oldBmp = SelectObject(memDC, bmp);

        RECT rc{0, 0, w, h};
        SetBkColor(memDC, RGB(0, 0, 0));
        SetTextColor(memDC, RGB(255, 255, 255));
        SetBkMode(memDC, OPAQUE);
        ExtTextOutW(memDC, 1, 1, ETO_OPAQUE, &rc, wtext.c_str(), static_cast<int>(wtext.size()),
                    nullptr);

        uint8_t colorR = static_cast<uint8_t>(((color >> 11) & 0x1F) * 255 / 31);
        uint8_t colorG = static_cast<uint8_t>(((color >> 5) & 0x3F) * 255 / 63);
        uint8_t colorB = static_cast<uint8_t>((color & 0x1F) * 255 / 31);

        const uint32_t* px = static_cast<const uint32_t*>(bits);
        for (int row = 0; row < h; ++row) {
            int dy = y + row;
            if (dy < 0 || dy >= Backbuffer::kHeight) continue;
            uint16_t* dstRow = bb.Row(dy);
            for (int col = 0; col < w; ++col) {
                int dx = x + col;
                if (dx < 0 || dx >= Backbuffer::kWidth) continue;
                // Grayscale AA (forced above) -> any channel is the coverage.
                uint8_t lum = static_cast<uint8_t>(px[static_cast<size_t>(row) * w + col] & 0xFF);
                if (lum == 0) continue;

                uint16_t existing = dstRow[dx];
                uint8_t exR = static_cast<uint8_t>(((existing >> 11) & 0x1F) * 255 / 31);
                uint8_t exG = static_cast<uint8_t>(((existing >> 5) & 0x3F) * 255 / 63);
                uint8_t exB = static_cast<uint8_t>((existing & 0x1F) * 255 / 31);

                uint8_t outR = static_cast<uint8_t>((colorR * lum + exR * (255 - lum)) / 255);
                uint8_t outG = static_cast<uint8_t>((colorG * lum + exG * (255 - lum)) / 255);
                uint8_t outB = static_cast<uint8_t>((colorB * lum + exB * (255 - lum)) / 255);
                dstRow[dx] = PackRGB565(outR, outG, outB);
            }
        }

        SelectObject(memDC, oldBmp);
        DeleteObject(bmp);
        SelectObject(memDC, oldFont);
        DeleteDC(memDC);
    }

private:
    bool loaded_ = false;
    HFONT font_ = nullptr;
    int ascent_ = 0;
};

TtfFont& RealTtfFont() {
    static TtfFont font;
    return font;
}

// Each glyph is 7 rows x 5 columns, '#' = lit, '.' = off, written as
// ASCII art so the shape is checkable by eye rather than by decoding a
// bitmask. Returns nullptr for unsupported characters.
const char* const* GlyphRows(char c) {
    static const char* const kSpace[7] = {".....", ".....", ".....", ".....",
                                           ".....", ".....", "....."};
    static const char* const kA[7] = {".###.", "#...#", "#...#", "#####",
                                       "#...#", "#...#", "#...#"};
    static const char* const kB[7] = {"####.", "#...#", "#...#", "####.",
                                       "#...#", "#...#", "####."};
    static const char* const kC[7] = {".####", "#....", "#....", "#....",
                                       "#....", "#....", ".####"};
    static const char* const kD[7] = {"####.", "#...#", "#...#", "#...#",
                                       "#...#", "#...#", "####."};
    static const char* const kE[7] = {"#####", "#....", "#....", "####.",
                                       "#....", "#....", "#####"};
    static const char* const kF[7] = {"#####", "#....", "#....", "####.",
                                       "#....", "#....", "#...."};
    static const char* const kG[7] = {".####", "#....", "#....", "#..##",
                                       "#...#", "#...#", ".####"};
    static const char* const kH[7] = {"#...#", "#...#", "#...#", "#####",
                                       "#...#", "#...#", "#...#"};
    static const char* const kI[7] = {"#####", "..#..", "..#..", "..#..",
                                       "..#..", "..#..", "#####"};
    static const char* const kJ[7] = {"..###", "...#.", "...#.", "...#.",
                                       "...#.", "#..#.", ".##.."};
    static const char* const kK[7] = {"#...#", "#..#.", "#.#..", "##...",
                                       "#.#..", "#..#.", "#...#"};
    static const char* const kL[7] = {"#....", "#....", "#....", "#....",
                                       "#....", "#....", "#####"};
    static const char* const kM[7] = {"#...#", "##.##", "#.#.#", "#.#.#",
                                       "#...#", "#...#", "#...#"};
    static const char* const kN[7] = {"#...#", "##..#", "#.#.#", "#.#.#",
                                       "#..##", "#...#", "#...#"};
    static const char* const kO[7] = {".###.", "#...#", "#...#", "#...#",
                                       "#...#", "#...#", ".###."};
    static const char* const kP[7] = {"####.", "#...#", "#...#", "####.",
                                       "#....", "#....", "#...."};
    static const char* const kQ[7] = {".###.", "#...#", "#...#", "#...#",
                                       "#.#.#", "#..#.", ".##.#"};
    static const char* const kR[7] = {"####.", "#...#", "#...#", "####.",
                                       "#.#..", "#..#.", "#...#"};
    static const char* const kS[7] = {".####", "#....", "#....", ".###.",
                                       "....#", "....#", "####."};
    static const char* const kT[7] = {"#####", "..#..", "..#..", "..#..",
                                       "..#..", "..#..", "..#.."};
    static const char* const kU[7] = {"#...#", "#...#", "#...#", "#...#",
                                       "#...#", "#...#", ".###."};
    static const char* const kV[7] = {"#...#", "#...#", "#...#", "#...#",
                                       "#...#", ".#.#.", "..#.."};
    static const char* const kW[7] = {"#...#", "#...#", "#...#", "#.#.#",
                                       "#.#.#", "##.##", "#...#"};
    static const char* const kX[7] = {"#...#", "#...#", ".#.#.", "..#..",
                                       ".#.#.", "#...#", "#...#"};
    static const char* const kY[7] = {"#...#", "#...#", ".#.#.", "..#..",
                                       "..#..", "..#..", "..#.."};
    static const char* const kZ[7] = {"#####", "....#", "...#.", "..#..",
                                       ".#...", "#....", "#####"};
    static const char* const k0[7] = {".###.", "#...#", "#..##", "#.#.#",
                                       "##..#", "#...#", ".###."};
    static const char* const k1[7] = {"..#..", ".##..", "..#..", "..#..",
                                       "..#..", "..#..", "#####"};
    static const char* const k2[7] = {".###.", "#...#", "....#", "...#.",
                                       "..#..", ".#...", "#####"};
    static const char* const k3[7] = {".###.", "#...#", "....#", "..##.",
                                       "....#", "#...#", ".###."};
    static const char* const k4[7] = {"...#.", "..##.", ".#.#.", "#..#.",
                                       "#####", "...#.", "...#."};
    static const char* const k5[7] = {"#####", "#....", "####.", "....#",
                                       "....#", "#...#", ".###."};
    static const char* const k6[7] = {"..##.", ".#...", "#....", "####.",
                                       "#...#", "#...#", ".###."};
    static const char* const k7[7] = {"#####", "....#", "...#.", "..#..",
                                       ".#...", ".#...", ".#..."};
    static const char* const k8[7] = {".###.", "#...#", "#...#", ".###.",
                                       "#...#", "#...#", ".###."};
    static const char* const k9[7] = {".###.", "#...#", "#...#", ".####",
                                       "....#", "...#.", ".##.."};
    static const char* const kPeriod[7] = {".....", ".....", ".....", ".....",
                                            ".....", ".##..", ".##.."};
    static const char* const kComma[7] = {".....", ".....", ".....", ".....",
                                           ".....", ".##..", "..#.."};
    static const char* const kExclaim[7] = {"..#..", "..#..", "..#..", "..#..",
                                             "..#..", ".....", "..#.."};
    static const char* const kQuestion[7] = {".###.", "#...#", "....#", "..##.",
                                              "..#..", ".....", "..#.."};
    static const char* const kApostrophe[7] = {".#...", ".#...", ".....", ".....",
                                                ".....", ".....", "....."};
    static const char* const kColon[7] = {".....", ".##..", ".##..", ".....",
                                           ".##..", ".##..", "....."};
    static const char* const kHyphen[7] = {".....", ".....", ".....", "#####",
                                            ".....", ".....", "....."};

    switch (std::toupper(static_cast<unsigned char>(c))) {
        case ' ': return kSpace;
        case 'A': return kA;
        case 'B': return kB;
        case 'C': return kC;
        case 'D': return kD;
        case 'E': return kE;
        case 'F': return kF;
        case 'G': return kG;
        case 'H': return kH;
        case 'I': return kI;
        case 'J': return kJ;
        case 'K': return kK;
        case 'L': return kL;
        case 'M': return kM;
        case 'N': return kN;
        case 'O': return kO;
        case 'P': return kP;
        case 'Q': return kQ;
        case 'R': return kR;
        case 'S': return kS;
        case 'T': return kT;
        case 'U': return kU;
        case 'V': return kV;
        case 'W': return kW;
        case 'X': return kX;
        case 'Y': return kY;
        case 'Z': return kZ;
        case '0': return k0;
        case '1': return k1;
        case '2': return k2;
        case '3': return k3;
        case '4': return k4;
        case '5': return k5;
        case '6': return k6;
        case '7': return k7;
        case '8': return k8;
        case '9': return k9;
        case '.': return kPeriod;
        case ',': return kComma;
        case '!': return kExclaim;
        case '?': return kQuestion;
        case '\'': return kApostrophe;
        case ':': return kColon;
        case '-': return kHyphen;
        default: return nullptr;
    }
}

}  // namespace

bool BitmapFont::LoadRealFont(const std::string& path, const std::string& typefaceName, Face face) {
    // Dispatch by extension: a real Symbian .gdr (assets/gdr_font.h) or
    // a .ttf (TtfFont above). Both are genuinely implemented and
    // exercised -- .gdr is what main.cpp actually loads.
    //
    // Same-session history worth keeping, since it explains why both
    // paths exist: shadowkey's own text-draw call chain (decompiled --
    // DrawUIText -> FUN_1008f8a4 -> FUN_10022b20) calls genuine
    // EIKCORE::LegendFont() for ordinary menu text, i.e. the N-Gage
    // ROM's own ".gdr" system font -- so Ceurope.gdr was the first,
    // correct answer. A mid-session detour (comparing rendered .gdr
    // glyphs against a real screenshot) wrongly concluded none of its
    // typefaces matched and switched to a "Nokia Cellphone FC" TTF
    // lookalike instead. That comparison was against an *upscaled,
    // video-compressed* screenshot -- downscaling it back to native
    // 176x208 and comparing pixel-for-pixel showed Ceurope.gdr's
    // LatinBold12 matches exactly, letter for letter. Compression blur
    // on an ~11px bitmap font reads as "rounded" to the eye; it isn't.
    if (path.size() >= 4 &&
        _stricmp(path.c_str() + path.size() - 4, ".ttf") == 0) {
        // The TTF path is the Ui face only -- it predates M111's second
        // face and nothing asks a .ttf for the small one.
        constexpr int kTtfPixelHeight = 8;
        return RealTtfFont().Load(path, typefaceName, kTtfPixelHeight);
    }
    return FontSlot(face).Load(path, typefaceName);
}

void BitmapFont::DrawString(Backbuffer& bb, int x, int y, std::string_view text, uint16_t color,
                            Face face) {
    if (face == Face::Ui && RealTtfFont().IsLoaded()) {
        RealTtfFont().DrawString(bb, x, y, text, color);
        return;
    }

    GdrFont& font = RealFont(face);
    int cursorX = x;
    const int baselineY = y + font.Ascent();

    for (char c : text) {
        const GdrGlyph* glyph =
            font.GetGlyph(static_cast<char32_t>(static_cast<unsigned char>(c)));
        if (glyph) {
            int glyphX = cursorX + glyph->leftBearing;
            int glyphY = baselineY - glyph->ascentAboveBaseline;
            for (int row = 0; row < glyph->height; ++row) {
                for (int col = 0; col < glyph->width; ++col) {
                    if (glyph->bits[static_cast<size_t>(row) * glyph->width + col])
                        bb.SetPixel(glyphX + col, glyphY + row, color);
                }
            }
            cursorX += glyph->advance;
            continue;
        }

        const char* const* rows = GlyphRows(c);
        if (rows) {
            for (int row = 0; row < 7; ++row) {
                for (int col = 0; col < kGlyphWidth; ++col) {
                    if (rows[row][col] == '#') bb.SetPixel(cursorX + col, y + row, color);
                }
            }
        }
        cursorX += kAdvance;
    }
}

int BitmapFont::TextWidth(std::string_view text, Face face) {
    if (face == Face::Ui && RealTtfFont().IsLoaded()) return RealTtfFont().MeasureWidth(text);
    GdrFont& font = RealFont(face);
    if (font.IsLoaded()) {
        int w = 0;
        for (char c : text) {
            const GdrGlyph* glyph =
                font.GetGlyph(static_cast<char32_t>(static_cast<unsigned char>(c)));
            w += glyph ? glyph->advance : kAdvance;
        }
        return w;
    }
    return static_cast<int>(text.size()) * kAdvance;
}

int BitmapFont::LineHeight(Face face) {
    GdrFont& font = RealFont(face);
    return font.IsLoaded() ? font.CellHeight() : kGlyphHeight;
}

}  // namespace sk
