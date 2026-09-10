#include "simkin_bindings/popup_menu_executable.h"

#include <utility>

#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/native_binding_common.h"
#include "skRValue.h"

namespace sk_bindings {

PopupMenuExecutable::PopupMenuExecutable(MenuExecutable& owner, int x, int y, int w, int h)
    : NativeStubExecutable("PopupMenu"), m_Owner(owner), m_X(x), m_Y(y), m_W(w), m_H(h) {}

bool PopupMenuExecutable::method(const skString& methodName, skRValueArray& args,
                                  skRValue& returnValue, skExecutableContext& context) {
    if (methodName == skString("AddItem") && args.entries() >= 1) {
        // Every observed popup's first AddItem() is a bare prompt/title
        // with no callback (e.g. "Do you really wish to quit?"),
        // followed by the real Yes/No-style choices, each with one --
        // matching every SetSelectable(0,false) call seen in the corpus
        // exactly, so "has a callback" doubles as "is selectable"
        // (IsSelectable() above) without needing to separately track the
        // SetSelectable() calls.
        Item item;
        item.textId = args[0].intValue();
        item.callback = args.entries() >= 2 ? ToStdString(args[1].str()) : "";
        m_Items.push_back(std::move(item));
        return true;
    }
    if (methodName == skString("UpdatePopupItem") && args.entries() >= 2) {
        // M10: inventory.s's real per-action-slot text refresh (e.g.
        // toggling "Equip"/"UnEquip" based on the selected item's current
        // state) -- see Item::blanked's comment for the ""-hides-this-
        // item convention.
        //
        // M60: the real binding (popup class 0x14cfc, index 6) takes an
        // optional **third** argument, and it is a callback name --
        // `FUN_10087a60` walks to item `index`, sets its text, and then,
        // only if the argument count is greater than two, sets its
        // method too. That is how `buysell.s` turns one popup into both
        // sides of the counter without rebuilding it:
        //
        //     popup.UpdatePopupItem(0, 3787, "BuyInv");   // "Buy"
        //     popup.UpdatePopupItem(0, 3789, "SellItem"); // "Sell"
        //
        // Without it the popup kept whatever AddItem() first gave it, so
        // the store's confirm row pointed at the wrong handler on one of
        // the two pages.
        //
        // Note also what the real function does *not* do: an index past
        // the end matches nothing, and it then skips the callback
        // argument as well rather than appending an item.
        size_t index = static_cast<size_t>(args[0].intValue());
        if (index < m_Items.size()) {
            Item& item = m_Items[index];
            if (args[1].type() == skRValue::T_String) {
                std::string text = ToStdString(args[1].str());
                item.blanked = text.empty();
                item.literalText = text;
                item.textId = -1;
            } else {
                item.blanked = false;
                item.textId = args[1].intValue();
                item.literalText.clear();
            }
            if (args.entries() >= 3) item.callback = ToStdString(args[2].str());
        }
        return true;
    }
    if (methodName == skString("SetBack") && args.entries() == 1) {
        m_BackCallback = ToStdString(args[0].str());
        return true;
    }
    if (methodName == skString("SetVisible") && args.entries() == 1) {
        m_Visible = args[0].boolValue();
        return true;
    }
    if (methodName == skString("SetSelectedItem") && args.entries() == 1) {
        // M91: 0-based on the wire, 1-based inside -- see selectedItem().
        m_SelectedItem = args[0].intValue() + 1;
        return true;
    }
    if (methodName == skString("GetSelectedItem") && args.entries() == 0) {
        // M91: popup binding 0xc, the other half of the pair. Guarded the
        // same way the real case is (`-1 < idx && idx < count`), which
        // answers -1 for a popup that has never been pointed anywhere.
        int index = m_SelectedItem - 1;
        returnValue = skRValue(index >= 0 && static_cast<size_t>(index) < m_Items.size() ? index
                                                                                        : -1);
        return true;
    }
    if (methodName == skString("IsVisible") && args.entries() == 0) {
        returnValue = skRValue(m_Visible);
        return true;
    }
    if (methodName == skString("SetSelectable") || methodName == skString("SetBackground") ||
        methodName == skString("SetAutoAdjust") || methodName == skString("SetToWidget")) {
        // SetSelectable: see the AddItem() comment above -- selectability
        // is derived from having a callback, this call doesn't need to
        // change any state. SetBackground/SetAutoAdjust/SetToWidget are
        // purely cosmetic (real background images/precise pixel
        // repositioning are out of scope, see main.cpp's RenderMenu()).
        return true;
    }
    return SoftFailNativeCall("PopupMenu", methodName, args, returnValue);
}

void PopupMenuExecutable::MoveSelection(int delta) {
    std::vector<size_t> selectableIndices;
    for (size_t i = 0; i < m_Items.size(); ++i) {
        if (IsSelectable(m_Items[i])) selectableIndices.push_back(i);
    }
    if (selectableIndices.empty()) return;

    size_t currentPos = 0;
    bool found = false;
    for (size_t i = 0; i < selectableIndices.size(); ++i) {
        if (static_cast<int>(selectableIndices[i]) + 1 == m_SelectedItem) {
            currentPos = i;
            found = true;
            break;
        }
    }
    if (!found) {
        // A popup that has never been pointed anywhere (no
        // SetSelectedItem, or one aimed at a non-selectable row) lands on
        // its *first* selectable choice when the player presses a
        // direction, rather than applying `delta` from an assumed
        // position 0 and skipping straight past it.
        //
        // M91 note: this used to be the load-bearing correction for every
        // confirm popup in the game, on the belief that the scripts
        // deliberately pointed at their own prompt line. They do not --
        // SetSelectedItem is 0-based (see selectedItem()) -- so this is
        // back to being the guard it reads as.
        m_SelectedItem = static_cast<int>(selectableIndices[0]) + 1;
        return;
    }
    int count = static_cast<int>(selectableIndices.size());
    int nextPos = (static_cast<int>(currentPos) + delta % count + count) % count;
    m_SelectedItem = static_cast<int>(selectableIndices[static_cast<size_t>(nextPos)]) + 1;
}

void PopupMenuExecutable::ActivateSelected() {
    if (m_SelectedItem < 1 || static_cast<size_t>(m_SelectedItem) > m_Items.size()) return;
    const Item& item = m_Items[static_cast<size_t>(m_SelectedItem - 1)];
    // M91: a popup row is a row -- InvokeCallback, not TryInvoke. See
    // MenuExecutable::InvokeCallback(). Every one-button popup in the
    // corpus ("Okay" over a message) is `AddItem(id, "Quit")`.
    m_Owner.InvokeCallback(item.callback);
}

void PopupMenuExecutable::GoBack() { m_Owner.InvokeCallback(m_BackCallback); }

}  // namespace sk_bindings
