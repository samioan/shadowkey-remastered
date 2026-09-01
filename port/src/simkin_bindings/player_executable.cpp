#include "simkin_bindings/player_executable.h"

#include "simkin_bindings/native_binding_common.h"
#include "skRValue.h"
#include "skRValueArray.h"

namespace sk_bindings {

PlayerExecutable::PlayerExecutable() : NativeStubExecutable("Player") {}

bool PlayerExecutable::method(const skString& methodName, skRValueArray& args,
                               skRValue& returnValue, skExecutableContext& context) {
    if (methodName == skString("SetPlayerName") && args.entries() == 1) {
        m_Name = ToStdString(args[0].str());
        return true;
    }
    if (methodName == skString("SetSex") && args.entries() == 1) {
        m_Sex = args[0].intValue();
        return true;
    }
    if (methodName == skString("ChooseRace") && args.entries() == 1) {
        m_Race = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetPortraitID") && args.entries() == 1) {
        m_PortraitId = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetGold") && args.entries() == 1) {
        m_Gold = args[0].intValue();
        return true;
    }
    if (methodName == skString("GetGold") && args.entries() == 0) {
        returnValue = skRValue(m_Gold);
        return true;
    }
    if (methodName == skString("StatModGold") && args.entries() == 1) {
        m_Gold += args[0].intValue();
        return true;
    }
    if (methodName == skString("ChooseCharacter") && args.entries() == 1) {
        // args[0] is a class id -- creating a character with the chosen
        // class is the last step of new-game flow.
        m_HasCreatedCharacter = true;
        return true;
    }
    if (methodName == skString("HasCreatedCharacter") && args.entries() == 0) {
        returnValue = skRValue(m_HasCreatedCharacter);
        return true;
    }
    return SoftFailNativeCall("Player", methodName, args, returnValue);
}

}  // namespace sk_bindings
