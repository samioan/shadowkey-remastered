// M0/M1: window + solid-color backbuffer (M0), a fixed 40ms tick loop and
// keyboard-driven InputState matching the original's binding scheme (M1).
// See C:\Users\Admin\.claude\plans\vast-wandering-summit.md for the full
// staged plan.
#include <algorithm>
#include <cstdio>

#include "engine/game_clock.h"
#include "engine/input_state.h"
#include "engine/pc_key_map.h"
#include "graphics/backbuffer.h"
#include "platform/win32/window.h"

int main() {
    sk::Window window(sk::Backbuffer::kWidth * 3, sk::Backbuffer::kHeight * 3,
                       L"shadowkey-port (M1 scaffold)");

    sk::InputState input;
    window.SetKeyCallback([&](int vkCode, bool down) {
        if (auto slot = sk::MapPcKeyToButtonSlot(vkCode)) {
            input.SetButton(*slot, down);
        }
    });

    sk::Backbuffer backbuffer;
    sk::GameClock clock;

    // M1 smoke test: a small square driven by Action::MoveForward/Backward/
    // TurnLeft/TurnRight through the *bound* action layer (not raw slots),
    // proving the remap indirection round-trips correctly end to end, and
    // a per-tick counter proving the 40ms cadence is real (logged every 25
    // ticks = ~1s).
    int markerX = sk::Backbuffer::kWidth / 2;
    int markerY = sk::Backbuffer::kHeight / 2;
    uint64_t tickCount = 0;

    std::printf("shadowkey-port: M1 -- arrow keys move the marker (through the "
                "MoveForward/Backward/TurnLeft/TurnRight action bindings).\n");

    window.RunMessageLoop([&]() {
        if (window.ShouldClose()) return;
        if (clock.PollTick()) {
            input.BeginFrame();

            if (input.GetBoundButton(sk::Action::TurnLeft)) markerX -= 1;
            if (input.GetBoundButton(sk::Action::TurnRight)) markerX += 1;
            if (input.GetBoundButton(sk::Action::MoveForward)) markerY -= 1;
            if (input.GetBoundButton(sk::Action::MoveBackward)) markerY += 1;
            markerX = std::max(0, std::min(sk::Backbuffer::kWidth - 1, markerX));
            markerY = std::max(0, std::min(sk::Backbuffer::kHeight - 1, markerY));

            backbuffer.Fill(sk::PackRGB565(32, 32, 48));
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    backbuffer.SetPixel(markerX + dx, markerY + dy, sk::PackRGB565(255, 220, 80));
                }
            }
            window.Present(backbuffer);

            ++tickCount;
            if (tickCount % 25 == 0) {
                std::printf("shadowkey-port: tick %llu (~%llus)\n",
                            static_cast<unsigned long long>(tickCount),
                            static_cast<unsigned long long>(tickCount / 25));
            }
        }
    });

    std::printf("shadowkey-port: window closed cleanly after %llu ticks.\n",
                static_cast<unsigned long long>(tickCount));
    return 0;
}
