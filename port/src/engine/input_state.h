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

    // M90: drop every *pending* edge and repeat, but leave the held keys
    // held. For the moment a menu takes over from live gameplay: the
    // player is normally holding a direction key when they walk into
    // whatever opened it, and the port's menu navigation reads auto-repeat
    // (ConsumeJustPressedOrRepeat, see TickRepeats()) as well as edges, so
    // the still-held key drove the new screen the instant it appeared.
    //
    // The engine cannot have this problem: its own menu tick
    // (FUN_10075bdc) tests `GetButton2(slot) && !GetButtonPrev(slot)`
    // throughout -- edges only, never a held key -- and its open-by-name
    // (FUN_100779b8) clears two input-ish flags on the way in besides.
    // Auto-repeat in menus is this port's own convenience, so this is the
    // narrowest place to keep it from leaking across the handover.
    //
    // Cheap and visible where it mattered: `levelconfirm.s` puts "No"
    // first precisely so that a stray confirm does nothing, and walking
    // into a doorway with the forward key held moved the highlight to
    // "Yes" before the player had seen the screen.
    void ClearPendingEdges() {
        justPressed_.fill(false);
        repeat_.fill(false);
        holdTicks_.fill(0);
    }

    // M99: `ConfigKeysDefault` (GameEngine binding 0x11) is one call,
    // `FUN_1001a220(engine + 0x488)` -- the very function that seeds the
    // table at startup, run again. It makes sixteen `FUN_1001a784(input,
    // action, slot, nameId)` stores and touches nothing else, so unlike the
    // constructor's fill it leaves a 17th action's slot alone. Nothing binds
    // one, so the two agree in practice.
    void RestoreDefaultBindings() {
        AssignDefaultBindings();
    }

    // M99: `FUN_1001a610`, what the key-capture screen does with the key it
    // caught. The action takes the new slot, and the first *other* action
    // (of the first sixteen) already on that slot is given the action's old
    // one -- a swap, so two actions never share a key and none is left
    // unbound:
    //
    //     if (action < 0x11) {
    //         old = map[action];  map[action] = slot;
    //         for (i = 0; i < 0x10; i++)
    //             if (i != action && map[i] == slot) { map[i] = old; return; }
    //     }
    void RedefineBinding(Action action, ButtonSlot slot) {
        const int a = static_cast<int>(action);
        if (a < 0 || a > 0x10) return;
        const int old = bindingOffset_[static_cast<size_t>(a)];
        bindingOffset_[static_cast<size_t>(a)] = static_cast<int>(slot);
        for (int i = 0; i < 0x10; ++i) {
            if (i != a && bindingOffset_[static_cast<size_t>(i)] == static_cast<int>(slot)) {
                bindingOffset_[static_cast<size_t>(i)] = old;
                return;
            }
        }
    }

    // M99: the per-slot byte at `engine+0x548` (`InputState+0xc0`), the
    // flag argument of `InputState_RegisterBinding` -- 1 for the sixteen
    // keypad/d-pad slots, 0 for the two selection keys and the unused tail.
    // The capture loop only accepts a slot whose flag is set, so a softkey
    // can never be bound to an action.
    static bool slotIsBindable(ButtonSlot slot) {
        const int i = static_cast<int>(slot);
        return i >= 0 && i <= 15;
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

    // M80: the *other* half of the binding table -- the per-slot key-name
    // resource id (`InputState+0x6c`, 21 entries), which
    // `InputState_InitDefaultBindings` fills in with one
    // `InputState_RegisterBinding(this, slot, id, flag)` call per slot:
    //
    //     slots 0x00..0x0f -> 0xd05 + slot, flag 1  (the 16 real keys)
    //     slot  0x10       -> 0xd15,        flag 0  ("Left Selection Key")
    //     slot  0x11       -> 0xd16,        flag 0  ("Right Selection Key")
    //     slot  0x12       -> 0xd15,        flag 0  (reuses the same label)
    //     slot  0x13       -> never registered at all -- the documented gap
    //     slot  0x14       -> 0xd15,        flag 0
    //
    // 0xd05 + slot lands exactly on this enum's own order, which is the
    // engine's scan-code table order: 0xd05 "Left", 0xd06 "Right", 0xd07
    // "Up", 0xd08 "Down", then 0xd09.."Key 1" through 0xd14 "Key #".
    // Nothing read this before because nothing needed to *name* a key;
    // `ParseActionText` does (simkin_bindings/action_text.h).
    // Returns -1 for the unregistered slot 19.
    static int slotNameStringId(ButtonSlot slot) {
        const int i = static_cast<int>(slot);
        if (i < 0 || i >= static_cast<int>(ButtonSlot::kCount)) return -1;
        if (i <= 17) return 0xd05 + i;
        if (i == 19) return -1;  // never registered
        return 0xd15;            // slots 18 and 20 reuse "Left Selection Key"
    }

    // M99: an action's own name -- `InputState+0xd8[action]`, the fourth
    // argument of each of FUN_1001a220's stores, which FUN_1001a5c4 resolves
    // for the key-configuration rows. -1 for anything past the sixteenth.
    static int actionNameStringId(Action action) {
        static constexpr int kNames[16] = {0xced, 0xcee, 0xcef, 0xcf0, 0xcf1, 0xcf4,
                                           0xcf2, 0xcf3, 0xcf6, 0xd04, 0xcfd, 0xcfe,
                                           0xcff, 0xd01, 0xd02, 0xd03};
        const int i = static_cast<int>(action);
        return i >= 0 && i < 16 ? kNames[i] : -1;
    }

    // The name of whatever key `action` is bound to *right now* -- the
    // engine's own `FUN_1001a578(out, input, ResolveBindingOffset(input,
    // action))`, which resolves the action through the remappable table
    // and then reads that physical slot's label id. -1 when unbound.
    int bindingNameStringId(Action action) const {
        const int slot = binding(action);
        if (slot < 0) return -1;
        return slotNameStringId(static_cast<ButtonSlot>(slot));
    }

private:
    void InitDefaultBindings() {
        bindingOffset_.fill(-1);
        AssignDefaultBindings();
    }
    // FUN_1001a220's sixteen stores.
    void AssignDefaultBindings() {
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
