#include "simkin_bindings/text_area_executable.h"

#include "simkin_bindings/native_binding_common.h"
#include "skRValue.h"
#include "skRValueArray.h"

namespace sk_bindings {

bool TextAreaExecutable::method(const skString& methodName, skRValueArray& args,
                                 skRValue& returnValue, skExecutableContext& context) {
    if (methodName == skString("SetLocalizedText") && args.entries() == 1) {
        SetRowTextId(args[0].intValue());
        return true;
    }
    if (methodName == skString("SetTextWidth") && args.entries() == 1) {
        m_TextWidth = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetSelectable") && args.entries() == 1) {
        SetRowSelectable(args[0].boolValue());
        return true;
    }
    return SoftFailNativeCall("TextArea", methodName, args, returnValue);
}

}  // namespace sk_bindings
