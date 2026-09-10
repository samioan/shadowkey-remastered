#pragma once

// Native binding for a menu .s script object -- mainmenu.s itself and
// every menu it (transitively) creates via CreateMenu(). One class
// suffices for all of them: structurally they're the same kind of
// object (a script-defined Init/OnDisplay/callback set, backed by native
// menu-item state), matching the "Menu (generic)" class hypothesis at
// binding offset 0x14cfc (docs/SIMKIN_NATIVE_API.md).
//
// M5 extends M3's plain selectable/static item list to a unified "row"
// model -- AddMenuItem/AddStaticItem/AddComboBox/AddTextArea/
// AddFloatingSprite/OpenEditText all append to the same ordered list, in
// call order, so the host's Up/Down navigation and rendering (main.cpp)
// only need to walk one list regardless of what's actually on a given
// screen. Everything else still soft-fails per M3's original design.
//
// M10 adds AddButton/AddQuitButton/AddFloatingText (charactermanager.s/
// inventory.s/statsscreen.s's UI, all real screens now instead of soft-
// fail placeholders) -- AddButton and AddFloatingText both reuse the
// existing MenuItem/StaticItem row kinds rather than adding new ones
// (they're the same "text + optional selectable callback" shape), just
// returning a different native handle object (ButtonExecutable) so
// scripts can chain the extra cosmetic setters those screens call
// (SetWidth/ShowBorder/...). Two genuinely new row kinds are added for
// widgets with real distinct behavior: ItemButton (AddItemButton, an
// icon+text equip-slot display) and Table (AddTable, the inventory/
// stats/quest-log grid -- see table_executable.h).
//
// A row's display text can come from either a stringtable id (textId, the
// M3-M5 convention) or an already-resolved literal string (textId == -1,
// literalText used instead) -- AddButton/AddFloatingText's first argument
// is dynamically typed in the real scripts (sometimes a text id like
// AddButton(3043,...), sometimes a precomputed string like
// AddButton("Cymric",...) or AddFloatingText(healthText,...)), so which
// one applies is decided per call via the argument's own skRValue::type().

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/native_stub_executable.h"
#include "skRValue.h"
#include "skScriptedExecutable.h"

namespace sk_bindings {

class ItemExecutable;
class MenuExecutable;
class PopupMenuExecutable;
class TableExecutable;

// M80: `FUN_1007dca0`'s two wrap widths, the fourth argument it is called
// with and the one it overrides that argument to. See the AddStaticItem
// handler for how a static item picks between them.
constexpr int kStaticItemWrapChars = 0x11;    // 17 -- AddStaticItem's default
constexpr int kLeftAlignedWrapChars = 0x19;   // 25 -- forced for a left-aligned row

// M80: `FUN_10076b64`'s own row cursor step (`uVar9 = uVar8 + 0xc`) and
// the x it draws a left-aligned static item at (the literal 9 it passes
// as `FUN_1007f49c`'s first argument). The shadow is that function's
// left-aligned arm drawing the same string twice -- once at (x+1, y+1) in
// colour 0x0b96, then again at (x, y) in the row's real colour.
constexpr int kMenuRowPitch = 0xc;
constexpr int kStaticItemX = 9;
// Raw, in the engine's own RGB444 -- `FUN_1008f8a4`'s colour argument is
// the 12-bit kind `FUN_1008f97c` unpacks explicitly (M57's note in
// docs/GRAPHICS_FORMAT.md settled that the framebuffer carries 12 bits of
// colour in 16). 0x0b96 is (0xb, 0x9, 0x6), a warm tan; the caller
// converts to this port's RGB565.
constexpr unsigned short kTextShadowColor444 = 0x0b96;

// Post-M80 -- **every menu opened by name starts on `global.spr` slot 20,
// the parchment.** `FUN_100779b8`, the routine behind every script-level
// `OpenMenu(name)`, clears the widget list and then writes
// `*(menu + 0x50) = 0x14` before loading the new script -- so
// `MenuBackground(id)` is an *override* of this, not the only source. The
// corpus agrees from the other side: of 79 explicit `MenuBackground` calls
// 66 pass 20, restating the default they already have.
//
// M80 read `FUN_10076b64`'s `if (-1 < menu+0x50)` guard as "a menu with no
// background draws over whatever is on screen" and gave in-game popups the
// frozen 3D frame. The guard is real but unreachable for a script-opened
// menu, and the result was every background-less screen showing the world
// through it.
constexpr int kDefaultMenuBackground = 0x14;

// `FUN_1007dca0`'s word wrap, which builds one menu-item widget per line:
// walk the text, and at each space whose running line length has passed
// `maxChars`, break at the *previous* space. Text shorter than `maxChars`
// comes back as a single line. Exposed for
// port/src/tests/m80_tutorial_popup_smoke.cpp.
std::vector<std::string> WrapMenuText(const std::string& text, int maxChars);

// M10: AddTitle()'s return value -- inventory.s's WeaponsMenu()/
// ArmorMenu()/etc. call .SetLocalizedText(id) on it to change the title
// text after the fact (a single title slot, m_TitleTextId, not a row --
// so this doesn't use the RowOwnerRef mechanism the other widget handles
// do). Defined here (not its own file) since it's a tiny, MenuExecutable-
// only-coupled value member (see MenuExecutable::m_TitleHandle below);
// method() is implemented out-of-line in menu_executable.cpp, once
// MenuExecutable's own definition is visible.
class TitleHandle : public NativeStubExecutable {
public:
    explicit TitleHandle(MenuExecutable& owner) : NativeStubExecutable("Title"), m_Owner(owner) {}
    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

private:
    MenuExecutable& m_Owner;
};

class MenuExecutable : public skScriptedExecutable {
public:
    MenuExecutable(const skString& filename, skExecutableContext& ctxt, MenuStack& stack);

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

    // skTreeNodeObject's default setValue()/getValue() treat every field
    // as TreeNode data: assigning a native object (a popup, floating
    // text, ...) to a field silently collapses it to its str()
    // representation (skTreeNodeObject.cpp, setValue()'s
    // "child->data(v.str())" fallback), discarding the object reference
    // -- so e.g. "myPopup = CreatePopupMenu(...); myPopup.AddItem(...)"
    // fails with "Method AddItem not found" the moment myPopup is read
    // back. Any field holding a non-TreeNode object is instead kept in
    // m_NativeFields, bypassing the TreeNode entirely; plain data fields
    // (strings/ints declared in the script) still go through the base
    // class unchanged.
    bool setValue(const skString& fieldName, const skString& attribute,
                  const skRValue& value) override;
    bool getValue(const skString& fieldName, const skString& attribute,
                  skRValue& value) override;

    // Runs the script's Init() handler with a placeholder "(s)" argument.
    // Called once by MenuStack right after construction.
    void RunInit();
    // Runs the script's OnDisplay() handler, if it defines one. Called by
    // MenuStack::OpenMenu each time this menu becomes current.
    void RunOnDisplay();

    // M4/M5: host-driven navigation, called from the input/tick loop.
    // Moves the 1-based selection to the next/previous *selectable* row,
    // wrapping around either end.
    //
    // IMPORTANT (and the source of a real navigation bug fixed this
    // session): `selectedItem()` is a **1-based index into every row**, not
    // into just the selectable ones -- that's the convention the real
    // scripts use, since AddMenuItem()/AddStaticItem() hand back exactly
    // that number (see MenuItemHandle::intValue(), and AddStaticItem's own
    // `m_Rows.size()` return) for scripts to pass straight back into
    // SetSelectedItem(). main.cpp's renderer used to compare it against a
    // counter that skipped non-selectable rows, so on any screen mixing
    // static and selectable rows the highlight landed on the wrong row --
    // use IsRowSelected() below rather than recounting.
    void MoveSelection(int delta);

    // M35: directional navigation that respects the screen's own layout.
    //
    // MoveSelection() treats every selectable row as one flat vertical
    // list, which is right for the AddMenuItem screens it was written for
    // and wrong for every screen a real script lays out by hand. The
    // inventory/equip screen is the clear case: inventory.s's OnDisplay()
    // puts five category buttons (weapons, armour, consumables, spells,
    // misc) side by side on one row at y=25 via AddButton(...,x,y,...),
    // with x stepping by 34 each time, and then the item table below them
    // at y=60. Flattened, Up/Down walked through the five buttons *and*
    // the table as if they were stacked, Left/Right did nothing at all
    // (they went to CycleSelectedCombo, and there is no combo on that
    // screen), and once the selection reached the table it could never
    // leave -- Up/Down were being consumed by the table forever. That is
    // both "the equip menus don't move through the selections properly"
    // and "I can't get to the armour/consumables/spells tabs".
    //
    // The fix reads the layout the scripts already provide (MenuRow's real
    // x/y, stored since M10): rows sharing a y band form a horizontal
    // group navigated with Left/Right, and Up/Down step between bands,
    // keeping the horizontally-nearest row. A Table row navigates
    // internally until it reaches its own first/last row, then releases
    // focus to the neighbouring band.
    //
    // Returns false when it does not apply -- an unpositioned screen (the
    // classic centred AddMenuItem list, untouched), or a horizontal move
    // on a ComboBox/Slider, which belongs to CycleSelectedCombo instead.
    bool NavigateDirectional(int dx, int dy);

    // True if `rowIndex` (0-based, into rows()) is the currently selected
    // row. The single place the 1-based/all-rows convention above is
    // decoded, so renderers can't get it wrong again.
    bool IsRowSelected(size_t rowIndex) const {
        return static_cast<int>(rowIndex) + 1 == m_SelectedItem;
    }

    // Snaps the selection to the first selectable row if it currently
    // points at nothing selectable -- i.e. after ClearMenu() (which resets
    // it to 0) or after a script's own SetSelectedItem() named a row that
    // is now static or gone. Without this, a screen whose OnDisplay()
    // rebuilds its rows comes back with no selection at all: nothing is
    // highlighted and the confirm key does nothing until the player
    // happens to press Up or Down. Cheap and idempotent -- called once per
    // tick from the input loop.
    void EnsureValidSelection();
    // Cycles the currently selected row's value if it's a ComboBox (fires
    // its SetCallback() handler on change) or nudges it if it's a Slider
    // (M28, the real Options screen's two volume rows -- applies the new
    // value to the audio engine straight away); no-op for any other row
    // kind.
    void CycleSelectedCombo(int delta);
    // Pushes a slider row's current value into the audio engine (see
    // the .cpp) -- shared by the initial AddMenuSlider() and each
    // later Left/Right adjustment.
    void ApplySliderValue(const class SliderExecutable& slider);
    // Fires the currently selected row's script callback (MenuItem's
    // AddMenuItem callback, ComboBox's SetOnEnterCallback, or
    // FloatingSprite's own callback), if it has one -- same dispatch path
    // CreateMenu's children go through, so a callback that calls
    // OpenMenu() correctly switches MenuStack::currentMenu().
    void ActivateSelected();
    // Fires a named handler if the script defines one (e.g.
    // "OnRightSoftkey" for the back/cancel softkey); no-ops silently if
    // it doesn't, unlike method()'s normal soft-fail logging -- this is
    // an optional hook, not an unresolved native call. Returns whether the
    // script actually had it.
    bool TryInvoke(const std::string& handlerName);
    // M60: the same, with one argument -- the real table/row callbacks
    // take the selected cell ("SelectedItem[ (cell) ...").
    bool TryInvokeWithArg(const std::string& handlerName, const skRValue& arg);

    // M60: reachbacks the table's cell class needs (FUN_100a4ce4 reads
    // both straight off the player).
    int PlayerCharacterClass() const;
    bool PlayerOwnsTemplate(int templateId) const;
    class PlayerExecutable& stackPlayer() const;
    // M60: `player+0xf8c + slot*4`, the eight worn-armour slots the store
    // table's comparison columns look up for an Armor row. This port has
    // no per-slot array, so the slot is resolved through products.dat --
    // the only place the game records which slot a template occupies --
    // and the wearer is found by scanning the player's own equipped
    // armour. Same answer, different bookkeeping; noted rather than
    // pretended otherwise.
    int ArmorSlotOfTemplate(int templateId) const;
    class ItemExecutable* EquippedArmorInSlot(int slot) const;

    // The back/cancel softkey for this screen -- the screen's own handler
    // if it defines one, else its real SetPrevMenu() target. See the .cpp
    // for why the SetPrevMenu fallback is required rather than optional.
    // Returns false only if the screen has neither.
    bool GoBack();

    const std::string& prevMenuPath() const { return m_PrevMenuPath; }

    enum class RowKind {
        MenuItem,
        StaticItem,
        ComboBox,
        TextArea,
        FloatingSprite,
        TextEntry,
        ItemButton,  // M10: AddItemButton()
        Table,       // M10: AddTable()
        Slider,      // M28: AddMenuSlider() -- see slider_executable.h
    };
    struct MenuRow {
        RowKind kind;
        int textId = -1;  // -1 = use literalText instead, see header comment
        std::string literalText;
        std::string callback;  // MenuItem's AddMenuItem callback
        bool selectable = false;
        // Non-null for ComboBox/TextArea/FloatingSprite/ItemButton/Table
        // rows -- the same object handed back to the script (see
        // AddComboBox() etc.); the row owns it, the script only holds a
        // non-owning reference (see setValue()'s comment above), so
        // clearing/replacing rows is what actually frees it, matching how
        // the real UI would invalidate a widget handle when its menu
        // redraws.
        std::unique_ptr<NativeStubExecutable> widget;
        // M21: AddMenuItem's real 3-arg form (text, callback,
        // associatedObject) -- lootmenu.s's own
        // `AddMenuItem(Item.GetName(),"SelectItem",Item)`, read back via
        // MenuItemHandle::GetAssociatedObject(). Non-owning (the real
        // owner is wherever the object actually lives -- an ItemExecutable
        // bag's own m_Contents, for the loot-menu case).
        skiExecutable* associatedObject = nullptr;
        // Absolute screen position/size a real script provided directly
        // (AddButton/AddFloatingText/AddItemButton/AddFloatingSprite's own
        // real x,y[,w,h] arguments -- charactermanager.s's whole real 2x2
        // grid + portrait + stat-text layout is built entirely from these,
        // previously read and discarded). x/y default to -1 ("no real
        // position given" -- AddMenuItem/AddStaticItem/AddTable/etc. never
        // provide one) -- main.cpp's RenderMenu() only switches a row to
        // absolute placement when x>=0, so every screen that only ever
        // used the vertical centered-list convention (mainmenu.s and
        // everything built on AddMenuItem) renders exactly as before.
        // w/h default to 0 (real scripts vary in whether they ever call
        // SetWidth()/SetHeight() at all -- ButtonExecutable's own
        // real-corpus callers always do, ItemButtonExecutable's own w/h
        // come from AddItemButton's args directly).
        int x = -1, y = -1, w = 0, h = 0;
        // AddButton's real ShowBorder(true) call (charactermanager.s's
        // Stats/Equip/Quest buttons) -- drawn as an actual outline
        // rectangle using w/h above, matching the real screenshot's boxed
        // 2x2 grid.
        bool showBorder = false;
        // M36: AddButton's real 5th/6th arguments -- global.spr slot ids
        // for the button's normal and highlighted art. -1 when the script
        // used the 4-arg text-only form. See the AddButton handler.
        int spriteNormal = -1;
        int spriteSelected = -1;
        // M36: AddQuitButton() rather than AddButton()/AddMenuItem() --
        // the real screens' bottom-centred softkey label ("Back"), which
        // has no x/y of its own and so used to land in the middle of the
        // vertical flow, on top of whatever was there.
        bool isQuitButton = false;
        // M60: widget class 0x14d20's SetVisible/SetEnabled byte
        // (widget+0x5d). A hidden row draws nothing and cannot be
        // selected. `buysell.s` hides the category button of every
        // category the merchant does not stock; the shipped focus-wiring
        // pass (FUN_10033c88) skips hidden buttons for exactly that
        // reason, which this port gets for free by excluding them from
        // navigation.
        bool visible = true;
        // M80: `AddStaticItem`'s own second argument, which decides how
        // the engine draws the row -- **not** whether it can be selected.
        // `FUN_10076b64`'s case 0 passes it, inverted, to `FUN_1007f49c`
        // as that function's `param_7`: 0 takes the left-aligned arm
        // (drawn twice, a 0x0b96 shadow at (x+1,y+1) and the real text at
        // (9,y)), 1 takes the centred one (`FUN_1008f97c`, no x at all).
        // `AddStaticItem(id)` with one argument defaults it to 1 --
        // left-aligned -- except when the text starts with "---", the
        // separator convention, which centres instead. See the
        // AddStaticItem handler and RenderMenu().
        bool centered = false;
    };
    const std::vector<MenuRow>& rows() const { return m_Rows; }
    int backgroundId() const { return m_BackgroundId; }
    // M64: `SetStartCoord` (root-class binding 0x2c) -- the y the menu's
    // draw (FUN_10076b64) starts its row cursor at, menu+0x96. The menu
    // constructor seeds it with **0x32**, so 50 is every menu's default,
    // and exactly one script in the whole corpus overrides it:
    // `levelup.s`, which moves to 10 to fit eleven rows on one page.
    int startCoord() const { return m_StartCoord; }
    int titleTextId() const { return m_TitleTextId; }
    // M88: AddTitle's real (textId, y) pairs, in call order. questlog.s is
    // the screen that made one slot untenable -- it adds two,
    // `AddTitle(3785,10)` ("Quest Log") and `AddTitle(3786,25)` ("Use 'key
    // 5' to exit"), and with a single slot the second silently replaced
    // the first and then drew at the shared layout cursor, i.e. straight
    // across the first quest in the table. A title whose `y` is -1 (the
    // one-argument form every other screen uses) still draws at the
    // cursor and still advances it, so nothing else moves.
    struct MenuTitle {
        int textId = -1;
        int y = -1;
    };
    const std::vector<MenuTitle>& titles() const { return m_Titles; }
    int selectedItem() const { return m_SelectedItem; }  // 1-based, 0 = none
    bool useHoriz() const { return m_UseHoriz; }
    bool textEntryActive() const { return m_TextEntryActive; }
    // The current screen's modal confirmation popup (myPopup, savePopup,
    // ...), if any of the ones it's created is currently visible -- popups
    // live in m_NativeFields (see setValue()'s comment), keyed by
    // whatever script variable name they were assigned to, so this is the
    // only way for the host (main.cpp) to find one to render/route input
    // to without knowing those names.
    PopupMenuExecutable* activePopup() const;

    // Used by RowOwnerRef-derived widgets (MenuItemHandle,
    // TextAreaExecutable) to implement .SetSelectable(bool) and (for
    // TextAreaExecutable) .SetLocalizedText(id).
    void SetRowSelectable(size_t rowIndex, bool selectable);
    // M88: same thing addressed by the widget rather than by row index --
    // `FUN_10034830`'s tail clears the quest-log table's own focusable
    // byte (`table+0x5c`) when the log holds fewer than two quests, and a
    // widget has no idea which row of its owner it ended up on.
    void SetWidgetSelectable(const skiExecutable* widget, bool selectable);
    void SetRowTextId(size_t rowIndex, int textId);
    // M10: ButtonExecutable/MenuItemHandle's .SetItemText(literalString).
    void SetRowLiteralText(size_t rowIndex, const std::string& text);
    // ButtonExecutable's real .SetWidth(w)/.SetHeight(h)/.ShowBorder(true)
    // -- see MenuRow's own x/y/w/h/showBorder comment.
    void SetRowWidth(size_t rowIndex, int w);
    void SetRowHeight(size_t rowIndex, int h);
    void SetRowShowBorder(size_t rowIndex, bool showBorder);
    // M60: widget class 0x14d20's SetX/SetY/SetVisible.
    void SetRowX(size_t rowIndex, int x);
    void SetRowY(size_t rowIndex, int y);
    void SetRowVisible(size_t rowIndex, bool visible);
    // M10: TitleHandle's .SetLocalizedText(id) -- AddTitle()'s return
    // value, see inventory.s's WeaponsMenu()/ArmorMenu()/etc.
    void SetTitleTextId(int textId) {
        m_TitleTextId = textId;
        // TitleHandle::SetLocalizedText() retitles whichever title the
        // script is holding, which is always the one it just added.
        if (m_Titles.empty()) {
            m_Titles.push_back(MenuTitle{textId, -1});
        } else {
            m_Titles.back().textId = textId;
        }
    }
    // Resolves a stringtable id through this menu's stack. "?" when the
    // table is missing or the id is out of range.
    std::string ResolveText(int textId) const;

    // M60: the store/inventory screen's mode, `menu+0xd0`. The shipped
    // engine writes it from exactly one place --
    //
    //     FUN_10034f38(menu, buying) { menu->mode = buying ? 1 : 2; }
    //
    // which `GetPlayer().BuyFromMerchant()` and `SellToMerchant()` call
    // before handing the screen over, and the store screen's own
    // constructor seeds it to 2 (Sell) while the inventory screen's seeds
    // it to 0. `IsBuyMode()` is `mode == 1`, which is why the sell page
    // and the plain inventory page both report false and still behave
    // completely differently: the table population (FUN_10032f78)
    // branches three ways on this field, not two.
    enum class ScreenMode { Inventory = 0, Buy = 1, Sell = 2 };
    ScreenMode screenMode() const { return m_ScreenMode; }
    void SetScreenMode(ScreenMode mode) { m_ScreenMode = mode; }

    // FUN_10032f78 -- the one function behind all five Display*Page
    // natives, RedrawPage, and the store screen's whole reason to exist.
    // Fills SetInventoryList()'s remembered table with every product (Buy)
    // or item (Sell/Inventory) of `category`, in the real four-column
    // shape `buysell.s` and `inventory.s` lay out. `clear` is the second
    // argument the real function takes: true wipes the table's cells
    // first, false overwrites them in place -- see the .cpp for why that
    // distinction is visible.
    void PopulatePage(int category, bool clear);
    int currentPage() const { return m_CurrentPage; }

    // M10: Up/Down/Enter routed into the currently-selected row's Table
    // widget (see table_executable.h) instead of this menu's own outer
    // MoveSelection/ActivateSelected. Returns false (did nothing) if the
    // current selection isn't a Table row, so main.cpp falls through to
    // its normal handling otherwise -- mirrors how CycleSelectedCombo
    // already encapsulates ComboBox's own Left/Right special case.
    bool TryMoveTableSelection(int delta);
    bool TryActivateTable();

    // Decompiled (FUN_10034d8c) this session: SetLeftActionQueue()/
    // SetRightActionQueue() set a hand-selection byte on the CALLING menu
    // instance (charactermanager.s), and ShowActionQueue() copies it onto
    // the target actionqueue.s instance before opening it -- both ends are
    // this same per-instance field on whichever MenuExecutable it's read
    // or written on. See ShowActionQueue()'s own handler in the .cpp for
    // the full real/faithful-vs-simplified writeup.
    bool queueHandIsRight() const { return m_QueueHandIsRight; }
    void SetQueueHandIsRight(bool right) { m_QueueHandIsRight = right; }

    // M21: the object that opened this menu on itself (a real loot bag's
    // `OpenMenu("LootMenu")`, ItemExecutable::method()'s OpenMenu handler)
    // -- set by MenuStack::OpenMenu()/ReopenMenu()'s new `opener` parameter,
    // read back via lootmenu.s's own `GetOpener()` calls
    // (`GetOpener().GetFirst()`, `GetOpener().RemoveObject(...)`, ...).
    // nullptr for every menu not opened this way (the overwhelming
    // majority) -- GetOpener() soft-fails to a benign default in that case
    // rather than crash, same as any other unset optional reference in
    // this port.
    void SetOpener(skiExecutable* opener) { m_Opener = opener; }
    // M75: read back host-side, so a test can assert *which* object a
    // conversation was opened by -- the thing 210 real `GetOpener()` call
    // sites depend on.
    skiExecutable* opener() const { return m_Opener; }

    // M90: the screen's own name -- the last component of the path it was
    // opened by, kept verbatim (case included). The engine keeps the same
    // thing at `menu+0xc` and builds this screen's back-key handler out of
    // it; see GoBack().
    void SetScriptName(std::string name) { m_ScriptName = std::move(name); }
    const std::string& scriptName() const { return m_ScriptName; }

private:
    MenuRow& AddRow(RowKind kind, int textId, const std::string& callback, bool selectable);

    MenuStack& m_Stack;
    skiExecutable* m_Opener = nullptr;
    int m_BackgroundId = kDefaultMenuBackground;  // see the constant's comment
    // menu+0x96, and 0x32 is what FUN_10073bd8 puts there -- see
    // startCoord() above.
    int m_StartCoord = 50;
    int m_TitleTextId = -1;
    std::vector<MenuTitle> m_Titles;
    TitleHandle m_TitleHandle{*this};
    bool m_UseHoriz = false;
    bool m_TextEntryActive = false;
    // Real SetPrevMenu() target -- see GoBack(). Survives ClearMenu()
    // (which every screen's OnDisplay runs), since the real call site is
    // Init(), which runs only once per screen.
    std::string m_PrevMenuPath;
    // M90: see SetScriptName(). Set by MenuStack when the screen is built.
    std::string m_ScriptName;
    std::vector<MenuRow> m_Rows;
    int m_SelectedItem = 0;
    bool m_QueueHandIsRight = false;
    int m_PrevSelectedItem = 0;
    std::map<std::string, skRValue> m_NativeFields;
    // Non-owning -- see CreatePopupMenu's handler and ClearMenu's reset in
    // the .cpp. Lets activePopup() find a visible one without scanning
    // m_NativeFields by (unknown) name or type.
    std::vector<PopupMenuExecutable*> m_KnownPopups;

    // M10: SetInventoryList()'s remembered target -- DisplayWeaponsPage()/
    // DisplayArmorMenu()/etc. populate whichever Table this last pointed
    // at, matching inventory.s's own "SetInventoryList(inventoryTable);
    // ... DisplayWeaponsPage(weaponsButton);" call order. Non-owning, same
    // convention as m_KnownPopups.
    TableExecutable* m_InventoryListTarget = nullptr;

    // M60: menu+0xd0 and menu+0xcc -- the screen mode and the category
    // currently on show. RedrawPage() takes no category argument; it
    // re-runs the last one, which is what m_CurrentPage remembers.
    ScreenMode m_ScreenMode = ScreenMode::Inventory;
    int m_CurrentPage = 0;
};

}  // namespace sk_bindings
