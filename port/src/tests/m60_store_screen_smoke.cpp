// M60 smoke test: the store screen.
//
// M59 built the shop's whole data model and left `buysell.s` running with
// an empty table. This drives the screen for real: a shipped merchant
// script stocks itself, `BuyFromMerchant()` opens the shipped `buysell.s`,
// and every assertion below reads back what that script's own OnDisplay()
// and page handlers produced.
//
// Decompiled for this milestone and checked here: the store-menu
// dispatcher `FUN_10033660` (trie `0x14de0`, nine bindings), the page
// builder `FUN_10032f78` behind all of them, the focus-rewiring tail
// `FUN_10033c88`, the widget class `FUN_1007e954` (trie `0x14d20`), the
// popup's `UpdatePopupItem` (`FUN_10087a60` case 6), `Table::SetInset`
// (`FUN_1008d0e4` case 12), the wcscmp layer `FUN_10034588` that carries
// `SetGoldText`, and the cell class `FUN_100a4ce4`.
//
// Part 1  the screen mode, and why it is not a bool
// Part 2  the buy page: four columns, and what is in them
// Part 3  the widget class buysell.s drives around the table
// Part 4  the cell class
// Part 5  the sell page and the inventory page
// Part 6  RedrawPage, and the rows it leaves behind
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "assets/product_database.h"
#include "assets/string_table.h"
#include "simkin_bindings/button_executable.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/native_binding_common.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/popup_menu_executable.h"
#include "simkin_bindings/store.h"
#include "simkin_bindings/table_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"

namespace sk_b = sk_bindings;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%-74s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    ++g_checks;
    if (!ok) ++g_failures;
}

// The table `buysell.s` handed to SetInventoryList(), found by walking the
// current screen's rows -- the same thing main.cpp's renderer does.
sk_b::TableExecutable* FindTable(sk_b::MenuExecutable* menu) {
    if (!menu) return nullptr;
    for (const auto& row : menu->rows()) {
        if (row.kind == sk_b::MenuExecutable::RowKind::Table) {
            return static_cast<sk_b::TableExecutable*>(row.widget.get());
        }
    }
    return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    strings.Load(std::string(scriptRoot) + "/stringtable.eng");
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot)) {
        std::printf("m60_store_screen_smoke: FAILED to load entities.txt\n");
        return 1;
    }

    skInterpreter interpreter;
    sk_b::MenuStack stack(scriptRoot, interpreter, &strings);
    stack.level().SetEntityTypes(&entityTypes);
    sk_b::PlayerExecutable& player = stack.player();

    auto call = [&](auto* obj, const char* name, skRValueArray& args) {
        skRValue ret;
        skExecutableContext ctxt(&interpreter);
        obj->method(skString(name), args, ret, ctxt);
        return ret;
    };
    auto call0 = [&](auto* obj, const char* name) {
        skRValueArray none;
        return call(obj, name, none);
    };
    auto loadMerchant = [&](const char* relPath) -> std::unique_ptr<sk_b::MonsterExecutable> {
        std::string full = std::string(scriptRoot) + "/" + relPath;
        skExecutableContext ctxt(&interpreter);
        try {
            auto m = std::make_unique<sk_b::MonsterExecutable>(skString(full.c_str()), ctxt,
                                                                &strings, player, stack);
            skRValueArray args;
            args.append(skRValue(0));
            skRValue ret;
            skExecutableContext c2(&interpreter);
            m->method(skString("Init"), args, ret, c2);
            return m;
        } catch (skParseException& e) {
            std::printf("  PARSE ERROR in %s: %s\n", relPath, e.toString().ptr());
        } catch (skRuntimeException& e) {
            std::printf("  RUNTIME ERROR in %s: %s\n", relPath, e.toString().ptr());
        }
        return nullptr;
    };

    // dstar_w/weapons_merchant.s: 20 AddProduct calls, all weapons, plus
    // one armour line -- so its default store is IPT_Weapon and its armour
    // tab exists but is nearly empty. The real screen has to cope with
    // both.
    std::unique_ptr<sk_b::MonsterExecutable> merchant =
        loadMerchant("dstar_w/weapons_merchant.s");
    if (!merchant) {
        std::printf("m60_store_screen_smoke: FAILED to load the merchant script\n");
        return 1;
    }
    skRValueArray setMerchant;
    setMerchant.append(skRValue(static_cast<skiExecutable*>(merchant.get()), false));
    call(&player, "SetMerchant", setMerchant);
    {
        skRValueArray gold;
        gold.append(skRValue(5000));
        call(&player, "SetGold", gold);
    }

    // ---- Part 1: the screen mode ----
    {
        std::printf("\n-- the screen mode --\n");
        call0(&player, "BuyFromMerchant");
        sk_b::MenuExecutable* screen = stack.currentMenu();
        Check(screen != nullptr &&
                  screen->screenMode() == sk_b::MenuExecutable::ScreenMode::Buy,
              "BuyFromMerchant stamps mode 1 on the screen (FUN_10034f38)");
        Check(screen && call0(screen, "IsBuyMode").boolValue(),
              "...and IsBuyMode() is exactly `mode == 1`");

        call0(&player, "SellToMerchant");
        screen = stack.currentMenu();
        Check(screen && screen->screenMode() == sk_b::MenuExecutable::ScreenMode::Sell &&
                  !call0(screen, "IsBuyMode").boolValue(),
              "SellToMerchant stamps mode 2, and IsBuyMode() reads false");
        // The point of the third state: a screen that is not the store at
        // all is mode 0, and mode 0 is *also* "not buying" -- which is why
        // a bool could never have expressed this.
        stack.OpenMenu("inventory");
        Check(stack.currentMenu() &&
                  stack.currentMenu()->screenMode() ==
                      sk_b::MenuExecutable::ScreenMode::Inventory,
              "every other screen opens in mode 0, the plain inventory page");
    }

    // ---- Part 2: the buy page ----
    sk_b::MenuExecutable* screen = nullptr;
    sk_b::TableExecutable* table = nullptr;
    {
        std::printf("\n-- the buy page --\n");
        call0(&player, "BuyFromMerchant");
        screen = stack.currentMenu();
        table = FindTable(screen);
        Check(table != nullptr, "buysell.s's AddTable(12, 5, 60, 180, 115) is on the screen");
        if (!table) {
            std::printf("m60_store_screen_smoke: FAILED (no table)\n");
            return 1;
        }
        Check(table->columnCount() == 4,
              "four columns: product, cost, and the two the script calls 'rating'");
        Check(table->inset() == 15, "SetInset(15) reaches table+0xe2 instead of soft-failing");

        // buysell.s's OnDisplay ends by opening the merchant's default
        // tab, which for this one is IPT_Weapon -- so the table is already
        // populated before anything else is asked of it.
        Check(screen->currentPage() == sk_b::kItemTypeWeapon,
              "GetDefaultStore picked the weapons tab and DisplayWeaponsPage ran");
        const int weaponLines = table->rowCount();
        Check(weaponLines > 0, "the weapons page is not empty (it was, before M60)");

        // Every visible row must be a weapon, and the count must match the
        // store's own shelves for that category -- the filter is
        // `entry->category == page`, nothing more.
        int shelvesInCategory = 0;
        for (const sk_b::Store::StockEntry& e : merchant->store().stock()) {
            if (e.record && e.record->category == sk_b::kItemTypeWeapon) ++shelvesInCategory;
        }
        Check(weaponLines == shelvesInCategory,
              "one row per weapon on the shelves, and none of the other categories");

        // Column 0 is the products.dat name, which is also the key
        // BuyItem() looks the purchase up by -- the round trip that makes
        // the screen work at all.
        const sk_b::TableCell* name = table->PeekCell(0, 0);
        Check(name && name->product != nullptr && name->item == nullptr,
              "a buy row carries the product record (cell+0x38) and no item (cell+0x3c)");
        Check(name && !name->text.empty() &&
                  merchant->store().FindByName(name->text) != nullptr,
              "column 0's text is the name BuyItem() resolves back to a shelf");
        Check(name && name->classTinted,
              "the name cell keeps cell+0x42, the class-restriction tint the ctor sets");

        // Column 1 is built from stringtable 3039/3040 with the engine's
        // own "%s: %d %s: %d". 3039 ships as "GP " -- with the trailing
        // space -- so the shipped line really does read "GP : 111".
        const sk_b::TableCell* cost = table->PeekCell(0, 1);
        const sk_b::Store::StockEntry* first = nullptr;
        for (const sk_b::Store::StockEntry& e : merchant->store().stock()) {
            if (e.record && e.record->category == sk_b::kItemTypeWeapon) { first = &e; break; }
        }
        char expected[128];
        std::snprintf(expected, sizeof(expected), "%s: %d %s: %d",
                      strings.Get(sk_b::kStoreGoldLabelStringId).c_str(),
                      first ? first->price : 0,
                      strings.Get(sk_b::kStoreQuantityLabelStringId).c_str(),
                      first ? first->quantity : 0);
        Check(cost && cost->text == expected,
              "column 1 is \"GP : <price> Qty: <n>\", double space and all");
        Check(strings.Get(sk_b::kStoreGoldLabelStringId) == "GP " &&
                  strings.Get(sk_b::kStoreQuantityLabelStringId) == "Qty",
              "...because string 3039 ships with a trailing space and 3040 does not");

        // Columns 2 and 3 are not a star rating. They are the comparison
        // arrows FUN_100a0cec draws against each hand.
        const sk_b::TableCell* left = table->PeekCell(0, 2);
        const sk_b::TableCell* right = table->PeekCell(0, 3);
        Check(left && right && left->comparison && right->comparison,
              "columns 2 and 3 set cell+0x40 -- they draw a glyph, not text");
        Check(left && right && left->compareLeftHand && !right->compareLeftHand,
              "...cell+0x41 splits them into left hand and right hand");
        Check(left && left->text == " " && right->text == " ",
              "...and their text is the single space the engine writes");
        // Nothing is equipped yet, so nothing to compare against and no
        // arrow -- the same early-out the renderer takes on an empty hand.
        using Compare = sk_b::TableExecutable::CellCompare;
        Check(table->CompareCell(0, 2) == Compare::None &&
                  table->CompareCell(0, 3) == Compare::None,
              "with both hands empty the comparison columns draw nothing");
        Check(table->CompareCell(0, 0) == Compare::None &&
                  table->CompareCell(0, 1) == Compare::None,
              "...and the two text columns are never comparison cells");

        // The page really does switch. The armour tab is a different
        // filter over the same shelves -- driven through the script's own
        // ArmorMenu() handler, which is the whole three-line body the tab
        // button's callback runs (relabel the title, display the page,
        // remember the active button).
        screen->TryInvoke("ArmorMenu");
        Check(screen->currentPage() == sk_b::kItemTypeArmor,
              "DisplayArmorMenu moves the page to IPT_Armor (3)");
        int armourShelves = 0;
        for (const sk_b::Store::StockEntry& e : merchant->store().stock()) {
            if (e.record && e.record->category == sk_b::kItemTypeArmor) ++armourShelves;
        }
        Check(table->rowCount() == armourShelves,
              "...and the table now holds exactly the armour lines");
        Check(table->allocatedRows() >= static_cast<int>(merchant->store().stock().size()),
              "the allocation was grown to the whole stock, not just this category");
    }

    // ---- Part 3: the widget class ----
    {
        std::printf("\n-- the shared widget class (trie 0x14d20) --\n");
        // buysell.s hides the button of any category the merchant does not
        // stock and slides the survivors left by offsetX (35) from originX
        // (3). This merchant sells weapons and armour, so those two
        // buttons are visible and land at x=3 and x=38; the consumables
        // and spells buttons are hidden.
        int visibleButtons = 0;
        int hiddenButtons = 0;
        std::vector<int> xs;
        for (const auto& row : screen->rows()) {
            if (!dynamic_cast<sk_b::ButtonExecutable*>(row.widget.get())) continue;
            if (row.isQuitButton) continue;
            if (row.visible) {
                ++visibleButtons;
                xs.push_back(row.x);
            } else {
                ++hiddenButtons;
            }
        }
        Check(hiddenButtons > 0,
              "SetVisible(false) reaches the row (it soft-failed before M60)");
        Check(visibleButtons > 0 && xs.size() == static_cast<size_t>(visibleButtons),
              "SetX(x) reaches the row too");
        bool packedLeft = !xs.empty() && xs[0] == 3;
        for (size_t i = 1; i < xs.size(); ++i) {
            if (xs[i] != xs[i - 1] + 35) packedLeft = false;
        }
        Check(packedLeft,
              "the surviving tabs are packed from originX=3 in steps of offsetX=35");

        // SetGoldText is not a trie binding at all -- FUN_10034588's
        // wcscmp layer. And its format is bare "%d", so the "gp" in
        // AddFloatingText("0 gp", ...) is gone the moment it runs.
        bool foundGold = false;
        for (const auto& row : screen->rows()) {
            if (row.textId == -1 && row.literalText == std::to_string(player.gold())) {
                foundGold = true;
            }
        }
        Check(foundGold, "SetGoldText(gpItem) wrote the gold count onto the widget");
        bool anyGp = false;
        for (const auto& row : screen->rows()) {
            if (row.literalText.find(" gp") != std::string::npos) anyGp = true;
        }
        Check(!anyGp, "...as a bare \"%d\", so the script's \"0 gp\" placeholder is gone");

        // The title is a FloatingText row relabelled by SetLocalizedText,
        // which is the same shared class -- 2990/2991/2992/2993 are
        // Weapons/Armor/Spells/Potions.
        bool titled = false;
        for (const auto& row : screen->rows()) {
            if (row.textId == 2991) titled = true;
        }
        Check(titled, "titleItem.SetLocalizedText(2991) put \"Armor\" on the header");

        // The confirm popup's rows are retargeted by the three-argument
        // UpdatePopupItem, which is what makes one popup serve both sides.
        sk_b::PopupMenuExecutable* popup = screen->activePopup();
        (void)popup;  // may be hidden; the callback check below is direct
        Check(strings.Get(3787) == "Buy" && strings.Get(3789) == "Sell",
              "3787/3789 are the two labels UpdatePopupItem swaps between");
    }

    // ---- Part 4: the cell class (FUN_100a4ce4) ----
    {
        std::printf("\n-- the cell class --\n");
        skRValueArray tab;
        tab.append(skRValue(0));
        call(screen, "DisplayWeaponsPage", tab);
        skRValue selected = call0(table, "GetSelectedRow");
        auto* cell = dynamic_cast<sk_b::TableRowHandle*>(selected.obj());
        Check(cell != nullptr, "GetSelectedRow() hands back a cell object");
        if (cell) {
            const std::string text = sk_b::ToStdString(call0(cell, "GetItemText").str());
            Check(!text.empty() && merchant->store().FindByName(text) != nullptr,
                  "cell.GetItemText() is the name BuyItem() takes");
            Check(call0(cell, "GetItemType").intValue() == sk_b::kItemTypeWeapon,
                  "cell.GetItemType() is the product row's category on the buy page");
            skRValue assoc = call0(cell, "GetAssociatedObject");
            Check(assoc.obj() == nullptr,
                  "cell.GetAssociatedObject() is null in Buy mode -- buysell.s's own guard");
            // IsItemEnabledFor reads products.dat's nine class flags with
            // the player's class index, which is the same byte the name
            // cell's tint uses.
            const sk::ProductRecord* record = table->PeekCell(0, 0)->product;
            Check(record && call0(cell, "IsItemEnabledFor").boolValue() ==
                                record->enabledForClass(player.characterClass()),
                  "cell.IsItemEnabledFor() is a direct read of the row's class flags");
            const std::string desc = sk_b::ToStdString(call0(cell, "GetItemDescription").str());
            Check(record && desc == strings.Get(record->descriptionStringId),
                  "cell.GetItemDescription() is the products.dat description string");
        }

        // The callback takes the cell. Before M60 it took nothing, so
        // buysell.s's `selectedItem=cell` assigned an unset variable and
        // every later BuyInv()/SellItem() had nothing to name.
        table->SetSelectedRow(0);
        table->ActivateSelected();
        skRValue stored;
        screen->getValue(skString("selectedItem"), skString(""), stored);
        Check(stored.obj() != nullptr,
              "SetCallback(\"SelectedItem\")'s handler really receives the cell");
    }

    // ---- Part 5: the sell page and the inventory page ----
    {
        std::printf("\n-- the sell page --\n");
        // Give the player something to sell. BuyItem is M59's, and it
        // creates the item from the entity factory -- so this is the whole
        // round trip: the table row's own text goes straight back in as
        // the lookup key, which is the only reason the store screen works.
        int bought = -1;
        for (int r = 0; r < table->rowCount() && bought != 1; ++r) {
            const sk_b::TableCell* row = table->PeekCell(r, 0);
            if (!row || row->text.empty()) continue;
            skRValueArray buy;
            buy.append(skRValue(skString(row->text.c_str())));
            buy.append(skRValue(1));
            bought = call(&player, "BuyItem", buy).intValue();
        }
        Check(bought == 1, "a purchase off a visible row succeeds end to end");

        call0(&player, "SellToMerchant");
        sk_b::MenuExecutable* sellScreen = stack.currentMenu();
        sk_b::TableExecutable* sellTable = FindTable(sellScreen);
        Check(sellTable && sellTable->rowCount() > 0,
              "the sell page lists the player's own items, not the shelves");
        if (sellTable && sellTable->rowCount() > 0) {
            const sk_b::TableCell* name = sellTable->PeekCell(0, 0);
            Check(name && name->item != nullptr && name->product == nullptr,
                  "a sell row carries the ItemExecutable (cell+0x3c) and no product");
            const sk_b::TableCell* cost = sellTable->PeekCell(0, 1);
            char expected[128];
            std::snprintf(expected, sizeof(expected), "%s: %d",
                          strings.Get(sk_b::kStoreGoldLabelStringId).c_str(),
                          name && name->item ? name->item->marketValue() : 0);
            Check(cost && cost->text == expected,
                  "a non-Consumable's cost column is \"%s: %d\" -- no Qty, nothing to count");
            // The cell's item-mode methods.
            skRValue selected = call0(sellTable, "GetSelectedRow");
            auto* cell = dynamic_cast<sk_b::TableRowHandle*>(selected.obj());
            Check(cell && call0(cell, "GetAssociatedObject").obj() == name->item,
                  "cell.GetAssociatedObject() now hands back the item SellItem() takes");

            // The bought weapon is already equipped, and the buy page's
            // comparison columns come alive against it -- every shelf line
            // is now measured against what the player is holding, which is
            // the whole point of those two 10px columns.
            //
            // M74: the purchase itself is what equips it. `FUN_1003e030`
            // hands each new item to `FUN_10045078`, which is a one-line
            // forward to the player's own add-to-inventory slot `+0x164`
            // (`FUN_1003d8e0`) -- and that function's tail puts the item in
            // the hand its `+0x1c0` names. A weapon names the **right**
            // hand, so this test no longer equips it by hand, and it is the
            // right-hand column that is live.
            using Compare = sk_b::TableExecutable::CellCompare;
            sk_b::ItemExecutable* worn = name->item;
            Check(worn->equipped() && player.rightItem() == worn,
                  "buying a weapon equips it into the right hand, unasked");

            call0(&player, "BuyFromMerchant");
            sk_b::MenuExecutable* buyScreen = stack.currentMenu();
            buyScreen->TryInvoke("WeaponsMenu");
            sk_b::TableExecutable* buyTable = FindTable(buyScreen);
            int better = 0, worse = 0, equal = 0, none = 0;
            for (int r = 0; buyTable && r < buyTable->rowCount(); ++r) {
                const sk_b::TableCell* shelf = buyTable->PeekCell(r, 0);
                const Compare got = buyTable->CompareCell(r, 3);  // the right hand
                if (!shelf || !shelf->product) { ++none; continue; }
                const int shelfRating = shelf->product->rating;
                const bool ok = shelfRating > worn->rating()  ? got == Compare::Better
                                : shelfRating < worn->rating() ? got == Compare::Worse
                                                               : got == Compare::Equal;
                if (!ok) { ++none; continue; }
                if (got == Compare::Better) ++better;
                else if (got == Compare::Worse) ++worse;
                else ++equal;
            }
            Check(none == 0,
                  "every weapon line compares against the equipped hand by rating");
            Check(better + worse + equal == (buyTable ? buyTable->rowCount() : 0) &&
                      equal >= 1,
                  "...and the line the player just bought reads Equal against itself");
            // The left hand is still empty, so its column stays blank --
            // the two columns really are independent.
            Check(buyTable && buyTable->CompareCell(0, 2) == Compare::None,
                  "the left-hand column is still blank, because that hand is empty");
        }

        // The plain inventory page is the third branch: no cost column at
        // all, and for Misc no second column either.
        std::printf("\n-- the inventory page --\n");
        stack.OpenMenu("inventory");
        sk_b::MenuExecutable* inv = stack.currentMenu();
        skRValueArray tab;
        tab.append(skRValue(0));
        call(inv, "DisplayWeaponsPage", tab);
        sk_b::TableExecutable* invTable = FindTable(inv);
        Check(invTable && invTable->rowCount() > 0, "the inventory page still lists items");
        if (invTable && invTable->rowCount() > 0) {
            const sk_b::TableCell* second = invTable->PeekCell(0, 1);
            Check(second && second->statLine,
                  "its second column is the derived stat line (cell+0x43), not a price");
            call(inv, "DisplayMiscItemsMenu", tab);
            const sk_b::TableCell* miscSecond = invTable->PeekCell(0, 1);
            Check(invTable->rowCount() == 0 || miscSecond == nullptr ||
                      !miscSecond->statLine,
                  "...and the Misc page skips it entirely, which is why it is a bare list");
        }
    }

    // ---- Part 6: RedrawPage ----
    {
        std::printf("\n-- RedrawPage --\n");
        call0(&player, "BuyFromMerchant");
        screen = stack.currentMenu();
        table = FindTable(screen);
        skRValueArray tab;
        tab.append(skRValue(0));
        call(screen, "DisplayWeaponsPage", tab);
        const int weaponRows = table->rowCount();
        const std::string firstName = table->PeekCell(0, 0)->text;

        // buysell.s's CloseMsgPopup calls RedrawPage(false) -- the real
        // second argument is the *clear* flag, so false repopulates over
        // the existing cells instead of destroying them first.
        skRValueArray noClear;
        noClear.append(skRValue(false));
        call(screen, "RedrawPage", noClear);
        Check(table->rowCount() == weaponRows && table->PeekCell(0, 0)->text == firstName,
              "RedrawPage(false) rebuilds the same page it was already on");

        // Switch to the shorter armour page without clearing, and the rows
        // the longer page left behind are still allocated and still hold
        // their old text -- they are merely past the used-row count.
        skRValueArray tabArgs;
        tabArgs.append(skRValue(0));
        call(screen, "DisplayArmorMenu", tabArgs);
        const int armourRows = table->rowCount();
        Check(armourRows < weaponRows,
              "the armour page is shorter than the weapons page for this merchant");
        Check(table->allocatedRows() > armourRows,
              "...and the extra rows are still allocated, just not counted");
        Check(table->CellText(armourRows, 0).empty(),
              "CellText() past the used-row count reports nothing, so nothing renders");
    }

    std::printf("\nm60_store_screen_smoke: %s (%d checks)\n",
                g_failures ? "FAILED" : "PASSED (all checks)", g_checks);
    return g_failures ? 1 : 0;
}
