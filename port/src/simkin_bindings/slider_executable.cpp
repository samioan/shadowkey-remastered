#include "simkin_bindings/slider_executable.h"

#include "simkin_bindings/native_binding_common.h"
#include "skRValue.h"
#include "skRValueArray.h"

namespace sk_bindings {

bool SliderExecutable::method(const skString& methodName, skRValueArray& args,
                              skRValue& returnValue, skExecutableContext& context) {
    // No real script in the corpus calls anything on the object
    // AddMenuSlider() returns (options.s discards it), so these are here
    // for symmetry with the other widget bindings rather than to satisfy a
    // known call site.
    if (methodName == skString("SetValue") && args.entries() == 1) {
        SetValue(args[0].intValue());
        return true;
    }
    if (methodName == skString("GetValue") && args.entries() == 0) {
        returnValue = skRValue(m_Value);
        return true;
    }
    return SoftFailNativeCall("MenuSlider", methodName, args, returnValue);
}

}  // namespace sk_bindings
