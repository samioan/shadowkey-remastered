#include "simkin_bindings/action_text.h"

#include <cstddef>

#include "assets/string_table.h"
#include "engine/input_state.h"

namespace sk_bindings {

namespace {

// The twelve literals, in `FUN_10030250`'s own comparison order, with the
// action index each one's `mov r1, #N` supplies. Read out of the image
// rather than the decompiler: Ghidra drops the second call's argument
// (`InputState_ResolveBindingOffset(iVar5)` with nothing after the comma),
// which is `mov r1, #8` at 0x100305dc.
struct KeyToken {
    const char* token;
    int action;
};

constexpr KeyToken kKeyTokens[] = {
    {"KD_0", 10},         // Cycle Right Queue
    {"KD_1", 8},          // Jump
    {"KD_2", 4},          // Look Up
    {"KD_3", 13},         // Use
    {"KD_4", 6},          // Side Step Left
    {"KD_5", 15},         // Use Right Action
    {"KD_6", 7},          // Side Step Right
    {"KD_7", 14},         // Use Left Action
    {"KD_8", 5},          // Look Down
    {"KD_9", 9},          // Map Toggle
    {"KD_ASTERISK", 11},  // Cycle Left Queue
    {"KD_POUND", 12},     // Character Manager
};

// The label of whatever key `action` is bound to. Without an InputState
// the engine's own default binding table answers, which is what every
// token was written against in the first place.
std::string KeyNameForAction(int action, const sk::InputState* input,
                             const sk::StringTable* strings) {
    if (!strings) return std::string();
    static const sk::InputState kDefaults;  // InitDefaultBindings() in its ctor
    const sk::InputState& state = input ? *input : kDefaults;
    const int nameId = state.bindingNameStringId(static_cast<sk::Action>(action));
    if (nameId < 0) return std::string();
    return strings->Get(nameId);
}

}  // namespace

int ActionIndexForKeyToken(const std::string& token) {
    for (const KeyToken& entry : kKeyTokens) {
        if (token == entry.token) return entry.action;
    }
    return -1;
}

std::string SubstituteActionKeys(const std::string& text, const sk::InputState* input,
                                 const sk::StringTable* strings) {
    std::string out = text;
    // The real loop rescans from index 0 after every substitution rather
    // than continuing where it left off -- same result, and reproducing it
    // keeps the "stop at the first unknown token" behaviour below exact.
    for (;;) {
        const std::size_t open = out.find('[');
        if (open == std::string::npos) return out;
        const std::size_t close = out.find(']', open + 1);
        if (close == std::string::npos) return out;
        const std::string token = out.substr(open + 1, close - open - 1);
        const int action = ActionIndexForKeyToken(token);
        // `bVar3 = false` -- an unrecognised token ends the whole pass,
        // leaving itself *and* every later token in the text. Worth
        // reproducing rather than skipping past: it is what makes a typo'd
        // token visible in-game instead of silently swallowed.
        if (action < 0) return out;
        const std::string name = KeyNameForAction(action, input, strings);
        // An unbound action resolves to slot 0x15 in the engine
        // (ResolveBindingOffset's own out-of-range return) and reads back
        // past the 21-entry label array; this port substitutes nothing
        // instead, and still moves past the token so the scan terminates.
        out = out.substr(0, open) + name + out.substr(close + 1);
    }
}

std::string ParseActionText(int textId, const sk::InputState* input,
                            const sk::StringTable* strings) {
    if (!strings) return std::string();
    return SubstituteActionKeys(strings->Get(textId), input, strings);
}

}  // namespace sk_bindings
