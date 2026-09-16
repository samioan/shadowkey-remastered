#include "simkin_bindings/popup_menu_executable.h"

#include <cstdio>
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
        // with no callback (e.g. "Do you really wish to quit?"), followed
        // by the real Yes/No-style choices, each with one.
        //
        // M106: that pattern is real but it is **not** what makes an item
        // selectable, and this port used to treat it as if it were. The
        // engine's item constructor (`FUN_100875e8`) sets `item+0x5c = 1`
        // for every item it builds, callback or no callback; only
        // `SetSelectable` clears it. See that handler below.
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
    // M104: popup binding 4 (`FUN_10087a60` case 4). It is case 5 --
    // `SetSelectedItem` -- with one extra line: if the index is past the
    // item count it logs a warning first, and then writes `popup+0xa0`
    // anyway. Same field, same 0-based index (M91), so this shares the
    // handler below rather than restating it.
    //
    // Two shipped callers, both `buysell.s`: `msgPopup.SetFocus(1)` on the
    // "your trade cannot use this" prompt, whose row 1 is "Buy Anyway" and
    // whose row 0 is made unselectable on the next line; and
    // `inventoryInfoPopup.SetFocus(3)`. Until now both soft-failed, so the
    // store's warning opened pointing at nothing and the first D-pad press
    // was spent finding a row.
    if (methodName == skString("SetFocus") && args.entries() == 1) {
        const int index = args[0].intValue();
        if (index >= static_cast<int>(m_Items.size())) {
            // `FUN_1000e02c(...)` -- the engine's own out-of-range notice.
            // It does not clamp, and neither does this.
            std::printf("  [popup] SetFocus(%d) past the %d items this popup has\n", index,
                        static_cast<int>(m_Items.size()));
        }
        m_SelectedItem = index + 1;  // internal 1-based, see selectedItem()
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
    if (methodName == skString("SetSelectable") && args.entries() >= 2) {
        // M106: `SetSelectable(idx, flag)` -- case 3 of `FUN_10087a60`,
        // which walks `idx` links from the item list head and writes the
        // flag into `item+0x5c`.
        //
        // This port used to no-op the call and derive selectability from
        // "has a callback". That holds for a popup built once and used
        // once, and breaks on the one built once and **reused**:
        // `buysell.s` creates `msgPopup` with all three items carrying
        // callbacks --
        //     msgPopup.AddItem(3297,"CloseMsgPopup");  // 0
        //     msgPopup.AddItem(3297,"ForcePurchase");  // 1
        //     msgPopup.AddItem(3297,"CloseMsgPopup");  // 2
        // -- and then repurposes 0 and 1 as plain message lines
        // ("You bought X" / "for N gold"), with `SetSelectable(i,false)`
        // as the *only* thing making them inert. Ignoring it left item 1
        // live with `ForcePurchase` still attached, so selecting the
        // "for N gold" line bought the item a second time.
        //
        // The engine tolerates the cursor *resting* on a cleared item
        // (move-down, `FUN_10088664`, only requires the item have text);
        // it is activation, `FUN_10088a40`, that tests `+0x5c` before
        // firing the callback. This port skips such items in navigation
        // instead, so the cursor never lands where nothing can happen --
        // a cosmetic difference with the same reachable outcome.
        const int index = args[0].intValue();
        if (index >= 0 && static_cast<size_t>(index) < m_Items.size()) {
            m_Items[static_cast<size_t>(index)].selectable = args[1].boolValue();
        }
        return true;
    }
    if (methodName == skString("SetBackground") || methodName == skString("SetAutoAdjust") ||
        methodName == skString("SetToWidget")) {
        // Purely cosmetic (real background images/precise pixel
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

// M103 -- see the declaration. The engine builds this popup inline in two
// places (`FUN_10035368` for an item's DisplayPopup, the menu class's own
// case 0xa for a menu's) with byte-identical bodies: a 174x70 box at
// (0, 0x68), two rows, visible.
void PopupMenuExecutable::MakeMessagePopup() {
    m_X = 0;
    m_Y = 0x68;
    m_W = 0xae;
    m_H = 0x46;
    m_ClosesOnActivate = true;
    m_Items.clear();
    Item message;
    message.textId = -1;
    m_Items.push_back(message);
    Item back;
    back.textId = -1;
    back.literalText = "Back";  // the engine's own literal, not a string id
    // Not a script callback name in the engine -- `FUN_1002fd94` calls the
    // method directly -- but naming it here is what makes this row the
    // selectable one, and ActivateSelected() below routes it through
    // TryInvoke rather than InvokeCallback so a screen with no
    // OnMsgPopupClosed (every screen but inventory.s) stays quiet.
    back.callback = "OnMsgPopupClosed";
    m_Items.push_back(back);
    // The engine leaves `popup+0xa0` at -1 when it shows the popup, so the
    // first D-pad press lands on the first selectable row -- which is row
    // 1. Pointing there up front saves the player that press and cannot
    // land anywhere else: row 0 has no callback.
    m_SelectedItem = 2;  // 1-based, see selectedItem()
}

void PopupMenuExecutable::SetMessage(const std::string& text) {
    if (!m_ClosesOnActivate) MakeMessagePopup();
    m_Items[0].literalText = text;
    m_Items[0].textId = -1;
    m_Items[0].blanked = text.empty();
    m_SelectedItem = 2;
    m_Visible = true;
}

void PopupMenuExecutable::ActivateSelected() {
    if (m_SelectedItem < 1 || static_cast<size_t>(m_SelectedItem) > m_Items.size()) return;
    const Item& item = m_Items[static_cast<size_t>(m_SelectedItem - 1)];
    if (m_ClosesOnActivate) {
        // `FUN_1002fd94`: hide first, then the method call -- and the call
        // is a plain one on the menu script, so it is not a soft-fail for a
        // screen that defines no handler.
        m_Visible = false;
        m_Owner.TryInvoke(item.callback);
        return;
    }
    // M106: `FUN_10088a40` fires the callback only when `item+0x5c` is set
    // *and* the item has one. MoveSelection() already refuses to land on an
    // unselectable row, but `SetSelectedItem`/`SetFocus` point straight at
    // an index without consulting the flag -- and `buysell.s` aims the
    // cursor *before* clearing rows, so this is the guard that holds.
    // (Scoped to the scripted popup class; the engine-made message popup
    // above goes through `FUN_1002fd94`, which has no such test.)
    if (!IsSelectable(item)) return;
    // M91: a popup row is a row -- InvokeCallback, not TryInvoke. See
    // MenuExecutable::InvokeCallback(). Every one-button popup in the
    // corpus ("Okay" over a message) is `AddItem(id, "Quit")`.
    m_Owner.InvokeCallback(item.callback);
}

void PopupMenuExecutable::GoBack() {
    // M103: the message popup carries no SetBack target -- the engine never
    // gives it one -- and a visible popup owns the back key outright
    // (main.cpp), so routing it to InvokeCallback("") would latch the popup
    // up with no way out. Its second row is labelled "Back"; the right
    // softkey does what that row does. A port decision, not a decompiled
    // one: what the shipped build does with this key was not established.
    if (m_ClosesOnActivate) {
        m_Visible = false;
        m_Owner.TryInvoke("OnMsgPopupClosed");
        return;
    }
    m_Owner.InvokeCallback(m_BackCallback);
}

}  // namespace sk_bindings
