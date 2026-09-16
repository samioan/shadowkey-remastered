#include "simkin_bindings/native_binding_common.h"

#include "skTreeNodeObject.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace sk_bindings {

std::string ToStdString(const skString& s) {
    return std::string(s.ptr(), s.length());
}

namespace {
// SK_DEBUG_SUITE (M68) -- see native_binding_common.h.
SoftFailObserver g_SoftFailObserver = nullptr;
}  // namespace

void SetSoftFailObserver(SoftFailObserver observer) {
    g_SoftFailObserver = observer;
}

bool SoftFailNativeCall(const char* objectDebugName, const skString& methodName,
                         skRValueArray& args, skRValue& returnValue) {
    // The argument list is joined once and reused for both the log line and
    // the observer, so installing the observer costs one string build on a
    // path that was already formatting the same text.
    std::string joined;
    for (unsigned int i = 0; i < args.entries(); ++i) {
        if (i) joined += ", ";
        joined += ToStdString(args[i].str());
    }
    const std::string method = ToStdString(methodName);
    std::printf("  [soft-fail] %s: %s(%s) -- not implemented\n", objectDebugName, method.c_str(),
                joined.c_str());
    if (g_SoftFailObserver) {
        g_SoftFailObserver(objectDebugName, method.c_str(), joined.c_str());
    }
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

bool TryHandleMultiplayerQuery(const skString& methodName, skRValueArray& args,
                                skRValue& returnValue) {
    if (args.entries() != 0) return false;
    if (methodName != skString("IsMultiplayer") &&
        methodName != skString("IsMultiplayerClient")) {
        return false;
    }
    // See the declaration: `engine+0x5c0` is zero in this port and there is
    // nothing that could set it, so both answer false.
    returnValue = skRValue(false);
    return true;
}

}  // namespace sk_bindings
