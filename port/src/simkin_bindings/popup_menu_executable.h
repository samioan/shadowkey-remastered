#pragma once

// Native binding for objects returned by CreateMenu()'s CreatePopupMenu()
// -- confirmation dialogs like mainmenu.s's "really quit?" / "really
// save?" popups. Tracks just enough state (items, visibility, selection)
// for M4's navigation to work; anything else soft-fails.

#include <string>
#include <vector>

#include "simkin_bindings/native_stub_executable.h"
#include "skRValueArray.h"

namespace sk_bindings {

class PopupMenuExecutable : public NativeStubExecutable {
public:
    PopupMenuExecutable(int x, int y, int w, int h);

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

    bool visible() const { return m_Visible; }
    int selectedItem() const { return m_SelectedItem; }

private:
    struct Item {
        int textId;
        std::string callback;
    };

    int m_X, m_Y, m_W, m_H;
    bool m_Visible = false;
    int m_SelectedItem = 0;
    std::string m_BackCallback;
    std::vector<Item> m_Items;
};

}  // namespace sk_bindings
