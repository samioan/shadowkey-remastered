#pragma once

// Native binding for objects returned by AddFloatingTextJustify() --
// mainmenu.s's "fun text" caption in the corner. Only tracked well enough
// to not throw; the game's real font/glyph format was never RE'd (see the
// port scaffold plan's known-stubs list), so rendering is out of scope
// here regardless.

#include "simkin_bindings/native_stub_executable.h"

namespace sk_bindings {

class FloatingTextExecutable : public NativeStubExecutable {
public:
    FloatingTextExecutable(int textId, int x, int y, bool justify, int color);

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

private:
    int m_TextId, m_X, m_Y, m_Color;
    bool m_Justify;
    int m_FontNum = 0;
};

}  // namespace sk_bindings
