#pragma once

// Shared helpers for the M3 native-binding layer. See the port scaffold
// plan's "Soft-fail native bindings" decision: any native call a binding
// class doesn't specifically implement gets logged and answered with a
// benign default instead of throwing, so scripts can run to completion
// well before every one of the ~700 bindings exists.

#include <string>

#include "skRValue.h"
#include "skRValueArray.h"
#include "skString.h"

namespace sk_bindings {

// Converts a skString (narrow-char build) to a std::string.
std::string ToStdString(const skString& s);

// Logs "objectDebugName: method_name(args...) -- not implemented, soft-failing"
// to stdout and sets returnValue to a benign default (0 / false / "").
// Always returns true, so the interpreter treats the call as handled
// rather than throwing a "Method not found" runtime error.
bool SoftFailNativeCall(const char* objectDebugName, const skString& methodName,
                         skRValueArray& args, skRValue& returnValue);

}  // namespace sk_bindings
