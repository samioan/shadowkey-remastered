#include "simkin_bindings/door_executable.h"

#include <cstdio>

#include "simkin_bindings/native_binding_common.h"
#include "simkin_bindings/player_executable.h"
#include "skExecutableContext.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"

namespace sk_bindings {

DoorExecutable::DoorExecutable(const skString& filename, skExecutableContext& ctxt,
                                PlayerExecutable& player)
    : skScriptedExecutable(filename, ctxt), m_Player(player), m_Interpreter(ctxt.getInterpreter()) {}

float DoorExecutable::yawRadians() const {
    constexpr float kTwoPi = 6.28318530718f;
    // See door_executable.h's class comment -- 65536 raw units == one
    // full turn, same convention docs/ZONE_FORMAT.md confirmed for
    // .ent's rotOrScale fields.
    return static_cast<float>(m_RotationRaw) / 65536.0f * kTwoPi;
}

void DoorExecutable::InvokeOnUse() {
    if (!m_Interpreter) return;
    // M17: real placeholder arg for OnUse's "(s)" parameter -- same fix
    // as monster_executable.cpp's InvokeOnUse()/InvokeOnKilled() (see
    // their comments); door.s/door02.s's own OnUse(s) never happens to
    // read `s`, so this was latent here too rather than observably
    // broken, but the fix is the same either way.
    skRValueArray args;
    args.append(skRValue(0));
    skRValue ret;
    skExecutableContext ctxt(m_Interpreter);
    try {
        method(skString("OnUse"), args, ret, ctxt);
    } catch (skParseException& e) {
        std::printf("DoorExecutable: PARSE ERROR in OnUse(): %s\n", e.toString().ptr());
    } catch (skRuntimeException& e) {
        std::printf("DoorExecutable: RUNTIME ERROR in OnUse(): %s\n", e.toString().ptr());
    }
}

bool DoorExecutable::method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                             skExecutableContext& context) {
    if (methodName == skString("SetUseText") && args.entries() == 1) {
        m_UseTextId = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetMPUsable") && args.entries() == 1) {
        m_MpUsable = args[0].boolValue();
        return true;
    }
    if (methodName == skString("SetPassable") && args.entries() == 1) {
        m_Passable = args[0].boolValue();
        return true;
    }
    if (methodName == skString("AddRotationTurn") && args.entries() == 1) {
        m_RotationRaw += args[0].intValue();
        return true;
    }
    if (methodName == skString("DoorOpened")) {
        // Real signature is DoorOpened(self, isOpen, isNormal) -- network/
        // replication bookkeeping in the original (docs/PORT_ROADMAP.md:
        // this port has no multiplayer). The visible open/close state is
        // already fully captured by AddRotationTurn()/SetUseText() above,
        // so there's nothing left for this call to do here.
        return true;
    }
    if (methodName == skString("GetPlayer") && args.entries() == 0) {
        returnValue = skRValue(static_cast<skiExecutable*>(&m_Player), false);
        return true;
    }
    if (skScriptedExecutable::method(methodName, args, returnValue, context)) {
        return true;
    }
    return SoftFailNativeCall("Door", methodName, args, returnValue);
}

}  // namespace sk_bindings
