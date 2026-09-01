#include "simkin_bindings/menu_stack.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/player_executable.h"
#include "skInterpreter.h"

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

MenuStack::MenuStack(std::string scriptRoot, skInterpreter& interpreter)
    : m_ScriptRoot(std::move(scriptRoot)),
      m_Interpreter(interpreter),
      m_Player(new PlayerExecutable()) {}

MenuStack::~MenuStack() = default;

MenuExecutable* MenuStack::GetOrCreateMenu(const std::string& simkinPath) {
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
    m_Menus[key] = std::move(menu);
    raw->RunInit();
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

void MenuStack::OpenMenu(const std::string& simkinPath) {
    MenuExecutable* menu = GetOrCreateMenu(simkinPath);
    if (!menu) {
        std::printf("  [MenuStack] OpenMenu(\"%s\") -- could not resolve, staying on current menu\n",
                    simkinPath.c_str());
        return;
    }
    m_Current = menu;
    menu->RunOnDisplay();
}

bool MenuStack::GameAvailableForLoad(int slot) const {
    if (slot < 0) {
        for (const auto& s : m_SaveSlots) {
            if (s.used) return true;
        }
        return false;
    }
    if (slot >= kSaveSlotCount) return false;
    return m_SaveSlots[static_cast<size_t>(slot)].used;
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
    std::printf("  [MenuStack] saved to slot %d (%s)\n", slot, s.timeStr.c_str());
}

void MenuStack::DeleteGame(int slot) {
    if (slot < 0 || slot >= kSaveSlotCount) return;
    m_SaveSlots[static_cast<size_t>(slot)] = SaveSlot{};
}

void MenuStack::DeleteAllGames() {
    for (auto& s : m_SaveSlots) s = SaveSlot{};
}

std::string MenuStack::GetSavedTimeStr(int slot) const {
    if (slot < 0 || slot >= kSaveSlotCount) return "";
    return m_SaveSlots[static_cast<size_t>(slot)].timeStr;
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
