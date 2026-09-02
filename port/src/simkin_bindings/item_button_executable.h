#pragma once

// M10: native binding for objects returned by AddItemButton() --
// charactermanager.s's left/right hand equip-slot buttons (shows an
// equipped item's icon + name, or a placeholder when empty). Distinct from
// the plain button/menu-item row model because it tracks its own icon +
// visibility + a nested "text area" sub-object real scripts configure
// separately (leftActionItem.GetTextArea().SetTextWidth(11)).

#include <string>

#include "simkin_bindings/native_stub_executable.h"
#include "simkin_bindings/row_owner_ref.h"

namespace sk_bindings {

// The GetTextArea() sub-object -- charactermanager.s only ever calls
// SetTextWidth()/SetTrimText() on it, both purely cosmetic here (the
// item's real bitmap-font name label already comes from the owning
// ItemButtonExecutable's itemText()).
class ItemButtonTextArea : public NativeStubExecutable {
public:
    ItemButtonTextArea() : NativeStubExecutable("ItemButtonTextArea") {}
    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;
};

class ItemButtonExecutable : public NativeStubExecutable, public RowOwnerRef {
public:
    ItemButtonExecutable(MenuExecutable& owner, size_t rowIndex, int icon, std::string callback,
                          int x, int y, int w, int h)
        : NativeStubExecutable("ItemButton"),
          RowOwnerRef(owner, rowIndex),
          m_Icon(icon),
          m_Callback(std::move(callback)),
          m_X(x),
          m_Y(y),
          m_W(w),
          m_H(h) {}

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

    const std::string& callback() const { return m_Callback; }
    // Rendering (main.cpp): prefer the literal itemText() set via
    // SetItemText() if non-empty, else resolve textId() (set via
    // SetLocalizedText()) through the real stringtable, same
    // literal-vs-id convention as MenuExecutable::MenuRow.
    const std::string& itemText() const { return m_ItemText; }
    int textId() const { return m_TextId; }
    int icon() const { return m_Icon; }
    bool visible() const { return m_Visible; }

private:
    int m_Icon;
    std::string m_Callback;
    int m_X, m_Y, m_W, m_H;
    std::string m_ItemText;
    int m_TextId = -1;
    bool m_Visible = true;
    ItemButtonTextArea m_TextArea;
};

}  // namespace sk_bindings
