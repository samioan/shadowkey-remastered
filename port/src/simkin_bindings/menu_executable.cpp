#include "simkin_bindings/menu_executable.h"

#include <cstdio>

#include "assets/string_table.h"
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
    skRValueArray args;
    skRValue ret;
    skExecutableContext ctxt(&m_Stack.interpreter());
    method(skString("OnDisplay"), args, ret, ctxt);
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
    for (size_t i = 0; i < selectableIndices.size(); ++i) {
        if (static_cast<int>(selectableIndices[i]) + 1 == m_SelectedItem) {
            currentPos = i;
            break;
        }
    }
    int count = static_cast<int>(selectableIndices.size());
    int nextPos = (static_cast<int>(currentPos) + delta % count + count) % count;
    m_PrevSelectedItem = m_SelectedItem;
    m_SelectedItem = static_cast<int>(selectableIndices[static_cast<size_t>(nextPos)]) + 1;
}

void MenuExecutable::CycleSelectedCombo(int delta) {
    if (m_SelectedItem < 1 || static_cast<size_t>(m_SelectedItem) > m_Rows.size()) return;
    MenuRow& row = m_Rows[static_cast<size_t>(m_SelectedItem - 1)];
    if (row.kind != RowKind::ComboBox) return;
    auto* combo = static_cast<ComboBoxExecutable*>(row.widget.get());
    combo->CycleSelection(delta);
    TryInvoke(combo->onChangeCallback());
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

void MenuExecutable::TryInvoke(const std::string& handlerName) {
    if (handlerName.empty()) return;
    skRValueArray args;
    skRValue ret;
    skExecutableContext ctxt(&m_Stack.interpreter());
    // Calls the base class directly (not this->method()) so an
    // undefined handler name just quietly does nothing instead of
    // spamming the soft-fail log for what's an optional hook, not an
    // unresolved native call.
    skScriptedExecutable::method(skString(handlerName.c_str()), args, ret, ctxt);
}

bool MenuExecutable::method(const skString& methodName, skRValueArray& args,
                             skRValue& returnValue, skExecutableContext& context) {
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
        // M25: menus/chooseportraitmenu.s's own real
        // `maleId = GetMalePortrait(race); femaleId = GetFemalePortrait(
        // race);` -- real per-race/sex portrait art almost certainly
        // exists (`global.spr`'s menu-icon range, docs/GRAPHICS_FORMAT.md,
        // has room for more than the one slot identified so far), but
        // only slot 45 (64x64, a real elf face, visually confirmed) is
        // actually pinned to "portrait" -- returned for every race/sex
        // until more variants are identified, a documented simplification
        // rather than an invented mapping.
        returnValue = skRValue(45);
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
        // No real save-file format exists to read a different zone/player
        // state from yet (M5's save system is in-memory only) -- this
        // just re-enters the same tutorial zone LoadGame() would in a
        // real fresh save.
        m_Stack.RequestGameStart("azra");
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
        row.widget.reset(new ButtonExecutable(*this, m_Rows.size() - 1));
        returnValue = skRValue(static_cast<skiExecutable*>(row.widget.get()), false);
        return true;
    }
    if (methodName == skString("AddQuitButton") && args.entries() == 2) {
        MenuRow& row =
            AddRow(RowKind::MenuItem, args[0].intValue(), ToStdString(args[1].str()), true);
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
