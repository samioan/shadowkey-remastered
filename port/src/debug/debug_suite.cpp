#include "debug/debug_suite.h"

#include <cstdio>
#include <fstream>

#include "debug/debug_host.h"
#include "debug/debug_metrics.h"
#include "simkin_bindings/native_binding_common.h"

namespace sk_debug {

namespace {

// Win32 virtual-key codes, spelled out rather than pulled in from
// <windows.h>: keeping this library free of the platform header is what
// lets the smoke test drive the console with no window, and these values
// are frozen ABI, not something that can drift.
constexpr int kVkBack = 0x08;
constexpr int kVkTab = 0x09;
constexpr int kVkReturn = 0x0D;
constexpr int kVkEscape = 0x1B;
constexpr int kVkPageUp = 0x21;
constexpr int kVkPageDown = 0x22;
constexpr int kVkEnd = 0x23;
constexpr int kVkHome = 0x24;
constexpr int kVkLeft = 0x25;
constexpr int kVkUp = 0x26;
constexpr int kVkRight = 0x27;
constexpr int kVkDown = 0x28;
constexpr int kVkDelete = 0x2E;
constexpr int kVkF1 = 0x70;
constexpr int kVkF12 = 0x7B;
constexpr int kVkOem3 = 0xC0;  // the `~ key, left of 1

// The suite's own fixed keys. F5-F12 are deliberately left alone so `bind`
// has somewhere to put a user's own shortcuts.
constexpr int kToggleConsoleKey = kVkF1;
constexpr int kNextPageKey = 0x71;   // F2
constexpr int kPrevPageKey = 0x72;   // F3
constexpr int kMiniBarKey = 0x73;    // F4

// SK_DEBUG_SUITE: the soft-fail observer installed into sk_bindings. A
// free function because the hook is a plain function pointer (see
// native_binding_common.h for why it is not a std::function).
void OnSoftFail(const char* object, const char* method, const char* args) {
    Count("script.softfails");
    Count(std::string("softfail.") + object + "." + method);
    Log("softfail", std::string(object) + "." + method + "(" + args + ")");
}

}  // namespace

DebugSuite::DebugSuite() = default;

DebugSuite::~DebugSuite() {
    // Both hooks must be torn down explicitly: the interpreter and the
    // bindings library outlive nothing here, but they hold raw pointers
    // into this object, and leaving either dangling would turn a clean
    // shutdown into a crash.
    m_Tracer.Detach();
    sk_bindings::SetSoftFailObserver(nullptr);
}

void DebugSuite::Attach(DebugHost& host, skInterpreter& interpreter,
                         std::string configDirectory) {
    m_Host = &host;
    m_Context.console = &m_Console;
    m_Context.host = &host;
    m_Context.overlay = &m_Overlay;
    m_Context.tracer = &m_Tracer;
    m_Context.configDirectory = std::move(configDirectory);

    RegisterDebugCommands(m_Context);
    m_Tracer.Attach(interpreter);
    m_Tracer.SetLevel(ScriptTracer::Level::Off);
    sk_bindings::SetSoftFailObserver(&OnSoftFail);
    m_Attached = true;

    m_Console.Print("F1 console  F2/F3 overlay page  F4 mini bar  F5-F12 free for `bind`",
                     LineKind::Notice);
    m_Console.Print("`help` for the command list. `mark`, do a thing, `diff` shows what ran.",
                     LineKind::Notice);

    // An autoexec makes a repro reproducible: put the setup for whatever
    // you are chasing in port/debug/autoexec.cfg and every launch starts
    // there. Silently absent is the normal case.
    const std::string autoexec = m_Context.configDirectory + "/autoexec.cfg";
    std::ifstream probe(autoexec);
    if (probe) {
        probe.close();
        m_Console.PrintMultiline(m_Console.Execute("exec autoexec"), LineKind::Output);
    }
}

// ------------------------------------------------------------------- input

ConsoleKey DebugSuite::ConsoleKeyFromHostKey(int hostKeyCode) {
    switch (hostKeyCode) {
        case kVkReturn: return ConsoleKey::Enter;
        case kVkBack: return ConsoleKey::Backspace;
        case kVkDelete: return ConsoleKey::Delete;
        case kVkTab: return ConsoleKey::Tab;
        case kVkUp: return ConsoleKey::Up;
        case kVkDown: return ConsoleKey::Down;
        case kVkLeft: return ConsoleKey::Left;
        case kVkRight: return ConsoleKey::Right;
        case kVkHome: return ConsoleKey::Home;
        case kVkEnd: return ConsoleKey::End;
        case kVkPageUp: return ConsoleKey::PageUp;
        case kVkPageDown: return ConsoleKey::PageDown;
        case kVkEscape: return ConsoleKey::Escape;
        default: return ConsoleKey::None;
    }
}

std::string DebugSuite::BindNameForHostKey(int hostKeyCode) {
    if (hostKeyCode >= kVkF1 && hostKeyCode <= kVkF12) {
        return "f" + std::to_string(hostKeyCode - kVkF1 + 1);
    }
    switch (hostKeyCode) {
        case kVkHome: return "home";
        case kVkEnd: return "end";
        case kVkDelete: return "delete";
        case kVkPageUp: return "pageup";
        case kVkPageDown: return "pagedown";
        default: return std::string();
    }
}

bool DebugSuite::HandleKey(int hostKeyCode, bool down, bool ctrlDown) {
    if (!m_Attached) return false;
    // Key-up is never consumed. If it were, a key held across a console
    // toggle would leave its InputState slot latched down forever -- the
    // exact hazard window.cpp's WM_SYSKEYUP comment already documents.
    if (!down) return false;

    if (hostKeyCode == kToggleConsoleKey || hostKeyCode == kVkOem3) {
        m_Console.Toggle();
        if (m_Console.open()) Count("debug.console_opens");
        return true;
    }

    if (m_Console.open()) {
        const ConsoleKey key = ConsoleKeyFromHostKey(hostKeyCode);
        m_Console.HandleKey(key, ctrlDown);
        // Everything is consumed while the console has focus, printable keys
        // included -- those arrive separately as WM_CHAR and are handled by
        // HandleChar. Returning true unconditionally is what stops a typed
        // digit from also being a game action.
        return true;
    }

    // Console closed: the suite's own function keys, then user bindings.
    if (m_Host) {
        if (hostKeyCode == kNextPageKey) {
            m_Overlay.CyclePage(*m_Host, 1);
            return true;
        }
        if (hostKeyCode == kPrevPageKey) {
            m_Overlay.CyclePage(*m_Host, -1);
            return true;
        }
    }
    if (hostKeyCode == kMiniBarKey) {
        m_Overlay.SetMiniBar(!m_Overlay.miniBar());
        return true;
    }

    const std::string bindName = BindNameForHostKey(hostKeyCode);
    if (!bindName.empty()) {
        const std::string command = m_Console.BindingFor(bindName);
        if (!command.empty()) {
            m_Console.Submit(command);
            return true;
        }
    }
    return false;
}

bool DebugSuite::HandleChar(char ch) {
    if (!m_Attached || !m_Console.open()) return false;
    // The two keys that open the console also produce a WM_CHAR. Swallowing
    // backtick outright is simpler than tracking "was this char produced by
    // the keystroke that just opened me", and costs nothing: no console
    // command needs a backtick.
    if (ch == '`' || ch == '~') return true;
    return m_Console.HandleChar(ch);
}

// ----------------------------------------------------------------- per tick

void DebugSuite::BeginTick(double frameSeconds) {
    if (!m_Attached) return;
    Metrics::Get().BeginFrame(frameSeconds);
    // Queued commands run here, inside the tick, not from the window
    // procedure that queued them -- see debug_console.h.
    if (m_Console.hasQueuedCommands()) m_Console.Drain();
    if (m_Host) m_Host->HostTick();
}

void DebugSuite::Render(sk::OverlaySurface& surface) {
    if (!m_Attached || !m_Host) return;
    ScopedTimer timer("debug.overlay");
    // The console occupies the top of the screen when open, so the stat
    // panels start below it rather than under it.
    const float consoleFraction = 0.55f;
    const int consoleBottom =
        m_Console.open() ? static_cast<int>(static_cast<float>(surface.height()) * consoleFraction)
                          : 0;
    m_Overlay.Render(surface, *m_Host, m_Console, consoleBottom);
    m_Console.Render(surface, consoleFraction);
}

void DebugSuite::Note(const std::string& category, std::string text) {
    if (!m_Attached) return;
    Log(category, std::move(text));
}

}  // namespace sk_debug
