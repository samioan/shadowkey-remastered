#pragma once

// Post-M91 fix: where the 176x208 frame goes inside a resizable window.
//
// Present() used to hand StretchDIBits the whole client rect, so every
// window shape the user dragged out stretched the N-Gage's 11:13 screen to
// match it -- a maximised 16:9 window rendered the game more than three
// times too wide. The device's screen is a fixed aspect and the engine has
// no notion of any other one (every HUD sprite, menu row and the 3D
// projection's 90-degree isotropic frustum are all authored against those
// exact 176x208 pixels, see docs/RENDER_3D.md's M71 entry), so the frame is
// scaled by the same factor on both axes and centred, with the leftover
// margin painted black.
//
// Kept as a free function over plain ints, with no Windows types and no
// Backbuffer dependency, so the arithmetic is unit-testable away from a
// real window (src/tests/aspect_viewport_smoke.cpp).

namespace sk {

// A destination rectangle in client-area pixels.
struct Viewport {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// The largest srcW:srcH rectangle that fits inside clientW x clientH,
// centred. Returns an empty viewport for any non-positive input (a
// minimized window reports a 0x0 client area).
//
// The comparison is a cross-multiply rather than a ratio so the choice of
// axis is exact: no rounding happens before the decision of which
// dimension is the binding one.
inline Viewport FitPreservingAspect(int clientW, int clientH, int srcW, int srcH) {
    if (clientW <= 0 || clientH <= 0 || srcW <= 0 || srcH <= 0) return Viewport{};

    Viewport out;
    if (static_cast<long long>(clientW) * srcH <= static_cast<long long>(clientH) * srcW) {
        // Width-bound: the window is proportionally wider than the source,
        // so the bars are top and bottom (letterbox).
        out.width = clientW;
        out.height = static_cast<int>(static_cast<long long>(clientW) * srcH / srcW);
    } else {
        // Height-bound: bars left and right (pillarbox).
        out.height = clientH;
        out.width = static_cast<int>(static_cast<long long>(clientH) * srcW / srcH);
    }
    // Integer division can only round down, so a degenerate client area
    // (one pixel of height, say) must not produce a zero-sized blit.
    if (out.width < 1) out.width = 1;
    if (out.height < 1) out.height = 1;

    out.x = (clientW - out.width) / 2;
    out.y = (clientH - out.height) / 2;
    return out;
}

}  // namespace sk
