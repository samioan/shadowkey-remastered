// M95 smoke test: the GameState odds and ends.
//
// Nine names on the Player/GameState class (`0x14dbc`, dispatcher
// `FUN_1003f130`) over 25 shipped call sites -- the last of that class's
// bucket in the post-M91 census once M94 took the two trap rolls:
//
//   case 0x00  DropGold(amount)          9  dropgoldmenu.s
//   case 0x01  IsMenuActive()            3  ratherb.s, twilite/pergan_asuul.s
//   case 0x02  SetPositionMirrorAll()    2  crypt2.s
//   case 0x18  EnableCoords()            1  cheatmenu.s
//   case 0x1a  VisitStore(category)      5  cheatmenu.s
//   case 0x1b  IsGhost()                 2  cheatmenu.s
//   case 0x1c  SetGhost(bool)            2  cheatmenu.s
//   case 0x1d  SetPlayerClassFlag(bool)  1  mainmenu.s
//
// What each part checks, and why it is the check that could fail:
//
//   1. The corpus census, so the site counts above are measured, not copied.
//   2. DropGold, driven through the real dropgoldmenu.s: the strict gate, the
//      gold item it builds, and the purse coming back when the bag is looted.
//   3. VisitStore: the God Vendor's stock is exactly "every template of that
//      category products.dat knows", every price is 0 -- on the shared
//      record too -- and a purchase really is free.
//   4. IsMenuActive against the real stack.
//   5. SetPositionMirrorAll is SetPosition, both argument counts.
//   6. EnableCoords toggles; SetGhost/IsGhost through cheatmenu.s's own
//      MakeGhost handler.
//   7. SetPlayerClassFlag through mainmenu.s's MenuNewGame.
//   8. The ghost's speed, from the engine's own integer velocity chain.
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "assets/product_database.h"
#include "assets/string_table.h"
#include "simkin_bindings/combo_box_executable.h"
#include "simkin_bindings/entity_base_ref.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/ghost_mode.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/store.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"

namespace sk_b = sk_bindings;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("  %-86s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    if (!ok) ++g_failures;
}

std::vector<std::filesystem::path> AllScripts(const std::string& root) {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_regular_file(ec) && it->path().extension() == ".s") files.push_back(it->path());
    }
    return files;
}

// Live (comment-stripped) whole-name calls of `name` across the corpus --
// whole name followed by `(`, because charactermanager.s also has a local
// `bCanDropGold`, a `DropGold[ ()` handler and a `"DropGold"` callback
// string, none of which is a call of the native.
int CountCalls(const std::vector<std::filesystem::path>& files, const std::string& name) {
    int total = 0;
    for (const std::filesystem::path& f : files) {
        std::ifstream in(f);
        std::string line;
        while (std::getline(in, line)) {
            size_t comment = line.find("//");
            if (comment != std::string::npos) line = line.substr(0, comment);
            size_t at = line.find(name);
            while (at != std::string::npos) {
                const bool boundary =
                    at == 0 || !(std::isalnum(static_cast<unsigned char>(line[at - 1])) ||
                                 line[at - 1] == '_');
                size_t open = at + name.size();
                while (open < line.size() && line[open] == ' ') ++open;
                if (boundary && open < line.size() && line[open] == '(') ++total;
                at = line.find(name, at + 1);
            }
        }
    }
    return total;
}

skRValue Call(skiExecutable& obj, const char* name, skRValueArray args, skInterpreter& interpreter) {
    skRValue ret;
    skExecutableContext ctxt(&interpreter);
    try {
        obj.method(skString(name), args, ret, ctxt);
    } catch (skRuntimeException& e) {
        std::printf("   (RUNTIME ERROR in %s: %s)\n", name, e.toString().ptr());
    }
    return ret;
}

skRValueArray Args() { return skRValueArray(); }
skRValueArray Args(skRValue a) {
    skRValueArray v;
    v.append(a);
    return v;
}
skRValueArray Args(skRValue a, skRValue b) {
    skRValueArray v;
    v.append(a);
    v.append(b);
    return v;
}
skRValueArray Args(skRValue a, skRValue b, skRValue c) {
    skRValueArray v;
    v.append(a);
    v.append(b);
    v.append(c);
    return v;
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const char* scriptRoot =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";
    const std::string root = scriptRoot;

    sk::StringTable strings;
    if (!strings.Load(root + "/stringtable.eng")) {
        std::printf("m95_gamestate_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(root)) {
        std::printf("m95_gamestate_smoke: FAILED to load entities.txt\n");
        return 1;
    }

    // ---------------------------------------------------------------
    // Part 1: the shipped corpus.
    // ---------------------------------------------------------------
    std::printf("\n== Part 1: what the shipped scripts ask for ==\n");
    {
        const std::vector<std::filesystem::path> files = AllScripts(root);
        struct Want {
            const char* name;
            int sites;
        };
        const Want wants[] = {{"DropGold", 9},     {"IsMenuActive", 3},       {"SetPositionMirrorAll", 2},
                              {"EnableCoords", 1}, {"VisitStore", 5},         {"IsGhost", 2},
                              {"SetGhost", 2},     {"SetPlayerClassFlag", 1}};
        int total = 0;
        for (const Want& w : wants) {
            const int n = CountCalls(files, w.name);
            total += n;
            Check(n == w.sites, std::string(w.name) + " has " + std::to_string(w.sites) +
                                    " live call sites (" + std::to_string(n) + ")");
        }
        Check(total == 25, "25 sites in all, the roadmap's figure for this bucket");
    }

    skInterpreter interpreter;
    sk_b::MenuStack stack(root, interpreter, &strings);
    stack.level().SetEntityTypes(&entityTypes);
    sk_b::PlayerExecutable& player = stack.player();

    try {
        // ---------------------------------------------------------------
        // Part 2: DropGold, through dropgoldmenu.s.
        // ---------------------------------------------------------------
        std::printf("\n== Part 2: DropGold ==\n");
        {
            // The strict gate first, on the bare native.
            Call(player, "SetGold", Args(skRValue(100)), interpreter);
            Call(player, "DropGold", Args(skRValue(100)), interpreter);
            Check(player.gold() == 100 && player.TakePendingDrops().empty(),
                  "DropGold(100) with exactly 100 does nothing -- `amount < gold`, strictly");
            Call(player, "DropGold", Args(skRValue(99)), interpreter);
            std::vector<std::unique_ptr<sk_b::ItemExecutable>> drops = player.TakePendingDrops();
            Check(player.gold() == 1 && drops.size() == 1,
                  "DropGold(99) with 100 leaves 1 coin and one thing on its way to the floor");
            if (drops.size() == 1 && drops[0]) {
                Check(drops[0]->templateId() == sk_b::kTemplateGold,
                      "  it is entities.txt template 52, gold.s");
                Check(drops[0]->quantity() == 99, "  holding the 99 (item+0x1c4)");
            }

            // Now the real screen. The gold row in charactermanager.s calls
            // `GetPlayer().OpenMenu("Menus\\\\DropGoldMenu")`.
            Call(player, "SetGold", Args(skRValue(600)), interpreter);
            Call(player, "OpenMenu", Args(skRValue(skString("Menus\\DropGoldMenu"))), interpreter);
            sk_b::MenuExecutable* menu = stack.currentMenu();
            Check(menu != nullptr, "Menus\\DropGoldMenu opens from the player's OpenMenu");
            sk_b::ComboBoxExecutable* combo = nullptr;
            if (menu) {
                skRValue field;
                if (menu->getValue(skString("comboBox"), skString(""), field)) {
                    combo = dynamic_cast<sk_b::ComboBoxExecutable*>(field.obj());
                }
            }
            Check(combo != nullptr, "  and its comboBox is a real combo box");
            if (combo && menu) {
                // With 600 gold: 25, and 50/100/500 (each `GetGold() > n`).
                Call(*combo, "SetSelection", Args(skRValue(3)), interpreter);
                Check(combo->currentOptionValue() == 500,
                      "  its fourth option at 600 gold is 500 -- 25/50/100/500 offered");
                combo->CycleSelection(1);
                Check(combo->currentOptionValue() == 25,
                      "  and there is no fifth: one step on wraps to 25 (1000 needs > 1000)");
                Call(*combo, "SetSelection", Args(skRValue(3)), interpreter);
                Call(*menu, "OnDropGold", Args(), interpreter);
                drops = player.TakePendingDrops();
                Check(player.gold() == 100, "OnDropGold at index 3 drops 500 of the 600");
                Check(drops.size() == 1 && drops[0] && drops[0]->quantity() == 500,
                      "  as one purse of 500 queued for the floor");

                // The purse is a real loot bag (main.cpp, M93), and the coins
                // come back through M82's template-52 fold.
                if (drops.size() == 1 && drops[0]) {
                    std::unique_ptr<sk_b::ItemExecutable> bag = stack.level().CreateEntityWithScript(
                        sk_b::kDroppedLootTypeId, sk_b::kDroppedLootScript);
                    Check(bag != nullptr, "  a Loot_Dropped bag can be built to hold it");
                    player.AddItem(std::move(drops[0]));
                    Check(player.gold() == 600, "picking the purse back up restores all 600");
                }
            }
        }

        // ---------------------------------------------------------------
        // Part 3: VisitStore -- the God Vendor.
        // ---------------------------------------------------------------
        std::printf("\n== Part 3: VisitStore ==\n");
        {
            const sk::ProductDatabase& products = stack.products();
            Check(products.loaded(), "products.dat loaded");
            // A second, untouched copy, to tell what VisitStore changed.
            sk::ProductDatabase pristine;
            pristine.Load(root);
            auto expectedLines = [&](int category) {
                int n = 0;
                for (const auto& [typeId, desc] : entityTypes.all()) {
                    if (desc.category != category && !(category == 5 && desc.category == 14)) {
                        continue;
                    }
                    if (products.Find(typeId)) ++n;
                }
                return n;
            };

            const int armorLines = expectedLines(6);
            Call(player, "VisitStore", Args(skRValue(6)), interpreter);
            const sk_b::Store* vendor = player.godVendor();
            Check(vendor != nullptr, "VisitStore(6) builds a store the player owns (+0xf88)");
            Check(player.merchant() == vendor, "  and trades with it (+0xf84 = +0xf88)");
            Check(stack.currentMenu() != nullptr, "  with the store screen open");
            Check(stack.currentMenu() &&
                      stack.currentMenu()->screenMode() == sk_b::MenuExecutable::ScreenMode::Buy,
                  "  on its buy page");
            // buysell.s titles the page `AddFloatingText("(Weapons)")` and
            // retitles it from ArmorMenu[] with SetLocalizedText(2991). The
            // retitle has to displace the literal, or every store reads
            // "(Weapons)" -- which this one did, live, before M95.
            if (sk_b::MenuExecutable* screen = stack.currentMenu()) {
                bool retitled = false, staleLiteral = false;
                for (const auto& row : screen->rows()) {
                    if (row.textId == 2991 && row.literalText.empty()) retitled = true;
                    if (row.literalText == "(Weapons)") staleLiteral = true;
                }
                Check(retitled && !staleLiteral,
                      "  titled \"" + strings.Get(2991) + "\" (2991), not the \"(Weapons)\" literal");
            }
            if (vendor) {
                std::printf("     armour: %zu lines (expected %d)\n", vendor->stock().size(),
                            armorLines);
                Check(static_cast<int>(vendor->stock().size()) == armorLines && armorLines > 0,
                      "its stock is every category-6 template products.dat knows");
                bool all99 = true, allFree = true, sharedFree = true, baseKept = true;
                for (const sk_b::Store::StockEntry& e : vendor->stock()) {
                    if (e.quantity != 99) all99 = false;
                    if (e.price != 0) allFree = false;
                    if (e.record && e.record->price != 0) sharedFree = false;
                    const sk::ProductRecord* before = e.record ? pristine.Find(e.record->templateId)
                                                               : nullptr;
                    if (!before || e.record->basePrice != before->price) baseKept = false;
                }
                Check(all99, "  99 of each");
                Check(allFree, "  every line at price 0");
                Check(sharedFree, "  and the shared catalogue record's price is 0 too");
                Check(baseKept, "  while its base price still holds the shipped price");
            }

            // A purchase really is free.
            if (vendor && !vendor->stock().empty() && vendor->stock()[0].record) {
                const std::string name = strings.Get(vendor->stock()[0].record->nameStringId);
                const int goldBefore = player.gold();
                const size_t itemsBefore = player.inventory().size();
                const skRValue bought =
                    Call(player, "BuyItem", Args(skRValue(skString(name.c_str())), skRValue(1)),
                         interpreter);
                Check(bought.intValue() == 1, "BuyItem(\"" + name + "\") from the God Vendor succeeds");
                Check(player.gold() == goldBefore, "  and costs nothing");
                Check(player.inventory().size() == itemsBefore + 1, "  and the item is in the bag");
            }

            // A spell visit takes the scrolls as well, and replaces the old
            // vendor.
            const int spellLines = expectedLines(5);
            int scrollLines = 0;
            for (const auto& [typeId, desc] : entityTypes.all()) {
                if (desc.category == 14 && products.Find(typeId)) ++scrollLines;
            }
            Call(player, "VisitStore", Args(skRValue(5)), interpreter);
            const sk_b::Store* spellVendor = player.godVendor();
            Check(spellVendor && player.merchant() == spellVendor,
                  "VisitStore(5) replaces the vendor and trades with the new one");
            if (spellVendor) {
                std::printf("     spells: %zu lines (expected %d, %d of them scrolls)\n",
                            spellVendor->stock().size(), spellLines, scrollLines);
                Check(static_cast<int>(spellVendor->stock().size()) == spellLines,
                      "  stocking every category-5 and category-14 template in products.dat");
            }
        }

        // ---------------------------------------------------------------
        // Part 4: IsMenuActive.
        // ---------------------------------------------------------------
        std::printf("\n== Part 4: IsMenuActive ==\n");
        {
            Check(Call(player, "IsMenuActive", Args(), interpreter).boolValue() == true,
                  "true while the store screen is up");
            sk_b::MenuExecutable* store = stack.currentMenu();
            if (store) Call(*store, "Quit", Args(), interpreter);
            Check(Call(player, "IsMenuActive", Args(), interpreter).boolValue() == false,
                  "the store's Quit() drops it at once -- a conversation may open");
            Check(stack.currentMenu() == store,
                  "  although the port still holds the screen for the host (why it is a flag)");
            stack.ClearCloseMenuRequest();

            // `Quit(); OpenMenu(...)` in one handler -- dropgoldmenu.s's
            // back row does it -- ends with a menu up.
            Call(player, "OpenMenu", Args(skRValue(skString("Menus\DropGoldMenu"))), interpreter);
            if (stack.currentMenu()) Call(*stack.currentMenu(), "Quit", Args(), interpreter);
            Call(player, "OpenMenu", Args(skRValue(skString("charactermanager"))), interpreter);
            Check(Call(player, "IsMenuActive", Args(), interpreter).boolValue() == true,
                  "Quit() then OpenMenu() in one handler leaves it raised");
            stack.ClearCloseMenuRequest();
            if (stack.currentMenu()) Call(*stack.currentMenu(), "Quit", Args(), interpreter);
            stack.ClearCloseMenuRequest();
        }

        // ---------------------------------------------------------------
        // Part 5: SetPositionMirrorAll.
        // ---------------------------------------------------------------
        std::printf("\n== Part 5: SetPositionMirrorAll ==\n");
        {
            float x = 0, y = 0, z = 0;
            player.TakePendingPosition(x, y, z);  // drain anything earlier
            Call(player, "SetPositionMirrorAll",
                 Args(skRValue(4404), skRValue(12339), skRValue(0)), interpreter);
            Check(player.TakePendingPosition(x, y, z) && x == 4404 && y == 12339 && z == 0,
                  "crypt2.s's Port11 call moves the player to (4404, 12339, 0)");
            Call(player, "SetPositionMirrorAll", Args(skRValue(2472), skRValue(16872), skRValue(55)),
                 interpreter);
            float mx = 0, my = 0, mz = 0;
            player.TakePendingPosition(mx, my, mz);
            Call(player, "SetPosition", Args(skRValue(2472), skRValue(16872), skRValue(55)),
                 interpreter);
            player.TakePendingPosition(x, y, z);
            Check(mx == x && my == y && mz == z, "  identical to SetPosition with the same arguments");
            Call(player, "SetPositionMirrorAll", Args(skRValue(100), skRValue(200)), interpreter);
            Check(player.TakePendingPosition(x, y, z) && z == 0,
                  "  and the two-argument form puts z at 0, as case 0x30's does");
            Check(player.isActorForPositioning(), "  so the host still floor-snaps it (vtable[0xc8])");
        }

        // ---------------------------------------------------------------
        // Part 6: EnableCoords, SetGhost / IsGhost.
        // ---------------------------------------------------------------
        std::printf("\n== Part 6: EnableCoords and the ghost ==\n");
        {
            Check(!player.coordsEnabled(), "coordinates start hidden (constructor +0xfc0 = 0)");
            Call(player, "EnableCoords", Args(), interpreter);
            Check(player.coordsEnabled(), "EnableCoords() shows them");
            Call(player, "EnableCoords", Args(), interpreter);
            Check(!player.coordsEnabled(), "  and the second call hides them -- it is a toggle");

            Check(!player.ghost(), "the player starts solid (constructor +0x10a1 = 0)");
            stack.ReopenMenu("cheatmenu");
            sk_b::MenuExecutable* cheat = stack.currentMenu();
            Check(cheat != nullptr, "cheatmenu.s opens");
            if (cheat) {
                Call(*cheat, "More3", Args(skRValue(0)), interpreter);  // builds ghostItem
                Call(*cheat, "MakeGhost", Args(), interpreter);
                Check(player.ghost(), "its MakeGhost row turns the player into a ghost");
                Check(Call(player, "IsGhost", Args(), interpreter).boolValue(),
                      "  and IsGhost() says so");
                Call(*cheat, "MakeGhost", Args(), interpreter);
                Check(!player.ghost(), "  pressing it again turns the ghost off");
                Call(*cheat, "Quit", Args(), interpreter);
            }
        }

        // ---------------------------------------------------------------
        // Part 7: SetPlayerClassFlag.
        // ---------------------------------------------------------------
        std::printf("\n== Part 7: SetPlayerClassFlag ==\n");
        {
            Call(player, "ChooseCharacter", Args(skRValue(3)), interpreter);
            Check(Call(player, "HasCreatedCharacter", Args(), interpreter).boolValue(),
                  "a character has been made");
            stack.OpenMenu("MainMenu");
            sk_b::MenuExecutable* mainMenu = stack.currentMenu();
            Check(mainMenu != nullptr, "mainmenu.s opens");
            if (mainMenu) {
                Call(*mainMenu, "MenuNewGame", Args(skRValue(0)), interpreter);
                Check(!Call(player, "HasCreatedCharacter", Args(), interpreter).boolValue(),
                      "New Game's SetPlayerClassFlag(false) clears +0xf35 -- HasCreatedCharacter "
                      "reads false");
            }
        }
    } catch (skParseException& e) {
        std::printf("   (PARSE ERROR: %s)\n", e.toString().ptr());
        ++g_failures;
    } catch (skRuntimeException& e) {
        std::printf("   (RUNTIME ERROR: %s)\n", e.toString().ptr());
        ++g_failures;
    }

    // ---------------------------------------------------------------
    // Part 8: how fast a ghost walks.
    // ---------------------------------------------------------------
    std::printf("\n== Part 8: the ghost's speed ==\n");
    {
        const float scale = sk_b::GhostMoveSpeedScale();
        const double f = 1.0 - 0x50 / 256.0;  // FUN_10068814(v, 0x50)
        const double closedForm = 7.0 / 5.0 * (1.0 - f) / (1.0 - f / 4.0);
        std::printf("     integer chain %.4f, closed form %.4f\n", scale, closedForm);
        Check(std::fabs(scale - closedForm) < 0.01,
              "the engine's integer chain agrees with 7/5 * (1-f)/(1-f/4)");
        Check(scale > 0.5f && scale < 0.56f, "a ghost walks at about half speed, not faster");
        // The ratio should not be an artefact of the chosen acceleration.
        const float small = sk_b::GhostMoveSpeedScale(2000);
        Check(std::fabs(small - scale) < 0.02f, "  and barely moves with the acceleration");
        // Normal walking has to converge at all, or the ratio means nothing.
        int v = 0, a = 0, b = 0;
        for (int i = 0; i < 64; ++i) a = sk_b::ghost_detail::WalkTick(v, 102336, 10, false);
        b = sk_b::ghost_detail::WalkTick(v, 102336, 10, false);
        Check(a == b && a > 0, "  normal walking settles to a steady step (friction wins)");
    }

    std::printf("\nm95_gamestate_smoke: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
