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
    // M99: `strings` is no longer const -- SetLanguage() below swaps its
    // contents in place, so every holder of the pointer sees the new
    // language without being told.
    MenuStack(std::string scriptRoot, skInterpreter& interpreter,
              sk::StringTable* strings = nullptr, sk::SoundArchive* sounds = nullptr,
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

    // ---- M99: the Options screen's language and the settings file ----

    // `engine+0x14a4c`, see language.h.
    int language() const { return m_Language; }
    // GameEngine binding 0x72, `SetLanguage(n)`:
    //
    //     old = engine->language;
    //     engine->language = n;                       // FUN_1001b64c
    //     sprintf(path, "z:\\system\\apps\\6R51\\StringTable.%s", suffix);
    //     data = FUN_10027468(path, &size);           // read the whole file
    //     if (!data) engine->language = old;          // keep the old table
    //     else { FUN_1001b5e8(engine);                // free the old table
    //            FUN_10015878(engine, data, size); }  // parse the new one
    //
    // The file is read first and the old table freed only once it has been,
    // so a missing table changes nothing at all. Returns whether the new
    // table loaded. With no string table attached (most tests) only the
    // index moves.
    bool SetLanguage(int language);

    // Where `dragonstar.set` lives. Empty (every test) makes SaveConfig a
    // success that writes nothing -- the port's own case, not the engine's.
    void SetConfigPath(std::string path) { m_ConfigPath = std::move(path); }
    const std::string& configPath() const { return m_ConfigPath; }
    // `FUN_10019c04`, GameEngine binding 0x71: write the action map, the
    // language, both volumes and mute-on-call (assets/game_config.h has the
    // format). Returns false for a file that will not open. With
    // saving disabled (below) it writes nothing and returns **true** -- the
    // writer's first test jumps straight to its success return.
    bool SaveConfig();
    // `engine+0x14a78`. `DeleteAllGames` unlinks `dragonstar.set` along with
    // the saves and then raises this, and every writer -- SaveConfig, the
    // application-exit handler, `Quit`, `QuitGame` -- checks it first, so a
    // "delete everything" is not undone by the next quit writing the file
    // straight back. Nothing lowers it again this session.
    bool configSaveDisabled() const { return m_ConfigSaveDisabled; }

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
    // M95: and the engine's own record of the same thing, which the script
    // *can* see. The menu manager's `+0x48` is raised by every open
    // (`FUN_100779b8` writes 1 after the parse) and dropped by the stack
    // class's `Quit` (case 4 of `FUN_100801fc`: `root+0x48 = 0`, current =
    // null) -- so a `Quit(); OpenMenu(...)` handler ends with it raised, as
    // it should. `IsMenuActive()` reads it. This port does not null
    // `currentMenu()` on a Quit (the host still needs the screen to decide
    // where "close" goes), which is why the flag is separate; the host also
    // drops it on the one resume path that closes a screen without a script
    // Quit, the back-key shortcut.
    bool menuActive() const { return m_MenuActive; }
    void SetMenuActive(bool active) { m_MenuActive = active; }
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

    // M101: clear `object` as the opener of every cached screen. A menu keeps
    // its opener across visits (OpenMenu() only overwrites it with a non-null
    // one), so the host calls this before it destroys a world object a
    // screen may have been opened by -- a loot chest whose own menu ran
    // `GetOpener().DestroyObject()`.
    void ForgetOpener(const skiExecutable* object);

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
    // M99: not const any more -- ConfigKeysDefault and the key-capture
    // screen write the bindings through it.
    void SetInput(sk::InputState* input) { m_Input = input; }
    const sk::InputState* input() const { return m_Input; }
    sk::InputState* mutableInput() const { return m_Input; }

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
    // M95: the one writer -- `VisitStore` zeroes the price on the shared
    // catalogue record, not on a copy (PlayerExecutable::VisitStore).
    sk::ProductDatabase& mutableProducts() { return m_Products; }

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

    // M103: a script has fired `CastAzraWrath()` (Item binding 0, the two
    // vermin bombs). `FUN_1002c848` case 0 rolls Random(2, 12) and hands it
    // to `FUN_1004720c` centred on the item's **owner**, so the roll and
    // the epicentre are settled inside the native and only the sweep is
    // left for the world. Parked here rather than done on the spot for the
    // same reason PickupItem's is: the item is running inside a script call
    // and the level's creature list belongs to main.cpp.
    struct AreaBlast {
        const skiExecutable* origin = nullptr;  // the item's owner; excluded from the sweep
        int damage = 0;
    };
    void RequestAreaBlast(const skiExecutable* origin, int damage) {
        m_PendingBlasts.push_back(AreaBlast{origin, damage});
    }
    std::vector<AreaBlast> TakePendingAreaBlasts() {
        std::vector<AreaBlast> taken;
        taken.swap(m_PendingBlasts);
        return taken;
    }

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
    // M91: returns whether the write actually landed -- the real
    // ActuallySaveGame (menu binding 0x31) branches on FUN_10018e70's
    // status word and calls one of three script methods back: 0 ->
    // DoneSave(), 3 -> NotEnoughSpace(), 1 or 2 -> SaveFailed(). The port
    // has no quota to run out of, so it distinguishes the two it can:
    // wrote it, or couldn't.
    bool ActuallySaveGame(int slot);
    int saveSlot() const { return m_SaveSlot; }
    void SetSaveSlot(int slot) { m_SaveSlot = slot; }
    void DeleteGame(int slot);
    void DeleteAllGames();
    std::string GetSavedTimeStr(int slot) const;
    // Always "not the current slot" -- no multiplayer session exists at
    // this milestone, so nothing should ever be excluded from delete/load
    // lists on that basis.
    int multiplayerSlot() const { return -1; }

    void RequestQuit() { m_QuitRequested = true; }
    bool quitRequested() const { return m_QuitRequested; }

    // M91: `GameActive()` -- menu-class binding 0x5e, and one line of the
    // real dispatcher: `engine + 0x6908 != 0`, the pointer to the loaded
    // level. "A session exists", nothing subtler. It had been answering a
    // flat `false` since M10, which is why the front end was the *only*
    // main menu the port could show: mainmenu.s's OnDisplay is one long
    // `if (GameActive())` and every branch it picks -- Return to Game
    // (1965), Save Game (718), the "End Game" spelling of Quit (3495),
    // suppressing New Game/Credits/Multiplayer -- is the in-game menu.
    // loadgamemenu.s reads it too, to ask "abandon the current game?"
    // before loading over a live session, and so does mainmenu.s's
    // ConfirmQuitGame, which is what routes End Game into "save first?".
    //
    // Set by the host (main.cpp) rather than inferred from m_CurrentLevel,
    // because that name is written by LoadGame() *before* the zone it
    // names is loaded, and the answer must be about the live session.
    void SetGameActive(bool active) { m_GameActive = active; }
    bool gameActive() const { return m_GameActive; }

    // M91: the player's live heading, in the engine's own `player+0xb6`
    // units ([0, 65536) to the turn). x/y/z already live on the player's
    // EntityBaseRef, which main.cpp mirrors the camera onto every
    // tick; this is the fourth channel a save needs and the one nothing
    // else had a home for. See ActuallySaveGame(), which writes all four
    // into the record's Entity layer, and LoadGameFromSlot(), which arms
    // them back through the SetCameraStart path.
    void SetPlayerHeadingUnits(int units) { m_PlayerHeadingUnits = units; }
    int playerHeadingUnits() const { return m_PlayerHeadingUnits; }

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
    // M107: this grants no inventory. It used to (M10's curated club/coif/
    // bread fixture); the original grants nothing on New Game -- see
    // player_executable.h.
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
    sk::StringTable* m_Strings;
    sk::InputState* m_Input = nullptr;  // M80, see SetInput()
    int m_Language = 0;                 // M99, engine+0x14a4c
    std::string m_ConfigPath;           // M99, see SetConfigPath()
    bool m_ConfigSaveDisabled = false;  // M99, engine+0x14a78
    sk::SoundArchive* m_Sounds;  // M27: see sounds()/audio()'s own comment above
    sk::AudioEngine* m_Audio;
    // M28: see muteOnCall() above. M99: **true** until a settings file says
    // otherwise -- the GameEngine constructor stores `engine+0x14a79 = 1`
    // on the line before it runs the config loader.
    bool m_MuteOnCall = true;
    GameClock m_GameClock;      // M53: see gameClock() above
    bool m_CloseMenuRequested = false;  // M30: see closeMenuRequested() above
    bool m_MenuActive = false;          // M95: see menuActive() above
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
    std::vector<AreaBlast> m_PendingBlasts;  // M103: see RequestAreaBlast()
    bool m_QuitRequested = false;
    bool m_QuitToMenuRequested = false;  // M54: see quitToMenuRequested() above
    bool m_GameActive = false;          // M91: see gameActive() above
    int m_PlayerHeadingUnits = 0;       // M91: see playerHeadingUnits() above
    // M91: `GetSaveSlot()` / `SaveGame(slot)` -- engine+0x14a71, one byte
    // shared by the whole save chain. SaveGame() writes it and opens
    // SaveConfirm; saveconfirm.s reads it straight back out with
    // GetSaveSlot() and hands it to ActuallySaveGame().
    int m_SaveSlot = 0;
    bool m_QuitAfterSave = false;        // M54: see quitAfterSave() above
    bool m_CreditsActive = false;
    std::vector<std::string> m_CreditsLines;
    bool m_GameStartRequested = false;
    std::string m_RequestedZone;
};

}  // namespace sk_bindings
