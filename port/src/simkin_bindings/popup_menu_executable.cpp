#include "simkin_bindings/popup_menu_executable.h"

#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/native_binding_common.h"
#include "skRValue.h"

namespace sk_bindings {

PopupMenuExecutable::PopupMenuExecutable(MenuExecutable& owner, int x, int y, int w, int h)
    : NativeStubExecutable("PopupMenu"), m_Owner(owner), m_X(x), m_Y(y), m_W(w), m_H(h) {}

bool PopupMenuExecutable::method(const skString& methodName, skRValueArray& args,
                                  skRValue& returnValue, skExecutableContext& context) {
    if (methodName == skString("AddItem") && args.entries() >= 1) {
        // Every observed popup's first AddItem() is a bare prompt/title
        // with no callback (e.g. "Do you really wish to quit?"),
        // followed by the real Yes/No-style choices, each with one --
        // matching every SetSelectable(0,false) call seen in the corpus
        // exactly, so "has a callback" doubles as "is selectable"
        // without needing to separately track the SetSelectable() calls.
        m_Items.push_back(
            Item{args[0].intValue(), args.entries() >= 2 ? ToStdString(args[1].str()) : ""});
        return true;
    }
    if (methodName == skString("SetBack") && args.entries() == 1) {
        m_BackCallback = ToStdString(args[0].str());
        return true;
    }
    if (methodName == skString("SetVisible") && args.entries() == 1) {
        m_Visible = args[0].boolValue();
        return true;
    }
    if (methodName == skString("SetSelectedItem") && args.entries() == 1) {
        m_SelectedItem = args[0].intValue();
        return true;
    }
    if (methodName == skString("IsVisible") && args.entries() == 0) {
        returnValue = skRValue(m_Visible);
        return true;
    }
    if (methodName == skString("SetSelectable") || methodName == skString("SetBackground") ||
        methodName == skString("SetAutoAdjust")) {
        // SetSelectable: see the AddItem() comment above -- selectability
        // is derived from having a callback, this call doesn't need to
        // change any state. SetBackground/SetAutoAdjust are purely
        // cosmetic (real background images are out of scope, see
        // main.cpp's RenderMenu()).
        return true;
    }
    return SoftFailNativeCall("PopupMenu", methodName, args, returnValue);
}

void PopupMenuExecutable::MoveSelection(int delta) {
    std::vector<size_t> selectableIndices;
    for (size_t i = 0; i < m_Items.size(); ++i) {
        if (!m_Items[i].callback.empty()) selectableIndices.push_back(i);
    }
    if (selectableIndices.empty()) return;

    size_t currentPos = 0;
    for (size_t i = 0; i < selectableIndices.size(); ++i) {
        if (static_cast<int>(selectableIndices[i]) + 1 == m_SelectedItem) {
            currentPos = i;
            break;
        }
    }
    int count = static_cast<int>(selectableIndices.size());
    int nextPos = (static_cast<int>(currentPos) + delta % count + count) % count;
    m_SelectedItem = static_cast<int>(selectableIndices[static_cast<size_t>(nextPos)]) + 1;
}

void PopupMenuExecutable::ActivateSelected() {
    if (m_SelectedItem < 1 || static_cast<size_t>(m_SelectedItem) > m_Items.size()) return;
    const Item& item = m_Items[static_cast<size_t>(m_SelectedItem - 1)];
    m_Owner.TryInvoke(item.callback);
}

void PopupMenuExecutable::GoBack() { m_Owner.TryInvoke(m_BackCallback); }

}  // namespace sk_bindings
