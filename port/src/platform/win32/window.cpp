#include "platform/win32/window.h"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include "graphics/viewport.h"

#include <windows.h>
#include <timeapi.h>

// <windows.h> #defines DrawText to DrawTextW, which silently renames
// OverlaySurface::DrawText's override out of existence. The overlay never
// calls the Win32 function, so the macro is simply dropped here.
#undef DrawText

namespace sk {

namespace {

// BITMAPINFO with room for the 3 BI_BITFIELDS color masks (RGB565) right
// after the header, as GDI expects.
struct Rgb565BitmapInfo {
    BITMAPINFOHEADER header;
    DWORD masks[3];
};

// SK_DEBUG_SUITE (M68): the OverlaySurface implementation handed to the
// per-present overlay callback. Draws straight onto the window DC with GDI,
// after StretchDIBits has already put the game's frame there -- so nothing
// here can reach the game's 176x208 Backbuffer even by accident.
//
// The font is a real fixed-pitch system face at native resolution (the
// game's own 6px bitmap font would give ~29 columns across a 528px window,
// which is not enough for a command line). FixedPitchAndFamily is asked for
// explicitly and the metrics are measured back out of the DC rather than
// assumed, because the console's whole layout is column arithmetic.
class GdiOverlaySurface : public OverlaySurface {
public:
    GdiOverlaySurface(HDC hdc, int w, int h, HFONT font, int charW, int lineH)
        : hdc_(hdc), width_(w), height_(h), charWidth_(charW), lineHeight_(lineH) {
        previousFont_ = static_cast<HFONT>(SelectObject(hdc_, font));
        SetBkMode(hdc_, TRANSPARENT);
    }
    ~GdiOverlaySurface() override {
        if (previousFont_) SelectObject(hdc_, previousFont_);
    }

    GdiOverlaySurface(const GdiOverlaySurface&) = delete;
    GdiOverlaySurface& operator=(const GdiOverlaySurface&) = delete;

    int width() const override { return width_; }
    int height() const override { return height_; }
    int charWidth() const override { return charWidth_; }
    int lineHeight() const override { return lineHeight_; }

    void FillRect(int x, int y, int w, int h, OverlayColor color, int alphaPercent) override {
        if (w <= 0 || h <= 0 || alphaPercent <= 0) return;
        if (alphaPercent >= 100) {
            RECT r{x, y, x + w, y + h};
            HBRUSH brush = CreateSolidBrush(RGB(color.r, color.g, color.b));
            ::FillRect(hdc_, &r, brush);
            DeleteObject(brush);
            return;
        }
        // A 1x1 source bitmap stretched over the destination is the
        // cheapest way to get a constant-alpha fill out of GDI: with
        // AC_SRC_ALPHA left off, AlphaBlend uses SourceConstantAlpha alone
        // and the source needs no premultiplication.
        HDC memDc = CreateCompatibleDC(hdc_);
        if (!memDc) return;
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = 1;
        bi.bmiHeader.biHeight = 1;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(memDc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (bmp && bits) {
            *static_cast<uint32_t*>(bits) =
                (static_cast<uint32_t>(color.r) << 16) | (static_cast<uint32_t>(color.g) << 8) |
                static_cast<uint32_t>(color.b);
            HGDIOBJ old = SelectObject(memDc, bmp);
            BLENDFUNCTION blend{};
            blend.BlendOp = AC_SRC_OVER;
            blend.SourceConstantAlpha = static_cast<BYTE>(alphaPercent * 255 / 100);
            AlphaBlend(hdc_, x, y, w, h, memDc, 0, 0, 1, 1, blend);
            SelectObject(memDc, old);
        }
        if (bmp) DeleteObject(bmp);
        DeleteDC(memDc);
    }

    void DrawText(int x, int y, std::string_view text, OverlayColor color) override {
        if (text.empty()) return;
        SetTextColor(hdc_, RGB(color.r, color.g, color.b));
        TextOutA(hdc_, x, y, text.data(), static_cast<int>(text.size()));
    }

private:
    HDC hdc_;
    int width_;
    int height_;
    int charWidth_;
    int lineHeight_;
    HFONT previousFont_ = nullptr;
};

}  // namespace

struct Window::Impl {
    HWND hwnd = nullptr;
    int clientWidth = 0;
    int clientHeight = 0;
    bool closed = false;
    Rgb565BitmapInfo bmi{};
    Window::KeyCallback keyCallback;
    Window::CharCallback charCallback;
    Window::FocusLostCallback focusLostCallback;
    // SK_DEBUG_SUITE (M68). The font is created lazily on the first overlay
    // present and lives as long as the window, so the per-frame cost of the
    // overlay is a SelectObject rather than a CreateFont.
    Window::OverlayCallback overlayCallback;
    HFONT overlayFont = nullptr;
    int overlayCharWidth = 0;
    int overlayLineHeight = 0;
    // Post-M68 fix: the offscreen surface the frame is composed into before being
    // blitted to the window in one go -- see Present(). Cached across
    // frames and rebuilt only when the client area changes size.
    HDC backDc = nullptr;
    HBITMAP backBitmap = nullptr;
    HGDIOBJ backOldBitmap = nullptr;
    int backWidth = 0;
    int backHeight = 0;
    // M119: a high-resolution frame widened to 32 bits for GDI -- see
    // Present(). Kept between frames; empty until one is presented.
    std::vector<uint32_t> wide;
    // M120: borderless fullscreen, and the windowed placement to go back to.
    bool fullscreen = false;
    DWORD windowedStyle = 0;
    WINDOWPLACEMENT windowedPlacement{sizeof(WINDOWPLACEMENT)};
};

namespace {

// Post-M68 fix: tears down the cached offscreen surface, restoring the memory DC's
// original bitmap first -- a DC still holding a selected bitmap will not
// let that bitmap be deleted, which is how this kind of cache turns into a
// slow GDI handle leak on every window resize.
void ReleaseBackBuffer(Window::Impl* impl) {
    if (impl->backDc) {
        if (impl->backOldBitmap) SelectObject(impl->backDc, impl->backOldBitmap);
        DeleteDC(impl->backDc);
    }
    if (impl->backBitmap) DeleteObject(impl->backBitmap);
    impl->backDc = nullptr;
    impl->backBitmap = nullptr;
    impl->backOldBitmap = nullptr;
    impl->backWidth = 0;
    impl->backHeight = 0;
}

}  // namespace

namespace {

void ToggleFullscreen(HWND hwnd);

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* impl = reinterpret_cast<Window::Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CLOSE:
            if (impl) impl->closed = true;
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        // WM_SYSKEYDOWN/UP carry the same keys when Alt is held (and F10
        // unconditionally) -- without these, a key press that happens to
        // overlap an Alt tap silently never reaches InputState, leaving
        // the slot latched "down" forever if the release arrived as a
        // SYS message.
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_KEYDOWN:
        case WM_KEYUP:
            // M120: F11 and Alt+Enter are the fullscreen toggle, handled
            // here and kept from the game -- Alt+Enter would otherwise also
            // be an Enter, a confirm in every menu. Auto-repeat (bit 30)
            // is ignored, so holding the key toggles once.
            if ((msg == WM_KEYDOWN && wParam == VK_F11) ||
                (msg == WM_SYSKEYDOWN && wParam == VK_RETURN)) {
                if (!(lParam & (1 << 30))) ToggleFullscreen(hwnd);
                return 0;
            }
            if ((msg == WM_KEYUP && wParam == VK_F11) ||
                (msg == WM_SYSKEYUP && wParam == VK_RETURN)) {
                return 0;
            }
            if (impl && impl->keyCallback) {
                bool down = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
                impl->keyCallback(static_cast<int>(wParam), down);
            }
            // Alt+F4 and the system menu still need DefWindowProc for the
            // SYS variants, or the window can't be closed with the
            // keyboard.
            if (msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) {
                return DefWindowProcW(hwnd, msg, wParam, lParam);
            }
            return 0;
        // M69: the control sheet binds keypad 5 (the right-hand attack) to
        // mouse button 0. Delivered through the *key* callback as
        // VK_LBUTTON, which WM_KEYDOWN can never produce, so the mapping
        // stays in one table (engine/pc_key_map.h) and nothing downstream
        // needs a second input path.
        //
        // The capture matters: without it, pressing inside the window and
        // releasing outside never delivers WM_LBUTTONUP, and the slot stays
        // latched down forever -- the same hazard the WM_SYSKEYUP comment
        // above describes for Alt.
        case WM_LBUTTONDOWN:
            SetCapture(hwnd);
            if (impl && impl->keyCallback) impl->keyCallback(VK_LBUTTON, true);
            return 0;
        case WM_LBUTTONUP:
            ReleaseCapture();
            if (impl && impl->keyCallback) impl->keyCallback(VK_LBUTTON, false);
            return 0;
        // Focus loss (alt-tab, a click elsewhere) means no further key-up
        // will arrive for anything currently held. Without this the game
        // keeps walking forward after the window is left. Signalled as a
        // key-up on every slot the map knows, which is exactly what the
        // input layer needs to hear.
        case WM_KILLFOCUS:
            ReleaseCapture();
            if (impl && impl->focusLostCallback) impl->focusLostCallback();
            return 0;
        // Post-M68 fix: the other half of the flicker. The window class asks for a
        // COLOR_WINDOW background brush, so any invalidation had Windows
        // repaint the whole client area in the system window colour before
        // the next Present overwrote it -- a full-window flash. Present
        // paints every pixel of the client area itself, every tick, so
        // there is nothing for an erase to usefully do.
        case WM_ERASEBKGND:
            return 1;
        case WM_CHAR:
            if (impl && impl->charCallback) {
                impl->charCallback(static_cast<wchar_t>(wParam));
            }
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

// M120: the toggle F11 / Alt+Enter run. A free function because WndProc is
// one; it goes through the Window-level implementation below.
void ApplyFullscreen(Window::Impl* impl, bool on) {
    HWND hwnd = impl->hwnd;
    if (!hwnd || on == impl->fullscreen) return;
    if (on) {
        impl->windowedStyle = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE));
        impl->windowedPlacement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(hwnd, &impl->windowedPlacement);
        MONITORINFO mi{sizeof(mi)};
        if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) return;
        SetWindowLongW(hwnd, GWL_STYLE,
                       static_cast<LONG>((impl->windowedStyle & ~WS_OVERLAPPEDWINDOW) | WS_POPUP));
        SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
    } else {
        SetWindowLongW(hwnd, GWL_STYLE,
                       static_cast<LONG>(impl->windowedStyle ? impl->windowedStyle
                                                             : WS_OVERLAPPEDWINDOW | WS_VISIBLE));
        SetWindowPlacement(hwnd, &impl->windowedPlacement);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    impl->fullscreen = on;
}

void ToggleFullscreen(HWND hwnd) {
    auto* impl = reinterpret_cast<Window::Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (impl) ApplyFullscreen(impl, !impl->fullscreen);
}

}  // namespace

Window::Window(int clientWidth, int clientHeight, const std::wstring& title) {
    impl_ = new Impl();
    impl_->clientWidth = clientWidth;
    impl_->clientHeight = clientHeight;

    impl_->bmi.header.biSize = sizeof(BITMAPINFOHEADER);
    impl_->bmi.header.biWidth = Backbuffer::kWidth;
    // Negative height: top-down DIB, so row 0 in our buffer is the top row
    // on screen (matches the source data's row order, no manual flip).
    impl_->bmi.header.biHeight = -Backbuffer::kHeight;
    impl_->bmi.header.biPlanes = 1;
    impl_->bmi.header.biBitCount = 16;
    impl_->bmi.header.biCompression = BI_BITFIELDS;
    impl_->bmi.masks[0] = 0xF800;  // R
    impl_->bmi.masks[1] = 0x07E0;  // G
    impl_->bmi.masks[2] = 0x001F;  // B

    static const wchar_t* kClassName = L"ShadowkeyPortWindowClass";
    HINSTANCE hInstance = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    RECT rect{0, 0, clientWidth, clientHeight};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    impl_->hwnd = CreateWindowExW(
        0, kClassName, title.c_str(), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, hInstance, nullptr);

    SetWindowLongPtrW(impl_->hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(impl_));
    ShowWindow(impl_->hwnd, SW_SHOWNORMAL);
    UpdateWindow(impl_->hwnd);
}

Window::~Window() {
    if (impl_) {
        if (impl_->overlayFont) DeleteObject(impl_->overlayFont);  // SK_DEBUG_SUITE (M68)
        ReleaseBackBuffer(impl_);                                   // Post-M68 fix
        if (impl_->hwnd) DestroyWindow(impl_->hwnd);
        delete impl_;
    }
}

void Window::SetKeyCallback(KeyCallback callback) {
    impl_->keyCallback = std::move(callback);
}

void Window::SetCharCallback(CharCallback callback) {
    impl_->charCallback = std::move(callback);
}

void Window::SetFocusLostCallback(FocusLostCallback callback) {
    impl_->focusLostCallback = std::move(callback);
}

// SK_DEBUG_SUITE (M68).
void Window::SetOverlayCallback(OverlayCallback callback) {
    impl_->overlayCallback = std::move(callback);
}

bool Window::IsForeground() const {
    return impl_->hwnd != nullptr && GetForegroundWindow() == impl_->hwnd;
}

bool Window::WaitForCompositor() {
    // DwmFlush returns once the desktop compositor has presented its next
    // frame -- i.e. it paces to the monitor's refresh. Loaded by name so the
    // exe keeps no link-time dependency on dwmapi.dll for a setting most
    // players will never pick.
    using DwmFlushFn = HRESULT(WINAPI*)();
    static DwmFlushFn flush = []() -> DwmFlushFn {
        HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
        return dwm ? reinterpret_cast<DwmFlushFn>(GetProcAddress(dwm, "DwmFlush")) : nullptr;
    }();
    return flush != nullptr && SUCCEEDED(flush());
}

void Window::ClientSize(int* width, int* height) const {
    RECT client{};
    if (impl_->hwnd) GetClientRect(impl_->hwnd, &client);
    if (width) *width = client.right - client.left;
    if (height) *height = client.bottom - client.top;
}

void Window::SetFullscreen(bool on) { ApplyFullscreen(impl_, on); }

bool Window::IsFullscreen() const { return impl_->fullscreen; }

bool Window::IsMaximized() const { return impl_->hwnd != nullptr && IsZoomed(impl_->hwnd); }

void Window::SetClientSize(int width, int height) {
    HWND hwnd = impl_->hwnd;
    if (!hwnd || impl_->fullscreen || IsZoomed(hwnd) || IsIconic(hwnd)) return;
    const DWORD style = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE));
    RECT rect{0, 0, width, height};
    AdjustWindowRect(&rect, style, FALSE);
    int w = rect.right - rect.left, h = rect.bottom - rect.top;
    RECT now{};
    GetWindowRect(hwnd, &now);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    const RECT work = mi.rcWork;
    if (work.right > work.left) {
        w = (std::min)(w, static_cast<int>(work.right - work.left));
        h = (std::min)(h, static_cast<int>(work.bottom - work.top));
    }
    // Grown or shrunk about its own centre, then pulled back on screen.
    int x = (now.left + now.right) / 2 - w / 2;
    int y = (now.top + now.bottom) / 2 - h / 2;
    if (work.right > work.left) {
        x = (std::max)(static_cast<int>(work.left), (std::min)(x, static_cast<int>(work.right) - w));
        y = (std::max)(static_cast<int>(work.top), (std::min)(y, static_cast<int>(work.bottom) - h));
    }
    SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOOWNERZORDER);
}

void Window::SetFineTimer(bool on) {
    static bool active = false;
    if (on == active) return;
    if (on) {
        active = timeBeginPeriod(1) == TIMERR_NOERROR;
    } else {
        timeEndPeriod(1);
        active = false;
    }
}

void Window::Close() {
    if (impl_->hwnd) PostMessageW(impl_->hwnd, WM_CLOSE, 0, 0);
}

void Window::RunMessageLoop(const IdleCallback& onIdle) {
    MSG msg{};
    while (!impl_->closed) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                impl_->closed = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (impl_->closed) break;
        if (onIdle) onIdle();
        // The tick callback returns immediately whenever the fixed 40ms
        // tick isn't due yet (engine/game_clock.h), so without this the
        // loop spun a full CPU core flat out between frames -- ~25 useful
        // ticks a second and millions of wasted no-op iterations. Waiting
        // on the message queue with a 1ms timeout keeps input latency
        // unchanged (any keystroke wakes it immediately) while dropping
        // idle CPU to roughly nothing.
        MsgWaitForMultipleObjectsEx(0, nullptr, 1, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
    shouldClose_ = true;
}

void Window::Present(const Backbuffer& backbuffer) {
    if (!impl_->hwnd) return;
    HDC hdc = GetDC(impl_->hwnd);
    RECT client{};
    GetClientRect(impl_->hwnd, &client);
    int destW = client.right - client.left;
    int destH = client.bottom - client.top;
    if (destW <= 0 || destH <= 0) {  // minimized
        ReleaseDC(impl_->hwnd, hdc);
        return;
    }

    // Post-M68 fix: when there is an overlay, the frame is composed into an
    // offscreen bitmap and blitted once, instead of being drawn straight
    // onto the window.
    //
    // Why: StretchDIBits writes to the front buffer immediately, and the
    // overlay's own fills and text follow it as separate GDI calls. So
    // every frame the console's region was briefly repainted with *game*
    // pixels and then re-covered a moment later -- and at the fixed 25Hz
    // tick that gap is long enough to see. The game alone never flickered
    // because consecutive frames of a static menu are identical pixels;
    // the console flickered because that region toggled between two very
    // different images 25 times a second. Composing first and blitting
    // once closes the gap entirely.
    //
    // Only taken when an overlay is installed, so a build without the
    // debug suite presents through exactly the code path it always has.
    HDC target = hdc;
    if (impl_->overlayCallback) {
        if (!impl_->backDc || impl_->backWidth != destW || impl_->backHeight != destH) {
            ReleaseBackBuffer(impl_);
            impl_->backDc = CreateCompatibleDC(hdc);
            if (impl_->backDc) {
                // From the *window* DC, not the memory DC -- a bitmap made
                // compatible with a fresh memory DC would be 1bpp
                // monochrome, which is the classic form of this bug.
                impl_->backBitmap = CreateCompatibleBitmap(hdc, destW, destH);
                if (impl_->backBitmap) {
                    impl_->backOldBitmap = SelectObject(impl_->backDc, impl_->backBitmap);
                    impl_->backWidth = destW;
                    impl_->backHeight = destH;
                } else {
                    DeleteDC(impl_->backDc);
                    impl_->backDc = nullptr;
                }
            }
        }
        if (impl_->backDc) target = impl_->backDc;
    }

    // Post-M91 fix: the frame keeps the device's 176x208 aspect at any
    // window shape, centred, and whatever client area is left over is
    // painted black -- see graphics/viewport.h for why the engine cannot
    // simply be stretched to fit. Repainting the (constant, black) bars
    // every present rather than only on a size change is invisible and
    // costs two FillRects; it also means a freshly exposed strip after a
    // resize is covered by the very next tick, with no WM_PAINT handler
    // and no erase-background of its own (WM_ERASEBKGND is suppressed).
    //
    // M120: the shape is the frame's own, at the native height -- 176x208
    // for every frame the game drew before the Aspect Ratio setting, at any
    // Resolution (a whole multiple has exactly that shape, so this is the
    // same call as before for all of them), and wider when it is on.
    const int logicalWidth = backbuffer.height() > 0
                                 ? backbuffer.width() * Backbuffer::kHeight / backbuffer.height()
                                 : Backbuffer::kWidth;
    const Viewport view = FitPreservingAspect(destW, destH, logicalWidth, Backbuffer::kHeight);
    if (view.x > 0 || view.y > 0) {
        HBRUSH black = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        RECT bars[4] = {
            {0, 0, view.x, destH},                        // left
            {view.x + view.width, 0, destW, destH},       // right
            {view.x, 0, view.x + view.width, view.y},     // top
            {view.x, view.y + view.height, view.x + view.width, destH},  // bottom
        };
        for (const RECT& bar : bars) {
            if (bar.right > bar.left && bar.bottom > bar.top) ::FillRect(target, &bar, black);
        }
    }

    // M119: the source is whatever size the frame is -- the native 176x208,
    // or the Resolution setting's whole multiple of it. The viewport above
    // is deliberately computed from the native height: a multiple has the
    // same shape, so switching Resolution can never move the picture by a
    // pixel of rounding.
    SetStretchBltMode(target, COLORONCOLOR);
    if (backbuffer.width() == Backbuffer::kWidth && backbuffer.height() == Backbuffer::kHeight) {
        impl_->bmi.header.biWidth = Backbuffer::kWidth;
        impl_->bmi.header.biHeight = -Backbuffer::kHeight;
        StretchDIBits(target, view.x, view.y, view.width, view.height, 0, 0, Backbuffer::kWidth,
                      Backbuffer::kHeight, backbuffer.Data(),
                      reinterpret_cast<const BITMAPINFO*>(&impl_->bmi), DIB_RGB_COLORS, SRCCOPY);
    } else {
        // A high-resolution frame goes to GDI as 32-bit pixels. GDI's own
        // 16-bit-bitfields path converts every source pixel while it scales,
        // which measured at over 7ms a frame for a 4x frame -- longer than
        // rendering it. Widening here first (each 5/6-bit channel with its
        // top bits repeated below it, so full scale stays full scale) and
        // handing GDI the format the screen already has costs a fraction of
        // that. The native frame above keeps the exact path it always had.
        const int w = backbuffer.width(), h = backbuffer.height();
        impl_->wide.resize(static_cast<size_t>(w) * h);
        uint32_t* out = impl_->wide.data();
        const uint16_t* in = backbuffer.Data();
        for (size_t i = 0, n = impl_->wide.size(); i < n; ++i) {
            const uint32_t c = in[i];
            const uint32_t r = (c >> 11) & 0x1f, g = (c >> 5) & 0x3f, b = c & 0x1f;
            out[i] = ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
        }
        BITMAPINFO wideInfo{};
        wideInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        wideInfo.bmiHeader.biWidth = w;
        wideInfo.bmiHeader.biHeight = -h;
        wideInfo.bmiHeader.biPlanes = 1;
        wideInfo.bmiHeader.biBitCount = 32;
        wideInfo.bmiHeader.biCompression = BI_RGB;
        StretchDIBits(target, view.x, view.y, view.width, view.height, 0, 0, w, h, out, &wideInfo,
                      DIB_RGB_COLORS, SRCCOPY);
    }

    // SK_DEBUG_SUITE (M68): the debug overlay, drawn over the finished
    // frame at native resolution. Deliberately after the blit and outside
    // any game state -- see graphics/overlay_surface.h.
    if (impl_->overlayCallback) {
        if (!impl_->overlayFont) {
            impl_->overlayFont = CreateFontW(
                -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                FIXED_PITCH | FF_MODERN, L"Consolas");
            if (impl_->overlayFont) {
                HGDIOBJ old = SelectObject(target, impl_->overlayFont);
                TEXTMETRICW tm{};
                GetTextMetricsW(target, &tm);
                impl_->overlayCharWidth = static_cast<int>(tm.tmAveCharWidth);
                impl_->overlayLineHeight = static_cast<int>(tm.tmHeight + tm.tmExternalLeading);
                SelectObject(target, old);
            }
        }
        if (impl_->overlayFont && impl_->overlayCharWidth > 0 && impl_->overlayLineHeight > 0) {
            GdiOverlaySurface surface(target, destW, destH, impl_->overlayFont,
                                       impl_->overlayCharWidth, impl_->overlayLineHeight);
            impl_->overlayCallback(surface);
        }
    }

    // Post-M68 fix: the whole composed frame -- game and overlay together -- reaches
    // the window in one operation.
    if (target != hdc) {
        BitBlt(hdc, 0, 0, destW, destH, target, 0, 0, SRCCOPY);
    }

    ReleaseDC(impl_->hwnd, hdc);
}

}  // namespace sk
