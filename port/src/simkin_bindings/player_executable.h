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

private:
    std::string m_Name;
    int m_Sex = 0;
    int m_Race = 0;
    int m_PortraitId = 0;
    int m_Gold = 0;
    bool m_HasCreatedCharacter = false;
};

}  // namespace sk_bindings
