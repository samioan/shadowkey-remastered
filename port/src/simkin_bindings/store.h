#pragma once

// M59: the store -- `monster+0x330`, the shop every merchant carries
// around inside itself.
//
// There is no separate "shop" object in this game. A merchant is an
// ordinary creature whose script happens to call `AddProduct`, and the
// store is 0x40-odd bytes embedded in it. `GetPlayer().SetMerchant(self)`
// is literally `player+0xf84 = monster + 0x330`, so "the store the player
// is currently in" is one pointer, and every buy/sell native is a no-op
// while it is null.
//
// ## How the three merchant natives get dispatched
//
// `AddProduct`, `ClearProducts` and `ReducePrices` are **not in the
// 703-entry native trie**, which is why `SIMKIN_NATIVE_API.md`'s
// enumeration never listed them and why this port's coverage tool
// reported them as script-level names. `FUN_1008607c` is a dispatcher
// layer sitting *above* the creature's trie dispatcher that tests those
// three names with a plain `wcscmp` chain and tail-calls `FUN_10084924`
// for everything else:
//
//     if (name == L"AddProduct")    Store::Add(self+0x330, args[0], qty);
//     else if (name == L"ReducePrices") Store::ReducePrices(self+0x330, args[0]);
//     else if (name == L"ClearProducts") Store::Clear(self+0x330);
//     else return Monster_Dispatch(...);
//
// So there is a whole extra class in the hierarchy -- a merchant creature
// -- whose entire API is these three names, added by hand rather than
// through the trie.
//
// ## What AddProduct actually reads
//
// The shipped call is five arguments:
//
//     AddProduct(662, "Shadow Band", 4444, 3, IPT_Armor);
//
// and the handler takes **the first and the fourth**. Name, price and
// category come from `products.dat` (see product_database.h), and the
// file disagrees with the scripts -- a Steel Pauldron is 1111 there and
// 111 in `blk_market.s`. The quantity argument is read from args[3] when
// there are four or more arguments and from args[1] otherwise, which is
// the only reason a two-argument `AddProduct(id, n)` would work.
//
// Note what a product node *is*: a pointer into the one shared catalogue
// the database was parsed into, not a copy. So `AddProduct` writing the
// quantity writes it on the **global** record -- two merchants stocking
// the same template id share one stock counter. Reproduced (see
// StockEntry).

#include <string>
#include <vector>

#include "assets/product_database.h"

namespace sk {
class StringTable;
}

namespace sk_bindings {

// The IPT_* categories, which are also the store's counter slots. The
// values are the recovered constants (effects.cpp); repeated here because
// the counter array is indexed by them.
inline constexpr int kStoreCategoryCount = 5;

class Store {
public:
    // One line of stock. `record` points into the shared ProductDatabase,
    // and `quantity` is the number in stock -- which in the real engine
    // lives *on that shared record*, so this port keeps a per-store copy
    // and a note rather than pretending otherwise. See quantityIsShared().
    struct StockEntry {
        const sk::ProductRecord* record = nullptr;
        int quantity = 0;
        int price = 0;  // the record's price, after any ReducePrices
    };

    void SetDatabase(const sk::ProductDatabase* db) { m_Database = db; }
    void SetStrings(const sk::StringTable* strings) { m_Strings = strings; }

    // FUN_10035a48. Looks the template id up in the catalogue; on a miss
    // the engine logs "ERROR: MERCHANT - missing product %d, not found in
    // product file." and adds nothing. On a hit it sets the quantity,
    // links the record in, and adds the quantity to the category counter.
    // Returns the entry, or null on a miss.
    const StockEntry* Add(int templateId, int quantity);

    // FUN_10035fa4: drop every line. The category counters are **not**
    // reset -- the real function frees the list nodes and zeroes the head,
    // count and tail, and never touches `+0x24..+0x2c`. That is a real
    // leak of the counters, and it matters: `GetProductCount` and
    // `GetDefaultStore` both read them, so a merchant that calls
    // `ClearProducts()` in its `Init()` (every one of them does) still
    // shows the tabs of whatever it stocked before. Reproduced.
    void Clear();

    // FUN_10035f40: `price = base - base * pct / 100` on every line, and
    // remember the percentage.
    void ReducePrices(int percent);
    int priceReduction() const { return m_PriceReduction; }

    // FUN_10035e34: the per-category counter, which is what
    // `GetProductCount(IPT_Weapon)` answers.
    int productCount(int category) const;

    // FUN_10035e50: the first category 0..4 with a non-zero counter, or 0.
    // `buysell.s` opens on this tab.
    int defaultCategory() const;

    // FUN_10035ecc: the first line whose quantity is non-zero and whose
    // *name* string matches. This is the lookup `BuyItem(itemText, n)`
    // does, with the text of the table row the player picked.
    const StockEntry* FindByName(const std::string& name) const;

    // FUN_10035da8: take `count` off a line, if it has that many. Returns
    // false when it does not -- and note the real comparison is
    // `(u16)quantity - count >= 0` on a *signed* result of an unsigned
    // subtract, so it behaves as the plain `quantity >= count` it reads
    // as for every quantity the game can reach.
    bool TakeStock(const sk::ProductRecord* record, int count);

    // FUN_10035c28: "will this merchant buy this item?" -- and it is also
    // the code that *adds* what it buys to the shelves. Misc (category 0)
    // is refused outright; a Spell is refused unless the merchant already
    // deals in spells (its category-2 counter is non-zero); everything
    // else is taken. An item the merchant already stocks just bumps that
    // line's quantity.
    bool AcceptItem(int templateId, int category, int count);

    const std::vector<StockEntry>& stock() const { return m_Stock; }
    bool empty() const { return m_Stock.empty(); }

private:
    StockEntry* FindEntry(int templateId);

    const sk::ProductDatabase* m_Database = nullptr;
    const sk::StringTable* m_Strings = nullptr;
    std::vector<StockEntry> m_Stock;
    int m_CategoryCount[kStoreCategoryCount] = {0};  // +0x24..+0x2c
    int m_PriceReduction = 0;                        // +0x30
};

}  // namespace sk_bindings
