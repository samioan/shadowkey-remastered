#pragma once

// Native binding for objects returned by CreateMenu()'s CreatePopupMenu()
// -- confirmation dialogs like mainmenu.s's "really quit?" / "really
// save?" popups. Tracks enough state (items, visibility, selection) to
// both drive the state machine (M3) and actually render/navigate as a
// modal overlay (M5) -- when visible, it captures Up/Down/Enter/Esc
// ahead of the owning menu's own row navigation (see main.cpp).

#include <string>
#include <vector>

#include "simkin_bindings/native_stub_executable.h"
#include "skRValueArray.h"

namespace sk_bindings {

class MenuExecutable;

class PopupMenuExecutable : public NativeStubExecutable {
public:
    PopupMenuExecutable(MenuExecutable& owner, int x, int y, int w, int h);

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

    struct Item {
        int textId;
        std::string literalText;  // M10: UpdatePopupItem's string-arg case, see .cpp
        std::string callback;
        // M10: set when UpdatePopupItem(index, "") blanks this item's
        // text (inventory.s's own convention for "hide this action" --
        // e.g. no Drop option for a CanDrop()==false item) -- the item's
        // original AddItem() callback is kept as-is (a later
        // UpdatePopupItem(index, someText) un-blanks it without needing
        // to remember what the callback used to be), it's just excluded
        // from selection/rendering while blanked is true.
        bool blanked = false;
    };
    // True if this item can currently be selected/activated -- has a real
    // callback from AddItem() and isn't currently blanked via
    // UpdatePopupItem(index, "").
    static bool IsSelectable(const Item& item) { return !item.callback.empty() && !item.blanked; }
    const std::vector<Item>& items() const { return m_Items; }
    bool visible() const { return m_Visible; }
    // 1-based index into **all** items (`m_Items[selectedItem()-1]`), not
    // into just the selectable ones. Use IsItemSelected() rather than
    // recounting: main.cpp's popup renderer used to compare this against
    // a selectable-only counter, so the highlight sat on the wrong row on
    // any popup with a static message line above its buttons -- which is
    // every confirm popup in the game.
    //
    // M91: this is an *internal* 1-based index, and the script-facing
    // `SetSelectedItem`/`GetSelectedItem` are **0-based** -- see the
    // method() handlers, which convert. The popup class's own dispatcher
    // (FUN_10087a60) settles it: `SetSelectable(idx, flag)` (case 3) and
    // `GetSelectedItem()` (case 0xc) walk `idx` links from the head of
    // the same item list, and `SetSelectedItem(n)` (case 5) writes that
    // same `popup+0xa0`. So the pattern every confirm popup in the game
    // is built from --
    //     popup.AddItem(4029);                  // "Game Saved."
    //     popup.AddItem(4028,"CancelDoneSave"); // "OK"
    //     popup.SetSelectable(0,false);
    //     popup.SetSelectedItem(1);
    // -- opens on **OK**, not on the message line above it.
    int selectedItem() const { return m_SelectedItem; }
    bool IsItemSelected(size_t itemIndex) const {
        return static_cast<int>(itemIndex) + 1 == m_SelectedItem;
    }

    // Host-driven navigation while this popup is the active modal (see
    // main.cpp) -- mirrors MenuExecutable's own MoveSelection/
    // ActivateSelected, but over this popup's own item list, and
    // dispatches callbacks on the *owning* menu (popups don't define
    // their own script handlers -- "ConfirmQuitGame" etc. live on
    // whichever menu created the popup).
    void MoveSelection(int delta);
    void ActivateSelected();
    // Fires the SetBack()-registered callback (Esc/right-softkey while
    // this popup is open).
    void GoBack();

    // M103: the engine-owned message popup at `menu+0xa8` -- the one
    // `DisplayPopup(text)` creates and `GetMessagePopup()` hands back. It
    // is not a `CreatePopupMenu` popup: it has its own C++ class and its
    // own vtable (0x100fdc60), whose activate slot `FUN_1002fd94` is four
    // lines long --
    //
    //     if (selectedRow == 1) { SetVisible(popup, 0);
    //                             menu->script.OnMsgPopupClosed(); }
    //
    // -- so activating its second row hides it and calls a method on the
    // owning menu script, rather than dispatching a script-named callback
    // like every other popup row. `MakeMessagePopup` sets that up; the flag
    // is what makes ActivateSelected() hide the popup first.
    void MakeMessagePopup();
    bool isMessagePopup() const { return m_ClosesOnActivate; }
    // DisplayPopup's own body: row 0 is the message, row 1 the "Back"
    // label, and the popup is shown. Re-displaying an existing one only
    // rewrites row 0 (`FUN_10088ea4(popup, 0, text)`), which is why a
    // second message never grows the popup.
    void SetMessage(const std::string& text);

private:
    MenuExecutable& m_Owner;
    int m_X, m_Y, m_W, m_H;
    bool m_Visible = false;
    int m_SelectedItem = 0;
    std::string m_BackCallback;
    std::vector<Item> m_Items;
    bool m_ClosesOnActivate = false;  // M103, see MakeMessagePopup()
};

}  // namespace sk_bindings
