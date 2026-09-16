#pragma once

// Native binding for objects returned by AddTextArea() -- multi-line
// descriptive text (e.g. the class/race description on
// ChooseCharacterMenu/ChooseRaceMenu, refreshed each time the adjacent
// ComboBox's selection changes).

#include "simkin_bindings/native_stub_executable.h"
#include "simkin_bindings/row_owner_ref.h"

namespace sk_bindings {

class TextAreaExecutable : public NativeStubExecutable, public RowOwnerRef {
public:
    TextAreaExecutable(MenuExecutable& owner, size_t rowIndex, int x, int y)
        : NativeStubExecutable("TextArea"), RowOwnerRef(owner, rowIndex), m_X(x), m_Y(y) {}

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

    int textWidth() const { return m_TextWidth; }
    // M108: the engine's text-area draw (`FUN_1008f458`) starts its line
    // cursor at the widget's **own** `+0x78`/`+0x7c` -- the x and y the
    // script passed to AddTextArea -- not at the menu's shared row
    // cursor, and it does not advance that cursor afterwards.
    int x() const { return m_X; }
    int y() const { return m_Y; }

private:
    int m_X, m_Y;
    int m_TextWidth = 27;
};

}  // namespace sk_bindings
