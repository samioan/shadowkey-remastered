#pragma once
#include <optional>

#include <windows.h>

#include "engine/input_state.h"

// Maps PC keyboard virtual-key codes to the original N-Gage ButtonSlot
// scheme (docs/INPUT_HANDLING.md).
//
// **Only the physical-key side lives here.** The logical layer underneath
// -- InputState's action -> slot indirection table, seeded by
// InitDefaultBindings() from the game's own verified default control scheme
// -- is the engine's, is not a PC convention, and is untouched by anything
// in this file. Changing a key here changes which N-Gage button a PC key
// *is*, not what that button does.
//
// M69: the layout below is the user's own N-Gage-emulator binding sheet,
// adopted verbatim so muscle memory transfers between the emulator and this
// port. It replaces the M0 scaffold's placeholder scheme (arrow keys for the
// D-pad, top-row digits for the keypad, '-'/'=' standing in for '*'/'#'),
// which was only ever a documented guess -- see that scheme's own comment in
// the git history, which called the digits "arbitrary-but-documented".
//
// What each key ends up doing, once the engine's default action table is
// applied on top:
//
//     W A S D        D-pad          move forward / turn left /
//                                   move backward / turn right
//     arrow keys     keypad 2/4/6/8 look up-down, sidestep left-right
//     Space          keypad 1       jump
//     E              keypad 3       Use -- doors, NPCs, pickups
//     Q              keypad 7       swing the left-hand weapon
//     left mouse     keypad 5       swing the right-hand weapon
//     M              keypad 9       map
//     G              keypad 0       cycle the right-hand queue
//     C              keypad *       cycle the left-hand queue
//     Tab            keypad #       character manager
//     Return         middle softkey confirm  (menus, dialogue)
//     Esc            left/right     back / cancel
//                    softkey
//
// Two entries on the sheet deliberately map to nothing:
//
//   * **Green softkey (F3) and Red softkey (F4)** are the N-Gage's call and
//     end-call keys. The engine's input table is 21 slots and has no entry
//     for either (docs/INPUT_HANDLING.md: slots 18/19/20 are gaps -- 18
//     reuses the Left Selection Key label, 19 was never registered at all),
//     so there is no button here for them to be. Left unmapped rather than
//     invented, and kept clear of the debug suite too, which moved off F3/F4
//     for this scheme (src/debug/debug_suite.cpp).
//
//   * The sheet gives Esc as *both* the left and the right softkey. This
//     port distinguishes only two selection slots, and RightSelectionKey is
//     the one confirmed to mean "back/cancel" (mainmenu.s's own
//     OnRightSoftkey handler backs out to the quit-confirm popup), so Esc
//     goes there and Return -- the sheet's middle softkey, the D-pad centre,
//     which is what a device actually confirms with -- takes
//     LeftSelectionKey.
//
// Everything here remains rebindable at runtime through InputState::Rebind;
// this is the initial physical layout, not a constraint.
namespace sk {

inline std::optional<ButtonSlot> MapPcKeyToButtonSlot(int vkCode) {
    switch (vkCode) {
        // --- D-pad: WASD -------------------------------------------------
        case 'W': return ButtonSlot::Up;
        case 'A': return ButtonSlot::Left;
        case 'S': return ButtonSlot::Down;
        case 'D': return ButtonSlot::Right;

        // --- keypad ------------------------------------------------------
        case VK_SPACE: return ButtonSlot::Key1;
        case VK_UP: return ButtonSlot::Key2;
        case 'E': return ButtonSlot::Key3;
        case VK_LEFT: return ButtonSlot::Key4;
        // The one non-keyboard binding on the sheet. Delivered through this
        // same map as VK_LBUTTON, which WM_KEYDOWN can never produce, so the
        // mouse needs no callback of its own -- see window.cpp's
        // WM_LBUTTONDOWN handler.
        case VK_LBUTTON: return ButtonSlot::Key5;
        case VK_RIGHT: return ButtonSlot::Key6;
        case 'Q': return ButtonSlot::Key7;
        case VK_DOWN: return ButtonSlot::Key8;
        case 'M': return ButtonSlot::Key9;
        case 'G': return ButtonSlot::Key0;
        case 'C': return ButtonSlot::KeyStar;
        case VK_TAB: return ButtonSlot::KeyHash;

        // --- softkeys ----------------------------------------------------
        case VK_RETURN: return ButtonSlot::LeftSelectionKey;
        case VK_ESCAPE: return ButtonSlot::RightSelectionKey;

        // F3/F4 (green/red softkey) intentionally absent -- see above.
        default: return std::nullopt;
    }
}

}  // namespace sk
