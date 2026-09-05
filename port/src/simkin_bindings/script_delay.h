#pragma once

// M53: `Delay(seconds, tag)` / `StopDelay()` / `DelayReached(tag)` -- the
// engine's per-entity script timer, and the reason two scripts looked
// unplaceable.
//
// M52 named the three fields this uses (`Entity+0x10a` armed,
// `+0x10c` deadline, `+0x110` tag) from the binding side. The other half
// is `FUN_1006410c`, run once per frame per entity:
//
//     if (obj->armed && obj->deadline <= engine->clock) {
//         obj->armed = 0;
//         call the script's "DelayReached" with (int)obj->tag;
//     }
//
// and the arming side, `Delay`'s own case in the Entity dispatcher:
//
//     obj->armed    = 1;
//     obj->deadline = engine->clock + seconds * 0x100;
//     obj->tag      = second argument, or 0 when called with one argument
//
// **The clock is not a wall clock.** It is `engine+0x470 -> +0x460`, a
// cumulative counter advanced every frame by the same delta the animation
// player uses -- 8.8 fixed-point *seconds* (`elapsedMs * 256 / 1000`,
// clamped to [4, 64]). So `seconds * 0x100` is just "seconds, in 8.8",
// and the clock is reset to zero when a level loads.
//
// That matters twice over. It is why kUnitsPerSecond is 256 rather than a
// millisecond count -- and it is a real bug in the shipped game's save
// format, recorded here because this is where the two halves meet:
// `Entity`'s save writes `+0x10c` as `value - time(0)` and its loader adds
// its own `time(0)` back, the same treatment it gives `+0x118`. That is
// correct for `+0x118`, which really is `time(0) + n` (M52). It is wrong
// for `+0x10c`: subtracting and re-adding a wall clock leaves a *game*
// clock deadline shifted by however many real-world seconds passed
// between the save and the load, divided by 256. A `Delay(1, ...)` armed
// just before saving and reloaded a day later comes back as a deadline
// about five and a half minutes of game time away.

#include <cstdint>

#include "skRValue.h"
#include "skRValueArray.h"
#include "skString.h"

namespace sk_bindings {

// 0x100 clock units to the second -- 8.8 fixed point.
inline constexpr uint32_t kClockUnitsPerSecond = 256;

// The engine's own clock (`engine+0x470 -> +0x460`): accumulated 8.8
// fixed-point seconds, zeroed on a level load. Kept as its own tiny type
// so a caller cannot accidentally feed a timer milliseconds.
class GameClock {
public:
    void Reset() { m_Units = 0; }
    void Advance(float deltaSeconds) {
        if (deltaSeconds <= 0.0f) return;
        m_Units += static_cast<uint32_t>(deltaSeconds * kClockUnitsPerSecond);
    }
    uint32_t units() const { return m_Units; }

private:
    uint32_t m_Units = 0;
};

class ScriptDelay {
public:
    // `Delay(seconds)` / `Delay(seconds, tag)`. Re-arming replaces the
    // pending one, which is exactly what the chained scripts rely on:
    // crypt2/controller.s's DelayReached(s) ends by arming s+1.
    void Arm(const GameClock& clock, int seconds, int tag) {
        m_Armed = true;
        m_Deadline = clock.units() + static_cast<uint32_t>(seconds) * kClockUnitsPerSecond;
        m_Tag = tag;
    }
    void Stop() { m_Armed = false; }

    // True exactly once, on the frame the deadline passes -- the engine
    // clears the armed flag before dispatching, so a DelayReached that
    // re-arms is not immediately re-fired.
    bool Fire(const GameClock& clock, int* tagOut) {
        if (!m_Armed || m_Deadline > clock.units()) return false;
        m_Armed = false;
        if (tagOut) *tagOut = m_Tag;
        return true;
    }

    bool armed() const { return m_Armed; }
    int tag() const { return m_Tag; }
    uint32_t deadline() const { return m_Deadline; }

private:
    bool m_Armed = false;
    uint32_t m_Deadline = 0;
    int m_Tag = 0;
};

// The two bindings, for any entity class that can carry a timer. Returns
// true when it took the call, so a binding class chains this into its
// method() the same way it chains TryHandleRandom.
//
// `Delay` lives on the **Entity** root in the real engine, so every
// entity class has it; in this port the hosts are the two that any
// shipped `DelayReached` script actually lands in -- ItemExecutable
// (categories 3 and 8) and MonsterExecutable (category 2). All sixteen
// scripts and all fifty-nine of their placements are covered by those
// two.
bool TryHandleDelay(ScriptDelay& delay, const GameClock& clock,
                    const skString& methodName, skRValueArray& args,
                    skRValue& returnValue);

// The callback's name, verbatim from the wide literal at 0x100b1634 that
// FUN_1006410c passes to the interpreter.
extern const char* const kDelayReachedMethod;

}  // namespace sk_bindings
