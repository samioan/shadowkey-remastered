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
class skiExecutable;

namespace sk {
class StringTable;
class SoundArchive;
class AudioEngine;
}

namespace sk_bindings {

class LevelExecutable;
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

    // `strings` may be null (some smoke tests construct a MenuStack with
    // no real stringtable.eng loaded) -- M10's starting-inventory items
    // then fall back to ItemExecutable's own no-stringtable placeholder
    // rather than crash. Registers the real/placeholder Simkin global
    // constants (game_constants.h) once, here.
    //
    // M27: `sounds`/`audio` may also be null (every existing test
    // constructs a MenuStack without them) -- PlayerExecutable::PlaySound()
    // and LevelExecutable::PlayAmbient() (via sounds()/audio() below) then
    // silently no-op instead of crashing, same tolerance every other
    // optional subsystem in this port already has. Passed straight into
    // PlayerExecutable's own constructor (it's built here, not given a
    // MenuStack& to pull them from later, unlike LevelExecutable/
    // MonsterExecutable/ItemExecutable which already hold one).
    MenuStack(std::string scriptRoot, skInterpreter& interpreter,
              const sk::StringTable* strings = nullptr, sk::SoundArchive* sounds = nullptr,
              sk::AudioEngine* audio = nullptr);
    ~MenuStack();

    sk::SoundArchive* sounds() const { return m_Sounds; }
    sk::AudioEngine* audio() const { return m_Audio; }

    // M28: options.s's real "mute audio during an incoming call" setting
    // (MuteOnCall()/SetMuteOnCall(), previously soft-failed). Lives on the
    // stack rather than on the Options MenuExecutable because the screen
    // is rebuilt on every visit; nothing in this port consumes it beyond
    // showing the correct Mute On/Mute Off row, since there's no telephony
    // to mute on PC.
    bool muteOnCall() const { return m_MuteOnCall; }
    void SetMuteOnCall(bool mute) { m_MuteOnCall = mute; }

    // Loads (or returns the already-loaded) menu for `simkinPath`, running
    // its Init() the first time it's created. Returns nullptr (logged) if
    // the target .s file doesn't exist or fails to parse.
    //
    // M21: `opener`, if given, is set on a newly-created menu *before*
    // Init() runs -- lootmenu.s's own Init() calls UpdateMenu(), which
    // immediately calls GetOpener() (real corpus data), so the opener has
    // to already be in place by then, not set afterward the way
    // OpenMenu()'s own two-step "create, then configure" shape would
    // otherwise do it. Ignored on an already-cached menu (Init() doesn't
    // rerun then anyway).
    MenuExecutable* GetOrCreateMenu(const std::string& simkinPath, skiExecutable* opener = nullptr);

    // Switches the "current" menu, creating it first if necessary, and
    // runs its OnDisplay(). Logs and no-ops if that fails.
    //
    // M21: `opener` -- the object that called OpenMenu() on itself (e.g. a
    // real loot bag's `OpenMenu("LootMenu")`), stored on the target menu so
    // its own real GetOpener() calls resolve back to it (lootmenu.s's
    // GetOpener().GetFirst()/RemoveObject() etc.). nullptr (the default)
    // for every caller that isn't itself a script object -- matches every
    // pre-M21 call site, which never had (or needed) an opener at all.
    void OpenMenu(const std::string& simkinPath, skiExecutable* opener = nullptr);

    // M17: like OpenMenu(), but discards any cached instance for
    // `simkinPath` first, so it's rebuilt from scratch and its Init()
    // genuinely reruns -- see the .cpp for why this exists (NPC dialogue
    // needs its quest-state branching in Init() to re-evaluate on every
    // conversation, not just the first; M21's loot bags need the same
    // thing, for the same reason -- lootmenu.s's own UpdateMenu() re-walks
    // the bag's live Collection in Init(), which must rerun as the bag's
    // contents actually change across visits).
    void ReopenMenu(const std::string& simkinPath, skiExecutable* opener = nullptr);

    // Constructs, registers under `key` (so a later OpenMenu(key) -- e.g.
    // every "back to main menu" handler's OpenMenu("MainMenu") -- reuses
    // this same instance instead of silently constructing a duplicate),
    // and makes current the root menu (mainmenu.s). The host calls this
    // once at startup; it's the only menu not reached via CreateMenu.
    MenuExecutable* CreateRootMenu(const std::string& key, const std::string& filePath);

    MenuExecutable* currentMenu() const { return m_Current; }
    PlayerExecutable& player() const { return *m_Player; }
    // M18: the bare global `Level` object -- see level_executable.h. The
    // zone-load block (main.cpp) registers each live named door/monster
    // into it via RegisterEntity()/ClearEntities().
    LevelExecutable& level() const { return *m_Level; }
    const std::string& scriptRoot() const { return m_ScriptRoot; }
    skInterpreter& interpreter() const { return m_Interpreter; }
    // M10: real screens call the global GetLocalizedString(id) directly
    // (charactermanager.s's GetHealthText() etc.), not just relying on a
    // row's own textId -- may be null, same caveat as the constructor.
    const sk::StringTable* strings() const { return m_Strings; }

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

    // Set by MenuExecutable's NewGame()/LoadGame() handlers -- main.cpp
    // checks this each tick and, when set, switches from menu mode into
    // the 3D zone renderer (render3d/zone_renderer.h). Only "azra" (the
    // tutorial zone, and the only one this milestone's loader has been
    // verified against) is wired up for now; LoadGame() requests the
    // same zone since there's no real save-file format to read a
    // different one from yet.
    // M10: the first call also runs PlayerExecutable::LoadStartingInventory
    // (guarded by m_StartingInventoryLoaded so a later Load Game -- which
    // also calls this, see menu_executable.cpp's LoadGame() handler --
    // doesn't grant duplicate items on top of whatever the in-memory-only
    // save system already has).
    void RequestGameStart(std::string zoneName);
    bool gameStartRequested() const { return m_GameStartRequested; }
    const std::string& requestedZone() const { return m_RequestedZone; }
    void ClearGameStartRequest() { m_GameStartRequested = false; }

    // M26: mid-game zone transition -- a real script's own LoadLevel(name)
    // (LevelExecutable::method(), e.g. cheatmenu.s's real `Level.
    // LoadLevel("azra")`). Sets the exact same two fields RequestGameStart()
    // does (main.cpp's zone-load block doesn't care which caller set
    // them, and a real per-zone player-start position already makes
    // "arrive at the destination zone's entry point" the correct
    // behavior for this case too) but skips RequestGameStart()'s own
    // one-time starting-inventory grant -- a mid-game transition must not
    // touch the player's already-in-progress inventory. A dedicated name
    // keeps LoadLevel's call site honest about intent rather than reading
    // like a fresh game start.
    void RequestZoneChange(std::string zoneName) {
        m_GameStartRequested = true;
        m_RequestedZone = std::move(zoneName);
    }

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
    const sk::StringTable* m_Strings;
    sk::SoundArchive* m_Sounds;  // M27: see sounds()/audio()'s own comment above
    sk::AudioEngine* m_Audio;
    bool m_MuteOnCall = false;  // M28: see muteOnCall() above
    std::map<std::string, std::unique_ptr<MenuExecutable>> m_Menus;
    std::unique_ptr<PlayerExecutable> m_Player;
    std::unique_ptr<LevelExecutable> m_Level;
    MenuExecutable* m_Current = nullptr;
    std::array<SaveSlot, kSaveSlotCount> m_SaveSlots;
    bool m_QuitRequested = false;
    bool m_CreditsActive = false;
    std::vector<std::string> m_CreditsLines;
    bool m_GameStartRequested = false;
    std::string m_RequestedZone;
    bool m_StartingInventoryLoaded = false;
};

}  // namespace sk_bindings
