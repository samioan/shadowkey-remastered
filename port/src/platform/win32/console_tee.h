#pragma once

// Duplicates this process's own stdout/stderr so everything the console
// window already shows (every std::printf/std::fprintf(stderr, ...) call
// site across this whole codebase -- action logs, "SoftFailNativeCall"/
// "not implemented" traces, load errors, ...) also lands in a plain text
// log file, unchanged and at no cost to any existing call site. Requested
// by the user specifically to review a play session's unimplemented-call
// traces without having to copy/paste the live console.
//
// Implemented as a real OS-level tee (an anonymous pipe plus one small
// background reader thread that fans each chunk out to both the original
// console handle and the log file) rather than simply `freopen`-ing
// stdout to the file, so the live console keeps working exactly as
// before -- the alternative (redirecting stdout outright) would silence
// the console window the user already watches during play.

#include <string>

namespace sk {

// Call once, as early as possible in main() (before any other output --
// anything printed before this call only reaches the console, not the
// log). Returns false (logged to the console, not fatal) if setup fails
// for any reason -- console-only output keeps working either way, same
// "optional, non-critical subsystem" tolerance as everything else this
// port treats as non-fatal.
bool StartConsoleTeeLog(const std::string& logPath);

}  // namespace sk
