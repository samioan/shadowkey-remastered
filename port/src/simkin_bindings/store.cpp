#include "simkin_bindings/store.h"

#include <cstdio>

#include "assets/string_table.h"

namespace sk_bindings {

Store::StockEntry* Store::FindEntry(int templateId) {
    for (StockEntry& e : m_Stock) {
        if (e.record && e.record->templateId == templateId) return &e;
    }
    return nullptr;
}

const Store::StockEntry* Store::Add(int templateId, int quantity) {
    if (!m_Database) return nullptr;
    const sk::ProductRecord* record = m_Database->Find(templateId);
    if (!record) {
        std::printf("Store: ERROR: MERCHANT - missing product %d, not found in product file.\n",
                    templateId);
        return nullptr;
    }
    // The real function does not check for a duplicate line -- it appends
    // unconditionally and *assigns* (not adds) the quantity onto the shared
    // record, while adding it to the category counter. So a script that
    // lists the same id twice gets two lines, one counter bump per line,
    // and one shared quantity. Nothing shipped does that.
    StockEntry entry;
    entry.record = record;
    entry.quantity = quantity;
    entry.price = record->price;
    if (m_PriceReduction != 0) {
        entry.price = record->basePrice - record->basePrice * m_PriceReduction / 100;
    }
    m_Stock.push_back(entry);
    if (record->category >= 0 && record->category < kStoreCategoryCount) {
        m_CategoryCount[record->category] += quantity;
    }
    return &m_Stock.back();
}

void Store::ZeroLinePrice(const StockEntry* entry) {
    for (StockEntry& e : m_Stock) {
        if (&e == entry) {
            e.price = 0;
            return;
        }
    }
}

void Store::Clear() {
    // Deliberately only the list -- see the header. The counters and the
    // price reduction survive, as they do in FUN_10035fa4.
    m_Stock.clear();
}

void Store::ReducePrices(int percent) {
    m_PriceReduction = percent;
    for (StockEntry& e : m_Stock) {
        if (!e.record) continue;
        e.price = e.record->basePrice - e.record->basePrice * percent / 100;
    }
}

int Store::productCount(int category) const {
    if (category < 0 || category >= kStoreCategoryCount) return 0;
    return m_CategoryCount[category];
}

int Store::defaultCategory() const {
    for (int i = 0; i < kStoreCategoryCount; ++i) {
        if (m_CategoryCount[i] != 0) return i;
    }
    return 0;
}

const Store::StockEntry* Store::FindByName(const std::string& name) const {
    if (!m_Strings) return nullptr;
    for (const StockEntry& e : m_Stock) {
        if (!e.record || e.quantity == 0) continue;
        if (m_Strings->Get(e.record->nameStringId) == name) return &e;
    }
    return nullptr;
}

bool Store::TakeStock(const sk::ProductRecord* record, int count) {
    if (!record) return false;
    for (StockEntry& e : m_Stock) {
        if (e.record != record || e.quantity < count) continue;
        e.quantity -= count;
        if (record->category >= 0 && record->category < kStoreCategoryCount) {
            m_CategoryCount[record->category] -= count;
        }
        return true;
    }
    return false;
}

bool Store::AcceptItem(int templateId, int category, int count) {
    if (StockEntry* existing = FindEntry(templateId)) {
        existing->quantity += count;
        if (existing->record && existing->record->category >= 0 &&
            existing->record->category < kStoreCategoryCount) {
            m_CategoryCount[existing->record->category] += count;
        }
        return true;
    }
    // The real test is a four-way `type == 1 || type == 3 || type == 4 ||
    // type == 2` -- every category except Misc. Written out that way rather
    // than as `category != 0` because the engine calls its type accessor
    // four separate times to say it, and because the order (Weapon, Armor,
    // Consumable, Spell) is not the enum's.
    const bool sellable = category == 1 || category == 3 || category == 4 || category == 2;
    if (!sellable) return false;
    // A spell is only taken by a merchant who already deals in spells --
    // the guard reads the store's own category-2 counter. Note this is
    // checked *after* the accept decision, so a spell offered to a
    // weapon-only merchant still returns "sold" while adding nothing:
    // the player loses the item and gets the gold, and the merchant's
    // shelves stay empty. Reproduced.
    if (category != 2 || m_CategoryCount[2] != 0) {
        Add(templateId, count);
    }
    return true;
}

}  // namespace sk_bindings
