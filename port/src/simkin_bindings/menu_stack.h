#pragma once

// Owns every menu script object created via CreateMenu(), and implements
// the "Menu-stack manager" class the corpus cross-check hypothesized at
// binding offset 0x14d8c (docs/SIMKIN_NATIVE_API.md). Resolves the path
// strings scripts pass to CreateMenu/OpenMenu (e.g. "Menus\\NewGameMenu",
// "SaveGameMenu") to real .s files under a configurable script root --
// see the port scaffold plan's "assets stay out of the repo" decision.

#include <map>
#include <memory>
#include <string>

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
    MenuStack(std::string scriptRoot, skInterpreter& interpreter);
    ~MenuStack();

    // Loads (or returns the already-loaded) menu for `simkinPath`, running
    // its Init() the first time it's created. Returns nullptr (logged) if
    // the target .s file doesn't exist or fails to parse.
    MenuExecutable* GetOrCreateMenu(const std::string& simkinPath);

    // Switches the "current" menu, creating it first if necessary, and
    // runs its OnDisplay(). Logs and no-ops if that fails.
    void OpenMenu(const std::string& simkinPath);

    MenuExecutable* currentMenu() const { return m_Current; }
    // Used once by the host to register the root menu (mainmenu.s) as
    // current -- it's constructed directly by the host, not via
    // CreateMenu/OpenMenu, so MenuStack never sees it otherwise.
    void SetCurrent(MenuExecutable* menu) { m_Current = menu; }
    PlayerExecutable& player() const { return *m_Player; }
    const std::string& scriptRoot() const { return m_ScriptRoot; }
    skInterpreter& interpreter() const { return m_Interpreter; }

private:
    std::string m_ScriptRoot;
    skInterpreter& m_Interpreter;
    std::map<std::string, std::unique_ptr<MenuExecutable>> m_Menus;
    std::unique_ptr<PlayerExecutable> m_Player;
    MenuExecutable* m_Current = nullptr;
};

}  // namespace sk_bindings
