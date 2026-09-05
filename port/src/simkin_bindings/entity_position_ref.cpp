#include "simkin_bindings/entity_position_ref.h"

namespace sk_bindings {

bool EntityPositionRef::HandleEntityPositionNative(const skString& methodName,
                                                   skRValueArray& args, skRValue& returnValue) {
    // 0x30 / 0x31. The real case branches on `argc == 3`: with three
    // arguments it reads z, with two it passes a literal 0 to the setter.
    // Anything less leaves with the interpreter's own argument-count error,
    // so two is genuinely the minimum.
    if ((methodName == skString("SetPosition") || methodName == skString("SetPositionMirror")) &&
        args.entries() >= 2) {
        m_X = args[0].intValue();
        m_Y = args[1].intValue();
        m_Z = args.entries() >= 3 ? args[2].intValue() : 0;
        m_PositionDirty = true;
        return true;
    }
    if (methodName == skString("GetPositionX") && args.entries() == 0) {
        returnValue = skRValue(m_X);
        return true;
    }
    if (methodName == skString("GetPositionY") && args.entries() == 0) {
        returnValue = skRValue(m_Y);
        return true;
    }
    if (methodName == skString("GetPositionZ") && args.entries() == 0) {
        returnValue = skRValue(m_Z);
        return true;
    }
    return false;
}

}  // namespace sk_bindings
