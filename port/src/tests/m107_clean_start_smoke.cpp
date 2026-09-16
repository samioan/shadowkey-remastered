// M107 smoke test: what a brand-new character actually starts with, and
// what the game draws over the view before the player has asked for
// anything.
//
// Three parts:
//
//   1. A New Game grants **no inventory**. The port used to hand out
//      weapons/club.s, armor/chain_coif.s and items/bread.s -- M10's
//      real-data fixture, wired into MenuStack::RequestGameStart and
//      mistaken for the game's own starting kit ever since. It is not:
//      see player_executable.h for the four independent places in the
//      decompile and the script corpus that show the original grants
//      nothing, and azra.ent for where the dagger and Blaze really come
//      from.
//
//   2. An empty table does not fire its row-select callback. With every
//      inventory category now empty on a new character, pressing Enter on
//      one used to run `inventory.s:SelectedInventoryItem`, whose first
//      act is `inventoryTable.GetSelectedRow().GetAssociatedObject()` --
//      a method call on nothing. `FUN_1008d978` skips the invoke
//      entirely when the selection is out of range; see
//      table_executable.cpp.
//
//   3. The debug overlay starts silent -- no page *and* no mini bar.
#include <cstdio>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "debug/debug_overlay.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/table_executable.h"
#include "skInterpreter.h"
#include "skRValue.h"
#include "skRValueArray.h"

namespace {

int g_Checks = 0;
int g_Failed = 0;

void Check(bool ok, const std::string& what) {
    ++g_Checks;
    if (!ok) ++g_Failed;
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what.c_str());
}

}  // namespace

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    if (!strings.Load(std::string(scriptRoot) + "/stringtable.eng")) {
        std::printf("m107_clean_start_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }

    // ---- 1. a New Game grants nothing ----
    std::printf("\n-- 1. New Game grants no inventory --\n");
    {
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);

        stack.RequestGameStart("azra");
        Check(stack.player().inventory().empty(),
              "RequestGameStart() leaves the inventory empty");
        Check(stack.gameStartRequested(), "  and still actually starts the game");

        // A second New Game (or a Load Game, which takes the same path)
        // must not start granting things either.
        stack.RequestGameStart("azra");
        Check(stack.player().inventory().empty(), "a second RequestGameStart() grants nothing too");

        // The fixture loader still works -- the tests that need real item
        // scripts go through it now.
        stack.player().LoadItemScripts(
            stack, {"weapons/club.s", "armor/chain_coif.s", "items/bread.s"});
        Check(stack.player().inventory().size() == 3,
              "LoadItemScripts() still loads the three real fixture scripts");
    }

    // ---- 2. an empty table fires no callback ----
    std::printf("\n-- 2. an empty table's row-select callback --\n");
    {
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);

        // inventory.s is the real screen this bites on. Opening it with an
        // empty inventory and activating the table must not throw: before
        // M107 this raised
        //   "Inventory.s:SelectedInventoryItem:1-Method GetAssociatedObject
        //    not found"
        // out of the interpreter.
        stack.RequestGameStart("azra");
        stack.OpenMenu("inventory");
        sk_bindings::MenuExecutable* menu = stack.currentMenu();
        Check(menu != nullptr, "inventory.s opens");
        if (menu) {
            sk_bindings::TableExecutable* table = nullptr;
            for (const auto& row : menu->rows()) {
                if (row.kind == sk_bindings::MenuExecutable::RowKind::Table) {
                    table = static_cast<sk_bindings::TableExecutable*>(row.widget.get());
                    break;
                }
            }
            Check(table != nullptr, "  and builds its inventory table");
            if (table) {
                Check(table->rowCount() == 0, "  which is empty for a new character");
                bool threw = false;
                try {
                    table->ActivateSelected();
                } catch (...) {
                    threw = true;
                }
                Check(!threw, "  activating the empty table throws nothing");
                // And the popup the handler would have opened stays shut,
                // which is the observable half: no row, no action menu.
                Check(menu->activePopup() == nullptr,
                      "  and no action popup is opened for a row that isn't there");
            }
        }
    }

    // ---- 3. the overlay starts silent ----
    std::printf("\n-- 3. the debug overlay starts off --\n");
    {
        sk_debug::DebugOverlay overlay;
        Check(overlay.page().empty(), "no overlay page is selected at construction");
        Check(!overlay.visible(), "  so the panel column is not visible");
        Check(!overlay.miniBar(), "the mini bar is off at construction");
        // Still reachable -- this is a default, not a removal.
        overlay.SetMiniBar(true);
        Check(overlay.miniBar(), "  Shift+F1 / `mini` still turns it on");
    }

    std::printf("\nm107_clean_start_smoke: %d/%d checks passed -- %s\n", g_Checks - g_Failed,
                g_Checks, g_Failed == 0 ? "OK" : "FAILED");
    return g_Failed == 0 ? 0 : 1;
}
