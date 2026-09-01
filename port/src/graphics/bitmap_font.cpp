#include "graphics/bitmap_font.h"

#include <cctype>

namespace sk {

namespace {

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

void BitmapFont::DrawString(Backbuffer& bb, int x, int y, std::string_view text, uint16_t color) {
    int cursorX = x;
    for (char c : text) {
        const char* const* rows = GlyphRows(c);
        if (rows) {
            for (int row = 0; row < kGlyphHeight; ++row) {
                for (int col = 0; col < kGlyphWidth; ++col) {
                    if (rows[row][col] == '#') bb.SetPixel(cursorX + col, y + row, color);
                }
            }
        }
        cursorX += kAdvance;
    }
}

}  // namespace sk
