#pragma once

// M36: an unhandled-exception filter that writes the fault and a
// symbolized call stack to stdout -- which console_tee.h is already
// duplicating into shadowkey_port.log, so a crash leaves a diagnosable
// record next to the session that produced it.
//
// Added because a reported hard crash ("approaching an enemy, about to
// attack") could not be reproduced from outside: the game reaches its
// zone through a character-creation flow that synthetic input could not
// drive, and a C++ access violation otherwise leaves nothing behind at
// all. Reading the code found the other reported faults but not this one,
// so the honest next step is to make the crash report itself.
//
// Symbol names come from the .pdb the build already emits next to the
// executable; without it the log still gets module-relative addresses,
// which a later `dumpbin`/map lookup can resolve.

namespace sk {

// Installs the filter. Call once, as early in main() as possible (right
// after the console tee, so its output is captured). Safe to call when no
// debugger and no symbols are present.
void InstallCrashReporter();

}  // namespace sk
