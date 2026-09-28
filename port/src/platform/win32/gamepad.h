#pragma once
#include <cstdint>

#include "engine/gamepad_map.h"

namespace sk {

// M119: the first connected XInput controller (Xbox-style pads, and most
// others through Steam Input or their own XInput mode), read once per loop
// iteration. The same reader the sister ports use
// (tes-travels-decomp/stormhold/port/src/platform/win32/gamepad.h), with the
// sticks and triggers folded into digital controls here rather than by the
// caller: every action a pad reaches in this game is an N-Gage key, and an
// N-Gage key is either down or up.
//
// What each control does is engine/gamepad_map.h's business, not this
// file's -- this only answers "which controls are held right now".
class Gamepad {
public:
    Gamepad();

    // XInputGetState on the pad found last time, or a rescan of all four
    // slots at most once a second while none is connected (polling an empty
    // slot is slow enough to matter at 1000 polls a second).
    PadFrame Poll();

private:
    int padIndex_ = -1;
    std::int64_t lastRescanMs_ = 0;
    // XInputGetState, loaded by name so the exe runs on a machine with no
    // XInput DLL at all -- it simply never sees a pad.
    void* getState_ = nullptr;
};

}  // namespace sk
