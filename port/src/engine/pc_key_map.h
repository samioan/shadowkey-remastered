#pragma once
#include <optional>

#include <windows.h>

#include "engine/input_state.h"

// Maps PC keyboard virtual-key codes to the original N-Gage ButtonSlot
// scheme (docs/INPUT_HANDLING.md), so InputState's binding layer -- and
// therefore the default action scheme it's seeded with -- stays identical
// to the original. Only the physical-key side changes: arrow keys for the
// D-pad, top-row digits for the numeric keypad (the N-Gage's primary
// control surface per that doc), '-'/'=' as an arbitrary-but-documented
// stand-in for '*'/'#' (no direct PC equivalent) -- all trivially
// rebindable later via InputState::Rebind, this is just the initial
// default.
namespace sk {

inline std::optional<ButtonSlot> MapPcKeyToButtonSlot(int vkCode) {
    switch (vkCode) {
        case VK_LEFT: return ButtonSlot::Left;
        case VK_RIGHT: return ButtonSlot::Right;
        case VK_UP: return ButtonSlot::Up;
        case VK_DOWN: return ButtonSlot::Down;
        case '1': return ButtonSlot::Key1;
        case '2': return ButtonSlot::Key2;
        case '3': return ButtonSlot::Key3;
        case '4': return ButtonSlot::Key4;
        case '5': return ButtonSlot::Key5;
        case '6': return ButtonSlot::Key6;
        case '7': return ButtonSlot::Key7;
        case '8': return ButtonSlot::Key8;
        case '9': return ButtonSlot::Key9;
        case '0': return ButtonSlot::Key0;
        case VK_OEM_MINUS: return ButtonSlot::KeyStar;  // '-' stands in for '*'
        case VK_OEM_PLUS: return ButtonSlot::KeyHash;   // '=' stands in for '#'
        // The N-Gage's two softkeys sit either side of the D-pad and drive
        // context-sensitive menu actions -- mainmenu.s's own
        // OnRightSoftkey handler (which backs out to the quit-confirm
        // popup) confirms RightSelectionKey is "back/cancel". There's no
        // equally direct evidence pinning down which physical key is
        // "confirm" on this device (see INPUT_HANDLING.md's open items on
        // the untraced "Select" logical action) -- Enter/Escape are a
        // reasonable, clearly-a-guess PC stand-in pending that RE pass.
        case VK_RETURN: return ButtonSlot::LeftSelectionKey;
        case VK_ESCAPE: return ButtonSlot::RightSelectionKey;
        default: return std::nullopt;
    }
}

}  // namespace sk
