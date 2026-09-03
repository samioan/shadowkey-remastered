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

#include "assets/sprite_archive.h"
#include "assets/string_table.h"
#include "engine/game_clock.h"
#include "engine/input_state.h"
#include "engine/pc_key_map.h"
#include "graphics/backbuffer.h"
#include "graphics/bitmap_font.h"
#include "platform/win32/window.h"
#include "render3d/camera.h"
#include "render3d/zone_renderer.h"
#include "simkin_bindings/combat.h"
#include "simkin_bindings/combo_box_executable.h"
#include "simkin_bindings/door_executable.h"
#include "simkin_bindings/floating_sprite_executable.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/item_button_executable.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/popup_menu_executable.h"
#include "simkin_bindings/table_executable.h"
#include "simkin_bindings/text_area_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"
#include "world/model_archive.h"
#include "world/zone.h"

namespace {

// kBackgroundColor is a placeholder -- only the flat fallback fill for
// when a real MenuBackground() sprite fails to load (docs/
// GRAPHICS_FORMAT.md); the real backgrounds themselves are decoded
// sprite art, not a color. The menu text colors below, though, are
// real values sampled directly off a real screenshot (unselected =
// dark red, selected = near-white, static/disabled kept as a muted
// tone distinct from both -- not confirmed against a real disabled
// row, no screenshot of one on hand, but at minimum no longer
// identical-looking to the enabled colors).
constexpr uint16_t kBackgroundColor = sk::PackRGB565(16, 16, 32);
constexpr uint16_t kTextColor = sk::PackRGB565(140, 40, 40);
constexpr uint16_t kSelectedTextColor = sk::PackRGB565(235, 235, 235);
constexpr uint16_t kStaticTextColor = sk::PackRGB565(110, 95, 75);
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

// M15/M16: entities.txt's `name` column is a real loadable script path
// only for a minority of placements in any given category -- the rest
// use a "!label"-only convention for generic/decorative instances that
// share their category's appearance but have no unique per-instance
// script/behavior (docs/ZONE_FORMAT.md's category table; confirmed
// separately for category 11/doors and category 2/monsters this
// session). Shared by both the door and monster/NPC branches of the
// zone-load loop below.
bool HasRealScript(const std::string& entityTypeName) {
    return entityTypeName.size() > 2 &&
           entityTypeName.compare(entityTypeName.size() - 2, 2, ".s") == 0;
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

// Combat vertical-slice (docs/PORT_ROADMAP.md): a live, AI-driven
// azra_rat instance -- unlike gameEntities' static props, these move,
// fight, and can die, so they carry their own MonsterExecutable (real
// stats/OnKilled pulled from monsters/Azra_Rat.s, see
// simkin_bindings/monster_executable.h) plus mutable world position and
// AI state, separate from the PlacedEntity list the renderer otherwise
// draws untouched every frame. Idle/Chasing/Attacking is this port's
// own from-scratch AI design -- no real monster script ever calls
// AiAttack/AiPursue itself (only AiDetect(), confirmed by grepping the
// whole monsters/*.s corpus), so there's no real state machine to
// match, just parameters (chaseRadius etc.) to honor.
struct MonsterInstance {
    std::unique_ptr<sk_bindings::MonsterExecutable> script;
    float x = 0, y = 0, z = 0;
    int modelArchiveIndex = -1;
    enum class AiState { Idle, Chasing, Attacking } aiState = AiState::Idle;
    int attackCooldownTicks = 0;
};

// M15: Action::Use interact binding, first (narrow) slice -- doors only,
// same "one real category, not the whole native surface" precedent M12
// set for combat (docs/PORT_ROADMAP.md's "generic Action::Use interact
// binding (doors, pickups, NPC talk -- still unbound)" open item; pickups
// and NPC talk stay unbound, see the zone-load block below). Like
// MonsterInstance, a live door carries its own DoorExecutable (a real
// door.s/door02.s script's Init() actually runs) plus mutable world
// state (yaw, for the open/close swing) separate from gameEntities'
// untouched static-prop list.
struct DoorInstance {
    std::unique_ptr<sk_bindings::DoorExecutable> script;
    float x = 0, y = 0, z = 0;
    int modelArchiveIndex = -1;
};

// M19: Action::Use interact binding's last remaining slice -- pickups.
// Same "one real category" precedent (docs/PORT_ROADMAP.md) -- category 3
// (misc loot) real .s scripts like snowline/foxglove.s, not the broader
// loot-menu/container pattern (category 8, still unbound -- see
// item_executable.h's class comment). Like DoorInstance/MonsterInstance,
// a live world pickup carries its own real ItemExecutable (Init() genuinely
// runs), separate from gameEntities' untouched static-prop list -- but
// unlike a door/monster, a pickup's script instance doesn't live for the
// zone's whole lifetime: a successful real OnUse() (PickupItem(self) +
// MirrorDestroyObject(self)) moves script's ownership into the player's
// inventory and erases this entry (see the Action::Use handling below).
struct PickupInstance {
    std::unique_ptr<sk_bindings::ItemExecutable> script;
    float x = 0, y = 0, z = 0;
    int modelArchiveIndex = -1;
};

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
                 sk_bindings::PlayerExecutable& player, const sk::StringTable& strings,
                 sk::SpriteArchive& sprites) {
    using RowKind = sk_bindings::MenuExecutable::RowKind;
    // Real background (docs/GRAPHICS_FORMAT.md) when the real script's
    // MenuBackground(id) call resolved to a decodable global.spr slot
    // (id *is* the slot index directly, no translation -- confirmed
    // against every real MenuBackground() call in the corpus); falls
    // back to the flat placeholder color otherwise (asset missing, or a
    // menu that never calls MenuBackground() at all).
    const sk::Sprite* background =
        menu.backgroundId() >= 0 ? sprites.GetSprite(menu.backgroundId()) : nullptr;
    if (background) {
        backbuffer.Blit(0, 0, *background);
    } else {
        backbuffer.Fill(kBackgroundColor);
    }

    // Slot 69 (the main menu's background, MenuBackground(69) in
    // mainmenu.s) has "The Elder Scrolls Travels / SHADOWKEY" logo art
    // baked into its top ~50px -- unlike every other menu background
    // (20, 174), which are plain parchment. The real game's item list
    // starts below it; nothing in mainmenu.s sets this explicitly (no
    // native y-offset call exists), so this is a fixed native constant
    // measured against a real screenshot, not a decompiled value.
    int y = menu.backgroundId() == 69 ? 50 : 8;
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

        switch (row.kind) {
            case RowKind::MenuItem:
            case RowKind::StaticItem: {
                // Real menu item rows (mainmenu.s etc.) are centered on
                // screen -- confirmed against a real screenshot, which
                // also shows no left-margin arrow glyph on the selected
                // row (selection is color-only, kSelectedTextColor).
                std::string label = RowText(row.textId, row.literalText, strings);
                int textW = sk::BitmapFont::TextWidth(label);
                int textX = (sk::Backbuffer::kWidth - textW) / 2;
                sk::BitmapFont::DrawString(backbuffer, textX, y, label, color);
                y += lineHeight;
                break;
            }
            case RowKind::ItemButton: {
                auto* item = static_cast<sk_bindings::ItemButtonExecutable*>(row.widget.get());
                if (item->visible()) {
                    // Real icon (docs/GRAPHICS_FORMAT.md) when the
                    // real ItemExecutable::SetIcon() id resolved to a
                    // decoded, row-sized slot -- global.spr's icon ids
                    // aren't all small row icons (some of the same
                    // ids item scripts use are full-screen 176x208
                    // panels, presumably for a detail/examine view
                    // this port doesn't have), so only draw ones that
                    // actually fit a list row; anything bigger falls
                    // back to the label-only rendering below, same as
                    // a missing/undecoded slot.
                    int textX = 12;
                    const sk::Sprite* icon = sprites.GetSprite(item->icon());
                    if (icon && icon->width <= 40 && icon->height <= 40) {
                        backbuffer.Blit(textX, y, *icon);
                        textX += icon->width + 3;
                    }
                    std::string label = RowText(item->textId(), item->itemText(), strings);
                    sk::BitmapFont::DrawString(backbuffer, textX, y, label, color);
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

// M10/M14: the always-on HUD during the 3D view. M10 first placed a
// hand-drawn vitals HUD bottom-left (a real screenshot comparison had
// shown that's where the real bars sit, inside ornate art this port
// couldn't yet draw). M14 (this session) decompiled the real HUD draw
// functions and wires up the real assets docs/GRAPHICS_FORMAT.md's HUD
// section documents in full -- traced by finding the *fixed* (not
// variable-indexed) 384-slot cache addresses the compiler folds a
// compile-time-constant slot index into (engine+0x4460+slot*4 with slot
// a literal collapses to one constant address, e.g. slot 1 ->
// engine+0x4464 -- invisible to a plain "0x4460" text search, which is
// why this took a second pass; shadowkey/ghidra/scripts/
// pyghidra_grep_decompiled.py found the handful of functions using
// those specific fixed addresses).
//
// **Compass banner** (`FUN_1002ba64`, decompiled in full): draws
// global.spr slot 0 (a 322x13 strip repeating "N...E...S...W...", wider
// than the screen on purpose) at screen (52,5), showing only a 68px
// window starting at a heading-derived source X offset, then draws slot
// 1 (176x31, dragon-head-flanked frame with a transparent center
// window) on top at (0,0) -- the frame's transparent gap is exactly
// where the scrolled tape shows through. The real source-offset formula
// reads the *high byte* of a 16-bit heading field at player+0xb6 (an
// 8-bit angle, 0-255 across a full turn) as a signed value, wrapped into
// [0,255] and capped at 254. This port has no equivalent 16-bit fixed-
// point heading field (Camera::yaw is a float radian, see camera.h) and
// no real screenshot to confirm which turn direction should scroll the
// tape which way -- HeadingToCompassOffset() below is a best-effort,
// unverified-direction mapping from yaw to that same [0,254] range, not
// a decompiled formula.
//
// **Vitals bar cluster** (`FUN_1002ae88`, decompiled in full,
// corrected post-M14): this -- not `FUN_1002c010` below -- is the real
// always-on vitals HUD. Found via `FUN_1002ae88`'s actual (direct,
// non-vtable) caller `FUN_10029cb0`, which is itself vtable slot +0x1c
// on the same `ScreenModeController` secondary vtable, confirmed called
// unconditionally every tick from `GameTick_UpdateAndPresent`; inside
// it, the gameplay-screen-mode branch (state == 5) calls exactly
// `FUN_1002ae88(); FUN_1002ba64(); FUN_1002bb54();` -- vitals, compass,
// hand icons, together, every frame. `FUN_1002ae88` draws THREE 39x9
// bars (global.spr slots 162=red, 160=blue/white, 161=green, each 39x5)
// at (10,182)/(10,190)/(10,196), each width-clipped to its own
// percentage fill, then ONE shared frame (slot 180, 57x46, dragon-head/
// wing art) on top at (0,162) masking all three. Field-order in the
// underlying stat struct (max fields at +0x24/+0x26/+0x28, matching
// red/green/blue in that address order -- i.e. health/fatigue/magicka,
// not health/magicka/fatigue) plus standard health=red/magicka=blue/
// fatigue=green color convention fixes the three bars as health (top),
// magicka (middle), fatigue (bottom).
//
// A live user-provided screenshot from real gameplay confirms this
// exact 3-bar cluster (docs/GRAPHICS_FORMAT.md's HUD section has the
// verification writeup).
//
// **Open item**: a real screenshot *also* shows a second, different
// vitals widget -- `FUN_1002c010`'s single big bar (global.spr slot 205,
// 79x9 red gradient, at (44,182); slot 206, 94x42 dragon-wing frame, at
// (40,166)) -- in actual play. Exhaustively searched (whole-memory scan
// for both the function's own address and its containing vtable's base
// address as raw 4-byte words): `FUN_1002c010` is referenced exactly
// once anywhere in the program, at its one static vtable slot
// (`ScreenModeController`+0x44) -- there's no second static caller to
// find, meaning the real trigger is a fully dynamic/computed dispatch
// this pass couldn't resolve (not this project's usual "just search
// harder" case). Left unimplemented rather than guessing at a trigger
// condition; a real research lead if this HUD is revisited: whatever
// game state raises it is probably rare/conditional (an enemy
// lock-on/target health bar is the leading guess, given it's a single
// bar with no magicka/fatigue counterpart -- but unconfirmed).
//
// **Equipped-item icons** (`FUN_1002bb54`, decompiled in full): the
// real function draws the left/right hand's equipped item icon at
// (5,5)/(139,5) -- flanking the compass banner in the same top HUD
// row. Ties directly into this port's own PlayerExecutable::leftItem()/
// rightItem() (the real hand-equip system decompiled two sessions ago).
void RenderHud(sk::Backbuffer& backbuffer, const sk_bindings::PlayerExecutable& player,
                sk::SpriteArchive& sprites, float cameraYaw) {
    const sk::Sprite* compassTape = sprites.GetSprite(0);
    const sk::Sprite* compassFrame = sprites.GetSprite(1);
    if (compassTape && compassFrame) {
        constexpr float kTwoPi = 6.28318530718f;
        float turns = cameraYaw / kTwoPi;
        turns -= std::floor(turns);  // wrap to [0,1)
        int offset = static_cast<int>(turns * 255.0f);
        offset = (std::max)(0, (std::min)(253, offset));
        backbuffer.BlitRegion(52, 5, *compassTape, offset, 68);
        backbuffer.Blit(0, 0, *compassFrame);
    }

    auto drawHandIcon = [&](sk_bindings::ItemExecutable* handItem, int x) {
        if (!handItem) return;
        const sk::Sprite* icon = sprites.GetSprite(handItem->icon());
        if (icon && icon->width <= 32 && icon->height <= 32) backbuffer.Blit(x, 5, *icon);
    };
    drawHandIcon(player.leftItem(), 5);
    drawHandIcon(player.rightItem(), 139);

    const sk::Sprite* vitalsFrame = sprites.GetSprite(180);
    auto drawVitalBar = [&](int slot, int y, int value, int maxValue) {
        const sk::Sprite* fill = sprites.GetSprite(slot);
        if (!fill) return false;
        float fraction = maxValue > 0
                              ? (std::max)(0.0f, static_cast<float>(value)) / static_cast<float>(maxValue)
                              : 0.0f;
        int fillWidth = static_cast<int>(fill->width * (std::min)(1.0f, fraction));
        backbuffer.BlitRegion(10, y, *fill, 0, fillWidth);
        return true;
    };
    bool health = drawVitalBar(162, 182, player.health(), player.maxHealth());
    bool magicka = drawVitalBar(160, 190, player.magicka(), player.maxMagicka());
    bool fatigue = drawVitalBar(161, 196, player.fatigue(), player.maxFatigue());
    if (vitalsFrame && (health || magicka || fatigue)) {
        backbuffer.Blit(0, 162, *vitalsFrame);
    } else {
        // Fallback (missing asset): flat bars, same footprint the real
        // cluster occupies.
        constexpr int kBarWidth = 39, kBarHeight = 5, kBarGap = 3;
        int y = 182;
        auto drawFlatBar = [&](int value, int maxValue, uint16_t color) {
            int filled = maxValue > 0 ? (kBarWidth * (std::max)(0, value)) / maxValue : 0;
            filled = (std::min)(filled, kBarWidth);
            for (int dy = 0; dy < kBarHeight; ++dy) {
                for (int dx = 0; dx < kBarWidth; ++dx) {
                    backbuffer.SetPixel(10 + dx, y + dy,
                                         dx < filled ? color : kPopupBorderColor);
                }
            }
            y += kBarHeight + kBarGap;
        };
        drawFlatBar(player.health(), player.maxHealth(), sk::PackRGB565(200, 40, 40));
        drawFlatBar(player.magicka(), player.maxMagicka(), sk::PackRGB565(60, 80, 220));
        drawFlatBar(player.fatigue(), player.maxFatigue(), sk::PackRGB565(60, 180, 80));
    }
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

    // Real menu backgrounds/HUD icons (docs/GRAPHICS_FORMAT.md's "The
    // 384-slot image cache's real source format", docs/PORT_ROADMAP.md's
    // milestone) -- global.spr is the same global archive every menu
    // and zone icon set draws from; menu_sprites.txt prewarms exactly
    // the slots the main-menu/character-creation flow (running before
    // any zone is loaded) needs, including the real MenuBackground()
    // ids (20/69/174, confirmed by grepping every real script's call).
    // Not fatal to fail -- RenderMenu() falls back to a flat color per
    // menu when a slot isn't available, same "optional asset" spirit as
    // modelArchive/entityTypes above.
    sk::SpriteArchive spriteArchive;
    if (spriteArchive.Load(scriptRoot)) {
        spriteArchive.LoadCategory(scriptRoot, "menu");
    }

    // The real N-Gage menu/UI font (bitmap_font.h/.cpp's class
    // comments, docs/GRAPHICS_FORMAT.md). IS the device ROM's own
    // Ceurope.gdr after all -- confirmed by decompiling shadowkey's
    // real text-draw call chain (DrawUIText -> FUN_1008f8a4 ->
    // FUN_10022b20, whose fontNum-gated branch calls genuine
    // EIKCORE::LegendFont() for ordinary menu text), then verifying
    // pixel-for-pixel: downscaling a real screenshot back to native
    // 176x208 (undoing a video capture's ~4.4x upscale) shows every
    // glyph in "New Game" matches Ceurope.gdr's LatinBold12 exactly.
    // An earlier pass in this same session wrongly concluded it was a
    // "Nokia Cellphone FC" TrueType lookalike -- that was comparing
    // against the *upscaled, video-compressed* screenshot, where
    // compression blur on this blocky bitmap font reads as "rounded"
    // to the eye. Like the retail game install, Ceurope.gdr is Nokia
    // device firmware, never committed to this repo (see .gitignore);
    // point argv[2] at wherever it's been extracted to locally.
    // Missing/bad file is non-fatal -- DrawString keeps using the
    // placeholder glyphs, same as every other optional real asset here.
    const char* fontPath = argc > 2 ? argv[2] : "port/assets/fonts/Ceurope.gdr";
    sk::BitmapFont::LoadRealFont(fontPath, "LatinBold12");

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
        "shadowkey-port: '3' (real default Use binding, M15) opens/closes the nearest door "
        "you're facing -- '7'/'5' swing your left/right-hand weapon (M12).\n");
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
    // Combat vertical-slice: live monsters, pulled out of gameEntities'
    // static-prop list at zone load (see below) -- see MonsterInstance's
    // comment.
    std::vector<MonsterInstance> gameMonsters;
    // M15: live doors, pulled out of gameEntities the same way gameMonsters
    // is -- see DoorInstance's comment.
    std::vector<DoorInstance> gameDoors;
    // M19: live world pickups, pulled out of gameEntities the same way --
    // see PickupInstance's comment. Shrinks as items are actually picked
    // up (unlike gameDoors/gameMonsters, which stay fixed-size for a
    // zone's whole lifetime).
    std::vector<PickupInstance> gamePickups;
    sk::Camera gameCamera;
    sk::ZoneRenderer zoneRenderer;
    bool inGame = false;
    // Post-M11: real per-tick vertical physics (gravity/jump/ground-
    // and-ceiling clamp), see the tick loop below for the full writeup --
    // greenfield gameplay design (no RE ground truth exists for the
    // original's actor physics, docs/WORLD_MODEL.md), same spirit as
    // render3d/camera.h's kEyeHeightOffset.
    float gameVelZ = 0.0f;
    bool onGround = true;
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
                // Real per-zone icon set (docs/GRAPHICS_FORMAT.md) --
                // <zone>_sprites.txt includes the same item-icon cluster
                // (ItemExecutable::SetIcon() ids 209-218) every zone
                // ships, since the inventory/equip screens need them
                // available regardless of which zone the player is in.
                spriteArchive.LoadCategory(scriptRoot, stack.requestedZone());
                gameZone = std::move(zone);
                gameCamera.x = static_cast<float>(gameZone->playerStartX);
                gameCamera.y = static_cast<float>(gameZone->playerStartY);
                gameCamera.z = static_cast<float>(gameZone->playerStartZ) + sk::kEyeHeightOffset;
                gameCamera.yaw = 0.0f;
                gameCamera.fovY = 1.2f;
                gameVelZ = 0.0f;
                onGround = true;
                inGame = true;

                // Resolve every placed .ent record to a model archive
                // index via entities.txt -- same two-step chain the real
                // engine walks (docs/ZONE_FORMAT.md), just done eagerly
                // here instead of through the engine+0x6b38 zone-local
                // cache.
                //
                // Combat vertical-slice (M12) + interact binding (M15/
                // M16/M18/M19): category 2 (monster, including named NPCs
                // -- see monster_executable.h's class comment), category 7
                // (merchant, e.g. Gravel_Trothgar -- M18), category 11
                // (door), and category 3 (misc loot/world pickups, e.g.
                // snowline/foxglove.s -- M19) placements whose
                // entities.txt `name` is a real loadable script
                // (HasRealScript() above) are pulled out into
                // gameMonsters/gameDoors/gamePickups instead -- a live
                // MonsterExecutable/DoorExecutable/ItemExecutable actually
                // runs that real script's Init(), same load pattern
                // PlayerExecutable::LoadStartingInventory established for
                // real item scripts. Everything else (including the
                // "!label"-only majority of every category) still goes
                // into gameEntities as a static, unanimated prop,
                // unchanged.
                gameEntities.clear();
                gameMonsters.clear();
                gameDoors.clear();
                gamePickups.clear();
                // M18: every live object this loop is about to (re)create
                // is stale after this point -- drop any name -> object
                // registrations from the previous zone before repopulating
                // below, so Level.GetEntity() can never return a dangling
                // pointer into a destroyed zone's objects.
                stack.level().ClearEntities();
                for (const sk::Zone::EntPlacement& e : gameZone->entities()) {
                    const sk::EntityTypeDescriptor* desc = entityTypes.Lookup(e.typeId);
                    if (!desc) continue;
                    if (desc->category == 11 && HasRealScript(desc->name)) {
                        std::string relPath = desc->name;
                        std::replace(relPath.begin(), relPath.end(), '\\', '/');
                        std::string fullPath = std::string(scriptRoot) + "/" + relPath;
                        skExecutableContext loadCtxt(&interpreter);
                        try {
                            auto door = std::make_unique<sk_bindings::DoorExecutable>(
                                skString(fullPath.c_str()), loadCtxt, stack.player());
                            skRValueArray args;
                            args.append(skRValue(0));  // placeholder for Init's "(s)" parameter
                            skRValue ret;
                            skExecutableContext callCtxt(&interpreter);
                            door->method(skString("Init"), args, ret, callCtxt);
                            DoorInstance inst;
                            inst.x = static_cast<float>(e.x);
                            inst.y = static_cast<float>(e.y);
                            inst.z = static_cast<float>(e.z);
                            inst.modelArchiveIndex = desc->modelArchiveIndex;
                            inst.script = std::move(door);
                            stack.level().RegisterEntity(e.name, inst.script.get());
                            gameDoors.push_back(std::move(inst));
                        } catch (skParseException& ex) {
                            std::printf("shadowkey-port: PARSE ERROR loading door %s: %s\n",
                                        fullPath.c_str(), ex.toString().ptr());
                        } catch (skRuntimeException& ex) {
                            std::printf("shadowkey-port: RUNTIME ERROR loading door %s: %s\n",
                                        fullPath.c_str(), ex.toString().ptr());
                        }
                        continue;
                    }
                    // M19: category 3 (misc loot -- world pickups like
                    // snowline/foxglove.s), same HasRealScript() gate as
                    // every other category this loop pulls a live object
                    // out for.
                    if (desc->category == 3 && HasRealScript(desc->name)) {
                        std::string relPath = desc->name;
                        std::replace(relPath.begin(), relPath.end(), '\\', '/');
                        std::string fullPath = std::string(scriptRoot) + "/" + relPath;
                        skExecutableContext loadCtxt(&interpreter);
                        try {
                            auto item = std::make_unique<sk_bindings::ItemExecutable>(
                                skString(fullPath.c_str()), loadCtxt, &strings, stack.player());
                            skRValueArray args;
                            args.append(skRValue(0));  // placeholder for Init's "(s)" parameter
                            skRValue ret;
                            skExecutableContext callCtxt(&interpreter);
                            item->method(skString("Init"), args, ret, callCtxt);
                            PickupInstance inst;
                            inst.x = static_cast<float>(e.x);
                            inst.y = static_cast<float>(e.y);
                            inst.z = static_cast<float>(e.z);
                            inst.modelArchiveIndex = desc->modelArchiveIndex;
                            inst.script = std::move(item);
                            gamePickups.push_back(std::move(inst));
                        } catch (skParseException& ex) {
                            std::printf("shadowkey-port: PARSE ERROR loading pickup %s: %s\n",
                                        fullPath.c_str(), ex.toString().ptr());
                        } catch (skRuntimeException& ex) {
                            std::printf("shadowkey-port: RUNTIME ERROR loading pickup %s: %s\n",
                                        fullPath.c_str(), ex.toString().ptr());
                        }
                        continue;
                    }
                    // M18: category 7 (merchant, e.g. monsters/
                    // Gravel_Trothgar.s) generalized in alongside category
                    // 2 -- same MonsterExecutable shape already covers it
                    // fully (Init()'s SetInvulnerable/SetAggressive(false)/
                    // AddProduct(...) calls all soft-fail or are already
                    // handled exactly like an NPC's, see monster_
                    // executable.cpp; OnUse() opens a real conversation menu
                    // the same way M16's NPC dialogue already does).
                    if ((desc->category == 2 || desc->category == 7) && HasRealScript(desc->name)) {
                        std::string relPath = desc->name;
                        std::replace(relPath.begin(), relPath.end(), '\\', '/');
                        std::string fullPath = std::string(scriptRoot) + "/" + relPath;
                        skExecutableContext loadCtxt(&interpreter);
                        try {
                            auto monster = std::make_unique<sk_bindings::MonsterExecutable>(
                                skString(fullPath.c_str()), loadCtxt, &strings, stack.player(), stack);
                            skRValueArray args;
                            args.append(skRValue(0));  // placeholder for Init's "(s)" parameter
                            skRValue ret;
                            skExecutableContext callCtxt(&interpreter);
                            monster->method(skString("Init"), args, ret, callCtxt);
                            MonsterInstance inst;
                            inst.x = static_cast<float>(e.x);
                            inst.y = static_cast<float>(e.y);
                            inst.z = static_cast<float>(e.z);
                            inst.modelArchiveIndex = desc->modelArchiveIndex;
                            inst.script = std::move(monster);
                            stack.level().RegisterEntity(e.name, inst.script.get());
                            gameMonsters.push_back(std::move(inst));
                        } catch (skParseException& ex) {
                            std::printf("shadowkey-port: PARSE ERROR loading monster %s: %s\n",
                                        fullPath.c_str(), ex.toString().ptr());
                        } catch (skRuntimeException& ex) {
                            std::printf("shadowkey-port: RUNTIME ERROR loading monster %s: %s\n",
                                        fullPath.c_str(), ex.toString().ptr());
                        }
                        continue;
                    }
                    gameEntities.push_back({static_cast<float>(e.x), static_cast<float>(e.y),
                                             static_cast<float>(e.z), desc->modelArchiveIndex});
                }
                std::printf("shadowkey-port: %zu live monster(s), %zu live door(s), %zu live "
                            "pickup(s) loaded\n",
                            gameMonsters.size(), gameDoors.size(), gamePickups.size());
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
                // right = (sinYaw, -cosYaw), matching zone_renderer.cpp's
                // own forward/right basis comment -- used for strafing.
                float rx = std::sin(gameCamera.yaw) * kMoveSpeed;
                float ry = -std::cos(gameCamera.yaw) * kMoveSpeed;
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
                // The real default scheme binds these to Key4/Key6
                // (docs/INPUT_HANDLING.md) -- previously decoded but never
                // wired up; Up/Down/Left/Right above stayed on the raw
                // ButtonSlot layer since Left/Right double as turning, not
                // an Action.
                if (input.GetBoundButton(sk::Action::SideStepLeft)) tryMove(-rx, -ry);
                if (input.GetBoundButton(sk::Action::SideStepRight)) tryMove(rx, ry);

                // Post-M11: real gravity/jump/ground-and-ceiling physics,
                // replacing the M6-M11 fixed "camera.z set once at zone
                // load, never touched again" behavior -- the "floating
                // camera clipping through geometry" the port previously
                // had. No RE ground truth exists for the original's actor
                // physics (docs/WORLD_MODEL.md notes fixed-point actor
                // positions/collision exist but were never traced to the
                // byte level), so this is a deliberate, from-scratch
                // gameplay-feel design, same spirit as kEyeHeightOffset:
                // simple Euler integration against the per-tile
                // floor/ceiling heights Zone::FloorHeightAt/
                // CeilingHeightAt now expose (the same corner data
                // render3d/zone_renderer.cpp already draws the floor/
                // ceiling quads from). Grounded state auto-follows
                // slopes/steps of any height (snap-to-floor while
                // velZ<=0) rather than enforcing a max step height --
                // no data on what the original's real ledge/stair
                // behavior was, and CircleHitsWall's own tile-level wall
                // flag (not height) is what stops genuinely impassable
                // terrain.
                constexpr float kGravity = 4.0f;      // world units/tick^2
                constexpr float kJumpSpeed = 50.0f;   // world units/tick, initial upward velocity
                constexpr float kMaxFallSpeed = 200.0f;  // clamp, avoids tunneling through thin
                                                          // floors over one big tick step
                constexpr float kHeadroom = 60.0f;    // world units, eye-to-ceiling clearance
                if (onGround && input.ConsumeBoundJustPressed(sk::Action::Jump)) {
                    gameVelZ = kJumpSpeed;
                    onGround = false;
                }
                gameVelZ -= kGravity;
                if (gameVelZ < -kMaxFallSpeed) gameVelZ = -kMaxFallSpeed;
                gameCamera.z += gameVelZ;

                float floorEyeZ = gameZone->FloorHeightAt(gameCamera.x, gameCamera.y) +
                                   sk::kEyeHeightOffset;
                if (gameVelZ <= 0.0f && gameCamera.z <= floorEyeZ) {
                    gameCamera.z = floorEyeZ;
                    gameVelZ = 0.0f;
                    onGround = true;
                } else {
                    onGround = false;
                }
                float ceilingZ = gameZone->CeilingHeightAt(gameCamera.x, gameCamera.y);
                if (gameCamera.z + kHeadroom > ceilingZ) {
                    gameCamera.z = ceilingZ - kHeadroom;
                    if (gameVelZ > 0.0f) gameVelZ = 0.0f;
                }

                // Combat vertical-slice (docs/PORT_ROADMAP.md): from-
                // scratch AI loop -- Idle -> Chasing once the player
                // enters the monster's real SetChaseRadius(), Chasing ->
                // Attacking once within melee range. Never de-aggroes
                // once past Idle (no real data on leash/return-to-post
                // behavior -- see monster_executable.h's class comment).
                //
                // M16: gated on the real script's own SetAggressive()
                // flag -- gameMonsters now also holds non-hostile NPCs
                // (monster_executable.h's class comment), whose real
                // scripts already call SetAggressive(false) themselves;
                // skipping this whole block for them is the direct,
                // evidenced behavior (an NPC has no SetChaseRadius() call
                // either, so it would default to 0 and never trigger
                // anyway -- this makes the intent explicit rather than
                // relying on that coincidence).
                constexpr float kMeleeRange = 110.0f;      // world units
                constexpr float kMonsterMoveSpeed = 22.0f;  // world units/tick, slower than the
                                                             // player's 40 -- a rat shouldn't
                                                             // outrun a walking player
                constexpr float kMonsterRadius = 40.0f;     // world units, wall-collision only
                constexpr int kAttackCooldownTicks = 25;    // ~1s at the fixed 40ms tick
                for (MonsterInstance& m : gameMonsters) {
                    if (!m.script->alive() || !m.script->aggressive()) continue;
                    float mdx = gameCamera.x - m.x, mdy = gameCamera.y - m.y;
                    float dist = std::sqrt(mdx * mdx + mdy * mdy);
                    if (m.aiState == MonsterInstance::AiState::Idle) {
                        if (dist <= m.script->chaseRadius()) m.aiState = MonsterInstance::AiState::Chasing;
                    }
                    if (m.aiState == MonsterInstance::AiState::Idle) continue;
                    if (dist <= kMeleeRange) {
                        m.aiState = MonsterInstance::AiState::Attacking;
                        if (m.attackCooldownTicks > 0) {
                            --m.attackCooldownTicks;
                        } else {
                            int dmg = sk_bindings::RollDamage(
                                m.script->attack(), stack.player().baseDefense(),
                                stack.player().armorRating(), m.script->damageMin(),
                                m.script->damageMax());
                            stack.player().ApplyDamage(dmg);
                            m.attackCooldownTicks = kAttackCooldownTicks;
                        }
                    } else {
                        m.aiState = MonsterInstance::AiState::Chasing;
                        m.attackCooldownTicks = 0;
                        if (dist > 1.0f) {
                            float step = kMonsterMoveSpeed / dist;
                            float mmx = mdx * step, mmy = mdy * step;
                            float nx = m.x + mmx;
                            if (!gameZone->CircleHitsWall(nx, m.y, kMonsterRadius)) m.x = nx;
                            float ny = m.y + mmy;
                            if (!gameZone->CircleHitsWall(m.x, ny, kMonsterRadius)) m.y = ny;
                        }
                    }
                    m.z = gameZone->FloorHeightAt(m.x, m.y);
                }

                // Player attack -- UseLeftAction/UseRightAction (Key7/
                // Key5, real decoded default bindings, previously unused)
                // swing/fire whichever hand's weapon is equipped (bare-
                // fists 1-3 damage at kMeleeRange if empty) at the nearest
                // alive monster within range and roughly in front of the
                // camera.
                //
                // M20: range is now the real equipped weapon's own
                // SetRange() value (item_executable.h's range() comment --
                // 384 for every real melee weapon, 16384 for every real
                // bow/crossbow/thrown weapon, corpus-verified bimodal) --
                // this is what actually makes a bow/crossbow attack reach
                // farther than a sword; no separate "fire" input exists in
                // the real default control scheme (docs/INPUT_HANDLING.md
                // -- "Shoot"/"Reload" are real logical actions but were
                // never bound), so reusing the same two attack keys with a
                // longer real range is the evidenced design, not a guess.
                // No line-of-sight/wall check (sk_bindings::InAttackRange's
                // own comment) -- a ranged shot can theoretically clip
                // through a thin wall corner at extreme range, a documented
                // simplification, same footing as every other from-scratch
                // combat constant in this port.
                auto tryAttack = [&](sk_bindings::ItemExecutable* handItem) {
                    float range = handItem ? static_cast<float>(handItem->range()) : kMeleeRange;
                    MonsterInstance* target = nullptr;
                    float bestDist = range + 1.0f;
                    for (MonsterInstance& m : gameMonsters) {
                        // M16: invulnerable() (essential quest NPCs, e.g.
                        // Tanyin Aldwyr's real SetInvulnerable(true))
                        // can't be targeted at all -- matches the real
                        // script's own intent, not just a damage-application
                        // no-op (ApplyDamage() already guards this too, but
                        // skipping targeting means the crosshair/prompt line
                        // never shows an NPC as attackable in the first place).
                        if (!m.script->alive() || m.script->invulnerable()) continue;
                        if (!sk_bindings::InAttackRange(gameCamera.x, gameCamera.y, gameCamera.yaw,
                                                         m.x, m.y, range)) {
                            continue;
                        }
                        float ddx = m.x - gameCamera.x, ddy = m.y - gameCamera.y;
                        float dist = std::sqrt(ddx * ddx + ddy * ddy);
                        if (dist < bestDist) {
                            bestDist = dist;
                            target = &m;
                        }
                    }
                    if (!target) return;
                    bool isWeapon = handItem && handItem->itemType() == sk_bindings::kItemTypeWeapon;
                    int dmgMin = isWeapon ? handItem->damageMin() : 1;
                    int dmgMax = isWeapon ? handItem->damageMax() : 3;
                    int dmg = sk_bindings::RollDamage(stack.player().baseAttack(),
                                                       target->script->defense(),
                                                       target->script->armorValue(), dmgMin, dmgMax);
                    target->script->ApplyDamage(dmg);
                    if (!target->script->alive()) target->script->InvokeOnKilled();
                };
                if (input.ConsumeBoundJustPressed(sk::Action::UseLeftAction)) {
                    tryAttack(stack.player().leftItem());
                }
                if (input.ConsumeBoundJustPressed(sk::Action::UseRightAction)) {
                    tryAttack(stack.player().rightItem());
                }

                // M15/M16/M19: Action::Use (Key3, docs/INPUT_HANDLING.md's
                // default scheme) interact binding -- doors (M15), usable
                // NPCs (M16, e.g. Tanyin Aldwyr's real dialogue, see
                // monster_executable.h's class comment), and world pickups
                // (M19, e.g. snowline/foxglove.s -- the last remaining
                // unbound category, docs/PORT_ROADMAP.md). Same nearest-in-
                // range-and-facing-cone targeting tryAttack uses above,
                // reused here (and again below for the on-screen use-text
                // prompt).
                constexpr float kInteractRange = 140.0f;  // world units, slightly past melee range
                auto findNearbyDoor = [&]() -> DoorInstance* {
                    float fwdX = std::cos(gameCamera.yaw), fwdY = std::sin(gameCamera.yaw);
                    DoorInstance* nearest = nullptr;
                    float bestDist = kInteractRange + 1.0f;
                    for (DoorInstance& d : gameDoors) {
                        float ddx = d.x - gameCamera.x, ddy = d.y - gameCamera.y;
                        float dist = std::sqrt(ddx * ddx + ddy * ddy);
                        if (dist > kInteractRange || dist < 1.0f) continue;
                        float facing = (fwdX * ddx + fwdY * ddy) / dist;
                        if (facing < 0.5f) continue;  // ~60 degree forward cone
                        if (dist < bestDist) {
                            bestDist = dist;
                            nearest = &d;
                        }
                    }
                    return nearest;
                };
                // M16: usable() is the real script's own SetUsable(true)
                // (monster_executable.h) -- an aggressive monster never
                // sets it, so this naturally only ever finds NPCs, not
                // hostile creatures the player is fighting.
                auto findNearbyUsableMonster = [&]() -> MonsterInstance* {
                    float fwdX = std::cos(gameCamera.yaw), fwdY = std::sin(gameCamera.yaw);
                    MonsterInstance* nearest = nullptr;
                    float bestDist = kInteractRange + 1.0f;
                    for (MonsterInstance& m : gameMonsters) {
                        if (!m.script->alive() || !m.script->usable()) continue;
                        float ddx = m.x - gameCamera.x, ddy = m.y - gameCamera.y;
                        float dist = std::sqrt(ddx * ddx + ddy * ddy);
                        if (dist > kInteractRange || dist < 1.0f) continue;
                        float facing = (fwdX * ddx + fwdY * ddy) / dist;
                        if (facing < 0.5f) continue;  // ~60 degree forward cone
                        if (dist < bestDist) {
                            bestDist = dist;
                            nearest = &m;
                        }
                    }
                    return nearest;
                };
                // M19: same shape again for pickups.
                auto findNearbyPickup = [&]() -> PickupInstance* {
                    float fwdX = std::cos(gameCamera.yaw), fwdY = std::sin(gameCamera.yaw);
                    PickupInstance* nearest = nullptr;
                    float bestDist = kInteractRange + 1.0f;
                    for (PickupInstance& p : gamePickups) {
                        float ddx = p.x - gameCamera.x, ddy = p.y - gameCamera.y;
                        float dist = std::sqrt(ddx * ddx + ddy * ddy);
                        if (dist > kInteractRange || dist < 1.0f) continue;
                        float facing = (fwdX * ddx + fwdY * ddy) / dist;
                        if (facing < 0.5f) continue;  // ~60 degree forward cone
                        if (dist < bestDist) {
                            bestDist = dist;
                            nearest = &p;
                        }
                    }
                    return nearest;
                };
                // Doors, usable NPCs, and pickups are three separate lists
                // -- pick whichever real placement is actually nearer when
                // more than one is in range at once, same "nearest wins"
                // rule each list already uses internally.
                auto distanceTo = [&](float x, float y) {
                    float dx = x - gameCamera.x, dy = y - gameCamera.y;
                    return std::sqrt(dx * dx + dy * dy);
                };
                if (input.ConsumeBoundJustPressed(sk::Action::Use)) {
                    DoorInstance* door = findNearbyDoor();
                    MonsterInstance* npc = findNearbyUsableMonster();
                    PickupInstance* pickup = findNearbyPickup();
                    // Nearest of the (up to) three candidates wins.
                    float doorDist = door ? distanceTo(door->x, door->y) : kInteractRange + 1.0f;
                    float npcDist = npc ? distanceTo(npc->x, npc->y) : kInteractRange + 1.0f;
                    float pickupDist =
                        pickup ? distanceTo(pickup->x, pickup->y) : kInteractRange + 1.0f;
                    if (pickup && pickupDist <= doorDist && pickupDist <= npcDist) {
                        // M19: real OnUse() (PickupItem(self) +
                        // MirrorDestroyObject(self)) leaves the item marked
                        // for removal from the world; if it also actually
                        // asked to be picked up (TakePendingPickupItem()
                        // matches this exact instance -- a script could in
                        // principle destroy itself without ever granting
                        // the item, though no real corpus script does),
                        // move its real ItemExecutable into the player's
                        // inventory before erasing the world instance.
                        pickup->script->InvokeOnUse();
                        if (pickup->script->markedForRemoval()) {
                            skiExecutable* pending = stack.player().TakePendingPickupItem();
                            if (pending == static_cast<skiExecutable*>(pickup->script.get())) {
                                stack.player().AddItem(std::move(pickup->script));
                            }
                            gamePickups.erase(gamePickups.begin() +
                                               (pickup - gamePickups.data()));
                        }
                    } else if (npc && npcDist <= doorDist) {
                        // A real NPC's OnUse() (e.g. tanyinconvo.s) calls
                        // OpenMenu(...) -- detect that by comparing
                        // currentMenu() before/after (robust to whatever
                        // stale menu happened to be cached from before
                        // NewGame(), since a real conversation always
                        // resolves to a genuinely different MenuExecutable
                        // instance) and switch out of the 3D view into it,
                        // same mechanism the CharacterManager action above
                        // already uses -- RightSelectionKey (Esc) still
                        // returns straight to gameplay from there via
                        // gamePausedForMenu, bypassing tanyinconvo.s's own
                        // (soft-failed, native-only) Quit() handler.
                        sk_bindings::MenuExecutable* before = stack.currentMenu();
                        npc->script->InvokeOnUse();
                        if (stack.currentMenu() != before) {
                            inGame = false;
                            gamePausedForMenu = true;
                        }
                    } else if (door) {
                        door->script->InvokeOnUse();
                    }
                }

                // Per-frame render list: static props (gameEntities,
                // already excludes rats/doors -- see the zone-load block
                // above) plus one PlacedEntity per still-alive monster and
                // one per live door (its accumulated yaw() reflects any
                // real AddRotationTurn() calls OnUse() has made, so an
                // opened door visibly swings); PlacedEntity's shape
                // already covers both, no further renderer changes
                // needed. Dead monsters simply stop appearing here -- no
                // death animation/pose (M8's own "frame 0/skin 0 only"
                // simplification).
                std::vector<sk::PlacedEntity> frameEntities = gameEntities;
                for (const MonsterInstance& m : gameMonsters) {
                    if (m.script->alive()) {
                        frameEntities.push_back({m.x, m.y, m.z, m.modelArchiveIndex});
                    }
                }
                for (const DoorInstance& d : gameDoors) {
                    frameEntities.push_back(
                        {d.x, d.y, d.z, d.modelArchiveIndex, d.script->yawRadians()});
                }
                // M19: still-in-world pickups -- gamePickups shrinks as
                // items are actually picked up (see the Action::Use
                // handling above), so this naturally stops drawing one the
                // instant it's gone.
                for (const PickupInstance& p : gamePickups) {
                    frameEntities.push_back({p.x, p.y, p.z, p.modelArchiveIndex});
                }
                zoneRenderer.Render(backbuffer, *gameZone, gameCamera, frameEntities, &modelArchive);
                RenderHud(backbuffer, stack.player(), spriteArchive, gameCamera.yaw);
                // Minimal combat/interact feedback -- name + HP of
                // whatever *aggressive* monster is currently in the
                // player's actual attack range/facing cone, else the
                // nearer of a usable NPC's real SetUseText() prompt (M16)
                // or a door's (M15) (combat takes priority when both are
                // in range at once; aggressive()==false already keeps an
                // NPC like Tanyin Aldwyr out of this first loop entirely --
                // see monster_executable.h's class comment). Drawn at
                // y=34, below the real compass banner (RenderHud now
                // occupies y=0..31 across the top).
                //
                // M20: "in range" now means whichever hand's real weapon
                // reaches farthest (kMeleeRange for bare fists) -- matches
                // tryAttack's own per-weapon range above, so equipping a
                // bow genuinely shows the HP label from farther away too,
                // not just landing the hit.
                float playerAttackRange = kMeleeRange;
                if (stack.player().leftItem()) {
                    playerAttackRange =
                        (std::max)(playerAttackRange,
                                   static_cast<float>(stack.player().leftItem()->range()));
                }
                if (stack.player().rightItem()) {
                    playerAttackRange =
                        (std::max)(playerAttackRange,
                                   static_cast<float>(stack.player().rightItem()->range()));
                }
                const MonsterInstance* facingMonster = nullptr;
                for (const MonsterInstance& m : gameMonsters) {
                    if (!m.script->alive() || !m.script->aggressive()) continue;
                    if (!sk_bindings::InAttackRange(gameCamera.x, gameCamera.y, gameCamera.yaw, m.x,
                                                     m.y, playerAttackRange)) {
                        continue;
                    }
                    facingMonster = &m;
                    break;
                }
                if (facingMonster) {
                    std::string label = facingMonster->script->name() + "  " +
                                         std::to_string(facingMonster->script->currentHealth()) + "/" +
                                         std::to_string(facingMonster->script->maxHealth());
                    sk::BitmapFont::DrawString(backbuffer, 4, 34, label, kSelectedTextColor);
                } else {
                    DoorInstance* door = findNearbyDoor();
                    MonsterInstance* npc = findNearbyUsableMonster();
                    PickupInstance* pickup = findNearbyPickup();
                    float doorDist = door ? distanceTo(door->x, door->y) : kInteractRange + 1.0f;
                    float npcDist = npc ? distanceTo(npc->x, npc->y) : kInteractRange + 1.0f;
                    float pickupDist =
                        pickup ? distanceTo(pickup->x, pickup->y) : kInteractRange + 1.0f;
                    int useTextId = -1;
                    if (pickup && pickupDist <= doorDist && pickupDist <= npcDist) {
                        useTextId = pickup->script->useTextId();
                    } else if (npc && npcDist <= doorDist) {
                        useTextId = npc->script->useTextId();
                    } else if (door) {
                        useTextId = door->script->useTextId();
                    }
                    if (useTextId >= 0) {
                        sk::BitmapFont::DrawString(backbuffer, 4, 34, strings.Get(useTextId),
                                                    kSelectedTextColor);
                    }
                }
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
            RenderHud(backbuffer, stack.player(), spriteArchive, gameCamera.yaw);
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
            RenderMenu(backbuffer, *menu, stack.player(), strings, spriteArchive);
        } else {
            backbuffer.Fill(kBackgroundColor);
        }
        window.Present(backbuffer);
    });

    return 0;
}
