// M3 smoke test: boots mainmenu.s through the MenuExecutable/MenuStack
// native bindings -- the same chain the real main menu will use once M4
// wires it into the window/input loop. Proves the "CreateMenu constructs
// 19 sibling menus, then OnDisplay populates real menu items" flow runs
// to completion with the soft-fail bindings, and dumps the resulting
// state so it can be checked by hand against mainmenu.s's own source
// (see the port scaffold plan's M3 verification criteria).
#include <cstdio>

#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRuntimeException.h"

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    std::printf("shadowkey-port M3 smoke test: booting mainmenu.s from %s\n", scriptRoot);

    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter);

    try {
        std::string mainMenuPath = std::string(scriptRoot) + "/mainmenu.s";
        sk_bindings::MenuExecutable* mainMenu = stack.CreateRootMenu("MainMenu", mainMenuPath);

        std::printf("\nshadowkey-port M3 smoke test: mainmenu.s state after Init()+OnDisplay()\n");
        std::printf("  background id: %d\n", mainMenu->backgroundId());
        std::printf("  %zu row(s):\n", mainMenu->rows().size());
        for (const auto& row : mainMenu->rows()) {
            std::printf("    textId=%-5d selectable=%-5s callback=%s\n", row.textId,
                        row.selectable ? "true" : "false", row.callback.c_str());
        }
        std::printf("shadowkey-port M3 smoke test: OK\n");
        return 0;
    } catch (skParseException& e) {
        std::printf("shadowkey-port M3 smoke test: PARSE ERROR: %s\n", e.toString().ptr());
        return 2;
    } catch (skRuntimeException& e) {
        std::printf("shadowkey-port M3 smoke test: RUNTIME ERROR: %s\n", e.toString().ptr());
        return 2;
    }
}
