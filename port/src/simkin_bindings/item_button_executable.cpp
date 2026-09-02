#include "simkin_bindings/item_button_executable.h"

#include "simkin_bindings/native_binding_common.h"
#include "skRValue.h"
#include "skRValueArray.h"

namespace sk_bindings {

bool ItemButtonTextArea::method(const skString& methodName, skRValueArray& args,
                                 skRValue& returnValue, skExecutableContext& context) {
    if (methodName == skString("SetTextWidth") || methodName == skString("SetTrimText")) {
        return true;  // purely cosmetic here, see item_button_executable.h
    }
    return SoftFailNativeCall("ItemButtonTextArea", methodName, args, returnValue);
}

bool ItemButtonExecutable::method(const skString& methodName, skRValueArray& args,
                                   skRValue& returnValue, skExecutableContext& context) {
    if (methodName == skString("SetSprite") && args.entries() == 1) {
        m_Icon = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetItemText") && args.entries() == 1) {
        m_ItemText = ToStdString(args[0].str());
        m_TextId = -1;
        return true;
    }
    if (methodName == skString("SetLocalizedText") && args.entries() == 1) {
        m_TextId = args[0].intValue();
        m_ItemText.clear();
        return true;
    }
    if (methodName == skString("SetVisible") && args.entries() == 1) {
        m_Visible = args[0].boolValue();
        return true;
    }
    if (methodName == skString("GetTextArea") && args.entries() == 0) {
        returnValue = skRValue(static_cast<skiExecutable*>(&m_TextArea), false);
        return true;
    }
    if (methodName == skString("SetSelectable") && args.entries() == 1) {
        SetRowSelectable(args[0].boolValue());
        return true;
    }
    return SoftFailNativeCall("ItemButton", methodName, args, returnValue);
}

}  // namespace sk_bindings
