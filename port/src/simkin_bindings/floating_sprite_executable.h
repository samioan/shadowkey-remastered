#pragma once

// Native binding for objects returned by AddFloatingSprite() -- used by
// ChoosePortraitMenu.s for its male/female portrait picker. Real sprite
// rendering is out of scope (the game's portrait image format wasn't
// RE'd here either -- see GRAPHICS_FORMAT.md's open items), so this
// renders as a selectable placeholder label rather than an actual
// portrait image; its callback (fired on selection) is what actually
// matters for navigation to work.

#include <string>

#include "simkin_bindings/native_stub_executable.h"

namespace sk_bindings {

class FloatingSpriteExecutable : public NativeStubExecutable {
public:
    FloatingSpriteExecutable(std::string callback, int x, int y)
        : NativeStubExecutable("FloatingSprite"),
          m_Callback(std::move(callback)),
          m_X(x),
          m_Y(y) {}

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

    const std::string& callback() const { return m_Callback; }

private:
    std::string m_Callback;
    int m_X, m_Y;
    int m_SpriteId = -1;
};

}  // namespace sk_bindings
