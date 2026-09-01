#include "simkin_bindings/floating_text_executable.h"

#include "simkin_bindings/native_binding_common.h"
#include "skRValue.h"
#include "skRValueArray.h"

namespace sk_bindings {

FloatingTextExecutable::FloatingTextExecutable(int textId, int x, int y, bool justify, int color)
    : NativeStubExecutable("FloatingText"),
      m_TextId(textId),
      m_X(x),
      m_Y(y),
      m_Color(color),
      m_Justify(justify) {}

bool FloatingTextExecutable::method(const skString& methodName, skRValueArray& args,
                                     skRValue& returnValue, skExecutableContext& context) {
    if (methodName == skString("SetFontNum") && args.entries() == 1) {
        m_FontNum = args[0].intValue();
        return true;
    }
    return SoftFailNativeCall("FloatingText", methodName, args, returnValue);
}

}  // namespace sk_bindings
