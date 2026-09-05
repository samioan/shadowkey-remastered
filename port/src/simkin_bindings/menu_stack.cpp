#include "simkin_bindings/menu_stack.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

#include "assets/save_archive.h"
#include "assets/save_records.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/player_executable.h"
#include "skInterpreter.h"
#include "skRValue.h"
#include "skRuntimeException.h"
#include "skString.h"

namespace sk_bindings {

std::string ResolveScriptPath(const std::string& scriptRoot, const std::string& simkinPath) {
    std::string normalized;
    for (char c : simkinPath) {
        char out = (c == '\\') ? '/' : c;
        if (out == '/' && !normalized.empty() && normalized.back() == '/') continue;
        normalized += out;
    }
    std::string path = scriptRoot;
    if (!path.empty() && path.back() != '/' && path.back() != '\\') path += '/';
    path += normalized + ".s";
    return path;
}

namespace {

std::string NormalizeKey(const std::string& simkinPath) {
    std::string key;
    for (char c : simkinPath) {
        char out = (c == '\\') ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (out == '/' && !key.empty() && key.back() == '/') continue;
        key += out;
    }
    return key;
}

bool FileExists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

}  // namespace

MenuStack::MenuStack(std::string scriptRoot, skInterpreter& interpreter,
                      const sk::StringTable* strings, sk::SoundArchive* sounds, sk::AudioEngine* audio)
    : m_ScriptRoot(std::move(scriptRoot)),
      m_Interpreter(interpreter),
      m_Strings(strings),
      m_Sounds(sounds),
      m_Audio(audio),
      m_Player(new PlayerExecutable(strings, sounds, audio)),
      m_Level(new LevelExecutable(*this)) {
    // M56: see PlayerExecutable::AttachStack(). Done here rather than in
    // the initialiser list because `*this` is only usable once the members
    // above are built.
    m_Player->AttachStack(*this);
    // M59: the merchant product database. Every merchant creature stocks
    // itself out of this one catalogue -- see store.h.
    m_Products.Load(m_ScriptRoot);
    RegisterGameConstants(interpreter);
    // M18: `Level` is a bare global every real script can reach (docs/
    // SIMKIN_NATIVE_API.md's Zone/Level+Zone effects), never obtained via
    // a factory call -- registered the same way RegisterGameConstants()
    // registers scalar constants, just with an object reference instead.
    interpreter.addGlobalVariable(skString("Level"),
                                   skRValue(static_cast<skiExecutable*>(m_Level.get()), false));
}

MenuStack::~MenuStack() = default;

MenuExecutable* MenuStack::GetOrCreateMenu(const std::string& simkinPath, skiExecutable* opener) {
    std::string key = NormalizeKey(simkinPath);
    auto it = m_Menus.find(key);
    if (it != m_Menus.end()) return it->second.get();

    std::string filePath = ResolveScriptPath(m_ScriptRoot, simkinPath);
    if (!FileExists(filePath)) {
        std::printf("  [MenuStack] CreateMenu(\"%s\") -- file not found: %s\n",
                    simkinPath.c_str(), filePath.c_str());
        return nullptr;
    }

    skExecutableContext loadCtxt(&m_Interpreter);
    std::unique_ptr<MenuExecutable> menu;
    try {
        menu.reset(new MenuExecutable(skString(filePath.c_str()), loadCtxt, *this));
    } catch (...) {
        std::printf("  [MenuStack] CreateMenu(\"%s\") -- failed to parse: %s\n",
                    simkinPath.c_str(), filePath.c_str());
        throw;
    }
    MenuExecutable* raw = menu.get();
    if (opener) raw->SetOpener(opener);
    m_Menus[key] = std::move(menu);
    // M56: a menu script's Init() can raise a *runtime* exception, not
    // just a parse one, and until now that exception escaped all the way
    // into the host and aborted the process. Real example, found the
    // moment GetPlayer().OpenMenu() started working: glcrcrwl/gate1.s
    // opens with `Gate = Level.GetEntity("box1"); if (Gate.saved_Gate =
    // 0)`, and when that entity isn't in the level the interpreter throws
    // "Cannot get field saved_Gate from a non-object" out of
    // extractValue(). Contained here, where the "file not found" path
    // above already decides what a failed open does: log it, discard the
    // half-built screen so a later attempt rebuilds it cleanly, and let
    // OpenMenu() stay on the current menu.
    try {
        raw->RunInit();
    } catch (skRuntimeException& e) {
        std::printf("  [MenuStack] CreateMenu(\"%s\") -- Init() failed: %s\n", simkinPath.c_str(),
                    e.toString().ptr());
        m_Menus.erase(key);
        return nullptr;
    }
    return raw;
}

MenuExecutable* MenuStack::CreateRootMenu(const std::string& key, const std::string& filePath) {
    skExecutableContext loadCtxt(&m_Interpreter);
    std::unique_ptr<MenuExecutable> menu(new MenuExecutable(skString(filePath.c_str()), loadCtxt, *this));
    MenuExecutable* raw = menu.get();
    m_Menus[NormalizeKey(key)] = std::move(menu);
    m_Current = raw;
    raw->RunInit();
    return raw;
}

void MenuStack::OpenMenu(const std::string& simkinPath, skiExecutable* opener) {
    MenuExecutable* menu = GetOrCreateMenu(simkinPath, opener);
    if (!menu) {
        std::printf("  [MenuStack] OpenMenu(\"%s\") -- could not resolve, staying on current menu\n",
                    simkinPath.c_str());
        return;
    }
    // Covers the "already cached" case GetOrCreateMenu() can't set an
    // opener on itself (Init() doesn't rerun there, but the caller's
    // opener should still apply to a subsequent GetOpener() call).
    if (opener) menu->SetOpener(opener);
    m_Current = menu;
    // M56: same containment as Init() above -- OnDisplay() is script too.
    // The screen is already current and already drew its rows in Init(),
    // so a failure here logs and leaves it up rather than unwinding.
    try {
        menu->RunOnDisplay();
    } catch (skRuntimeException& e) {
        std::printf("  [MenuStack] OpenMenu(\"%s\") -- OnDisplay() failed: %s\n",
                    simkinPath.c_str(), e.toString().ptr());
    }
}

void MenuStack::ReopenMenu(const std::string& simkinPath, skiExecutable* opener) {
    // M17: drops the cached instance (if any) first, so GetOrCreateMenu()
    // rebuilds it from scratch and reruns its real Init() -- needed for
    // an NPC dialogue tree (monster_executable.h's OpenMenu handler)
    // whose Init() body is where the real quest-state branching happens
    // (e.g. snowline/tanyinconvo.s checks QuestSolved/QuestAssigned/
    // QuestCompleted there). Plain OpenMenu() only ever runs Init() once
    // per path (GetOrCreateMenu's own cache) and OnDisplay() on every
    // reopen -- correct for genuinely stateful screens navigated to and
    // from repeatedly (inventory/stats/options, whose Init() isn't meant
    // to redo its setup every visit), but wrong for a conversation that
    // needs to re-evaluate updated quest state each time it starts, so
    // this is used only for that narrower case, not a change to
    // OpenMenu()'s own general caching semantics.
    m_Menus.erase(NormalizeKey(simkinPath));
    OpenMenu(simkinPath, opener);
}

std::string MenuStack::SlotPath(int slot) const {
    // "game0%d.sav", the real format string -- the digit is appended to a
    // literal "game0", so slots above 9 would collide. There are four.
    char name[32];
    std::snprintf(name, sizeof(name), "game0%d.sav", slot);
    std::string path = m_SaveDir;
    if (!path.empty() && path.back() != '/' && path.back() != '\\') path += '/';
    return path + name;
}

bool MenuStack::GameAvailableForLoad(int slot) const {
    // The real GameAvailableForLoad probes the files themselves (with no
    // argument it walks 0..3), so this does too -- with the in-memory
    // flag as the fallback for a session that could not write any.
    auto probe = [this](int i) {
        if (m_SaveSlots[static_cast<size_t>(i)].used) return true;
        std::ifstream f(SlotPath(i), std::ios::binary);
        return f.good();
    };
    if (slot < 0) {
        for (int i = 0; i < kSaveSlotCount; ++i) {
            if (probe(i)) return true;
        }
        return false;
    }
    if (slot >= kSaveSlotCount) return false;
    return probe(slot);
}

void MenuStack::ActuallySaveGame(int slot) {
    if (slot < 0 || slot >= kSaveSlotCount) return;
    SaveSlot& s = m_SaveSlots[static_cast<size_t>(slot)];
    s.used = true;
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &local);
    s.timeStr = buf;

    // One archive, one member: `character.dat`, the player's own record.
    // The real writer adds a second member here, `<level>.dat`, built
    // from the live entity list (FUN_100187d0) -- this port has no
    // per-entity save state to put in one yet, so it writes the character
    // half only. A missing member is exactly what the real reader's
    // lookup already handles (FUN_1000b100 returns null and the caller
    // moves on), so the file stays loadable by its own rules.
    sk::SaveStream stream;
    m_Player->BuildSaveRecord(m_CurrentLevel).Write(stream);
    std::vector<sk::SaveArchive::Record> records;
    records.push_back({sk::kCharacterMemberName, stream.bytes()});
    const std::vector<uint8_t> bytes = sk::SaveArchive::Serialize(records);

    const std::string path = SlotPath(slot);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::printf("  [MenuStack] slot %d: cannot write %s -- keeping it in memory only\n", slot,
                    path.c_str());
        return;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    std::printf("  [MenuStack] saved to slot %d (%s) -- %s, %zu bytes\n", slot, s.timeStr.c_str(),
                path.c_str(), bytes.size());
}

std::string MenuStack::LoadGameFromSlot(int slot) {
    if (slot < 0 || slot >= kSaveSlotCount) return "";
    const std::string path = SlotPath(slot);
    std::ifstream in(path, std::ios::binary);
    if (!in) return "";
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
    std::vector<sk::SaveArchive::Record> records;
    if (!sk::SaveArchive::Parse(bytes, records)) {
        // The real open path wipes an archive whose size field disagrees
        // with the file and starts over -- the "SaveCorrupted" case.
        std::printf("  [MenuStack] slot %d: %s is corrupt\n", slot, path.c_str());
        return "";
    }
    const sk::SaveArchive::Record* member = sk::SaveArchive::Find(records, sk::kCharacterMemberName);
    if (!member) {
        std::printf("  [MenuStack] slot %d: no %s member\n", slot, sk::kCharacterMemberName);
        return "";
    }
    sk::SaveStream stream;
    stream.Reset(member->data);
    sk::SavedEntity record;
    record.kind = sk::SavedEntityKind::Player;
    // Nothing on the wire says what class a nested record is -- the real
    // loader resolves it from the typeId through entities.txt. This port
    // writes every inventory child under one class, so it reads them back
    // under the same one. See player_save.cpp.
    record.Read(stream, [](int32_t) { return PlayerExecutable::kInventoryChildKind; });
    if (stream.failed()) {
        std::printf("  [MenuStack] slot %d: %s is truncated\n", slot, sk::kCharacterMemberName);
        return "";
    }
    m_Player->ApplySaveRecord(record, *this);
    m_CurrentLevel = record.character.levelName;
    std::printf("  [MenuStack] loaded slot %d -- \"%s\", level \"%s\"\n", slot,
                record.player.characterName.c_str(), m_CurrentLevel.c_str());
    return m_CurrentLevel;
}

void MenuStack::DeleteGame(int slot) {
    if (slot < 0 || slot >= kSaveSlotCount) return;
    m_SaveSlots[static_cast<size_t>(slot)] = SaveSlot{};
    std::remove(SlotPath(slot).c_str());
}

void MenuStack::DeleteAllGames() {
    for (int i = 0; i < kSaveSlotCount; ++i) DeleteGame(i);
}

std::string MenuStack::GetSavedTimeStr(int slot) const {
    if (slot < 0 || slot >= kSaveSlotCount) return "";
    return m_SaveSlots[static_cast<size_t>(slot)].timeStr;
}

void MenuStack::RequestGameStart(std::string zoneName) {
    if (!m_StartingInventoryLoaded) {
        m_Player->LoadStartingInventory(*this);
        m_StartingInventoryLoaded = true;
    }
    m_GameStartRequested = true;
    m_RequestedZone = std::move(zoneName);
}

void MenuStack::ShowCredits() {
    if (m_CreditsLines.empty()) {
        std::ifstream f(m_ScriptRoot + "/credits.txt");
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            m_CreditsLines.push_back(line);
        }
        if (m_CreditsLines.empty()) m_CreditsLines.push_back("(credits.txt not found)");
    }
    m_CreditsActive = true;
}

}  // namespace sk_bindings
