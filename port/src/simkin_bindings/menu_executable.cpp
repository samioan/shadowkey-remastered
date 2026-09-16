#include "simkin_bindings/menu_executable.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "assets/string_table.h"
#include "assets/zone_display_names.h"
#include "audio/audio_engine.h"
#include "engine/input_state.h"
#include "simkin_bindings/action_text.h"
#include "simkin_bindings/button_executable.h"
#include "simkin_bindings/combo_box_executable.h"
#include "simkin_bindings/floating_sprite_executable.h"
#include "simkin_bindings/floating_text_executable.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/item_button_executable.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/language.h"
#include "simkin_bindings/menu_item_handle.h"
#include "simkin_bindings/native_binding_common.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/popup_menu_executable.h"
#include "simkin_bindings/quest_table.h"
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

// M60: the store table's two cost-column formats, verbatim from the
// image -- "%s: %d %s: %d" at 0x100aea88 and "%s: %d" at 0x100aea9c.
std::string FormatTwoLabels(const std::string& a, int av, const std::string& b, int bv) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s: %d %s: %d", a.c_str(), av, b.c_str(), bv);
    return buf;
}

std::string FormatOneLabel(const std::string& a, int av) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s: %d", a.c_str(), av);
    return buf;
}

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
    // `FUN_100779b8` sets `menu+0x50 = 0x14` on the way in, *before* the
    // script gets to run -- so a screen that calls `MenuBackground(69)`
    // still ends up on 69, and one that calls nothing lands on the
    // parchment rather than on nothing at all. Re-armed here rather than
    // only at construction because this port caches menu instances and
    // `ReopenMenu` runs Init again on the same object.
    m_BackgroundId = kDefaultMenuBackground;
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

// M80: `FUN_1007dca0`'s wrap, variable for variable. `start` is the
// current line's first character, `lastSpace` the most recent space seen
// (the engine's iVar4, which it also uses as the cut point), `run` its
// iVar7 -- the count since the last cut, which is what gets compared
// against the limit. The engine mutates the caller's buffer in place,
// writing a NUL over each cut space; this builds substrings instead,
// which is the same set of lines.
std::vector<std::string> WrapMenuText(const std::string& text, int maxChars) {
    std::vector<std::string> lines;
    if (static_cast<int>(text.size()) < maxChars || maxChars <= 0) {
        lines.push_back(text);
        return lines;
    }
    std::size_t start = 0;
    std::size_t lastSpace = 0;
    int run = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        std::size_t nextLastSpace = lastSpace;
        if (text[i] == ' ') {
            nextLastSpace = i;
            if (run > maxChars) {
                lines.push_back(text.substr(start, lastSpace - start));
                start = lastSpace + 1;
                run = static_cast<int>(i - lastSpace);
            }
        }
        ++run;
        lastSpace = nextLastSpace;
    }
    // The tail: one more cut if the last line is still over the limit and
    // there was a space to cut at (`if ((param_4 < iVar7) && (iVar4 != 0))`).
    if (run > maxChars && lastSpace != 0 && lastSpace >= start) {
        lines.push_back(text.substr(start, lastSpace - start));
        start = lastSpace + 1;
    }
    lines.push_back(text.substr(start));
    return lines;
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

// ---- M99: the key-configuration screen ----

namespace {

// The stringtable ids FUN_100780f8/FUN_10078890/FUN_10078c08 read, each as
// `engine->stringTable[id]` at a literal offset (0x3f00 / 4 = 4032, ...).
constexpr int kTextNextPage = 4032;         // "Next Page"
constexpr int kTextPrevPage = 4033;         // "Previous Page"
constexpr int kTextResetDefaults = 4034;    // "Reset to Defaults"
constexpr int kTextBackToOptions = 4035;    // "Back to Options"
constexpr int kTextRedefine = 4036;         // "Redefine"
constexpr int kTextBackToKeyConfig = 4037;  // "Back to Key Config"
constexpr int kTextCurrentDefinition = 4049;  // "Current Definition:"
constexpr int kTextRedefining = 4050;       // ": Redefining :"
constexpr int kTextPressAKey = 4051;        // "Press a key to use"
constexpr int kTextForThisAction = 4052;    // "for this action."

constexpr int kActionsPerPage = 5;
// `if (0x10 < last) last = 0xf` -- the last action a page can list.
constexpr int kLastConfigurableAction = 0xf;

// Every blank line the three builders add is the one-space literal at
// 0x100b33c8.
const char* const kBlankLine = " ";

}  // namespace

void MenuExecutable::ClearNativeRows() {
    // `for each widget in menu+0x34: delete; list.clear(); +0x30 = 0;
    // +0x40 = 0` -- the same teardown ClearMenu's native does, without
    // ClearMenu's extras (title, text entry, popups), none of which the
    // engine's list loop touches either.
    m_Rows.clear();
    m_SelectedItem = 0;
}

MenuExecutable::MenuRow& MenuExecutable::AddNativeStatic(int textId, const std::string& literal) {
    MenuRow& row = AddRow(RowKind::StaticItem, textId, "", false);
    row.literalText = literal;
    row.centered = true;
    return row;
}

MenuExecutable::MenuRow& MenuExecutable::AddNativeItem(int textId, const std::string& callback) {
    MenuRow& row = AddRow(RowKind::MenuItem, textId, callback, true);
    row.widget.reset(new MenuItemHandle(*this, m_Rows.size() - 1));
    return m_Rows.back();
}

void MenuExecutable::SelectFirstSelectableRow() {
    // `if (menu+0x40 == 0 && widget+0x5c) menu+0x40 = widget` on every add:
    // the first row that was selectable at the moment it was added.
    for (size_t i = 0; i < m_Rows.size(); ++i) {
        if (m_Rows[i].selectable) {
            m_SelectedItem = static_cast<int>(i) + 1;
            return;
        }
    }
}

void MenuExecutable::BuildConfigKeysPage(int page) {
    ClearNativeRows();
    m_ConfigKeysPage = page;
    const int first = (page - 1) * kActionsPerPage;
    int last = first + kActionsPerPage - 1;
    if (last > 0x10) last = kLastConfigurableAction;
    for (int action = first; action <= last; ++action) {
        // FUN_1001a5c4 names the action. The builder then compares that name
        // against the literal L"Rechten Gegenstand benutzen" and, on a match,
        // builds a wrapping item followed by four blank lines. No shipped
        // table contains the string -- German's action 15 reads "Rechte
        // Aktion benutzen" -- so the branch is dead in this build and not
        // reproduced.
        AddNativeItem(sk::InputState::actionNameStringId(static_cast<sk::Action>(action)),
                      "ConfigKeySelected");
        if (action == first) m_ConfigKeysFirstActionRow = static_cast<int>(m_Rows.size()) - 1;
    }
    AddNativeStatic(-1, kBlankLine);
    if (last < kLastConfigurableAction) {
        AddNativeItem(kTextNextPage, "NextPage");
    } else {
        AddNativeStatic(-1, kBlankLine);
    }
    if (first == 0) {
        AddNativeStatic(-1, kBlankLine);
    } else {
        AddNativeItem(kTextPrevPage, "PrevPage");
    }
    AddNativeItem(kTextResetDefaults, "DefaultKeys");
    AddNativeItem(kTextBackToOptions, "OptionsMenu");
    SelectFirstSelectableRow();
}

void MenuExecutable::BuildKeyDefinition(int action) {
    ClearNativeRows();
    const bool known = action >= 0 && action < 16;
    const sk::Action a = static_cast<sk::Action>(action);
    AddNativeStatic(known ? sk::InputState::actionNameStringId(a) : -1);
    AddNativeStatic(-1, kBlankLine);
    AddNativeStatic(kTextCurrentDefinition);
    // The key's name, through `InputState_ResolveBindingOffset` and then the
    // slot's label (FUN_1001a578) -- a row built as an item and then made
    // unselectable, with an unidentified byte `+0x34 = 1` besides. No
    // callback.
    int keyName = -1;
    if (known) {
        if (const sk::InputState* input = m_Stack.input()) {
            keyName = input->bindingNameStringId(a);
        } else {
            keyName = sk::InputState().bindingNameStringId(a);
        }
    }
    MenuRow& key = AddNativeItem(keyName, "");
    key.selectable = false;
    key.centered = true;
    AddNativeStatic(-1, kBlankLine);
    AddNativeStatic(-1, kBlankLine);
    AddNativeItem(kTextRedefine, "Redefine");
    AddNativeStatic(-1, kBlankLine);
    AddNativeItem(kTextBackToKeyConfig, "BackToConfig");
    m_ConfigKeysAction = action;
    SelectFirstSelectableRow();
}

void MenuExecutable::BuildRedefine() {
    ClearNativeRows();
    // Four of these lines are made selectable after they are added
    // (`widget+0x5c = 1`), which leaves the selection on the action's name
    // -- the only one selectable when it went in. Nothing reads either:
    // the capture mode below takes the tick before any navigation.
    AddNativeStatic(kTextRedefining).selectable = true;
    const bool known = m_ConfigKeysAction >= 0 && m_ConfigKeysAction < 16;
    int nameId = known ? sk::InputState::actionNameStringId(
                             static_cast<sk::Action>(m_ConfigKeysAction))
                       : -1;
    AddNativeItem(nameId, "");
    AddNativeStatic(-1, kBlankLine);
    AddNativeStatic(-1, kBlankLine);
    AddNativeStatic(kTextPressAKey).selectable = true;
    AddNativeStatic(kTextForThisAction).selectable = true;
    AddNativeStatic(-1, kBlankLine);
    m_SelectedItem = 2;
    m_AwaitingKey = true;
    m_KeyReleased = false;
}

void MenuExecutable::TickKeyCapture(sk::InputState& input) {
    if (!m_AwaitingKey) return;
    if (!m_KeyReleased) {
        if (!input.GetButton(sk::ButtonSlot::Key5)) m_KeyReleased = true;
        input.ClearPendingEdges();
        return;
    }
    if (input.ConsumeJustPressed(sk::ButtonSlot::RightSelectionKey)) {
        m_AwaitingKey = false;
        input.ClearPendingEdges();
        InvokeCallback("ConfigKeysBack");
        return;
    }
    for (int slot = 0; slot < static_cast<int>(sk::ButtonSlot::kCount); ++slot) {
        const sk::ButtonSlot s = static_cast<sk::ButtonSlot>(slot);
        if (!input.GetButton(s) || !sk::InputState::slotIsBindable(s)) continue;
        input.RedefineBinding(static_cast<sk::Action>(m_ConfigKeysAction), s);
        m_AwaitingKey = false;
        input.ClearPendingEdges();
        InvokeCallback("BackToConfig");
        return;
    }
    input.ClearPendingEdges();
}

void MenuExecutable::SetRowSelectable(size_t rowIndex, bool selectable) {
    if (rowIndex < m_Rows.size()) m_Rows[rowIndex].selectable = selectable;
}

void MenuExecutable::SetWidgetSelectable(const skiExecutable* widget, bool selectable) {
    for (MenuRow& row : m_Rows) {
        if (row.widget.get() == widget) {
            row.selectable = selectable;
            return;
        }
    }
}

// M95: `SetItemText` and `SetLocalizedText` write one slot, so each has to
// displace the other. They did not: a row built from a literal kept drawing
// it after a `SetLocalizedText` (the renderer prefers a literal), which is
// why every store screen was titled "(Weapons)" -- `buysell.s` builds its
// title as `AddFloatingText("(Weapons)", ...)` and each page handler then
// retitles it with `SetLocalizedText(2990..2994)`. Found through the God
// Vendor's armour page.
void MenuExecutable::SetRowTextId(size_t rowIndex, int textId) {
    if (rowIndex >= m_Rows.size()) return;
    m_Rows[rowIndex].textId = textId;
    m_Rows[rowIndex].literalText.clear();
}

void MenuExecutable::SetRowLiteralText(size_t rowIndex, const std::string& text) {
    if (rowIndex >= m_Rows.size()) return;
    m_Rows[rowIndex].literalText = text;
    m_Rows[rowIndex].textId = -1;
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

void MenuExecutable::SetRowX(size_t rowIndex, int x) {
    if (rowIndex < m_Rows.size()) m_Rows[rowIndex].x = x;
}

void MenuExecutable::SetRowY(size_t rowIndex, int y) {
    if (rowIndex < m_Rows.size()) m_Rows[rowIndex].y = y;
}

void MenuExecutable::SetRowVisible(size_t rowIndex, bool visible) {
    if (rowIndex < m_Rows.size()) m_Rows[rowIndex].visible = visible;
}

std::string MenuExecutable::ResolveText(int textId) const {
    const sk::StringTable* strings = m_Stack.strings();
    return strings ? strings->Get(textId) : std::string("?");
}

int MenuExecutable::PlayerCharacterClass() const {
    return m_Stack.player().characterClass();
}

PlayerExecutable& MenuExecutable::stackPlayer() const { return m_Stack.player(); }

LevelExecutable& MenuExecutable::stackLevel() const { return m_Stack.level(); }

int MenuExecutable::ArmorSlotOfTemplate(int templateId) const {
    const sk::ProductRecord* record = m_Stack.products().Find(templateId);
    return record ? record->armorSlot : -1;
}

ItemExecutable* MenuExecutable::EquippedArmorInSlot(int slot) const {
    if (slot < 0) return nullptr;
    for (const std::unique_ptr<ItemExecutable>& held : m_Stack.player().inventory()) {
        if (!held || held->markedForRemoval() || !held->equipped()) continue;
        if (held->itemType() != kItemTypeArmor) continue;
        if (ArmorSlotOfTemplate(held->templateId()) == slot) return held.get();
    }
    return nullptr;
}

bool MenuExecutable::PlayerOwnsTemplate(int templateId) const {
    for (const std::unique_ptr<ItemExecutable>& held : m_Stack.player().inventory()) {
        if (held && !held->markedForRemoval() && held->templateId() == templateId) return true;
    }
    return false;
}

// FUN_10032f78. The whole store/inventory table, three modes deep.
//
// The two label strings are the engine's own: id 3039 is "GP " (with the
// trailing space already in the shipped data, so the rendered line really
// does read "GP : 111") and 3040 is "Qty". The formats are literally
// "%s: %d %s: %d" and "%s: %d".
void MenuExecutable::PopulatePage(int category, bool clear) {
    m_CurrentPage = category;  // menu+0xcc, before anything can fail
    TableExecutable* table = m_InventoryListTarget;
    if (!table) return;  // the real function panics; nothing else to do here
    if (clear) table->ClearCells();

    const sk::StringTable* strings = m_Stack.strings();
    const std::string gpLabel = strings ? strings->Get(kStoreGoldLabelStringId) : "GP ";
    const std::string qtyLabel = strings ? strings->Get(kStoreQuantityLabelStringId) : "Qty";
    PlayerExecutable& player = m_Stack.player();

    int used = 0;
    if (m_ScreenMode == ScreenMode::Buy) {
        Store* merchant = player.merchant();
        if (!merchant) {
            // The real branch is guarded on the merchant pointer too, and
            // simply leaves the table alone when there is none.
            return;
        }
        const std::vector<Store::StockEntry>& stock = merchant->stock();
        // The real growth step asks for the *whole* stock count, not the
        // filtered one -- so the allocation is sized by everything the
        // merchant sells and the used-row count by this category alone.
        const int total = static_cast<int>(stock.size());
        if (table->allocatedRows() < total) table->AddRows(total - table->allocatedRows());
        for (const Store::StockEntry& entry : stock) {
            if (!entry.record || entry.record->category != category) continue;
            const size_t row = static_cast<size_t>(used);

            TableCell& name = table->CellAt(row, 0);
            name.text = strings ? strings->Get(entry.record->nameStringId) : std::string();
            name.product = entry.record;
            name.item = nullptr;
            // classTinted stays at its constructor default (true) -- the
            // name column is the one the class-restriction tint applies
            // to, and the real code sets the flag on nothing.

            TableCell& cost = table->CellAt(row, 1);
            cost.text = FormatTwoLabels(gpLabel, entry.price, qtyLabel, entry.quantity);
            cost.product = entry.record;
            cost.classTinted = false;

            // The two 10px columns buysell.s calls "rating left"/"rating
            // right": comparison arrows, left hand first.
            TableCell& left = table->CellAt(row, 2);
            left.text = " ";
            left.product = entry.record;
            left.comparison = true;
            left.compareLeftHand = true;
            left.classTinted = false;

            TableCell& right = table->CellAt(row, 3);
            right.text = " ";
            right.product = entry.record;
            right.comparison = true;
            right.compareLeftHand = false;
            right.classTinted = false;

            ++used;
        }
        table->SetUsedRows(used);
        return;
    }

    // Sell and plain Inventory both walk the player's own item list.
    for (const std::unique_ptr<ItemExecutable>& held : player.inventory()) {
        ItemExecutable* item = held.get();
        if (!item || item->markedForRemoval() || item->itemType() != category) continue;
        const size_t row = static_cast<size_t>(used);
        if (table->allocatedRows() <= used) table->AddRows(1);

        TableCell& name = table->CellAt(row, 0);
        name.text = item->name();
        name.item = item;
        name.classTinted = false;

        if (m_ScreenMode == ScreenMode::Inventory) {
            // The real inventory page's second column is the derived stat
            // line (FUN_100a0cec: a weapon's damage range, armour's AV, a
            // spell's magicka cost) -- and it is skipped entirely for
            // category 0, which is why the Misc page is a bare name list.
            if (category != kItemTypeMisc) {
                TableCell& stat = table->CellAt(row, 1);
                stat.text = " ";
                stat.item = item;
                stat.statLine = true;
                stat.classTinted = false;
            }
        } else {
            TableCell& cost = table->CellAt(row, 1);
            // Only the Consumable page shows a count -- everything else
            // is one line per object, so there is nothing to count.
            cost.text = category == kItemTypeConsumable
                            ? FormatTwoLabels(gpLabel, item->marketValue(), qtyLabel,
                                              item->quantity())
                            : FormatOneLabel(gpLabel, item->marketValue());
            cost.item = item;
            cost.classTinted = false;

            TableCell& left = table->CellAt(row, 2);
            left.text = " ";
            left.item = item;
            left.comparison = true;
            left.compareLeftHand = true;
            left.classTinted = false;

            TableCell& right = table->CellAt(row, 3);
            right.text = " ";
            right.item = item;
            right.comparison = true;
            right.compareLeftHand = false;
            right.classTinted = false;
        }
        ++used;
    }
    table->SetUsedRows(used);
}

void MenuExecutable::MoveSelection(int delta) {
    std::vector<size_t> selectableIndices;
    for (size_t i = 0; i < m_Rows.size(); ++i) {
        // M60: a hidden row is unreachable. SetVisible() does not clear
        // the selectable byte in the engine -- it is the focus-wiring
        // pass (FUN_10033c88) that skips invisible widgets -- so the
        // two fields stay separate here and navigation tests both.
        if (m_Rows[i].selectable && m_Rows[i].visible) selectableIndices.push_back(i);
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
        if (!m_Rows[i].selectable || !m_Rows[i].visible || m_Rows[i].y < 0) continue;
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
        m_Rows[static_cast<size_t>(m_SelectedItem - 1)].selectable &&
        m_Rows[static_cast<size_t>(m_SelectedItem - 1)].visible) {
        return;
    }
    for (size_t i = 0; i < m_Rows.size(); ++i) {
        if (m_Rows[i].selectable && m_Rows[i].visible) {
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

bool MenuExecutable::enterDelayed(std::time_t now) const { return now < m_EnterAllowedAt; }

void MenuExecutable::ActivateSelected() {
    // M104: `FUN_10032868`'s own first guard -- the row's callback does not
    // run while `time() < menu+0xc8`. See the DelayOnEnter handler.
    if (enterDelayed(std::time(nullptr))) return;
    if (m_SelectedItem < 1 || static_cast<size_t>(m_SelectedItem) > m_Rows.size()) return;
    MenuRow& row = m_Rows[static_cast<size_t>(m_SelectedItem - 1)];
    switch (row.kind) {
        // M91: InvokeCallback, not TryInvoke -- see its declaration. A row
        // callback naming a native (`AddMenuItem(3747, "Quit")`) is
        // ordinary Simkin and the corpus is full of it.
        case RowKind::MenuItem:
            InvokeCallback(row.callback);
            break;
        case RowKind::ComboBox:
            InvokeCallback(static_cast<ComboBoxExecutable*>(row.widget.get())->onEnterCallback());
            break;
        case RowKind::FloatingSprite:
            InvokeCallback(static_cast<FloatingSpriteExecutable*>(row.widget.get())->callback());
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
    // M103: the engine's own message popup is a popup like any other for
    // input and rendering -- `SetVisible` parks it in the same `menu+0x40`
    // active slot the script-created ones use -- and it is checked first
    // because DisplayPopup() is what just put it up.
    if (m_MessagePopup && m_MessagePopup->visible()) return m_MessagePopup.get();
    for (PopupMenuExecutable* popup : m_KnownPopups) {
        if (popup->visible()) return popup;
    }
    return nullptr;
}

void MenuExecutable::DisplayMessagePopup(const std::string& text) {
    if (!m_MessagePopup) {
        m_MessagePopup = std::make_unique<PopupMenuExecutable>(*this, 0, 0, 0, 0);
        m_MessagePopup->MakeMessagePopup();
    }
    m_MessagePopup->SetMessage(text);
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

bool MenuExecutable::InvokeCallback(const std::string& callbackName) {
    // See the declaration for why a row callback goes through more than
    // TryInvoke() does. method() is this object's full dispatch, so
    // calling it directly would also soft-fail-log an unresolved name --
    // which is exactly what should happen here (a row wired to nothing
    // is a bug worth seeing, and it is how the corpus's genuinely dead
    // rows, `MenuQuit`/`MenuBack`/`ExitMenu`, announce themselves).
    if (callbackName.empty()) return false;
    skRValueArray args;
    skRValue ret;
    skExecutableContext ctxt(&m_Stack.interpreter());
    skString name(callbackName.c_str());
    if (skScriptedExecutable::method(name, args, ret, ctxt)) return true;
    return method(name, args, ret, ctxt);
}

bool MenuExecutable::TryInvokeWithArg(const std::string& handlerName, const skRValue& arg) {
    if (handlerName.empty()) return false;
    skRValueArray args;
    args.append(arg);
    skRValue ret;
    skExecutableContext ctxt(&m_Stack.interpreter());
    // Same quiet path as TryInvoke, with the one argument the real
    // table/row callbacks take ("SelectedItem[ (cell) ...").
    return skScriptedExecutable::method(skString(handlerName.c_str()), args, ret, ctxt);
}

bool MenuExecutable::GoBack() {
    // The real back/cancel softkey, in the order the evidence supports.
    //
    // 0. `<ScriptName>Back` -- M90, and the one the *generic menu class*
    //    actually calls. `FUN_10075bdc` (the menu screen's input tick)
    //    routes the right softkey to `FUN_100768b4`, whose whole body is:
    //    take the screen's own name from `menu+0xc`, `sprintf("%sBack")`
    //    (the format string is at `0x100b32c8`), call that script method.
    //    It explains a family of handlers this port had no caller for --
    //    41 of them across the corpus, one per screen, and their names
    //    track the *open path's* last component exactly, case and all:
    //    `LockPickDoorBack`, `LootMenuBack`, `skeleton_key_menuBack`,
    //    `armor_convoBack`, and -- the one M90 needed -- levelconfirm.s's
    //    `LevelConfirmBack`, which is the only way to decline a trip with
    //    the back key rather than the "Don't Go" row.
    // 1. The screen's own handler, if it has one. The corpus spells the
    //    name both "OnRightSoftkey" and "OnRightSoftKey" depending on the
    //    file (a genuine authoring inconsistency in the original scripts,
    //    not something to "fix"), so both are tried. `OnRightSoftKey` is
    //    a real dispatch too, just a different screen class's:
    //    `FUN_1003006c` (the character-manager/store family) calls it
    //    where the generic class calls `<ScriptName>Back`. This port has
    //    one screen class, so it tries both.
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
    // Step 0 keeps the same "did it actually do something" guard the rest
    // of this function uses, rather than winning outright on the strength
    // of merely existing. Two of the 41 are deliberately conditional --
    // `mainmenu.s`'s `MainMenuBack` closes the front end only `if(
    // GameActive() )` -- and for those the pre-M90 chain below is still
    // what gets the player off the screen.
    bool handled = !m_ScriptName.empty() && TryInvoke(m_ScriptName + "Back");
    // M101: a QuitToMenu() counts too. `deathmenu.s`'s DeathMenuBack is
    // exactly that, and without it the chain carried on into the screen's
    // OnRightSoftkey -- a `MenuQuit()` no native answers -- on top of the
    // teardown it had already asked for.
    if (handled && (m_Stack.currentMenu() != before || m_Stack.closeMenuRequested() ||
                    m_Stack.quitRequested() || m_Stack.gameStartRequested() ||
                    m_Stack.quitToMenuRequested())) {
        return true;
    }
    handled = TryInvoke("OnRightSoftkey") || TryInvoke("OnRightSoftKey") || handled;
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
        // M60: `mode == 1`, exactly. The sell page and the plain
        // inventory page both answer false and are still different pages.
        returnValue = skRValue(m_ScreenMode == ScreenMode::Buy);
        return true;
    }
    if (methodName == skString("MenuBackground") && args.entries() == 1) {
        m_BackgroundId = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetStartCoord") && args.entries() == 1) {
        // M64: a bare call in `levelup.s`, and its only one in the corpus
        // -- see startCoord()'s comment for what menu+0x96 does.
        m_StartCoord = static_cast<int16_t>(args[0].intValue());
        return true;
    }
    if (methodName == skString("AddTitle") && args.entries() >= 1) {
        // M88: real, plural, and positioned. questlog.s calls this twice
        // with explicit y positions (`AddTitle(3785,10)`,
        // `AddTitle(3786,25)`); the old single-slot version kept only the
        // second and drew it at the shared layout cursor, which on that
        // screen is where the first quest's title goes -- "Use 'key 5' to
        // exit" printed straight through "Rat Quest". See
        // MenuExecutable::MenuTitle.
        MenuTitle title;
        title.textId = args[0].intValue();
        if (args.entries() >= 2) title.y = args[1].intValue();
        m_Titles.push_back(title);
        m_TitleTextId = title.textId;
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
        // M99: case 0x32 opens with `if (!engine->saveDisabled)
        // FUN_10019c04()`, result unused -- every screen that closes writes
        // the settings file. That is how an Options change or a rebinding
        // survives without the player ever seeing a "save" row.
        if (!m_Stack.configSaveDisabled()) m_Stack.SaveConfig();
        // See MenuStack::closeMenuRequested(). Deliberately does not
        // navigate here: a real Quit() is often followed immediately by an
        // OpenMenu(...) in the same handler (charactermanager.s's
        // OnRightSoftKey does exactly that), and acting instantly would
        // fight that second call.
        m_Stack.RequestCloseMenu();
        // M95: the flag half of the real Quit does happen on the spot --
        // see MenuStack::menuActive().
        m_Stack.SetMenuActive(false);
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
        // M80 -- three things this had wrong, all of them from the same
        // never-read handler (`FUN_10078de4` case 0x6c ->
        // `FUN_1007dca0`), and all three of them are why the game's
        // tutorial popups could not have worked:
        //
        //  1. **The argument is dynamically typed.** The case opens with
        //     `if (*(char *)(arg0 + 4) == '')` -- the Simkin
        //     string-vs-int discriminator -- and only resolves a
        //     stringtable id down the other arm. `starthelp.s` passes
        //     `ParseActionText(2996)`, an already-resolved string;
        //     `.intValue()` on that is 0, i.e. the wrong row entirely.
        //  2. **The second argument is an alignment, not "selectable".**
        //     See MenuRow::centered. With one argument it defaults to 1
        //     (left-aligned) unless the text is at least four characters
        //     long and starts with "---" -- the `-----` separator id 179
        //     that half the corpus puts between a message and its
        //     buttons, which centres instead.
        //  3. **A static item word-wraps into several rows.**
        //     `FUN_1007dca0` builds one widget per line: it walks the
        //     string, and at each space whose running line length has
        //     passed the limit it terminates the line at the *previous*
        //     space and starts a new widget. The limit is the call's
        //     fourth argument (default 0x11 = 17), except that the
        //     left-aligned arm overrides it outright --
        //     `if (param_3 != 0) param_4 = 0x19` -- so **every ordinary
        //     message wraps at 25 characters**. Nothing in this port
        //     wrapped at all, so a 170-character tutorial paragraph drew
        //     as a single line running off both edges of a 176px screen.
        MenuRow prototype;
        SetRowTextFromArg(prototype, args[0]);
        std::string text = prototype.textId >= 0 && m_Stack.strings()
                               ? m_Stack.strings()->Get(prototype.textId)
                               : prototype.literalText;
        bool leftAligned = true;
        if (args.entries() >= 2) {
            leftAligned = args[1].boolValue();
        } else if (text.size() > 3 && text.compare(0, 3, "---") == 0) {
            leftAligned = false;
        }
        int wrapChars = args.entries() >= 3 ? args[2].intValue() : kStaticItemWrapChars;
        if (leftAligned) wrapChars = kLeftAlignedWrapChars;
        for (const std::string& line : WrapMenuText(text, wrapChars)) {
            // Each line carries its own already-resolved text (RowText()
            // prefers a literal), but keeps the id it came from, so a
            // wrapped message still reports which stringtable entry it is
            // -- what a conversation screen's "which line did this menu
            // open with" assertion reads, and what a script that later
            // calls SetRowTextId() on the row would replace.
            MenuRow& row = AddRow(RowKind::StaticItem, prototype.textId, "", false);
            row.literalText = line;
            row.centered = !leftAligned;
        }
        // savegamemenu.s/loadgamemenu.s assign this to a variable used
        // later as a plain SetSelectedItem(...) argument -- no script
        // ever calls a method on it, so a plain int (1-based row index,
        // matching AddMenuItem's convention) is enough; no object needed.
        // The real handler returns the *last* widget it made, which for a
        // wrapped message is its final line; m_Rows.size() is already that.
        returnValue = skRValue(static_cast<int>(m_Rows.size()));
        return true;
    }
    if (methodName == skString("ParseActionText") && args.entries() == 1) {
        // M80: Menu binding 2. See simkin_bindings/action_text.h for the
        // whole substitution and why the `[KD_n]` tokens name actions
        // rather than keys. Previously the single loudest soft-fail in
        // the game's opening minute -- azra's "start" region opens
        // starthelp.s, whose every screen is built out of these.
        returnValue = skRValue(skString(
            ParseActionText(args[0].intValue(), m_Stack.input(), m_Stack.strings()).c_str()));
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
    // row.
    //
    // M99: **the real answer is "Language: English"**, not the "ENGLISH"
    // this returned (GameEngine case 6: a per-language prefix, then
    // FUN_1001b680's name -- see language.h). So options.s's God Mode row
    // is never offered in the shipped game; the string it compares against
    // is a leftover. The popup's title line reads "Language: English".
    if (methodName == skString("GetLanguageStr") && args.entries() == 0) {
        returnValue = skRValue(skString(LanguageDisplayString(m_Stack.language()).c_str()));
        return true;
    }
    // M99: GameEngine case 0x72. `SetLanguage()` with no argument is
    // `SetLanguage(0)` -- the case reads an argument only when there is
    // exactly one. See MenuStack::SetLanguage for the reload. options.s
    // follows every call with `ClearMenu(); Init();`, which is what redraws
    // the screen in the new language.
    if (methodName == skString("SetLanguage") && args.entries() <= 1) {
        m_Stack.SetLanguage(args.entries() == 1 ? args[0].intValue() : 0);
        return true;
    }
    // M99: case 0x71, `FUN_10019c04`. saveconfigfailed.s's Try is the one
    // script caller; the engine's own callers are Quit and QuitGame below
    // and the application's exit event.
    if (methodName == skString("SaveConfig") && args.entries() == 0) {
        returnValue = skRValue(m_Stack.SaveConfig());
        return true;
    }
    // M99: case 0x11, `FUN_1001a220(engine + 0x488)` -- the startup
    // bindings, again. configkeys.s's ActuallyDefaultKeys, behind its "are
    // you sure" popup.
    if (methodName == skString("ConfigKeysDefault") && args.entries() == 0) {
        if (sk::InputState* input = m_Stack.mutableInput()) input->RestoreDefaultBindings();
        return true;
    }
    // M99: case 0x12, `FUN_100780f8(menu, page)`. The row list is rebuilt
    // from scratch: a page is five actions (the fourth has one), each a row
    // whose callback is the native ConfigKeySelected below.
    if (methodName == skString("ConfigKeysMenu") && args.entries() == 1) {
        BuildConfigKeysPage(args[0].intValue());
        return true;
    }
    // M99: case 0x13. Which action a row is comes from the *selected row's
    // position*, not from anything stored on it:
    //
    //     index = position of menu+0x40 in the row list (or -1);
    //     FUN_10078890(menu, (index - menu+0x54) + (menu+0x58 - 1) * 5);
    //
    // menu+0x54 being the first action row of the page on show.
    if (methodName == skString("ConfigKeySelected") && args.entries() == 0) {
        const int index = m_SelectedItem - 1;
        BuildKeyDefinition((index - m_ConfigKeysFirstActionRow) +
                           (m_ConfigKeysPage - 1) * kActionsPerPage);
        return true;
    }
    // M99: case 0x14, `FUN_10078c08`. Arms the capture -- see
    // TickKeyCapture.
    if (methodName == skString("Redefine") && args.entries() == 0) {
        BuildRedefine();
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
        m_Titles.clear();
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
    // M103: menu bindings 0xa and 0xb (`FUN_1003136c`). DisplayPopup's case
    // is the same inlined body the *item* binding runs (`FUN_10035368`),
    // and GetMessagePopup's is four lines that hand back `menu+0xa8` --
    // **only if it already exists**. inventory.s's CheckForMsg() is written
    // around exactly that: `msgPopup=GetMessagePopup(); if (msgPopup !=
    // null)`, so returning a freshly built popup here instead of null would
    // make every Use of an item look like it had something to say.
    // M104: menu binding 0 -- see enterDelayed()'s declaration.
    if (methodName == skString("DelayOnEnter") && args.entries() == 0) {
        m_EnterAllowedAt = std::time(nullptr) + kEnterDelaySeconds;
        return true;
    }
    if (methodName == skString("DisplayPopup") && args.entries() == 1) {
        DisplayMessagePopup(ToStdString(args[0].str()));
        return true;
    }
    if (methodName == skString("GetMessagePopup") && args.entries() == 0) {
        if (m_MessagePopup) {
            returnValue = skRValue(static_cast<skiExecutable*>(m_MessagePopup.get()), false);
        }
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
    if (methodName == skString("ActuallySaveGame")) {
        // M91: the argument is genuinely optional in the real binding
        // (`if (argc != 0) slot = arg;` -- otherwise the slot SaveGame()
        // already stored is used), and the port required it, so a bare
        // `ActuallySaveGame()` soft-failed instead of writing.
        int slot = args.entries() >= 1 ? args[0].intValue() : m_Stack.saveSlot();
        m_Stack.SetSaveSlot(slot);
        const bool ok = m_Stack.ActuallySaveGame(slot);
        // M91: the real case 0x31 calls one of three script methods back
        // depending on the writer's status word -- see
        // MenuStack::ActuallySaveGame(). Both of these are optional hooks
        // (savegamemenu.s and saveconfirm.s define them; mainmenu.s does
        // not), so TryInvoke, not InvokeCallback.
        TryInvoke(ok ? "DoneSave" : "SaveFailed");
        return true;
    }
    // M91: menu bindings 0x2f/0x30, the pair that carries a chosen slot
    // from one screen to the next through `engine+0x14a71`. `SaveGame(n)`
    // stores it and opens **SaveConfirm** -- a plain
    // `FUN_100779b8(..., "SaveConfirm", 0, 0)` in case 0x30 -- and
    // saveconfirm.s's OnDisplay reads it straight back with GetSaveSlot()
    // to decide between "overwrite?" and writing outright. No shipped
    // script calls SaveGame(), which is why this never surfaced; the
    // native bridge and the multiplayer path both can.
    if (methodName == skString("GetSaveSlot") && args.entries() == 0) {
        returnValue = skRValue(m_Stack.saveSlot());
        return true;
    }
    if (methodName == skString("SaveGame") && args.entries() >= 1) {
        m_Stack.SetSaveSlot(args[0].intValue());
        m_Stack.ReopenMenu("SaveConfirm");
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
    if (methodName == skString("QuitGame") && args.entries() <= 1) {
        // M99: case 0x34 saves the settings on the way out, and a failed
        // save stops the quit:
        //
        //     save = (argc == 1) ? arg : true;
        //     if (saveDisabled || !save || FUN_10019c04()) { ...quit... }
        //     else OpenMenu("SaveConfigFailed");
        //
        // saveconfigfailed.s is that screen: "Try" calls SaveConfig again,
        // and its other row is `QuitGame(false)`, the one call that skips
        // the save. It used to soft-fail, having an argument.
        const bool save = args.entries() == 1 ? args[0].boolValue() : true;
        if (m_Stack.configSaveDisabled() || !save || m_Stack.SaveConfig()) {
            m_Stack.RequestQuit();
        } else {
            m_Stack.OpenMenu("SaveConfigFailed");
        }
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
    if (methodName == skString("GameActive") && args.entries() == 0) {
        // M91: the real one, at last -- see MenuStack::gameActive().
        returnValue = skRValue(m_Stack.gameActive());
        return true;
    }
    if (methodName == skString("CanSaveGame") && args.entries() == 0) {
        // M91: menu binding 0, and the whole case is `uVar3 = 1; goto
        // LAB_1007dc88` -- a constant true. Transcribed rather than
        // reasoned about: whatever it was meant to gate, the shipped
        // binary never says no.
        returnValue = skRValue(true);
        return true;
    }
    if (methodName == skString("CheatsActivated") || methodName == skString("ArenaActive")) {
        // No cheat or arena state exists yet -- a fixed "off" answer is the
        // correct behavior, not a soft-fail placeholder.
        returnValue = skRValue(false);
        return true;
    }
    // M104: the multiplayer pair moved to the shared helper, which every
    // other class now chains too -- same answer, one place.
    if (TryHandleMultiplayerQuery(methodName, args, returnValue)) return true;
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
    if (methodName == skString("GetNextLevelName") && args.entries() == 0) {
        // M90: GameEngine-root binding 10, and the whole of its case:
        //
        //     buf = malloc(0x40);
        //     FUN_100290a8(app+0x28 + 0x28, buf);   // internal name -> display
        //     return wide string of length wcslen(buf);
        //
        // `app+0x28`'s `+0x28` slot is the *destination* by the time this
        // runs -- `Level.LoadLevel` wrote it there on the line before it
        // raised this screen -- so the name is the place being travelled
        // to, not the place being left.
        //
        // `FUN_100290a8` is the engine's internal-zone-name -> display-
        // string lookup, which this port already has (M26 recovered the
        // whole 3821-3840 run of it, assets/zone_display_names.h). The one
        // script call site is `levelconfirm.s`'s own Init(),
        // `AddStaticItem(3950, false); AddStaticItem(GetNextLevelName(),
        // false)` -- string 3950 being "Travel to: ", which is why the two
        // rows read as one sentence.
        const std::string& zoneName = m_Stack.currentLevelName();
        int id = sk::ZoneDisplayNameStringId(zoneName);
        // A zone with no entry in the run (`ffarena` is the only one)
        // falls back to its internal name rather than an empty row.
        std::string text =
            (id >= 0 && m_Stack.strings()) ? m_Stack.strings()->Get(id) : zoneName;
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
        // M60: store-menu bindings 4..8 (trie 0x14de0). All five collapse
        // to `PopulatePage(category, clear = true)` in the real
        // dispatcher, and the categories they pass are the IPT_*
        // constants themselves -- Weapons 1, Spells 2, Armor 3, Misc 0,
        // Consumables 4, matching M58's recovered constant table exactly.
        const int category = methodName == skString("DisplayWeaponsPage") ? kItemTypeWeapon
                             : methodName == skString("DisplayArmorMenu") ? kItemTypeArmor
                             : methodName == skString("DisplayConsumablesMenu")
                                 ? kItemTypeConsumable
                             : methodName == skString("DisplaySpellsPage") ? kItemTypeSpell
                                                                           : kItemTypeMisc;
        PopulatePage(category, /*clear=*/true);
        // The argument is the category button that was pressed, and the
        // real tail (FUN_10033c88) uses it to rewire focus: every *other*
        // visible tab button is deselected, this one is selected, and the
        // table is linked above/below the tab row. Hidden buttons are
        // skipped -- which is why hiding a category's button is all
        // `buysell.s` has to do to take it out of the navigation.
        auto* active = dynamic_cast<ButtonExecutable*>(args[0].obj());
        for (auto& r : m_Rows) {
            if (auto* btn = dynamic_cast<ButtonExecutable*>(r.widget.get())) {
                btn->SetActive(btn == active && r.visible);
            }
        }
        return true;
    }
    if (methodName == skString("RedrawPage")) {
        // Store-menu binding 2. Takes an optional bool that is the
        // *clear* flag, and `buysell.s` passes **false** -- so the
        // repopulation overwrites the existing cells in place instead of
        // destroying them first. That is visible: if the new page is
        // shorter than the old one, the leftover rows keep their old
        // contents and are merely hidden by the row count, so a later
        // AddRows or a direct SetText can surface stale text. Reproduced.
        const bool clear = args.entries() >= 1 ? args[0].boolValue() : true;
        PopulatePage(m_CurrentPage, clear);
        return true;
    }
    if (methodName == skString("SetGoldText") && args.entries() == 1) {
        // M60: not a trie binding at all. `FUN_10034588` is a wcscmp
        // layer above the store-menu dispatcher that tests exactly two
        // names, "SetGoldText" and (redundantly) "IsBuyMode", and
        // tail-calls the trie for everything else -- the fourth such
        // hand-added native found, after the three merchant ones (M59).
        //
        // The body is one line: sprintf("%d", player.gold) into the
        // widget's item text. Note the format is bare "%d" -- the "gp"
        // in `AddFloatingText("0 gp", ...)`'s placeholder is gone the
        // moment this runs.
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d", m_Stack.player().gold());
        if (auto* widget = dynamic_cast<RowOwnerRef*>(args[0].obj())) {
            widget->SetTextFromHost(buf);
        }
        return true;
    }
    if (methodName == skString("UpdateEquipStatus") && args.entries() == 2) {
        auto* item = dynamic_cast<ItemExecutable*>(args[0].obj());
        returnValue = skRValue(m_Stack.player().UpdateEquipStatus(item, args[1].boolValue()));
        return true;
    }
    if (methodName == skString("DisplayObjectives") && args.entries() == 1) {
        // M88: the real quest log, `FUN_100347c8`. Not a trie binding --
        // it is one more `wcscmp`-dispatched native (the fifth found,
        // after the three merchant ones and `SetGoldText`), which is why
        // it never appeared in the 702-entry enumeration.
        //
        // The engine's loop, verbatim in shape:
        //
        //     FUN_1008e3d8(table);                 // drop every cell
        //     row = 0;
        //     for (id = 0; id < 0x100; id++) {
        //       FUN_10044d84(&rec, player, id);    // player+0xb34+id*4
        //       if (QuestAssigned(id) && !QuestCompleted(id) && rec.desc)
        //       {
        //         if (FUN_1008e210(table) < row + 3) FUN_1008e43c(table, 3);
        //         FUN_1008df60(table, row,   0, strings[rec.title], 6);
        //         FUN_1008df60(table, row+1, 0, strings[rec.desc],  0);
        //         cell = FUN_1008df60(table, row+2, 0, "", 0);
        //         cell[0x34] = 1;
        //         row += 3;
        //       }
        //     }
        //
        // Three things in that are worth not "improving":
        //
        //  - **Assigned and not completed** is the whole filter. A solved
        //    quest still shows, with its original objective line -- the
        //    game has no "done, go hand it in" text, and `QuestSolved` is
        //    only ever read by the conversation that closes the quest.
        //    Completing it is what removes it from the log.
        //  - **The id range is 0..255, not 0..49.** The description-id
        //    test is what bounds it; see quest_table.h.
        //  - **The blank third row is a real row**, not padding skipped at
        //    draw time. It is what separates two quests, and it is why the
        //    row budget below counts in threes.
        auto* table = dynamic_cast<TableExecutable*>(args[0].obj());
        if (!table) return true;
        const sk::StringTable* strings = m_Stack.strings();
        PlayerExecutable& player = m_Stack.player();
        table->ClearCells();
        int row = 0;
        for (int id = 0; id < kQuestStateSlots; ++id) {
            const QuestText text = QuestTextFor(id);
            if (!text.valid()) continue;
            if (!player.questAssigned(id)) continue;
            if (player.questCompleted(id)) continue;
            if (table->allocatedRows() < row + 3) table->AddRows(3);
            TableCell& title = table->CellAt(static_cast<size_t>(row), 0);
            title.text = strings ? strings->Get(text.titleId) : std::string();
            title.displayFlags = kCellFlagCentered;
            TableCell& objective = table->CellAt(static_cast<size_t>(row + 1), 0);
            objective.text = strings ? strings->Get(text.descriptionId) : std::string();
            TableCell& spacer = table->CellAt(static_cast<size_t>(row + 2), 0);
            spacer.text.clear();
            spacer.continuation = true;
            row += 3;
        }
        // The engine's tail: under two quests' worth of rows there is
        // nothing to scroll, so the table stops taking focus and the menu
        // hands it back to its default widget -- questlog.s's own quit
        // button, the only other thing on the screen.
        table->owner().SetWidgetSelectable(table, row >= 6);
        if (row == 0) table->SetUsedRows(0);
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
    // M90 -- **the six engine-opened screens are opened by the engine's own
    // spelling of their names now, not by their filenames.**
    //
    // It used to make no difference: `ResolveScriptPath` appends ".s" and
    // Windows opens `inventory.s` for "Inventory" just as Symbian's FAT
    // does. But GoBack() builds `<ScriptName>Back` out of exactly this
    // string, and `inventory.s` defines `InventoryBack`, not
    // `inventoryBack`, so the lowercase spellings would have silently
    // missed every one of these screens' real back handlers.
    //
    // The names are read off the engine's own persistent-menu list, the
    // run right after `"ClearPersistantMenu"` at `0x100ad794`: `Inventory`,
    // `CharacterManager`, `QuestLog`, `StatsScreen`, `BuySell`,
    // `ActionQueue`, `RemoveQueue`, `InventoryButtonItem` -- with
    // `"DragonStarStackMenu::OpenMenu %s"`, the open-by-name trace itself,
    // sitting a few bytes further on in the same block.
    if (methodName == skString("DisplayCharacterManager") && args.entries() == 0) {
        m_Stack.OpenMenu("CharacterManager");
        return true;
    }
    if (methodName == skString("DisplayLevelUp") && args.entries() == 0) {
        // M64: case 0xe, and the only way `levelup.s` opens -- no shipped
        // script calls it, so the screen is engine-driven. It is also the
        // odd one out among the six Display* natives: the other five hand
        // a screen-mode number (1 charactermanager, 2 inventory, 3
        // questlog, 4 statsscreen, ...) to one shared setter, while this
        // one calls `FUN_1002c274(controller, 8)` directly. Both arms
        // stall 10ms in `User::After` first.
        m_Stack.OpenMenu("LevelUp");
        return true;
    }
    if (methodName == skString("DisplayInventory") && args.entries() == 0) {
        m_Stack.OpenMenu("Inventory");
        return true;
    }
    if (methodName == skString("DisplayStatsScreen") && args.entries() == 0) {
        m_Stack.OpenMenu("StatsScreen");
        return true;
    }
    if (methodName == skString("DisplayQuestLog") && args.entries() == 0) {
        m_Stack.OpenMenu("QuestLog");
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
    // M104 correction: `IsRightQueue` **is** a real native, and the claim
    // that used to stand here -- that it is absent from the 702-entry table
    // and soft-fails in the shipped game too -- was wrong. It is not in the
    // trie, but M93 already established that three classes reach their
    // methods through a plain `wcscmp` chain instead, and the action-queue
    // screen is a fourth: `FUN_10033dd0` compares against `MoveItem`,
    // `GetLastItem`, `IsRightQueue` and `UpdateTextItems` before chaining
    // on. Neither coverage tool can see a name that reaches its handler
    // that way, and neither can the binding census, so "not in the JSON"
    // is not evidence of anything.
    //
    // `IsRightQueue()` is one line of that chain -- `SIMKIN_ord40(ret,
    // menu+0xcc)`, the same hand byte `ShowActionQueue` copies onto the
    // screen it opens, which is exactly this port's m_QueueHandIsRight. Its
    // one caller is `actionqueue.s`'s OnDisplay, choosing between title
    // 3779 and 3778, so until now the screen was always headed "Left
    // Queue". Nothing else on that screen depends on it, which is why this
    // one is safe to answer on its own -- see the roadmap for the other
    // three, which are the screen's actual contents.
    if (args.entries() == 0 &&
        (methodName == skString("IsLeftQueue") || methodName == skString("IsRightQueue"))) {
        // And `IsLeftQueue` returns the same byte UN-negated
        // (`FUN_10034d8c` case 2) -- true when the RIGHT hand was selected,
        // the opposite of what its name says. A naming bug in the shipped
        // binary, reproduced; no shipped script calls it.
        returnValue = skRValue(m_QueueHandIsRight);
        return true;
    }
    if (methodName == skString("ShowActionQueue") && args.entries() == 0) {
        // Real engine (FUN_10034d8c case 3): resolves the actionqueue.s
        // menu slot, copies the CALLER's hand flag onto it, then actually
        // pushes/opens it.
        //
        // M104 correction: this used to say the screen's own row population
        // (`UpdateTextItems`/`GetLastItem`) "isn't a registered native at
        // all", so the port left the screen blank on purpose. Both are real
        // -- they live on the `wcscmp` chain `FUN_10033dd0`, alongside
        // `MoveItem` and the `IsRightQueue` above. `UpdateTextItems` hides
        // every widget, walks the player's five queue slots for this hand,
        // and fills one widget per held item (text from the entity's name,
        // `widget+0x90` pointing at the object, the last one remembered at
        // `menu+0xd0` for `GetLastItem`). So the shipped screen does work,
        // and this port's is the one that is blank. Recorded as the next
        // milestone rather than bolted on here.
        // M104: **resolve, copy, then open** -- the engine's own order, and
        // the one this port had inverted. `OpenMenu` runs the screen's
        // `OnDisplay`, which is where `actionqueue.s` asks `IsRightQueue()`
        // to pick its title, so copying the flag afterwards left the screen
        // headed with the previous hand's title. Invisible until the
        // `IsRightQueue` above started answering.
        if (auto* target = m_Stack.GetOrCreateMenu("ActionQueue")) {
            target->SetQueueHandIsRight(m_QueueHandIsRight);
        }
        m_Stack.OpenMenu("ActionQueue");
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

    if (TryHandleRandom(methodName, args, returnValue)) {
        // M65: `Random(min, max)` is a real Menu-class binding in its own
        // right -- index 0x6a on the menu dispatcher (FUN_10078de4), not
        // only the root-class one M21 wired into items and creatures -- and
        // a menu script reaches it bare, from inside Init().
        //
        // Found by driving crypt1's attribute checks: every one of the
        // three fail menus opens with `GetPlayer().DoDamage(Random(2,4))`
        // (4-8 and 6-12 for the harder tiers), so without this the entire
        // cost of failing a check was silently zero.
        return true;
    }
    if (skScriptedExecutable::method(methodName, args, returnValue, context)) {
        return true;
    }
    return SoftFailNativeCall("Menu", methodName, args, returnValue);
}

}  // namespace sk_bindings
