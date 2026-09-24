// M118 smoke test: the widgets that draw themselves, read off the engine.
//
//   ScreenModeController_ctor  the UI palette at engine+0x28: +8 0x733,
//                              +10 0xddd, +0xc 0x730
//   FUN_1008e974               the combo's draw: arrows 207/208, only
//                              where there is somewhere to go
//   FUN_1008eeb4/FUN_1008f0b8  the combo's left/right: clamp, never wrap
//   FUN_1007f650 / FUN_1007f050 / FUN_1007f61c
//                              AddButton's icon widget: 0x3c square, the
//                              active/dormant art, the page flag
//   FUN_10032aa4               the hand-item button's box, icon and text
//   FUN_100a0cec               the inventory name tint: 0x00a for an item
//                              in a hand, worn, or queued
//
// Part 1  the palette and geometry constants
// Part 2  the class and race combos clamp and fire only on a real move
// Part 3  the inventory tabs: sizes, art and the page flag
// Part 4  the inventory list tints what is equipped, and only that
// Part 5  the character manager's hand-item text area keeps its settings
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "simkin_bindings/button_executable.h"
#include "simkin_bindings/combo_box_executable.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/item_button_executable.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/table_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "world/entity_types.h"

namespace sk_b = sk_bindings;
using Item = sk_b::ItemExecutable;
using RowKind = sk_b::MenuExecutable::RowKind;
using Tint = sk_b::TableExecutable::NameTint;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("  %-84s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    if (!ok) ++g_failures;
}

template <typename T>
T* FindWidget(sk_b::MenuExecutable* menu, RowKind kind) {
    if (!menu) return nullptr;
    for (const auto& row : menu->rows()) {
        if (row.kind == kind) return static_cast<T*>(row.widget.get());
    }
    return nullptr;
}

std::vector<std::pair<const sk_b::MenuExecutable::MenuRow*, sk_b::ButtonExecutable*>> Buttons(
    sk_b::MenuExecutable* menu) {
    std::vector<std::pair<const sk_b::MenuExecutable::MenuRow*, sk_b::ButtonExecutable*>> out;
    if (!menu) return out;
    for (const auto& row : menu->rows()) {
        if (row.isQuitButton) continue;
        if (auto* b = dynamic_cast<sk_b::ButtonExecutable*>(row.widget.get())) {
            out.emplace_back(&row, b);
        }
    }
    return out;
}

constexpr int kWeapons[] = {510, 511, 55};

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string root =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    if (!strings.Load(root + "/stringtable.eng")) {
        std::printf("m118_menu_widgets_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }
    sk::EntityTypeTable entityTypes;
    entityTypes.Load(root);
    skInterpreter interpreter;
    sk_b::MenuStack stack(root, interpreter, &strings);
    stack.level().SetEntityTypes(&entityTypes);
    sk_b::PlayerExecutable& player = stack.player();

    auto call = [&](skiExecutable* obj, const char* name, skRValueArray& args) {
        skRValue ret;
        skExecutableContext ctxt(&interpreter);
        obj->method(skString(name), args, ret, ctxt);
        return ret;
    };

    // ---------------------------------------------------------------
    std::printf("\n== Part 1: the palette and the geometry ==\n");
    Check(sk_b::kUiStandardColor444 == 0x733 && sk_b::kUiHighlightColor444 == 0xddd &&
              sk_b::kUiBoxColor444 == 0x730,
          "ScreenModeController_ctor's +8/+10/+0xc: 0x733, 0xddd, 0x730");
    Check(sk_b::kUiStandardColor444 == sk_b::kMenuItemColor444 &&
              sk_b::kUiHighlightColor444 == sk_b::kSelectedItemColor444,
          "  +8 and +10 are the menu's own item and selected colours");
    Check(sk_b::kComboLeftArrowSlot == 207 && sk_b::kComboRightArrowSlot == 208,
          "the combo's arrows are cache slots 0x479c/0x47a0 -> global.spr 207/208");
    Check(sk_b::kWidgetBoxDX == -5 && sk_b::kWidgetBoxDY == -10 &&
              sk_b::kButtonDefaultSize == 0x3c,
          "a widget's box starts 5 left and 10 above it; a bare button is 0x3c square");
    Check(sk_b::kQuitButtonX == 71 && sk_b::kQuitButtonY == 193,
          "AddQuitButton is a label at (0x47, 0xc1)");
    Check(sk_b::kInventoryEquippedColor444 == 0x00a &&
              sk_b::kInventoryClassBarredColor444 == 0xc33,
          "the inventory tints: equipped 0x00a, class-barred 0xc33");

    // ---------------------------------------------------------------
    std::printf("\n== Part 2: the class and race combos ==\n");
    for (const char* name : {"Menus\\ChooseCharacterMenu", "Menus\\ChooseRaceMenu"}) {
        stack.OpenMenu(name);
        sk_b::MenuExecutable* menu = stack.currentMenu();
        auto* combo = FindWidget<sk_b::ComboBoxExecutable>(menu, RowKind::ComboBox);
        Check(combo != nullptr, std::string(name) + " has its combo");
        if (!combo || !menu) continue;
        Check(combo->x() == 15 && combo->y() == 30 && combo->width() == 140,
              "  AddComboBox(15, 30) with SetWidth(140)");
        const int count = combo->optionCount();
        Check(combo->selection() == 0 && !combo->CycleSelection(-1) && combo->selection() == 0,
              "  on the first option Left goes nowhere (no left arrow is drawn)");
        for (int i = 1; i < count; ++i) combo->CycleSelection(1);
        Check(combo->selection() == count - 1 && !combo->CycleSelection(1) &&
                  combo->selection() == count - 1,
              "  on the last option Right goes nowhere, and does not wrap");
        Check(combo->CycleSelection(-1) && combo->selection() == count - 2,
              "  and Left from there moves one back");
    }

    // ---------------------------------------------------------------
    std::printf("\n== Part 3: the inventory tabs ==\n");
    {
        // A clean set of hands, then three real weapons: one wielded, one
        // queued behind it, and one only carried.
        if (player.rightItem()) player.UpdateEquipStatus(player.rightItem(), false);
        if (player.leftItem()) player.UpdateEquipStatus(player.leftItem(), false);
        player.queue(sk_b::kEquipSlotLeft).Clear();
        player.queue(sk_b::kEquipSlotRight).Clear();
        std::vector<Item*> w;
        for (int typeId : kWeapons) {
            std::unique_ptr<Item> it = stack.level().CreateItem(typeId, false);
            if (!it) continue;
            w.push_back(it.get());
            player.AddItem(std::move(it));
        }

        stack.OpenMenu("inventory");
        sk_b::MenuExecutable* inv = stack.currentMenu();
        auto tabs = Buttons(inv);
        Check(tabs.size() == 5, "inventory.s builds five tab buttons");
        if (tabs.size() == 5) {
            bool sized = true;
            for (auto& [row, b] : tabs) sized = sized && row->w == 0x3c && row->h == 0x3c;
            Check(sized, "  none calls SetWidth/SetHeight, so each keeps the 0x3c default");
            Check(tabs[0].second->spriteActive() == 47 && tabs[1].second->spriteActive() == 49,
                  "  AddButton's 5th argument is the active-page art (+0x9c)");
            Check(tabs[0].second->pageSelected() && !tabs[1].second->pageSelected() &&
                      !tabs[4].second->pageSelected(),
                  "  OnDisplay's DisplayWeaponsPage marks only the weapons tab (+0xb2)");
            Check(tabs[1].second->spriteCurrent() == 50 && tabs[4].second->spriteCurrent() == 52,
                  "  and every other tab shows its 6th argument, the dormant art");

            skRValueArray armorTab;
            armorTab.append(skRValue(static_cast<skiExecutable*>(tabs[1].second), false));
            call(inv, "DisplayArmorMenu", armorTab);
            Check(tabs[1].second->pageSelected() && !tabs[0].second->pageSelected() &&
                      tabs[0].second->spriteCurrent() == 48,
                  "DisplayArmorMenu moves the page flag, and weapons falls back to 48");
            skRValueArray weaponTab;
            weaponTab.append(skRValue(static_cast<skiExecutable*>(tabs[0].second), false));
            call(inv, "DisplayWeaponsPage", weaponTab);
        }

        // -----------------------------------------------------------
        std::printf("\n== Part 4: the equipped highlight ==\n");
        auto* table = FindWidget<sk_b::TableExecutable>(inv, RowKind::Table);
        Check(w.size() == 3 && table != nullptr, "three weapons and the inventory table");
        if (w.size() == 3 && table) {
            // w[0] was wielded on pickup; queue w[1] behind it; leave w[2].
            player.UpdateEquipStatus(w[1], true);
            skRValueArray weaponTab;
            weaponTab.append(skRValue(0));
            call(inv, "DisplayWeaponsPage", weaponTab);
            auto tintOf = [&](const Item* item) {
                for (int r = 0; r < table->rowCount(); ++r) {
                    const sk_b::TableCell* c = table->PeekCell(static_cast<size_t>(r), 0);
                    if (c && c->item == item) return table->CellTint(r, 0);
                }
                return Tint::Plain;
            };
            Check(player.rightItem() == w[0] && tintOf(w[0]) == Tint::Equipped,
                  "the wielded weapon's name is drawn in the equipped colour");
            Check(player.QueueHoldingItem(w[1]) >= 0 && player.rightItem() != w[1] &&
                      tintOf(w[1]) == Tint::Equipped,
                  "  so is one only waiting in the hand's queue (FUN_10045520)");
            Check(player.QueueHoldingItem(w[2]) < 0 && tintOf(w[2]) == Tint::Standard,
                  "  and one merely carried is not");
            const sk_b::TableCell* name = table->PeekCell(0, 0);
            Check(name && name->classTinted,
                  "the name cell keeps the constructor's +0x42 = 1 (it was cleared)");
            Check(table->columnCount() < 2 || table->CellTint(0, 1) == Tint::Plain,
                  "  the stat-line column is never tinted");

            player.UpdateEquipStatus(w[2], true);
            call(inv, "DisplayWeaponsPage", weaponTab);
            Check(tintOf(w[2]) == Tint::Equipped, "equipping the third turns it on");
            player.UpdateEquipStatus(w[2], false);
            call(inv, "DisplayWeaponsPage", weaponTab);
            Check(tintOf(w[2]) == Tint::Standard, "  and unequipping turns it off again");
        }
    }

    // ---------------------------------------------------------------
    std::printf("\n== Part 5: the hand-item buttons ==\n");
    {
        stack.OpenMenu("charactermanager");
        sk_b::MenuExecutable* cm = stack.currentMenu();
        std::vector<sk_b::ItemButtonExecutable*> hands;
        if (cm) {
            for (const auto& row : cm->rows()) {
                if (row.kind != RowKind::ItemButton) continue;
                hands.push_back(static_cast<sk_b::ItemButtonExecutable*>(row.widget.get()));
                Check(row.w == 80 && row.h == 60, "  AddItemButton(.., 80, 60) keeps its box");
            }
        }
        Check(hands.size() == 2, "charactermanager.s builds two hand-item buttons");
        bool widths = !hands.empty();
        for (auto* h : hands) widths = widths && h->textArea().textWidth() == 11;
        Check(widths, "  each text area keeps SetTextWidth(11)");
        const bool trimmed = hands.size() == 2 && player.rightItem() &&
                             hands[1]->textArea().trimText();
        Check(!player.rightItem() || trimmed,
              "  and a held item's text area keeps SetTrimText(true)");
        auto buttons = Buttons(cm);
        bool quests = false;
        for (auto& [row, b] : buttons) {
            if (row->textId == 3773) quests = row->w == 54 && row->h == 13 && b->hAdjust() == 8;
        }
        Check(quests, "the Quests button keeps 54x13 and SetHAdjust(8)");
    }

    std::printf("\nm118_menu_widgets_smoke: %d/%d checks passed\n", g_checks - g_failures,
                g_checks);
    return g_failures == 0 ? 0 : 1;
}
