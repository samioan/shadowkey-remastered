#pragma once

// M10: native binding for objects returned by AddButton()/AddQuitButton()
// -- charactermanager.s/inventory.s/statsscreen.s's category buttons and
// "back"/quit corner buttons. Functionally the same selectable-row-with-
// callback as AddMenuItem's MenuItemHandle; this adds the layout/
// appearance setters those screens chain onto the returned object.
// M25: SetWidth/SetHeight/ShowBorder now write back onto the owning
// MenuRow (RowOwnerRef, same mechanism SetSelectable/SetItemText already
// use) and are actually rendered -- main.cpp's RenderMenu() draws a real
// outline rectangle for a ShowBorder(true) row using its real w/h.
// SetHAdjust/SetInvokeMethodOnFocus stay cosmetic-only (no rendering
// concept of horizontal text adjustment or focus-invoke timing in this
// port).

#include <string>

#include "simkin_bindings/native_stub_executable.h"
#include "simkin_bindings/row_owner_ref.h"

namespace sk_bindings {

class ButtonExecutable : public NativeStubExecutable, public RowOwnerRef {
public:
    ButtonExecutable(MenuExecutable& owner, size_t rowIndex)
        : NativeStubExecutable("Button"), RowOwnerRef(owner, rowIndex) {}

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

    void SetActive(bool active) { m_Active = active; }
    bool active() const { return m_Active; }

private:
    bool m_Active = false;
    bool m_Enabled = true;
};

}  // namespace sk_bindings
