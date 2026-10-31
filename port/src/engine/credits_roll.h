#pragma once
#include <string>

// M127: the credits screen, as the engine draws it -- screen mode 0xb.
//
// `ShowCredits()` (case 0x15 of the menu dispatcher) is `FUN_1006b168`:
// it reads `credits.txt` line by line with `fgets(buf, 0x100)`, turns every
// '\r' and '\n' into a **space** (so a CRLF line keeps two trailing spaces,
// which count when the line is centred), appends each line to a list at
// ScreenModeController+0xf4, points +0x100 at its head and switches the
// screen mode to 0xb.
//
// Mode 0xb's frame is `FUN_1006b3fc`, every frame:
//
//     Blit_RLESprite(global.spr[20], 0, 0);              // the backdrop
//     scroll += frameDelta >> 2;                          // +0xec
//     y = -scroll;
//     for (each line, 15px apart)
//         if ((unsigned)(y - 1) < 0xa4)                   // 1 <= y <= 164
//             FUN_1008f97c(gc, line, y, 0x752, 1, 1, 0, 0);
//     if (lineCount * 15 < scroll) { sound(0xb0, 100); close; }
//     if (held(slot 10) || (held(0x11) && heldPrev(0x11)))
//         { sound(100, 100); close; }
//
// `FUN_1008f97c(..., 1, 1, 0, 0)` is the centred draw with the shadow on
// (one pixel below, in 0xb1/0x9c/0x65) in the Legend font -- its last
// argument selects the font, and 0 is LegendFont; the 1 after the shadow
// flag is a bold flag that only the other font honours. 0x752 is the
// static-item brown. There is no manual scrolling at all: the roll runs at
// `delta >> 2` pixels a frame, 2px per 40ms tick, 50px a second.
//
// Slot 10 is Key 7 (Q on the PC layout) and 0x11 the right softkey (Esc).
// Sound 0xb0 is past the end of the front end's own 102-slot
// `menu_sounds.txt`, so the end of the roll is silent there; 100 is
// menu1.wav. `FUN_1006bccc` closes it: back to the menu, the list freed,
// the scroll zeroed.

namespace sk {

constexpr int kCreditsBackgroundSprite = 20;  // engine+0x44b0 == +0x4460 + 20*4
constexpr unsigned short kCreditsColor444 = 0x752;
constexpr int kCreditsLinePitch = 0xf;
constexpr int kCreditsFirstVisibleY = 1;
constexpr int kCreditsLastVisibleY = 0xa4;  // `y - 1U < 0xa4`
constexpr int kCreditsEndSound = 0xb0;
constexpr int kCreditsSkipSound = 100;

inline int CreditsAdvance(int scroll, int frameDeltaUnits) { return scroll + (frameDeltaUnits >> 2); }

// The y line `index` is drawn at, for a scroll offset.
inline int CreditsLineY(int index, int scroll) { return index * kCreditsLinePitch - scroll; }

inline bool CreditsLineVisible(int y) {
    return static_cast<unsigned>(y - kCreditsFirstVisibleY) <
           static_cast<unsigned>(kCreditsLastVisibleY);
}

// `+0xfc * 0xf < +0xec`: the roll has passed its last line.
inline bool CreditsEnded(int scroll, int lineCount) {
    return lineCount * kCreditsLinePitch < scroll;
}

// One raw `fgets` line as the engine keeps it: '\r' and '\n' become spaces.
inline std::string CreditsLineFromRaw(std::string raw) {
    for (char& c : raw) {
        if (c == '\r' || c == '\n') c = ' ';
    }
    return raw;
}

}  // namespace sk
