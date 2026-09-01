#pragma once

// Native binding for the object GetPlayer() returns. Only the fields the
// main-menu / new-character-creation chain actually reads back (name,
// sex, race, portrait, gold, "has a character been created yet") are
// tracked for real; everything else (combat stats, inventory, ...) is out
// of scope for the main-menu milestone and soft-fails.

#include <string>

#include "simkin_bindings/native_stub_executable.h"

namespace sk_bindings {

class PlayerExecutable : public NativeStubExecutable {
public:
    PlayerExecutable();

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

    // Name-entry buffer that OpenEditText's on-screen keyboard would fill
    // character by character on the real device; namechar.s's Done()/
    // NameCharBack()/OnRightSoftKey() all read it back via GetCharName()
    // and then commit it with SetPlayerName(). On PC, main.cpp fills this
    // directly from WM_CHAR (see MenuExecutable::textEntryActive()).
    void AppendCharNameChar(char c) { m_CharNameBuffer.push_back(c); }
    void BackspaceCharName() {
        if (!m_CharNameBuffer.empty()) m_CharNameBuffer.pop_back();
    }
    const std::string& charNameBuffer() const { return m_CharNameBuffer; }

private:
    std::string m_Name;
    std::string m_CharNameBuffer;
    int m_Sex = 0;
    int m_Race = 0;
    int m_PortraitId = 0;
    int m_Gold = 0;
    int m_Temp = 0;
    bool m_HasCreatedCharacter = false;
};

}  // namespace sk_bindings
