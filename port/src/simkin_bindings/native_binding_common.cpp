#include "simkin_bindings/native_binding_common.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

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

bool TryHandleRandom(const skString& methodName, skRValueArray& args, skRValue& returnValue) {
    if (methodName != skString("Random") || args.entries() != 2) return false;
    int a = args[0].intValue(), b = args[1].intValue();
    int lo = (std::min)(a, b), hi = (std::max)(a, b);
    returnValue = skRValue(hi > lo ? lo + std::rand() % (hi - lo + 1) : lo);
    return true;
}

}  // namespace sk_bindings
