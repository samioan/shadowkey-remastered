#include "simkin_bindings/menu_item_handle.h"

#include "simkin_bindings/native_binding_common.h"
#include "skRValue.h"
#include "skRValueArray.h"

namespace sk_bindings {

bool MenuItemHandle::method(const skString& methodName, skRValueArray& args,
                             skRValue& returnValue, skExecutableContext& context) {
    // M60: every method this class ever had is one of the engine's
    // fourteen shared widget bindings (row_owner_ref.h) -- including
    // SetLocalizedText, which `buysell.s` calls on its title
    // (AddFloatingText) row to relabel the page, and SetItemText, which it
    // calls on the same kind of row to flip "Sell" to "Buy".
    if (HandleSharedWidgetNative(methodName, args, returnValue)) return true;
    return SoftFailNativeCall("MenuItem", methodName, args, returnValue);
}

}  // namespace sk_bindings
