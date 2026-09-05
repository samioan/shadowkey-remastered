#include "simkin_bindings/text_area_executable.h"

#include "simkin_bindings/native_binding_common.h"
#include "skRValue.h"
#include "skRValueArray.h"

namespace sk_bindings {

bool TextAreaExecutable::method(const skString& methodName, skRValueArray& args,
                                 skRValue& returnValue, skExecutableContext& context) {
    if (methodName == skString("SetTextWidth") && args.entries() == 1) {
        m_TextWidth = args[0].intValue();
        return true;
    }
    // M60: SetLocalizedText/SetSelectable and the rest are the shared
    // widget class -- see row_owner_ref.h.
    if (HandleSharedWidgetNative(methodName, args, returnValue)) return true;
    return SoftFailNativeCall("TextArea", methodName, args, returnValue);
}

}  // namespace sk_bindings
