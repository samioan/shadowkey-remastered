#include "simkin_bindings/row_owner_ref.h"

#include "assets/string_table.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/native_binding_common.h"
#include "skRValue.h"
#include "skRValueArray.h"

namespace sk_bindings {

bool RowOwnerRef::HandleSharedWidgetNative(const skString& methodName, skRValueArray& args,
                                           skRValue& returnValue) {
    if (methodName == skString("SetSelectable") && args.entries() == 1) {
        SetRowSelectable(args[0].boolValue());
        return true;
    }
    if (methodName == skString("SetVisible") && args.entries() == 1) {
        // Case 3: the visible byte only. Note it does *not* also clear
        // selectable -- that is SetEnabled's job below.
        SetRowVisible(args[0].boolValue());
        return true;
    }
    if (methodName == skString("IsVisible") && args.entries() == 0) {
        returnValue = skRValue(RowVisible());
        return true;
    }
    if (methodName == skString("SetEnabled") && args.entries() == 1) {
        // Case 6: both bytes, unlike SetVisible.
        const bool enabled = args[0].boolValue();
        SetRowVisible(enabled);
        SetRowSelectable(enabled);
        return true;
    }
    if (methodName == skString("GetItemText") && args.entries() == 0) {
        returnValue = skRValue(skString(RowText().c_str()));
        return true;
    }
    if (methodName == skString("SetItemText") && args.entries() == 1) {
        SetRowLiteralText(ToStdString(args[0].str()));
        return true;
    }
    if (methodName == skString("SetLocalizedText") && args.entries() == 1) {
        SetRowTextId(args[0].intValue());
        return true;
    }
    if (methodName == skString("GetX") && args.entries() == 0) {
        returnValue = skRValue(RowX());
        return true;
    }
    if (methodName == skString("GetY") && args.entries() == 0) {
        returnValue = skRValue(RowY());
        return true;
    }
    if (methodName == skString("SetX") && args.entries() == 1) {
        SetRowX(args[0].intValue());
        return true;
    }
    if (methodName == skString("SetY") && args.entries() == 1) {
        SetRowY(args[0].intValue());
        return true;
    }
    if (methodName == skString("GetAssociatedObject") && args.entries() == 0) {
        if (skiExecutable* obj = RowAssociatedObject()) returnValue = skRValue(obj, false);
        return true;
    }
    if ((methodName == skString("SetFontNum") ||
         methodName == skString("SetInvokeMethodOnFocus")) &&
        args.entries() == 1) {
        // Real fields (widget+0x3c and +0x5f) with no rendering concept in
        // this port -- there is one bitmap font, and focus-invoke timing
        // is not modelled. Accepted rather than soft-failed so the real
        // screens that call them stay quiet.
        return true;
    }
    return false;
}

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

void RowOwnerRef::SetRowWidth(int w) { m_Owner.SetRowWidth(m_RowIndex, w); }

void RowOwnerRef::SetRowHeight(int h) { m_Owner.SetRowHeight(m_RowIndex, h); }

void RowOwnerRef::SetRowShowBorder(bool showBorder) {
    m_Owner.SetRowShowBorder(m_RowIndex, showBorder);
}

void RowOwnerRef::SetRowX(int x) { m_Owner.SetRowX(m_RowIndex, x); }

void RowOwnerRef::SetRowY(int y) { m_Owner.SetRowY(m_RowIndex, y); }

void RowOwnerRef::SetRowVisible(bool visible) { m_Owner.SetRowVisible(m_RowIndex, visible); }

bool RowOwnerRef::RowVisible() const {
    const auto& rows = m_Owner.rows();
    return m_RowIndex < rows.size() ? rows[m_RowIndex].visible : false;
}

int RowOwnerRef::RowX() const {
    const auto& rows = m_Owner.rows();
    return m_RowIndex < rows.size() ? rows[m_RowIndex].x : 0;
}

int RowOwnerRef::RowY() const {
    const auto& rows = m_Owner.rows();
    return m_RowIndex < rows.size() ? rows[m_RowIndex].y : 0;
}

const std::string& RowOwnerRef::RowText() const {
    static const std::string kEmpty;
    const auto& rows = m_Owner.rows();
    if (m_RowIndex >= rows.size()) return kEmpty;
    const MenuExecutable::MenuRow& row = rows[m_RowIndex];
    // GetItemText reads back exactly what SetItemText/SetLocalizedText
    // last wrote -- a stringtable row resolves through the table, a
    // literal row is already text.
    if (row.textId >= 0) {
        static std::string resolved;
        resolved = m_Owner.ResolveText(row.textId);
        return resolved;
    }
    return row.literalText;
}

}  // namespace sk_bindings
