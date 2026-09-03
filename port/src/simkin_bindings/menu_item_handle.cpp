#include "simkin_bindings/menu_item_handle.h"

#include "simkin_bindings/native_binding_common.h"
#include "skRValue.h"
#include "skRValueArray.h"

namespace sk_bindings {

bool MenuItemHandle::method(const skString& methodName, skRValueArray& args,
                             skRValue& returnValue, skExecutableContext& context) {
    if (methodName == skString("SetSelectable") && args.entries() == 1) {
        SetRowSelectable(args[0].boolValue());
        return true;
    }
    if (methodName == skString("SetItemText") && args.entries() == 1) {
        // M10: charactermanager.s's classInfoItem (an AddFloatingText()
        // row, which shares this handle class) refreshes its text this
        // way after the fact.
        SetRowLiteralText(ToStdString(args[0].str()));
        return true;
    }
    if (methodName == skString("GetAssociatedObject") && args.entries() == 0) {
        // M21: lootmenu.s's own `MenuItem.GetAssociatedObject()`.
        skiExecutable* obj = RowAssociatedObject();
        if (obj) returnValue = skRValue(obj, false);
        return true;
    }
    return SoftFailNativeCall("MenuItem", methodName, args, returnValue);
}

}  // namespace sk_bindings
