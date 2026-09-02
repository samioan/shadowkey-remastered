#pragma once

// M10: native binding for objects returned by AddButton()/AddQuitButton()
// -- charactermanager.s/inventory.s/statsscreen.s's category buttons and
// "back"/quit corner buttons. Functionally the same selectable-row-with-
// callback as AddMenuItem's MenuItemHandle; this adds the cosmetic
// layout/appearance setters those screens chain onto the returned object
// (SetWidth/SetHeight/ShowBorder/SetHAdjust/SetInvokeMethodOnFocus) --
// stored but not rendered (the stand-in bitmap font/flat-background
// renderer doesn't have a notion of button borders or precise pixel
// layout, same simplification as every other cosmetic-only setter in this
// binding layer).

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
    int m_Width = 0, m_Height = 0;
};

}  // namespace sk_bindings
