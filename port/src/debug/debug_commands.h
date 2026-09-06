#pragma once

// M68: registers the game-facing command set onto a DebugConsole.
//
// Split out from debug_console.cpp so the console stays a generic component
// with no knowledge of Shadowkey, and from debug_suite.cpp so the command
// table -- the part that grows every time a new thing needs debugging -- is
// one file you can read top to bottom.

#include <string>

namespace sk_debug {

class DebugConsole;
class DebugHost;
class DebugOverlay;
class ScriptTracer;

struct CommandContext {
    DebugConsole* console = nullptr;
    DebugHost* host = nullptr;
    DebugOverlay* overlay = nullptr;
    ScriptTracer* tracer = nullptr;
    // Where `exec` looks for a .cfg of console commands, and where the
    // startup autoexec is read from. Set by DebugSuite.
    std::string configDirectory;
    // Set true by the `quit` command; DebugSuite reports it to the host.
    bool quitRequested = false;
};

// Registers every command. `context` must outlive the console.
void RegisterDebugCommands(CommandContext& context);

}  // namespace sk_debug
