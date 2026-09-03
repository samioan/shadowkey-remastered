#include "simkin_bindings/zone_script_executable.h"

#include <cstdio>

#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/native_binding_common.h"
#include "simkin_bindings/player_executable.h"
#include "skExecutableContext.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"

namespace sk_bindings {

bool TriggerExecutable::method(const skString& methodName, skRValueArray& args,
                                skRValue& returnValue, skExecutableContext& context) {
    if (methodName == skString("SetEntityID") && args.entries() == 1) {
        m_EntityId = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetLimit") && args.entries() == 1) {
        m_Limit = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetCallback") && args.entries() == 1) {
        m_Callback = ToStdString(args[0].str());
        return true;
    }
    if (methodName == skString("RemainActive")) {
        // Real, documented-but-inert -- see zone_script_executable.h's
        // class comment: this port doesn't reproduce the distinction
        // between "fires once" and "stays armed" for the kill-count
        // variant, only the physical-trap variant (not implemented at
        // all) is where real corpus usage suggests it actually matters.
        return true;
    }
    // AddEntity/SetTrap/SetDoor/OpenDoor/ShowDamageMessage/IsActive --
    // the physical-trap variant's own vocabulary, real but not
    // implemented (needs real trigger-volume/position data this port
    // doesn't have) -- soft-fails through, same as anything else this
    // class doesn't specifically answer.
    return SoftFailNativeCall("Trigger", methodName, args, returnValue);
}

bool TriggerExecutable::NotifyKilled() {
    if (m_Fired || m_Limit <= 0) return false;
    ++m_KillCount;
    if (m_KillCount < m_Limit) return false;
    m_Fired = true;
    return true;
}

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
    if (methodName == skString("AddTrigger") && args.entries() == 1) {
        // M24: real name (e.g. "zombiesKilled") is discarded -- nothing in
        // this port looks a trigger back up by it (unlike Level.GetEntity
        // ()'s own name registry, no real script ever does
        // `Level.GetTrigger("...")` or similar). Owned here for the
        // zone's whole lifetime; the script only holds a non-owning
        // reference (see class comment).
        auto trigger = std::make_unique<TriggerExecutable>();
        returnValue = skRValue(static_cast<skiExecutable*>(trigger.get()), false);
        m_Triggers.push_back(std::move(trigger));
        return true;
    }
    if (methodName == skString("AddEncounters")) {
        // M24: real ("Encounter spawner" class, docs/SIMKIN_NATIVE_API.md)
        // but not implemented -- ghstpass.s's own Init() calls this
        // unconditionally (`fight12 = AddEncounters(...);
        // fight12.AddRandomSets(...);` x5, no if-guard), so it has to
        // return a *real object* (InertHandleExecutable,
        // native_binding_common.h's own comment) rather than soft-failing
        // to an int -- otherwise the very next `.AddRandomSets()` call
        // would throw "Cannot call Method ... on a non object" and abort
        // the rest of this real script's Init(), not just this one call.
        returnValue = skRValue(new InertHandleExecutable("Encounter"), true);
        return true;
    }
    if (skScriptedExecutable::method(methodName, args, returnValue, context)) {
        return true;
    }
    return SoftFailNativeCall("ZoneScript", methodName, args, returnValue);
}

void ZoneScriptExecutable::NotifyKilled(int typeId) {
    for (auto& trigger : m_Triggers) {
        if (trigger->entityId() != typeId) continue;
        if (!trigger->NotifyKilled()) continue;
        skRValueArray args;
        args.append(skRValue(0));  // placeholder for the callback's "(s)" parameter
        skRValue ret;
        skExecutableContext ctxt(&m_Stack.interpreter());
        try {
            method(skString(trigger->callback().c_str()), args, ret, ctxt);
        } catch (skParseException& e) {
            std::printf("ZoneScript: PARSE ERROR in trigger callback \"%s\": %s\n",
                        trigger->callback().c_str(), e.toString().ptr());
        } catch (skRuntimeException& e) {
            std::printf("ZoneScript: RUNTIME ERROR in trigger callback \"%s\": %s\n",
                        trigger->callback().c_str(), e.toString().ptr());
        }
    }
}

}  // namespace sk_bindings
