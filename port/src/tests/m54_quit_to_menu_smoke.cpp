// M54 (screen mode 0x1f = quit to the main menu) smoke test.
//
// Covers, in five parts:
//   1. the recovered screen-mode table (engine/screen_mode.h) -- which
//      modes draw the progress bar, which of those also draw the "Travel
//      to:" banner, and both background threads' real progress lists;
//   2. `QuitToMenu()` driven for real through five shipped scripts, one
//      per distinct way the game reaches it;
//   3. that it is a *request*, not an action -- the two scripts that call
//      something else in the same handler depend on that;
//   4. the `menu` pseudo-level: real manifests, no zone files, and the
//      one music slot the quit thread starts out of it;
//   5. the localized strings the mode-0x1f screen can draw over itself.
//
// Real data, read directly off the install image:
//   menu_sprites.txt / menu_models.txt / menu_sounds.txt exist;
//     menu.zon / menu.ent / menu.zmp do not
//   menu_sounds.txt slot 70: battle3.ogg (its only music entry)
//   stringtable.eng 3811 "Connection lost", 4081 "Game terminated by the
//     host ", 3591 "Exit", 3950 "Travel to: "
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "engine/screen_mode.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "skInterpreter.h"
#include "skRValue.h"
#include "skRValueArray.h"

namespace {

bool g_Ok = true;
int g_Checks = 0;

void Check(bool cond, const char* what) {
    ++g_Checks;
    std::printf("%-78s %s\n", what, cond ? "OK" : "FAILED");
    if (!cond) g_Ok = false;
}

bool FileExists(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return f.good();
}

}  // namespace

int main(int argc, char** argv) {
    const std::string scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    if (!strings.Load(scriptRoot + "/stringtable.eng")) {
        std::printf("m54_quit_to_menu_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }

    // --- Part 1: the recovered screen-mode table. ---
    std::printf("\n-- Part 1: the four modes that draw the progress bar --\n");
    Check(static_cast<int>(sk::ScreenMode::kQuitToMenu) == 0x1f,
          "ScreenMode::kQuitToMenu == 0x1f");
    // The gate FUN_1002c010 sits behind, transcribed exactly.
    Check(sk::ScreenModeDrawsProgressBar(3) && sk::ScreenModeDrawsProgressBar(4) &&
              sk::ScreenModeDrawsProgressBar(10) && sk::ScreenModeDrawsProgressBar(0x1f),
          "progress bar draws for modes 3, 4, 10 and 0x1f");
    Check(!sk::ScreenModeDrawsProgressBar(1) && !sk::ScreenModeDrawsProgressBar(5) &&
              !sk::ScreenModeDrawsProgressBar(0x20) && !sk::ScreenModeDrawsProgressBar(0x1e),
          "  ...and for no other mode (1 gameplay, 5 menu, 0x1e, 0x20 vignette)");
    // The banner is nested one level deeper than the bar.
    Check(sk::ScreenModeDrawsTravelBanner(3) && sk::ScreenModeDrawsTravelBanner(10),
          "\"Travel to:\" banner draws for modes 3 and 10 (the two that travel)");
    Check(!sk::ScreenModeDrawsTravelBanner(4) && !sk::ScreenModeDrawsTravelBanner(0x1f),
          "  ...but not for 4 (saving) or 0x1f (quitting) -- no destination to name");
    // The vignette's own sub-dispatch is `mode < 0x25 && 0x1f < mode`, so
    // 0x1f sits one below the run and is deliberately outside it.
    Check(static_cast<int>(sk::ScreenMode::kVignetteFirst) ==
              static_cast<int>(sk::ScreenMode::kQuitToMenu) + 1,
          "the vignette run (0x20..0x24) begins one above 0x1f, and excludes it");

    Check(sk::kQuitProgressStageCount == 7, "the quit thread writes 7 progress values");
    const int expectedQuit[] = {4, 15, 22, 30, 40, 80, 100};
    bool quitStagesOk = true;
    for (int i = 0; i < sk::kQuitProgressStageCount; ++i) {
        if (sk::kQuitProgressStages[i] != expectedQuit[i]) quitStagesOk = false;
    }
    Check(quitStagesOk, "  ...and they are 4, 15, 22, 30, 40, 80, 100");
    Check(sk::kLoadProgressStages[0] == 0 &&
              sk::kLoadProgressStages[sk::kLoadProgressStageCount - 1] == 100 &&
              sk::kLoadProgressStageCount == 23,
          "the load thread's own list is a different, 23-value sequence starting at 0");
    // The two slots FUN_10023910 refuses to free are the bar's own.
    Check(sk::kProgressBarFillSlot == 205 && sk::kProgressBarFrameSlot == 206,
          "the bar is drawn from slots 205/206 -- the two a level unload skips");
    Check(sk::kProgressBarSplashSlot == 174, "the splash behind it is slot 174");

    // --- Part 2: QuitToMenu() driven through real shipped scripts. ---
    std::printf("\n-- Part 2: real scripts reaching QuitToMenu() --\n");
    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);

    // One entry per distinct route the shipped game takes into it. The
    // handler names are the real ones; `menu` is the real .s file.
    struct Route {
        const char* menu;
        const char* handler;
        const char* what;
    };
    const Route kRoutes[] = {
        {"deathmenu", "DeathMenuBack", "deathmenu.s -- the player died"},
        {"gameended", "CancelEndGame", "gameended.s -- the game is over"},
        {"mpdeathmenu", "MPDeathMenuBack", "mpdeathmenu.s -- died in multiplayer"},
        {"saveconfirm", "MenuDoneSave", "saveconfirm.s -- save, then quit"},
        {"mainmenu", "CancelEndGame", "mainmenu.s -- End Game confirmed"},
    };

    for (const Route& route : kRoutes) {
        sk_bindings::MenuExecutable* menu = stack.GetOrCreateMenu(route.menu);
        if (!menu) {
            std::printf("%-78s %s\n", route.what, "FAILED (script did not load)");
            ++g_Checks;
            g_Ok = false;
            continue;
        }
        stack.ClearQuitToMenuRequest();
        // saveconfirm.s's MenuDoneSave only quits when the "quit after
        // save" flag its own SetQuitAfterSave(true) arms is still set --
        // that is the whole point of the chain, so arm it the way
        // mainmenu.s's GoSave does.
        if (std::string(route.menu) == "saveconfirm") {
            skRValueArray args;
            args.append(skRValue(true));
            skRValue ret;
            skExecutableContext ctxt(&interpreter);
            menu->method(skString("SetQuitAfterSave"), args, ret, ctxt);
        }
        menu->TryInvoke(route.handler);
        std::string label = std::string(route.what) + " -> quitToMenuRequested()";
        Check(stack.quitToMenuRequested(), label.c_str());
    }

    // saveconfirm.s's own `SetQuitAfterSave(false)` runs before its
    // QuitToMenu(), so the flag must not survive the chain that used it.
    Check(!stack.quitAfterSave(),
          "  ...and saveconfirm.s's MenuDoneSave disarms QuitAfterSave on the way out");
    // The other half of that chain: with the flag clear, the same handler
    // takes the plain Quit() branch instead and never asks to end the
    // session at all.
    stack.ClearQuitToMenuRequest();
    if (sk_bindings::MenuExecutable* saveConfirm = stack.GetOrCreateMenu("saveconfirm")) {
        saveConfirm->TryInvoke("MenuDoneSave");
        Check(!stack.quitToMenuRequested(),
              "saveconfirm.s MenuDoneSave with QuitAfterSave() false: plain Quit(), no session end");
    }

    // --- Part 3: it is a request, not an action. ---
    std::printf("\n-- Part 3: the native must not block --\n");
    // mainmenu.s's CancelEndGame is `QuitToMenu(); OnDisplay();` and
    // gameended.s's is `QuitToMenu(); Init();`. Both only make sense
    // because the real native hands the teardown to a background thread
    // and returns immediately -- so the menu that called it is still
    // alive and still current afterwards.
    stack.ClearQuitToMenuRequest();
    sk_bindings::MenuExecutable* mainMenu = stack.GetOrCreateMenu("mainmenu");
    Check(mainMenu != nullptr, "mainmenu.s loads");
    if (mainMenu) {
        stack.OpenMenu("mainmenu");
        mainMenu->TryInvoke("CancelEndGame");
        Check(stack.quitToMenuRequested(),
              "mainmenu.s CancelEndGame: `QuitToMenu(); OnDisplay();` sets the request");
        Check(stack.currentMenu() == mainMenu,
              "  ...and the calling screen is still alive and current afterwards");
    }
    // The two "quit" natives are genuinely different things and neither
    // stands in for the other.
    Check(!stack.quitRequested(),
          "QuitToMenu() is not QuitGame() -- quitRequested() stays false");
    stack.ClearQuitToMenuRequest();
    Check(!stack.quitToMenuRequested(), "ClearQuitToMenuRequest() clears the latch");

    // --- Part 4: the `menu` pseudo-level. ---
    std::printf("\n-- Part 4: the `menu` pseudo-level the quit thread loads --\n");
    Check(std::string(sk::kFrontEndLevelName) == "menu",
          "FUN_10024c8c is called with the literal level name \"menu\"");
    Check(FileExists(scriptRoot + "/menu_sprites.txt") &&
              FileExists(scriptRoot + "/menu_models.txt") &&
              FileExists(scriptRoot + "/menu_sounds.txt"),
          "menu_sprites.txt / menu_models.txt / menu_sounds.txt all ship");
    Check(!FileExists(scriptRoot + "/menu.zon") && !FileExists(scriptRoot + "/menu.ent") &&
              !FileExists(scriptRoot + "/menu.zmp"),
          "  ...but menu.zon / menu.ent / menu.zmp do not -- it is a manifest set, not a place");

    // The one music slot the quit thread starts once that manifest is in.
    std::ifstream soundManifest(scriptRoot + "/menu_sounds.txt");
    std::string frontEndTrack;
    int musicEntries = 0;
    if (soundManifest) {
        int slot = 0;
        std::string file;
        while (soundManifest >> slot >> file) {
            if (file == "NULL.wav") continue;
            ++musicEntries;
            if (slot == sk::kFrontEndMusicSlot) frontEndTrack = file;
        }
    }
    std::printf("menu_sounds.txt slot %d: \"%s\" (%d non-NULL entries in the manifest)\n",
                sk::kFrontEndMusicSlot, frontEndTrack.c_str(), musicEntries);
    Check(frontEndTrack == "battle3.ogg",
          "FUN_1001b180(engine, 70, 100, 0xff) starts battle3.ogg -- the menu theme");

    // --- Part 5: what the mode-0x1f screen can draw over itself. ---
    std::printf("\n-- Part 5: the strings the quit screen can carry --\n");
    // Only drawn when engine+0x5c0 (a live Bluetooth session) is set --
    // the message is why the session ended, not a general status line.
    Check(strings.Get(3811) == "Connection lost",
          "stringtable 3811 (engine+0x14a3c +0x3b8c): \"Connection lost\"");
    Check(strings.Get(4081) == "Game terminated by the host ",
          "stringtable 4081 (+0x3fc4): \"Game terminated by the host \"");
    // savegamecorrupted.s / savegamenospace.s name QuitToMenu as a menu
    // row's handler outright; 3591 is the label they give it.
    Check(strings.Get(3591) == "Exit",
          "stringtable 3591: \"Exit\" -- the row savegamecorrupted.s wires to QuitToMenu");
    Check(strings.Get(3950) == "Travel to: ",
          "stringtable 3950: \"Travel to: \" -- the banner 0x1f does *not* draw");

    std::printf("\nm54_quit_to_menu_smoke: %s (%d checks)\n",
                g_Ok ? "PASSED (all checks)" : "FAILED", g_Checks);
    return g_Ok ? 0 : 1;
}
