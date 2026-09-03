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

// M21: shared "Random(min,max)" handler -- a real, bare-reachable global
// (the GameEngine root class every script's default reachable-class set
// chains in, docs/SIMKIN_NATIVE_API.md, same footing as Level/GetPlayer)
// called by real scripts for randomized setup values (monsters/
// azra_rat.s's SetScale(Random(206,306)), loot_gold6-10.s's
// Item.SetQuantity(Random(6,10))) -- previously unimplemented anywhere in
// this port (soft-failed to 0), a real, pre-existing gap this closes.
// Returns true (and fills returnValue) on a match; callers chain this
// into their own method() before falling through to SoftFailNativeCall,
// same "try, then soft-fail" shape as every other binding class.
bool TryHandleRandom(const skString& methodName, skRValueArray& args, skRValue& returnValue);

}  // namespace sk_bindings
