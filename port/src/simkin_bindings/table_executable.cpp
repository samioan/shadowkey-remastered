#include "simkin_bindings/table_executable.h"

#include "assets/product_database.h"
#include "assets/string_table.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/native_binding_common.h"
#include "simkin_bindings/player_executable.h"
#include "skRValue.h"
#include "skRValueArray.h"

namespace sk_bindings {
namespace {

// FUN_100a4ce4's own view of "what is this cell about": the product row's
// category in Buy mode, the item's own type otherwise. -1 when the cell
// carries neither (a plain SetText grid cell).
int CellItemType(const TableCell& cell) {
    if (cell.product) return cell.product->category;
    if (cell.item) return cell.item->itemType();
    return -1;
}

}  // namespace

bool TableRowHandle::method(const skString& methodName, skRValueArray& args,
                             skRValue& returnValue, skExecutableContext& context) {
    // The eight methods of the cell class (FUN_100a4ce4). Every one of
    // them branches on whether cell+0x3c (the item) is set, i.e. on
    // Sell/Inventory versus Buy -- the same object serves both pages.
    const TableCell* cell = m_Table.PeekCell(m_RowIndex, 0);
    if (!cell) return SoftFailNativeCall("TableCell", methodName, args, returnValue);

    if (methodName == skString("GetItemText") && args.entries() == 0) {
        returnValue = skRValue(skString(cell->text.c_str()));
        return true;
    }
    if (methodName == skString("GetAssociatedObject") && args.entries() == 0) {
        // cell+0x3c. Null on the buy page, which is exactly what
        // buysell.s's SellItem() guards on ("if (inventory!=null)").
        if (cell->item) returnValue = skRValue(static_cast<skiExecutable*>(cell->item), false);
        return true;
    }
    if (methodName == skString("GetItemType") && args.entries() == 0) {
        const int type = CellItemType(*cell);
        if (type >= 0) returnValue = skRValue(type);
        return true;
    }
    if (methodName == skString("IsInventoryEquipped") && args.entries() == 0) {
        // Buy mode asks a different question than the name suggests: with
        // no item to inspect it falls back to "does the player already own
        // one of these?" (FUN_10045474, by template id).
        if (cell->item) {
            returnValue = skRValue(cell->item->equipped());
        } else if (cell->product) {
            returnValue = skRValue(m_Table.owner().PlayerOwnsTemplate(cell->product->templateId));
        }
        return true;
    }
    if (methodName == skString("IsItemEnabledFor") && args.entries() == 0) {
        // The real class-restriction test. On the buy page it is a direct
        // read of the products.dat row's own nine class flags, indexed by
        // the player's class -- the same byte the name cell's tint reads.
        // buysell.s's BuyInv() shows "Your class cannot use this item!"
        // and offers "Buy anyway" off exactly this.
        if (cell->product) {
            returnValue =
                skRValue(cell->product->enabledForClass(m_Table.owner().PlayerCharacterClass()));
        } else {
            // No per-item class-restriction data exists in this port (see
            // M59's product_database.h) -- an owned item is always usable.
            returnValue = skRValue(true);
        }
        return true;
    }
    if (methodName == skString("GetItemDescription") && args.entries() == 0) {
        if (cell->item) {
            skRValueArray none;
            cell->item->method(skString("GetItemDescription"), none, returnValue, context);
        } else if (cell->product) {
            // Faithful: the real guard is `descriptionStringId > 0xff1`
            // (4081, the last id the shipped stringtable holds), and on
            // the failing side it writes a **boolean true** into the
            // return atom rather than a string. Reproduced because
            // buysell.s concatenates the result straight into a popup.
            if (cell->product->descriptionStringId > 0xff1) {
                returnValue = skRValue(true);
            } else {
                returnValue = skRValue(
                    skString(m_Table.owner().ResolveText(cell->product->descriptionStringId).c_str()));
            }
        }
        return true;
    }
    if (methodName == skString("GetArmorText") && args.entries() == 0) {
        // Real, and really this odd: it returns a *bool* (whether the
        // armour's slot-name string exists), not text, and only for an
        // owned Armor item. Nothing in the shipped corpus calls it.
        returnValue = skRValue(cell->item && cell->item->itemType() == kItemTypeArmor);
        return true;
    }
    if (methodName == skString("DropItem") && args.entries() == 0) {
        m_Table.DropRow(m_RowIndex);
        return true;
    }
    return SoftFailNativeCall("TableCell", methodName, args, returnValue);
}

bool TableRowHandle::equals(const skiExecutable* other) const {
    auto* o = dynamic_cast<const TableRowHandle*>(other);
    return o && &o->m_Table == &m_Table && o->m_RowIndex == m_RowIndex;
}

TableExecutable::TableExecutable(MenuExecutable& owner, int rowCountHint, int x, int y, int w,
                                  int h)
    : NativeStubExecutable("Table"), m_Owner(owner), m_X(x), m_Y(y), m_W(w), m_H(h) {
    if (rowCountHint > 0) AddRows(rowCountHint);
    // AddRows bumps the used-row count too (FUN_1008e43c does), but a
    // freshly built table shows nothing until something fills it -- every
    // real caller either SetText()s the rows it asked for or repopulates
    // the whole thing.
    m_VisibleRows = rowCountHint > 0 ? rowCountHint : 0;
}

TableCell& TableExecutable::CellAt(size_t row, size_t col) {
    if (row >= m_Rows.size()) m_Rows.resize(row + 1);
    Row& r = m_Rows[row];
    if (col >= r.cells.size()) r.cells.resize(col + 1);
    // FUN_1008df60's tail: writing past the used-row count extends it.
    if (static_cast<int>(row) >= m_VisibleRows) m_VisibleRows = static_cast<int>(row) + 1;
    return r.cells[col];
}

const TableCell* TableExecutable::PeekCell(size_t row, size_t col) const {
    if (row >= m_Rows.size()) return nullptr;
    const Row& r = m_Rows[row];
    if (col >= r.cells.size()) return nullptr;
    return &r.cells[col];
}

void TableExecutable::ClearCells() {
    for (Row& r : m_Rows) r.cells.clear();
    m_VisibleRows = 0;
    m_SelectedRow = -1;
}

void TableExecutable::AddRows(int n) {
    if (n <= 0) return;
    m_Rows.resize(m_Rows.size() + static_cast<size_t>(n));
    m_VisibleRows += n;
}

void TableExecutable::SetUsedRows(int n) {
    m_VisibleRows = n < 0 ? 0 : n;
    if (m_VisibleRows == 0) {
        m_SelectedRow = -1;
    } else if (m_SelectedRow >= m_VisibleRows) {
        m_SelectedRow = m_VisibleRows - 1;
    } else if (m_SelectedRow < 0) {
        m_SelectedRow = 0;
    }
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
        TableCell& cell = CellAt(static_cast<size_t>(args[0].intValue()),
                                 static_cast<size_t>(args[1].intValue()));
        cell.text = ToStdString(args[2].str());
        if (m_SelectedRow < 0) m_SelectedRow = 0;
        return true;
    }
    if (methodName == skString("AddText") && args.entries() == 3) {
        // Same shape as SetText in every shipped call site.
        TableCell& cell = CellAt(static_cast<size_t>(args[0].intValue()),
                                 static_cast<size_t>(args[1].intValue()));
        cell.text = ToStdString(args[2].str());
        return true;
    }
    if (methodName == skString("AddRows") && args.entries() == 1) {
        AddRows(args[0].intValue());
        return true;
    }
    if (methodName == skString("Clear") && args.entries() == 0) {
        ClearCells();
        return true;
    }
    if (methodName == skString("GetSelectedRow") && args.entries() == 0) {
        if (m_SelectedRow >= 0 && m_SelectedRow < m_VisibleRows) {
            auto* handle = new TableRowHandle(*this, static_cast<size_t>(m_SelectedRow));
            returnValue = skRValue(static_cast<skiExecutable*>(handle), true);
        }
        return true;
    }
    if (methodName == skString("GetMaxRows") && args.entries() == 0) {
        returnValue = skRValue(rowCount());
        return true;
    }
    if (methodName == skString("SetInset") && args.entries() == 1) {
        // table+0xe2. buysell.s: "adjust down arrow within height" -- a
        // pure layout number for the scroll indicator, stored and
        // reported, with no scroll art in this port to move.
        m_Inset = args[0].intValue();
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
    if (methodName == skString("SetLineWrap") && args.entries() == 1) {
        // M88: real, and no longer a no-op -- questlog.s is the only
        // shipped caller and it passes true, which is what turns a
        // 100-character objective line ("Return five types of herbs to
        // Rilora: Foxglove, Mountain Tail, ...") into something that fits
        // a 176px screen instead of running off the right edge.
        m_LineWrap = args[0].boolValue();
        return true;
    }
    if (methodName == skString("SetRowSelect") || methodName == skString("AutoWrapCells") ||
        methodName == skString("SetCellSpacing") || methodName == skString("SetSelectable")) {
        // Cosmetic/behavioral flags this port's simplified single-column-
        // of-rows table model doesn't need to branch on -- every table is
        // already row-selectable with wrapped-if-needed text. Accepted so
        // real screens that call them don't soft-fail-log noise.
        return true;
    }
    return SoftFailNativeCall("Table", methodName, args, returnValue);
}

void TableExecutable::MoveSelection(int delta) {
    if (m_VisibleRows <= 0) return;
    m_SelectedRow =
        ((m_SelectedRow < 0 ? 0 : m_SelectedRow) + delta % m_VisibleRows + m_VisibleRows) %
        m_VisibleRows;
}

void TableExecutable::ActivateSelected() {
    // The real callback takes the selected **cell** ("SelectedItem[ (cell)
    // ...") and every shipped handler uses it -- buysell.s stores it as
    // `selectedItem` and then drives the whole buy/sell popup off it.
    // Passing nothing (which this did before M60) left `cell` unset and
    // the store screen with no way to name what the player picked.
    if (m_SelectedRow < 0 || m_SelectedRow >= m_VisibleRows) {
        m_Owner.TryInvoke(m_Callback);
        return;
    }
    auto* handle = new TableRowHandle(*this, static_cast<size_t>(m_SelectedRow));
    skRValue arg(static_cast<skiExecutable*>(handle), true);
    m_Owner.TryInvokeWithArg(m_Callback, arg);
}

std::string TableExecutable::CellText(int row, int col) const {
    if (row < 0 || row >= m_VisibleRows) return "";
    const TableCell* cell = PeekCell(static_cast<size_t>(row), static_cast<size_t>(col));
    return cell ? cell->text : std::string();
}

TableExecutable::CellCompare TableExecutable::CompareCell(int row, int col) const {
    // FUN_100a0cec, the branch guarded by cell+0x40. In order:
    //
    //   * a Consumable is never compared (the renderer returns early on
    //     category 4), which is why the potions page shows two blank
    //     columns;
    //   * the opponent is the item in the right hand (player+0x3ac+0x4c)
    //     for the +0x41 == 0 column and the left hand (+0x48) for the
    //     other -- except for Armor, where it is whatever is worn in the
    //     subject's own products.dat armour slot;
    //   * an empty hand, a Consumable in it, or an item of a different
    //     category means no arrow at all;
    //   * equal ratings draw a mark of their own, and otherwise sprite 24
    //     when the shelf is better and 25 when it is worse.
    const TableCell* cell = PeekCell(static_cast<size_t>(row), static_cast<size_t>(col));
    if (!cell || !cell->comparison) return CellCompare::None;
    const int category = CellItemType(*cell);
    if (category < 0 || category == kItemTypeConsumable) return CellCompare::None;
    const int rating = cell->product ? cell->product->rating
                                     : (cell->item ? cell->item->rating() : 0);

    PlayerExecutable& player = m_Owner.stackPlayer();
    ItemExecutable* opponent = nullptr;
    if (category == kItemTypeArmor) {
        const int slot = cell->product ? cell->product->armorSlot
                                       : m_Owner.ArmorSlotOfTemplate(cell->item->templateId());
        opponent = m_Owner.EquippedArmorInSlot(slot);
    } else {
        opponent = cell->compareLeftHand ? player.leftItem() : player.rightItem();
    }
    if (!opponent || opponent == cell->item) return CellCompare::None;
    if (opponent->itemType() == kItemTypeConsumable) return CellCompare::None;
    if (opponent->itemType() != category) return CellCompare::None;

    if (opponent->rating() == rating) return CellCompare::Equal;
    return opponent->rating() < rating ? CellCompare::Better : CellCompare::Worse;
}

ItemExecutable* TableExecutable::AssociatedItem(size_t rowIndex) const {
    const TableCell* cell = PeekCell(rowIndex, 0);
    return cell ? cell->item : nullptr;
}

void TableExecutable::RemoveRowAt(size_t rowIndex) {
    if (rowIndex >= m_Rows.size()) return;
    m_Rows.erase(m_Rows.begin() + static_cast<long>(rowIndex));
    if (m_VisibleRows > 0) --m_VisibleRows;
    if (m_VisibleRows == 0) {
        m_SelectedRow = -1;
    } else if (m_SelectedRow >= m_VisibleRows) {
        m_SelectedRow = m_VisibleRows - 1;
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
    if (const TableCell* cell = PeekCell(rowIndex, 0)) {
        if (ItemExecutable* item = cell->item) item->MarkForRemoval();
    }
    RemoveRowAt(rowIndex);
}

}  // namespace sk_bindings
