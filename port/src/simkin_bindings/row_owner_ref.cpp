#include "simkin_bindings/row_owner_ref.h"

#include "simkin_bindings/menu_executable.h"

namespace sk_bindings {

void RowOwnerRef::SetRowSelectable(bool selectable) {
    m_Owner.SetRowSelectable(m_RowIndex, selectable);
}

void RowOwnerRef::SetRowTextId(int textId) {
    m_Owner.SetRowTextId(m_RowIndex, textId);
}

void RowOwnerRef::SetRowLiteralText(const std::string& text) {
    m_Owner.SetRowLiteralText(m_RowIndex, text);
}

skiExecutable* RowOwnerRef::RowAssociatedObject() const {
    const auto& rows = m_Owner.rows();
    if (m_RowIndex >= rows.size()) return nullptr;
    return rows[m_RowIndex].associatedObject;
}

}  // namespace sk_bindings
