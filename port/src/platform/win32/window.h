#pragma once
#include <functional>
#include <string>

#include "graphics/backbuffer.h"

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

    Window(int clientWidth, int clientHeight, const std::wstring& title);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    void SetKeyCallback(KeyCallback callback);

    // Runs the message pump until the window is closed. Calls onIdle every
    // time there are no pending messages (i.e. every "frame").
    void RunMessageLoop(const IdleCallback& onIdle);

    // Presents the backbuffer, nearest-neighbor scaled to the client area.
    void Present(const Backbuffer& backbuffer);

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
