#include "simkin_bindings/table_executable.h"

#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/native_binding_common.h"
#include "skRValue.h"
#include "skRValueArray.h"

namespace sk_bindings {

bool TableRowHandle::method(const skString& methodName, skRValueArray& args,
                             skRValue& returnValue, skExecutableContext& context) {
    if (methodName == skString("GetAssociatedObject") && args.entries() == 0) {
        ItemExecutable* item = m_Table.AssociatedItem(m_RowIndex);
        if (item) returnValue = skRValue(static_cast<skiExecutable*>(item), false);
        return true;
    }
    if (methodName == skString("IsInventoryEquipped") && args.entries() == 0) {
        ItemExecutable* item = m_Table.AssociatedItem(m_RowIndex);
        returnValue = skRValue(item && item->equipped());
        return true;
    }
    if (methodName == skString("IsItemEnabledFor") && args.entries() == 0) {
        // No encumbrance/curse/class-restriction system exists in this
        // port -- every owned item is always usable.
        returnValue = skRValue(true);
        return true;
    }
    if (methodName == skString("DropItem") && args.entries() == 0) {
        m_Table.DropRow(m_RowIndex);
        return true;
    }
    return SoftFailNativeCall("TableRow", methodName, args, returnValue);
}

bool TableRowHandle::equals(const skiExecutable* other) const {
    auto* o = dynamic_cast<const TableRowHandle*>(other);
    return o && &o->m_Table == &m_Table && o->m_RowIndex == m_RowIndex;
}

TableExecutable::TableExecutable(MenuExecutable& owner, int rowCountHint, int x, int y, int w,
                                  int h)
    : NativeStubExecutable("Table"), m_Owner(owner), m_X(x), m_Y(y), m_W(w), m_H(h) {
    if (rowCountHint > 0) m_Rows.resize(static_cast<size_t>(rowCountHint));
}

TableExecutable::Row& TableExecutable::RowAt(size_t index) {
    if (index >= m_Rows.size()) m_Rows.resize(index + 1);
    return m_Rows[index];
}

bool TableExecutable::method(const skString& methodName, skRValueArray& args,
                              skRValue& returnValue, skExecutableContext& context) {
    if (methodName == skString("AddColumn") && args.entries() == 1) {
        m_ColumnWidths.push_back(args[0].intValue());
        return true;
    }
    if (methodName == skString("SetCallback") && args.entries() == 1) {
        m_Callback = ToStdString(args[0].str());
        return true;
    }
    if (methodName == skString("SetText") && args.entries() == 3) {
        size_t row = static_cast<size_t>(args[0].intValue());
        size_t col = static_cast<size_t>(args[1].intValue());
        Row& r = RowAt(row);
        if (col >= r.cells.size()) r.cells.resize(col + 1);
        r.cells[col] = ToStdString(args[2].str());
        if (m_SelectedRow < 0) m_SelectedRow = 0;
        return true;
    }
    if (methodName == skString("Clear") && args.entries() == 0) {
        m_Rows.clear();
        m_SelectedRow = -1;
        return true;
    }
    if (methodName == skString("GetSelectedRow") && args.entries() == 0) {
        if (m_SelectedRow >= 0 && static_cast<size_t>(m_SelectedRow) < m_Rows.size()) {
            auto* handle = new TableRowHandle(*this, static_cast<size_t>(m_SelectedRow));
            returnValue = skRValue(static_cast<skiExecutable*>(handle), true);
        }
        return true;
    }
    if (methodName == skString("GetMaxRows") && args.entries() == 0) {
        returnValue = skRValue(rowCount());
        return true;
    }
    if (methodName == skString("RemoveRow") && args.entries() == 1) {
        // inventory.s's UseItem() calls this directly (not through
        // TableRowHandle::DropItem()) right before consuming the item --
        // same real intent (the item leaves the player's inventory too,
        // not just this table's display), so this shares DropRow()'s
        // implementation.
        if (auto* handle = dynamic_cast<TableRowHandle*>(args[0].obj())) {
            DropRow(handle->rowIndex());
        }
        return true;
    }
    if (methodName == skString("SetRowSelect") || methodName == skString("AutoWrapCells") ||
        methodName == skString("SetLineWrap") || methodName == skString("SetCellSpacing") ||
        methodName == skString("SetSelectable")) {
        // Cosmetic/behavioral flags this port's simplified single-column-
        // of-rows table model doesn't need to branch on -- every table is
        // already row-selectable with wrapped-if-needed text. Accepted so
        // real screens that call them don't soft-fail-log noise.
        return true;
    }
    return SoftFailNativeCall("Table", methodName, args, returnValue);
}

void TableExecutable::PopulateFromInventory(const std::vector<ItemExecutable*>& items) {
    m_Rows.clear();
    for (ItemExecutable* item : items) {
        Row row;
        row.associatedItem = item;
        row.cells.push_back(item->name());
        m_Rows.push_back(std::move(row));
    }
    m_SelectedRow = m_Rows.empty() ? -1 : 0;
}

void TableExecutable::MoveSelection(int delta) {
    if (m_Rows.empty()) return;
    int count = static_cast<int>(m_Rows.size());
    m_SelectedRow = ((m_SelectedRow < 0 ? 0 : m_SelectedRow) + delta % count + count) % count;
}

void TableExecutable::ActivateSelected() { m_Owner.TryInvoke(m_Callback); }

std::string TableExecutable::CellText(int row, int col) const {
    if (row < 0 || static_cast<size_t>(row) >= m_Rows.size()) return "";
    const Row& r = m_Rows[static_cast<size_t>(row)];
    if (col < 0 || static_cast<size_t>(col) >= r.cells.size()) return "";
    return r.cells[static_cast<size_t>(col)];
}

ItemExecutable* TableExecutable::AssociatedItem(size_t rowIndex) const {
    if (rowIndex >= m_Rows.size()) return nullptr;
    return m_Rows[rowIndex].associatedItem;
}

void TableExecutable::RemoveRowAt(size_t rowIndex) {
    if (rowIndex >= m_Rows.size()) return;
    m_Rows.erase(m_Rows.begin() + static_cast<long>(rowIndex));
    if (m_Rows.empty()) {
        m_SelectedRow = -1;
    } else if (static_cast<size_t>(m_SelectedRow) >= m_Rows.size()) {
        m_SelectedRow = static_cast<int>(m_Rows.size()) - 1;
    }
}

void TableExecutable::DropRow(size_t rowIndex) {
    if (rowIndex >= m_Rows.size()) return;
    // Marks for removal (PlayerExecutable::PurgeRemovedItems erases it
    // once this tick's script calls have fully unwound) rather than
    // erasing right now -- see item_executable.h's markedForRemoval()
    // comment for why an immediate erase here would be a use-after-free
    // (inventory.s's UseItem() still holds a live reference to this same
    // item on the line right after the RemoveRow() call that reaches
    // here).
    if (ItemExecutable* item = m_Rows[rowIndex].associatedItem) {
        item->MarkForRemoval();
    }
    RemoveRowAt(rowIndex);
}

}  // namespace sk_bindings
