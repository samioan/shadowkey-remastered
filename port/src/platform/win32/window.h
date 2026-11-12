#pragma once
#include <functional>
#include <string>

#include "graphics/backbuffer.h"
#include "graphics/overlay_surface.h"

namespace sk {

class D3D11Renderer;

// Minimal Win32 window + message pump, presenting a Backbuffer via GDI
// (StretchDIBits) each frame. No SDL2/OpenGL -- the original engine is a
// CPU-side software rasterizer into a flat 176x208 buffer, so a plain GDI
// blit reproduces that architecture directly (see the port scaffold plan's
// rationale). See docs/RENDER_LOOP.md for the tick-rate this will be
// driven at once the engine loop (M1) lands on top of this.
class Window {
public:
    // Called once per message-pump iteration where no window messages were
    // pending -- the host drives ticking/presenting from here.
    using IdleCallback = std::function<void()>;
    // vkCode = Windows virtual-key code (VK_LEFT, '1'..'9', etc.), down =
    // true on WM_KEYDOWN, false on WM_KEYUP. Auto-repeat key-downs are
    // passed through as-is (down=true again) -- callers using edge
    // detection (InputState::JustPressed) are unaffected either way.
    using KeyCallback = std::function<void(int vkCode, bool down)>;
    // WM_CHAR delivers the actual typed character (already handling
    // shift/caps/layout), unlike WM_KEYDOWN's raw virtual-key code --
    // used for the name-entry screen (see MenuExecutable::OpenEditText).
    // Includes control characters Windows folds into WM_CHAR: 0x08
    // (backspace), 0x0D (enter).
    using CharCallback = std::function<void(wchar_t ch)>;

    // M69: the window lost keyboard focus (alt-tab, a click elsewhere), so
    // no key-up will arrive for anything currently held. The host clears
    // its held-key state here; without it the game keeps walking forward
    // after the window is left behind.
    using FocusLostCallback = std::function<void()>;

    // SK_DEBUG_SUITE (M68): called from inside Present(), immediately after
    // the game's Backbuffer has been scaled and blitted, with a drawing
    // surface at the window's real client resolution. Null by default and
    // never set unless the debug suite is compiled in, so a shipping build
    // pays one null check per frame. See graphics/overlay_surface.h for why
    // the debug UI draws here instead of into the Backbuffer.
    using OverlayCallback = std::function<void(OverlaySurface&)>;

    Window(int clientWidth, int clientHeight, const std::wstring& title);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    void SetKeyCallback(KeyCallback callback);
    void SetCharCallback(CharCallback callback);
    void SetFocusLostCallback(FocusLostCallback callback);
    // SK_DEBUG_SUITE (M68) -- see OverlayCallback above.
    void SetOverlayCallback(OverlayCallback callback);

    // Posts WM_CLOSE to this window, same as the user clicking the close
    // button -- for native code (QuitGame()) that needs to end the
    // program from outside the message-pump thread's own call stack.
    void Close();

    // Runs the message pump until the window is closed. Calls onIdle every
    // time there are no pending messages (i.e. every "frame").
    void RunMessageLoop(const IdleCallback& onIdle);

    // Presents the backbuffer, nearest-neighbor scaled to the client area.
    // M119: any size -- see the Resolution setting (graphics/scene_upscale.h).
    void Present(const Backbuffer& backbuffer);

    // M129: the Direct3D 11 renderer (platform/win32/d3d11_renderer.h).
    //
    // SetGpuRenderer(true) creates the device and gives it this window; from
    // then on every Present goes through it, 2D frames included, because a
    // window a swap chain presents to cannot also be drawn with GDI. It
    // returns whether the GPU path is now active: false means Direct3D 11
    // is not available here (gpuError() says why) and the window is still
    // presenting through GDI, exactly as before. SetGpuRenderer(false) goes
    // back to GDI.
    bool SetGpuRenderer(bool on);
    bool GpuRendererActive() const;
    D3D11Renderer* gpu();
    const std::string& gpuError() const;
    // Presents the scene the GPU renderer last drew, under the game's 2D
    // layer -- see D3D11Renderer::PresentScene. False when the GPU path is
    // not active or the present failed; the caller then draws in software.
    bool PresentGpuScene(const Backbuffer& layer, const Backbuffer& under);

    // M119: whether this window is the one the user is using. A gamepad is
    // read whatever window has focus, so the game ignores it while it is
    // in the background, the same way it gets no keys.
    bool IsForeground() const;

    // M119: blocks until the desktop compositor's next frame (DwmFlush) --
    // the Frame Rate setting's V-Sync pacing. False when that is not
    // available, and the caller falls back to a timed rate.
    bool WaitForCompositor();

    // M119: the client area's current size in pixels (0x0 while minimized).
    void ClientSize(int* width, int* height) const;

    // M120: borderless fullscreen on the monitor the window is on, and back.
    // Leaving restores the window's previous position and size. F11 and
    // Alt+Enter toggle it from inside the window itself (neither reaches
    // the key callback, so neither is a game key).
    void SetFullscreen(bool on);
    bool IsFullscreen() const;
    // M120: whether the window is maximized -- the Aspect Ratio setting
    // leaves such a window's shape alone, as it does a fullscreen one.
    bool IsMaximized() const;
    // M120: resizes the windowed client area, kept inside the work area of
    // the monitor it is on and centred where it was. Ignored while
    // fullscreen or maximized.
    void SetClientSize(int width, int height);

    // M119: asks Windows for its 1ms scheduler tick (timeBeginPeriod) while
    // `on`. The default ~15.6ms tick is fine for the original 25Hz -- every
    // wait overshoots, but by less than a tick -- and cannot pace 60 frames a
    // second or more, so it is requested only while the Frame Rate setting
    // is above Original, and handed back when it returns there.
    static void SetFineTimer(bool on);

    // M125: mouse look. While `on` (and the window is in front) the cursor
    // is hidden and held inside the client area, and relative mouse motion
    // -- raw input, so it is neither accelerated nor stopped by the screen
    // edge -- accumulates for TakeMouseDelta. Called every loop iteration
    // with whether gameplay wants it; cheap when nothing changes. Losing
    // focus lets the cursor go at once; it is taken again only when the
    // game asks with the window back in front.
    void SetMouseLook(bool on);
    bool MouseLookActive() const;
    // The motion accumulated since the last call, in raw mouse counts
    // (+x right, +y down), and resets it. Both 0 while mouse look is off.
    void TakeMouseDelta(int* dx, int* dy);
    // The same, without resetting it -- what the frame loop adds to an
    // interpolated frame drawn between two ticks.
    void PeekMouseDelta(int* dx, int* dy) const;

    bool ShouldClose() const { return shouldClose_; }

    // Opaque pimpl -- public only so window.cpp's free-function WndProc
    // (not a Window member) can name/define it; callers outside window.cpp
    // still can't do anything with an incomplete type.
    struct Impl;

private:
    Impl* impl_;
    bool shouldClose_ = false;
};

}  // namespace sk
