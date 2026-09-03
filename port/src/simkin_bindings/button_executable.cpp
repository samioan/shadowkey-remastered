#include "simkin_bindings/button_executable.h"

#include "simkin_bindings/native_binding_common.h"
#include "skRValue.h"
#include "skRValueArray.h"

namespace sk_bindings {

bool ButtonExecutable::method(const skString& methodName, skRValueArray& args,
                               skRValue& returnValue, skExecutableContext& context) {
    if (methodName == skString("SetActive") && args.entries() == 1) {
        m_Active = args[0].boolValue();
        return true;
    }
    if (methodName == skString("SetEnabled") && args.entries() == 1) {
        m_Enabled = args[0].boolValue();
        return true;
    }
    if (methodName == skString("IsActive") && args.entries() == 0) {
        returnValue = skRValue(m_Active);
        return true;
    }
    if (methodName == skString("SetSelectable") && args.entries() == 1) {
        SetRowSelectable(args[0].boolValue());
        return true;
    }
    if (methodName == skString("SetItemText") && args.entries() == 1) {
        SetRowLiteralText(ToStdString(args[0].str()));
        return true;
    }
    if (methodName == skString("SetWidth") && args.entries() == 1) {
        SetRowWidth(args[0].intValue());
        return true;
    }
    if (methodName == skString("SetHeight") && args.entries() == 1) {
        SetRowHeight(args[0].intValue());
        return true;
    }
    if (methodName == skString("ShowBorder") && args.entries() == 1) {
        // M25: real (charactermanager.s's Stats/Equip/Quest buttons all
        // call ShowBorder(true)) -- drawn as a real outline rectangle
        // using the row's own real w/h, matching the real screenshot's
        // boxed 2x2 grid.
        SetRowShowBorder(args[0].boolValue());
        return true;
    }
    if (methodName == skString("SetHAdjust") || methodName == skString("SetInvokeMethodOnFocus")) {
        return true;  // cosmetic, see button_executable.h
    }
    return SoftFailNativeCall("Button", methodName, args, returnValue);
}

}  // namespace sk_bindings
