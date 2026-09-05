#include "simkin_bindings/menu_executable.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "assets/string_table.h"
#include "audio/audio_engine.h"
#include "simkin_bindings/button_executable.h"
#include "simkin_bindings/combo_box_executable.h"
#include "simkin_bindings/floating_sprite_executable.h"
#include "simkin_bindings/floating_text_executable.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/item_button_executable.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/menu_item_handle.h"
#include "simkin_bindings/native_binding_common.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/popup_menu_executable.h"
#include "simkin_bindings/slider_executable.h"
#include "simkin_bindings/table_executable.h"
#include "simkin_bindings/text_area_executable.h"
#include "skRValue.h"
#include "skRValueArray.h"

namespace sk_bindings {

bool TitleHandle::method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                          skExecutableContext& context) {
    if (methodName == skString("SetLocalizedText") && args.entries() == 1) {
        m_Owner.SetTitleTextId(args[0].intValue());
        return true;
    }
    return SoftFailNativeCall("Title", methodName, args, returnValue);
}

namespace {

// AddButton()/AddFloatingText()'s first argument is dynamically typed in
// the real scripts -- see menu_executable.h's class comment.
void SetRowTextFromArg(MenuExecutable::MenuRow& row, const skRValue& arg) {
    if (arg.type() == skRValue::T_String) {
        row.literalText = ToStdString(arg.str());
        row.textId = -1;
    } else {
        row.textId = arg.intValue();
        row.literalText.clear();
    }
}

}  // namespace

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
    // M56: OnDisplay() is an optional hook -- most shipped menu scripts
    // don't define one -- so it goes through TryInvoke()'s base-class path
    // for exactly the reason spelled out there, rather than through
    // this->method() and its unresolved-native log. It had been the
    // loudest line in the suite's soft-fail output for a call that is
    // supposed to be absent.
    TryInvoke("OnDisplay");
}

MenuExecutable::MenuRow& MenuExecutable::AddRow(RowKind kind, int textId,
                                                 const std::string& callback, bool selectable) {
    // Fields are set by name, not via aggregate-initializer position --
    // MenuRow has grown fields (literalText, M10) since this was written,
    // and a positional initializer silently miscompiles the moment the
    // struct's member order and this call's argument order diverge.
    MenuRow row;
    row.kind = kind;
    row.textId = textId;
    row.callback = callback;
    row.selectable = selectable;
    m_Rows.push_back(std::move(row));
    return m_Rows.back();
}

void MenuExecutable::SetRowSelectable(size_t rowIndex, bool selectable) {
    if (rowIndex < m_Rows.size()) m_Rows[rowIndex].selectable = selectable;
}

void MenuExecutable::SetRowTextId(size_t rowIndex, int textId) {
    if (rowIndex < m_Rows.size()) m_Rows[rowIndex].textId = textId;
}

void MenuExecutable::SetRowLiteralText(size_t rowIndex, const std::string& text) {
    if (rowIndex < m_Rows.size()) m_Rows[rowIndex].literalText = text;
}

void MenuExecutable::SetRowWidth(size_t rowIndex, int w) {
    if (rowIndex < m_Rows.size()) m_Rows[rowIndex].w = w;
}

void MenuExecutable::SetRowHeight(size_t rowIndex, int h) {
    if (rowIndex < m_Rows.size()) m_Rows[rowIndex].h = h;
}

void MenuExecutable::SetRowShowBorder(size_t rowIndex, bool showBorder) {
    if (rowIndex < m_Rows.size()) m_Rows[rowIndex].showBorder = showBorder;
}

void MenuExecutable::MoveSelection(int delta) {
    std::vector<size_t> selectableIndices;
    for (size_t i = 0; i < m_Rows.size(); ++i) {
        if (m_Rows[i].selectable) selectableIndices.push_back(i);
    }
    if (selectableIndices.empty()) return;

    // m_SelectedItem is a 1-based index into m_Rows (matching the
    // scripts' own SetSelectedItem(1)-style convention) -- find where the
    // current selection sits among just the selectable ones.
    size_t currentPos = 0;
    bool found = false;
    for (size_t i = 0; i < selectableIndices.size(); ++i) {
        if (static_cast<int>(selectableIndices[i]) + 1 == m_SelectedItem) {
            currentPos = i;
            found = true;
            break;
        }
    }
    if (!found) {
        // The selection points at a static row, a row that no longer
        // exists, or nothing at all (a fresh/ClearMenu'd screen) -- land on
        // the first selectable row rather than applying `delta` from an
        // assumed position 0, which used to silently skip that first row
        // whenever the player's first keypress was Down.
        m_PrevSelectedItem = m_SelectedItem;
        m_SelectedItem = static_cast<int>(selectableIndices[0]) + 1;
        return;
    }
    int count = static_cast<int>(selectableIndices.size());
    int nextPos = (static_cast<int>(currentPos) + delta % count + count) % count;
    m_PrevSelectedItem = m_SelectedItem;
    m_SelectedItem = static_cast<int>(selectableIndices[static_cast<size_t>(nextPos)]) + 1;
}

bool MenuExecutable::NavigateDirectional(int dx, int dy) {
    if (m_SelectedItem < 1 || static_cast<size_t>(m_SelectedItem) > m_Rows.size()) return false;
    MenuRow& current = m_Rows[static_cast<size_t>(m_SelectedItem - 1)];

    // Left/Right on a value widget adjusts the value; that is not a move.
    if (dx != 0 && (current.kind == RowKind::ComboBox || current.kind == RowKind::Slider)) {
        return false;
    }

    // A table consumes Up/Down while it still has somewhere to go. At its
    // first/last row it declines, so focus can leave it for the band above
    // or below -- which is what makes the inventory table escapable.
    if (dy != 0 && current.kind == RowKind::Table) {
        auto* table = static_cast<TableExecutable*>(current.widget.get());
        if (table && table->rowCount() > 0) {
            int next = table->selectedRow() + dy;
            if (table->selectedRow() < 0 || (next >= 0 && next < table->rowCount())) {
                table->MoveSelection(dy);
                return true;
            }
        }
    }

    // Group the positioned selectable rows into horizontal bands. Rows the
    // script never gave a position (AddMenuItem lists, the quit softkey)
    // stay out of this entirely.
    constexpr int kBandTolerance = 8;  // px; a hand-laid row is pixel-aligned
    struct Positioned {
        size_t index;
        int x, y;
    };
    std::vector<Positioned> placed;
    for (size_t i = 0; i < m_Rows.size(); ++i) {
        if (!m_Rows[i].selectable || m_Rows[i].y < 0) continue;
        placed.push_back({i, m_Rows[i].x, m_Rows[i].y});
    }
    if (placed.size() < 2) return false;

    auto sameBand = [kBandTolerance](int a, int b) { return std::abs(a - b) <= kBandTolerance; };
    int curX = current.x, curY = current.y;
    if (curY < 0) return false;  // selection isn't part of the laid-out grid

    if (dy != 0) {
        // Nearest band strictly above/below, then the nearest row in it.
        bool haveBand = false;
        int bandY = 0;
        for (const Positioned& p : placed) {
            if (sameBand(p.y, curY)) continue;
            if (dy > 0 ? (p.y <= curY) : (p.y >= curY)) continue;
            if (!haveBand || (dy > 0 ? p.y < bandY : p.y > bandY)) {
                bandY = p.y;
                haveBand = true;
            }
        }
        if (!haveBand) return false;
        size_t best = placed.front().index;
        int bestDx = -1;
        for (const Positioned& p : placed) {
            if (!sameBand(p.y, bandY)) continue;
            int d = std::abs(p.x - curX);
            if (bestDx < 0 || d < bestDx) {
                bestDx = d;
                best = p.index;
            }
        }
        m_PrevSelectedItem = m_SelectedItem;
        m_SelectedItem = static_cast<int>(best) + 1;
        // Entering a table from above/below starts at its near edge rather
        // than wherever it was left, so the two directions stay symmetric.
        MenuRow& landed = m_Rows[best];
        if (landed.kind == RowKind::Table) {
            auto* table = static_cast<TableExecutable*>(landed.widget.get());
            if (table && table->rowCount() > 0) {
                table->SetSelectedRow(dy > 0 ? 0 : table->rowCount() - 1);
            }
        }
        return true;
    }

    // Horizontal: move within this band, wrapping (a five-tab strip reads
    // as a ring, and the real screens never have more than a handful).
    std::vector<Positioned> band;
    for (const Positioned& p : placed) {
        if (sameBand(p.y, curY)) band.push_back(p);
    }
    if (band.size() < 2) return false;
    std::sort(band.begin(), band.end(),
              [](const Positioned& a, const Positioned& b) { return a.x < b.x; });
    int pos = 0;
    for (size_t i = 0; i < band.size(); ++i) {
        if (band[i].index == static_cast<size_t>(m_SelectedItem - 1)) pos = static_cast<int>(i);
    }
    int count = static_cast<int>(band.size());
    int next = ((pos + dx) % count + count) % count;
    m_PrevSelectedItem = m_SelectedItem;
    m_SelectedItem = static_cast<int>(band[static_cast<size_t>(next)].index) + 1;
    return true;
}

void MenuExecutable::EnsureValidSelection() {
    if (m_SelectedItem >= 1 && static_cast<size_t>(m_SelectedItem) <= m_Rows.size() &&
        m_Rows[static_cast<size_t>(m_SelectedItem - 1)].selectable) {
        return;
    }
    for (size_t i = 0; i < m_Rows.size(); ++i) {
        if (m_Rows[i].selectable) {
            m_SelectedItem = static_cast<int>(i) + 1;
            return;
        }
    }
    m_SelectedItem = 0;  // nothing selectable on this screen at all
}

void MenuExecutable::CycleSelectedCombo(int delta) {
    if (m_SelectedItem < 1 || static_cast<size_t>(m_SelectedItem) > m_Rows.size()) return;
    MenuRow& row = m_Rows[static_cast<size_t>(m_SelectedItem - 1)];
    if (row.kind == RowKind::Slider) {
        // M28: the real Options screen's SoundFXSlider/MusicSlider rows.
        auto* slider = static_cast<SliderExecutable*>(row.widget.get());
        slider->Adjust(delta);
        ApplySliderValue(*slider);
        TryInvoke(slider->callback());
        return;
    }
    if (row.kind != RowKind::ComboBox) return;
    auto* combo = static_cast<ComboBoxExecutable*>(row.widget.get());
    combo->CycleSelection(delta);
    TryInvoke(combo->onChangeCallback());
}

void MenuExecutable::ApplySliderValue(const SliderExecutable& slider) {
    // options.s names its two sliders in their AddMenuSlider() call; the
    // real engine dispatches the behaviour off that name (neither
    // callback is defined by any script in the corpus). Percent maps
    // straight through -- both real calls pass a max of 100.
    sk::AudioEngine* audio = m_Stack.audio();
    if (!audio) return;
    int percent = slider.maxValue() > 0 ? slider.value() * 100 / slider.maxValue() : 0;
    if (slider.callback() == "SoundFXSlider") {
        audio->SetSfxVolumePercent(percent);
    } else if (slider.callback() == "MusicSlider") {
        audio->SetMusicVolumePercent(percent);
    }
}

void MenuExecutable::ActivateSelected() {
    if (m_SelectedItem < 1 || static_cast<size_t>(m_SelectedItem) > m_Rows.size()) return;
    MenuRow& row = m_Rows[static_cast<size_t>(m_SelectedItem - 1)];
    switch (row.kind) {
        case RowKind::MenuItem:
            TryInvoke(row.callback);
            break;
        case RowKind::ComboBox:
            TryInvoke(static_cast<ComboBoxExecutable*>(row.widget.get())->onEnterCallback());
            break;
        case RowKind::FloatingSprite:
            TryInvoke(static_cast<FloatingSpriteExecutable*>(row.widget.get())->callback());
            break;
        default:
            break;
    }
}

bool MenuExecutable::TryMoveTableSelection(int delta) {
    if (m_SelectedItem < 1 || static_cast<size_t>(m_SelectedItem) > m_Rows.size()) return false;
    MenuRow& row = m_Rows[static_cast<size_t>(m_SelectedItem - 1)];
    if (row.kind != RowKind::Table) return false;
    static_cast<TableExecutable*>(row.widget.get())->MoveSelection(delta);
    return true;
}

bool MenuExecutable::TryActivateTable() {
    if (m_SelectedItem < 1 || static_cast<size_t>(m_SelectedItem) > m_Rows.size()) return false;
    MenuRow& row = m_Rows[static_cast<size_t>(m_SelectedItem - 1)];
    if (row.kind != RowKind::Table) return false;
    static_cast<TableExecutable*>(row.widget.get())->ActivateSelected();
    return true;
}

PopupMenuExecutable* MenuExecutable::activePopup() const {
    for (PopupMenuExecutable* popup : m_KnownPopups) {
        if (popup->visible()) return popup;
    }
    return nullptr;
}

bool MenuExecutable::TryInvoke(const std::string& handlerName) {
    if (handlerName.empty()) return false;
    skRValueArray args;
    skRValue ret;
    skExecutableContext ctxt(&m_Stack.interpreter());
    // Calls the base class directly (not this->method()) so an
    // undefined handler name just quietly does nothing instead of
    // spamming the soft-fail log for what's an optional hook, not an
    // unresolved native call. The return value says whether the script
    // actually defined it -- GoBack() below needs to know.
    return skScriptedExecutable::method(skString(handlerName.c_str()), args, ret, ctxt);
}

bool MenuExecutable::GoBack() {
    // The real back/cancel softkey, in the order the evidence supports.
    //
    // 1. The screen's own handler, if it has one. The corpus spells the
    //    name both "OnRightSoftkey" and "OnRightSoftKey" depending on the
    //    file (a genuine authoring inconsistency in the original scripts,
    //    not something to "fix"), so both are tried.
    // 2. Otherwise the screen's own SetPrevMenu() target.
    //
    // Step 2 is the fix for a real, badly user-visible bug: 15 real
    // screens call SetPrevMenu(...) in their Init(), and **no script
    // anywhere in the corpus ever reads it back** -- there is no
    // GetPrevMenu() call in the whole corpus -- so it can only ever have
    // been consumed natively by the engine's own back key. 11 of those 15
    // (Options, ConfigKeys, LoadGameMenu, SaveGameMenu, DeleteSavedGames,
    // ChooseCharacterMenu, NameChar's chain, MultiPlayerMenu and friends)
    // define no OnRightSoftkey handler at all. This port soft-failed
    // SetPrevMenu and had no fallback, so pressing back on any of them did
    // nothing whatsoever and the player was stranded on that screen with
    // no way out.
    MenuExecutable* before = m_Stack.currentMenu();
    bool handled = TryInvoke("OnRightSoftkey") || TryInvoke("OnRightSoftKey");
    // A handler that actually navigated somewhere is done.
    if (handled && m_Stack.currentMenu() != before) return true;
    if (m_Stack.quitRequested() || m_Stack.gameStartRequested()) return true;
    // Otherwise fall through to SetPrevMenu -- including when the screen
    // *did* define a handler. That is not a fallback for sloppy scripts,
    // it is what the real game does: options.s's OnRightSoftkey body is a
    // single call to `OptionsMenuBack()`, and `OptionsMenuBack` is
    // **absent from the fully-enumerated real 702-entry native table**
    // (shadowkey/simkin_native_bindings.json) -- so is `MenuBack()`, which
    // inventory.s / questlog.s / buysell.s / actionqueue.s call, and
    // `HostGameMenuBack()`. They were never registered, so those calls
    // miss and do nothing in the real binary too, exactly like the
    // already-documented `UpdateTextItems`/`GetLastItem`/`IsRightQueue`
    // case. The only thing left that can move a player off those screens
    // is the SetPrevMenu target the engine records.
    if (!m_PrevMenuPath.empty()) {
        m_Stack.OpenMenu(m_PrevMenuPath);
        return true;
    }
    return handled;
}

bool MenuExecutable::method(const skString& methodName, skRValueArray& args,
                             skRValue& returnValue, skExecutableContext& context) {
    if (methodName == skString("IsBuyMode") && args.entries() == 0) {
        // M59: store-screen binding 0 (trie 0x14de0). `buysell.s` is one
        // script serving both sides of the counter and this is the only
        // thing that tells them apart -- it picks the popup rows ("Buy"/
        // "Buy 5" against "Sell") and the header text.
        returnValue = skRValue(m_Stack.storeBuyMode());
        return true;
    }
    if (methodName == skString("MenuBackground") && args.entries() == 1) {
        m_BackgroundId = args[0].intValue();
        return true;
    }
    if (methodName == skString("AddTitle") && args.entries() >= 1) {
        // questlog.s calls this twice with an explicit y position
        // (AddTitle(3785,10); AddTitle(3786,25);) -- this port's renderer
        // only has one title slot, so the extra y argument is accepted
        // but ignored and the second call simply replaces the first
        // (documented simplification, same spirit as the flat-color
        // MenuBackground stand-in).
        m_TitleTextId = args[0].intValue();
        returnValue = skRValue(static_cast<skiExecutable*>(&m_TitleHandle), false);
        return true;
    }
    // M36: the two natives lootmenu.s uses instead of a plain Quit() once
    // its container has been emptied. Both branches of its LootExit()
    // carry a commented-out `//Quit();` next to the call that replaced it,
    // so both of these close the screen -- and the port implementing
    // neither is why emptying a chest left the player on a blank menu with
    // the "Okay" row gone: UpdateMenu() calls LootExit() and *returns
    // before adding any rows at all* when GetFirst() is null.
    if (methodName == skString("QueryDestroy") && args.entries() == 0) {
        // Container stays in the world (lootmenu.s has already called
        // SetUsable(false) on it) -- just close.
        m_Stack.RequestCloseMenu();
        return true;
    }
    if (methodName == skString("QuitAndDestroyOpener") && args.entries() == 0) {
        // Close, and take the container with it. Only reached when the
        // opener's own SetDestroy(true) was set, which real scripts do
        // solely for *spawned* loot bags (monsters/arat.s's own
        // `Loot.SetDestroy(true)`), never for a placed chest.
        if (auto* opener = dynamic_cast<ItemExecutable*>(m_Opener)) {
            opener->MarkForRemoval();
        }
        // Dropped immediately so nothing here can outlive the object it
        // just condemned -- main.cpp erases the world instance on its next
        // tick, once this whole script call chain has returned.
        m_Opener = nullptr;
        m_Stack.RequestCloseMenu();
        return true;
    }
    if (methodName == skString("Quit") && args.entries() == 0) {
        // See MenuStack::closeMenuRequested(). Deliberately does not
        // navigate here: a real Quit() is often followed immediately by an
        // OpenMenu(...) in the same handler (charactermanager.s's
        // OnRightSoftKey does exactly that), and acting instantly would
        // fight that second call.
        m_Stack.RequestCloseMenu();
        return true;
    }
    if (methodName == skString("SetPrevMenu") && args.entries() == 1) {
        // Where the back/cancel softkey goes when this screen defines no
        // handler of its own -- see GoBack(). The argument is a Simkin
        // script path in the corpus's usual escaped-backslash form
        // ("Menus\\\\NewGameMenu"); MenuStack::OpenMenu normalises it the
        // same way every other CreateMenu/OpenMenu path argument is.
        m_PrevMenuPath = ToStdString(args[0].str());
        return true;
    }
    if (methodName == skString("SetUseHoriz") && args.entries() == 1) {
        m_UseHoriz = args[0].boolValue();
        return true;
    }
    if (methodName == skString("AddStaticItem") && args.entries() >= 1) {
        AddRow(RowKind::StaticItem, args[0].intValue(), "", false);
        // savegamemenu.s/loadgamemenu.s assign this to a variable used
        // later as a plain SetSelectedItem(...) argument -- no script
        // ever calls a method on it, so a plain int (1-based row index,
        // matching AddMenuItem's convention) is enough; no object needed.
        returnValue = skRValue(static_cast<int>(m_Rows.size()));
        return true;
    }
    if (methodName == skString("AddMenuItem") && (args.entries() == 2 || args.entries() == 3)) {
        MenuRow& row = AddRow(RowKind::MenuItem, -1, ToStdString(args[1].str()), true);
        // M21: unlike every other AddMenuItem call site (a plain textId),
        // lootmenu.s's own `AddMenuItem(Item.GetQuantity() # " " #
        // Item.GetName(), "SelectItem", Item)` passes an already-resolved
        // literal string -- same dynamically-typed-first-argument shape
        // AddButton()/AddFloatingText() already handle, reused here.
        SetRowTextFromArg(row, args[0]);
        // Real 3-arg form (associatedObject) carries a per-row associated
        // object, read back via MenuItemHandle::GetAssociatedObject().
        if (args.entries() == 3) row.associatedObject = args[2].obj();
        row.widget.reset(new MenuItemHandle(*this, m_Rows.size() - 1));
        returnValue = skRValue(static_cast<skiExecutable*>(row.widget.get()), false);
        return true;
    }
    // M28: the real Options screen's two volume rows,
    // `AddMenuSlider(labelId, callbackName, maxValue, step)` -- the only
    // AddMenuSlider call sites in the whole corpus (options.s). These
    // soft-failed before, so the rows simply didn't exist and the Options
    // screen had no volume controls at all. See slider_executable.h.
    if (methodName == skString("AddMenuSlider") && args.entries() >= 2) {
        MenuRow& row = AddRow(RowKind::Slider, args[0].intValue(), "", true);
        int maxValue = args.entries() >= 3 ? args[2].intValue() : 100;
        int step = args.entries() >= 4 ? args[3].intValue() : 10;
        auto* slider = new SliderExecutable(ToStdString(args[1].str()), maxValue, step);
        row.widget.reset(slider);
        // Seed the row from the engine's current gain so reopening
        // Options shows where the sliders were actually left, rather than
        // snapping both back to full.
        if (sk::AudioEngine* audio = m_Stack.audio()) {
            int percent = slider->callback() == "MusicSlider" ? audio->musicVolumePercent()
                                                               : audio->sfxVolumePercent();
            slider->SetValue(percent * slider->maxValue() / 100);
        }
        returnValue = skRValue(static_cast<skiExecutable*>(row.widget.get()), false);
        return true;
    }
    // Same screen, same "previously soft-failed" story. options.s branches
    // on both: GetLanguageStr() gates the God Mode row (`GameActive() and
    // langStr = "ENGLISH"`) and also fills the language popup's first,
    // non-selectable line; MuteOnCall() picks between the Mute On/Mute Off
    // row. This port ships the English stringtable (stringtable.eng), so
    // "ENGLISH" is the honest answer rather than a guess.
    if (methodName == skString("GetLanguageStr") && args.entries() == 0) {
        returnValue = skRValue(skString("ENGLISH"));
        return true;
    }
    if (methodName == skString("MuteOnCall") && args.entries() == 0) {
        returnValue = skRValue(m_Stack.muteOnCall());
        return true;
    }
    if (methodName == skString("SetMuteOnCall") && args.entries() == 1) {
        // Real setting, stored for the round trip options.s expects. This
        // port has no telephony to mute, so nothing consumes it beyond
        // showing the correct Mute On/Mute Off label.
        m_Stack.SetMuteOnCall(args[0].boolValue());
        return true;
    }
    if (methodName == skString("AddComboBox") && args.entries() == 2) {
        MenuRow& row = AddRow(RowKind::ComboBox, 0, "", true);
        row.widget.reset(new ComboBoxExecutable(args[0].intValue(), args[1].intValue()));
        returnValue = skRValue(static_cast<skiExecutable*>(row.widget.get()), false);
        return true;
    }
    if (methodName == skString("AddTextArea") && args.entries() == 2) {
        MenuRow& row = AddRow(RowKind::TextArea, 0, "", true);
        row.widget.reset(
            new TextAreaExecutable(*this, m_Rows.size() - 1, args[0].intValue(), args[1].intValue()));
        returnValue = skRValue(static_cast<skiExecutable*>(row.widget.get()), false);
        return true;
    }
    if (methodName == skString("AddFloatingSprite") && args.entries() == 5) {
        // M25: real signature is AddFloatingSprite(textId, callback, x, y,
        // selectable) -- charactermanager.s's own portrait row
        // (`AddFloatingSprite(0,"",109,0,false)`) is the real motivating
        // case: x/y now stored for real (main.cpp's RenderMenu() draws
        // the real portrait sprite there instead of a bracketed text
        // label), and selectable is honored instead of hardcoded true.
        MenuRow& row = AddRow(RowKind::FloatingSprite, 0, ToStdString(args[1].str()),
                               args[4].boolValue());
        row.x = args[2].intValue();
        row.y = args[3].intValue();
        row.widget.reset(
            new FloatingSpriteExecutable(ToStdString(args[1].str()), args[2].intValue(),
                                          args[3].intValue()));
        returnValue = skRValue(static_cast<skiExecutable*>(row.widget.get()), false);
        return true;
    }
    if (methodName == skString("OpenEditText") && args.entries() == 6) {
        m_TextEntryActive = true;
        AddRow(RowKind::TextEntry, 0, "", true);
        return true;
    }
    if (methodName == skString("ClearMenu") && args.entries() == 0) {
        m_Rows.clear();
        m_SelectedItem = 0;
        m_TitleTextId = -1;
        m_UseHoriz = false;
        m_TextEntryActive = false;
        // Old popups get replaced (their script variable reassigned) or
        // just left unreferenced the next time OnDisplay runs after a
        // ClearMenu -- either way these pointers may already be dangling
        // by the time OnDisplay finishes, so drop them now rather than
        // wait for a fresh CreatePopupMenu() to (maybe) repopulate the
        // list.
        m_KnownPopups.clear();
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
    if (methodName == skString("CreatePopupMenu") && args.entries() >= 4) {
        // M10: real callers pass a 5th trailing bool (inventory.s/
        // charactermanager.s's CreatePopupMenu(x,y,w,h,false)) whose
        // meaning was never RE'd and nothing here currently branches on
        // -- accepted and ignored rather than requiring exactly 4 args.
        auto* popup = new PopupMenuExecutable(*this, args[0].intValue(), args[1].intValue(),
                                               args[2].intValue(), args[3].intValue());
        m_KnownPopups.push_back(popup);
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
    if (methodName == skString("GetPlayer") && args.entries() == 0) {
        returnValue = skRValue(static_cast<skiExecutable*>(&m_Stack.player()), false);
        return true;
    }
    if ((methodName == skString("GetMalePortrait") || methodName == skString("GetFemalePortrait")) &&
        args.entries() == 1) {
        // M25 (corrected): menus/chooseportraitmenu.s's own real
        // `maleId = GetMalePortrait(race); femaleId = GetFemalePortrait(
        // race);` -- `race` is menus/chooseracemenu.s's real combo-box
        // index (0=Argonian, 1=Breton, 2=Dark Elf, 3=High Elf, 4=Khajiit,
        // 5=Nord, 6=Redguard, 7=Wood Elf, matching both its own
        // `AddOption()` order and `PlayerExecutable::ChooseRace()`'s
        // stored value). A user-provided real screenshot showed this
        // port's earlier single-hardcoded-slot-45 stand-in was wrong (the
        // game genuinely has a distinct portrait per race/sex, not one
        // shared face) -- rescanned the whole 384-slot `global.spr` for
        // every 64x64 (the confirmed portrait size) slot and found
        // exactly 16 in one contiguous run, 31-46, rendered and visually
        // matched one-for-one against every real UESP race/sex gallery
        // image (en.uesp.net/wiki/Shadowkey:Races) in `chooseracemenu`'s
        // exact race order, alternating male/female: 31/32=Argonian
        // M/F (reptilian), 33/34=Breton M/F (human), 35/36=Dark Elf M/F
        // (grey skin, red eyes, fangs), 37/38=High Elf M/F (golden
        // skin, blonde), 39/40=Khajiit M/F (feline), 41/42=Nord M/F
        // (pale, blonde/white hair), 43/44=Redguard M/F (dark skin,
        // 44 with real face markings), 45/46=Wood Elf M/F (tan skin,
        // pointy ears -- slot 45 is *specifically* Wood Elf Male, not a
        // generic elf as first assumed). A 17th nearby 64x64 slot (28,
        // outside the 31-46 run) visually looks like another reptilian
        // face but doesn't fit this table's alternating-pairs shape or
        // count -- left unidentified, not used here.
        int race = args[0].intValue();
        if (race < 0 || race > 7) race = 0;  // GetRace()'s own default (m_Race == 0, Argonian)
        int base = 31 + race * 2;
        bool female = methodName == skString("GetFemalePortrait");
        returnValue = skRValue(base + (female ? 1 : 0));
        return true;
    }
    if (methodName == skString("GameAvailableForLoad")) {
        int slot = args.entries() >= 1 ? args[0].intValue() : -1;
        returnValue = skRValue(m_Stack.GameAvailableForLoad(slot));
        return true;
    }
    if (methodName == skString("ActuallySaveGame") && args.entries() == 1) {
        m_Stack.ActuallySaveGame(args[0].intValue());
        TryInvoke("DoneSave");  // no-ops if this menu doesn't define one
        return true;
    }
    if (methodName == skString("DeleteGame") && args.entries() == 1) {
        m_Stack.DeleteGame(args[0].intValue());
        return true;
    }
    if (methodName == skString("DeleteAllGames") && args.entries() == 0) {
        m_Stack.DeleteAllGames();
        return true;
    }
    if (methodName == skString("GetSavedTimeStr") && args.entries() == 1) {
        returnValue = skRValue(skString(m_Stack.GetSavedTimeStr(args[0].intValue()).c_str()));
        return true;
    }
    if (methodName == skString("GetMultiplayerSlot") && args.entries() == 0) {
        returnValue = skRValue(m_Stack.multiplayerSlot());
        return true;
    }
    if (methodName == skString("LoadGame") && args.entries() == 1) {
        // M50: reads the slot's real character.dat back into the player
        // and returns to the level it names -- SavedCharacter::levelName,
        // which is the field the real loader (FUN_1001ea54) copies
        // straight into the engine for exactly this purpose. A slot that
        // will not load falls back to the tutorial zone, which is what
        // this did unconditionally before a save format existed.
        const std::string level = m_Stack.LoadGameFromSlot(args[0].intValue());
        if (level.empty()) {
            m_Stack.RequestGameStart("azra");
        } else {
            // RequestZoneChange, not RequestGameStart: the latter grants
            // the starting inventory on its first call, which would pile
            // a club and a loaf on top of the inventory the save just
            // restored. Same reason M26's LoadLevel uses it.
            m_Stack.RequestZoneChange(level);
        }
        return true;
    }
    if (methodName == skString("NewGame") && args.entries() == 0) {
        m_Stack.RequestGameStart("azra");
        return true;
    }
    if (methodName == skString("NewGameHook") && args.entries() == 0) {
        returnValue = skRValue(0);  // no host process to hand off to -- always single-player
        return true;
    }
    if (methodName == skString("QuitGame") && args.entries() == 0) {
        m_Stack.RequestQuit();
        return true;
    }
    // M54: the other half of that pair, and the one real scripts reach
    // far more often -- GameEngine root binding index 0x33. `QuitGame()`
    // above ends the process; `QuitToMenu()` ends the current game
    // session and returns to mainmenu.s. Seven shipped scripts call it:
    // deathmenu.s's DeathMenuBack (whose `//QuitGame();` sits commented
    // out on the line right above the call that replaced it),
    // gameended.s and mainmenu.s's CancelEndGame, mpdeathmenu.s,
    // saveconfirm.s's MenuDoneSave (the "save first, then quit" chain
    // SetQuitAfterSave arms), and savegamecorrupted.s/savegamenospace.s,
    // which name it as a menu row's handler outright.
    //
    // Two of those -- mainmenu.s and gameended.s -- call something else
    // in the *same* handler immediately afterwards (`QuitToMenu();
    // OnDisplay();` and `QuitToMenu(); Init();`), which only works
    // because the real native does not block: it hands the teardown to a
    // background thread and returns at once. Recording a request and
    // letting the host act on it next tick reproduces that, and is the
    // same shape RequestGameStart()/RequestCloseMenu() already use.
    if (methodName == skString("QuitToMenu") && args.entries() == 0) {
        m_Stack.RequestQuitToMenu();
        return true;
    }
    // M54: the flag that carries that intent across the save screen --
    // GameEngine root bindings 0x37/0x38, see MenuStack::quitAfterSave().
    if (methodName == skString("QuitAfterSave") && args.entries() == 0) {
        returnValue = skRValue(m_Stack.quitAfterSave());
        return true;
    }
    if (methodName == skString("SetQuitAfterSave") && args.entries() == 1) {
        m_Stack.SetQuitAfterSave(args[0].boolValue());
        return true;
    }
    // M51: the two real music-fade bindings (GameEngine indices 11 and
    // 12, FUN_100090bc / FUN_1000915c), four real call sites in the whole
    // corpus: multiplayermenu.s fades the front-end track out on entry
    // and back in on exit, and mainmenu.s and bluetooth.s each un-fade on
    // their own way back.
    //
    // The asymmetry in the real pair is worth keeping: FadeMusic() only
    // records a restore target when the music is *currently audible*, so
    // fading twice, or fading from silence, leaves nothing for
    // UnFadeMusic() to come back to and it does nothing at all.
    if (methodName == skString("FadeMusic") && args.entries() == 0) {
        if (m_Stack.audio()) m_Stack.audio()->FadeMusic();
        return true;
    }
    if (methodName == skString("UnFadeMusic") && args.entries() == 0) {
        if (m_Stack.audio()) m_Stack.audio()->UnFadeMusic();
        return true;
    }
    if (methodName == skString("ShowCredits") && args.entries() == 0) {
        m_Stack.ShowCredits();
        return true;
    }
    if (methodName == skString("GameActive") || methodName == skString("CheatsActivated") ||
        methodName == skString("IsMultiplayer") || methodName == skString("IsMultiplayerClient") ||
        methodName == skString("ArenaActive")) {
        // No game session or multiplayer/cheat state exists yet at this
        // milestone -- a fixed "off" answer is the correct behavior for a
        // freshly booted main menu, not a soft-fail placeholder.
        returnValue = skRValue(false);
        return true;
    }
    if (methodName == skString("UnFadeMusic") || methodName == skString("FadeMusic") ||
        methodName == skString("ClearNewGameHook")) {
        return true;
    }

    // --- M10: buttons/item buttons/tables (charactermanager.s/
    // inventory.s/statsscreen.s/questlog.s) ---
    if (methodName == skString("AddButton") && args.entries() >= 4) {
        std::string callback = args.entries() >= 2 ? ToStdString(args[1].str()) : "";
        MenuRow& row = AddRow(RowKind::MenuItem, -1, callback, true);
        SetRowTextFromArg(row, args[0]);
        // M25: real AddButton(text, callback, x, y) -- x/y stored for real
        // (main.cpp's RenderMenu() places this row at its own real screen
        // position instead of the shared vertical-list cursor); w/h come
        // later via SetWidth()/SetHeight() (ButtonExecutable's own
        // RowOwnerRef writeback).
        row.x = args[2].intValue();
        row.y = args[3].intValue();
        // M36: AddButton's real 6-arg form carries two global.spr slot ids
        // -- the button's normal and highlighted art. Both were being
        // discarded, which is why the inventory/equip screen's category
        // strip was *invisible*: inventory.s builds those five tabs as
        // AddButton("", "WeaponsMenu", x, y, 47, 48) and friends, with an
        // empty label, so with the art dropped there was nothing at all to
        // draw and Left/Right moved a selection the player could not see.
        if (args.entries() >= 6) {
            row.spriteNormal = args[4].intValue();
            row.spriteSelected = args[5].intValue();
        }
        row.widget.reset(new ButtonExecutable(*this, m_Rows.size() - 1));
        returnValue = skRValue(static_cast<skiExecutable*>(row.widget.get()), false);
        return true;
    }
    if (methodName == skString("AddQuitButton") && args.entries() == 2) {
        MenuRow& row =
            AddRow(RowKind::MenuItem, args[0].intValue(), ToStdString(args[1].str()), true);
        row.isQuitButton = true;  // M36: bottom-centred softkey, see MenuRow
        row.widget.reset(new ButtonExecutable(*this, m_Rows.size() - 1));
        returnValue = skRValue(static_cast<skiExecutable*>(row.widget.get()), false);
        return true;
    }
    if (methodName == skString("AddFloatingText") && args.entries() >= 4) {
        std::string callback = args.entries() >= 2 ? ToStdString(args[1].str()) : "";
        bool selectable = args.entries() >= 5 ? args[4].boolValue() : !callback.empty();
        MenuRow& row =
            AddRow(selectable ? RowKind::MenuItem : RowKind::StaticItem, -1, callback, selectable);
        SetRowTextFromArg(row, args[0]);
        // M25: real AddFloatingText(text, callback, x, y, selectable) --
        // charactermanager.s's whole real stat-text column
        // (health/magicka/fatigue/class/level/gold) is built from this.
        row.x = args[2].intValue();
        row.y = args[3].intValue();
        row.widget.reset(new MenuItemHandle(*this, m_Rows.size() - 1));
        returnValue = skRValue(static_cast<skiExecutable*>(row.widget.get()), false);
        return true;
    }
    if (methodName == skString("AddItemButton") && args.entries() == 6) {
        MenuRow& row = AddRow(RowKind::ItemButton, -1, "", true);
        // M25: real AddItemButton(iconId, callback, x, y, w, h) --
        // charactermanager.s's real left/right-hand equip-slot boxes.
        row.x = args[2].intValue();
        row.y = args[3].intValue();
        row.w = args[4].intValue();
        row.h = args[5].intValue();
        row.widget.reset(new ItemButtonExecutable(*this, m_Rows.size() - 1, args[0].intValue(),
                                                    ToStdString(args[1].str()), args[2].intValue(),
                                                    args[3].intValue(), args[4].intValue(),
                                                    args[5].intValue()));
        returnValue = skRValue(static_cast<skiExecutable*>(row.widget.get()), false);
        return true;
    }
    if (methodName == skString("AddTable") && args.entries() == 5) {
        MenuRow& row = AddRow(RowKind::Table, -1, "", true);
        row.widget.reset(new TableExecutable(*this, args[0].intValue(), args[1].intValue(),
                                              args[2].intValue(), args[3].intValue(),
                                              args[4].intValue()));
        // M35: AddTable's real x/y/w/h were handed to the widget but never
        // recorded on the row itself, so the table was the one thing on a
        // hand-laid screen with no position. NavigateDirectional() needs it
        // to know the table sits *below* the category buttons rather than
        // being an unpositioned list row -- without it, Down from the tab
        // strip had nowhere to go.
        row.x = args[1].intValue();
        row.y = args[2].intValue();
        row.w = args[3].intValue();
        row.h = args[4].intValue();
        returnValue = skRValue(static_cast<skiExecutable*>(row.widget.get()), false);
        return true;
    }
    if (methodName == skString("GetLocalizedString") && args.entries() == 1) {
        // charactermanager.s's GetHealthText() etc. build their own
        // display strings by calling this directly, unlike every M3-M5
        // row whose text resolution happens later at render time via its
        // own textId -- needs the real stringtable right here.
        std::string text =
            m_Stack.strings() ? m_Stack.strings()->Get(args[0].intValue()) : "?";
        returnValue = skRValue(skString(text.c_str()));
        return true;
    }
    if (methodName == skString("SetInventoryList") && args.entries() == 1) {
        m_InventoryListTarget = dynamic_cast<TableExecutable*>(args[0].obj());
        return true;
    }
    if ((methodName == skString("DisplayWeaponsPage") || methodName == skString("DisplayArmorMenu") ||
         methodName == skString("DisplayConsumablesMenu") ||
         methodName == skString("DisplayMiscItemsMenu") ||
         methodName == skString("DisplaySpellsPage")) &&
        args.entries() == 1) {
        int itemType = methodName == skString("DisplayWeaponsPage")   ? kItemTypeWeapon
                        : methodName == skString("DisplayArmorMenu")   ? kItemTypeArmor
                        : methodName == skString("DisplayConsumablesMenu") ? kItemTypeConsumable
                        : methodName == skString("DisplaySpellsPage")  ? kItemTypeSpell
                                                                        : kItemTypeMisc;
        // Marks the passed-in category button active and every other
        // known button on this screen inactive (matches inventory.s's own
        // "activeButton=weaponsButton"-style single-active-tab pattern),
        // then repopulates SetInventoryList()'s remembered target table
        // with the real inventory items of this category -- see
        // table_executable.h's PopulateFromInventory().
        auto* active = dynamic_cast<ButtonExecutable*>(args[0].obj());
        for (auto& r : m_Rows) {
            if (auto* btn = dynamic_cast<ButtonExecutable*>(r.widget.get())) {
                btn->SetActive(btn == active);
            }
        }
        if (m_InventoryListTarget) {
            std::vector<ItemExecutable*> filtered;
            for (const auto& item : m_Stack.player().inventory()) {
                if (item->itemType() == itemType) filtered.push_back(item.get());
            }
            m_InventoryListTarget->PopulateFromInventory(filtered);
        }
        return true;
    }
    if (methodName == skString("UpdateEquipStatus") && args.entries() == 2) {
        auto* item = dynamic_cast<ItemExecutable*>(args[0].obj());
        returnValue = skRValue(m_Stack.player().UpdateEquipStatus(item, args[1].boolValue()));
        return true;
    }
    if (methodName == skString("DisplayObjectives") && args.entries() == 1) {
        // No quest-state system exists in this port (a separate,
        // sizeable undertaking -- see docs/PORT_ROADMAP.md's "next
        // milestones") -- a single placeholder row stands in for real
        // objective text so questlog.s renders as a real, non-empty
        // screen rather than throwing or staying blank.
        if (auto* table = dynamic_cast<TableExecutable*>(args[0].obj())) {
            skRValueArray setArgs;
            setArgs.append(skRValue(0));
            setArgs.append(skRValue(0));
            setArgs.append(skRValue(skString("No active quests.")));
            skRValue ret;
            table->method(skString("SetText"), setArgs, ret, context);
        }
        return true;
    }
    if (methodName == skString("TrimText") && args.entries() == 2) {
        std::string text = ToStdString(args[0].str());
        size_t maxLen = static_cast<size_t>(args[1].intValue());
        if (text.size() > maxLen) text = text.substr(0, maxLen);
        returnValue = skRValue(skString(text.c_str()));
        return true;
    }
    if (methodName == skString("GetOpener") && args.entries() == 0) {
        // M21: see this class's SetOpener()/m_Opener comment.
        if (m_Opener) returnValue = skRValue(m_Opener, false);
        return true;
    }
    if (methodName == skString("GetSelectedItem") && args.entries() == 0) {
        if (m_SelectedItem >= 1 && static_cast<size_t>(m_SelectedItem) <= m_Rows.size()) {
            MenuRow& row = m_Rows[static_cast<size_t>(m_SelectedItem - 1)];
            if (row.widget) {
                returnValue = skRValue(static_cast<skiExecutable*>(row.widget.get()), false);
            }
        }
        return true;
    }
    if (methodName == skString("DisplayCharacterManager") && args.entries() == 0) {
        m_Stack.OpenMenu("charactermanager");
        return true;
    }
    if (methodName == skString("DisplayInventory") && args.entries() == 0) {
        m_Stack.OpenMenu("inventory");
        return true;
    }
    if (methodName == skString("DisplayStatsScreen") && args.entries() == 0) {
        m_Stack.OpenMenu("statsscreen");
        return true;
    }
    if (methodName == skString("DisplayQuestLog") && args.entries() == 0) {
        m_Stack.OpenMenu("questlog");
        return true;
    }
    if (methodName == skString("OpenMainMenu") && args.entries() == 0) {
        m_Stack.OpenMenu("MainMenu");
        return true;
    }
    if (methodName == skString("SetLeftActionQueue") && args.entries() == 0) {
        m_QueueHandIsRight = false;
        return true;
    }
    if (methodName == skString("SetRightActionQueue") && args.entries() == 0) {
        m_QueueHandIsRight = true;
        return true;
    }
    if (methodName == skString("IsLeftQueue") && args.entries() == 0) {
        // Decompiled ground truth (FUN_10034d8c case 2): the real native
        // returns the raw hand-selection byte UN-negated -- true exactly
        // when the RIGHT hand was most recently selected, the opposite of
        // what its name says. A genuine naming bug in the shipped binary,
        // reproduced faithfully here; harmless since no real script ever
        // calls IsLeftQueue() (actionqueue.s calls the never-registered
        // "IsRightQueue()" instead, which always soft-fails to false/null
        // -- see actionqueue.s's OnDisplay(), confirmed by this session's
        // decompile that no such native binding exists in the real
        // 702-entry table).
        returnValue = skRValue(m_QueueHandIsRight);
        return true;
    }
    if (methodName == skString("ShowActionQueue") && args.entries() == 0) {
        // Real engine (FUN_10034d8c case 3): resolves the actionqueue.s
        // menu slot, copies the CALLER's hand flag onto it, then actually
        // pushes/opens it. actionqueue.s's own row-population
        // (UpdateTextItems()/GetLastItem()) isn't a registered native at
        // all in the real binary, so the screen it opens genuinely never
        // shows real item names or wires up DropItem/ItemUp/ItemDown to a
        // real object in the shipped game either -- this port reproduces
        // that same (non-functional) screen rather than inventing a
        // working one, faithful to the real, confirmed-broken behavior.
        m_Stack.OpenMenu("actionqueue");
        if (auto* target = dynamic_cast<MenuExecutable*>(m_Stack.currentMenu())) {
            target->SetQueueHandIsRight(m_QueueHandIsRight);
        }
        return true;
    }
    if (methodName == skString("ShowRemovedQueue") && args.entries() == 0) {
        // Decompiled ground truth (FUN_10034d8c case 4): unlike
        // ShowActionQueue() above, this resolves the removequeue.s slot
        // but never calls the "copy flag + actually open" step -- a real,
        // confirmed dead native in the shipped binary. Matches
        // charactermanager.s's own DisplayRemoved() handler being
        // unreachable too (its only popup entry is commented out). No-op,
        // faithfully.
        return true;
    }

    if (skScriptedExecutable::method(methodName, args, returnValue, context)) {
        return true;
    }
    return SoftFailNativeCall("Menu", methodName, args, returnValue);
}

}  // namespace sk_bindings
