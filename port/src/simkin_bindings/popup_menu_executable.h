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
        std::string callback;
    };
    const std::vector<Item>& items() const { return m_Items; }
    bool visible() const { return m_Visible; }
    int selectedItem() const { return m_SelectedItem; }  // 1-based

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

private:
    MenuExecutable& m_Owner;
    int m_X, m_Y, m_W, m_H;
    bool m_Visible = false;
    int m_SelectedItem = 0;
    std::string m_BackCallback;
    std::vector<Item> m_Items;
};

}  // namespace sk_bindings
