// M103 smoke test: the four item natives of audit item 5, and the item
// constructor defaults that finding turned up.
//
//   FUN_1002c848  the Item dispatcher: case 0 CastAzraWrath,
//                 case 6 DisplayPopup, case 0xe GetMarketValue,
//                 cases 0x14/0x15/0x16 the three MoveTo*Queue markers
//   FUN_1002eeb8  the misc-item constructor, whose opening block every
//                 other item arm repeats verbatim -- icon/cost/market
//                 value/weight/quantity/canDrop
//   FUN_10047740  the spell arm, the one that overrides the icon
//   FUN_10035368  DisplayPopup's body: the menu+0xa8 message popup
//   FUN_1003136c  the menu class's own case 0xa DisplayPopup and
//                 case 0xb GetMessagePopup
//   FUN_1002fd94  the message popup's activate slot: row 1 hides it and
//                 calls OnMsgPopupClosed on the owning menu script
//   FUN_1004720c  the area sweep CastAzraWrath hands its roll to
//   FUN_100646a8  OnUsedBy: "if usable, call the script's OnUse" -- and
//                 nothing else, which is what this port had wrong
//   FUN_1008d0e4  the table class's case 4 RemoveRow: unlink the row
//                 object, never touch its item
//
// Part 1   the constructor defaults, against the shipped corpus
// Part 2   GetMarketValue, and what the merchant pays
// Part 3   the three queue markers
// Part 4   DisplayPopup, through Trothgar's magicka potion
// Part 4b  RemoveRow, the other half of the same bug
// Part 5   CastAzraWrath, through both vermin bombs
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/popup_menu_executable.h"
#include "simkin_bindings/spell_cast.h"
#include "simkin_bindings/table_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"

namespace sk_b = sk_bindings;
using Item = sk_b::ItemExecutable;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("  %-92s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    if (!ok) ++g_failures;
}

// entities.txt rows this test drives.
constexpr int kDagger = 55;               // dagger.s -- names no market value
constexpr int kChainCoif = 614;           // armor/chain_coif.s -- names no icon
constexpr int kVerminBomb = 711;          // items/vermin_bomb.s
constexpr int kTrothgarsPotion = 4715;    // items/trothgars_magicka_potion.s
constexpr int kOlpacPack = 4005;          // ghstpass/olpac_pack.s
constexpr int kBlaze = 50;                // spells/blaze.s -- sets its own icon
constexpr int kBread = 700;               // items/bread.s, a plain consumable
// spells/ignitescroll.s, one of the seven placed spell rows that name no
// icon (the other six are the u_*.s upgrade scripts).
constexpr int kIgniteScroll = 4036;

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string root =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    if (!strings.Load(root + "/stringtable.eng")) {
        std::printf("m103_item_natives_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }
    sk::EntityTypeTable entityTypes;
    entityTypes.Load(root);
    skInterpreter interpreter;
    sk_b::MenuStack stack(root, interpreter, &strings);
    stack.level().SetEntityTypes(&entityTypes);
    sk_b::PlayerExecutable& player = stack.player();

    auto call = [&](skiExecutable* obj, const char* name, skRValueArray args) {
        skRValue ret;
        skExecutableContext ctxt(&interpreter);
        try {
            obj->method(skString(name), args, ret, ctxt);
        } catch (skRuntimeException& e) {
            std::printf("   (RUNTIME ERROR in %s: %s)\n", name, e.toString().ptr());
        }
        return ret;
    };
    auto noArgs = []() { return skRValueArray(); };
    auto make = [&](int typeId) {
        return stack.level().CreateItem(typeId, /*requireItemCategory=*/false);
    };

    // ---------------------------------------------------------------
    // Part 1: what an item is before its script says a word
    // ---------------------------------------------------------------
    std::printf("\n== Part 1: the constructor defaults ==\n");
    {
        std::unique_ptr<Item> coif = make(kChainCoif);
        Check(coif != nullptr, "armor/chain_coif.s loads");
        if (coif) {
            // It calls SetName/SetArmorValue/SetCost/SetMarketValue and
            // never SetIcon -- like 184 other item scripts, every piece of
            // armour among them.
            Check(coif->icon() == sk_b::kItemDefaultIcon,
                  "a script that never calls SetIcon keeps FUN_1002eeb8's 0x1d");
        }
        std::unique_ptr<Item> dagger = make(kDagger);
        Check(dagger != nullptr, "dagger.s loads");
        if (dagger) {
            // dagger.s is the shortest weapon script in the game: a name, a
            // sprite, a range, a damage spread, and nothing else. No
            // SetCost, no SetMarketValue, no SetIcon.
            Check(dagger->marketValue() == sk_b::kItemDefaultMarketValue &&
                      dagger->cost() == sk_b::kItemDefaultCost,
                  "and one that names neither price keeps the constructor's 0x14 and 0x1e");
            Check(dagger->icon() == sk_b::kItemDefaultIcon,
                  "  and no icon either, so the starting dagger has one at all now");
        }
        // The spell arm's own icon, 0x3d -- which is real in the engine and
        // which **no placed spell in the shipped game can show**. All 38
        // category-5/14 rows end up running a script that calls SetIcon:
        // 31 call it themselves, and the other seven (ignitescroll.s and
        // the six u_*.s upgrades) RunScript into one that does. So this is
        // checked where it is observable -- on the object before Init() --
        // and recorded as unreachable from the shipped data, the same shape
        // as M102's WalkTo arrival.
        std::unique_ptr<Item> spell = make(kBlaze);
        Check(spell != nullptr, "blaze.s loads");
        if (spell) {
            Check(spell->itemType() == sk_b::kItemTypeSpell, "  and is a spell");
            Check(spell->icon() == 209, "  whose own SetIcon(209) is what the player sees");
            Check(spell->marketValue() == 112, "  and its own SetMarketValue(112)");
        }
        std::unique_ptr<Item> scroll = make(kIgniteScroll);
        Check(scroll != nullptr, "spells/ignitescroll.s loads");
        if (scroll) {
            // Its Init is four setters and `RunScript("spells\IgniteFoe")`,
            // and IgniteFoe.s calls SetIcon(209). The swap is M92's.
            Check(scroll->icon() == 209,
                  "  and the seven icon-less spell rows all RunScript into one that sets it");
        }
        Check(sk_b::kSpellDefaultIcon != sk_b::kItemDefaultIcon,
              "the spell constructor's 0x3d is a different slot from the base 0x1d");
        // The default is only a default: a script's own call still wins,
        // which is what keeps every pre-M103 expectation intact.
        std::unique_ptr<Item> bomb = make(kVerminBomb);
        Check(bomb && bomb->cost() == 400 && bomb->marketValue() == 140 && bomb->icon() == 210,
              "items/vermin_bomb.s's own SetCost/SetMarketValue/SetIcon are untouched");
    }

    // ---------------------------------------------------------------
    // Part 2: GetMarketValue
    // ---------------------------------------------------------------
    std::printf("\n== Part 2: GetMarketValue ==\n");
    {
        std::unique_ptr<Item> bomb = make(kVerminBomb);
        Check(bomb != nullptr, "a priced item loads");
        if (bomb) {
            const int got = call(bomb.get(), "GetMarketValue", noArgs()).intValue();
            Check(got == 140, "GetMarketValue() answers SetMarketValue's number (140)");
            Check(call(bomb.get(), "GetCost", noArgs()).intValue() == 400,
                  "  and is a different field from GetCost (400) -- buy price vs sell price");
        }
        // The visible half: what the merchant actually pays. buysell.s reads
        // GetMarketValue into a local it drops on the floor and then calls
        // GetPlayer().SellItem(inventory), which uses the same number -- so
        // this is the check that matters for the ten droppable items whose
        // scripts never set one.
        std::unique_ptr<sk_b::MonsterExecutable> merchant;
        try {
            skExecutableContext ctxt(&interpreter);
            const std::string path = root + "/dstar_w/weapons_merchant.s";
            merchant = std::make_unique<sk_b::MonsterExecutable>(skString(path.c_str()), ctxt,
                                                                  &strings, player, stack);
            skRValueArray initArgs;
            initArgs.append(skRValue(0));
            call(merchant.get(), "Init", initArgs);
        } catch (skParseException& e) {
            std::printf("   (PARSE ERROR loading the merchant: %s)\n", e.toString().ptr());
        }
        std::unique_ptr<Item> dagger = make(kDagger);
        Check(merchant != nullptr && dagger != nullptr, "a real merchant and a dagger");
        if (merchant && dagger) {
            skRValueArray setMerchant;
            setMerchant.append(skRValue(static_cast<skiExecutable*>(merchant.get()), false));
            call(&player, "SetMerchant", setMerchant);
            Item* raw = dagger.get();
            player.AddItem(std::move(dagger));
            const int goldBefore = player.gold();
            skRValueArray sell;
            sell.append(skRValue(static_cast<skiExecutable*>(raw), false));
            const int paid = call(&player, "SellItem", sell).intValue();
            Check(paid == 1 && player.gold() == goldBefore + sk_b::kItemDefaultMarketValue,
                  "so selling a dagger now pays 20, not the 0 an unset field used to");
        }
    }

    // ---------------------------------------------------------------
    // Part 3: the queue markers
    // ---------------------------------------------------------------
    std::printf("\n== Part 3: MoveTo*Queue ==\n");
    {
        std::unique_ptr<Item> bomb = make(kVerminBomb);
        Check(bomb != nullptr, "a consumable loads");
        if (bomb) {
            // Category 9: the consumable constructor's `+0x1c0 = 0`.
            Check(bomb->equipSlot() == sk_b::kEquipSlotLeft,
                  "a consumable starts in the left hand (FUN_1002e78c)");
            call(bomb.get(), "MoveToRightQueue", noArgs());
            Check(bomb->equipSlot() == sk_b::kEquipSlotRight, "MoveToRightQueue writes 1");
            call(bomb.get(), "MoveToEmptyQueue", noArgs());
            Check(bomb->equipSlot() == sk_b::kEquipSlotNone, "MoveToEmptyQueue writes 2");
            call(bomb.get(), "MoveToLeftQueue", noArgs());
            Check(bomb->equipSlot() == sk_b::kEquipSlotLeft, "MoveToLeftQueue writes 0");
        }
        // And the shipped caller, which cannot tell: olpac_pack.s is
        // category 3, and the misc constructor already wrote 2 there.
        std::unique_ptr<Item> pack = make(kOlpacPack);
        Check(pack != nullptr, "ghstpass/olpac_pack.s loads");
        if (pack) {
            Check(pack->equipSlot() == sk_b::kEquipSlotNone,
                  "  its Init's MoveToEmptyQueue() restates the value it already had");
        }
    }

    // ---------------------------------------------------------------
    // Part 4: DisplayPopup
    // ---------------------------------------------------------------
    std::printf("\n== Part 4: Trothgar's magicka potion ==\n");
    {
        // The potion needs a screen to put its popup on: the item native
        // resolves `engine+0x28`'s current menu and does nothing without
        // one.
        stack.OpenMenu("inventory");
        sk_b::MenuExecutable* screen = stack.currentMenu();
        Check(screen != nullptr, "the inventory screen is open");
        std::unique_ptr<Item> potion = make(kTrothgarsPotion);
        Check(potion != nullptr, "items/trothgars_magicka_potion.s loads");
        if (screen && potion) {
            Check(screen->messagePopup() == nullptr,
                  "a fresh screen has no message popup -- GetMessagePopup() answers null");
            // The refusal branch: fatigue below 50.
            player.SetActorFatigue(10);
            const int magickaBefore = player.magicka();
            skRValueArray usedBy;
            usedBy.append(skRValue(static_cast<skiExecutable*>(&player), false));
            call(potion.get(), "OnUsedBy", usedBy);
            sk_b::PopupMenuExecutable* popup = screen->messagePopup();
            Check(popup != nullptr && popup->visible(),
                  "using it on 10 fatigue puts the screen's message popup up");
            Check(player.magicka() == magickaBefore && !potion->markedForRemoval(),
                  "  and the potion is neither drunk nor destroyed");
            if (popup) {
                Check(popup->items().size() == 2 &&
                          popup->items()[0].literalText == "Must have at least 50 fatigue!",
                      "  row 0 carries the script's own literal");
                Check(popup->items()[1].literalText == "Back" &&
                          popup->items()[1].callback == "OnMsgPopupClosed",
                      "  and row 1 is the engine's \"Back\", the only selectable one");
                Check(popup->IsItemSelected(1), "  which is where the selection opens");
                Check(screen->activePopup() == popup,
                      "  it is the screen's active modal, so input and rendering find it");
                // FUN_1002fd94: activating row 1 hides it and calls
                // OnMsgPopupClosed -- which inventory.s defines.
                popup->ActivateSelected();
                Check(!popup->visible(), "activating that row hides the popup");
                Check(screen->activePopup() == nullptr, "  and hands the back key back");
            }
            // The other branch: enough fatigue, so no popup and a real drink.
            player.SetActorFatigue(80);
            skRValueArray zeroMagicka;
            zeroMagicka.append(skRValue(0));
            call(&player, "SetMagicka", zeroMagicka);
            const int fatigueBefore = player.fatigue();
            call(potion.get(), "OnUsedBy", usedBy);
            Check(player.fatigue() == fatigueBefore - 50, "on 80 fatigue the potion costs 50");
            Check(player.magicka() == 25, "  and pays 25 magicka");
            Check(potion->markedForRemoval(), "  and DestroyObject(self) consumes it");
            Check(screen->messagePopup() != nullptr && !screen->messagePopup()->visible(),
                  "  with no popup this time -- the one from before stays built but hidden");
        }
    }

    // ---------------------------------------------------------------
    // Part 4b: RemoveRow, the other half of the same bug
    // ---------------------------------------------------------------
    std::printf("\n== Part 4b: inventory.s's RemoveRow ==\n");
    {
        // `UseItem()` is two lines: `inventoryTable.RemoveRow(...)` and
        // then `inv.OnUsedBy(GetPlayer())`. This port used to route the
        // first one through DropRow -- the *Drop* action -- so using
        // anything put it in a loot bag on the floor before OnUse ran. The
        // real binding (`FUN_1008d0e4` case 4) unlinks the row object and
        // nothing else.
        stack.OpenMenu("inventory");
        sk_b::MenuExecutable* screen = stack.currentMenu();
        sk_b::TableExecutable* table = nullptr;
        if (screen) {
            for (const auto& row : screen->rows()) {
                if (row.kind == sk_b::MenuExecutable::RowKind::Table) {
                    table = static_cast<sk_b::TableExecutable*>(row.widget.get());
                    break;
                }
            }
        }
        Check(table != nullptr, "inventory.s's own table is on the screen");
        std::unique_ptr<Item> bread = make(kBread);
        Check(bread != nullptr, "items/bread.s loads");
        if (table && bread) {
            Item* raw = bread.get();
            player.AddItem(std::move(bread));
            // Put it in a row the way DisplayConsumablesMenu does.
            skRValueArray listArgs;
            listArgs.append(skRValue(static_cast<skiExecutable*>(table), false));
            call(screen, "SetInventoryList", listArgs);
            skRValueArray pageArgs;
            pageArgs.append(skRValue(0));
            call(screen, "DisplayConsumablesMenu", pageArgs);
            int found = -1;
            for (int i = 0; i < table->rowCount(); ++i) {
                if (table->AssociatedItem(static_cast<size_t>(i)) == raw) found = i;
            }
            Check(found >= 0, "  and the bread is one of its rows");
            if (found >= 0) {
                const int before = table->rowCount();
                // The row object inventory.s passes is the one its own
                // SetCallback handed it -- the selected row. Point the
                // table at the bread and ask for the same handle.
                table->SetSelectedRow(found);
                skRValueArray none;
                skRValue rowHandle = call(table, "GetSelectedRow", none);
                Check(rowHandle.obj() != nullptr, "  GetSelectedRow names it");
                skRValueArray rowArg;
                rowArg.append(rowHandle);
                call(table, "RemoveRow", rowArg);
                Check(table->rowCount() == before - 1, "RemoveRow takes the display row away");
                Check(!raw->markedForRemoval() && player.CarriesItem(raw),
                      "  and leaves the item in the bag -- it is not the Drop action");
                player.PurgeRemovedItems();
                Check(player.CarriesItem(raw), "  which survives the purge, and drops nothing");
            }
        }
    }

    // ---------------------------------------------------------------
    // Part 5: CastAzraWrath
    // ---------------------------------------------------------------
    std::printf("\n== Part 5: the vermin bomb ==\n");
    {
        // Drain anything an earlier part left queued.
        stack.TakePendingAreaBlasts();
        std::unique_ptr<Item> bomb = make(kVerminBomb);
        Check(bomb != nullptr, "items/vermin_bomb.s loads");
        if (bomb) {
            Item* raw = bomb.get();
            player.AddItem(std::move(bomb));
            const int healthBefore = player.health();
            skRValueArray usedBy;
            usedBy.append(skRValue(static_cast<skiExecutable*>(&player), false));
            call(raw, "OnUsedBy", usedBy);
            std::vector<sk_b::MenuStack::AreaBlast> blasts = stack.TakePendingAreaBlasts();
            Check(blasts.size() == 1, "its OnUse queues exactly one blast");
            if (blasts.size() == 1) {
                Check(blasts[0].damage >= sk_b::kAzraWrathDamageMin &&
                          blasts[0].damage <= sk_b::kAzraWrathDamageMax,
                      "rolled Random(2, 12), the native's own literals");
                Check(blasts[0].origin == static_cast<const skiExecutable*>(&player),
                      "centred on the item's owner -- who is excluded from the sweep");
            }
            // The user's own share is the script's AddEffect, not the
            // blast: a separate roll, in the opposite direction.
            const int lost = healthBefore - player.health();
            Check(lost >= sk_b::kAzraWrathDamageMin && lost <= sk_b::kAzraWrathDamageMax,
                  "and the thrower takes GetOwner().AddEffect's own -2..-12, separately");
            Check(raw->markedForRemoval(), "the bomb destroys itself");
        }
        // The roll is a roll: over many uses it must cover the range and
        // stay inside it.
        std::set<int> seen;
        for (int i = 0; i < 400; ++i) {
            std::unique_ptr<Item> again = make(kVerminBomb);
            if (!again) break;
            call(again.get(), "CastAzraWrath", noArgs());
            for (const sk_b::MenuStack::AreaBlast& b : stack.TakePendingAreaBlasts()) {
                seen.insert(b.damage);
            }
        }
        Check(!seen.empty() && *seen.begin() == sk_b::kAzraWrathDamageMin &&
                  *seen.rbegin() == sk_b::kAzraWrathDamageMax,
              "400 rolls land on 2 and on 12, and nowhere outside");
        Check(seen.size() == static_cast<size_t>(sk_b::kAzraWrathDamageMax -
                                                  sk_b::kAzraWrathDamageMin + 1),
              "  hitting every value in between -- an inclusive range, as FUN_100730c8 is");
    }

    std::printf("\nm103_item_natives_smoke: %d/%d checks passed -- %s\n", g_checks - g_failures,
                g_checks, g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
