#include "simkin_bindings/menu_executable.h"

#include "simkin_bindings/combo_box_executable.h"
#include "simkin_bindings/floating_text_executable.h"
#include "simkin_bindings/native_binding_common.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/popup_menu_executable.h"
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

void MenuExecutable::MoveSelection(int delta) {
    std::vector<size_t> selectableIndices;
    for (size_t i = 0; i < m_Items.size(); ++i) {
        if (m_Items[i].selectable) selectableIndices.push_back(i);
    }
    if (selectableIndices.empty()) return;

    // m_SelectedItem is a 1-based index into m_Items (matching the
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

void MenuExecutable::ActivateSelected() {
    if (m_SelectedItem < 1 || static_cast<size_t>(m_SelectedItem) > m_Items.size()) return;
    const MenuItem& item = m_Items[static_cast<size_t>(m_SelectedItem - 1)];
    if (item.selectable) TryInvoke(item.callback);
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
    if (methodName == skString("AddStaticItem") && args.entries() >= 1) {
        m_Items.push_back(MenuItem{args[0].intValue(), "", false});
        return true;
    }
    if (methodName == skString("AddMenuItem") && args.entries() == 2) {
        m_Items.push_back(MenuItem{args[0].intValue(), ToStdString(args[1].str()), true});
        returnValue = skRValue(static_cast<int>(m_Items.size()));  // 1-based handle
        return true;
    }
    if (methodName == skString("ClearMenu") && args.entries() == 0) {
        m_Items.clear();
        m_SelectedItem = 0;
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
        auto* popup = new PopupMenuExecutable(args[0].intValue(), args[1].intValue(),
                                               args[2].intValue(), args[3].intValue());
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
    if (methodName == skString("AddComboBox") && args.entries() == 2) {
        auto* combo = new ComboBoxExecutable(args[0].intValue(), args[1].intValue());
        returnValue = skRValue(static_cast<skiExecutable*>(combo), true);
        return true;
    }
    if (methodName == skString("GetPlayer") && args.entries() == 0) {
        returnValue = skRValue(static_cast<skiExecutable*>(&m_Stack.player()), false);
        return true;
    }
    if (methodName == skString("GameActive") || methodName == skString("CheatsActivated") ||
        methodName == skString("GameAvailableForLoad") ||
        methodName == skString("IsMultiplayer") || methodName == skString("IsMultiplayerClient") ||
        methodName == skString("ArenaActive")) {
        // No game session or multiplayer/cheat state exists yet at this
        // milestone -- a fixed "off" answer is the correct behavior for a
        // freshly booted main menu, not a soft-fail placeholder.
        returnValue = skRValue(false);
        return true;
    }
    if (methodName == skString("UnFadeMusic") || methodName == skString("FadeMusic") ||
        methodName == skString("ClearNewGameHook") || methodName == skString("NewGameHook")) {
        return true;
    }
    if (skScriptedExecutable::method(methodName, args, returnValue, context)) {
        return true;
    }
    return SoftFailNativeCall("Menu", methodName, args, returnValue);
}

}  // namespace sk_bindings
