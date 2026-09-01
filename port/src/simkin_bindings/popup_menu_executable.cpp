#include "simkin_bindings/popup_menu_executable.h"

#include <cstdio>

#include "simkin_bindings/native_binding_common.h"
#include "skRValue.h"

namespace sk_bindings {

PopupMenuExecutable::PopupMenuExecutable(int x, int y, int w, int h)
    : NativeStubExecutable("PopupMenu"), m_X(x), m_Y(y), m_W(w), m_H(h) {}

bool PopupMenuExecutable::method(const skString& methodName, skRValueArray& args,
                                  skRValue& returnValue, skExecutableContext& context) {
    if (methodName == skString("AddItem") && args.entries() >= 1) {
        Item item{args[0].intValue(), args.entries() >= 2 ? ToStdString(args[1].str()) : ""};
        m_Items.push_back(item);
        std::printf("  PopupMenu(%d,%d): AddItem(%d, \"%s\")\n", m_X, m_Y, item.textId,
                    item.callback.c_str());
        return true;
    }
    if (methodName == skString("SetBack") && args.entries() == 1) {
        m_BackCallback = ToStdString(args[0].str());
        return true;
    }
    if (methodName == skString("SetVisible") && args.entries() == 1) {
        m_Visible = args[0].boolValue();
        std::printf("  PopupMenu(%d,%d): SetVisible(%s)\n", m_X, m_Y,
                    m_Visible ? "true" : "false");
        return true;
    }
    if (methodName == skString("SetSelectedItem") && args.entries() == 1) {
        m_SelectedItem = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetSelectable") || methodName == skString("SetBackground")) {
        // Tracked at the pixel/rendering level once M4 actually draws
        // popups -- not needed to prove the state machine works.
        return true;
    }
    return SoftFailNativeCall("PopupMenu", methodName, args, returnValue);
}

}  // namespace sk_bindings
