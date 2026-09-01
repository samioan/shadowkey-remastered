#include "simkin_bindings/native_binding_common.h"

#include <cstdio>

namespace sk_bindings {

std::string ToStdString(const skString& s) {
    return std::string(s.ptr(), s.length());
}

bool SoftFailNativeCall(const char* objectDebugName, const skString& methodName,
                         skRValueArray& args, skRValue& returnValue) {
    std::printf("  [soft-fail] %s: %s(", objectDebugName, ToStdString(methodName).c_str());
    for (unsigned int i = 0; i < args.entries(); ++i) {
        if (i) std::printf(", ");
        std::printf("%s", ToStdString(args[i].str()).c_str());
    }
    std::printf(") -- not implemented\n");
    returnValue = skRValue(0);
    return true;
}

}  // namespace sk_bindings
