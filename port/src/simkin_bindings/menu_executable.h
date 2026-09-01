#pragma once

// Native binding for a menu .s script object -- mainmenu.s itself and
// every menu it (transitively) creates via CreateMenu(). One class
// suffices for all of them: structurally they're the same kind of
// object (a script-defined Init/OnDisplay/callback set, backed by native
// menu-item state), matching the "Menu (generic)" class hypothesis at
// binding offset 0x14cfc (docs/SIMKIN_NATIVE_API.md).
//
// M5 extends M3's plain selectable/static item list to a unified "row"
// model -- AddMenuItem/AddStaticItem/AddComboBox/AddTextArea/
// AddFloatingSprite/OpenEditText all append to the same ordered list, in
// call order, so the host's Up/Down navigation and rendering (main.cpp)
// only need to walk one list regardless of what's actually on a given
// screen. Everything else still soft-fails per M3's original design.

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/native_stub_executable.h"
#include "skRValue.h"
#include "skScriptedExecutable.h"

namespace sk_bindings {

class PopupMenuExecutable;

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

    // M4/M5: host-driven navigation, called from the input/tick loop.
    // Moves the 1-based selection to the next/previous *selectable* row,
    // wrapping around either end.
    void MoveSelection(int delta);
    // Cycles the currently selected row's value if it's a ComboBox (fires
    // its SetCallback() handler on change); no-op for any other row kind.
    void CycleSelectedCombo(int delta);
    // Fires the currently selected row's script callback (MenuItem's
    // AddMenuItem callback, ComboBox's SetOnEnterCallback, or
    // FloatingSprite's own callback), if it has one -- same dispatch path
    // CreateMenu's children go through, so a callback that calls
    // OpenMenu() correctly switches MenuStack::currentMenu().
    void ActivateSelected();
    // Fires a named handler if the script defines one (e.g.
    // "OnRightSoftkey" for the back/cancel softkey); no-ops silently if
    // it doesn't, unlike method()'s normal soft-fail logging -- this is
    // an optional hook, not an unresolved native call.
    void TryInvoke(const std::string& handlerName);

    enum class RowKind { MenuItem, StaticItem, ComboBox, TextArea, FloatingSprite, TextEntry };
    struct MenuRow {
        RowKind kind;
        int textId = 0;
        std::string callback;  // MenuItem's AddMenuItem callback
        bool selectable = false;
        // Non-null for ComboBox/TextArea/FloatingSprite rows -- the same
        // object handed back to the script (see AddComboBox() etc.); the
        // row owns it, the script only holds a non-owning reference (see
        // setValue()'s comment above), so clearing/replacing rows is what
        // actually frees it, matching how the real UI would invalidate a
        // widget handle when its menu redraws.
        std::unique_ptr<NativeStubExecutable> widget;
    };
    const std::vector<MenuRow>& rows() const { return m_Rows; }
    int backgroundId() const { return m_BackgroundId; }
    int titleTextId() const { return m_TitleTextId; }
    int selectedItem() const { return m_SelectedItem; }  // 1-based, 0 = none
    bool useHoriz() const { return m_UseHoriz; }
    bool textEntryActive() const { return m_TextEntryActive; }
    // The current screen's modal confirmation popup (myPopup, savePopup,
    // ...), if any of the ones it's created is currently visible -- popups
    // live in m_NativeFields (see setValue()'s comment), keyed by
    // whatever script variable name they were assigned to, so this is the
    // only way for the host (main.cpp) to find one to render/route input
    // to without knowing those names.
    PopupMenuExecutable* activePopup() const;

    // Used by RowOwnerRef-derived widgets (MenuItemHandle,
    // TextAreaExecutable) to implement .SetSelectable(bool) and (for
    // TextAreaExecutable) .SetLocalizedText(id).
    void SetRowSelectable(size_t rowIndex, bool selectable);
    void SetRowTextId(size_t rowIndex, int textId);

private:
    MenuRow& AddRow(RowKind kind, int textId, const std::string& callback, bool selectable);

    MenuStack& m_Stack;
    int m_BackgroundId = -1;
    int m_TitleTextId = -1;
    bool m_UseHoriz = false;
    bool m_TextEntryActive = false;
    std::vector<MenuRow> m_Rows;
    int m_SelectedItem = 0;
    int m_PrevSelectedItem = 0;
    std::map<std::string, skRValue> m_NativeFields;
    // Non-owning -- see CreatePopupMenu's handler and ClearMenu's reset in
    // the .cpp. Lets activePopup() find a visible one without scanning
    // m_NativeFields by (unknown) name or type.
    std::vector<PopupMenuExecutable*> m_KnownPopups;
};

}  // namespace sk_bindings
