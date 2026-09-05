#pragma once

// Shared by native widgets that need to reach back into the MenuRow that
// owns them (currently just MenuItemHandle and TextAreaExecutable, for
// their .SetSelectable(bool) method) -- holds an index, not a pointer,
// since MenuExecutable::rows() is a std::vector and push_back on a later
// AddXxx() call can reallocate and invalidate any pointer taken into it
// earlier. An index stays valid across that.
//
// M60: this is also where the engine's *shared widget base class* lands.
// This port grew one native class per widget kind (Button, MenuItem,
// FloatingText, TextArea, ItemButton), each re-declaring the setters it
// happened to need, but the shipped engine has exactly one: trie
// `0x14d20`, dispatcher `FUN_1007e954`, fourteen bindings that every
// on-screen widget inherits --
//
//     0  SetFontNum              widget+0x3c
//     1  GetAssociatedObject     widget+0x90
//     2  SetSelectable           widget+0x5c
//     3  SetVisible              widget+0x5d
//     4  IsVisible               widget+0x5d
//     5  SetInvokeMethodOnFocus  widget+0x5f
//     6  SetEnabled              widget+0x5d AND +0x5c   (both, unlike SetVisible)
//     7  GetItemText             widget+0x48
//     8  SetItemText             widget+0x40
//     9  SetLocalizedText        widget+0x40 = stringtable[id]
//     10 GetX / 11 GetY          widget+0x78 / +0x7c
//     12 SetX / 13 SetY          widget+0x78 / +0x7c
//
// So `weaponsButton.SetVisible(false)` and `titleItem.SetLocalizedText
// (2990)` and `productTable`'s neighbours are all *the same* fourteen
// methods on the same class -- which is why `buysell.s` mixes them freely
// across a Button, a FloatingText and a table. HandleSharedWidgetNative()
// below is that class, dispatched once for every RowOwnerRef-derived
// widget rather than five times over.
//
// Two details worth keeping straight, both real:
//   * SetVisible writes only the visible byte; SetEnabled writes the
//     visible byte *and* the selectable byte. They are not synonyms.
//   * SetItemText and SetLocalizedText write the same slot, so the last
//     of the two wins -- a literal string and a stringtable id are the
//     same field, not two layers.

#include <cstddef>
#include <string>

class skiExecutable;
class skRValue;
class skRValueArray;
class skString;

namespace sk_bindings {

class MenuExecutable;

class RowOwnerRef {
public:
    // Host-side equivalent of the widget's own SetItemText -- used by
    // SetGoldText(), which is a *menu* native that reaches into whatever
    // widget it was handed and writes text onto it.
    void SetTextFromHost(const std::string& text) { SetRowLiteralText(text); }

    virtual ~RowOwnerRef() = default;

protected:
    RowOwnerRef(MenuExecutable& owner, size_t rowIndex) : m_Owner(owner), m_RowIndex(rowIndex) {}

    // The fourteen bindings of engine widget class 0x14d20 (see above).
    // Returns false if `methodName` is not one of them, so each derived
    // widget can try its own extra methods first and soft-fail after.
    bool HandleSharedWidgetNative(const skString& methodName, skRValueArray& args,
                                  skRValue& returnValue);

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
    // M60: widget class 0x14d20's SetX/SetY/SetVisible -- see above.
    // `buysell.s` calls SetX on its four category buttons to close the gap
    // left by any category the merchant does not stock, and SetVisible
    // (false) on the button for the category itself.
    void SetRowX(int x);
    void SetRowY(int y);
    void SetRowVisible(bool visible);
    bool RowVisible() const;
    int RowX() const;
    int RowY() const;
    const std::string& RowText() const;

    MenuExecutable& m_Owner;
    size_t m_RowIndex;
};

}  // namespace sk_bindings
