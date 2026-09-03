#pragma once

// Native binding for objects returned by AddFloatingSprite() -- used by
// ChoosePortraitMenu.s for its male/female portrait picker (a selectable
// placeholder label there -- SpriteLabel(callback), since that picker's
// two rows are actually chosen by their callback, not their sprite id)
// and by charactermanager.s's real player portrait (`portrait.SetSprite(
// GetPlayer().GetPortraitID())`, M25 -- global.spr's own portrait slot,
// GRAPHICS_FORMAT.md's real portrait-sprite finding, drawn for real by
// main.cpp's RenderMenu() using spriteId()/x()/y() below).

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
    int x() const { return m_X; }
    int y() const { return m_Y; }
    // M25: real SetSprite(id) -- -1 (the default) means never set, e.g.
    // ChoosePortraitMenu.s's rows, which are real but sprite-less
    // (SpriteLabel(callback()) is the only real content there).
    int spriteId() const { return m_SpriteId; }

private:
    std::string m_Callback;
    int m_X, m_Y;
    int m_SpriteId = -1;
};

}  // namespace sk_bindings
