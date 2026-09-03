#include "simkin_bindings/native_binding_common.h"

#include "skTreeNodeObject.h"

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

// M38: see native_binding_common.h for why these exist.
bool StoreScriptObjectField(std::map<std::string, skRValue>& fields, const skString& fieldName,
                             const skRValue& value) {
    const std::string key = ToStdString(fieldName);
    skiExecutable* obj = value.obj();
    // TREENODE_TYPE objects are Simkin's own script-side structures and
    // *are* handled correctly by the TreeNode path (it deep-copies them),
    // so only native objects are diverted here.
    if (value.type() == skRValue::T_Object && obj && obj->executableType() != TREENODE_TYPE) {
        fields[key] = value;
        return true;
    }
    // Reassigning a field to a plain value has to drop any object that was
    // there, or the stale object would keep shadowing the new value.
    fields.erase(key);
    return false;
}

bool LoadScriptObjectField(const std::map<std::string, skRValue>& fields,
                            const skString& fieldName, skRValue& value) {
    auto it = fields.find(ToStdString(fieldName));
    if (it == fields.end()) return false;
    value = it->second;
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
