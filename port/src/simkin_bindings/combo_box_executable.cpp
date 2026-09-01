#include "simkin_bindings/combo_box_executable.h"

#include "simkin_bindings/native_binding_common.h"
#include "skRValue.h"
#include "skRValueArray.h"

namespace sk_bindings {

ComboBoxExecutable::ComboBoxExecutable(int x, int y) : NativeStubExecutable("ComboBox"), m_X(x), m_Y(y) {}

bool ComboBoxExecutable::method(const skString& methodName, skRValueArray& args,
                                 skRValue& returnValue, skExecutableContext& context) {
    if (methodName == skString("SetWidth") && args.entries() == 1) {
        m_Width = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetNumericalMode") && args.entries() == 1) {
        m_Numerical = args[0].boolValue();
        return true;
    }
    if (methodName == skString("AddOption") && args.entries() >= 1) {
        m_Options.push_back(args[0].intValue());
        return true;
    }
    if (methodName == skString("SetOnEnterCallback") && args.entries() == 1) {
        m_OnEnterCallback = ToStdString(args[0].str());
        return true;
    }
    if (methodName == skString("SetCallback") && args.entries() == 1) {
        m_OnChangeCallback = ToStdString(args[0].str());
        return true;
    }
    if (methodName == skString("GetSelection") && args.entries() == 0) {
        returnValue = skRValue(m_Selection);
        return true;
    }
    if (methodName == skString("SetSelection") && args.entries() == 1) {
        m_Selection = args[0].intValue();
        return true;
    }
    return SoftFailNativeCall("ComboBox", methodName, args, returnValue);
}

}  // namespace sk_bindings
