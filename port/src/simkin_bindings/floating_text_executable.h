#pragma once

// Native binding for objects returned by AddFloatingTextJustify().
//
// M110 correction: this comment used to end "the game's real font/glyph
// format was never RE'd ... so rendering is out of scope here regardless".
// The font was solved long ago (the device ROM's Ceurope.gdr, see
// assets/gdr_font.h) and the text is rendered now -- but by
// MenuExecutable::FloatingText, which the AddFloatingTextJustify handler
// records alongside the menu's titles, not by this object. These are the
// two softkey captions along the bottom of a menu screen, and all 95
// shipped call sites are that same pair.
//
// This class survives only because the script keeps the returned object
// long enough to call `SetFontNum(1)` on it, and that selection is the one
// part still unimplemented: it asks for a `TFontSpec("Swiss", 0xd5)`,
// which is not a typeface Ceurope.gdr carries.

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
