#pragma once
#include <functional>
#include <string>

#include "graphics/backbuffer.h"
#include "graphics/overlay_surface.h"

namespace sk {

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
