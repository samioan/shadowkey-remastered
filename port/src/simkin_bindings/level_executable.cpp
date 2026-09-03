#include "simkin_bindings/level_executable.h"

#include <cstdio>

#include "simkin_bindings/native_binding_common.h"
#include "simkin_bindings/player_executable.h"
#include "skRValue.h"
#include "skRValueArray.h"

namespace sk_bindings {

bool LevelExecutable::method(const skString& methodName, skRValueArray& args,
                              skRValue& returnValue, skExecutableContext& context) {
    if (methodName == skString("GetEntity") && args.entries() == 1) {
        auto it = m_Entities.find(ToStdString(args[0].str()));
        if (it != m_Entities.end()) {
            returnValue = skRValue(it->second, false);
        } else {
            // Matches the "null" global game_constants.cpp registers --
            // see this class's header comment for why this specific
            // default (not a dedicated sentinel type) is what makes real
            // scripts' `if (X != null)` checks evaluate correctly.
            returnValue = skRValue();
        }
        return true;
    }
    if (methodName == skString("GetPlayer") && args.entries() == 0) {
        returnValue = skRValue(static_cast<skiExecutable*>(&m_Player), false);
        return true;
    }
    if (methodName == skString("PlayAmbient")) {
        // No audio system in this port (consistent with every prior
        // milestone) -- real signature is PlayAmbient(soundId, volume);
        // logged and no-op'd rather than a dedicated SoftFailNativeCall so
        // it doesn't read as an unimplemented-and-unexpected call.
        std::printf("Level: PlayAmbient(...) -- no audio system, ignored\n");
        returnValue = skRValue(0);
        return true;
    }
    return SoftFailNativeCall("Level", methodName, args, returnValue);
}

}  // namespace sk_bindings
