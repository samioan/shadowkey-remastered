#pragma once

// M54: the engine's screen modes -- `ScreenModeController + 0x78`, written
// by `SetScreenMode` (`FUN_1006a344`) and read by every per-tick draw the
// controller does. See docs/RENDER_LOOP.md's "Screen mode 0x1f" for the
// full recovery writeup; this header exists so the recovered facts are
// testable without the windowed game loop, the same way
// assets/zone_display_names.h holds M26's table.
//
// Only the modes this port has evidence for are named. `SetScreenMode`'s
// own switch enumerates 1, 6..9, 0xb..0x1e and 0x20..0x24 as the values
// that call `User::ResetInactivityTime()` -- i.e. the ones where the
// player is looking at something interactive and the backlight should
// stay on. The six it deliberately leaves out (2, 3, 4, 5, 10 and 0x1f)
// are the modes where the engine, not the player, is busy. Four of those
// six are exactly the four that draw the progress bar.

namespace sk {

enum class ScreenMode : int {
    // Normal gameplay. Set by GameEngine_InitLevel when a level has
    // finished loading, and by the menu stack on Quit.
    kGameplay = 1,
    // Travelling to another zone (`FUN_1006c31c`, controller vtable slot
    // +0x2c) -- the `nGEN_Loading` thread is running.
    kZoneTravel = 3,
    // Writing a save (`FUN_10018e70` feeds the bar its own literal
    // 3/10/30/50/70 percentages).
    kSaving = 4,
    // A menu/UI screen.
    kMenu = 5,
    // Loading a saved game -- LoadGame's own path.
    kLoadingSavedGame = 10,
    // M54: quitting the current session and returning to the main menu.
    // Never appears as an argument to SetScreenMode or to the fade-arming
    // function, because `FUN_10026f40` writes it straight into
    // `controller+0x78` as it spawns the `nGEN_Quitting` thread.
    kQuitToMenu = 0x1f,
    // The story slideshow (M44) occupies one mode per slide.
    kVignetteFirst = 0x20,
    kVignetteLast = 0x24,
};

// The gate on the progress-bar draw (`FUN_1002c010`, controller vtable
// slot +0x44), transcribed: `mode == 10 || mode == 3 || mode == 0x1f ||
// mode == 4`. All four are long jobs the player waits through, and all
// four fill the same bar from the same counter (`appview+0x408`).
inline bool ScreenModeDrawsProgressBar(int mode) {
    return mode == static_cast<int>(ScreenMode::kZoneTravel) ||
           mode == static_cast<int>(ScreenMode::kSaving) ||
           mode == static_cast<int>(ScreenMode::kLoadingSavedGame) ||
           mode == static_cast<int>(ScreenMode::kQuitToMenu);
}

// The banner is *not* drawn for all four. The real code nests
// `if (mode == 10 || mode == 3)` inside the block above -- the two that
// travel to a named zone, and only those. A save and a quit have no
// destination to name, so they show the bar over the bare splash.
inline bool ScreenModeDrawsTravelBanner(int mode) {
    return mode == static_cast<int>(ScreenMode::kZoneTravel) ||
           mode == static_cast<int>(ScreenMode::kLoadingSavedGame);
}

// The real progress values each background thread writes into the shared
// counter as its own stages complete.
//
// Loading (`GameEngine_InitLevel`, the `nGEN_Loading` thread) -- M26.
inline constexpr int kLoadProgressStages[] = {0,  3,  5,  10, 12, 14, 22, 30,
                                                40, 45, 50, 55, 58, 60, 65, 70,
                                                75, 85, 87, 90, 95, 98, 100};
inline constexpr int kLoadProgressStageCount =
    static_cast<int>(sizeof(kLoadProgressStages) / sizeof(kLoadProgressStages[0]));

// Quitting to the menu (`FUN_1002707c`, the `nGEN_Quitting` thread) --
// M54. Seven writes, one per real teardown stage:
//    4  stop the level's audio
//   15  engine-side teardown (`FUN_1000e3c4`/`FUN_1000e5c8`)
//   22  free every loaded asset (`FUN_10023910`), then queue the `menu`
//       sound set
//   30  clear the multiplayer flags, tell the world object to stop
//   40  load the `menu` sprite manifest (`FUN_10024c8c(this, "menu")`)
//   80  start the front-end music (`FUN_1001b180(engine, 70, 100, 0xff)`)
//  100  open MainMenu (`FUN_100779b8(..., "MainMenu", 0, 0)`)
inline constexpr int kQuitProgressStages[] = {4, 15, 22, 30, 40, 80, 100};
inline constexpr int kQuitProgressStageCount =
    static_cast<int>(sizeof(kQuitProgressStages) / sizeof(kQuitProgressStages[0]));

// The pseudo-level the quit thread loads once the real one is gone
// (`FUN_10024c8c(this, "menu")`). It ships menu_sprites.txt,
// menu_models.txt and menu_sounds.txt but no .zon/.ent, so it is a
// manifest set rather than a place.
inline constexpr const char* kFrontEndLevelName = "menu";

// The sound slot the quit thread starts once that manifest is in
// (`FUN_1001b180(engine, 0x46, 100, 0xff)`). menu_sounds.txt maps it to
// battle3.ogg, and it is the only non-NULL music entry in the whole
// front-end manifest -- so this is the main menu's own theme.
inline constexpr int kFrontEndMusicSlot = 70;

// The two `global.spr` slots `FUN_10023910` skips when it frees all 384
// on a level unload (`engine+0x4794` / `+0x4798`) -- which is exactly why
// the progress bar can still be drawn on a screen where nothing else is
// loaded any more.
inline constexpr int kProgressBarFillSlot = 205;
inline constexpr int kProgressBarFrameSlot = 206;
// The full-screen splash behind it (`engine+0x4718`).
inline constexpr int kProgressBarSplashSlot = 174;

// M90: the screen `Level.LoadLevel(name, x, y)` raises instead of loading
// -- the "Travel to: <zone> / Go / Don't Go" prompt. Spelled exactly as
// the engine spells it, in the one place it appears: a plain ASCII string
// constant at `0x100b2558`, passed to `FUN_100779b8` (open-menu-by-name)
// by the Zone/Level dispatcher's case 0x17. The file on disk is
// `levelconfirm.s` -- Symbian's FAT filenames are case-insensitive, so the
// engine's own `"%s\\%s.s"` open finds it -- but the *name* matters
// independently of the file, because the menu class builds its back-key
// handler out of it (`"%sBack"` at `0x100b32c8`, hence levelconfirm.s's
// own `LevelConfirmBack`). See MenuExecutable::GoBack().
inline constexpr const char* kLevelConfirmMenuName = "LevelConfirm";

}  // namespace sk
