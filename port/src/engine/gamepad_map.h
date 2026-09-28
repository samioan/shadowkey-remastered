#pragma once
#include <array>
#include <optional>

#include "engine/input_state.h"

namespace sk {

// M119: controller support -- which N-Gage key each pad control presses.
//
// A pad is a *second keyboard*, in the only sense the engine has: it presses
// and releases the same 21 InputState slots engine/pc_key_map.h maps PC keys
// onto. Nothing downstream learns a pad exists, so every screen, every
// script and the key-configuration menu behave exactly as they do from the
// keyboard.
//
// The layout is a modern first-person one, not a copy of the N-Gage keypad:
//
//     control          in the 3D view                 in a menu
//     ---------------  -----------------------------  -----------------
//     left stick       walk forward/back, sidestep    move the selection
//     right stick      turn left/right, look up/down  move the selection
//     d-pad            the N-Gage d-pad: walk / turn  move the selection
//     A                Use (doors, people, pickups)   confirm
//     B                open the menu                  back
//     Start            open the menu                  back
//     X                character manager              character manager
//     Y                jump                           jump
//     RT / LT          right / left hand              right / left hand
//     RB / LB          cycle right / left hand queue  (same)
//     Back (View)      map                            map
//
// Everything that is an *action* (walk, turn, use, jump, the hands, the
// queues, the map, the character manager) goes through the action table,
// so it follows the player's own bindings: rebind Use to Key 6 in Customize
// Controls and A still uses. The d-pad and the menu directions are the
// N-Gage's own d-pad slots, which is what the menus read. A and B/Start in
// a menu are the two softkeys, which are not rebindable in the engine
// either.
//
// **A held control keeps the key it pressed.** The slot is chosen on the
// press and released on the release, never re-chosen in between. Without
// that, holding A to open a door that starts a conversation would, the
// moment the conversation opened, turn into a held *confirm* -- and pick
// the first dialogue row before the player had read it (the sister port hit
// exactly this: tes-travels-decomp's "a held button closing the screen it
// just opened").
enum class PadControl : int {
    DpadUp, DpadDown, DpadLeft, DpadRight,
    A, B, X, Y,
    LB, RB, LT, RT,
    Back, Start, L3, R3,
    LStickUp, LStickDown, LStickLeft, LStickRight,
    RStickUp, RStickDown, RStickLeft, RStickRight,
    kCount,
};
constexpr int kPadControlCount = static_cast<int>(PadControl::kCount);

// One poll of a pad: which controls are held (sticks and triggers already
// folded to digital by the reader).
struct PadFrame {
    std::array<bool, kPadControlCount> held{};
    bool connected = false;
};

// The slot `control` presses right now. `gameplay` is true while the 3D view
// owns the input, false while any screen is up.
inline std::optional<ButtonSlot> PadControlSlot(PadControl control, bool gameplay,
                                                const InputState& input) {
    auto bound = [&](Action action) -> std::optional<ButtonSlot> {
        const int slot = input.binding(action);
        if (slot < 0 || slot >= static_cast<int>(ButtonSlot::kCount)) return std::nullopt;
        return static_cast<ButtonSlot>(slot);
    };
    switch (control) {
        case PadControl::DpadUp: return ButtonSlot::Up;
        case PadControl::DpadDown: return ButtonSlot::Down;
        case PadControl::DpadLeft: return ButtonSlot::Left;
        case PadControl::DpadRight: return ButtonSlot::Right;

        case PadControl::LStickUp: return gameplay ? bound(Action::MoveForward) : ButtonSlot::Up;
        case PadControl::LStickDown:
            return gameplay ? bound(Action::MoveBackward) : ButtonSlot::Down;
        case PadControl::LStickLeft:
            return gameplay ? bound(Action::SideStepLeft) : ButtonSlot::Left;
        case PadControl::LStickRight:
            return gameplay ? bound(Action::SideStepRight) : ButtonSlot::Right;
        case PadControl::RStickUp: return gameplay ? bound(Action::LookUp) : ButtonSlot::Up;
        case PadControl::RStickDown: return gameplay ? bound(Action::LookDown) : ButtonSlot::Down;
        case PadControl::RStickLeft: return gameplay ? bound(Action::TurnLeft) : ButtonSlot::Left;
        case PadControl::RStickRight:
            return gameplay ? bound(Action::TurnRight) : ButtonSlot::Right;

        case PadControl::A: return gameplay ? bound(Action::Use) : ButtonSlot::LeftSelectionKey;
        case PadControl::B:
        case PadControl::Start: return ButtonSlot::RightSelectionKey;
        case PadControl::X: return bound(Action::CharacterManager);
        case PadControl::Y: return bound(Action::Jump);
        case PadControl::RT: return bound(Action::UseRightAction);
        case PadControl::LT: return bound(Action::UseLeftAction);
        case PadControl::RB: return bound(Action::CycleRightQueue);
        case PadControl::LB: return bound(Action::CycleLeftQueue);
        case PadControl::Back: return bound(Action::MapToggle);

        case PadControl::L3:
        case PadControl::R3:
        case PadControl::kCount: break;
    }
    return std::nullopt;
}

// Turns successive PadFrames into slot presses and releases.
class GamepadMapper {
public:
    // Applies one poll. `setSlot(slot, padHeld)` is called for each slot
    // whose pad-held state changes -- the caller ORs that with whatever the
    // keyboard is holding on the same slot, so a key and a button on one
    // slot never release each other.
    template <typename SetSlot>
    void Update(const PadFrame& frame, bool gameplay, const InputState& input, SetSlot&& setSlot) {
        for (int c = 0; c < kPadControlCount; ++c) {
            const bool held = frame.connected && frame.held[static_cast<size_t>(c)];
            const bool was = latched_[static_cast<size_t>(c)] >= 0;
            if (held == was) continue;
            if (held) {
                const auto slot = PadControlSlot(static_cast<PadControl>(c), gameplay, input);
                if (!slot) continue;  // an unmapped control stays unlatched
                const int s = static_cast<int>(*slot);
                latched_[static_cast<size_t>(c)] = s;
                if (count_[static_cast<size_t>(s)]++ == 0) setSlot(*slot, true);
            } else {
                Release(c, setSlot);
            }
        }
    }

    // Lets go of everything (the window lost focus, or the pad vanished).
    template <typename SetSlot>
    void ReleaseAll(SetSlot&& setSlot) {
        for (int c = 0; c < kPadControlCount; ++c) {
            if (latched_[static_cast<size_t>(c)] >= 0) Release(c, setSlot);
        }
    }

    bool SlotHeld(ButtonSlot slot) const { return count_[static_cast<size_t>(slot)] > 0; }

private:
    template <typename SetSlot>
    void Release(int c, SetSlot& setSlot) {
        const int s = latched_[static_cast<size_t>(c)];
        latched_[static_cast<size_t>(c)] = -1;
        if (--count_[static_cast<size_t>(s)] == 0) setSlot(static_cast<ButtonSlot>(s), false);
    }

    std::array<int, kPadControlCount> latched_ = MakeUnlatched();
    std::array<int, static_cast<size_t>(ButtonSlot::kCount)> count_{};

    static std::array<int, kPadControlCount> MakeUnlatched() {
        std::array<int, kPadControlCount> a{};
        a.fill(-1);
        return a;
    }
};

}  // namespace sk
