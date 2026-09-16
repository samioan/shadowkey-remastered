// M108 smoke test: the menu layout constants, taken from the engine
// rather than from a screenshot.
//
// Menu *layout* had no coverage at all before this milestone, and no way
// to get any: the positions of the ~500 menus built from bare
// AddMenuItem/AddStaticItem are not in the scripts, they are in
// `FUN_10076b64`. This pins what that function and its neighbours
// actually say, so the next person to "fix" a menu by eye has something
// to contradict them.
//
// The rendering itself is checked by eye through `SK_DUMP_MENUS=<dir>`,
// which writes every menu in the game to a .ppm in one pass (see main.cpp).
#include <cstdio>
#include <string>

#include "assets/string_table.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
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

// The engine's own 4-bit expansion: `FUN_1008f8a4` splits an RGB444 word
// as (c & 0xf00) >> 4, c & 0xf0, (c & 0xf) << 4 -- nibble << 4, so a full
// nibble is 0xf0. NOT the `nibble * 17` replicate expansion.
struct Rgb {
    int r, g, b;
};
constexpr Rgb Expand444(int c) {
    return Rgb{((c >> 8) & 0xF) << 4, ((c >> 4) & 0xF) << 4, (c & 0xF) << 4};
}

}  // namespace

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";

    namespace skb = sk_bindings;

    // ---- 1. the row-flow constants ----
    std::printf("\n-- 1. row flow (FUN_10076b64) --\n");
    {
        // `uVar9 = uVar8 + 0xc` after every text row. The `+ 0x18` arm in
        // the same function is dead: it is taken only when FUN_1007f49c
        // returns non-zero, and that function's every path ends `return 0`.
        Check(skb::kMenuRowPitch == 0xc, "row pitch is 0xc (12), and only 0xc");
        // The literal first argument to FUN_1007f49c in the case-0 arm.
        Check(skb::kStaticItemX == 9, "a left-aligned static item draws at x=9");
        // menu+0x96, seeded 0x32 by the menu constructor FUN_10073bd8.
        Check(skb::kDefaultMenuBackground == 0x14, "the default background is slot 20 (0x14)");
        Check(skb::kStaticItemWrapChars == 0x11, "AddStaticItem's default wrap is 17 chars");
        Check(skb::kLeftAlignedWrapChars == 0x19, "  forced to 25 for a left-aligned row");
    }

    // ---- 2. the colours ----
    std::printf("\n-- 2. colours (FUN_10073bd8 / FUN_1008f8a4) --\n");
    {
        Check(skb::kMenuItemColor444 == 0x733, "menu+0x90, an unselected menu item, is 0x733");
        Check(skb::kStaticItemColor444 == 0x752, "menu+0x92, a static item, is 0x752");
        Check(skb::kSelectedItemColor444 == 0xddd, "menu+0x94, the selected row, is 0xddd");
        Check(skb::kTextShadowColor444 == 0x0b96, "the left-aligned shadow is 0x0b96");

        // The expansion is the part that was wrong, so pin it explicitly:
        // a full nibble must come out 0xf0, not 0xff.
        constexpr Rgb white = Expand444(0xfff);
        Check(white.r == 0xf0 && white.g == 0xf0 && white.b == 0xf0,
              "0xfff expands to (0xf0,0xf0,0xf0) -- nibble<<4, not nibble*17");
        constexpr Rgb sel = Expand444(skb::kSelectedItemColor444);
        Check(sel.r == 0xd0 && sel.g == 0xd0 && sel.b == 0xd0,
              "  so the selected row is (208,208,208), not (235,235,235)");
        constexpr Rgb item = Expand444(skb::kMenuItemColor444);
        Check(item.r == 0x70 && item.g == 0x30 && item.b == 0x30,
              "  and an unselected item is (112,48,48), not (140,40,40)");
    }

    // ---- 3. titles ----
    std::printf("\n-- 3. AddTitle (FUN_1003136c case 7) --\n");
    {
        Check(skb::kTitleDefaultY == 10, "the default title y is 10");
        Check(skb::MenuExecutable::MenuTitle{}.y == skb::kTitleDefaultY,
              "  and MenuTitle carries that default, not -1");

        sk::StringTable strings;
        if (!strings.Load(std::string(scriptRoot) + "/stringtable.eng")) {
            std::printf("m108_menu_layout_smoke: FAILED to load stringtable.eng\n");
            return 1;
        }
        skInterpreter interpreter;
        skb::MenuStack stack(scriptRoot, interpreter, &strings);

        // choosecharactermenu.s opens with a bare `AddTitle(3783)` and then
        // builds its combo box and text area. The title must land at y=10
        // and must not have moved anything else.
        stack.OpenMenu("menus/choosecharactermenu");
        skb::MenuExecutable* menu = stack.currentMenu();
        Check(menu != nullptr, "menus/choosecharactermenu.s opens");
        if (menu) {
            Check(menu->titles().size() == 1, "  and adds exactly one title");
            if (!menu->titles().empty()) {
                Check(menu->titles()[0].y == 10,
                      "  whose bare AddTitle(3783) sits at y=10");
            }
        }

        // questlog.s is the other shape: two titles with explicit y's.
        stack.OpenMenu("questlog");
        skb::MenuExecutable* log = stack.currentMenu();
        Check(log != nullptr, "questlog.s opens");
        if (log && log->titles().size() >= 2) {
            Check(log->titles()[0].y == 10 && log->titles()[1].y == 25,
                  "  its AddTitle(3785,10) / AddTitle(3786,25) keep their own y");
        } else if (log) {
            Check(false, "  questlog.s should add two positioned titles");
        }
    }

    std::printf("\nm108_menu_layout_smoke: %d/%d checks passed -- %s\n", g_Checks - g_Failed,
                g_Checks, g_Failed == 0 ? "OK" : "FAILED");
    return g_Failed == 0 ? 0 : 1;
}
