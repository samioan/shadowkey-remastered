#pragma once

// M68: a text/rectangle drawing surface at the *window's native* client
// resolution, handed to a per-present callback that runs immediately after
// the 176x208 Backbuffer has already been scaled and blitted to the window.
//
// This exists so the developer debug suite (src/debug/) can draw a readable
// console and stat panels without ever touching the game's own framebuffer.
// That matters for two reasons:
//
//   1. The real backbuffer is 176x208. At the project's ~6px font that is
//      about 29 columns -- unusable for a command console. The window is
//      presented at 3x (528x624), so drawing at native resolution with a
//      real fixed-pitch system font gives ~75 columns instead.
//   2. The port's regression evidence is byte-comparison of the tracked
//      `.ppm` render dumps (see docs/PORT_ROADMAP.md's M66 entry). An
//      overlay that never writes a pixel into `sk::Backbuffer` cannot move
//      those dumps, cannot perturb the software rasterizer's output, and
//      cannot change what a smoke test sees. Non-interference is
//      structural here, not a promise.
//
// The interface is deliberately tiny and free of any Windows type, so the
// debug library stays platform-agnostic and unit-testable: the smoke test
// implements this same interface over a plain character grid and asserts
// what the overlay drew.

#include <cstdint>
#include <string_view>

namespace sk {

// A plain 8-bit-per-channel color. Not PackRGB565 like the game's own
// Backbuffer -- this surface draws through the host's native compositor
// (GDI here), which has no use for the N-Gage's 16-bit format.
struct OverlayColor {
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
};

class OverlaySurface {
public:
    virtual ~OverlaySurface() = default;

    // Client-area size in real pixels.
    virtual int width() const = 0;
    virtual int height() const = 0;

    // Metrics of the fixed-pitch font DrawText() uses. Fixed-pitch is a
    // requirement, not a preference: the console's column arithmetic (text
    // wrapping, the caret, the aligned stat tables) is all done in
    // characters.
    virtual int charWidth() const = 0;
    virtual int lineHeight() const = 0;

    // `alphaPercent` 0 = invisible, 100 = opaque. Panels are drawn
    // semi-transparent so the game underneath stays visible while you read
    // the numbers describing it.
    virtual void FillRect(int x, int y, int w, int h, OverlayColor color, int alphaPercent) = 0;

    // Draws one line of text with its top-left corner at (x, y). No
    // wrapping, no newline handling -- the caller owns layout.
    virtual void DrawText(int x, int y, std::string_view text, OverlayColor color) = 0;

    // Convenience: how many whole characters fit across the surface.
    int columns() const { return charWidth() > 0 ? width() / charWidth() : 0; }
    int rows() const { return lineHeight() > 0 ? height() / lineHeight() : 0; }
};

}  // namespace sk
