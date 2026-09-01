#pragma once
#include <array>
#include <cstdint>

// Mirrors the original engine's InputState (engine+0x488, see
// docs/INPUT_HANDLING.md): a flat 21-slot current/previous button array
// (edge detection) plus a 17-entry logical-action -> slot indirection table
// (the remappable binding layer real gameplay code and script bindings like
// ConfigKeysMenu/ConfigKeysDefault go through), seeded with the game's own
// verified default control scheme.
namespace sk {

// The 21 raw button slots, in the game's own scan-code table order
// (docs/INPUT_HANDLING.md's "scan code -> action slot" table). Slots 18/19
// are gaps in the original (18 reuses the "Left Selection Key" label, 19
// was never registered at all) -- kept here only as placeholders so the
// array size/indices match the real engine exactly.
enum class ButtonSlot : int {
    Left = 0, Right = 1, Up = 2, Down = 3,
    Key1 = 4, Key2 = 5, Key3 = 6, Key4 = 7, Key5 = 8,
    Key6 = 9, Key7 = 10, Key8 = 11, Key9 = 12, Key0 = 13,
    KeyStar = 14, KeyHash = 15,
    LeftSelectionKey = 16, RightSelectionKey = 17,
    Slot18Unused = 18, Slot19Unused = 19, Slot20Unused = 20,
    kCount = 21,
};

// The 16 core remappable gameplay actions with a known default binding
// (docs/INPUT_HANDLING.md's "full default control scheme" table). The real
// bindingOffset table has 17 entries (0x11) but only these 16 have a
// documented default -- the 17th slot's role wasn't pinned down there
// either, left unbound here to match that same open item honestly rather
// than guessing.
enum class Action : int {
    MoveForward = 0, MoveBackward = 1, TurnLeft = 2, TurnRight = 3,
    Jump = 4, LookUp = 5, Use = 6, SideStepLeft = 7,
    UseRightAction = 8, SideStepRight = 9, UseLeftAction = 10, LookDown = 11,
    MapToggle = 12, CycleRightQueue = 13, CycleLeftQueue = 14, CharacterManager = 15,
    kCount = 17,  // matches the real 17-entry bindingOffset table
};

class InputState {
public:
    InputState() { InitDefaultBindings(); }

    // No-op now -- kept so existing call sites (main.cpp's tick handler)
    // don't need to change. See SetButton()'s comment for why edge
    // detection moved off a per-tick current/previous snapshot.
    void BeginFrame() {}

    void SetButton(ButtonSlot slot, bool down) {
        size_t i = static_cast<size_t>(slot);
        // Latches on the actual 0->1 transition, at the moment the OS
        // key event arrives -- NOT by diffing this tick's state against
        // last tick's. The window's message pump (window.cpp's
        // RunMessageLoop) drains + dispatches WM_KEYDOWN/UP continuously,
        // independent of the ~40ms tick cadence (game_clock.h), so a
        // press-then-release can both land (via key auto-repeat or just
        // bad luck) *before* the next tick's snapshot ever runs --
        // diffing current-vs-previous at tick boundaries would then see
        // current_==previous_==true and silently miss the edge. Latching
        // in SetButton (called directly from the key callback) instead
        // of at tick time removes that race entirely; ConsumeJustPressed
        // clears the latch once the tick that reads it has acted on it.
        if (down && !current_[i]) justPressed_[i] = true;
        current_[i] = down;
    }

    bool GetButton(ButtonSlot slot) const { return current_[static_cast<size_t>(slot)]; }
    // Consumes (clears) the latch -- call at most once per slot per tick.
    bool ConsumeJustPressed(ButtonSlot slot) {
        size_t i = static_cast<size_t>(slot);
        bool wasPressed = justPressed_[i];
        justPressed_[i] = false;
        return wasPressed;
    }

    // Goes through the remappable binding indirection, exactly like the
    // real engine's InputState_GetBoundButton/_Prev -- gameplay/UI code
    // should call these, not the raw slot accessors above, so a future
    // "customize controls" screen (per the game's own ConfigKeysMenu) can
    // rebind Action -> ButtonSlot without touching call sites.
    bool GetBoundButton(Action action) const {
        int slot = bindingOffset_[static_cast<size_t>(action)];
        return slot >= 0 && GetButton(static_cast<ButtonSlot>(slot));
    }
    bool ConsumeBoundJustPressed(Action action) {
        int slot = bindingOffset_[static_cast<size_t>(action)];
        return slot >= 0 && ConsumeJustPressed(static_cast<ButtonSlot>(slot));
    }

    void Rebind(Action action, ButtonSlot slot) {
        bindingOffset_[static_cast<size_t>(action)] = static_cast<int>(slot);
    }

private:
    void InitDefaultBindings() {
        bindingOffset_.fill(-1);
        Rebind(Action::MoveForward, ButtonSlot::Up);
        Rebind(Action::MoveBackward, ButtonSlot::Down);
        Rebind(Action::TurnLeft, ButtonSlot::Left);
        Rebind(Action::TurnRight, ButtonSlot::Right);
        Rebind(Action::Jump, ButtonSlot::Key1);
        Rebind(Action::LookUp, ButtonSlot::Key2);
        Rebind(Action::Use, ButtonSlot::Key3);
        Rebind(Action::SideStepLeft, ButtonSlot::Key4);
        Rebind(Action::UseRightAction, ButtonSlot::Key5);
        Rebind(Action::SideStepRight, ButtonSlot::Key6);
        Rebind(Action::UseLeftAction, ButtonSlot::Key7);
        Rebind(Action::LookDown, ButtonSlot::Key8);
        Rebind(Action::MapToggle, ButtonSlot::Key9);
        Rebind(Action::CycleRightQueue, ButtonSlot::Key0);
        Rebind(Action::CycleLeftQueue, ButtonSlot::KeyStar);
        Rebind(Action::CharacterManager, ButtonSlot::KeyHash);
    }

    std::array<bool, static_cast<size_t>(ButtonSlot::kCount)> current_{};
    std::array<bool, static_cast<size_t>(ButtonSlot::kCount)> justPressed_{};
    std::array<int, static_cast<size_t>(Action::kCount)> bindingOffset_{};
};

}  // namespace sk
