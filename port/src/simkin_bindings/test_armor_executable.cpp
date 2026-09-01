#include "simkin_bindings/test_armor_executable.h"

#include <cstdio>

#include "skRValue.h"
#include "skRValueArray.h"

namespace sk_bindings {

namespace {

void LogCall(const char* name, skRValueArray& args) {
    std::printf("  native call: %s(", name);
    for (unsigned int i = 0; i < args.entries(); ++i) {
        if (i) std::printf(", ");
        std::printf("%s", args[i].str().ptr());
    }
    std::printf(")\n");
}

}  // namespace

bool TestArmorExecutable::method(const skString& methodName, skRValueArray& args,
                                  skRValue& returnValue, skExecutableContext& context) {
    static const char* kLoggedNatives[] = {
        "SetName",       "SetShortName", "SetItemDescription", "SetArmorValue",
        "SetArmorType",  "SetCost",      "SetMarketValue",     "SetArmorConstraint",
    };
    for (const char* native : kLoggedNatives) {
        if (methodName == skString(native)) {
            LogCall(native, args);
            return true;
        }
    }
    // Not one of ours -- fall back to the script-defined methods (e.g. "Init"
    // itself, which lives in the .s file's own TreeNode tree).
    return skScriptedExecutable::method(methodName, args, returnValue, context);
}

}  // namespace sk_bindings
