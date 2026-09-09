#pragma once

// M80: `ParseActionText(textId)` -- the Menu class's binding 2
// (`FUN_1003136c` case 2), and the reason the game's tutorial popups
// showed nothing.
//
// The shipped tutorial strings are written against the *default* N-Gage
// keypad and name their keys with a bracketed token:
//
//     2995  "Press [KD_5] to continue."
//     3748  "...can be used by [KD_7]. [KD_ASTERISK] will cycle through
//            the queue if you have multiple consumables..."
//
// `ParseActionText` is what turns those into real key names. The native
// itself is small -- copy `stringTable[textId]` into a 1024-wchar scratch
// buffer, hand it to `FUN_10030250`, return the result as a Simkin string
// -- and `FUN_10030250` is the substitution:
//
//   * find the first `[`, then the first `]` after it; stop if either is
//     missing (`bVar3` stays false and the outer `do/while` exits);
//   * compare the token between them against twelve literals, in this
//     order: KD_0, KD_1, KD_2, KD_3, KD_4, KD_5, KD_6, KD_7, KD_8, KD_9,
//     KD_ASTERISK, KD_POUND;
//   * on a match, replace `[token]` (brackets included) with
//     `FUN_1001a578(input, InputState_ResolveBindingOffset(input, N))` --
//     the name of the key that logical action N is bound to *now*;
//   * restart the scan from the beginning of the rewritten string;
//   * on no match, stop -- the unknown token is left in the text and no
//     later token is substituted either.
//
// **The token is not the key, it is the action that key defaults to.**
// Each `mov r1, #N` ahead of the twelve `bl InputState_ResolveBindingOffset`
// calls gives N, and every one of them lands on the action whose default
// binding is that very key (docs/INPUT_HANDLING.md's M57 index table):
//
//     [KD_0] -> 10 Cycle Right Queue    [KD_6] ->  7 Side Step Right
//     [KD_1] ->  8 Jump                 [KD_7] -> 14 Use Left Action
//     [KD_2] ->  4 Look Up              [KD_8] ->  5 Look Down
//     [KD_3] -> 13 Use                  [KD_9] ->  9 Map Toggle
//     [KD_4] ->  6 Side Step Left       [KD_ASTERISK] -> 11 Cycle Left Queue
//     [KD_5] -> 15 Use Right Action     [KD_POUND]    -> 12 Character Manager
//
// So the tokens are named for the stock layout, and the indirection is
// what keeps "Press [KD_5] to continue" honest after the player remaps
// their controls in the Configure Keys screen. That twelve-for-twelve
// agreement is also independent confirmation of the M57 action-index
// table, recovered from an entirely different function.
//
// This port's keyboard sits under the same two layers (M69), so `Key 5`
// is what a player sees here too -- the physical key is a porting
// concern, the label is the engine's.

#include <string>

namespace sk {
class InputState;
class StringTable;
}  // namespace sk

namespace sk_bindings {

// The engine action a `KD_*` token names, or -1 if the token is not one
// of the twelve. `token` is the text *between* the brackets.
int ActionIndexForKeyToken(const std::string& token);

// `FUN_10030250`. `input`/`strings` may be null (tests, and the same
// tolerance every other optional subsystem in this port has) -- with no
// input state the default bindings are assumed, with no string table the
// token is left in place, which is exactly what the engine's own
// "unknown token" arm does.
std::string SubstituteActionKeys(const std::string& text, const sk::InputState* input,
                                 const sk::StringTable* strings);

// The whole native: look `textId` up in the string table, then substitute.
std::string ParseActionText(int textId, const sk::InputState* input,
                            const sk::StringTable* strings);

}  // namespace sk_bindings
