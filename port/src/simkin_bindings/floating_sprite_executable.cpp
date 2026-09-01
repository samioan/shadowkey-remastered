#include "simkin_bindings/floating_sprite_executable.h"

#include "simkin_bindings/native_binding_common.h"
#include "skRValue.h"
#include "skRValueArray.h"

namespace sk_bindings {

bool FloatingSpriteExecutable::method(const skString& methodName, skRValueArray& args,
                                       skRValue& returnValue, skExecutableContext& context) {
    if (methodName == skString("SetSprite") && args.entries() == 1) {
        m_SpriteId = args[0].intValue();
        return true;
    }
    return SoftFailNativeCall("FloatingSprite", methodName, args, returnValue);
}

}  // namespace sk_bindings
