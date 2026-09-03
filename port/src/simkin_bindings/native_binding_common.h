#pragma once

// Shared helpers for the M3 native-binding layer. See the port scaffold
// plan's "Soft-fail native bindings" decision: any native call a binding
// class doesn't specifically implement gets logged and answered with a
// benign default instead of throwing, so scripts can run to completion
// well before every one of the ~700 bindings exists.

#include <map>
#include <string>

#include "simkin_bindings/native_stub_executable.h"
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

// M38: keep a native object assigned to a *pre-declared* script field.
//
// The vendored Simkin's skTreeNodeObject::setValue() stores a non-TreeNode
// object by writing `child->data(v.str())` -- i.e. it keeps the object's
// string form and throws the object away. That is invisible for an
// undeclared identifier (which becomes a stack-frame local instead, and
// locals hold real skRValues), but fatal for a declared one:
//
//   lothcav.s declares `fight41` and `doorTrigger` at the top of the
//   class and then does `fight41 = AddEncounters("battle41");
//   fight41.AddRandomSets(272, 1);` -- the second line threw "Method
//   AddRandomSets not found", aborting the whole zone Init() *before* its
//   three trap triggers were ever registered. ghstpass.s does the same
//   thing without declaring the fields, which is why only some zones hit
//   this.
//
// The shipped game's own interpreter is a modified Simkin and clearly does
// not lose the object, so this is a port-side incompatibility, not a
// scripting error. These two helpers implement the side table a binding
// class overriding setValue()/getValue() uses to hold object-valued
// fields; anything that is not a native object falls through to the normal
// TreeNode path, so ordinary `saved_X` flags are completely unaffected.
bool StoreScriptObjectField(std::map<std::string, skRValue>& fields, const skString& fieldName,
                             const skRValue& value);
bool LoadScriptObjectField(const std::map<std::string, skRValue>& fields,
                            const skString& fieldName, skRValue& value);

// M24: a real object handle with no behavior of its own -- for a factory
// call this port doesn't implement (e.g. ghstpass.s's own unconditional
// `fight12 = AddEncounters(...); fight12.AddRandomSets(...);`, an
// "Encounter spawner" class not attempted here) whose result still needs
// *further* real method calls to not throw. Returning `SoftFailNativeCall`
// -like default straight from the factory handler would make the result
// an int (T_Int), and the vendored interpreter's own `makeMethodCall()`
// only proceeds for `T_Object` -- so a genuinely-missing object crashes
// the *calling script*, not just soft-fails the one call. This is a real
// object instead; every method called on it soft-fails individually
// (through the base `SoftFailNativeCall`, same logging), letting the rest
// of the calling script run to completion. Not a general-purpose
// substitute for implementing a real class -- only for a factory result
// nothing in this port reads back.
class InertHandleExecutable : public NativeStubExecutable {
public:
    explicit InertHandleExecutable(const char* debugTypeName)
        : NativeStubExecutable(debugTypeName) {}
    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext&) override {
        return SoftFailNativeCall(debugTypeName(), methodName, args, returnValue);
    }
};

}  // namespace sk_bindings
