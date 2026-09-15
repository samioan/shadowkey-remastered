#pragma once

// M59: `products.dat` -- the merchant product database, and the last
// shipped data file this port had never opened.
//
// Every merchant in the game stocks its shelves from a script:
//
//     ClearProducts();
//     AddProduct(662, "Shadow Band", 4444, 3, IPT_Armor);
//     AddProduct(678, "Steel Pauldron", 111, 10, IPT_Armor);
//
// and the engine's handler **reads only the first argument and the
// quantity**. The display name, the price and the category are looked up
// in `products.dat` instead, and the file disagrees with the script: a
// Steel Pauldron costs 1111, not the 111 written there, and a Vicar Herb
// 45, not 30. So the numbers in the shop scripts are stale documentation
// and this file is the truth.
//
// ## The format
//
// Recovered from `FUN_10035634`, which is a plain `fopen`/`fread` walk
// over `e:\system\apps\6R51\products.dat`. It reads a version first and
// refuses anything below 36 with the message *"This product database file
// is obsolete- version #%d"* -- 36 is `0x24`, the size of one record, so
// the version field is really the record stride.
//
//     u16 version                 (36 in the shipped file)
//     u16 count                   (279)
//     count x {
//         u16 templateId          -> +0x00
//         u16 category            -> +0x14, the IPT_* enum
//         u32 price               -> +0x04, and copied to +0x08 as the base
//         u16 rating              -> +0x0c
//         u16 descriptionStringId -> +0x0e
//         u8  armorSlot           -> +0x12
//         u16 nameStringId        -> +0x10
//         u16 classFlagCount      (always 9)
//         u8  classFlags[count]   -> +0x18..
//     }
//
// Note the read order is not the struct order -- the name string id is
// read *after* the one-byte armour slot, which is why a naive
// "fields in offset order" guess does not round-trip. This one does:
// 279 records consume the file's 7258 bytes exactly.
//
// Every field is named from a real reader:
//
//   * **templateId** is what `AddProduct`'s first argument is matched
//     against, and what `BuyItem` hands to the entity factory.
//   * **category** indexes the store's per-category counter, and those
//     counters are what `GetProductCount(IPT_Weapon)` answers -- so this
//     is the `IPT_*` enum (`effects.cpp` has the recovered values).
//   * **price** is what `BuyItem` multiplies by the quantity and charges;
//     **base price** is what `ReducePrices(pct)` recomputes it from.
//   * **rating** is passed straight into the created item's `SetRating`,
//     and `buysell.s`'s product table has two columns commented
//     "rating left" / "rating right".
//   * **nameStringId** is the *lookup key*: the store's find-by-name
//     compares this string against the text of the table row the player
//     selected, which is how `BuyItem(itemText, n)` works at all. The
//     shipped names line up with the shop scripts' literals exactly
//     (662 -> "Shadow Band", 4016 -> "Daedric Weapon", 806 -> "Skeleton
//     Key").
//   * **descriptionStringId** is what `GetProduct` and
//     `GetItemDescription` return -- the "Long blade, damage 4 - 12" line.
//   * **armorSlot** is 0 for everything that is not armour, and for
//     armour it partitions the 99 armour rows cleanly by what the
//     description says: 1 body, 2 legs, 3 feet, 4 head, 5 hands,
//     6 shield, 7 worn jewellery, 0 arms.
//   * **classFlags** is nine booleans, one per character class -- the
//     same 0..8 range `ChooseCharacter` clamps into. `buysell.s` asks
//     `IsItemEnabledFor()` before a purchase and offers a "Buy Anyway"
//     if it comes back false.

#include <cstdint>
#include <string>
#include <vector>

namespace sk {

inline constexpr int kProductRecordVersion = 36;
inline constexpr int kProductClassCount = 9;

// The engine's own armour-slot numbering (`+0x12`). Named from the
// shipped descriptions, which partition the 99 armour rows perfectly.
enum ProductArmorSlot {
    kArmorSlotArms = 0,  // also every non-armour row
    kArmorSlotBody = 1,
    kArmorSlotLegs = 2,
    kArmorSlotFeet = 3,
    kArmorSlotHead = 4,
    kArmorSlotHands = 5,
    kArmorSlotShield = 6,
    kArmorSlotWorn = 7,
};

struct ProductRecord {
    int templateId = 0;
    int category = 0;  // IPT_*
    int price = 0;
    int basePrice = 0;
    int rating = 0;
    int descriptionStringId = 0;
    int nameStringId = 0;
    int armorSlot = 0;
    uint8_t classFlags[kProductClassCount] = {0};

    bool enabledForClass(int characterClass) const {
        if (characterClass < 0 || characterClass >= kProductClassCount) return false;
        return classFlags[characterClass] != 0;
    }
};

class ProductDatabase {
public:
    // `<scriptRoot>/products.dat`. Returns false and leaves the database
    // empty when the file is missing or its version is below 36 -- the
    // same two failures the engine logs and carries on from.
    bool Load(const std::string& scriptRoot);

    bool loaded() const { return m_Loaded; }
    int version() const { return m_Version; }
    size_t size() const { return m_Records.size(); }
    const std::vector<ProductRecord>& records() const { return m_Records; }

    // The catalogue lookup `AddProduct` does: first record with this
    // template id, or null. The engine logs *"ERROR: MERCHANT - missing
    // product %d, not found in product file."* on a miss and adds nothing.
    const ProductRecord* Find(int templateId) const;

    // M95: `VisitStore`'s `entry+4 = 0`. A store line *is* its catalogue
    // record in the engine, so the God Vendor's free prices land here, on
    // the first record with this id, and every merchant stocked afterwards
    // inherits them. `basePrice` is untouched, which is what a later
    // `ReducePrices` recomputes from -- the engine's `+0x08` survives too.
    void ZeroPrice(int templateId);

private:
    std::vector<ProductRecord> m_Records;
    int m_Version = 0;
    bool m_Loaded = false;
};

}  // namespace sk
