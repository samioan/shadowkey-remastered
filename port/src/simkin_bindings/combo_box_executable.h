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
    // The AddOption() value at the current selection (a string-table id
    // for e.g. race names, a raw game-side id for e.g. class ids -- see
    // RenderMenu()'s comment on why it's rendered as-is either way), or
    // -1 if no options were ever added.
    int currentOptionValue() const {
        return m_Selection >= 0 && static_cast<size_t>(m_Selection) < m_Options.size()
                   ? m_Options[static_cast<size_t>(m_Selection)]
                   : -1;
    }
    // M95: `SetNumericalMode(true)` (case 3, `+0xb2`). The draw
    // (`FUN_1008e974`) tests it and, when set, formats the option with the
    // literal "%d" at 0x100f78d4 instead of looking it up in the string
    // table -- so dropgoldmenu.s's `AddOption(25)` reads "25", not string 25
    // ("Medium Bow"), which is what this port drew until the menu worked.
    bool numericalMode() const { return m_Numerical; }
    int width() const { return m_Width; }
    // M108: the combo's widget kind is 10 (`FUN_1008f82c` sets `+0x58 = 10`),
    // so it draws itself at its own `+0x78`/`+0x7c` -- the x and y the
    // script passed to AddComboBox -- and does not move the menu's row
    // cursor. See RenderMenu's ComboBox case.
    int x() const { return m_X; }
    int y() const { return m_Y; }
    const std::string& onChangeCallback() const { return m_OnChangeCallback; }
    const std::string& onEnterCallback() const { return m_OnEnterCallback; }

    // Host-driven navigation (main.cpp's Left/Right on a selected combo
    // row). Does NOT fire onChangeCallback itself; the caller
    // (MenuExecutable::CycleSelectedCombo) does that, since only it can
    // dispatch a method call back into the owning script.
    //
    // CORRECTED (M118): this wrapped at both ends, and the engine does
    // not. The combo's vtable +0x20/+0x24 (`FUN_1008eeb4`/`FUN_1008f0b8`)
    // step `+0x98` down only while it is non-zero and up only while it is
    // below `count - 1`, and run the callback only when it moved -- which
    // is also why the draw hides the left arrow on the first option and
    // the right one on the last. Returns whether the selection changed.
    bool CycleSelection(int delta) {
        const int count = static_cast<int>(m_Options.size());
        const int next = m_Selection + (delta < 0 ? -1 : delta > 0 ? 1 : 0);
        if (next < 0 || next >= count || next == m_Selection) return false;
        m_Selection = next;
        return true;
    }
    int optionCount() const { return static_cast<int>(m_Options.size()); }

private:
    int m_X, m_Y;
    int m_Width = 0;
    bool m_Numerical = false;
    std::string m_OnChangeCallback;
    std::string m_OnEnterCallback;
    std::vector<int> m_Options;
    int m_Selection = 0;
};

}  // namespace sk_bindings
