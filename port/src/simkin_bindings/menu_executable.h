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
//
// M10 adds AddButton/AddQuitButton/AddFloatingText (charactermanager.s/
// inventory.s/statsscreen.s's UI, all real screens now instead of soft-
// fail placeholders) -- AddButton and AddFloatingText both reuse the
// existing MenuItem/StaticItem row kinds rather than adding new ones
// (they're the same "text + optional selectable callback" shape), just
// returning a different native handle object (ButtonExecutable) so
// scripts can chain the extra cosmetic setters those screens call
// (SetWidth/ShowBorder/...). Two genuinely new row kinds are added for
// widgets with real distinct behavior: ItemButton (AddItemButton, an
// icon+text equip-slot display) and Table (AddTable, the inventory/
// stats/quest-log grid -- see table_executable.h).
//
// A row's display text can come from either a stringtable id (textId, the
// M3-M5 convention) or an already-resolved literal string (textId == -1,
// literalText used instead) -- AddButton/AddFloatingText's first argument
// is dynamically typed in the real scripts (sometimes a text id like
// AddButton(3043,...), sometimes a precomputed string like
// AddButton("Cymric",...) or AddFloatingText(healthText,...)), so which
// one applies is decided per call via the argument's own skRValue::type().

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/native_stub_executable.h"
#include "skRValue.h"
#include "skScriptedExecutable.h"

namespace sk_bindings {

class ItemExecutable;
class MenuExecutable;
class PopupMenuExecutable;
class TableExecutable;

// M10: AddTitle()'s return value -- inventory.s's WeaponsMenu()/
// ArmorMenu()/etc. call .SetLocalizedText(id) on it to change the title
// text after the fact (a single title slot, m_TitleTextId, not a row --
// so this doesn't use the RowOwnerRef mechanism the other widget handles
// do). Defined here (not its own file) since it's a tiny, MenuExecutable-
// only-coupled value member (see MenuExecutable::m_TitleHandle below);
// method() is implemented out-of-line in menu_executable.cpp, once
// MenuExecutable's own definition is visible.
class TitleHandle : public NativeStubExecutable {
public:
    explicit TitleHandle(MenuExecutable& owner) : NativeStubExecutable("Title"), m_Owner(owner) {}
    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

private:
    MenuExecutable& m_Owner;
};

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

    enum class RowKind {
        MenuItem,
        StaticItem,
        ComboBox,
        TextArea,
        FloatingSprite,
        TextEntry,
        ItemButton,  // M10: AddItemButton()
        Table,       // M10: AddTable()
    };
    struct MenuRow {
        RowKind kind;
        int textId = -1;  // -1 = use literalText instead, see header comment
        std::string literalText;
        std::string callback;  // MenuItem's AddMenuItem callback
        bool selectable = false;
        // Non-null for ComboBox/TextArea/FloatingSprite/ItemButton/Table
        // rows -- the same object handed back to the script (see
        // AddComboBox() etc.); the row owns it, the script only holds a
        // non-owning reference (see setValue()'s comment above), so
        // clearing/replacing rows is what actually frees it, matching how
        // the real UI would invalidate a widget handle when its menu
        // redraws.
        std::unique_ptr<NativeStubExecutable> widget;
        // M21: AddMenuItem's real 3-arg form (text, callback,
        // associatedObject) -- lootmenu.s's own
        // `AddMenuItem(Item.GetName(),"SelectItem",Item)`, read back via
        // MenuItemHandle::GetAssociatedObject(). Non-owning (the real
        // owner is wherever the object actually lives -- an ItemExecutable
        // bag's own m_Contents, for the loot-menu case).
        skiExecutable* associatedObject = nullptr;
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
    // M10: ButtonExecutable/MenuItemHandle's .SetItemText(literalString).
    void SetRowLiteralText(size_t rowIndex, const std::string& text);
    // M10: TitleHandle's .SetLocalizedText(id) -- AddTitle()'s return
    // value, see inventory.s's WeaponsMenu()/ArmorMenu()/etc.
    void SetTitleTextId(int textId) { m_TitleTextId = textId; }

    // M10: Up/Down/Enter routed into the currently-selected row's Table
    // widget (see table_executable.h) instead of this menu's own outer
    // MoveSelection/ActivateSelected. Returns false (did nothing) if the
    // current selection isn't a Table row, so main.cpp falls through to
    // its normal handling otherwise -- mirrors how CycleSelectedCombo
    // already encapsulates ComboBox's own Left/Right special case.
    bool TryMoveTableSelection(int delta);
    bool TryActivateTable();

    // Decompiled (FUN_10034d8c) this session: SetLeftActionQueue()/
    // SetRightActionQueue() set a hand-selection byte on the CALLING menu
    // instance (charactermanager.s), and ShowActionQueue() copies it onto
    // the target actionqueue.s instance before opening it -- both ends are
    // this same per-instance field on whichever MenuExecutable it's read
    // or written on. See ShowActionQueue()'s own handler in the .cpp for
    // the full real/faithful-vs-simplified writeup.
    bool queueHandIsRight() const { return m_QueueHandIsRight; }
    void SetQueueHandIsRight(bool right) { m_QueueHandIsRight = right; }

    // M21: the object that opened this menu on itself (a real loot bag's
    // `OpenMenu("LootMenu")`, ItemExecutable::method()'s OpenMenu handler)
    // -- set by MenuStack::OpenMenu()/ReopenMenu()'s new `opener` parameter,
    // read back via lootmenu.s's own `GetOpener()` calls
    // (`GetOpener().GetFirst()`, `GetOpener().RemoveObject(...)`, ...).
    // nullptr for every menu not opened this way (the overwhelming
    // majority) -- GetOpener() soft-fails to a benign default in that case
    // rather than crash, same as any other unset optional reference in
    // this port.
    void SetOpener(skiExecutable* opener) { m_Opener = opener; }

private:
    MenuRow& AddRow(RowKind kind, int textId, const std::string& callback, bool selectable);

    MenuStack& m_Stack;
    skiExecutable* m_Opener = nullptr;
    int m_BackgroundId = -1;
    int m_TitleTextId = -1;
    TitleHandle m_TitleHandle{*this};
    bool m_UseHoriz = false;
    bool m_TextEntryActive = false;
    std::vector<MenuRow> m_Rows;
    int m_SelectedItem = 0;
    bool m_QueueHandIsRight = false;
    int m_PrevSelectedItem = 0;
    std::map<std::string, skRValue> m_NativeFields;
    // Non-owning -- see CreatePopupMenu's handler and ClearMenu's reset in
    // the .cpp. Lets activePopup() find a visible one without scanning
    // m_NativeFields by (unknown) name or type.
    std::vector<PopupMenuExecutable*> m_KnownPopups;

    // M10: SetInventoryList()'s remembered target -- DisplayWeaponsPage()/
    // DisplayArmorMenu()/etc. populate whichever Table this last pointed
    // at, matching inventory.s's own "SetInventoryList(inventoryTable);
    // ... DisplayWeaponsPage(weaponsButton);" call order. Non-owning, same
    // convention as m_KnownPopups.
    TableExecutable* m_InventoryListTarget = nullptr;
};

}  // namespace sk_bindings
