// M137: the boot sequence -- FUN_100298a4's start state, FUN_10068e0c's
// boot arms and FUN_1004fc50's fade. See engine/boot_sequence.h. Needs no
// game data.
#include <cstdio>
#include <string>

#include "engine/boot_sequence.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("  %-86s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    if (!ok) ++g_failures;
}

constexpr int kDelta = 0x100 / 25;  // the 40ms tick, as kAiFrameDeltaUnits

// Ticks with no input until the mode changes; returns the ticks taken.
int RunUntilModeChanges(sk::BootSequence& boot, int limit = 2000) {
    const sk::BootMode start = boot.mode();
    for (int i = 1; i <= limit; ++i) {
        boot.Step(kDelta, false, false, false);
        if (boot.mode() != start) return i;
    }
    return -1;
}

}  // namespace

int main() {
    std::printf("\n-- 1. the fade's per-pixel step --\n");
    {
        Check(sk::BootSequence::FadeRgb444(0xfff, 0) == 0xfff, "amount 0 leaves the pixel");
        Check(sk::BootSequence::FadeRgb444(0xf84, 5) == 0xa30, "each nibble loses 5, floored at 0");
        Check(sk::BootSequence::FadeRgb444(0x1fff, 15) == 0x1000, "15 is black; the top nibble stays");
    }

    std::printf("\n-- 2. the unattended sequence --\n");
    {
        sk::BootSequence boot;
        Check(!boot.active(), "a default sequence is finished");
        boot.Start();
        Check(boot.active() && boot.mode() == sk::BootMode::kLogoVir2L,
              "Start(): mode 0x26 (FUN_100298a4)");
        boot.Step(kDelta, false, false, false);
        Check(boot.frameFade() == 15, "the first frame is under FUN_10068d10's full fade-in");

        // 0x26 runs its 0x300, then a 0x100 fade-out: ~4 s.
        int ticks = RunUntilModeChanges(boot);
        Check(boot.mode() == sk::BootMode::kLogoBethesda, "0x26 -> 0x25");
        Check(ticks >= 95 && ticks <= 105, "Vir2L lasts 0x400 units (~100 ticks), got " +
                                                std::to_string(ticks));
        ticks = RunUntilModeChanges(boot);
        Check(boot.mode() == sk::BootMode::kLogoTko, "0x25 -> 0x27");
        Check(ticks >= 70 && ticks <= 80,
              "a later logo is 0x300 in all, fade-out included (~75 ticks), got " +
                  std::to_string(ticks));
        RunUntilModeChanges(boot);
        Check(boot.mode() == sk::BootMode::kCopyright, "0x27 -> 6");
        RunUntilModeChanges(boot);
        Check(boot.mode() == sk::BootMode::kIntroFirst, "6 -> 0xc on its timeout");
        for (int m = 0xd; m <= 0x10; ++m) {
            ticks = RunUntilModeChanges(boot);
            Check(static_cast<int>(boot.mode()) == m,
                  "slide -> " + std::to_string(m) + " (" + std::to_string(ticks) + " ticks)");
        }
        RunUntilModeChanges(boot);
        Check(!boot.active() && boot.mode() == sk::BootMode::kMenu,
              "the title times out to mode 1, the menu");
        boot.Step(kDelta, false, false, false);
        Check(boot.menuFadingIn() && boot.frameFade() > 0, "and the menu fades in");
    }

    std::printf("\n-- 3. skipping --\n");
    {
        sk::BootSequence boot;
        boot.Start();
        boot.Step(kDelta, true, false, false);
        Check(boot.fadingOut() && boot.mode() == sk::BootMode::kLogoVir2L,
              "a key on a logo fades it out (port)");
        RunUntilModeChanges(boot);
        Check(boot.mode() == sk::BootMode::kLogoBethesda, "to the next logo");

        boot.Start();
        boot.Step(kDelta, false, false, true);
        RunUntilModeChanges(boot);
        Check(!boot.active(), "Esc skips the whole sequence to the menu (port)");

        boot.Start();
        while (boot.mode() != sk::BootMode::kIntroFirst) boot.Step(kDelta, false, false, false);
        for (int i = 0; i < 40; ++i) boot.Step(kDelta, false, false, false);
        boot.Step(kDelta, true, false, false);
        RunUntilModeChanges(boot);
        Check(!boot.active(), "any key in the intro goes to the menu (original)");

        boot.Start();
        boot.Finish();
        Check(!boot.active() && !boot.menuFadingIn(), "Finish(): straight to the menu, no fade");
    }

    std::printf("\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
