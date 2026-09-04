#include "assets/save_records.h"

#include <ctime>

namespace sk {

const char* const kCharacterMemberName = "character.dat";

const char* const SavedPlayer::kGlobalFlagNames[SavedPlayer::kTrailingInts] = {
    "saved_Guild",      "saved_NA_Crystal", "saved_Business",   "saved_Birgidda",
    "saved_Skelos",     "saved_Rescue1",    "saved_Rescue2",    "saved_Rescue3",
    "saved_Rescue4",    "saved_Heather",    "saved_Dstar_Pass", "saved_GoAway",
    "saved_Goblin",     "saved_StarTooth",  "saved_Makor",      "saved_SkelosDead",
    "saved_KethStatus", "saved_EndGame",
};

std::string LevelMemberName(const std::string& levelName) { return levelName + ".dat"; }

namespace {
// FUN_10066614 and FUN_10066cc4 each call time(0) exactly once and use it
// for both deadlines in the record. Overridable so a round-trip test does
// not straddle a second boundary.
int32_t g_ClockOverride = -1;
}  // namespace

int32_t SaveClockNow() {
    if (g_ClockOverride >= 0) return g_ClockOverride;
    return static_cast<int32_t>(std::time(nullptr));
}

void SetSaveClockOverride(int32_t seconds) { g_ClockOverride = seconds; }

// ---------------------------------------------------------------------
// Stats -- FUN_1004a284 / FUN_1004a6f4
// ---------------------------------------------------------------------
void SavedStats::Write(SaveStream& s) const {
    s.WriteI16(attack);
    s.WriteI16(defense);
    s.WriteI16(spellcast);
    s.WriteI16(magicResistance);
    s.WriteI16(damageMin);
    s.WriteI16(damageMax);
    s.WriteI16(armorValue);
    s.WriteI16(expWorth);
    s.WriteI16(strength);
    s.WriteI16(intelligence);
    s.WriteI16(agility);
    s.WriteI16(willpower);
    s.WriteI16(speed);
    s.WriteI16(endurance);
    s.WriteI16(personality);
    s.WriteI16(luck);
    s.WriteI16(maxHealth);
    s.WriteI16(maxFatigue);
    s.WriteI16(maxMagicka);
    s.WriteI16(health);
    s.WriteI16(fatigue);
    s.WriteI16(magicka);
    s.WriteI32(experience);
    s.WriteI16(level);
    s.WriteI32(gold);
    // Out of field order on purpose -- the writer doubles back here.
    s.WriteI16(strengthBonus);
    s.WriteI16(healthBonus);
    s.WriteU32(effectFlags);
    s.WriteI16(effect70);
    s.WriteI16(effect78);
    s.WriteI16(effectTimer);
    s.WriteI16(dotKind);
}

void SavedStats::Read(SaveStream& s) {
    attack = s.ReadI16();
    defense = s.ReadI16();
    spellcast = s.ReadI16();
    magicResistance = s.ReadI16();
    damageMin = s.ReadI16();
    damageMax = s.ReadI16();
    armorValue = s.ReadI16();
    expWorth = s.ReadI16();
    strength = s.ReadI16();
    intelligence = s.ReadI16();
    agility = s.ReadI16();
    willpower = s.ReadI16();
    speed = s.ReadI16();
    endurance = s.ReadI16();
    personality = s.ReadI16();
    luck = s.ReadI16();
    maxHealth = s.ReadI16();
    maxFatigue = s.ReadI16();
    maxMagicka = s.ReadI16();
    health = s.ReadI16();
    fatigue = s.ReadI16();
    magicka = s.ReadI16();
    experience = s.ReadI32();
    level = s.ReadI16();
    gold = s.ReadI32();
    strengthBonus = s.ReadI16();
    healthBonus = s.ReadI16();
    effectFlags = s.ReadU32();
    effect70 = s.ReadI16();
    effect78 = s.ReadI16();
    effectTimer = s.ReadI16();
    dotKind = s.ReadI16();
}

// ---------------------------------------------------------------------
// Entity root -- FUN_10066614 / FUN_10066cc4
// ---------------------------------------------------------------------
void SavedEntityBase::Write(SaveStream& s) const {
    s.WriteU16(entityId);
    s.WriteI16(f86);
    s.WriteI32(f8c);
    s.WriteI16(f8e);
    s.WriteI16(f90);
    s.WriteString8(artName);
    s.WriteU16(fe0);
    s.WriteU8(f93);
    s.WriteI32(x);
    s.WriteI32(y);
    s.WriteI16(z);
    s.WriteI16(fb2);
    s.WriteI16(pitch);
    s.WriteI16(yaw);
    s.WriteString8(scriptName);
    s.WriteU8(fca);
    s.WriteU8(f92);
    s.WriteU8(fd5);
    s.WriteU8(fd8);
    s.WriteU8(fd9);
    s.WriteU32(fdc);
    s.WriteI16(spriteId);
    s.WriteU8(f6c);
    s.WriteU8(f6d);
    s.WriteU32(f70);
    s.WriteU32(f74);
    s.WriteU32(f78);
    s.WriteU32(f7c);
    s.WriteU16(f80);
    const int32_t now = SaveClockNow();
    s.WriteU8(f10a);
    s.WriteI32(deadline10c - now);
    s.WriteI16(f110);
    s.WriteU8(f114);
    s.WriteI32(deadline118 - now);
    s.WriteU8(f115);
    s.WriteI16(f60);
    s.WriteU8(f11c);
    for (const SavedScriptVar& var : scriptVars) {
        s.WriteU8(kVarPresent);
        s.WriteStringW(var.name);
        s.WriteStringW(var.value);
    }
    s.WriteU8(kVarListEnd);
}

void SavedEntityBase::Read(SaveStream& s) {
    entityId = s.ReadU16();
    f86 = s.ReadI16();
    f8c = s.ReadI32();
    f8e = s.ReadI16();
    f90 = s.ReadI16();
    artName = s.ReadString8();
    fe0 = s.ReadU16();
    f93 = s.ReadU8();
    x = s.ReadI32();
    y = s.ReadI32();
    z = s.ReadI16();
    fb2 = s.ReadI16();
    pitch = s.ReadI16();
    yaw = s.ReadI16();
    scriptName = s.ReadString8();
    fca = s.ReadU8();
    f92 = s.ReadU8();
    fd5 = s.ReadU8();
    fd8 = s.ReadU8();
    fd9 = s.ReadU8();
    fdc = s.ReadU32();
    spriteId = s.ReadI16();
    f6c = s.ReadU8();
    f6d = s.ReadU8();
    f70 = s.ReadU32();
    f74 = s.ReadU32();
    f78 = s.ReadU32();
    f7c = s.ReadU32();
    f80 = s.ReadU16();
    const int32_t now = SaveClockNow();
    f10a = s.ReadU8();
    deadline10c = now + s.ReadI32();
    f110 = s.ReadI16();
    f114 = s.ReadU8();
    deadline118 = now + s.ReadI32();
    f115 = s.ReadU8();
    f60 = s.ReadI16();
    f11c = s.ReadU8();
    scriptVars.clear();
    // FUN_10066cc4 reads the tag once up front, then name/value/tag in a
    // do-while -- so a stream whose first tag is already 0xff stores no
    // variables, and any tag that is neither 1 nor 0xff is logged and
    // then treated as "present" anyway.
    uint8_t tag = s.ReadU8();
    while (tag != kVarListEnd && !s.failed()) {
        SavedScriptVar var;
        var.name = s.ReadStringWAscii();
        var.value = s.ReadStringWAscii();
        scriptVars.push_back(std::move(var));
        tag = s.ReadU8();
    }
}

// ---------------------------------------------------------------------
// Drawable -- FUN_10067db0 / FUN_10067ca0
// ---------------------------------------------------------------------
void SavedDrawable::Write(SaveStream& s) const {
    s.WriteI32(f12c);
    s.WriteI32(f130);
    s.WriteI16(f5e);
    s.WriteU8(f6c);
    s.WriteU32(f74);
    s.WriteU32(f78);
    s.WriteU16(f80);
    s.WriteU32(f7c);
    s.WriteU32(f70);
    s.WriteU8(f6d);
}

void SavedDrawable::Read(SaveStream& s) {
    f12c = s.ReadI32();
    f130 = s.ReadI32();
    f5e = s.ReadI16();
    f6c = s.ReadU8();
    f74 = s.ReadU32();
    f78 = s.ReadU32();
    f80 = s.ReadU16();
    f7c = s.ReadU32();
    f70 = s.ReadU32();
    f6d = s.ReadU8();
}

// ---------------------------------------------------------------------
// Spellbook -- FUN_10028a60 / FUN_10028be8
// ---------------------------------------------------------------------
void SavedSpellbook::Write(SaveStream& s) const {
    s.WriteU8(f180);
    s.WriteU8(f160);
    s.WriteU8(f181);
    s.WriteI32(static_cast<int32_t>(spells.size()));
    for (const SavedSpellEntry& e : spells) {
        s.WriteI16(e.typeId);
        s.WriteI32(e.charges);
        s.WriteU8(e.hasEnchantment ? 1 : 0);
        if (e.hasEnchantment) s.WriteU16(e.enchantment);
    }
}

void SavedSpellbook::Read(SaveStream& s) {
    f180 = s.ReadU8();
    f160 = s.ReadU8();
    f181 = s.ReadU8();
    const int32_t count = s.ReadI32();
    spells.clear();
    for (int32_t i = 0; i < count && !s.failed(); ++i) {
        SavedSpellEntry e;
        e.typeId = s.ReadI16();
        e.charges = s.ReadI32();
        e.hasEnchantment = s.ReadU8() != 0;
        if (e.hasEnchantment) e.enchantment = s.ReadU16();
        spells.push_back(e);
    }
}

// ---------------------------------------------------------------------
// Item and its three subclasses
// ---------------------------------------------------------------------
void SavedItem::Write(SaveStream& s) const {
    s.WriteU8(f180);
    s.WriteI32(f19c);
    s.WriteI32(f1a0);
    s.WriteU8(usesRangedPath);
}

void SavedItem::Read(SaveStream& s) {
    f180 = s.ReadU8();
    f19c = s.ReadI32();
    f1a0 = s.ReadI32();
    usesRangedPath = s.ReadU8();
}

void SavedStackable::Write(SaveStream& s) const { s.WriteI32(quantity); }
void SavedStackable::Read(SaveStream& s) { quantity = s.ReadI32(); }

void SavedWearable::Write(SaveStream& s) const { s.WriteU8(f1d4); }
void SavedWearable::Read(SaveStream& s) { f1d4 = s.ReadU8(); }

void SavedWeapon::Write(SaveStream& s) const {
    s.WriteI16(damageMin);
    s.WriteI16(damageMax);
    s.WriteI32(f1d0);
    s.WriteI32(quantity);
}

void SavedWeapon::Read(SaveStream& s) {
    damageMin = s.ReadI16();
    damageMax = s.ReadI16();
    f1d0 = s.ReadI32();
    quantity = s.ReadI32();
}

// ---------------------------------------------------------------------
// InventoryHolder -- FUN_10005164 / FUN_10005320. Its own scalar fields
// only; the child list is walked by SavedEntity::Write, which is where
// each child's typeId (and therefore its class) is known.
// ---------------------------------------------------------------------
void SavedInventoryHolder::Write(SaveStream& s) const {
    s.WriteU8(f1e8);
    s.WriteU8(f1b9);
    s.WriteI32(f1bc);
    s.WriteI32(f1c0);
    s.WriteI32(f1c4);
    s.WriteU8(f1e1);
    s.WriteU8(f1e2);
    s.WriteU8(f1e4);
}

void SavedInventoryHolder::Read(SaveStream& s) {
    f1e8 = s.ReadU8();
    f1b9 = s.ReadU8();
    f1bc = s.ReadI32();
    f1c0 = s.ReadI32();
    f1c4 = s.ReadI32();
    f1e1 = s.ReadU8();
    f1e2 = s.ReadU8();
    f1e4 = s.ReadU8();
}

// ---------------------------------------------------------------------
// Actor / LinkedActor / Character
// ---------------------------------------------------------------------
void SavedActor::Write(SaveStream& s) const {
    s.WriteU8(f2a8);
    s.WriteU8(f2ac);
    s.WriteU8(f1e2);
    s.WriteU8(fd8);
    s.WriteI16(f2ec);
}

void SavedActor::Read(SaveStream& s) {
    f2a8 = s.ReadU8();
    f2ac = s.ReadU8();
    f1e2 = s.ReadU8();
    fd8 = s.ReadU8();
    f2ec = s.ReadI16();
}

void SavedLinkedActor::Write(SaveStream& s) const {
    s.WriteI32(link224);
    s.WriteI32(link228);
}

void SavedLinkedActor::Read(SaveStream& s) {
    link224 = s.ReadI32();
    link228 = s.ReadI32();
}

void SavedCharacter::Write(SaveStream& s) const {
    s.WriteString8(levelName);
    s.WriteU8(f398);
    s.WriteI32(f22c);
    s.WriteU8(f358);
    s.WriteU8(f359);
    s.WriteU8(f35a);
    s.WriteI32(linkIndex);
    s.WriteI32(linkTypeId);
    s.WriteI32(f36c);
    s.WriteI32(f370);
    s.WriteI32(f374);
    s.WriteI32(f378);
    s.WriteI32(f37c);
    s.WriteI32(f380);
    s.WriteI32(f384);
}

void SavedCharacter::Read(SaveStream& s) {
    levelName = s.ReadString8();
    f398 = s.ReadU8();
    f22c = s.ReadI32();
    f358 = s.ReadU8();
    f359 = s.ReadU8();
    f35a = s.ReadU8();
    linkIndex = s.ReadI32();
    linkTypeId = s.ReadI32();
    f36c = s.ReadI32();
    f370 = s.ReadI32();
    f374 = s.ReadI32();
    f378 = s.ReadI32();
    f37c = s.ReadI32();
    f380 = s.ReadI32();
    f384 = s.ReadI32();
}

// ---------------------------------------------------------------------
// Player -- FUN_10043308 / FUN_10043bb0, split around the base chain
// ---------------------------------------------------------------------
void SavedPlayer::WriteHead(SaveStream& s) const {
    s.WriteStringW(characterName);
    for (int i = 0; i < kQuestSlots; ++i) {
        s.WriteU8(questAssigned[static_cast<size_t>(i)]);
        s.WriteU8(questCompleted[static_cast<size_t>(i)]);
        s.WriteU8(questSolved[static_cast<size_t>(i)]);
    }
    for (int i = 0; i < kMonsterSlots; ++i) s.WriteU8(monstersKilled[static_cast<size_t>(i)]);
    s.WriteI32(levelUpPoints);
}

void SavedPlayer::ReadHead(SaveStream& s) {
    characterName = s.ReadStringWAscii();
    for (int i = 0; i < kQuestSlots; ++i) {
        questAssigned[static_cast<size_t>(i)] = s.ReadU8();
        questCompleted[static_cast<size_t>(i)] = s.ReadU8();
        questSolved[static_cast<size_t>(i)] = s.ReadU8();
    }
    for (int i = 0; i < kMonsterSlots; ++i) monstersKilled[static_cast<size_t>(i)] = s.ReadU8();
    levelUpPoints = s.ReadI32();
}

void SavedPlayer::WriteTail(SaveStream& s) const {
    for (int list = 0; list < kQuickLists; ++list) {
        for (int slot = 0; slot < kQuickSlots; ++slot) {
            s.WriteI16(quickSlots[static_cast<size_t>(list)][static_cast<size_t>(slot)]);
        }
    }
    s.WriteI16(selectedQuick0);
    s.WriteI16(selectedQuick1);
    s.WriteU8(hasCreatedCharacter);
    s.WriteU8(characterClass);
    s.WriteU8(race);
    s.WriteU16(portraitId);
    s.WriteU8(sex);
    s.WriteI32(specialAbility);
    s.WriteI32(raceAbility);
    s.WriteU16(manaReduction);
    s.WriteI16(ffb8);
    s.WriteU16(ffba);
    for (int i = 0; i < kTrailingInts; ++i) s.WriteI32(trailingInts[static_cast<size_t>(i)]);
    for (int i = 0; i < kEquipSlots; ++i) s.WriteU16(equipTypeIds[static_cast<size_t>(i)]);
}

void SavedPlayer::ReadTail(SaveStream& s) {
    for (int list = 0; list < kQuickLists; ++list) {
        for (int slot = 0; slot < kQuickSlots; ++slot) {
            quickSlots[static_cast<size_t>(list)][static_cast<size_t>(slot)] = s.ReadI16();
        }
    }
    selectedQuick0 = s.ReadI16();
    selectedQuick1 = s.ReadI16();
    hasCreatedCharacter = s.ReadU8();
    characterClass = s.ReadU8();
    race = s.ReadU8();
    portraitId = s.ReadU16();
    sex = s.ReadU8();
    specialAbility = s.ReadI32();
    raceAbility = s.ReadI32();
    manaReduction = s.ReadU16();
    ffb8 = s.ReadI16();
    ffba = s.ReadU16();
    for (int i = 0; i < kTrailingInts; ++i) trailingInts[static_cast<size_t>(i)] = s.ReadI32();
    for (int i = 0; i < kEquipSlots; ++i) equipTypeIds[static_cast<size_t>(i)] = s.ReadU16();
}

// ---------------------------------------------------------------------
// The chain
// ---------------------------------------------------------------------
bool SavedEntity::HasInventory(SavedEntityKind kind) {
    switch (kind) {
        case SavedEntityKind::InventoryHolder:
        case SavedEntityKind::Actor:
        case SavedEntityKind::LinkedActor:
        case SavedEntityKind::Character:
        case SavedEntityKind::Player:
            return true;
        default:
            return false;
    }
}

bool SavedEntity::HasSpellbook(SavedEntityKind kind) {
    return kind == SavedEntityKind::Spellbook || HasInventory(kind);
}

void SavedEntity::Write(SaveStream& s) const {
    // Derived first, then the base -- each save function writes its own
    // fields and *then* calls its parent's, so the wire order is
    // most-derived to least.
    if (kind == SavedEntityKind::Player) player.WriteHead(s);
    if (kind == SavedEntityKind::Player) stats.Write(s);
    if (kind == SavedEntityKind::Actor) {
        actor.Write(s);
        stats.Write(s);
    }
    if (kind == SavedEntityKind::LinkedActor) linkedActor.Write(s);
    if (kind == SavedEntityKind::Character || kind == SavedEntityKind::Player) character.Write(s);

    if (kind == SavedEntityKind::Weapon) weapon.Write(s);
    if (kind == SavedEntityKind::Wearable) wearable.Write(s);
    if (kind == SavedEntityKind::Wearable || kind == SavedEntityKind::Stackable) {
        stackable.Write(s);
    }
    if (kind == SavedEntityKind::Item || kind == SavedEntityKind::Stackable ||
        kind == SavedEntityKind::Wearable || kind == SavedEntityKind::Weapon) {
        item.Write(s);
    }

    if (HasInventory(kind)) {
        holder.Write(s);
        s.WriteI32(static_cast<int32_t>(inventory.size()));
        for (const SavedEntity& child : inventory) {
            s.WriteI32(child.typeId);
            child.Write(s);
        }
    }
    if (HasSpellbook(kind)) spellbook.Write(s);
    if (kind != SavedEntityKind::Entity) drawable.Write(s);
    entity.Write(s);
    if (kind == SavedEntityKind::Player) player.WriteTail(s);
}

void SavedEntity::Read(SaveStream& s, const SavedKindResolver& resolve) {
    if (kind == SavedEntityKind::Player) player.ReadHead(s);
    if (kind == SavedEntityKind::Player) stats.Read(s);
    if (kind == SavedEntityKind::Actor) {
        actor.Read(s);
        stats.Read(s);
    }
    if (kind == SavedEntityKind::LinkedActor) linkedActor.Read(s);
    if (kind == SavedEntityKind::Character || kind == SavedEntityKind::Player) character.Read(s);

    if (kind == SavedEntityKind::Weapon) weapon.Read(s);
    if (kind == SavedEntityKind::Wearable) wearable.Read(s);
    if (kind == SavedEntityKind::Wearable || kind == SavedEntityKind::Stackable) {
        stackable.Read(s);
    }
    if (kind == SavedEntityKind::Item || kind == SavedEntityKind::Stackable ||
        kind == SavedEntityKind::Wearable || kind == SavedEntityKind::Weapon) {
        item.Read(s);
    }

    if (HasInventory(kind)) {
        holder.Read(s);
        const int32_t count = s.ReadI32();
        inventory.clear();
        for (int32_t i = 0; i < count && !s.failed(); ++i) {
            SavedEntity child;
            child.typeId = s.ReadI32();
            // The real loader resolves the class from the typeId through
            // entities.txt. Without that table the commonest inventory
            // case, a plain Item, is the fallback.
            child.kind = resolve ? resolve(child.typeId) : SavedEntityKind::Item;
            child.Read(s, resolve);
            inventory.push_back(std::move(child));
        }
    }
    if (HasSpellbook(kind)) spellbook.Read(s);
    if (kind != SavedEntityKind::Entity) drawable.Read(s);
    entity.Read(s);
    if (kind == SavedEntityKind::Player) player.ReadTail(s);
}

// ---------------------------------------------------------------------
// <level>.dat -- FUN_100187d0 / FUN_100188dc
// ---------------------------------------------------------------------
void SavedLevelState::Write(SaveStream& s) const {
    for (const SavedEntity& e : entities) {
        s.WriteU8(kEntryPresent);
        s.WriteI16(static_cast<int16_t>(e.typeId));
        e.Write(s);
    }
    s.WriteU8(kEntryListEnd);
    s.WriteU8(renderEnabled);
}

void SavedLevelState::Read(SaveStream& s, const SavedKindResolver& resolve) {
    entities.clear();
    uint8_t tag = s.ReadU8();
    while (tag != kEntryListEnd && !s.failed()) {
        SavedEntity e;
        e.typeId = s.ReadI16();
        if (resolve) e.kind = resolve(e.typeId);
        e.Read(s, resolve);
        entities.push_back(std::move(e));
        tag = s.ReadU8();
    }
    renderEnabled = s.ReadU8();
}

}  // namespace sk
