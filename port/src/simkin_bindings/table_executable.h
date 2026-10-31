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
// products.dat armour slot) and draws global.spr sprite 25 (a plus) for
// "better" or 24 (a minus) for "worse" -- the two ids `buysell.s` assigns to
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
    // M88: cell+0x2c, the display-flags word that is `FUN_1008df60`'s
    // fifth argument. Every store-screen cell passes 0; the quest log
    // passes 6 for a quest's title row and 0 for its objective row. The
    // renderer reads exactly one bit of it -- `FUN_1008e4b0` hands
    // `(flags >> 1) & 1` to `FUN_1007f49c`, which switches from the
    // shadowed left-aligned draw (`FUN_1008f8a4` twice, once offset by
    // one pixel for the drop shadow) to `FUN_1008f97c`, whose signature
    // has no x at all: a centred line. Bit 2 of the 6 is never read
    // anywhere, so "centred" is the whole observable meaning.
    unsigned int displayFlags = 0;
    // M88: cell+0x34. Set on a row's *second* cell by both screens that
    // build multi-cell rows -- the store's "GP : 111 Qty: 3" line under a
    // product name, and the quest log's blank spacer under a quest -- and
    // cleared on every cell that carries the row's subject. Recorded for
    // the same reason the other four flag bytes are: it is part of the
    // real cell, and a continuation cell is not a thing to select.
    bool continuation = false;
};

// M124: the text of a `statLine` cell (`cell+0x43`), built at draw time by
// FUN_100a0cec from the item it points at -- the inventory's second line
// under each name:
//
//   Weapon      "%s %d-%d"  string 3045 "Attack", +0x1cc/+0x1ce
//                           (SetDamageMin / SetDamageMax)
//   Armor       "%s %d"     string 2991 "Armor",  +0x1cc (SetArmorValue)
//   Consumable  "%s %d"     string 3040 "Qty",    +0x1c4 (the stack count)
//   Spell       "%s %d"     string 3068 "Level",  a scroll's own SetLevel
//                           (+0x1d0 when +0x1d4) or else the player's
//                           level (stats+0x34)
//
// Any other type draws nothing (empty string). The four formats are read
// out of the binary at the addresses the branch loads them from.
std::string StatLineText(const ItemExecutable& item, const sk::StringTable* strings,
                         int playerLevel);

// cell+0x2c's only meaningful bit, as `FUN_1008e4b0` reads it.
constexpr unsigned int kCellFlagCentered = 0x2;

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
    // M124: AddColumn's width (`column+0`), and the character limit
    // FUN_1008e5e0 derives from it (`column+4`): `(width >> 3) + 4`, never
    // under 8. A cell whose text is at least that long is wrapped.
    int columnWidth(int col) const {
        return col >= 0 && col < columnCount() ? m_ColumnWidths[static_cast<size_t>(col)] : 0;
    }
    int columnCharLimit(int col) const {
        const int limit = (columnWidth(col) >> 3) + 4;
        return limit < 8 ? 8 : limit;
    }
    // M124: SetCellSpacing (table case 1), `table+0xe8` -- the gap the
    // draw adds after every column's width. statsscreen.s is the one
    // shipped caller (10).
    int cellSpacing() const { return m_CellSpacing; }

    // What a `comparison` cell resolves to -- FUN_100a0cec's own test,
    // reproduced. The two 10px columns of the store table hold no text;
    // they draw one of two global.spr sprites, or nothing, depending on
    // how the row's subject compares to what the player already has in
    // that hand (or, for armour, in that products.dat armour slot).
    enum class CellCompare {
        None,    // nothing to compare against -- draws nothing
        Equal,   // same rating as what is equipped
        Better,  // sprite 25 (a plus)
        Worse,   // sprite 24 (a minus)
    };
    CellCompare CompareCell(int row, int col) const;

    // M118: the other half of FUN_100a0cec -- the colour it gives a name
    // cell (cell+0x42 set, cell+0x40 clear, and a products.dat record at
    // cell+0x38). In the engine's order:
    //
    //   * with an item (Sell/Inventory): `FUN_100453e0` -- it is in either
    //     hand or one of the eight worn-equipment slots -- or
    //     `FUN_10045520` -- it is in either hand's quick queue -- draws it
    //     in 0x00a, the "equipped" colour;
    //   * with no item (Buy): `FUN_10045474` -- something of this typeId
    //     is held or worn -- draws it in 0x00a too;
    //   * otherwise it is 0xc33 when the class may not use it and the
    //     palette's standard colour when it may.
    //
    // "May not use" is `(no item || !FUN_100450a0(player, item)) &&
    // !product.classFlags[class]`. `FUN_100450a0` walks a per-class
    // proficiency table this port does not have; the port already answers
    // IsItemEnabledFor with "yes" for any owned item for the same reason,
    // so here an owned item is never barred and only the Buy page can
    // show 0xc33.
    //
    // A cell outside that arm (a plain grid cell, a stat line, a
    // comparison column) is `Plain`: drawn in the standard colour by
    // `FUN_1008e4b0`.
    enum class NameTint { Plain, Standard, Equipped, ClassBarred };
    NameTint CellTint(int row, int col) const;

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
    // AddTable(rows, x, y, w, h)'s own box. Only the two sizes needed a
    // reader before M88; the quest log is the first screen whose rows are
    // taller than one line, so the renderer has to know both how wide a
    // line may run and where the table's box ends.
    int width() const { return m_W; }
    // M124: `widget+0x78` / `+0x7c`, AddTable's x and y -- what a popup's
    // SetToWidget(table, dx) positions itself against.
    int x() const { return m_X; }
    int y() const { return m_Y; }
    int height() const { return m_H; }
    // M88: SetLineWrap(true) -- real, and the quest log is the only
    // shipped caller that passes true (`inventory.s`/`buysell.s` never
    // call it at all, `statsscreen.s` builds its own aligned columns).
    // A wrapped table's rows are drawn as however many lines the text
    // needs inside `width()` rather than one clipped line.
    bool lineWrap() const { return m_LineWrap; }

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
    int m_CellSpacing = 0;  // M124, table+0xe8
    bool m_LineWrap = false;
};

}  // namespace sk_bindings
