#pragma once

// M105: the player's two action queues -- `player + hand*0x1c + 0xf4c`,
// hand 0 left and hand 1 right, the thing `actionqueue.s` puts on screen
// and the `charactermanager.s` Equip path feeds.
//
// The container is tiny and fully decompiled. A queue is 0x1c bytes: two
// words this port does not need, then **five entity pointers at +8**. Six
// operations touch it, and every one of them is transcribed below:
//
//   FUN_1006c4c8(q, i)        At(i)        -- q[8 + i*4], no bounds test
//   FUN_1006c6a8(q, e)        IndexOf(e)   -- linear, -1 if absent
//   FUN_1006c6f8(q, e)        Add(e)       -- first empty slot, else fail
//   FUN_1006c4d4(q, e)        Remove(e)    -- shift the tail down, clear
//                                             slot 4; **compacting**, so a
//                                             queue never has holes
//   FUN_1006c6d4(q)           Clear()      -- all five to zero
//   FUN_1006c628(q, e, up)    Move(e, up)  -- swap with the neighbour
//   FUN_1006c560 / FUN_1006c5c8            -- rotate back / rotate forward
//
// Two details in `Move` that the shipped screen is built around: the
// target index is computed as an **unsigned** compare against 5, so
// moving slot 0 up underflows and fails, and moving slot 4 down is out of
// range and fails. It is a *swap*, not an insert. `actionqueue.s`'s
// `ShowPopup` blanks its "Up" row on the first item and its "Down" row on
// the last (that is what `GetLastItem()` is for), so the two failing cases
// are exactly the two the player is never offered.
//
// The rotates (`FUN_10044c9c`/`FUN_10044cdc`, which wrap them and then set
// the hand's equipped item) are **dead in the shipped binary**: neither
// wrapper has a caller, a pointer, or any other mention anywhere in the
// image. So the default control scheme's "Cycle Left Queue" (KD_ASTERISK)
// and "Cycle Right Queue" (KD_0) actions -- docs/INPUT_HANDLING.md lists
// both -- never ran on the device either. They are implemented here
// because the container has them and they cost two loops; nothing in this
// port calls them either, and that is faithful.

#include <array>
#include <cstddef>

namespace sk_bindings {

class ItemExecutable;

class HandQueue {
public:
    static constexpr int kSlots = 5;

    // FUN_1006c4c8. Out of range answers null rather than reading past the
    // struct, which is the one place this deliberately differs from the
    // engine -- no caller in the corpus passes an index above 4.
    ItemExecutable* At(int index) const {
        if (index < 0 || index >= kSlots) return nullptr;
        return m_Slots[static_cast<size_t>(index)];
    }

    // FUN_1006c6a8.
    int IndexOf(const ItemExecutable* item) const {
        if (!item) return -1;
        for (int i = 0; i < kSlots; ++i) {
            if (m_Slots[static_cast<size_t>(i)] == item) return i;
        }
        return -1;
    }

    bool Contains(const ItemExecutable* item) const { return IndexOf(item) >= 0; }

    // How many slots are filled. Not an engine function -- the engine never
    // needs it, because Remove compacts and Add fills the first hole, so
    // "the first empty slot" is always the count.
    int size() const {
        int n = 0;
        while (n < kSlots && m_Slots[static_cast<size_t>(n)] != nullptr) ++n;
        return n;
    }

    // FUN_1006c6f8: the first empty slot, or false if there is none.
    bool Add(ItemExecutable* item) {
        if (!item) return false;
        for (ItemExecutable*& slot : m_Slots) {
            if (!slot) {
                slot = item;
                return true;
            }
        }
        return false;
    }

    // FUN_1006c4d4: find it, shift everything after it down one, clear the
    // last slot. False if it was not here.
    bool Remove(const ItemExecutable* item) {
        const int at = IndexOf(item);
        if (at < 0) return false;
        for (int i = at; i < kSlots - 1; ++i) {
            m_Slots[static_cast<size_t>(i)] = m_Slots[static_cast<size_t>(i + 1)];
        }
        m_Slots[kSlots - 1] = nullptr;
        return true;
    }

    // FUN_1006c6d4.
    void Clear() { m_Slots.fill(nullptr); }

    // FUN_1006c628. `up` moves toward slot 0. See the header comment for
    // why both ends fail rather than wrapping or clamping.
    bool Move(const ItemExecutable* item, bool up) {
        const int at = IndexOf(item);
        if (at < 0) return false;
        // The engine's own unsigned compare: `at - 1` at slot 0 wraps, and
        // `at + 1` at slot 4 is 5. Both are "not < 5", both fail.
        const unsigned target = up ? static_cast<unsigned>(at - 1) : static_cast<unsigned>(at + 1);
        if (target >= static_cast<unsigned>(kSlots)) return false;
        std::swap(m_Slots[static_cast<size_t>(at)], m_Slots[target]);
        return true;
    }

    // FUN_1006c560: the last filled entry becomes the first, everything
    // else shifts up one. Returns the new head -- what the wrapper equips.
    ItemExecutable* RotateBackward() {
        const int filled = size();
        if (filled == 0) return nullptr;
        ItemExecutable* last = m_Slots[static_cast<size_t>(filled - 1)];
        for (int i = filled - 1; i > 0; --i) {
            m_Slots[static_cast<size_t>(i)] = m_Slots[static_cast<size_t>(i - 1)];
        }
        m_Slots[0] = last;
        return m_Slots[0];
    }

    // FUN_1006c5c8: the head goes to the back, everything else shifts down.
    ItemExecutable* RotateForward() {
        const int filled = size();
        if (filled == 0) return nullptr;
        ItemExecutable* head = m_Slots[0];
        for (int i = 0; i < filled - 1; ++i) {
            m_Slots[static_cast<size_t>(i)] = m_Slots[static_cast<size_t>(i + 1)];
        }
        m_Slots[static_cast<size_t>(filled - 1)] = head;
        return m_Slots[0];
    }

    // Drop an item that is going away (sold, dropped, consumed) from this
    // queue. Not an engine entry point of its own -- the engine's item
    // teardown calls the same Remove -- but this port needs one place to
    // call from PurgeRemovedItems, because a queue slot is a raw pointer
    // into the inventory's unique_ptrs.
    void Forget(const ItemExecutable* item) { Remove(item); }

private:
    std::array<ItemExecutable*, kSlots> m_Slots{};
};

}  // namespace sk_bindings
