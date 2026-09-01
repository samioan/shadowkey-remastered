#pragma once

// Owns every menu script object created via CreateMenu(), and implements
// the "Menu-stack manager" class the corpus cross-check hypothesized at
// binding offset 0x14d8c (docs/SIMKIN_NATIVE_API.md). Resolves the path
// strings scripts pass to CreateMenu/OpenMenu (e.g. "Menus\\NewGameMenu",
// "SaveGameMenu") to real .s files under a configurable script root --
// see the port scaffold plan's "assets stay out of the repo" decision.
//
// Also holds the handful of pieces of *global* game state multiple menus
// need to agree on -- the shared Player object (M3), a simulated 4-slot
// save system, the credits-screen text, and a quit request -- none of
// which belong to any one menu instance.

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "skExecutableContext.h"

class skInterpreter;

namespace sk_bindings {

class MenuExecutable;
class PlayerExecutable;

// Turns a Simkin script path (backslash-separated, no extension) into a
// real filesystem path under scriptRoot (forward-slash separated, ".s"
// appended). Exposed standalone for testing -- see
// port/src/tests/m3_mainmenu_smoke.cpp.
std::string ResolveScriptPath(const std::string& scriptRoot, const std::string& simkinPath);

class MenuStack {
public:
    static constexpr int kSaveSlotCount = 4;

    MenuStack(std::string scriptRoot, skInterpreter& interpreter);
    ~MenuStack();

    // Loads (or returns the already-loaded) menu for `simkinPath`, running
    // its Init() the first time it's created. Returns nullptr (logged) if
    // the target .s file doesn't exist or fails to parse.
    MenuExecutable* GetOrCreateMenu(const std::string& simkinPath);

    // Switches the "current" menu, creating it first if necessary, and
    // runs its OnDisplay(). Logs and no-ops if that fails.
    void OpenMenu(const std::string& simkinPath);

    // Constructs, registers under `key` (so a later OpenMenu(key) -- e.g.
    // every "back to main menu" handler's OpenMenu("MainMenu") -- reuses
    // this same instance instead of silently constructing a duplicate),
    // and makes current the root menu (mainmenu.s). The host calls this
    // once at startup; it's the only menu not reached via CreateMenu.
    MenuExecutable* CreateRootMenu(const std::string& key, const std::string& filePath);

    MenuExecutable* currentMenu() const { return m_Current; }
    PlayerExecutable& player() const { return *m_Player; }
    const std::string& scriptRoot() const { return m_ScriptRoot; }
    skInterpreter& interpreter() const { return m_Interpreter; }

    // Simulated save system -- there's no real save-file format RE'd here
    // (out of scope for the menu milestone), just enough in-memory state
    // for the save/load/delete menu chain to behave consistently: slots
    // start empty, ActuallySaveGame() fills one, DeleteGame()/
    // DeleteAllGames() empty them again.
    // slot == -1 means "is ANY slot available" (mainmenu.s/newgamemenu.s's
    // usage); slot in [0, kSaveSlotCount) checks that one specifically.
    bool GameAvailableForLoad(int slot) const;
    void ActuallySaveGame(int slot);
    void DeleteGame(int slot);
    void DeleteAllGames();
    std::string GetSavedTimeStr(int slot) const;
    // Always "not the current slot" -- no multiplayer session exists at
    // this milestone, so nothing should ever be excluded from delete/load
    // lists on that basis.
    int multiplayerSlot() const { return -1; }

    void RequestQuit() { m_QuitRequested = true; }
    bool quitRequested() const { return m_QuitRequested; }

    // Native-only "screen" (ShowCredits() has no script-side handler
    // anywhere in the corpus -- see credits.txt right next to the .s
    // files) -- main.cpp checks creditsActive() before rendering the
    // current menu at all.
    void ShowCredits();
    void CloseCredits() { m_CreditsActive = false; }
    bool creditsActive() const { return m_CreditsActive; }
    const std::vector<std::string>& creditsLines() const { return m_CreditsLines; }

private:
    struct SaveSlot {
        bool used = false;
        std::string timeStr;
    };

    std::string m_ScriptRoot;
    skInterpreter& m_Interpreter;
    std::map<std::string, std::unique_ptr<MenuExecutable>> m_Menus;
    std::unique_ptr<PlayerExecutable> m_Player;
    MenuExecutable* m_Current = nullptr;
    std::array<SaveSlot, kSaveSlotCount> m_SaveSlots;
    bool m_QuitRequested = false;
    bool m_CreditsActive = false;
    std::vector<std::string> m_CreditsLines;
};

}  // namespace sk_bindings
