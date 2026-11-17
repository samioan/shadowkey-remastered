#include "simkin_bindings/trap_entity.h"

#include "skRValue.h"
#include "skRValueArray.h"
#include "skString.h"

namespace sk_bindings {

namespace {

// FUN_10090288.
void ResetSpring(TrapEntityState& trap) {
    trap.animTime = 0;
    trap.sprung = false;
    trap.animAccum = 0;
}

// One animation step: the block FUN_1008ff14 runs once for type 2 and
// twice for type 1 (once inline, once at LAB_10090120, which the inline
// copy always falls into -- its own reset included).
void StepSpring(TrapEntityState& trap, int frameDelta) {
    trap.animTime += frameDelta;
    if (trap.animTime <= trap.animDuration) {
        const int16_t stride =
            static_cast<int16_t>(static_cast<uint32_t>(trap.animTime * 10) >> 8);
        if (trap.animTime < (trap.animDuration >> 1)) {
            trap.z = static_cast<int16_t>(trap.z + static_cast<int16_t>(trap.direction) * stride);
            return;
        }
        if (trap.z < trap.restZ) {
            trap.z = static_cast<int16_t>(trap.z + stride);
            return;
        }
        trap.z = static_cast<int16_t>(trap.restZ);
    }
    ResetSpring(trap);
}

}  // namespace

void TickTrapEntity(TrapEntityState& trap, int frameDelta, const TrapTickHooks& hooks) {
    if (!trap.active) return;
    trap.checkAccum += frameDelta;
    if (trap.checkAccum < 0x40) return;
    trap.checkAccum = 0;

    if (!trap.sprung) {
        if (trap.rearmCountdown < 1) {
            const int distance = hooks.distanceToPlayer ? hooks.distanceToPlayer() : 0x7fffffff;
            if (trap.range <= distance) return;  // out of range: no countdown
            const bool avoided = hooks.playerAvoids && hooks.playerAvoids();
            if (!avoided && hooks.notifyTriggers && hooks.notifyTriggers()) {
                trap.sprung = true;
                trap.restZ = trap.z;
            }
            trap.rearmCountdown = trap.rearmSeconds << 8;
        } else {
            // Note: one frame's delta per 0x40 window, not the window --
            // the engine's own, which makes the 7 "seconds" much longer.
            trap.rearmCountdown -= frameDelta;
        }
        return;
    }

    trap.animAccum += frameDelta;
    if (trap.animAccum < 0x2e) return;
    if (trap.trapType == 1 && hooks.playMasherSound) hooks.playMasherSound();
    trap.animAccum = 0;
    if (trap.trapType == 1) {
        StepSpring(trap, frameDelta);
        StepSpring(trap, frameDelta);
    } else if (trap.trapType == 2) {
        StepSpring(trap, frameDelta);
    }
}

bool TrapEntityExecutable::method(const skString& methodName, skRValueArray& args,
                                   skRValue& returnValue, skExecutableContext& context) {
    // FUN_1008fd28: three wcscmp'd names, everything else to the actor base.
    if (methodName == skString("SetTrapType") && args.entries() >= 1) {
        m_State.trapType = static_cast<int16_t>(args[0].intValue());
        if (m_State.trapType == 1) m_State.direction = -1;
        return true;
    }
    if (methodName == skString("SetAttackRange") && args.entries() >= 1) {
        m_State.range = static_cast<int16_t>(args[0].intValue());
        return true;
    }
    if (methodName == skString("SetActive") && args.entries() >= 1) {
        m_State.active = args[0].boolValue();
        return true;
    }
    return skScriptedExecutable::method(methodName, args, returnValue, context);
}

}  // namespace sk_bindings
