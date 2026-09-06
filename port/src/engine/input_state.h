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
// M57: **these are now the engine's own action indices.** They used to be
// this port's own numbering, taken from the order
// docs/INPUT_HANDLING.md's table lists the actions in -- which is the
// order of their name resource ids, not the order the engine assigns.
// `FUN_1001a220` gives the real assignment directly, one call per action:
//
//     FUN_1001a784(input, actionIndex, keySlot, nameResourceId)
//
// The (action, default key) *pairs* the doc records were all correct; what
// was wrong was which integer each action is. It mattered as soon as
// something had to be read out of the binary by index -- the map toggle is
// polled as literal action 9 in `FUN_1001c9c0`, which under the old
// numbering read as "Side Step Right". Cross-checked against every branch
// of that same input poll: 0/1 and 6/7 are the four translation moves,
// 2/3 the two turns sharing a release call, 4/5 the two looks sharing
// another, and 8..15 the edge-triggered actions.
enum class Action : int {
    MoveForward = 0,      // 0xced, Up
    MoveBackward = 1,     // 0xcee, Down
    TurnLeft = 2,         // 0xcef, Left
    TurnRight = 3,        // 0xcf0, Right
    LookUp = 4,           // 0xcf1, Key 2
    LookDown = 5,         // 0xcf4, Key 8
    SideStepLeft = 6,     // 0xcf2, Key 4
    SideStepRight = 7,    // 0xcf3, Key 6
    Jump = 8,             // 0xcf6, Key 1
    MapToggle = 9,        // 0xd04, Key 9
    CycleRightQueue = 10, // 0xcfd, Key 0
    CycleLeftQueue = 11,  // 0xcfe, Key *
    CharacterManager = 12,// 0xcff, Key #
    Use = 13,             // 0xd01, Key 3
    UseLeftAction = 14,   // 0xd02, Key 7
    UseRightAction = 15,  // 0xd03, Key 5
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

    // Call exactly once per game tick, before any Consume* call. Drives
    // key auto-repeat for held buttons.
    //
    // Why this exists: Windows does send repeated WM_KEYDOWN messages
    // while a key is held, but SetButton() deliberately only latches the
    // 0->1 edge, so a held direction key produced exactly one menu move
    // and then nothing. Menus were effectively tap-only -- moving several
    // rows meant several separate presses. Gameplay actions must NOT
    // repeat (holding the attack key shouldn't machine-gun), so repeat is
    // exposed as a *separate* query (ConsumeJustPressedOrRepeat) that only
    // menu/list navigation calls; ConsumeJustPressed keeps its exact old
    // edge-only meaning everywhere else.
    void TickRepeats() {
        for (size_t i = 0; i < current_.size(); ++i) {
            if (!current_[i]) {
                holdTicks_[i] = 0;
                repeat_[i] = false;
                continue;
            }
            ++holdTicks_[i];
            if (holdTicks_[i] >= kRepeatDelayTicks &&
                (holdTicks_[i] - kRepeatDelayTicks) % kRepeatPeriodTicks == 0) {
                repeat_[i] = true;
            }
        }
    }

    // Edge OR auto-repeat -- for menu/list navigation only, see
    // TickRepeats().
    bool ConsumeJustPressedOrRepeat(ButtonSlot slot) {
        size_t i = static_cast<size_t>(slot);
        bool fired = justPressed_[i] || repeat_[i];
        justPressed_[i] = false;
        repeat_[i] = false;
        return fired;
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

    // M69: drop every held key, without firing anything. For the host's
    // focus-lost path: alt-tabbing away means the key-up for whatever is
    // held will never arrive, and the slot would stay latched down forever
    // (the game keeps walking forward while you are in another window).
    // Deliberately clears the just-pressed latches too -- a press the
    // player made before leaving should not fire on their return.
    void ReleaseAll() {
        current_.fill(false);
        justPressed_.fill(false);
        repeat_.fill(false);
        holdTicks_.fill(0);
    }

    void Rebind(Action action, ButtonSlot slot) {
        bindingOffset_[static_cast<size_t>(action)] = static_cast<int>(slot);
    }
    // The raw table entry, -1 when unbound. This is exactly what the real
    // `SaveConfig` writes into `dragonstar.set`'s ACTIONMAP block and what
    // its loader feeds back through Rebind (assets/game_config.h).
    int binding(Action action) const {
        return bindingOffset_[static_cast<size_t>(action)];
    }
    void RebindRaw(Action action, int slot) {
        bindingOffset_[static_cast<size_t>(action)] = slot;
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

    // At the fixed 40ms tick (engine/game_clock.h, matching the real
    // engine's CPeriodic -- docs/RENDER_LOOP.md): ~320ms before the first
    // repeat, then one every ~120ms. Ordinary UI feel; no original value
    // was recovered for this, and the real device's own key repeat was a
    // Symbian setting rather than something the game shipped.
    static constexpr int kRepeatDelayTicks = 8;
    static constexpr int kRepeatPeriodTicks = 3;

    std::array<bool, static_cast<size_t>(ButtonSlot::kCount)> current_{};
    std::array<bool, static_cast<size_t>(ButtonSlot::kCount)> justPressed_{};
    std::array<bool, static_cast<size_t>(ButtonSlot::kCount)> repeat_{};
    std::array<int, static_cast<size_t>(ButtonSlot::kCount)> holdTicks_{};
    std::array<int, static_cast<size_t>(Action::kCount)> bindingOffset_{};
};

}  // namespace sk
