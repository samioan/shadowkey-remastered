#include "simkin_bindings/script_delay.h"

namespace sk_bindings {

const char* const kDelayReachedMethod = "DelayReached";

bool TryHandleDelay(ScriptDelay& delay, const GameClock& clock,
                    const skString& methodName, skRValueArray& args,
                    skRValue& returnValue) {
    if (methodName == skString("Delay") && args.entries() >= 1) {
        // The dispatcher reads the tag only when a second argument is
        // present and otherwise stores 0 -- `Delay()` with no arguments
        // reaches the `args.entries() >= 1` guard above and is left to
        // soft-fail, which is what the two bare `Delay()` calls in the
        // corpus deserve.
        const int seconds = args[0].intValue();
        const int tag = args.entries() >= 2 ? args[1].intValue() : 0;
        delay.Arm(clock, seconds, tag);
        returnValue = skRValue();
        return true;
    }
    if (methodName == skString("StopDelay")) {
        delay.Stop();
        returnValue = skRValue();
        return true;
    }
    return false;
}

}  // namespace sk_bindings
