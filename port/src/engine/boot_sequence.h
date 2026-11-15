#pragma once

#include <algorithm>
#include <cstdint>

// M137: what the game shows between launching and the main menu -- the
// publisher logos, the copyright page, the four-slide "War comes to
// Hammerfell" intro and the "Press any key to Start" title. The port went
// straight to MainMenu; all of it was in the binary.
//
// **How the original boots.** `GameEngine_FirstTickBootstrap` calls the
// screen controller's one-time init, `FUN_100298a4`. That calls
// `FUN_10068d10` first, which starts the front-end theme
// (`FUN_1001b180(engine, 0x46, 100, 0xff)`), puts the controller in mode 6
// with a 0xa00 timer and arms a fade *in* (`FUN_1001b7a0(engine, -1)`) --
// and then overrides the mode straight away: `FUN_1006a344(this, 0x26)`,
// timer `+0xe8 = 0x300`. Every arm after that is in `FUN_10068e0c`, the
// controller's per-frame mode switch:
//
//   mode   draws                                           then
//   0x26   global.spr 246 (Vir2L) + string 4072 at y 0x28   fade to 0x25
//   0x25   global.spr 245 (Bethesda)                         fade to 0x27
//   0x27   global.spr 247 (TKO) + 4073 at y 0x28, 4075 at    fade to 6
//          y 0x82
//   6      global.spr 20 (parchment) + strings 58, 2280,     fade to 0xc on
//          60..68, centred, one every 12px from y 0x32       timeout or the
//                                                            bound action 15
//   0xc..  global.spr mode+0xc9 (213..216) at (0xe,5),       fade to mode+1
//   0xf    caption 3844 + (mode-0xc) under it -- the         every 0xa00
//          vignette's own text box
//   0x10   global.spr 174 + 4074 "Press any key to Start"    fade to 1 (menu)
//          centred at y 0xb4
//
// In 0xc..0x10 *any* button (slots 0..0x14) fades straight to mode 1 with
// the "open the menu" flag -- the main menu. That is the original's only
// skip; the logos and the copyright page run their full time.
//
// The three logo arms share one shape: count `+0xe8` down by the frame
// delta, draw while it is positive, and once it is not *and no fade-out is
// already running* (`engine+0x5a8 == 0`) arm the fade to the next mode with
// `+0xe8 = 0x300`. The timer keeps running through the fade, so a logo is
// one second fading in, one second held, one second fading out (the first
// one, which starts at a full 0x300 under the boot fade-in, holds for two).
//
// **The fade** is `FUN_1004fc50`, run after every mode's draw while either
// counter is live. `FUN_1001b788` arms the fade-out (`+0x5a8 = 0x100`,
// target `+0x5ac`, flag `+0x5b0`); each frame subtracts
// `(0x100 - out) * 0xf00 >> 16` -- 0 rising to 15 -- from every RGB444
// nibble of the frame, in place. When it runs out the target mode is set
// (or, with the flag, the menu is opened through the controller's vtable
// +0x38) and `FUN_1001b7a0` arms the fade-in (`+0x5a4 = 0x100`, target -1),
// which subtracts `in * 0xf00 >> 16`, 15 falling to 0. The main menu itself
// fades in that way.
//
// **Not the original's:** the port lets a key skip the logos and the
// copyright page too (each press fades to the next screen), and Esc -- the
// right softkey -- skips everything at once. The user asked for both.

namespace sk {

enum class BootMode : int {
    kMenu = 1,          // done: the main menu is up
    kCopyright = 6,
    kIntroFirst = 0xc,  // four slides, 0xc..0xf
    kIntroLast = 0xf,
    kTitle = 0x10,
    kLogoBethesda = 0x25,
    kLogoVir2L = 0x26,
    kLogoTko = 0x27,
};

// global.spr slots, read off the engine+0x4460 sprite-pointer table.
constexpr int kBootSpriteBethesda = 245;  // engine+0x4834
constexpr int kBootSpriteVir2L = 246;     // engine+0x4838
constexpr int kBootSpriteTko = 247;       // engine+0x483c
constexpr int kBootSpriteParchment = 20;  // engine+0x44b0
constexpr int kBootSpriteTitle = 174;     // engine+0x4718
constexpr int kBootIntroSpriteBase = 0xc9;  // + mode: 213..216

// String-table ids (engine+0x14a3c + id*4).
constexpr int kBootTextPublished = 4072;    // +0x3fa0 "Published and Developed by"
constexpr int kBootTextAssociation = 4073;  // +0x3fa4 "In Association with"
constexpr int kBootTextPressAnyKey = 4074;  // +0x3fa8 "Press any key to Start"
constexpr int kBootTextTkoNotice = 4075;    // +0x3fac the TKO trademark line
constexpr int kBootIntroTextBase = 3844;    // +0x3c10, + (mode - 0xc)

// Mode 6's eleven centred lines, top to bottom (y = 0x32 + 12*i). The
// second is +0x23a0 -- string 2280 "Shadowkey" -- and the rest run 60..68.
// `engine+0x14a4c == 1` swaps the last line for two hard-coded strings; the
// port has no reason to be in that configuration.
constexpr int kBootCopyrightLines[] = {58, 2280, 60, 61, 62, 63, 64, 65, 66, 67, 68};
constexpr int kBootCopyrightFirstY = 0x32;
constexpr int kBootCopyrightPitch = 0xc;

constexpr int kBootLogoUnits = 0x300;
constexpr int kBootCopyrightToIntroUnits = 0x500;
constexpr int kBootIntroSlideUnits = 0xa00;
constexpr int kBootFadeUnits = 0x100;

class BootSequence {
public:
    // The state `FUN_100298a4` leaves behind: mode 0x26, 0x300 on the
    // timer, and FUN_10068d10's fade-in still armed.
    void Start() {
        mode_ = BootMode::kLogoVir2L;
        timer_ = kBootLogoUnits;
        fadeOut_ = 0;
        fadeIn_ = kBootFadeUnits;
        target_ = -1;
        frameFade_ = 0;
    }

    // Drop straight to the menu with no fade (a game start requested from
    // the command line or the debug console, or SK_SKIP_INTRO).
    void Finish() {
        mode_ = BootMode::kMenu;
        fadeOut_ = fadeIn_ = 0;
        frameFade_ = 0;
    }

    bool active() const { return mode_ != BootMode::kMenu; }
    BootMode mode() const { return mode_; }
    int timer() const { return timer_; }
    // The fade still running over the main menu once the sequence is done.
    bool menuFadingIn() const { return mode_ == BootMode::kMenu && fadeIn_ > 0; }
    // The nibble amount (0..15) to subtract from this frame, set by Step().
    int frameFade() const { return frameFade_; }
    bool fadingOut() const { return fadeOut_ > 0; }

    // One controller frame: the mode's own arm, then the fade.
    //   anyButton    -- some button slot 0..0x14 went down this frame
    //   useRight     -- the bound action 15 went down this frame
    //   skipAll      -- the port's own Esc: go to the menu now
    // Returns true while there is a boot screen to draw this frame.
    bool Step(int delta, bool anyButton, bool useRight, bool skipAll) {
        if (mode_ == BootMode::kMenu) {
            StepFade(delta);
            return false;
        }
        if (skipAll && !(fadeOut_ > 0 && target_ == static_cast<int>(BootMode::kMenu))) {
            ArmFadeOut(BootMode::kMenu);
            timer_ = 0;
        }
        const int m = static_cast<int>(mode_);
        switch (mode_) {
        case BootMode::kLogoVir2L:
        case BootMode::kLogoBethesda:
        case BootMode::kLogoTko: {
            timer_ -= delta;
            const BootMode next = mode_ == BootMode::kLogoVir2L     ? BootMode::kLogoBethesda
                                  : mode_ == BootMode::kLogoBethesda ? BootMode::kLogoTko
                                                                     : BootMode::kCopyright;
            // Port: a key cuts the hold short. The original waits.
            if ((timer_ <= 0 || anyButton) && fadeOut_ == 0) {
                ArmFadeOut(next);
                timer_ = kBootLogoUnits;
            }
            break;
        }
        case BootMode::kCopyright:
            timer_ -= delta;
            // The original skips on action 15 only; the port takes any key.
            if ((timer_ < 1 || useRight || anyButton) && fadeOut_ == 0) {
                ArmFadeOut(BootMode::kIntroFirst);
                timer_ = kBootCopyrightToIntroUnits;
            }
            break;
        default:  // 0xc..0x10
            if (timer_ > 0) {
                timer_ -= delta;
                if (timer_ < 1) {
                    int next = m + 1;
                    if (next > static_cast<int>(BootMode::kTitle)) next = 1;
                    ArmFadeOut(static_cast<BootMode>(next));
                    timer_ = kBootIntroSlideUnits;
                }
                if (anyButton) {
                    ArmFadeOut(BootMode::kMenu);
                    timer_ = 0;
                }
            }
            break;
        }
        StepFade(delta);
        // A fade that just ended may have put the menu up; the frame it
        // ended on still shows the old screen at full black.
        return true;
    }

    // What the frame that Step() just ran should draw. Valid while
    // Step() returned true; `drawnMode` can lag `mode()` by the one frame
    // a fade-out lands on.
    BootMode drawnMode() const { return drawn_; }

    // `FUN_1004fc50`'s per-pixel step on an RGB444 value.
    static uint16_t FadeRgb444(uint16_t v, int amount) {
        auto sub = [amount](int n) { return n > amount ? n - amount : 0; };
        const int r = sub((v >> 8) & 0xf), g = sub((v >> 4) & 0xf), b = sub(v & 0xf);
        return static_cast<uint16_t>((v & 0xf000) | (r << 8) | (g << 4) | b);
    }

private:
    void ArmFadeOut(BootMode target) {
        fadeOut_ = kBootFadeUnits;
        target_ = static_cast<int>(target);
    }

    void StepFade(int delta) {
        drawn_ = mode_;
        frameFade_ = 0;
        if (fadeOut_ > 0) {
            frameFade_ = ((kBootFadeUnits - fadeOut_) * 0xf00) >> 16;
            fadeOut_ = (std::max)(0, fadeOut_ - delta);
            if (fadeOut_ == 0) {
                if (target_ != -1) mode_ = static_cast<BootMode>(target_);
                fadeIn_ = kBootFadeUnits;
                target_ = -1;
                // The landing frame: the last one of the old screen. The
                // original leaves it at whatever the in-place subtraction
                // reached; drawing it fully black is what that amounts to.
                frameFade_ = 15;
            }
        } else if (fadeIn_ > 0) {
            frameFade_ = (fadeIn_ * 0xf00) >> 16;
            fadeIn_ = (std::max)(0, fadeIn_ - delta);
        }
    }

    BootMode mode_ = BootMode::kMenu;
    BootMode drawn_ = BootMode::kMenu;
    int timer_ = 0;
    int fadeOut_ = 0;  // engine+0x5a8
    int fadeIn_ = 0;   // engine+0x5a4
    int target_ = -1;  // engine+0x5ac
    int frameFade_ = 0;
};

}  // namespace sk
