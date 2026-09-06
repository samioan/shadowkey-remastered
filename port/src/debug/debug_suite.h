#pragma once

// M68: the one type main.cpp talks to.
//
// Everything else in src/debug/ hangs off this: the console, the overlay,
// the metrics sink, the script tracer and the command table. main.cpp's
// integration is six calls, all tagged `SK_DEBUG_SUITE` so a grep finds
// every touchpoint when the suite is eventually removed:
//
//     sk_debug::DebugSuite debug;              // construct
//     debug.Attach(host, interpreter, dir);    // once, after setup
//     debug.HandleKey(key, down, ctrl);        // from the window key hook
//     debug.HandleChar(ch);                    // from the window char hook
//     debug.BeginTick(seconds);                // top of the game tick
//     debug.Render(surface);                   // from the window overlay hook
//
// See docs/DEBUG_SUITE.md for the removal recipe and the command reference.

#include <string>

#include "debug/debug_commands.h"
#include "debug/debug_console.h"
#include "debug/debug_overlay.h"
#include "debug/script_tracer.h"
#include "graphics/overlay_surface.h"

class skInterpreter;

namespace sk_debug {

class DebugHost;

class DebugSuite {
public:
    DebugSuite();
    ~DebugSuite();

    DebugSuite(const DebugSuite&) = delete;
    DebugSuite& operator=(const DebugSuite&) = delete;

    // `configDirectory` is where `exec` looks for .cfg files, and where
    // `autoexec.cfg` is read from at attach time.
    void Attach(DebugHost& host, skInterpreter& interpreter, std::string configDirectory);

    // ---- input ---------------------------------------------------------
    // `HandleKey` returns true when the suite consumed the key and the game
    // must not see it. `key` is a raw host key id (a Win32 virtual key in
    // this port); the suite owns the mapping onto its own enum so main.cpp
    // does not have to.
    bool HandleKey(int hostKeyCode, bool down, bool ctrlDown, bool shiftDown);
    bool HandleChar(char ch);
    // True while the console has keyboard focus -- main.cpp uses this to
    // stop feeding InputState, so typing `3` into the console does not also
    // open a door.
    bool capturingInput() const { return m_Console.open(); }

    // ---- per-tick -------------------------------------------------------
    // Called at the top of the game tick with the length of the frame that
    // just elapsed. Rolls the metrics frame and runs any queued commands --
    // deliberately here rather than in the input callback, so a command that
    // runs script code runs inside the tick like everything else.
    void BeginTick(double frameSeconds);

    // ---- rendering -------------------------------------------------------
    void Render(sk::OverlaySurface& surface);

    // ---- access ----------------------------------------------------------
    DebugConsole& console() { return m_Console; }
    DebugOverlay& overlay() { return m_Overlay; }
    ScriptTracer& tracer() { return m_Tracer; }
    // Set by the `quit` command.
    bool quitRequested() const { return m_Context.quitRequested; }

    // Convenience for main.cpp's own instrumentation: one place that knows
    // whether the suite is attached, so a call site does not need a null
    // check of its own.
    void Note(const std::string& category, std::string text);

    // Maps a Win32 virtual-key code onto the console's platform-neutral
    // enum. Public so the smoke test can drive the console without Windows.
    static ConsoleKey ConsoleKeyFromHostKey(int hostKeyCode);
    // The name `bind` uses for a host key code ("F5", "Home", ...), or empty
    // if the key is not bindable.
    static std::string BindNameForHostKey(int hostKeyCode);

private:
    DebugConsole m_Console;
    DebugOverlay m_Overlay;
    ScriptTracer m_Tracer;
    CommandContext m_Context;
    DebugHost* m_Host = nullptr;
    bool m_Attached = false;
};

}  // namespace sk_debug
