#pragma once

// Shared by native widgets that need to reach back into the MenuRow that
// owns them (currently just MenuItemHandle and TextAreaExecutable, for
// their .SetSelectable(bool) method) -- holds an index, not a pointer,
// since MenuExecutable::rows() is a std::vector and push_back on a later
// AddXxx() call can reallocate and invalidate any pointer taken into it
// earlier. An index stays valid across that.

#include <cstddef>
#include <string>

class skiExecutable;

namespace sk_bindings {

class MenuExecutable;

class RowOwnerRef {
protected:
    RowOwnerRef(MenuExecutable& owner, size_t rowIndex) : m_Owner(owner), m_RowIndex(rowIndex) {}

    // Defined in row_owner_ref.cpp, which includes menu_executable.h --
    // kept out of this header to avoid MenuExecutable.h <-> widget-header
    // include cycles (menu_executable.h includes the widget headers to
    // construct them).
    void SetRowSelectable(bool selectable);
    void SetRowTextId(int textId);
    // M10: ButtonExecutable/MenuItemHandle's SetItemText(literal string)
    // -- charactermanager.s's nameButton.SetItemText(myname) etc.
    void SetRowLiteralText(const std::string& text);
    // M21: MenuItemHandle's GetAssociatedObject() -- AddMenuItem's real
    // 3-arg form stores this on the row (menu_executable.h's MenuRow::
    // associatedObject comment); nullptr if the row was never given one
    // (every non-loot-menu AddMenuItem call site).
    skiExecutable* RowAssociatedObject() const;
    // ButtonExecutable's real .SetWidth(w)/.SetHeight(h)/.ShowBorder(true)
    // -- see MenuRow's own x/y/w/h/showBorder comment.
    void SetRowWidth(int w);
    void SetRowHeight(int h);
    void SetRowShowBorder(bool showBorder);

    MenuExecutable& m_Owner;
    size_t m_RowIndex;
};

}  // namespace sk_bindings
