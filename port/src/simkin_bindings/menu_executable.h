#pragma once

// Native binding for a menu .s script object -- mainmenu.s itself and
// every menu it (transitively) creates via CreateMenu(). One class
// suffices for all of them: structurally they're the same kind of
// object (a script-defined Init/OnDisplay/callback set, backed by native
// menu-item state), matching the "Menu (generic)" class hypothesis at
// binding offset 0x14cfc (docs/SIMKIN_NATIVE_API.md).
//
// M3 scope (see the port scaffold plan): implement enough of the native
// calls mainmenu.s's chain actually makes to track menu state correctly
// -- CreateMenu/OpenMenu, item lists, popups, the player object -- with
// everything else soft-failing rather than throwing.

#include <map>
#include <string>
#include <vector>

#include "simkin_bindings/menu_stack.h"
#include "skRValue.h"
#include "skScriptedExecutable.h"

namespace sk_bindings {

class MenuExecutable : public skScriptedExecutable {
public:
    MenuExecutable(const skString& filename, skExecutableContext& ctxt, MenuStack& stack);

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

    // skTreeNodeObject's default setValue()/getValue() treat every field
    // as TreeNode data: assigning a native object (a popup, floating
    // text, ...) to a field silently collapses it to its str()
    // representation (skTreeNodeObject.cpp, setValue()'s
    // "child->data(v.str())" fallback), discarding the object reference
    // -- so e.g. "myPopup = CreatePopupMenu(...); myPopup.AddItem(...)"
    // fails with "Method AddItem not found" the moment myPopup is read
    // back. Any field holding a non-TreeNode object is instead kept in
    // m_NativeFields, bypassing the TreeNode entirely; plain data fields
    // (strings/ints declared in the script) still go through the base
    // class unchanged.
    bool setValue(const skString& fieldName, const skString& attribute,
                  const skRValue& value) override;
    bool getValue(const skString& fieldName, const skString& attribute,
                  skRValue& value) override;

    // Runs the script's Init() handler with a placeholder "(s)" argument.
    // Called once by MenuStack right after construction.
    void RunInit();
    // Runs the script's OnDisplay() handler, if it defines one. Called by
    // MenuStack::OpenMenu each time this menu becomes current.
    void RunOnDisplay();

    // M4: host-driven navigation, called from the input/tick loop.
    // Moves the 1-based selection to the next/previous *selectable* item
    // (AddStaticItem rows are skipped), wrapping around either end.
    void MoveSelection(int delta);
    // Fires the currently selected item's script callback (e.g.
    // "MenuNewGame"), if it has one -- same dispatch path CreateMenu's
    // children go through, so a callback that calls OpenMenu() correctly
    // switches MenuStack::currentMenu().
    void ActivateSelected();
    // Fires a named handler if the script defines one (e.g.
    // "OnRightSoftkey" for the back/cancel softkey); no-ops silently if
    // it doesn't, unlike method()'s normal soft-fail logging -- this is
    // an optional hook, not an unresolved native call.
    void TryInvoke(const std::string& handlerName);

    struct MenuItem {
        int textId;
        std::string callback;  // blank for AddStaticItem
        bool selectable;
    };
    const std::vector<MenuItem>& items() const { return m_Items; }
    int backgroundId() const { return m_BackgroundId; }
    int selectedItem() const { return m_SelectedItem; }  // 1-based, 0 = none

private:
    MenuStack& m_Stack;
    int m_BackgroundId = -1;
    std::vector<MenuItem> m_Items;
    int m_SelectedItem = 0;
    int m_PrevSelectedItem = 0;
    std::map<std::string, skRValue> m_NativeFields;
};

}  // namespace sk_bindings
