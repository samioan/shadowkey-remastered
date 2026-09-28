#pragma once
#include <chrono>
#include <thread>

// Reproduces the original engine's fixed 40ms/25Hz tick rate (see
// docs/RENDER_LOOP.md: InitGameLoopTimer_CPeriodic starts a CPeriodic timer
// at a hardcoded 40000us interval, and GameTick_UpdateAndPresent does both
// simulation update and frame present inside that single tick -- kept
// coupled here too, faithfully, for this milestone). Decoupling update
// rate from present rate is a real, already-documented port-goal
// improvement (see that doc's "For a PC port this means..." section).
// M119: done, as a setting -- the tick stays exactly this; above the
// Original frame rate, main.cpp draws interpolated frames between ticks
// (engine/frame_interpolation.h). At Original, tick and present are still
// the one coupled step described above.
namespace sk {

class GameClock {
public:
    static constexpr std::chrono::microseconds kTickInterval{40000};

    GameClock() : nextTick_(Clock::now() + kTickInterval) {}

    // Returns true if a tick is due (caller should Update()+Present()),
    // sleeping briefly otherwise to avoid busy-spinning the CPU.
    bool PollTick() {
        auto now = Clock::now();
        if (now >= nextTick_) {
            nextTick_ += kTickInterval;
            // If we've fallen behind by more than a few ticks (debugger
            // pause, window drag, etc.), don't try to burn through a huge
            // backlog of catch-up ticks -- resync to "next tick from now".
            if (nextTick_ < now) nextTick_ = now + kTickInterval;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return false;
    }

private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point nextTick_;
};

}  // namespace sk
