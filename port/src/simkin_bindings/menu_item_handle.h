#pragma once

// Native binding for the object AddMenuItem() returns. Most scripts just
// discard it or pass it straight back into SetSelectedItem(handle) (which
// only needs intValue()), but newgamemenu.s calls
// "start.SetSelectable(bCreatedChr)" directly on it -- a plain int can't
// have methods called on it, so AddMenuItem needs to return a real
// object, not intValue()'s underlying integer.

#include "simkin_bindings/native_stub_executable.h"
#include "simkin_bindings/row_owner_ref.h"

namespace sk_bindings {

class MenuItemHandle : public NativeStubExecutable, public RowOwnerRef {
public:
    MenuItemHandle(MenuExecutable& owner, size_t rowIndex)
        : NativeStubExecutable("MenuItem"), RowOwnerRef(owner, rowIndex) {}

    // The 1-based row position -- what scripts actually want when they
    // pass this handle somewhere an int is expected (SetSelectedItem(...)).
    int intValue() const override { return static_cast<int>(m_RowIndex) + 1; }

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;
};

}  // namespace sk_bindings
