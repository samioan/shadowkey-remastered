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

#include <string>
#include <vector>

#include "simkin_bindings/native_stub_executable.h"

namespace sk {
class StringTable;
}

namespace sk_bindings {

class ItemExecutable;
class MenuExecutable;
class TableExecutable;

// Returned by TableExecutable::GetSelectedRow() -- inventory.s's
// "activeInventoryItem" object: GetAssociatedObject() is a real
// ItemExecutable&-backed row, IsInventoryEquipped()/IsItemEnabledFor()
// read its state, DropItem() removes it from both the player's inventory
// and this table.
class TableRowHandle : public NativeStubExecutable {
public:
    TableRowHandle(TableExecutable& table, size_t rowIndex)
        : NativeStubExecutable("TableRow"), m_Table(table), m_RowIndex(rowIndex) {}

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

    // Host-only population: clears any existing rows and adds one per
    // item in `items`, in order. Used by MenuExecutable's
    // DisplayWeaponsPage/DisplayArmorMenu/etc. handlers (M10) -- not
    // script-visible, the real per-category filtering lives on the
    // MenuExecutable side (it knows the player's full inventory; this
    // widget just displays whatever subset it's handed).
    void PopulateFromInventory(const std::vector<ItemExecutable*>& items);

    // Host-driven navigation (main.cpp), only meaningful while this
    // table's owning row is the outer menu's current selection -- mirrors
    // MenuExecutable::MoveSelection/ActivateSelected but over this
    // table's own rows.
    void MoveSelection(int delta);
    void ActivateSelected();  // fires SetCallback()'s handler, no args

    int rowCount() const { return static_cast<int>(m_Rows.size()); }
    int selectedRow() const { return m_SelectedRow; }  // 0-based, -1 = none
    // M35: used when focus enters the table from outside, so arriving from
    // above lands on the first row and from below on the last -- see
    // MenuExecutable::NavigateDirectional(). Clamped; a no-op on an empty
    // table.
    void SetSelectedRow(int row) {
        if (m_Rows.empty()) return;
        int count = static_cast<int>(m_Rows.size());
        m_SelectedRow = row < 0 ? 0 : (row >= count ? count - 1 : row);
    }
    // Rendering data for main.cpp: resolved display text for (row, col),
    // "" if out of range. Column 0 of an item row is the item's own name
    // (ItemExecutable::name()); grid-mode rows use whatever SetText() put
    // there.
    std::string CellText(int row, int col) const;
    int columnCount() const { return static_cast<int>(m_ColumnWidths.size()); }

    ItemExecutable* AssociatedItem(size_t rowIndex) const;
    // Removes row `rowIndex` from this table only (does not touch the
    // player's inventory) -- used by RemoveRow()/DropRow() below.
    void RemoveRowAt(size_t rowIndex);
    // TableRowHandle::DropItem()'s real implementation: removes the item
    // from the owning menu's player inventory, then removes this row.
    void DropRow(size_t rowIndex);

private:
    struct Row {
        std::vector<std::string> cells;
        ItemExecutable* associatedItem = nullptr;  // null for a plain grid row
    };
    Row& RowAt(size_t index);  // grows m_Rows if needed

    MenuExecutable& m_Owner;
    int m_X, m_Y, m_W, m_H;
    std::vector<int> m_ColumnWidths;
    std::string m_Callback;
    std::vector<Row> m_Rows;
    int m_SelectedRow = -1;
};

}  // namespace sk_bindings
