#include "simkin_bindings/menu_stack.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>

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

}  // namespace sk_bindings
