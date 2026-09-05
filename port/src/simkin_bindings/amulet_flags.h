#pragma once

// M56: the game's small global "key item" bitfield, and the three natives
// that read it.
//
// `FUN_1002f914(registry, mask)` is a one-line predicate --
// `(*(u16 *)(registry + 0x468) & mask) != 0` -- with a matching setter
// (`FUN_1002f8fc`, OR) and clearer (`FUN_1002f8e0`, AND-NOT). Three
// script-visible natives are built on it, and all three surprised this
// port's earlier assumptions:
//
//  * **`HasAmulet(name)` never looks at the inventory.** The real handler
//    (Player dispatcher case 0x3e) is a four-way `wcscmp` chain --
//    "blueam", "redam", "goldam", "frozen_key" -- each paired with one
//    bit of this word. Anything else returns false.
//  * **`FindInventory("frozen_key")` has a special case in front of the
//    real search**: if bit 0x10 is set it returns the bare *integer*
//    0x325 instead of an item object, so the caller's `if (Key != null)`
//    still passes once the real key object is gone.
//  * **`RemoveItem()` takes either an object or that integer**, and the
//    integer arm is where the counter below is maintained.
//
// The word is fed from the *other* end: `FUN_1003d8e0`, the engine's
// add-to-inventory path (the player vtable's `+0x164`, which `GiveItem`
// also calls), tests the incoming item's `entities.txt` template id and
// sets the matching bit. So the flags are a cache of "the player has
// ever held one of these four specific items", maintained at the moment
// of acquisition rather than searched for on demand.
//
// Every id below is verified against the shipped `entities.txt`.

#include <string>

namespace sk_bindings {

// entities.txt template ids. The comment on each row is that row's real
// `entities.txt` script path.
inline constexpr int kTemplateRedAmulet = 0x2cd;   // 717  raiders\RedAmulet.s
inline constexpr int kTemplateBlueAmulet = 0x2ce;  // 718  raiders\BlueAmulet.s
inline constexpr int kTemplateGoldAmulet = 0x2cf;  // 719  raiders\GoldAmulet.s
inline constexpr int kTemplateFrozenKey = 0x325;   // 805  items\frozen_key.s

// The bit each one sets in `registry+0x468`. Note the order is *not* the
// template order -- red is 2, gold is 4, blue is 8 -- which is only
// visible by reading both `FUN_1003d8e0` and the `HasAmulet` chain, and
// is the kind of thing a guess would have got wrong.
inline constexpr unsigned kFlagRedAmulet = 0x02;
inline constexpr unsigned kFlagGoldAmulet = 0x04;
inline constexpr unsigned kFlagBlueAmulet = 0x08;
inline constexpr unsigned kFlagFrozenKey = 0x10;

struct KeyItemFlag {
    const char* scriptName;  // what HasAmulet(name) matches, verbatim
    unsigned bit;
    int templateId;
};

// In the order the real handler's wcscmp chain tests them.
inline constexpr KeyItemFlag kKeyItemFlags[] = {
    {"blueam", kFlagBlueAmulet, kTemplateBlueAmulet},
    {"redam", kFlagRedAmulet, kTemplateRedAmulet},
    {"goldam", kFlagGoldAmulet, kTemplateGoldAmulet},
    {"frozen_key", kFlagFrozenKey, kTemplateFrozenKey},
};
inline constexpr int kKeyItemFlagCount = 4;

// What `FindInventory("frozen_key")` returns in place of an item object
// once the flag is set. The real handler builds an int atom out of a
// literal 0x325 -- the key's own template id, reused as a stand-in
// handle, which is exactly why `RemoveItem` has an integer arm at all.
inline constexpr int kFrozenKeyStandIn = kTemplateFrozenKey;

// `registry+0x468` plus the frozen key's own separate counter at
// `registry+0x478`.
class KeyItemFlags {
public:
    bool test(unsigned bit) const { return (m_Word & bit) != 0; }
    void set(unsigned bit) { m_Word = static_cast<unsigned short>(m_Word | bit); }
    void clear(unsigned bit) { m_Word = static_cast<unsigned short>(m_Word & ~bit); }
    unsigned word() const { return m_Word; }
    void Reset() {
        m_Word = 0;
        m_FrozenKeyCount = 0;
    }

    // `HasAmulet(name)`. Returns false for any name outside the table --
    // the real chain's final else does exactly that, so e.g. a mistyped
    // amulet name is silently "you don't have it", not an error.
    bool HasAmulet(const std::string& name) const {
        for (const KeyItemFlag& f : kKeyItemFlags) {
            if (name == f.scriptName) return test(f.bit);
        }
        return false;
    }

    // `FUN_1003d8e0`, the add-to-inventory hook. Only the four template
    // ids do anything; everything else falls straight through.
    void OnItemAcquired(int templateId) {
        for (const KeyItemFlag& f : kKeyItemFlags) {
            if (templateId != f.templateId) continue;
            set(f.bit);
            if (templateId == kTemplateFrozenKey) ++m_FrozenKeyCount;
            return;
        }
    }

    // The frozen-key arm of `RemoveItem`, in the real handler's own order:
    //
    //     if (*(short *)(reg + 0x478) == 0) FUN_1002f8e0(reg, 0x10);
    //     *(short *)(reg + 0x478) = *(short *)(reg + 0x478) + -1;
    //
    // i.e. the *test comes before the decrement*, so one acquired key
    // survives its first removal with the flag still set (count 1 -> 0,
    // bit untouched) and only the second removal clears it (count 0 -> -1,
    // bit cleared). Reproduced exactly, including the counter going
    // negative: this is what makes a single frozen key open two of
    // glcrcrwl's gates and no more, which is observable behaviour, not an
    // internal detail worth "fixing".
    void OnFrozenKeyRemoved() {
        if (m_FrozenKeyCount == 0) clear(kFlagFrozenKey);
        --m_FrozenKeyCount;
    }

    int frozenKeyCount() const { return m_FrozenKeyCount; }

private:
    unsigned short m_Word = 0;
    short m_FrozenKeyCount = 0;
};

}  // namespace sk_bindings
