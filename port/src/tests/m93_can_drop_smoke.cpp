// M93 smoke test: `Item.SetCanDrop`, and the drop it guards.
//
// 39 call sites, all of them `SetCanDrop(false)` in an item's `Init()`, and
// all of them quest items: the eleven Shadowkey fragments, the cell keys
// and the bandit key, the letters and the mages' roster, the Dark Star
// pass, the three raider amulets, the five snowline herbs, the twilite
// scrolls. It was the only thing a real driven play session soft-failed on,
// and the reason was simple -- `CanDrop()` answered a hardcoded `true`, so
// the Shadowkey itself could be thrown on the floor.
//
// What each part checks, and why it is the check that could fail:
//
//   1. The shipped corpus: 39 setters, all false, all in Init(); and the
//      only two readers, both in `inventory.s`.
//   2. The flag itself, on real scripts -- a quest item answers false, an
//      ordinary one answers the constructor's true.
//   3. `inventory.s`'s own guard, driven through the real screen: the
//      action popup's Drop row is blanked for a quest item and present for
//      an ordinary one, and `DropInventoryItem[]` refuses to raise the
//      confirmation.
//   4. The native gate underneath it. The engine checks the byte again in
//      `DropItem` itself, so a quest item is undroppable by any route, not
//      only by the route the script guards.
//   5. The quantity split: a stack drops one and keeps the rest.
//   6. Where a dropped item goes -- the `Loot_Dropped` bag, checked against
//      the real `entities.txt` row and the real script.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/popup_menu_executable.h"
#include "simkin_bindings/table_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"

namespace sk_b = sk_bindings;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("  %-86s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    if (!ok) ++g_failures;
}

std::string ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Every `SetCanDrop(` / `CanDrop(` in the shipped scripts, comment-stripped.
struct Site {
    std::string file;
    bool setter = false;
    bool argTrue = false;
    bool insideInit = false;
};

void CollectSites(const std::filesystem::path& file, std::vector<Site>& out) {
    std::ifstream in(file);
    if (!in) return;
    std::string line;
    bool inInit = false;
    int depth = 0;
    while (std::getline(in, line)) {
        size_t comment = line.find("//");
        if (comment != std::string::npos) line = line.substr(0, comment);
        // A crude but sufficient "are we inside Init[...]" tracker: the
        // corpus writes every handler as `Name[ (args)` then braces.
        if (line.find("Init[") != std::string::npos) {
            inInit = true;
            depth = 0;
        }
        for (char c : line) {
            if (c == '[') ++depth;
            if (c == ']') {
                --depth;
                if (inInit && depth <= 0) inInit = false;
            }
        }
        size_t at = line.find("CanDrop");
        while (at != std::string::npos) {
            // Whole call only: `charactermanager.s` has a local named
            // `bCanDropGold` that a bare substring search happily counts
            // three times.
            size_t open = at + std::string("CanDrop").size();
            while (open < line.size() && line[open] == ' ') ++open;
            if (open >= line.size() || line[open] != '(') {
                at = line.find("CanDrop", at + 1);
                continue;
            }
            const bool setter = at >= 3 && line.compare(at - 3, 3, "Set") == 0;
            Site site;
            site.file = file.filename().string();
            site.setter = setter;
            site.insideInit = inInit;
            site.argTrue = line.find("true") != std::string::npos;
            out.push_back(std::move(site));
            at = line.find("CanDrop", at + 1);
        }
    }
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

std::unique_ptr<sk_b::ItemExecutable> LoadItem(sk_b::MenuStack& stack, skInterpreter& interpreter,
                                                const std::string& root, const std::string& rel) {
    skExecutableContext loadCtxt(&interpreter);
    try {
        auto item = std::make_unique<sk_b::ItemExecutable>(skString((root + "/" + rel).c_str()),
                                                            loadCtxt, stack);
        skExecutableContext callCtxt(&interpreter);
        sk_b::RunEntityInit(*item, stack.scriptRoot(), callCtxt);
        return item;
    } catch (skParseException& e) {
        std::printf("   (PARSE ERROR in %s: %s)\n", rel.c_str(), e.toString().ptr());
    } catch (skRuntimeException& e) {
        std::printf("   (RUNTIME ERROR in %s: %s)\n", rel.c_str(), e.toString().ptr());
    }
    return nullptr;
}

sk_b::TableExecutable* InventoryTable(sk_b::MenuExecutable& menu) {
    for (const auto& row : menu.rows()) {
        if (row.kind == sk_b::MenuExecutable::RowKind::Table) {
            return static_cast<sk_b::TableExecutable*>(row.widget.get());
        }
    }
    return nullptr;
}

sk_b::PopupMenuExecutable* PopupField(sk_b::MenuExecutable& menu, const char* fieldName) {
    skRValue value;
    if (!menu.getValue(skString(fieldName), skString(""), value)) return nullptr;
    return dynamic_cast<sk_b::PopupMenuExecutable*>(value.obj());
}

void Invoke(sk_b::MenuExecutable& menu, const char* handler, skInterpreter& interpreter) {
    skRValueArray args;
    args.append(skRValue(0));
    skRValue ret;
    skExecutableContext ctxt(&interpreter);
    try {
        menu.method(skString(handler), args, ret, ctxt);
    } catch (skRuntimeException& e) {
        std::printf("   (RUNTIME ERROR in %s: %s)\n", handler, e.toString().ptr());
    }
}

// Walks the three inventory pages until the row holding `item` shows up.
int FindRowFor(sk_b::MenuExecutable& menu, skInterpreter& interpreter,
                const sk_b::ItemExecutable* item) {
    for (const char* page : {"ConsumablesMenu", "MiscItemsMenu", "WeaponsMenu",
                              "ArmorMenu", "SpellMenu"}) {
        Invoke(menu, page, interpreter);
        sk_b::TableExecutable* table = InventoryTable(menu);
        if (!table) continue;
        for (int row = 0; row < table->rowCount(); ++row) {
            const sk_b::TableCell* cell = table->PeekCell(static_cast<size_t>(row), 0);
            if (cell && cell->item == item) return row;
        }
    }
    return -1;
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
        std::printf("m93_can_drop_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }

    // ---------------------------------------------------------------
    // Part 1: the shipped corpus.
    // ---------------------------------------------------------------
    std::printf("\n== Part 1: what the shipped scripts ask for ==\n");
    {
        std::vector<Site> sites;
        for (const std::filesystem::path& f : AllScripts(root)) CollectSites(f, sites);

        int setters = 0, getters = 0, settersTrue = 0, settersInInit = 0;
        std::vector<std::string> readerFiles;
        for (const Site& s : sites) {
            if (s.setter) {
                ++setters;
                if (s.argTrue) ++settersTrue;
                if (s.insideInit) ++settersInInit;
            } else {
                ++getters;
                readerFiles.push_back(s.file);
            }
        }
        std::printf("     %d setters, %d getters\n", setters, getters);
        Check(setters == 39, "SetCanDrop has 39 call sites (" + std::to_string(setters) + ")");
        Check(settersTrue == 0, "  and not one of them passes true -- the flag is only ever "
                                "switched off");
        Check(settersInInit == setters,
              "  and every one is in the item's own Init(), so it is set before anything can "
              "read it");
        Check(getters == 2, "CanDrop() is read exactly twice (" + std::to_string(getters) + ")");
        std::sort(readerFiles.begin(), readerFiles.end());
        readerFiles.erase(std::unique(readerFiles.begin(), readerFiles.end()), readerFiles.end());
        Check(readerFiles.size() == 1 && readerFiles[0] == "inventory.s",
              "  and both reads are in inventory.s, the only screen that offers a Drop");
    }

    // ---------------------------------------------------------------
    // Part 2: the flag, on real item scripts.
    // ---------------------------------------------------------------
    std::printf("\n== Part 2: the flag itself ==\n");
    skInterpreter interpreter;
    sk_b::MenuStack stack(root, interpreter, &strings);
    {
        std::unique_ptr<sk_b::ItemExecutable> shadowkey =
            LoadItem(stack, interpreter, root, "items/shadowkey1.s");
        std::unique_ptr<sk_b::ItemExecutable> bread =
            LoadItem(stack, interpreter, root, "items/bread.s");
        Check(shadowkey && bread, "items/shadowkey1.s and items/bread.s both load");
        if (shadowkey && bread) {
            Check(!shadowkey->canDrop(),
                  "a Shadowkey fragment answers CanDrop() false -- its own SetCanDrop(false)");
            Check(bread->canDrop(),
                  "bread answers true -- the item constructors write 1 into entity+0x1c9");

            // Through the native, not just the accessor.
            skRValueArray none;
            skRValue ret;
            skExecutableContext ctxt(&interpreter);
            shadowkey->method(skString("CanDrop"), none, ret, ctxt);
            Check(ret.boolValue() == false, "  and the CanDrop() binding answers the same");
            bread->method(skString("CanDrop"), none, ret, ctxt);
            Check(ret.boolValue() == true, "  for both of them");

            // And it can be switched back on, which nothing shipped does.
            skRValueArray yes;
            yes.append(skRValue(true));
            shadowkey->method(skString("SetCanDrop"), yes, ret, ctxt);
            Check(shadowkey->canDrop(), "SetCanDrop(true) works too, though no shipped script "
                                        "does it");
            skRValueArray no;
            no.append(skRValue(false));
            shadowkey->method(skString("SetCanDrop"), no, ret, ctxt);
            Check(!shadowkey->canDrop(), "  and back off again");
        }
    }

    // ---------------------------------------------------------------
    // Parts 3-5: the real inventory screen.
    // ---------------------------------------------------------------
    std::printf("\n== Parts 3-5: the real inventory screen ==\n");
    try {
        stack.RequestGameStart("azra");

        // Two items in the bag: one quest item and one ordinary stack.
        sk_b::ItemExecutable* quest = nullptr;
        sk_b::ItemExecutable* stackable = nullptr;
        {
            std::unique_ptr<sk_b::ItemExecutable> q =
                LoadItem(stack, interpreter, root, "items/shadowkey1.s");
            std::unique_ptr<sk_b::ItemExecutable> b =
                LoadItem(stack, interpreter, root, "items/bread.s");
            if (b) b->SetQuantity(5);
            quest = q.get();
            stackable = b.get();
            if (q) stack.player().AddItem(std::move(q));
            if (b) stack.player().AddItem(std::move(b));
        }
        Check(quest && stackable, "a quest item and a 5-stack are in the inventory");

        stack.OpenMenu("inventory");
        sk_b::MenuExecutable* menu = stack.currentMenu();
        Check(menu != nullptr, "inventory.s opens");

        if (menu && quest && stackable) {
            sk_b::PopupMenuExecutable* actions = PopupField(*menu, "menuActionPopup");
            sk_b::PopupMenuExecutable* confirm = PopupField(*menu, "confirmDropPopup");
            Check(actions && confirm,
                  "its menuActionPopup and confirmDropPopup are both real popup objects");

            // ---- Part 3: the script's own guard ----------------------
            //
            //     SelectedInventoryItem[] {
            //         activeInventoryItem = inventoryTable.GetSelectedRow();
            //         inv = activeInventoryItem.GetAssociatedObject();
            //         ...
            //         if (inv.CanDrop() = false) UpdatePopupItem(2, "");
            //         else                       UpdatePopupItem(2, 4016);
            //     }
            //
            // Popup item 2 is the third `AddItem`, `AddItem(4016,
            // "DropInventoryItem")` -- the Drop row. Blanking it is
            // inventory.s's own convention for "hide this action".
            if (actions) {
                auto selectRow = [&](const sk_b::ItemExecutable* item) -> bool {
                    const int row = FindRowFor(*menu, interpreter, item);
                    if (row < 0) return false;
                    sk_b::TableExecutable* table = InventoryTable(*menu);
                    if (!table) return false;
                    table->SetSelectedRow(row);
                    Invoke(*menu, "SelectedInventoryItem", interpreter);
                    return true;
                };

                Check(selectRow(stackable), "the bread's row is selectable on a real page");
                Check(actions->items().size() > 2 && !actions->items()[2].blanked,
                      "  and the action popup offers Drop for it");

                Check(selectRow(quest), "the Shadowkey fragment's row is selectable");
                Check(actions->items().size() > 2 && actions->items()[2].blanked,
                      "  and the action popup has no Drop row at all -- "
                      "UpdatePopupItem(2, \"\")");

                // `DropInventoryItem[]` guards again on the way in.
                if (confirm) {
                    Invoke(*menu, "DropInventoryItem", interpreter);
                    Check(!confirm->visible(),
                          "DropInventoryItem on a quest item refuses to raise the confirmation");
                }
            }

            // ---- Part 4: the native gate ----------------------------
            //
            // The engine tests the same byte inside `DropItem` itself, so
            // the script's two checks are belt and braces rather than the
            // only thing standing between a player and a lost Shadowkey.
            {
                sk_b::TableExecutable* table = InventoryTable(*menu);
                const int row = FindRowFor(*menu, interpreter, quest);
                const size_t before = stack.player().inventory().size();
                if (table && row >= 0) {
                    table->DropRow(static_cast<size_t>(row));
                }
                stack.player().PurgeRemovedItems();
                Check(stack.player().inventory().size() == before,
                      "DropRow on a quest item does nothing -- the native gate, not the script");
                Check(stack.player().TakePendingDrops().empty(),
                      "  and nothing was queued to hit the floor");
                bool stillThere = false;
                for (const auto& item : stack.player().inventory()) {
                    if (item.get() == quest) stillThere = true;
                }
                Check(stillThere, "  the Shadowkey is still in the bag");
            }

            // ---- Part 5: the quantity split -------------------------
            //
            //     if (item->quantity == 1) { DropObject(item); remove it; }
            //     else { item->quantity -= 1; DropObject(a fresh copy); }
            {
                sk_b::TableExecutable* table = InventoryTable(*menu);
                const int row = FindRowFor(*menu, interpreter, stackable);
                Check(row >= 0 && table != nullptr, "the 5-stack has a row");
                if (table && row >= 0) {
                    Check(stackable->quantity() == 5, "  holding 5");
                    table->DropRow(static_cast<size_t>(row));
                    Check(stackable->quantity() == 4,
                          "dropping it drops ONE -- the stack keeps the other four");
                    Check(!stackable->markedForRemoval(),
                          "  and the item itself stays in the inventory");

                    // Drain it down to the last one, then drop that.
                    stackable->SetQuantity(1);
                    table->DropRow(static_cast<size_t>(row));
                    Check(stackable->markedForRemoval(),
                          "the last one leaves the inventory instead");
                    Check(stackable->markedForDrop(),
                          "  marked as a *drop*, so the purge hands it over rather than "
                          "destroying it");
                    stack.player().PurgeRemovedItems();
                    std::vector<std::unique_ptr<sk_b::ItemExecutable>> dropped =
                        stack.player().TakePendingDrops();
                    Check(!dropped.empty(),
                          "  and it comes out of the purge alive, ready to be a bag on the floor");
                    bool gone = true;
                    for (const auto& item : stack.player().inventory()) {
                        if (item.get() == stackable) gone = false;
                    }
                    Check(gone, "  while the inventory no longer lists it");
                }
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
    // Part 6: where a dropped item actually goes.
    // ---------------------------------------------------------------
    std::printf("\n== Part 6: the Loot_Dropped bag ==\n");
    {
        Check(sk_b::kDroppedLootTypeId == 300,
              "the drop bag is typeId 300, the literal in FUN_1002c3a8");
        Check(std::string(sk_b::kDroppedLootScript) == "Loot_Dropped",
              "  with the script tag \"Loot_Dropped\" (the literal at 0x100addf0)");

        // entities.txt line 212: `300 30 8 !bag_loot` -- model 30, category
        // 8 (container). The real case re-derives the category from the
        // descriptor before flagging the spawned object as a container.
        std::ifstream ents(root + "/entities.txt");
        std::string line;
        bool found = false;
        while (std::getline(ents, line)) {
            int typeId = 0, model = 0, category = 0;
            char name[128] = {};
            if (std::sscanf(line.c_str(), "%d %d %d %127s", &typeId, &model, &category, name) == 4 &&
                typeId == sk_b::kDroppedLootTypeId) {
                found = true;
                Check(category == 8, "entities.txt's row 300 is category 8 -- a container");
                Check(model == 30, "  and model 30, bag_dropped.bin");
                break;
            }
        }
        Check(found, "  and the row is really there");

        const std::string script = ReadFile(root + "/loot_dropped.s");
        Check(!script.empty(), "loot_dropped.s is a real shipped script");
        Check(script.find("SetUsable( true )") != std::string::npos,
              "  its Init() makes the bag usable");
        Check(script.find("OpenMenu(\"LootMenu\")") != std::string::npos,
              "  and its OnUse() opens LootMenu -- so a dropped item is recoverable");

        Check(sk_b::kLootDropRise == 300.0f,
              "both loot spawns raise the requested z by 300 before the floor snap");
    }

    std::printf("\n%d checks, %d failures -- %s\n", g_checks, g_failures,
                g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
