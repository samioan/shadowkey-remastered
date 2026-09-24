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
// SetTextWidth()/SetTrimText() on it.
//
// M118: both are real. `FUN_100351b4` builds the item button with a child
// text-area widget at `+0xb4` (`FUN_1008f7d0`, the same class AddTextArea
// makes), and the item button's draw (`FUN_10032aa4`) copies its own text
// into the child and has the child draw itself at the button's (x, y).
// The child's draw (`FUN_1008f458`) word-wraps at SetTextWidth characters
// (`+0x98`) and, when SetTrimText (`+0x99`) is set and the text is longer
// than 0x17 characters, first cuts it to 0x17 and appends "...".
class ItemButtonTextArea : public NativeStubExecutable {
public:
    ItemButtonTextArea() : NativeStubExecutable("ItemButtonTextArea") {}
    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;
    int textWidth() const { return m_TextWidth; }
    bool trimText() const { return m_TrimText; }

private:
    int m_TextWidth = 0;
    bool m_TrimText = false;
};

// `FUN_1008f458`'s trim: over 0x17 characters, keep 0x17 and add "...".
constexpr int kTextAreaTrimChars = 0x17;

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
    const ItemButtonTextArea& textArea() const { return m_TextArea; }

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
