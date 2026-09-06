#pragma once

// M68: a game console. Generic on purpose -- it knows about commands,
// arguments, history, scrollback and completion, and nothing at all about
// Shadowkey. Every game-facing command is registered into it from
// debug_commands.cpp against a DebugHost, so this file stays testable
// without a window, a zone or an interpreter.
//
// Input arrives as two streams, mirroring what Win32 actually delivers:
// `Key` for non-printing keys (WM_KEYDOWN virtual keys, mapped by the host
// into the platform-neutral enum below) and `Char` for typed text (WM_CHAR,
// which has already applied shift/caps/layout). The console never sees a
// Windows type.
//
// Commands are *queued*, not executed, when Enter is pressed. `Drain()`
// runs them, and the host calls that from inside the game tick. That
// matters: a command like `call player LevelUp` runs real script code, and
// running script from a window-procedure callback would execute it outside
// the tick, on a half-updated world, in a place no exception handler
// covers.

#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "graphics/overlay_surface.h"

namespace sk_debug {

// Platform-neutral non-printing keys. The host maps its own key codes onto
// these; see main.cpp's DebugKeyFromVirtualKey.
enum class ConsoleKey {
    None,
    Enter,
    Backspace,
    Delete,
    Tab,
    Up,
    Down,
    Left,
    Right,
    Home,
    End,
    PageUp,
    PageDown,
    Escape,
};

// A command handler. `args` excludes the command name itself. Anything
// written to the returned string is printed to the console.
using CommandHandler = std::function<std::string(const std::vector<std::string>& args)>;

// Supplies completion candidates for argument `argIndex` (0-based, so the
// first argument after the command name is 0). Optional per command.
using CompletionProvider =
    std::function<std::vector<std::string>(int argIndex, const std::vector<std::string>& args)>;

struct Command {
    std::string name;
    std::string usage;    // e.g. "tp <x> <y> [z]"
    std::string help;     // one line
    std::string group;    // "world", "character", "items", "diagnostics", ...
    CommandHandler handler;
    CompletionProvider completer;
};

// Console output is colored by severity so a failure is visible in a wall
// of scrollback.
enum class LineKind { Output, Input, Notice, Warning, Error };

struct ConsoleLine {
    LineKind kind = LineKind::Output;
    std::string text;
};

class DebugConsole {
public:
    DebugConsole();

    // ---- registration -------------------------------------------------

    void Register(Command command);
    // Registers `alias` as another name for an existing command, with its
    // arguments prepended. `alias("tpt", "tp", {"--tile"})` style.
    void RegisterAlias(const std::string& alias, const std::string& target,
                        std::vector<std::string> prefixArgs);
    const std::map<std::string, Command>& commands() const { return m_Commands; }

    // ---- open/closed --------------------------------------------------

    bool open() const { return m_Open; }
    void SetOpen(bool open);
    void Toggle() { SetOpen(!m_Open); }

    // ---- input ----------------------------------------------------------
    // Both return true if the console consumed the event, in which case the
    // host must not forward it to the game.

    bool HandleKey(ConsoleKey key, bool ctrlDown);
    bool HandleChar(char ch);

    // ---- execution -------------------------------------------------------

    // Queues a command line exactly as if it had been typed and entered.
    void Submit(const std::string& line);
    // Runs everything queued. Call from the game tick, not from an input
    // callback -- see this file's header comment.
    void Drain();
    bool hasQueuedCommands() const { return !m_Queue.empty(); }
    // Runs one line immediately, returning its output. Used by `exec` and
    // by the smoke test, which has no tick to drain from.
    std::string Execute(const std::string& line);

    // ---- output -----------------------------------------------------------

    void Print(const std::string& text, LineKind kind = LineKind::Output);
    void PrintMultiline(const std::string& text, LineKind kind = LineKind::Output);
    void Clear();
    const std::deque<ConsoleLine>& lines() const { return m_Lines; }

    // ---- key bindings ------------------------------------------------------
    // `bind F5 "tp 100 100"` -- the host asks whether a key has a binding
    // while the console is closed and submits it if so.
    void Bind(const std::string& key, const std::string& commandLine);
    void Unbind(const std::string& key);
    const std::map<std::string, std::string>& bindings() const { return m_Bindings; }
    // Returns the command bound to `key`, or empty.
    std::string BindingFor(const std::string& key) const;

    // ---- rendering ----------------------------------------------------------

    // Draws the drop-down over the top `heightFraction` of the surface.
    void Render(sk::OverlaySurface& surface, float heightFraction) const;

    // ---- helpers exposed for commands and tests --------------------------

    // Splits a command line into tokens, honouring double quotes so an
    // argument can contain spaces.
    static std::vector<std::string> Tokenize(const std::string& line);

private:
    void PushHistory(const std::string& line);
    void CompleteCurrentToken();
    void ScrollBy(int lines);

    std::map<std::string, Command> m_Commands;
    std::map<std::string, std::string> m_Bindings;
    std::deque<ConsoleLine> m_Lines;
    std::vector<std::string> m_History;
    std::deque<std::string> m_Queue;
    std::string m_Input;
    size_t m_Caret = 0;
    int m_HistoryCursor = -1;   // -1 = editing a fresh line
    int m_Scroll = 0;           // lines scrolled back from the newest
    bool m_Open = false;
    size_t m_MaxLines = 2000;
    // Set while Drain()/Execute() is running, so a command that prints does
    // not also get echoed as if the user typed it.
    bool m_Executing = false;
};

}  // namespace sk_debug
