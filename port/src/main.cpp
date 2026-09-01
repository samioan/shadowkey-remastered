// M4: renders the real mainmenu.s chain (M3) to the backbuffer with a
// placeholder bitmap font and real stringtable.eng text, and wires
// D-pad/confirm input to move selection and fire the selected item's
// script callback. See C:\Users\Admin\.claude\plans\vast-wandering-summit.md
// for the full staged plan -- this is the M4 milestone target ("main menu
// renders and is navigable").
#include <cstdio>
#include <memory>
#include <string>

#include "assets/string_table.h"
#include "engine/game_clock.h"
#include "engine/input_state.h"
#include "engine/pc_key_map.h"
#include "graphics/backbuffer.h"
#include "graphics/bitmap_font.h"
#include "platform/win32/window.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRuntimeException.h"

namespace {

// Colors are placeholders -- the real palette/background-image format was
// never RE'd (docs/GRAPHICS_FORMAT.md flags the image-cache source format
// as unconfirmed; see the port plan's known-stubs list). Flat colors here
// stand in for MenuBackground(id) until that's resolved.
constexpr uint16_t kBackgroundColor = sk::PackRGB565(16, 16, 32);
constexpr uint16_t kTextColor = sk::PackRGB565(220, 220, 220);
constexpr uint16_t kSelectedTextColor = sk::PackRGB565(255, 220, 80);
constexpr uint16_t kStaticTextColor = sk::PackRGB565(120, 120, 130);

void RenderMenu(sk::Backbuffer& backbuffer, sk_bindings::MenuExecutable& menu,
                 const sk::StringTable& strings) {
    backbuffer.Fill(kBackgroundColor);

    int y = 10;
    const int lineHeight = sk::BitmapFont::kGlyphHeight + 4;
    int itemIndex = 1;  // 1-based, matches MenuExecutable::selectedItem()
    for (const auto& item : menu.items()) {
        std::string text = strings.Get(item.textId);
        bool isSelected = item.selectable && itemIndex == menu.selectedItem();
        uint16_t color = !item.selectable ? kStaticTextColor
                          : isSelected     ? kSelectedTextColor
                                            : kTextColor;
        int x = 12;
        if (isSelected) {
            sk::BitmapFont::DrawString(backbuffer, 2, y, ">", kSelectedTextColor);
        }
        sk::BitmapFont::DrawString(backbuffer, x, y, text, color);
        y += lineHeight;
        ++itemIndex;
    }
}

}  // namespace

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    if (!strings.Load(std::string(scriptRoot) + "/stringtable.eng")) {
        std::printf("shadowkey-port: failed to load stringtable.eng from %s, aborting.\n",
                    scriptRoot);
        return 1;
    }

    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter);

    std::string mainMenuPath = std::string(scriptRoot) + "/mainmenu.s";
    skExecutableContext loadCtxt(&interpreter);
    std::unique_ptr<sk_bindings::MenuExecutable> mainMenu;
    try {
        mainMenu.reset(
            new sk_bindings::MenuExecutable(skString(mainMenuPath.c_str()), loadCtxt, stack));
        mainMenu->RunInit();
    } catch (skParseException& e) {
        std::printf("shadowkey-port: PARSE ERROR loading mainmenu.s: %s\n", e.toString().ptr());
        return 2;
    } catch (skRuntimeException& e) {
        std::printf("shadowkey-port: RUNTIME ERROR running mainmenu.s: %s\n", e.toString().ptr());
        return 2;
    }
    stack.SetCurrent(mainMenu.get());

    sk::Window window(sk::Backbuffer::kWidth * 3, sk::Backbuffer::kHeight * 3,
                       L"shadowkey-port (M4: main menu)");

    sk::InputState input;
    window.SetKeyCallback([&](int vkCode, bool down) {
        if (auto slot = sk::MapPcKeyToButtonSlot(vkCode)) {
            input.SetButton(*slot, down);
        }
    });

    sk::Backbuffer backbuffer;
    sk::GameClock clock;

    std::printf("shadowkey-port: M4 -- Up/Down move selection, Enter confirms, Esc goes back.\n");

    sk_bindings::MenuExecutable* lastMenu = nullptr;

    window.RunMessageLoop([&]() {
        if (window.ShouldClose()) return;
        if (!clock.PollTick()) return;

        sk_bindings::MenuExecutable* menu = stack.currentMenu();
        if (menu) {
            try {
                if (input.ConsumeJustPressed(sk::ButtonSlot::Up)) menu->MoveSelection(-1);
                if (input.ConsumeJustPressed(sk::ButtonSlot::Down)) menu->MoveSelection(1);
                if (input.ConsumeJustPressed(sk::ButtonSlot::LeftSelectionKey)) {
                    menu->ActivateSelected();
                }
                if (input.ConsumeJustPressed(sk::ButtonSlot::RightSelectionKey)) {
                    menu->TryInvoke("OnRightSoftkey");
                }
            } catch (skRuntimeException& e) {
                std::printf("shadowkey-port: RUNTIME ERROR: %s\n", e.toString().ptr());
            }
            menu = stack.currentMenu();  // a callback may have opened a new one
        }

        // A freshly opened menu (via OpenMenu()) starts with no selection
        // of its own -- snap to its first selectable item, same as the
        // root menu's one-time setup above.
        if (menu && menu != lastMenu) {
            menu->MoveSelection(0);
            lastMenu = menu;
        }

        if (menu) {
            RenderMenu(backbuffer, *menu, strings);
        } else {
            backbuffer.Fill(kBackgroundColor);
        }
        window.Present(backbuffer);
    });

    return 0;
}
