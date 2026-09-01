#pragma once

// Native binding for objects returned by AddComboBox() -- used by
// DropGoldMenu.s (a gold-amount picker) and the language/options combo
// in options.s's sibling menus. Tracks just enough state (options list,
// selection) for M4's navigation to work; everything else soft-fails.

#include <string>
#include <vector>

#include "simkin_bindings/native_stub_executable.h"

namespace sk_bindings {

class ComboBoxExecutable : public NativeStubExecutable {
public:
    ComboBoxExecutable(int x, int y);

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

    int selection() const { return m_Selection; }

private:
    int m_X, m_Y;
    int m_Width = 0;
    bool m_Numerical = false;
    std::string m_OnEnterCallback;
    std::vector<int> m_Options;
    int m_Selection = 0;
};

}  // namespace sk_bindings
