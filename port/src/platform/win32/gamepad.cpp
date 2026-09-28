#include "platform/win32/gamepad.h"

#include <windows.h>

#include <initializer_list>

namespace sk {

namespace {

// The part of XINPUT_STATE this reads, declared here so the build needs
// neither xinput.h nor an import library.
struct XGamepad {
    WORD wButtons;
    BYTE bLeftTrigger, bRightTrigger;
    SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY;
};
struct XState {
    DWORD dwPacketNumber;
    XGamepad Gamepad;
};
using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XState*);

enum : WORD {
    kDpadUp = 0x0001,
    kDpadDown = 0x0002,
    kDpadLeft = 0x0004,
    kDpadRight = 0x0008,
    kStart = 0x0010,
    kBack = 0x0020,
    kL3 = 0x0040,
    kR3 = 0x0080,
    kLShoulder = 0x0100,
    kRShoulder = 0x0200,
    kA = 0x1000,
    kB = 0x2000,
    kX = 0x4000,
    kY = 0x8000,
};

// Past stick drift and wear: a little under a third of full travel. The
// sticks are digital here, so the only job is to not walk on its own.
constexpr SHORT kStickDeadzone = 10000;
// XInput's own documented trigger threshold (XINPUT_GAMEPAD_TRIGGER_THRESHOLD).
constexpr BYTE kTriggerThreshold = 30;

}  // namespace

Gamepad::Gamepad() {
    for (const wchar_t* dll : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"}) {
        if (HMODULE m = LoadLibraryW(dll)) {
            getState_ = reinterpret_cast<void*>(GetProcAddress(m, "XInputGetState"));
            if (getState_) break;
        }
    }
}

PadFrame Gamepad::Poll() {
    PadFrame out;
    if (!getState_) return out;
    auto get = reinterpret_cast<XInputGetStateFn>(getState_);
    XState st{};
    bool ok = false;
    if (padIndex_ >= 0) {
        ok = get(static_cast<DWORD>(padIndex_), &st) == ERROR_SUCCESS;
        if (!ok) padIndex_ = -1;
    }
    if (!ok) {
        const std::int64_t now = static_cast<std::int64_t>(GetTickCount64());
        if (now - lastRescanMs_ < 1000) return out;
        lastRescanMs_ = now;
        for (int i = 0; i < 4 && !ok; i++) {
            if (get(static_cast<DWORD>(i), &st) == ERROR_SUCCESS) {
                ok = true;
                padIndex_ = i;
            }
        }
        if (!ok) return out;
    }

    out.connected = true;
    const WORD b = st.Gamepad.wButtons;
    auto set = [&](PadControl c, bool held) { out.held[static_cast<int>(c)] = held; };
    set(PadControl::DpadUp, (b & kDpadUp) != 0);
    set(PadControl::DpadDown, (b & kDpadDown) != 0);
    set(PadControl::DpadLeft, (b & kDpadLeft) != 0);
    set(PadControl::DpadRight, (b & kDpadRight) != 0);
    set(PadControl::A, (b & kA) != 0);
    set(PadControl::B, (b & kB) != 0);
    set(PadControl::X, (b & kX) != 0);
    set(PadControl::Y, (b & kY) != 0);
    set(PadControl::LB, (b & kLShoulder) != 0);
    set(PadControl::RB, (b & kRShoulder) != 0);
    set(PadControl::LT, st.Gamepad.bLeftTrigger > kTriggerThreshold);
    set(PadControl::RT, st.Gamepad.bRightTrigger > kTriggerThreshold);
    set(PadControl::Back, (b & kBack) != 0);
    set(PadControl::Start, (b & kStart) != 0);
    set(PadControl::L3, (b & kL3) != 0);
    set(PadControl::R3, (b & kR3) != 0);
    set(PadControl::LStickUp, st.Gamepad.sThumbLY > kStickDeadzone);
    set(PadControl::LStickDown, st.Gamepad.sThumbLY < -kStickDeadzone);
    set(PadControl::LStickLeft, st.Gamepad.sThumbLX < -kStickDeadzone);
    set(PadControl::LStickRight, st.Gamepad.sThumbLX > kStickDeadzone);
    set(PadControl::RStickUp, st.Gamepad.sThumbRY > kStickDeadzone);
    set(PadControl::RStickDown, st.Gamepad.sThumbRY < -kStickDeadzone);
    set(PadControl::RStickLeft, st.Gamepad.sThumbRX < -kStickDeadzone);
    set(PadControl::RStickRight, st.Gamepad.sThumbRX > kStickDeadzone);
    return out;
}

}  // namespace sk
