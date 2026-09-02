// M5: deepens the menu chain from M4's main-menu-only milestone --
// New Game -> character creation (choose character/race/portrait, name
// entry) is now playable end to end through the menus, plus Load/Save/
// Delete (a simulated in-memory save system, no real save-file format
// RE'd) and Credits (a native-only screen reading credits.txt directly,
// with no script-side handler in the whole corpus) and the Quit
// confirmation popup actually closing the app. See
// C:\Users\Admin\.claude\plans\vast-wandering-summit.md for the original
// M0-M4 plan; M5 continues past it in the same "deepen the menu chain"
// direction the user chose. M6 added the 3D zone renderer, M7 tile-grid
// collision, M8 placed-entity rendering -- see docs/PORT_ROADMAP.md.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "engine/game_clock.h"
#include "engine/input_state.h"
#include "engine/pc_key_map.h"
#include "graphics/backbuffer.h"
#include "graphics/bitmap_font.h"
#include "platform/win32/window.h"
#include "render3d/camera.h"
#include "render3d/zone_renderer.h"
#include "simkin_bindings/combo_box_executable.h"
#include "simkin_bindings/floating_sprite_executable.h"
#include "simkin_bindings/item_button_executable.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/popup_menu_executable.h"
#include "simkin_bindings/table_executable.h"
#include "simkin_bindings/text_area_executable.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"
#include "world/model_archive.h"
#include "world/zone.h"

namespace {

// Colors are placeholders -- the real palette/background-image format was
// never RE'd (docs/GRAPHICS_FORMAT.md flags the image-cache source format
// as unconfirmed; see the port plan's known-stubs list). Flat colors here
// stand in for MenuBackground(id) until that's resolved.
constexpr uint16_t kBackgroundColor = sk::PackRGB565(16, 16, 32);
constexpr uint16_t kTextColor = sk::PackRGB565(220, 220, 220);
constexpr uint16_t kSelectedTextColor = sk::PackRGB565(255, 220, 80);
constexpr uint16_t kStaticTextColor = sk::PackRGB565(120, 120, 130);
constexpr uint16_t kTitleColor = sk::PackRGB565(140, 180, 255);
constexpr uint16_t kPopupBgColor = sk::PackRGB565(40, 40, 60);
constexpr uint16_t kPopupBorderColor = sk::PackRGB565(90, 90, 130);

// Word-wraps `text` at `maxChars` per line, drawing each line starting at
// (x,y). Returns the number of lines drawn, so callers can advance their
// own layout cursor by that many line-heights.
int DrawWrappedText(sk::Backbuffer& bb, int x, int y, const std::string& text, int maxChars,
                     uint16_t color) {
    if (maxChars <= 0) maxChars = 27;
    const int lineHeight = sk::BitmapFont::kGlyphHeight + 3;
    std::vector<std::string> lines;
    std::string current;
    std::istringstream words(text);
    std::string word;
    while (words >> word) {
        std::string candidate = current.empty() ? word : current + " " + word;
        if (static_cast<int>(candidate.size()) > maxChars && !current.empty()) {
            lines.push_back(current);
            current = word;
        } else {
            current = candidate;
        }
    }
    if (!current.empty() || lines.empty()) lines.push_back(current);
    for (size_t i = 0; i < lines.size(); ++i) {
        sk::BitmapFont::DrawString(bb, x, y + static_cast<int>(i) * lineHeight, lines[i], color);
    }
    return static_cast<int>(lines.size());
}

// A row's display text: a literal (already-resolved) string if one was
// set (AddButton("Cymric",...), AddFloatingText(healthText,...)) takes
// priority; otherwise resolves textId through the real stringtable, same
// as every row kind before M10. See menu_executable.h's class comment.
std::string RowText(int textId, const std::string& literalText, const sk::StringTable& strings) {
    if (!literalText.empty()) return literalText;
    if (textId < 0) return "";
    return strings.Get(textId);
}

// "ChooseMale" -> "Male", "ChooseFemale" -> "Female" -- strips the
// convention these callbacks use; falls back to the raw name otherwise.
// Real portrait sprites are out of scope here (see
// FloatingSpriteExecutable's header comment), so this text label stands
// in for the picker entirely.
std::string SpriteLabel(const std::string& callback) {
    const std::string prefix = "Choose";
    if (callback.size() > prefix.size() && callback.compare(0, prefix.size(), prefix) == 0) {
        return callback.substr(prefix.size());
    }
    return callback;
}

void RenderPopup(sk::Backbuffer& backbuffer, sk_bindings::PopupMenuExecutable& popup,
                  const sk::StringTable& strings) {
    int x0 = 10, y0 = 60, x1 = sk::Backbuffer::kWidth - 10, y1 = 150;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            bool border = (x == x0 || x == x1 - 1 || y == y0 || y == y1 - 1);
            backbuffer.SetPixel(x, y, border ? kPopupBorderColor : kPopupBgColor);
        }
    }
    int y = y0 + 6;
    int itemIndex = 1;
    for (const auto& item : popup.items()) {
        if (item.blanked) continue;  // M10: UpdatePopupItem(index, "") hides this row entirely
        bool selectable = sk_bindings::PopupMenuExecutable::IsSelectable(item);
        bool isSelected = selectable && itemIndex == popup.selectedItem();
        uint16_t color = isSelected ? kSelectedTextColor : kTextColor;
        std::string text = !item.literalText.empty() ? item.literalText : strings.Get(item.textId);
        int lines = DrawWrappedText(backbuffer, x0 + 6, y, text, 22, color);
        y += lines * (sk::BitmapFont::kGlyphHeight + 3) + 2;
        if (selectable) ++itemIndex;
    }
}

void RenderMenu(sk::Backbuffer& backbuffer, sk_bindings::MenuExecutable& menu,
                 sk_bindings::PlayerExecutable& player, const sk::StringTable& strings) {
    using RowKind = sk_bindings::MenuExecutable::RowKind;
    backbuffer.Fill(kBackgroundColor);

    int y = 8;
    const int lineHeight = sk::BitmapFont::kGlyphHeight + 4;

    if (menu.titleTextId() >= 0) {
        sk::BitmapFont::DrawString(backbuffer, 4, y, strings.Get(menu.titleTextId()), kTitleColor);
        y += lineHeight + 2;
    }

    int itemIndex = 1;
    for (const auto& row : menu.rows()) {
        bool isSelected = row.selectable && itemIndex == menu.selectedItem();
        uint16_t color = !row.selectable      ? kStaticTextColor
                          : isSelected         ? kSelectedTextColor
                                                : kTextColor;
        if (isSelected) sk::BitmapFont::DrawString(backbuffer, 2, y, ">", kSelectedTextColor);

        switch (row.kind) {
            case RowKind::MenuItem:
            case RowKind::StaticItem:
                sk::BitmapFont::DrawString(backbuffer, 12, y,
                                            RowText(row.textId, row.literalText, strings), color);
                y += lineHeight;
                break;
            case RowKind::ItemButton: {
                auto* item = static_cast<sk_bindings::ItemButtonExecutable*>(row.widget.get());
                if (item->visible()) {
                    std::string label = RowText(item->textId(), item->itemText(), strings);
                    sk::BitmapFont::DrawString(backbuffer, 12, y, label, color);
                    y += lineHeight;
                }
                break;
            }
            case RowKind::Table: {
                auto* table = static_cast<sk_bindings::TableExecutable*>(row.widget.get());
                // Only a handful of rows fit on a 208px-tall screen
                // alongside everything else on the page -- show a window
                // centered on the current selection rather than the whole
                // table (a real scrollable viewport, docs/PORT_ROADMAP.md
                // flags this table widget's layout as simplified overall).
                constexpr int kVisibleRows = 6;
                int selected = table->selectedRow();
                // Parenthesized -- this file transitively includes the
                // vendored Simkin headers (skGeneral.h), which '#define
                // max(a,b)'/'min(a,b)' as plain macros; (std::max)(...)
                // defeats the function-like-macro expansion without
                // touching that vendored file.
                int start = (std::max)(0, selected - kVisibleRows / 2);
                int end = (std::min)(table->rowCount(), start + kVisibleRows);
                for (int r = start; r < end; ++r) {
                    std::string line = table->CellText(r, 0);
                    for (int c = 1; c < table->columnCount(); ++c) {
                        std::string cell = table->CellText(r, c);
                        if (!cell.empty()) line += "  " + cell;
                    }
                    uint16_t rowColor = (isSelected && r == selected) ? kSelectedTextColor
                                                                       : kTextColor;
                    sk::BitmapFont::DrawString(backbuffer, 16, y, line, rowColor);
                    y += lineHeight;
                }
                if (table->rowCount() == 0) {
                    sk::BitmapFont::DrawString(backbuffer, 16, y, "(empty)", kStaticTextColor);
                    y += lineHeight;
                }
                break;
            }
            case RowKind::ComboBox: {
                auto* combo = static_cast<sk_bindings::ComboBoxExecutable*>(row.widget.get());
                int value = combo->currentOptionValue();
                std::string label = value >= 0 ? ("< " + strings.Get(value) + " >") : "< -- >";
                sk::BitmapFont::DrawString(backbuffer, 12, y, label, color);
                y += lineHeight;
                break;
            }
            case RowKind::TextArea: {
                auto* textArea = static_cast<sk_bindings::TextAreaExecutable*>(row.widget.get());
                int lines = DrawWrappedText(backbuffer, 12, y, strings.Get(row.textId),
                                             textArea->textWidth(), color);
                y += lines * lineHeight;
                break;
            }
            case RowKind::FloatingSprite: {
                auto* sprite = static_cast<sk_bindings::FloatingSpriteExecutable*>(row.widget.get());
                std::string label = "[ " + SpriteLabel(sprite->callback()) + " ]";
                sk::BitmapFont::DrawString(backbuffer, 12, y, label, color);
                y += lineHeight;
                break;
            }
            case RowKind::TextEntry: {
                std::string label = "NAME: " + player.charNameBuffer() + "_";
                sk::BitmapFont::DrawString(backbuffer, 12, y, label, kSelectedTextColor);
                y += lineHeight;
                break;
            }
        }
        if (row.selectable) ++itemIndex;
    }

    if (auto* popup = menu.activePopup()) {
        RenderPopup(backbuffer, *popup, strings);
    }
}

// M10: a minimal always-on HUD during the 3D view -- three bars reading
// the player's real vitals (world/PlayerExecutable state, the same
// numbers charactermanager.s's health/magicka/fatigue text lines show).
//
// Positioned bottom-left this session, after a real screenshot comparison
// (a player-submitted port screenshot vs. the original game) showed the
// real HUD's health/magicka/fatigue bars anchored there (inside an ornate
// gold dragon-head/wing border), not top-left, plus a separate compass
// banner across the top (heading readout flanked by two dragon heads) --
// neither of those two art pieces exist as a decoded on-disk asset yet:
// docs/GRAPHICS_FORMAT.md's "Open follow-ups" already flags the 384-slot
// sprite/icon cache's *source* file format as unresolved (only the
// in-memory RLE layout `Blit_RLESprite` reads is decoded), so there's no
// known way yet to blit the real dragon-head/compass art here -- still a
// stand-in, same "not a byte-exact reproduction" spirit as the bitmap
// font/flat menu backgrounds, just correctly *placed* now instead of in
// an arbitrary corner. Finding that sprite source format (probably the
// natural next step for pixel-accurate HUD art) is tracked as a follow-up
// in docs/PORT_ROADMAP.md, not attempted here.
void RenderHud(sk::Backbuffer& backbuffer, const sk_bindings::PlayerExecutable& player) {
    constexpr int kBarWidth = 50, kBarHeight = 4, kBarGap = 2;
    constexpr int kBarCount = 3;
    constexpr int x0 = 4;
    int y = sk::Backbuffer::kHeight - 4 - kBarCount * kBarHeight - (kBarCount - 1) * kBarGap;
    auto drawBar = [&](int value, int maxValue, uint16_t color) {
        // See the (std::max)/(std::min) comment above -- same vendored-
        // header macro-collision workaround.
        int filled = maxValue > 0 ? (kBarWidth * (std::max)(0, value)) / maxValue : 0;
        filled = (std::min)(filled, kBarWidth);
        for (int dy = 0; dy < kBarHeight; ++dy) {
            for (int dx = 0; dx < kBarWidth; ++dx) {
                backbuffer.SetPixel(x0 + dx, y + dy, dx < filled ? color : kPopupBorderColor);
            }
        }
        y += kBarHeight + kBarGap;
    };
    drawBar(player.health(), player.maxHealth(), sk::PackRGB565(200, 40, 40));
    drawBar(player.magicka(), player.maxMagicka(), sk::PackRGB565(60, 80, 220));
    drawBar(player.fatigue(), player.maxFatigue(), sk::PackRGB565(60, 180, 80));
}

void RenderCredits(sk::Backbuffer& backbuffer, const std::vector<std::string>& lines,
                    int scrollOffset) {
    backbuffer.Fill(kBackgroundColor);
    const int lineHeight = sk::BitmapFont::kGlyphHeight + 4;
    int y = 8;
    int maxLines = sk::Backbuffer::kHeight / lineHeight;
    for (int i = 0; i < maxLines && (scrollOffset + i) < static_cast<int>(lines.size()); ++i) {
        sk::BitmapFont::DrawString(backbuffer, 4, y, lines[static_cast<size_t>(scrollOffset + i)],
                                    kTextColor);
        y += lineHeight;
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

    // M8: the global model archive (models.idx/.huge) and entity type
    // table (entities.txt) -- loaded once at startup, matching the real
    // engine's own GameEngine_FirstTickBootstrap timing for
    // EntityTypeConfig_Load (docs/ZONE_FORMAT.md). Not fatal to fail --
    // the game still runs, just without placed entities in the 3D view.
    sk::EntityTypeTable entityTypes;
    entityTypes.Load(scriptRoot);
    sk::ModelArchive modelArchive;
    modelArchive.Load(scriptRoot);

    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);

    std::string mainMenuPath = std::string(scriptRoot) + "/mainmenu.s";
    try {
        // Registered under "MainMenu" -- the exact string every "back to
        // main menu" handler across the corpus (SaveGameMenuBack,
        // LoadGameMenuBack, OptionsBack, newgamemenu.s's MenuQuit, ...)
        // passes to OpenMenu(). Without this, each of those would
        // silently construct and switch to a *new*, separate MenuStack-
        // owned instance instead of coming back to this same one.
        stack.CreateRootMenu("MainMenu", mainMenuPath);
    } catch (skParseException& e) {
        std::printf("shadowkey-port: PARSE ERROR loading mainmenu.s: %s\n", e.toString().ptr());
        return 2;
    } catch (skRuntimeException& e) {
        std::printf("shadowkey-port: RUNTIME ERROR running mainmenu.s: %s\n", e.toString().ptr());
        return 2;
    }

    sk::Window window(sk::Backbuffer::kWidth * 3, sk::Backbuffer::kHeight * 3,
                       L"shadowkey-port (M6: 3D zone renderer)");

    sk::InputState input;
    window.SetKeyCallback([&](int vkCode, bool down) {
        if (auto slot = sk::MapPcKeyToButtonSlot(vkCode)) {
            input.SetButton(*slot, down);
        }
    });
    window.SetCharCallback([&](wchar_t ch) {
        sk_bindings::MenuExecutable* menu = stack.currentMenu();
        if (!menu || !menu->textEntryActive()) return;
        if (ch == 0x08) {
            stack.player().BackspaceCharName();
        } else if (ch >= 0x20 && ch < 0x7F && stack.player().charNameBuffer().size() < 20) {
            stack.player().AppendCharNameChar(static_cast<char>(ch));
        }
        // 0x0D (Enter) is handled in the tick loop below, not here --
        // it needs to fire a script callback, which the char callback
        // (running off the message pump, not the tick loop) shouldn't do.
    });

    sk::Backbuffer backbuffer;
    sk::GameClock clock;

    std::printf(
        "shadowkey-port: menus -- Up/Down move selection, Left/Right cycle combo values or "
        "navigate horizontal screens, Enter confirms, Esc goes back.\n");
    std::printf(
        "shadowkey-port: New Game/Load Game enter the 3D zone (M6) -- Up/Down walk, "
        "Left/Right turn, Esc returns to the main menu.\n");
    std::printf(
        "shadowkey-port: '=' (stands in for the N-Gage's '#') opens the character manager "
        "(real inventory/stats/quest-log screens, M10) -- Esc there always returns straight "
        "to the 3D view.\n");

    sk_bindings::MenuExecutable* lastMenu = nullptr;
    int creditsScroll = 0;

    // M6: the 3D zone renderer, entered when a menu calls NewGame()/
    // LoadGame() (see MenuStack::RequestGameStart()). M7 added simple
    // circle-vs-wall-tile collision (Zone::CircleHitsWall); M8 added
    // placed-entity rendering (props/monsters/doors resolved through
    // entities.txt -> models.idx) -- see render3d/zone_renderer.h for
    // what each milestone does and doesn't reproduce from the real
    // engine.
    std::unique_ptr<sk::Zone> gameZone;
    std::vector<sk::PlacedEntity> gameEntities;
    sk::Camera gameCamera;
    sk::ZoneRenderer zoneRenderer;
    bool inGame = false;
    // M10: set while the character-manager screen chain is open *from*
    // the 3D view (see the CharacterManager action below) -- gameZone/
    // gameCamera stay alive so RightSelectionKey can resume gameplay
    // directly instead of falling through to charactermanager.s's own
    // OnRightSoftKey handler, which calls Quit()+OpenMainMenu() (correct
    // for reaching it from a menu, wrong for reaching it mid-game -- no
    // real in-game pause-menu entry point was ever found to disambiguate
    // the two contexts, so this is a deliberate host-side simplification:
    // RightSelectionKey always means "back to gameplay" here, even from a
    // nested Inventory/Stats/QuestLog screen, rather than backing out one
    // level at a time).
    bool gamePausedForMenu = false;

    window.RunMessageLoop([&]() {
        if (window.ShouldClose()) return;
        if (!clock.PollTick()) return;

        // M10: erase any inventory items marked for removal last tick
        // (UseItem/DropItem) -- safe here since any script call chain
        // that marked them has long since returned. See item_executable.h.
        stack.player().PurgeRemovedItems();

        if (stack.quitRequested()) {
            window.Close();
            return;
        }

        if (stack.gameStartRequested()) {
            stack.ClearGameStartRequest();
            auto zone = std::make_unique<sk::Zone>();
            if (zone->Load(scriptRoot, stack.requestedZone())) {
                gameZone = std::move(zone);
                gameCamera.x = static_cast<float>(gameZone->playerStartX);
                gameCamera.y = static_cast<float>(gameZone->playerStartY);
                gameCamera.z = static_cast<float>(gameZone->playerStartZ) + sk::kEyeHeightOffset;
                gameCamera.yaw = 0.0f;
                gameCamera.fovY = 1.2f;
                inGame = true;

                // Resolve every placed .ent record to a model archive
                // index via entities.txt -- same two-step chain the real
                // engine walks (docs/ZONE_FORMAT.md), just done eagerly
                // here instead of through the engine+0x6b38 zone-local
                // cache.
                gameEntities.clear();
                for (const sk::Zone::EntPlacement& e : gameZone->entities()) {
                    const sk::EntityTypeDescriptor* desc = entityTypes.Lookup(e.typeId);
                    if (!desc) continue;
                    gameEntities.push_back({static_cast<float>(e.x), static_cast<float>(e.y),
                                             static_cast<float>(e.z), desc->modelArchiveIndex});
                }
            } else {
                std::printf("shadowkey-port: failed to load zone '%s', staying in menu\n",
                            stack.requestedZone().c_str());
            }
        }

        if (inGame && gameZone) {
            // M10: the real default control scheme's own CharacterManager
            // action (docs/INPUT_HANDLING.md, KeyHash by default) opens
            // the real charactermanager.s screen chain, pausing the 3D
            // view -- RightSelectionKey (Esc) still returns straight to
            // the main menu when not paused for a menu.
            if (input.ConsumeBoundJustPressed(sk::Action::CharacterManager)) {
                stack.OpenMenu("charactermanager");
                inGame = false;
                gamePausedForMenu = true;
            } else if (input.ConsumeJustPressed(sk::ButtonSlot::RightSelectionKey)) {
                inGame = false;
            } else {
                constexpr float kMoveSpeed = 40.0f;    // world units/tick (256 units/tile)
                constexpr float kTurnSpeed = 0.06f;    // radians/tick
                constexpr float kPlayerRadius = 48.0f;  // world units, M7 collision
                // yaw's forward vector rotates toward -right as yaw increases
                // (see render3d/zone_renderer.cpp's forward/right basis), so
                // turning right means *decreasing* yaw.
                if (input.GetButton(sk::ButtonSlot::Left)) gameCamera.yaw += kTurnSpeed;
                if (input.GetButton(sk::ButtonSlot::Right)) gameCamera.yaw -= kTurnSpeed;
                float dx = std::cos(gameCamera.yaw) * kMoveSpeed;
                float dy = std::sin(gameCamera.yaw) * kMoveSpeed;
                // Axis-separated collision (try X, then Y, independently)
                // gives a simple wall-slide instead of a hard stop the
                // instant either component would clip a wall.
                auto tryMove = [&](float mx, float my) {
                    float nx = gameCamera.x + mx;
                    if (!gameZone->CircleHitsWall(nx, gameCamera.y, kPlayerRadius)) {
                        gameCamera.x = nx;
                    }
                    float ny = gameCamera.y + my;
                    if (!gameZone->CircleHitsWall(gameCamera.x, ny, kPlayerRadius)) {
                        gameCamera.y = ny;
                    }
                };
                if (input.GetButton(sk::ButtonSlot::Up)) tryMove(dx, dy);
                if (input.GetButton(sk::ButtonSlot::Down)) tryMove(-dx, -dy);
                zoneRenderer.Render(backbuffer, *gameZone, gameCamera, gameEntities, &modelArchive);
                RenderHud(backbuffer, stack.player());
                window.Present(backbuffer);
                return;
            }
        }

        if (gamePausedForMenu && input.ConsumeJustPressed(sk::ButtonSlot::RightSelectionKey)) {
            // See gamePausedForMenu's declaration comment -- always
            // resumes gameplay directly, bypassing whatever menu screen
            // (charactermanager.s or a nested Inventory/Stats/QuestLog)
            // is currently open.
            gamePausedForMenu = false;
            inGame = true;
            zoneRenderer.Render(backbuffer, *gameZone, gameCamera, gameEntities, &modelArchive);
            RenderHud(backbuffer, stack.player());
            window.Present(backbuffer);
            return;
        }

        if (stack.creditsActive()) {
            if (input.ConsumeJustPressed(sk::ButtonSlot::Up) && creditsScroll > 0) --creditsScroll;
            if (input.ConsumeJustPressed(sk::ButtonSlot::Down)) ++creditsScroll;
            if (input.ConsumeJustPressed(sk::ButtonSlot::LeftSelectionKey) ||
                input.ConsumeJustPressed(sk::ButtonSlot::RightSelectionKey)) {
                stack.CloseCredits();
                creditsScroll = 0;
            }
            RenderCredits(backbuffer, stack.creditsLines(), creditsScroll);
            window.Present(backbuffer);
            return;
        }

        sk_bindings::MenuExecutable* menu = stack.currentMenu();
        if (menu) {
            try {
                if (sk_bindings::PopupMenuExecutable* popup = menu->activePopup()) {
                    // A visible confirmation popup captures input ahead of
                    // the underlying menu's own row navigation.
                    if (input.ConsumeJustPressed(sk::ButtonSlot::Up)) popup->MoveSelection(-1);
                    if (input.ConsumeJustPressed(sk::ButtonSlot::Down)) popup->MoveSelection(1);
                    if (input.ConsumeJustPressed(sk::ButtonSlot::LeftSelectionKey)) {
                        popup->ActivateSelected();
                    }
                    if (input.ConsumeJustPressed(sk::ButtonSlot::RightSelectionKey)) {
                        popup->GoBack();
                    }
                } else if (menu->textEntryActive()) {
                    // Typed characters land via SetCharCallback above; here
                    // just the two control actions matter -- confirm and
                    // back. The corpus spells the back handler both
                    // "OnRightSoftkey" and "OnRightSoftKey" depending on
                    // the file (a genuine authoring inconsistency in the
                    // original scripts, not something to "fix") --
                    // TryInvoke no-ops silently on whichever name a given
                    // screen doesn't define, so trying both is harmless.
                    if (input.ConsumeJustPressed(sk::ButtonSlot::LeftSelectionKey)) {
                        menu->TryInvoke("Done");
                    }
                    if (input.ConsumeJustPressed(sk::ButtonSlot::RightSelectionKey)) {
                        menu->TryInvoke("OnRightSoftkey");
                        menu->TryInvoke("OnRightSoftKey");
                    }
                } else {
                    // M10: Up/Down on a Table row (inventory/stats/quest-
                    // log screens) navigates within the table instead of
                    // to the next outer row -- TryMoveTableSelection()
                    // only does something (and returns true) when the
                    // current selection actually is a Table.
                    if (input.ConsumeJustPressed(sk::ButtonSlot::Up)) {
                        if (!menu->TryMoveTableSelection(-1)) menu->MoveSelection(-1);
                    }
                    if (input.ConsumeJustPressed(sk::ButtonSlot::Down)) {
                        if (!menu->TryMoveTableSelection(1)) menu->MoveSelection(1);
                    }
                    if (menu->useHoriz()) {
                        // Portrait/name-entry-style screens: Left/Right
                        // navigate rows instead of cycling a combo (there
                        // isn't one on these screens anyway).
                        if (input.ConsumeJustPressed(sk::ButtonSlot::Left)) menu->MoveSelection(-1);
                        if (input.ConsumeJustPressed(sk::ButtonSlot::Right)) menu->MoveSelection(1);
                    } else {
                        if (input.ConsumeJustPressed(sk::ButtonSlot::Left)) {
                            menu->CycleSelectedCombo(-1);
                        }
                        if (input.ConsumeJustPressed(sk::ButtonSlot::Right)) {
                            menu->CycleSelectedCombo(1);
                        }
                    }
                    if (input.ConsumeJustPressed(sk::ButtonSlot::LeftSelectionKey)) {
                        // M10: Enter on a selected Table row fires its own
                        // SetCallback() handler (inventory.s's
                        // SelectedInventoryItem, statsscreen.s's
                        // OnTableSel, ...) instead of the outer row's
                        // usual ActivateSelected() path.
                        if (!menu->TryActivateTable()) menu->ActivateSelected();
                    }
                    if (input.ConsumeJustPressed(sk::ButtonSlot::RightSelectionKey)) {
                        menu->TryInvoke("OnRightSoftkey");
                        menu->TryInvoke("OnRightSoftKey");
                    }
                }
            } catch (skRuntimeException& e) {
                std::printf("shadowkey-port: RUNTIME ERROR: %s\n", e.toString().ptr());
            }
            menu = stack.currentMenu();  // a callback may have opened a new one
        }

        // A freshly opened menu (via OpenMenu()) starts with no selection
        // of its own -- snap to its first selectable row.
        if (menu && menu != lastMenu) {
            menu->MoveSelection(0);
            lastMenu = menu;
        }

        if (menu) {
            RenderMenu(backbuffer, *menu, stack.player(), strings);
        } else {
            backbuffer.Fill(kBackgroundColor);
        }
        window.Present(backbuffer);
    });

    return 0;
}
