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
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "assets/product_database.h"
#include "simkin_bindings/script_delay.h"
#include "skExecutableContext.h"

class skInterpreter;
class skiExecutable;

namespace sk {
class StringTable;
class SoundArchive;
class AudioEngine;
class InputState;
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

    // M53: the engine's script-timer clock (`engine+0x470 -> +0x460`),
    // 8.8 fixed-point seconds accumulated per frame and zeroed on a level
    // load. It hangs off the engine in the real game and off the stack
    // here for the same reason muteOnCall does: every entity script that
    // arms a `Delay` needs the one shared clock, and the stack is what
    // every entity executable already holds a reference to. See
    // script_delay.h.
    GameClock& gameClock() { return m_GameClock; }
    const GameClock& gameClock() const { return m_GameClock; }

    // M30: a script's own Quit() -- "close this screen". 30+ real scripts
    // call it, most visibly every NPC conversation's own "Goodbye" row
    // (e.g. almatheaconvo.s's `MenuQuit[ (s) { Quit(); } ]`). It was
    // soft-failing, so choosing Goodbye did nothing at all. The host
    // decides what "close" means -- resume gameplay if the screen was
    // opened from the 3D view, otherwise fall back to the screen's own
    // SetPrevMenu target -- so this only records the request.
    bool closeMenuRequested() const { return m_CloseMenuRequested; }
    void RequestCloseMenu() { m_CloseMenuRequested = true; }
    void ClearCloseMenuRequest() { m_CloseMenuRequested = false; }

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
    MenuExecutable* GetOrCreateMenu(const std::string& simkinPath, skiExecutable* opener = nullptr,
                                    int screenMode = 0);

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

    // M80: the live control bindings, for `ParseActionText` -- the one
    // native that has to *name* a key rather than read one
    // (simkin_bindings/action_text.h). Null in every test that does not
    // set one, which that function reads as "the engine defaults", the
    // same bindings the tutorial text was written against. Set by
    // main.cpp once, right after the InputState it points at is built.
    void SetInput(const sk::InputState* input) { m_Input = input; }
    const sk::InputState* input() const { return m_Input; }

    // M80: does the interpreter have a global of this name? Zone scripts
    // need this to stop shadowing `Level` -- see
    // ZoneScriptExecutable::getValue().
    bool HasGlobalVariable(const skString& name) const;

    // M59: `products.dat`, the merchant product database
    // (assets/product_database.h). It hangs off the stack for the same
    // reason the string table does -- every merchant creature needs the
    // one shared catalogue, and the stack is what every entity executable
    // already holds. Loaded once by the constructor; empty (and every
    // AddProduct a no-op with a logged miss) if the file is absent.
    const sk::ProductDatabase& products() const { return m_Products; }

    // M59/M60: which side of the counter `buysell.s` is showing. In the
    // real engine `BuyFromMerchant()` and `SellToMerchant()` both call
    // `FUN_10034f38(storeScreen, buying)`, which writes `mode = buying ?
    // 1 : 2` onto the screen object before handing it over. There is no
    // screen-mode table here, so the pending mode is parked on the stack
    // and stamped onto whichever menu opens next -- see
    // MenuStack::OpenMenu(). M60 widened it from a bool to the real
    // three-state field, because mode 0 (the plain inventory screen) is a
    // genuinely different page and not just "not buying".
    void SetPendingScreenMode(int mode) { m_PendingScreenMode = mode; }
    int pendingScreenMode() const { return m_PendingScreenMode; }

    // M50: the save system writes real files.
    //
    // This was a simulated four-slot table (a `used` bool and a
    // timestamp) for as long as no on-disk format was decoded. M40
    // decoded the container and M50 the members, so a slot is now an
    // actual `game0<N>.sav` -- a real SaveArchive holding a real
    // `character.dat`, produced by PlayerExecutable::BuildSaveRecord and
    // read back by ApplySaveRecord.
    //
    // The real game keeps those under `c:\system\apps\6R51\`; here the
    // directory is whatever SetSaveDirectory() was given, defaulting to
    // the working directory. With no directory writable the slots fall
    // back to the old in-memory behaviour rather than failing a save, so
    // a smoke test or a read-only checkout still exercises the menus.
    void SetSaveDirectory(std::string dir) { m_SaveDir = std::move(dir); }
    const std::string& saveDirectory() const { return m_SaveDir; }
    // The level a save should record and a load should return to. Set by
    // main.cpp's zone-load block; it is SavedCharacter::levelName, the
    // field FUN_1001ea54 copies straight back into the engine.
    void SetCurrentLevelName(std::string name) { m_CurrentLevel = std::move(name); }
    const std::string& currentLevelName() const { return m_CurrentLevel; }
    // Reads slot `slot`'s character.dat back into the player and returns
    // the level name it was saved in ("" if the slot will not load).
    std::string LoadGameFromSlot(int slot);

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

    // M54: `QuitToMenu()` -- GameEngine root binding index 0x33, the
    // *other* thing a real "Quit" row can mean. RequestQuit() above is
    // `QuitGame()`, which ends the process; this one ends the current
    // *session* and returns to mainmenu.s, and it is what deathmenu.s,
    // gameended.s, mainmenu.s's End Game confirmation, mpdeathmenu.s and
    // saveconfirm.s's save-then-quit chain all actually call.
    //
    // In the real engine it is screen mode **0x1f** (docs/RENDER_LOOP.md's
    // "Screen mode 0x1f"): the controller's own vtable slot +0x3c spawns
    // the `nGEN_Quitting` background thread, which tears the level down,
    // loads the `menu` pseudo-level's manifests and opens MainMenu behind
    // the same progress bar a zone load uses -- so the host renders a
    // progress screen for it too, and only then does the teardown. Same
    // request/latch shape as RequestGameStart().
    void RequestQuitToMenu() { m_QuitToMenuRequested = true; }
    bool quitToMenuRequested() const { return m_QuitToMenuRequested; }
    void ClearQuitToMenuRequest() { m_QuitToMenuRequested = false; }

    // M54: `QuitAfterSave()` / `SetQuitAfterSave(b)` -- GameEngine root
    // bindings 0x37 and 0x38, and the one piece of state that carries a
    // "quit" intent across a menu boundary. mainmenu.s's and gameended.s's
    // End Game rows arm it and then send the player to SaveGameMenu;
    // saveconfirm.s's MenuDoneSave reads it back once the write has
    // finished and calls QuitToMenu() instead of a plain Quit(). Both
    // scripts soft-failed here before, so choosing "save first" left the
    // player sitting on the save screen with the session still running.
    // Global rather than per-menu for the same reason muteOnCall is: the
    // two screens that set and read it are different objects, and the one
    // that reads it is rebuilt on every visit.
    bool quitAfterSave() const { return m_QuitAfterSave; }
    void SetQuitAfterSave(bool quit) { m_QuitAfterSave = quit; }

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

    // ---- M61: the scripted spawn override (`engine+0x14a20`) ----------
    //
    // `GetPlayer().SetCameraStart(x, y, z, pitch, yaw, roll)` -- Player
    // binding 0x39, 49 call sites, and the state behind them. This mirrors
    // the engine's own seven fields byte for byte, **in the engine's own
    // memory order**, because that order is the whole explanation for the
    // native's odd-looking argument mapping:
    //
    //     engine+0x14a20  armed      <- set to 1 by SetCameraStart
    //     engine+0x14a24  x          <- argument 1
    //     engine+0x14a28  y          <- argument 2
    //     engine+0x14a2c  z          <- argument 3
    //     engine+0x14a30  roll       <- argument 6   (!)
    //     engine+0x14a34  pitch      <- argument 4
    //     engine+0x14a38  yaw        <- argument 5
    //
    // The last three look shuffled only if you expect the struct to follow
    // the signature. It doesn't: it is a verbatim copy of the first six
    // 32-bit fields of a `.ent` **placement record** (docs/ZONE_FORMAT.md)
    // -- `x, y, z, rotOrScale[0..1], rotOrScale[2..3], unkA` -- whose three
    // orientation channels land on the object at `+0xb2` (roll), `+0xa8`
    // (pitch) and `+0xb6` (yaw), in that file order. The *script* signature
    // is the human one, `(x, y, z, pitch, yaw, roll)`. Nothing is out of
    // order; the storage mirrors the file and the signature mirrors the
    // reader, and `GameEngine_InitLevel` is where the two meet.
    //
    // Lifecycle, all three ends decompiled:
    //   arm      `SetCameraStart` (Player 0x39), called by a script right
    //            after a `Level.LoadLevel(...)` that has not happened yet.
    //   consume  `GameEngine_InitLevel`'s `.ent` loop, at the `typeId == 1`
    //            player-start record: when armed it writes these six values
    //            onto the player instead of the record's own, and does so
    //            **unconditionally** -- the record path is gated on
    //            InitLevel's `param_3`, the override is not.
    //   disarm   the same function, right after the entity pass; and
    //            `Level.RestoreSaveLevel()` (Level 0x15), which is what
    //            `levelconfirm.s` calls when the player declines the trip.
    struct CameraStart {
        bool armed = false;
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;
        int32_t roll = 0;
        int32_t pitch = 0;
        int32_t yaw = 0;
    };
    // Argument order, not field order -- see CameraStart's comment.
    void ArmCameraStart(int32_t x, int32_t y, int32_t z, int32_t pitch, int32_t yaw,
                        int32_t roll) {
        m_CameraStart.armed = true;
        m_CameraStart.x = x;
        m_CameraStart.y = y;
        m_CameraStart.z = z;
        m_CameraStart.pitch = pitch;
        m_CameraStart.yaw = yaw;
        m_CameraStart.roll = roll;
    }
    const CameraStart& cameraStart() const { return m_CameraStart; }
    // Only the flag is cleared, exactly as `GameEngine_InitLevel` and
    // `RestoreSaveLevel` do -- the six values stay behind. Nothing reads
    // them while disarmed, so this is invisible, but keeping the real
    // shape means a future save-record pass (`FUN_1003cc5c` ships all
    // seven fields in a 0x20-byte multiplayer packet, flag included) has
    // the same state to serialise the engine has.
    void ClearCameraStart() { m_CameraStart.armed = false; }

    // ---- M61: the pending level (`app+0x28`'s own name slots) ---------
    //
    // `Level.LoadLevel(name, x, y)` does not load anything. It pushes the
    // current level name to the "previous" slot (`+0x50`), writes the
    // destination into the current slot (`+0x28`) and the two optional
    // coordinates to `+0x48`/`+0x4c`, and then opens `levelconfirm.s` --
    // the "Travel to: <name> / Go / Don't Go" prompt. `Go` calls
    // `Level.ActuallyLoadLevel(Level.GetNextLevel(), GetNextLevelX(),
    // GetNextLevelY())`; `Don't Go` calls `Level.RestoreSaveLevel()`,
    // which copies the previous name back and disarms the camera start.
    //
    // That is why arming the spawn override before the load is safe, and
    // why disarming it is a named script binding at all.
    //
    // M90: the prompt is real now -- `LoadLevel` raises `LevelConfirm` and
    // it is `Go` / `Don't Go` that decide whether the load happens. See
    // LevelExecutable's three handlers.
    void SetPendingLevel(std::string name, int nextX, int nextY) {
        m_PreviousLevel = m_CurrentLevel;
        m_CurrentLevel = std::move(name);
        m_PendingLevelX = nextX;
        m_PendingLevelY = nextY;
    }
    // M90: `ActuallyLoadLevel` (0x18), the one transition binding that
    // does *not* touch either name slot -- `LoadLevel` has already
    // written both, and pushing the destination over the saved name would
    // throw away the only record of where the player came from.
    void SetPendingLevelCoords(int nextX, int nextY) {
        m_PendingLevelX = nextX;
        m_PendingLevelY = nextY;
    }
    int pendingLevelX() const { return m_PendingLevelX; }
    int pendingLevelY() const { return m_PendingLevelY; }
    // `Level.RestoreSaveLevel()` -- Level binding 0x15.
    void RestorePendingLevel() {
        ClearCameraStart();
        m_CurrentLevel = m_PreviousLevel;
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

    // "game0%d.sav" -- the real format string, from the binary.
    std::string SlotPath(int slot) const;

    std::string m_ScriptRoot;
    skInterpreter& m_Interpreter;
    const sk::StringTable* m_Strings;
    const sk::InputState* m_Input = nullptr;  // M80, see SetInput()
    sk::SoundArchive* m_Sounds;  // M27: see sounds()/audio()'s own comment above
    sk::AudioEngine* m_Audio;
    bool m_MuteOnCall = false;  // M28: see muteOnCall() above
    GameClock m_GameClock;      // M53: see gameClock() above
    bool m_CloseMenuRequested = false;  // M30: see closeMenuRequested() above
    std::map<std::string, std::unique_ptr<MenuExecutable>> m_Menus;
    std::unique_ptr<PlayerExecutable> m_Player;
    std::unique_ptr<LevelExecutable> m_Level;
    MenuExecutable* m_Current = nullptr;
    std::array<SaveSlot, kSaveSlotCount> m_SaveSlots;
    std::string m_SaveDir = ".";
    std::string m_CurrentLevel;
    // M61: see SetPendingLevel()/ArmCameraStart() above.
    std::string m_PreviousLevel;
    int m_PendingLevelX = -1;  // the real "no coordinate given" default
    int m_PendingLevelY = -1;
    CameraStart m_CameraStart;
    sk::ProductDatabase m_Products;  // M59: see products() above
    // M60: 0 Inventory / 1 Buy / 2 Sell -- see SetPendingScreenMode().
    // Defaults to Inventory so every screen that is not the store gets
    // the mode its own constructor would have given it.
    int m_PendingScreenMode = 0;
    bool m_QuitRequested = false;
    bool m_QuitToMenuRequested = false;  // M54: see quitToMenuRequested() above
    bool m_QuitAfterSave = false;        // M54: see quitAfterSave() above
    bool m_CreditsActive = false;
    std::vector<std::string> m_CreditsLines;
    bool m_GameStartRequested = false;
    std::string m_RequestedZone;
    bool m_StartingInventoryLoaded = false;
};

}  // namespace sk_bindings
