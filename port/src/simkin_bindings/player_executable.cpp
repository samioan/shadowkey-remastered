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
        // ShowRaceInfo.s reads this back via GetTemp() -- same scratch
        // field ShowCharacterClass.s reads after ChooseCharacter() below;
        // whichever was picked most recently is what a following "show
        // info about what I just picked" screen would want.
        m_Temp = m_Race;
        return true;
    }
    if (methodName == skString("GetRace") && args.entries() == 0) {
        returnValue = skRValue(m_Race);
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
        // ShowCharacterClass.s reads this back via GetTemp() -- see the
        // ChooseRace() comment above for why the same field is shared.
        m_Temp = args[0].intValue();
        m_HasCreatedCharacter = true;
        return true;
    }
    if (methodName == skString("HasCreatedCharacter") && args.entries() == 0) {
        returnValue = skRValue(m_HasCreatedCharacter);
        return true;
    }
    if (methodName == skString("GetTemp") && args.entries() == 0) {
        returnValue = skRValue(m_Temp);
        return true;
    }
    if (methodName == skString("GetCharName") && args.entries() == 0) {
        returnValue = skRValue(skString(m_CharNameBuffer.c_str()));
        return true;
    }
    return SoftFailNativeCall("Player", methodName, args, returnValue);
}

}  // namespace sk_bindings
