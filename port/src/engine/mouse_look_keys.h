#pragma once
#include "engine/input_state.h"

namespace sk {

// M126: with mouse look on, the mouse turns -- so the keys that turn
// (A / D by default: the D-pad's Left / Right, bound to TurnLeft /
// TurnRight) sidestep instead, the layout every mouse-look game uses.
//
// Applied to the *keyboard* only, at the moment a key is pressed: the slot
// a PC key would press is swapped for the one SideStepLeft / SideStepRight
// is bound to. Going through the binding table means Customize Controls
// still decides what turning and sidestepping are; a controller's d-pad is
// not touched (it has a right stick to turn with). Outside mouse look --
// menus, the map, conversations, or the setting Off -- nothing changes, so
// A / D still move a menu's selection.
inline ButtonSlot MouseLookKeySlot(ButtonSlot slot, bool mouseLook, const InputState& input) {
    if (!mouseLook) return slot;
    auto swap = [&](Action from, Action to, ButtonSlot fallback) {
        const int target = input.binding(to);
        if (input.binding(from) != static_cast<int>(slot)) return fallback;
        if (target < 0 || target >= static_cast<int>(ButtonSlot::kCount)) return fallback;
        return static_cast<ButtonSlot>(target);
    };
    ButtonSlot out = swap(Action::TurnLeft, Action::SideStepLeft, slot);
    if (out != slot) return out;
    return swap(Action::TurnRight, Action::SideStepRight, slot);
}

}  // namespace sk
