#include "simkin_bindings/menu_executable.h"

#include <cstdio>

#include "simkin_bindings/combo_box_executable.h"
#include "simkin_bindings/floating_sprite_executable.h"
#include "simkin_bindings/floating_text_executable.h"
#include "simkin_bindings/menu_item_handle.h"
#include "simkin_bindings/native_binding_common.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/popup_menu_executable.h"
#include "simkin_bindings/text_area_executable.h"
#include "skRValue.h"
#include "skRValueArray.h"

namespace sk_bindings {

MenuExecutable::MenuExecutable(const skString& filename, skExecutableContext& ctxt,
                                MenuStack& stack)
    : skScriptedExecutable(filename, ctxt), m_Stack(stack) {}

bool MenuExecutable::setValue(const skString& fieldName, const skString& attribute,
                               const skRValue& value) {
    std::string key = ToStdString(fieldName);
    skiExecutable* obj = value.obj();
    if (obj != nullptr && obj->executableType() != TREENODE_TYPE) {
        m_NativeFields[key] = value;
        return true;
    }
    m_NativeFields.erase(key);
    return skScriptedExecutable::setValue(fieldName, attribute, value);
}

bool MenuExecutable::getValue(const skString& fieldName, const skString& attribute,
                               skRValue& value) {
    auto it = m_NativeFields.find(ToStdString(fieldName));
    if (it != m_NativeFields.end()) {
        value = it->second;
        return true;
    }
    return skScriptedExecutable::getValue(fieldName, attribute, value);
}

void MenuExecutable::RunInit() {
    skRValueArray args;
    args.append(skRValue(0));  // placeholder for Init's "(s)" parameter
    skRValue ret;
    skExecutableContext ctxt(&m_Stack.interpreter());
    method(skString("Init"), args, ret, ctxt);
}

void MenuExecutable::RunOnDisplay() {
    skRValueArray args;
    skRValue ret;
    skExecutableContext ctxt(&m_Stack.interpreter());
    method(skString("OnDisplay"), args, ret, ctxt);
}

MenuExecutable::MenuRow& MenuExecutable::AddRow(RowKind kind, int textId,
                                                 const std::string& callback, bool selectable) {
    m_Rows.push_back(MenuRow{kind, textId, callback, selectable, nullptr});
    return m_Rows.back();
}

void MenuExecutable::SetRowSelectable(size_t rowIndex, bool selectable) {
    if (rowIndex < m_Rows.size()) m_Rows[rowIndex].selectable = selectable;
}

void MenuExecutable::SetRowTextId(size_t rowIndex, int textId) {
    if (rowIndex < m_Rows.size()) m_Rows[rowIndex].textId = textId;
}

void MenuExecutable::MoveSelection(int delta) {
    std::vector<size_t> selectableIndices;
    for (size_t i = 0; i < m_Rows.size(); ++i) {
        if (m_Rows[i].selectable) selectableIndices.push_back(i);
    }
    if (selectableIndices.empty()) return;

    // m_SelectedItem is a 1-based index into m_Rows (matching the
    // scripts' own SetSelectedItem(1)-style convention) -- find where the
    // current selection sits among just the selectable ones.
    size_t currentPos = 0;
    for (size_t i = 0; i < selectableIndices.size(); ++i) {
        if (static_cast<int>(selectableIndices[i]) + 1 == m_SelectedItem) {
            currentPos = i;
            break;
        }
    }
    int count = static_cast<int>(selectableIndices.size());
    int nextPos = (static_cast<int>(currentPos) + delta % count + count) % count;
    m_PrevSelectedItem = m_SelectedItem;
    m_SelectedItem = static_cast<int>(selectableIndices[static_cast<size_t>(nextPos)]) + 1;
}

void MenuExecutable::CycleSelectedCombo(int delta) {
    if (m_SelectedItem < 1 || static_cast<size_t>(m_SelectedItem) > m_Rows.size()) return;
    MenuRow& row = m_Rows[static_cast<size_t>(m_SelectedItem - 1)];
    if (row.kind != RowKind::ComboBox) return;
    auto* combo = static_cast<ComboBoxExecutable*>(row.widget.get());
    combo->CycleSelection(delta);
    TryInvoke(combo->onChangeCallback());
}

void MenuExecutable::ActivateSelected() {
    if (m_SelectedItem < 1 || static_cast<size_t>(m_SelectedItem) > m_Rows.size()) return;
    MenuRow& row = m_Rows[static_cast<size_t>(m_SelectedItem - 1)];
    switch (row.kind) {
        case RowKind::MenuItem:
            TryInvoke(row.callback);
            break;
        case RowKind::ComboBox:
            TryInvoke(static_cast<ComboBoxExecutable*>(row.widget.get())->onEnterCallback());
            break;
        case RowKind::FloatingSprite:
            TryInvoke(static_cast<FloatingSpriteExecutable*>(row.widget.get())->callback());
            break;
        default:
            break;
    }
}

PopupMenuExecutable* MenuExecutable::activePopup() const {
    for (PopupMenuExecutable* popup : m_KnownPopups) {
        if (popup->visible()) return popup;
    }
    return nullptr;
}

void MenuExecutable::TryInvoke(const std::string& handlerName) {
    if (handlerName.empty()) return;
    skRValueArray args;
    skRValue ret;
    skExecutableContext ctxt(&m_Stack.interpreter());
    // Calls the base class directly (not this->method()) so an
    // undefined handler name just quietly does nothing instead of
    // spamming the soft-fail log for what's an optional hook, not an
    // unresolved native call.
    skScriptedExecutable::method(skString(handlerName.c_str()), args, ret, ctxt);
}

bool MenuExecutable::method(const skString& methodName, skRValueArray& args,
                             skRValue& returnValue, skExecutableContext& context) {
    if (methodName == skString("MenuBackground") && args.entries() == 1) {
        m_BackgroundId = args[0].intValue();
        return true;
    }
    if (methodName == skString("AddTitle") && args.entries() == 1) {
        m_TitleTextId = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetUseHoriz") && args.entries() == 1) {
        m_UseHoriz = args[0].boolValue();
        return true;
    }
    if (methodName == skString("AddStaticItem") && args.entries() >= 1) {
        AddRow(RowKind::StaticItem, args[0].intValue(), "", false);
        // savegamemenu.s/loadgamemenu.s assign this to a variable used
        // later as a plain SetSelectedItem(...) argument -- no script
        // ever calls a method on it, so a plain int (1-based row index,
        // matching AddMenuItem's convention) is enough; no object needed.
        returnValue = skRValue(static_cast<int>(m_Rows.size()));
        return true;
    }
    if (methodName == skString("AddMenuItem") && args.entries() == 2) {
        MenuRow& row =
            AddRow(RowKind::MenuItem, args[0].intValue(), ToStdString(args[1].str()), true);
        row.widget.reset(new MenuItemHandle(*this, m_Rows.size() - 1));
        returnValue = skRValue(static_cast<skiExecutable*>(row.widget.get()), false);
        return true;
    }
    if (methodName == skString("AddComboBox") && args.entries() == 2) {
        MenuRow& row = AddRow(RowKind::ComboBox, 0, "", true);
        row.widget.reset(new ComboBoxExecutable(args[0].intValue(), args[1].intValue()));
        returnValue = skRValue(static_cast<skiExecutable*>(row.widget.get()), false);
        return true;
    }
    if (methodName == skString("AddTextArea") && args.entries() == 2) {
        MenuRow& row = AddRow(RowKind::TextArea, 0, "", true);
        row.widget.reset(
            new TextAreaExecutable(*this, m_Rows.size() - 1, args[0].intValue(), args[1].intValue()));
        returnValue = skRValue(static_cast<skiExecutable*>(row.widget.get()), false);
        return true;
    }
    if (methodName == skString("AddFloatingSprite") && args.entries() == 5) {
        MenuRow& row =
            AddRow(RowKind::FloatingSprite, 0, ToStdString(args[1].str()), true);
        row.widget.reset(
            new FloatingSpriteExecutable(ToStdString(args[1].str()), args[2].intValue(),
                                          args[3].intValue()));
        returnValue = skRValue(static_cast<skiExecutable*>(row.widget.get()), false);
        return true;
    }
    if (methodName == skString("OpenEditText") && args.entries() == 6) {
        m_TextEntryActive = true;
        AddRow(RowKind::TextEntry, 0, "", true);
        return true;
    }
    if (methodName == skString("ClearMenu") && args.entries() == 0) {
        m_Rows.clear();
        m_SelectedItem = 0;
        m_TitleTextId = -1;
        m_UseHoriz = false;
        m_TextEntryActive = false;
        // Old popups get replaced (their script variable reassigned) or
        // just left unreferenced the next time OnDisplay runs after a
        // ClearMenu -- either way these pointers may already be dangling
        // by the time OnDisplay finishes, so drop them now rather than
        // wait for a fresh CreatePopupMenu() to (maybe) repopulate the
        // list.
        m_KnownPopups.clear();
        return true;
    }
    if (methodName == skString("SetSelectedItem") && args.entries() == 1) {
        m_PrevSelectedItem = m_SelectedItem;
        m_SelectedItem = args[0].intValue();
        return true;
    }
    if (methodName == skString("GetPrevItem") && args.entries() == 0) {
        returnValue = skRValue(m_PrevSelectedItem);
        return true;
    }
    if (methodName == skString("GetSelectedItemNumber") && args.entries() == 0) {
        returnValue = skRValue(m_SelectedItem);
        return true;
    }
    if (methodName == skString("CreateMenu") && args.entries() == 1) {
        std::string path = ToStdString(args[0].str());
        MenuExecutable* child = m_Stack.GetOrCreateMenu(path);
        // Owned by MenuStack (unique_ptr), not by this RValue reference --
        // created=false so Simkin's ref-counting never deletes it.
        if (child) returnValue = skRValue(static_cast<skiExecutable*>(child), false);
        return true;
    }
    if (methodName == skString("OpenMenu") && args.entries() == 1) {
        m_Stack.OpenMenu(ToStdString(args[0].str()));
        return true;
    }
    if (methodName == skString("CreatePopupMenu") && args.entries() == 4) {
        auto* popup = new PopupMenuExecutable(*this, args[0].intValue(), args[1].intValue(),
                                               args[2].intValue(), args[3].intValue());
        m_KnownPopups.push_back(popup);
        // No native owner besides the script variable -- created=true so
        // it's freed once Simkin's ref count on it reaches zero.
        returnValue = skRValue(static_cast<skiExecutable*>(popup), true);
        return true;
    }
    if (methodName == skString("AddFloatingTextJustify") && args.entries() == 5) {
        auto* text = new FloatingTextExecutable(args[0].intValue(), args[1].intValue(),
                                                 args[2].intValue(), args[3].boolValue(),
                                                 args[4].intValue());
        returnValue = skRValue(static_cast<skiExecutable*>(text), true);
        return true;
    }
    if (methodName == skString("GetPlayer") && args.entries() == 0) {
        returnValue = skRValue(static_cast<skiExecutable*>(&m_Stack.player()), false);
        return true;
    }
    if (methodName == skString("GameAvailableForLoad")) {
        int slot = args.entries() >= 1 ? args[0].intValue() : -1;
        returnValue = skRValue(m_Stack.GameAvailableForLoad(slot));
        return true;
    }
    if (methodName == skString("ActuallySaveGame") && args.entries() == 1) {
        m_Stack.ActuallySaveGame(args[0].intValue());
        TryInvoke("DoneSave");  // no-ops if this menu doesn't define one
        return true;
    }
    if (methodName == skString("DeleteGame") && args.entries() == 1) {
        m_Stack.DeleteGame(args[0].intValue());
        return true;
    }
    if (methodName == skString("DeleteAllGames") && args.entries() == 0) {
        m_Stack.DeleteAllGames();
        return true;
    }
    if (methodName == skString("GetSavedTimeStr") && args.entries() == 1) {
        returnValue = skRValue(skString(m_Stack.GetSavedTimeStr(args[0].intValue()).c_str()));
        return true;
    }
    if (methodName == skString("GetMultiplayerSlot") && args.entries() == 0) {
        returnValue = skRValue(m_Stack.multiplayerSlot());
        return true;
    }
    if (methodName == skString("LoadGame") && args.entries() == 1) {
        // No real save-file format exists to read a different zone/player
        // state from yet (M5's save system is in-memory only) -- this
        // just re-enters the same tutorial zone LoadGame() would in a
        // real fresh save.
        m_Stack.RequestGameStart("azra");
        return true;
    }
    if (methodName == skString("NewGame") && args.entries() == 0) {
        m_Stack.RequestGameStart("azra");
        return true;
    }
    if (methodName == skString("NewGameHook") && args.entries() == 0) {
        returnValue = skRValue(0);  // no host process to hand off to -- always single-player
        return true;
    }
    if (methodName == skString("QuitGame") && args.entries() == 0) {
        m_Stack.RequestQuit();
        return true;
    }
    if (methodName == skString("ShowCredits") && args.entries() == 0) {
        m_Stack.ShowCredits();
        return true;
    }
    if (methodName == skString("GameActive") || methodName == skString("CheatsActivated") ||
        methodName == skString("IsMultiplayer") || methodName == skString("IsMultiplayerClient") ||
        methodName == skString("ArenaActive")) {
        // No game session or multiplayer/cheat state exists yet at this
        // milestone -- a fixed "off" answer is the correct behavior for a
        // freshly booted main menu, not a soft-fail placeholder.
        returnValue = skRValue(false);
        return true;
    }
    if (methodName == skString("UnFadeMusic") || methodName == skString("FadeMusic") ||
        methodName == skString("ClearNewGameHook")) {
        return true;
    }
    if (skScriptedExecutable::method(methodName, args, returnValue, context)) {
        return true;
    }
    return SoftFailNativeCall("Menu", methodName, args, returnValue);
}

}  // namespace sk_bindings
