#pragma once

#include <string>

namespace sk {

// Directory containing the running executable (no trailing slash), e.g.
// "C:\...\shadowkey-decomp\port\build" -- used to resolve this port's
// default asset paths (scriptRoot, the .gdr font) relative to where the
// binary actually lives rather than the process's current working
// directory, which varies by launch method (double-click in Explorer
// sets CWD to the exe's own folder; running it from a shell keeps
// whatever CWD the shell was already in -- e.g. the repo root, or
// port/build). Falls back to "." if GetModuleFileNameA fails.
std::string ExecutableDirectory();

}  // namespace sk
