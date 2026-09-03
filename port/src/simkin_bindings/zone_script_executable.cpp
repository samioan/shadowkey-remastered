#include "simkin_bindings/zone_script_executable.h"

#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/native_binding_common.h"
#include "simkin_bindings/player_executable.h"
#include "skRValue.h"
#include "skRValueArray.h"

namespace sk_bindings {

ZoneScriptExecutable::ZoneScriptExecutable(const skString& filename, skExecutableContext& ctxt,
                                            MenuStack& stack)
    : skScriptedExecutable(filename, ctxt), m_Stack(stack) {}

bool ZoneScriptExecutable::method(const skString& methodName, skRValueArray& args,
                                   skRValue& returnValue, skExecutableContext& context) {
    if (methodName == skString("GetPlayer") && args.entries() == 0) {
        returnValue = skRValue(static_cast<skiExecutable*>(&m_Stack.player()), false);
        return true;
    }
    if (methodName == skString("GetEntity") && args.entries() == 1) {
        // M23: azra.s calls this both bare (`Trinket = GetEntity(
        // "trinket")`) and Level-qualified (`M1 = Level.GetEntity("m1")`)
        // -- both need to work; delegate to the same registry either way.
        skRValueArray levelArgs;
        levelArgs.append(args[0]);
        return m_Stack.level().method(skString("GetEntity"), levelArgs, returnValue, context);
    }
    if (methodName == skString("isObject") && args.entries() == 1) {
        // M23: a plain "is this a real object reference" check -- e.g.
        // `if (isObject(Trinket) = true)`. True only for a genuine
        // T_Object (a found Level.GetEntity() result); the "null" global
        // (game_constants.cpp) is a blank T_String, so this is exactly
        // the same real/not-found distinction `!= null` checks already
        // rely on, just phrased as a function instead of a comparison.
        returnValue = skRValue(args[0].type() == skRValue::T_Object);
        return true;
    }
    if (methodName == skString("SetZone") && args.entries() == 2) {
        // M23: azra.s's own bare `SetZone(1, 2000)` -- real corpus
        // research (docs/SIMKIN_NATIVE_API.md) placed this on the "Zone
        // effects" class, bare-reachable the same way GetEntity/GetPlayer
        // are. Real meaning unconfirmed (no getter anywhere reads it
        // back) -- stored only so the call doesn't soft-fail-log on
        // every zone load; nothing in this port consumes the values yet.
        return true;
    }
    if (skScriptedExecutable::method(methodName, args, returnValue, context)) {
        return true;
    }
    return SoftFailNativeCall("ZoneScript", methodName, args, returnValue);
}

}  // namespace sk_bindings
