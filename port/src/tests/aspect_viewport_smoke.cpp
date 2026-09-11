// Post-M91 fix -- the window stretched the frame to whatever shape it was
// dragged to.
//
// Reported from play: resizing or maximising `shadowkey_port.exe` stretched
// the N-Gage's 176x208 screen to the new client rect, so a 16:9 window drew
// the game more than three times too wide. `Window::Present` handed
// StretchDIBits the full client area; it now asks
// `sk::FitPreservingAspect` for a centred 176:208 destination rectangle and
// paints the leftover margin black (`graphics/viewport.h`).
//
// The arithmetic is the whole of the change that can be wrong -- the GDI
// calls around it are a FillRect and the same StretchDIBits with different
// destination corners -- so it is tested here, without a window.
#include <cstdio>

#include "graphics/backbuffer.h"
#include "graphics/viewport.h"

namespace {

int g_Checks = 0;
int g_Failures = 0;

void Check(bool ok, const char* what) {
    ++g_Checks;
    if (!ok) {
        ++g_Failures;
        std::printf("  FAIL: %s\n", what);
    } else {
        std::printf("  ok:   %s\n", what);
    }
}

void CheckViewport(int clientW, int clientH, int x, int y, int w, int h, const char* what) {
    const sk::Viewport v = sk::FitPreservingAspect(clientW, clientH, sk::Backbuffer::kWidth,
                                                    sk::Backbuffer::kHeight);
    ++g_Checks;
    if (v.x != x || v.y != y || v.width != w || v.height != h) {
        ++g_Failures;
        std::printf("  FAIL: %s -- %dx%d gave (%d,%d %dx%d), wanted (%d,%d %dx%d)\n", what,
                     clientW, clientH, v.x, v.y, v.width, v.height, x, y, w, h);
    } else {
        std::printf("  ok:   %s\n", what);
    }
}

// What the aspect fix is actually for: the frame's own proportions must not
// move, whatever the window does. Checked as a cross-multiply against the
// source aspect, allowing the one pixel integer division can shave off.
void CheckAspectHeld(int clientW, int clientH, const char* what) {
    const sk::Viewport v = sk::FitPreservingAspect(clientW, clientH, sk::Backbuffer::kWidth,
                                                    sk::Backbuffer::kHeight);
    const long long lhs = static_cast<long long>(v.width) * sk::Backbuffer::kHeight;
    const long long rhs = static_cast<long long>(v.height) * sk::Backbuffer::kWidth;
    const long long slack = static_cast<long long>(sk::Backbuffer::kHeight) +
                            sk::Backbuffer::kWidth;
    ++g_Checks;
    const bool fits = v.x >= 0 && v.y >= 0 && v.x + v.width <= clientW &&
                      v.y + v.height <= clientH;
    if (!fits || lhs - rhs > slack || rhs - lhs > slack) {
        ++g_Failures;
        std::printf("  FAIL: %s -- %dx%d gave (%d,%d %dx%d)\n", what, clientW, clientH, v.x, v.y,
                     v.width, v.height);
    } else {
        std::printf("  ok:   %s\n", what);
    }
}

}  // namespace

int main() {
    std::printf("== the default window is still an exact 3x, with no bars ==\n");
    // 528x624 is what main() asks CreateWindowExW for. An exact multiple of
    // the source must land at the origin filling the client area, or the
    // fix would have cost the port its pixel-exact default presentation.
    CheckViewport(176 * 3, 208 * 3, 0, 0, 176 * 3, 208 * 3, "528x624 -> full client, no bars");
    CheckViewport(176, 208, 0, 0, 176, 208, "1:1 -> full client, no bars");

    std::printf("\n== a wider window pillarboxes (bars left and right) ==\n");
    // 1920x1080: height-bound. 1080 * 176 / 208 = 913 wide, leaving
    // (1920-913)/2 = 503 of black each side.
    CheckViewport(1920, 1080, 503, 0, 913, 1080, "1920x1080 -> 913 wide, centred");
    // Double the width of the 3x window, same height: the frame keeps its
    // 3x size and the extra 528px becomes margin.
    CheckViewport(176 * 6, 208 * 3, 264, 0, 176 * 3, 208 * 3, "twice as wide -> unchanged frame");

    std::printf("\n== a taller window letterboxes (bars top and bottom) ==\n");
    // Width-bound: 528 * 208 / 176 = 624, so a 1000px-tall window keeps the
    // 3x frame and splits the remaining 376 rows.
    CheckViewport(176 * 3, 1000, 0, 188, 176 * 3, 624, "528x1000 -> 624 tall, centred");

    std::printf("\n== the aspect holds across shapes, and stays inside the client ==\n");
    CheckAspectHeld(1366, 768, "1366x768");
    CheckAspectHeld(2560, 1440, "2560x1440");
    CheckAspectHeld(3440, 1440, "3440x1440 ultrawide");
    CheckAspectHeld(640, 480, "640x480");
    CheckAspectHeld(300, 1200, "300x1200, taller than it is wide");
    CheckAspectHeld(177, 209, "177x209, one pixel over 1:1");

    std::printf("\n== degenerate client areas ==\n");
    // A minimized window reports a 0x0 client area; Present bails before
    // this is reached, but the helper must not hand back a negative or
    // zero-sized blit rectangle if it ever is.
    const sk::Viewport minimized = sk::FitPreservingAspect(0, 0, 176, 208);
    Check(minimized.width == 0 && minimized.height == 0, "0x0 client -> empty viewport");
    const sk::Viewport negative = sk::FitPreservingAspect(-4, 600, 176, 208);
    Check(negative.width == 0 && negative.height == 0, "negative client -> empty viewport");
    const sk::Viewport sliver = sk::FitPreservingAspect(400, 1, 176, 208);
    Check(sliver.width >= 1 && sliver.height >= 1, "1px-tall client -> never a zero-sized blit");

    std::printf("\n%d checks, %d failures -- %s\n", g_Checks, g_Failures,
                 g_Failures == 0 ? "OK" : "FAILED");
    return g_Failures == 0 ? 0 : 1;
}
