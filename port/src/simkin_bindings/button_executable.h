#pragma once

// M10: native binding for objects returned by AddButton()/AddQuitButton()
// -- charactermanager.s/inventory.s/statsscreen.s's category buttons and
// "back"/quit corner buttons. Functionally the same selectable-row-with-
// callback as AddMenuItem's MenuItemHandle; this adds the layout/
// appearance setters those screens chain onto the returned object.
// M25: SetWidth/SetHeight/ShowBorder now write back onto the owning
// MenuRow (RowOwnerRef, same mechanism SetSelectable/SetItemText already
// use) and are actually rendered -- main.cpp's RenderMenu() draws a real
// outline rectangle for a ShowBorder(true) row using its real w/h.
// SetInvokeMethodOnFocus stays cosmetic-only. M118: SetHAdjust is real --
// it moves the button's rectangle (see below).

#include <string>

#include "simkin_bindings/native_stub_executable.h"
#include "simkin_bindings/row_owner_ref.h"

namespace sk_bindings {

class ButtonExecutable : public NativeStubExecutable, public RowOwnerRef {
public:
    ButtonExecutable(MenuExecutable& owner, size_t rowIndex)
        : NativeStubExecutable("Button"), RowOwnerRef(owner, rowIndex) {}

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

    void SetActive(bool active) { m_Active = active; }
    bool active() const { return m_Active; }

    // M118: AddButton's widget is the engine's icon widget (`FUN_1007f650`,
    // trie 0x14d2c), and these are its sprite and page fields, named for
    // what they do rather than what M36 guessed they were:
    //
    //   +0x9c  AddButton's 5th argument -- the art shown while this is the
    //          *active page's* tab. -1 for a text button (4-arg form),
    //          which is what makes the draw take its rectangle arm.
    //   +0xa0  the 6th -- the art shown otherwise.
    //   +0x98  the art actually drawn when not the active page. The
    //          constructor starts it on +0xa0; the script's SetActive(b)
    //          points it at +0x9c or +0xa0; the page-select native resets
    //          it to +0xa0 when it deselects.
    //   +0xb2  "this is the active page" -- set only by the page-select
    //          native (`FUN_1007f61c`, from FUN_10033c88 on every
    //          DisplayXPage call), never by the script's SetActive.
    //   +0xae  SetHAdjust: moves the rectangle down, never the label.
    //
    // M36 read the pair as "normal art" and "focused art" and swapped on
    // focus. The engine never swaps on focus: focus draws a ring around
    // whatever sprite is current (DrawEntitySpriteWithOutline).
    void SetSprites(int activeSprite, int dormantSprite) {
        m_SpriteActive = activeSprite;
        m_SpriteDormant = dormantSprite;
        m_SpriteCurrent = dormantSprite;
    }
    void SetPageSelected(bool selected) {
        m_PageSelected = selected;
        if (!selected) m_SpriteCurrent = m_SpriteDormant;
    }
    int spriteActive() const { return m_SpriteActive; }
    int spriteCurrent() const { return m_SpriteCurrent; }
    bool pageSelected() const { return m_PageSelected; }
    int hAdjust() const { return m_HAdjust; }

private:
    bool m_Active = false;
    bool m_Enabled = true;
    int m_SpriteActive = -1;   // +0x9c
    int m_SpriteDormant = -1;  // +0xa0
    int m_SpriteCurrent = -1;  // +0x98
    bool m_PageSelected = false;  // +0xb2
    int m_HAdjust = 0;            // +0xae
};

}  // namespace sk_bindings
