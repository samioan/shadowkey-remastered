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
    if (methodName == skString("IsActive") && args.entries() == 0) {
        returnValue = skRValue(m_Active);
        return true;
    }
    // M60: SetSelectable/SetVisible/SetEnabled/SetItemText/GetItemText/
    // SetLocalizedText/SetX/SetY/GetX/GetY all come from the engine's one
    // shared widget class (row_owner_ref.h) -- a button is not special.
    if (HandleSharedWidgetNative(methodName, args, returnValue)) {
        if (methodName == skString("SetEnabled")) m_Enabled = args[0].boolValue();
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
    if (methodName == skString("SetHAdjust") || methodName == skString("SetXAdjust") ||
        methodName == skString("SetActiveSprite") || methodName == skString("SetDormantSprite")) {
        // The remaining five of button class 0x14d2c's own eight bindings
        // (SetActive/SetWidth/SetHeight/ShowBorder above are the others);
        // cosmetic in this port, see button_executable.h.
        return true;
    }
    return SoftFailNativeCall("Button", methodName, args, returnValue);
}

}  // namespace sk_bindings
