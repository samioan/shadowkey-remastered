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
    //
    // Deliberately the *script's* accumulated turn only, not the full
    // heading: main.cpp composes this with DoorInstance::placementYaw
    // when it draws the door, and has since M15. headingRaw() below is
    // the composed one, for the stamp walk.
    return static_cast<float>(m_RotationRaw) / 65536.0f * kTwoPi;
}

void DoorExecutable::AttachTileStamp(TileStamp* stamp, int halfExtentX, int halfExtentY,
                                      int placementYawRaw) {
    m_TileStamp = stamp;
    m_HalfExtentX = halfExtentX;
    m_HalfExtentY = halfExtentY;
    m_PlacementYawRaw = placementYawRaw;
}

bool DoorExecutable::isTileStamped() const {
    // `ModelCollision::tileStamped()` -- kept as a literal rather than an
    // include, because `sk_bindings` does not link `sk_world`. Half a
    // tile, the same 128 the real `GameEngine_InitLevel` test uses.
    constexpr int kTileStampThreshold = 128;
    return m_HalfExtentX > kTileStampThreshold || m_HalfExtentY > kTileStampThreshold;
}

int DoorExecutable::headingRaw() const {
    return (m_PlacementYawRaw + m_RotationRaw) & 0xffff;
}

void DoorExecutable::ApplyTileStamp() {
    if (!m_TileStamp || !isTileStamped()) return;
    // Mask 4 and no other: that is what every real call site passes, and
    // it is the bit Zone::CircleHitsWall reads.
    m_TileStamp->StampEntityBox(positionX(), positionY(), headingRaw(), m_HalfExtentX,
                                 m_HalfExtentY, 0x04, !m_Passable);
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
        // M67: the real case 0x17 -- assign, then stamp or unstamp the
        // tile grid at the door's *current* heading. See the class
        // comment for why the ordering inside door.s makes that the only
        // correct moment to do it.
        m_Passable = args[0].boolValue();
        ApplyTileStamp();
        return true;
    }
    if (methodName == skString("AddRotationTurn") && args.entries() == 1) {
        m_RotationRaw += args[0].intValue();
        return true;
    }
    // M62: Entity bindings 0x30/0x34/0x35/0x36 (entity_position_ref.h).
    // A door is not only a thing that swings: `gate.s` is a **portcullis**,
    // and its entire open/close mechanism is
    //
    //     x = GetPositionX(); y = GetPositionY(); z = GetPositionZ() + 1200;
    //     SetPosition(x, y, z);
    //
    // -- it lifts straight up and drops back down, 1200 raw units (a little
    // under five tile-widths). `glcrcrwl/icegate.s` is the same script with
    // the ice-gate's own sounds. Neither could work before: the getters
    // soft-failed to 0, so the script would have computed (0, 0, 1200) and
    // teleported the gate to the world origin if the setter had existed.
    //
    // Crucially a door is **not** an actor (`vtable[0xc8]` is false for
    // it), so its teleport is exempt from the floor snap. A snapped
    // portcullis would drop straight back down and never open.
    if (HandleEntityPositionNative(methodName, args, returnValue)) return true;
    if (methodName == skString("OpenDoor")) {
        // M38: a trapped door's trigger callback opens the door itself --
        // crypt1.s's `OnOpenDoor[ (trigger, who) ]` does
        // `who.OpenDoor("Open Door")` when the trap is no longer armed.
        // The real notification order (FUN_1002e6cc) is "tell the trigger
        // list, then run the open", so by the time this callback lands the
        // port has already got the open queued for this same Use -- doing
        // it again here would swing the door twice. Accepted as a no-op so
        // the real callback runs to completion instead of throwing
        // "Method OpenDoor not found" and skipping everything after it.
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


// M38: see native_binding_common.h's StoreScriptObjectField().
bool DoorExecutable::setValue(const skString& fieldName, const skString& attribute,
                     const skRValue& value) {
    if (StoreScriptObjectField(m_ObjectFields, fieldName, value)) return true;
    return skScriptedExecutable::setValue(fieldName, attribute, value);
}

bool DoorExecutable::getValue(const skString& fieldName, const skString& attribute, skRValue& value) {
    if (LoadScriptObjectField(m_ObjectFields, fieldName, value)) return true;
    return skScriptedExecutable::getValue(fieldName, attribute, value);
}

}  // namespace sk_bindings
