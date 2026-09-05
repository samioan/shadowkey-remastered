// M59 smoke test: merchants.
//
// The whole shop system, from the file up: `products.dat` (which this port
// had never opened), the store object every merchant creature carries at
// `monster+0x330`, the three natives that fill it, and the buy/sell verbs
// on the player. Everything checked here is either decompiled
// (`FUN_10035634` for the file, `FUN_10035a48`/`c28`/`da8`/`e34`/`e50`/
// `ecc`/`f40`/`fa4` for the store, `FUN_1003e030` for BuyItem, and the
// Player dispatcher `FUN_1003f130`) or read straight out of shipped data,
// including the ten real merchant scripts, which are run for real.
//
// Part 1  products.dat, and the 363 shipped AddProduct calls against it
// Part 2  a real merchant's shelves
// Part 3  ClearProducts, and the counters it does not clear
// Part 4  ReducePrices
// Part 5  buying: the four return codes
// Part 6  selling: what a merchant will and will not take
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "assets/product_database.h"
#include "assets/string_table.h"
#include "simkin_bindings/effects.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/native_binding_common.h"
#include "simkin_bindings/monster_executable.h"
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
    std::printf("%-74s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    ++g_checks;
    if (!ok) ++g_failures;
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    strings.Load(std::string(scriptRoot) + "/stringtable.eng");
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot)) {
        std::printf("m59_merchant_smoke: FAILED to load entities.txt\n");
        return 1;
    }

    skInterpreter interpreter;
    sk_b::MenuStack stack(scriptRoot, interpreter, &strings);
    stack.level().SetEntityTypes(&entityTypes);
    sk_b::PlayerExecutable& player = stack.player();

    auto call = [&](auto* obj, const char* name, skRValueArray& args) {
        skRValue ret;
        skExecutableContext ctxt(&interpreter);
        obj->method(skString(name), args, ret, ctxt);
        return ret;
    };
    auto loadMerchant = [&](const char* relPath) -> std::unique_ptr<sk_b::MonsterExecutable> {
        std::string full = std::string(scriptRoot) + "/" + relPath;
        skExecutableContext ctxt(&interpreter);
        try {
            auto m = std::make_unique<sk_b::MonsterExecutable>(skString(full.c_str()), ctxt,
                                                                &strings, player, stack);
            skRValueArray args;
            args.append(skRValue(0));
            skRValue ret;
            skExecutableContext c2(&interpreter);
            m->method(skString("Init"), args, ret, c2);
            return m;
        } catch (skParseException& e) {
            std::printf("  PARSE ERROR in %s: %s\n", relPath, e.toString().ptr());
        } catch (skRuntimeException& e) {
            std::printf("  RUNTIME ERROR in %s: %s\n", relPath, e.toString().ptr());
        }
        return nullptr;
    };

    const sk::ProductDatabase& db = stack.products();

    // ---- Part 1: products.dat ----
    {
        std::printf("\n-- products.dat --\n");
        Check(db.loaded() && db.version() == 36 && db.size() == 279,
              "products.dat: version 36, 279 records, exact byte consumption");

        // Three rows the shop scripts name, so the names can be checked
        // against the scripts' own string literals.
        const sk::ProductRecord* band = db.Find(662);
        const sk::ProductRecord* pauldron = db.Find(678);
        const sk::ProductRecord* herb = db.Find(4719);
        Check(band && strings.Get(band->nameStringId) == "Shadow Band" && band->price == 4444 &&
                  band->rating == 40 && band->category == sk_b::kItemTypeArmor,
              "662 is the Shadow Band: 4444 gold, rating 40, IPT_Armor");
        // The point of the whole file: the handler ignores the script's
        // price argument, and the two disagree.
        Check(pauldron && pauldron->price == 1111,
              "678's price is 1111, not the 111 blk_market.s writes next to it");
        Check(herb && herb->price == 45,
              "4719's is 45, not the 30 in the same call -- 65 of 363 sites disagree");

        // Every id the shipped merchants stock has to resolve, or the
        // engine logs a miss and the shelf line silently vanishes.
        int sites = 0, misses = 0;
        static const char* kMerchants[] = {
            "dstar_e/blk_market.s",          "dstar_e/blk_market_merchant.s",
            "dstar_w/armor_merchant.s",      "dstar_w/consumables_merchant.s",
            "dstar_w/weapons_merchant.s",    "monsters/eranthos_merchant.s",
            "monsters/gravel_trothgar.s",    "monsters/llewydr.s",
            "monsters/wendek_freetalker.s",  "stouttp/egrienstout.s",
        };
        for (const char* relPath : kMerchants) {
            std::unique_ptr<sk_b::MonsterExecutable> m = loadMerchant(relPath);
            if (!m) continue;
            for (const sk_b::Store::StockEntry& e : m->store().stock()) {
                ++sites;
                if (!e.record) ++misses;
            }
        }
        Check(sites == 363 && misses == 0,
              "all 363 AddProduct lines across the 10 merchant scripts resolve");

        // The armour slot partitions the armour rows and is 0 for
        // everything else -- 99 armour rows, 89 of them in slots 1..7.
        int armor = 0, slotted = 0, badFlags = 0;
        for (const sk::ProductRecord& p : db.records()) {
            if (p.category == sk_b::kItemTypeArmor) ++armor;
            if (p.armorSlot != 0) {
                ++slotted;
                if (p.category != sk_b::kItemTypeArmor) ++badFlags;
            }
            for (int c = 0; c < sk::kProductClassCount; ++c) {
                if (p.classFlags[c] > 1) ++badFlags;
            }
        }
        Check(armor == 99 && slotted == 89 && badFlags == 0,
              "99 armour rows, 89 of them slotted 1..7, and no slot on anything else");
    }

    // ---- Part 2: a real merchant's shelves ----
    std::unique_ptr<sk_b::MonsterExecutable> weaponsMerchant;
    {
        std::printf("\n-- a real merchant's shelves --\n");
        weaponsMerchant = loadMerchant("dstar_w/weapons_merchant.s");
        Check(weaponsMerchant != nullptr, "dstar_w/weapons_merchant.s loads and runs its Init()");
        if (!weaponsMerchant) {
            std::printf("\nm59_merchant_smoke: FAILED (%d checks)\n", g_checks);
            return 1;
        }
        sk_b::Store& shop = weaponsMerchant->store();
        Check(shop.stock().size() == 34 && shop.productCount(sk_b::kItemTypeWeapon) == 34,
              "34 lines, all IPT_Weapon, and the category counter agrees");
        Check(shop.productCount(sk_b::kItemTypeArmor) == 0 &&
                  shop.productCount(sk_b::kItemTypeSpell) == 0,
              "...and nothing in any other category");
        Check(shop.defaultCategory() == sk_b::kItemTypeWeapon,
              "GetDefaultStore picks the first non-empty category -- Weapon");

        const sk_b::Store::StockEntry* mace = shop.FindByName("Daedric Mace");
        Check(mace && mace->record && mace->record->templateId == 525 && mace->price == 953,
              "the by-name lookup finds 'Daedric Mace' at 953 gold");
        Check(shop.FindByName("Shadow Band") == nullptr,
              "...and does not find something this merchant does not stock");

        // wendek_freetalker.s lists template 704 twice, under two different
        // names ("Rat I" and "Ratseye"). The handler appends unconditionally,
        // so that really is two shelf lines sharing one product row.
        std::unique_ptr<sk_b::MonsterExecutable> wendek =
            loadMerchant("monsters/wendek_freetalker.s");
        int lines704 = 0;
        if (wendek) {
            for (const sk_b::Store::StockEntry& e : wendek->store().stock()) {
                if (e.record && e.record->templateId == 704) ++lines704;
            }
        }
        Check(wendek && lines704 == 2,
              "wendek_freetalker.s's duplicate id 704 really does make two shelf lines");
    }

    // ---- Part 3: ClearProducts, and what it leaves behind ----
    {
        std::printf("\n-- ClearProducts --\n");
        sk_b::Store& shop = weaponsMerchant->store();
        const int before = shop.productCount(sk_b::kItemTypeWeapon);
        skRValueArray none;
        call(weaponsMerchant.get(), "ClearProducts", none);
        Check(shop.stock().empty(), "ClearProducts empties the shelf list");
        Check(shop.productCount(sk_b::kItemTypeWeapon) == before,
              "...and leaves the category counter exactly where it was -- the real leak");
        // Which means the store still claims a default tab it cannot fill.
        Check(shop.defaultCategory() == sk_b::kItemTypeWeapon &&
                  shop.FindByName("Daedric Mace") == nullptr,
              "...so GetDefaultStore still says Weapon for a shop with nothing on it");

        // Rebuild it for the rest of the test.
        weaponsMerchant = loadMerchant("dstar_w/weapons_merchant.s");
    }

    // ---- Part 4: ReducePrices ----
    {
        std::printf("\n-- ReducePrices --\n");
        sk_b::Store& shop = weaponsMerchant->store();
        skRValueArray args;
        args.append(skRValue(20));
        call(weaponsMerchant.get(), "ReducePrices", args);
        const sk_b::Store::StockEntry* mace = shop.FindByName("Daedric Mace");
        // wendek_freetalker.s's only caller: `if (QuestCompleted(17)) ReducePrices(20);`
        Check(mace && mace->price == 953 - 953 * 20 / 100 && shop.priceReduction() == 20,
              "ReducePrices(20) takes a fifth off every line, from the base price");
        skRValueArray back;
        back.append(skRValue(0));
        call(weaponsMerchant.get(), "ReducePrices", back);
        Check(shop.FindByName("Daedric Mace")->price == 953,
              "...and it is recomputed from the base, so 0 puts every price back");
    }

    // ---- Part 5: buying ----
    {
        std::printf("\n-- buying --\n");
        skRValueArray setMerchant;
        setMerchant.append(skRValue(static_cast<skiExecutable*>(weaponsMerchant.get()), false));
        call(&player, "SetMerchant", setMerchant);
        Check(player.merchant() == &weaponsMerchant->store(),
              "SetMerchant(self) points the player at that creature's own shop");

        skRValueArray none;
        Check(call(&player, "GetDefaultStore", none).intValue() == sk_b::kItemTypeWeapon,
              "GetDefaultStore answers over the script bridge too");
        skRValueArray countArgs;
        countArgs.append(skRValue(sk_b::kItemTypeWeapon));
        Check(call(&player, "GetProductCount", countArgs).intValue() == 34,
              "GetProductCount(IPT_Weapon) is 34");

        // The description, which is what buysell.s's info popup shows.
        skRValueArray descArgs;
        descArgs.append(skRValue(skString("Daedric Mace")));
        const std::string description = sk_b::ToStdString(call(&player, "GetItemDescription", descArgs).str());
        Check(description == "Blunt, damage 6 - 16",
              "GetItemDescription returns the product's description line");

        auto buy = [&](const char* name, int count) {
            skRValueArray args;
            args.append(skRValue(skString(name)));
            args.append(skRValue(count));
            return call(&player, "BuyItem", args).intValue();
        };

        // 0 -- not enough gold. The player starts with none.
        Check(player.gold() == 0 && buy("Daedric Mace", 1) == 0,
              "BuyItem returns 0 with no gold, and buysell.s says 'Not Enough Gold!'");

        skRValueArray gold;
        gold.append(skRValue(5000));
        call(&player, "SetGold", gold);

        // 2 -- the merchant does not have that many. Every weapon line in
        // this shop has a quantity of 1.
        Check(buy("Daedric Mace", 2) == 2 && player.gold() == 5000,
              "BuyItem returns 2 for more than the shelf holds, and charges nothing");

        // 1 -- bought.
        const int itemsBefore = player.inventoryCount();
        const int result = buy("Daedric Mace", 1);
        Check(result == 1 && player.gold() == 5000 - 953 &&
                  player.inventoryCount() == itemsBefore + 1,
              "BuyItem returns 1, takes the gold and puts a real item in the bag");
        sk_b::ItemExecutable* bought = player.inventory().back().get();
        Check(bought && bought->templateId() == 525 && bought->rating() == 22,
              "...and the item's rating comes from the products.dat row, not its script");
        Check(weaponsMerchant->store().FindByName("Daedric Mace") == nullptr,
              "...and the shelf line is out of stock, so the name stops resolving");

        // 0 again -- the product is gone from the shelf entirely.
        Check(buy("Daedric Mace", 1) == 0, "buying the last one leaves nothing to buy");
    }

    // ---- Part 6: selling ----
    {
        std::printf("\n-- selling --\n");
        sk_b::Store& shop = weaponsMerchant->store();
        sk_b::ItemExecutable* mace = player.inventory().back().get();
        const int goldBefore = player.gold();
        const int marketValue = mace->marketValue();

        skRValueArray sellArgs;
        sellArgs.append(skRValue(static_cast<skiExecutable*>(mace), false));
        const int sold = call(&player, "SellItem", sellArgs).intValue();
        player.PurgeRemovedItems();
        Check(sold == 1 && player.gold() == goldBefore + marketValue,
              "SellItem pays the item's own market value");
        Check(shop.FindByName("Daedric Mace") != nullptr,
              "...and the merchant puts it straight back on the shelf");

        // What a merchant will not take. Category 0 (Misc) is refused
        // outright -- the four-way type test in FUN_10035c28 lists every
        // category except it.
        Check(!shop.AcceptItem(806, sk_b::kItemTypeMisc, 1),
              "a Misc item is refused: 'The merchant is not interested'");
        // A Spell is accepted -- the player loses it and is paid -- but a
        // merchant who does not already deal in spells puts nothing on the
        // shelf, because that guard sits after the accept decision.
        const int spellsBefore = shop.productCount(sk_b::kItemTypeSpell);
        Check(shop.AcceptItem(4021, sk_b::kItemTypeSpell, 1) &&
                  shop.productCount(sk_b::kItemTypeSpell) == spellsBefore,
              "a spell sold to a weapons merchant is paid for and then vanishes");
        // A merchant who does deal in spells keeps it.
        std::unique_ptr<sk_b::MonsterExecutable> llewydr = loadMerchant("monsters/llewydr.s");
        Check(llewydr && llewydr->store().productCount(sk_b::kItemTypeSpell) == 2,
              "monsters/llewydr.s stocks two spells and nothing else");
        if (llewydr) {
            const int before = llewydr->store().productCount(sk_b::kItemTypeSpell);
            llewydr->store().AcceptItem(4022, sk_b::kItemTypeSpell, 1);
            Check(llewydr->store().productCount(sk_b::kItemTypeSpell) == before + 1,
                  "...so a spell sold to that one really does reach the shelf");
        }
    }

    // ---- the two verbs that open the screen ----
    {
        std::printf("\n-- BuyFromMerchant / SellToMerchant --\n");
        skRValueArray none;
        call(&player, "SellToMerchant", none);
        Check(!stack.storeBuyMode() && stack.currentMenu() != nullptr,
              "SellToMerchant opens buysell.s with the buy flag clear");
        skRValue isBuy;
        skExecutableContext ctxt(&interpreter);
        skRValueArray noArgs;
        stack.currentMenu()->method(skString("IsBuyMode"), noArgs, isBuy, ctxt);
        Check(!isBuy.boolValue(), "...and the script's own IsBuyMode() reads it back as false");
        call(&player, "BuyFromMerchant", none);
        Check(stack.storeBuyMode(), "BuyFromMerchant sets it");
    }

    std::printf("\nm59_merchant_smoke: %s (%d checks)\n",
                g_failures ? "FAILED" : "PASSED (all checks)", g_checks);
    return g_failures ? 1 : 0;
}
