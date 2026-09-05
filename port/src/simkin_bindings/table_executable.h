#pragma once

// M10: native binding for objects returned by AddTable() -- the grid
// widget real screens use two different ways: statsscreen.s/questlog.s
// direct-write a fixed text grid via SetText(row,col,text), while
// inventory.s populates it from a real item list and reads back whichever
// row the player has highlighted via GetSelectedRow(). Both usages share
// one row-indexed backing store here rather than two separate widget
// classes -- a "grid" row (SetText'd, no associated item) and an
// "inventory" row (host-populated, ItemExecutable-backed) are the same
// Row struct, just with associatedItem left null for the former.
//
// AddTable(rowCount, x, y, w, h)'s first argument is consistently either
// the real row count a screen is about to fill via SetText (statsscreen.s:
// 15, matching its 15 SetText(0..14,...) calls exactly) or a small
// scroll/layout hint (inventory.s: 1, despite the real inventory holding
// many more items) -- treated here as an initial-capacity hint only, not
// an enforced cap, since the inventory case needs to hold an arbitrary
// real item count.
//
// ## M60: cells are objects, and the store screen is why
//
// A row was a `vector<string>` plus one item pointer. That was enough for
// the inventory screen and not remotely enough for the store, so it is now
// what the engine actually builds: a grid of **cells**, each 0x44 bytes,
// each carrying its own subject and its own four display flags. Every one
// of them is set by name in `FUN_10032f78`:
//
//     cell+0x38  the product record (Buy) or the item's icon id
//     cell+0x3c  the ItemExecutable (Sell/Inventory), null in Buy mode
//     cell+0x40  draw a comparison arrow instead of text
//     cell+0x41  ...against the left hand rather than the right
//     cell+0x42  tint the text by whether the player's class may use it
//                (**the constructor sets this to 1**, and only the three
//                non-name columns clear it -- so it is the name column's
//                behaviour by default, not by assignment)
//     cell+0x43  draw the item's derived stat line (damage / AV / cost)
//
// The four columns `buysell.s` declares -- `//product`, `//cost`,
// `//rating left`, `//rating right` -- are therefore: the name, a
// "GP : 111 Qty: 3" line, and two 10px columns that are **not** a star
// rating. `FUN_100a0cec`'s renderer compares the product's own rating
// against whatever the player has in that hand (or, for Armor, in that
// products.dat armour slot) and draws global.spr sprite 24 for "better"
// or 25 for "worse" -- the two ids `buysell.s` assigns to
// `image_item_active`/`image_item_dormant` at the top of OnDisplay and
// then never uses. A Consumable gets no arrow at all (the renderer
// returns early on category 4), which is why the potions page shows two
// blank columns.
//
// ## The cell is its own SimKin class, and it is not in the trie
//
// `SelectedItem(cell)` receives one of these, and `cell.GetItemText()`,
// `cell.GetAssociatedObject()`, `cell.IsItemEnabledFor()` etc. dispatch
// through `FUN_100a4ce4` -- a plain `wcscmp` chain over eight names, none
// of which appear in the 703-entry enumeration. This is the third such
// class found (after the merchant natives and `SetGoldText`; see
// docs/SIMKIN_NATIVE_API.md), and the largest: `GetItemType`,
// `IsInventoryEquipped`, `IsItemEnabledFor`, `GetItemDescription`,
// `DropItem`, `GetAssociatedObject`, `GetArmorText`, `GetItemText`.

#include <string>
#include <vector>

#include "simkin_bindings/native_stub_executable.h"

namespace sk {
struct ProductRecord;
class StringTable;
}  // namespace sk

namespace sk_bindings {

class ItemExecutable;
class MenuExecutable;
class TableExecutable;

// One cell of the grid -- the engine's 0x44-byte object, reduced to the
// seven fields anything actually reads. See the header comment for where
// each one comes from.
struct TableCell {
    std::string text;                              // cell+0x20/+0x28
    const sk::ProductRecord* product = nullptr;    // cell+0x38, Buy mode
    ItemExecutable* item = nullptr;                // cell+0x3c
    bool comparison = false;                       // cell+0x40
    bool compareLeftHand = false;                  // cell+0x41
    bool classTinted = true;                       // cell+0x42, ctor default 1
    bool statLine = false;                         // cell+0x43
};

// Returned by TableExecutable::GetSelectedRow(), and handed to the table's
// SetCallback() handler as its `cell` argument -- inventory.s's
// "activeInventoryItem" and buysell.s's "selectedItem" are both this.
// Addresses the selected row's **column 0** cell, which is the one the
// engine's row-select mode passes along and the only one carrying the
// name text `BuyItem(itemText, n)` looks the product up by.
class TableRowHandle : public NativeStubExecutable {
public:
    TableRowHandle(TableExecutable& table, size_t rowIndex)
        : NativeStubExecutable("TableCell"), m_Table(table), m_RowIndex(rowIndex) {}

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;
    bool equals(const skiExecutable* other) const override;

    size_t rowIndex() const { return m_RowIndex; }

private:
    TableExecutable& m_Table;
    size_t m_RowIndex;
};

class TableExecutable : public NativeStubExecutable {
public:
    TableExecutable(MenuExecutable& owner, int rowCountHint, int x, int y, int w, int h);

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

    // Host-driven navigation (main.cpp), only meaningful while this
    // table's owning row is the outer menu's current selection -- mirrors
    // MenuExecutable::MoveSelection/ActivateSelected but over this
    // table's own rows.
    void MoveSelection(int delta);
    void ActivateSelected();  // fires SetCallback()'s handler with the cell

    // The **used** row count (table+0xe4), not the allocated one
    // (table+0xc8). FUN_10032f78 grows the allocation to fit and then sets
    // this to however many rows the current category filled, so rows left
    // over from a longer page are still allocated, still hold their old
    // cells, and are simply not shown. `GetMaxRows()` answers this.
    int rowCount() const { return m_VisibleRows; }
    int allocatedRows() const { return static_cast<int>(m_Rows.size()); }
    int selectedRow() const { return m_SelectedRow; }  // 0-based, -1 = none
    // M35: used when focus enters the table from outside, so arriving from
    // above lands on the first row and from below on the last -- see
    // MenuExecutable::NavigateDirectional(). Clamped; a no-op on an empty
    // table.
    void SetSelectedRow(int row) {
        if (m_VisibleRows <= 0) return;
        m_SelectedRow = row < 0 ? 0 : (row >= m_VisibleRows ? m_VisibleRows - 1 : row);
    }
    // Rendering data for main.cpp: resolved display text for (row, col),
    // "" if out of range.
    std::string CellText(int row, int col) const;
    int columnCount() const { return static_cast<int>(m_ColumnWidths.size()); }

    // What a `comparison` cell resolves to -- FUN_100a0cec's own test,
    // reproduced. The two 10px columns of the store table hold no text;
    // they draw one of two global.spr sprites, or nothing, depending on
    // how the row's subject compares to what the player already has in
    // that hand (or, for armour, in that products.dat armour slot).
    enum class CellCompare {
        None,    // nothing to compare against -- draws nothing
        Equal,   // same rating as what is equipped
        Better,  // sprite 24
        Worse,   // sprite 25
    };
    CellCompare CompareCell(int row, int col) const;

    // FUN_1008df60: fetch cell (row, col), creating it (and any rows or
    // columns before it) if needed, and set its text. The single way
    // FUN_10032f78 writes the grid.
    TableCell& CellAt(size_t row, size_t col);
    const TableCell* PeekCell(size_t row, size_t col) const;

    // FUN_1008e3d8: destroy every cell but keep the row containers, and
    // reset the used-row count to 0.
    void ClearCells();
    // FUN_1008e43c: append `n` empty row containers.
    void AddRows(int n);
    // FUN_1008e1f8: set the used-row count.
    void SetUsedRows(int n);

    ItemExecutable* AssociatedItem(size_t rowIndex) const;
    // Removes row `rowIndex` from this table only (does not touch the
    // player's inventory) -- used by RemoveRow()/DropRow() below.
    void RemoveRowAt(size_t rowIndex);
    // TableRowHandle::DropItem()'s real implementation: removes the item
    // from the owning menu's player inventory, then removes this row.
    void DropRow(size_t rowIndex);

    MenuExecutable& owner() const { return m_Owner; }
    int inset() const { return m_Inset; }  // table+0xe2, SetInset()

private:
    struct Row {
        std::vector<TableCell> cells;
    };

    MenuExecutable& m_Owner;
    int m_X, m_Y, m_W, m_H;
    std::vector<int> m_ColumnWidths;
    std::string m_Callback;
    std::vector<Row> m_Rows;
    int m_VisibleRows = 0;
    int m_SelectedRow = -1;
    int m_Inset = 0;
};

}  // namespace sk_bindings
