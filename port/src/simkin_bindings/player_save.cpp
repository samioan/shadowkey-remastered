// M50: the player half of a real save file.
//
// PlayerExecutable <-> sk::SavedEntity{kind = Player}, the record
// `FUN_10043308` writes into `character.dat` and `FUN_10043bb0` reads
// back. Field-by-field provenance lives in assets/save_records.h; this
// file only says which of those fields this port has something to put in.
//
// The honest summary of the mapping: the stats block, the quest arrays,
// the kill counters, the level-up points, the character-creation block
// (name, class, race, portrait, sex, the two ability ids and the mana
// reduction), the two hand queues, the equipment slots and the inventory
// all have real counterparts here. The rest of the record -- the entity
// root's forty-odd scalars, the drawable layer, the spellbook, and the
// eighteen unidentified trailing ints -- does not, and is deliberately
// left alone rather than filled with plausible-looking values.

#include <cstdio>

#include "simkin_bindings/entity_base_ref.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/player_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "skString.h"

namespace sk_bindings {

namespace {

// The three quest arrays and the kill counter are fixed 256-entry byte
// arrays in the real record, so an id outside [0, 256) has nowhere to go.
// Every shipped script's quest id is well inside that, but a port that
// silently dropped one would be worse than one that says so.
template <typename Container>
void FillFlagArray(const Container& ids, std::array<uint8_t, 256>& out, const char* what) {
    out.fill(0);
    for (int id : ids) {
        if (id < 0 || id >= 256) {
            std::printf("PlayerExecutable: %s id %d is outside the save format's 256 slots\n", what,
                        id);
            continue;
        }
        out[static_cast<size_t>(id)] = 1;
    }
}

}  // namespace

sk::SavedEntity PlayerExecutable::BuildSaveRecord(const std::string& levelName) const {
    sk::SavedEntity rec;
    rec.kind = sk::SavedEntityKind::Player;

    // --- the head: name, quest arrays, kill counters, level-up points ---
    rec.player.characterName = m_Name;
    FillFlagArray(m_QuestAssigned, rec.player.questAssigned, "quest");
    FillFlagArray(m_QuestCompleted, rec.player.questCompleted, "quest");
    FillFlagArray(m_QuestSolved, rec.player.questSolved, "quest");
    rec.player.monstersKilled.fill(0);
    for (const auto& kv : m_MonstersKilled) {
        if (kv.first < 0 || kv.first >= 256) {
            std::printf("PlayerExecutable: monster id %d is outside the save format's 256 slots\n",
                        kv.first);
            continue;
        }
        // One byte per type, and AddMonsterKilled is a bare `+= 1` with no
        // clamp -- so the real counter wraps at 256. Reproduced.
        rec.player.monstersKilled[static_cast<size_t>(kv.first)] =
            static_cast<uint8_t>(kv.second & 0xff);
    }
    // M64: real now. GetLevelUpPoints / DecreaseLevelUpPoints (+0xf44).
    rec.player.levelUpPoints = m_LevelUpPoints;

    // --- the stats block (player+0x3ac) ---
    sk::SavedStats& st = rec.stats;
    st.attack = static_cast<int16_t>(m_BaseAttack);
    st.defense = static_cast<int16_t>(m_BaseDefense);
    st.spellcast = static_cast<int16_t>(m_Spellcast);
    st.magicResistance = static_cast<int16_t>(m_MagicResistance);
    st.strength = static_cast<int16_t>(m_Strength);
    st.strengthBonus = static_cast<int16_t>(m_StrengthBonus);
    st.intelligence = static_cast<int16_t>(m_Intelligence);
    st.agility = static_cast<int16_t>(m_Agility);
    st.willpower = static_cast<int16_t>(m_Will);
    st.speed = static_cast<int16_t>(m_Speed);
    st.endurance = static_cast<int16_t>(m_Endurance);
    st.personality = static_cast<int16_t>(m_Personality);
    st.luck = static_cast<int16_t>(m_Luck);
    st.maxHealth = static_cast<int16_t>(m_MaxHealth);
    st.maxFatigue = static_cast<int16_t>(m_MaxFatigue);
    st.maxMagicka = static_cast<int16_t>(m_MaxMagicka);
    st.health = static_cast<int16_t>(m_Health);
    st.fatigue = static_cast<int16_t>(m_Fatigue);
    st.magicka = static_cast<int16_t>(m_Magicka);
    st.experience = m_Experience;
    st.level = static_cast<int16_t>(m_Level);
    st.gold = m_Gold;

    // --- the Character layer: which level this save is in ---
    rec.character.levelName = levelName;

    // --- the tail: the character-creation block ---
    rec.player.hasCreatedCharacter = m_HasCreatedCharacter ? 1 : 0;
    rec.player.race = static_cast<uint8_t>(m_Race & 0xff);
    rec.player.portraitId = static_cast<uint16_t>(m_PortraitId);
    rec.player.sex = static_cast<uint8_t>(m_Sex & 0xff);
    // M64: the four progression words the tail already had slots for and
    // nothing to put in them. The class is one of them -- every one of the
    // other three is derived from it, so a save that dropped it would come
    // back a Battlemage (FUN_1003d670's default) with a Thief's numbers.
    rec.player.characterClass = static_cast<uint8_t>(m_CharacterClass & 0xff);
    rec.player.specialAbility = m_SpecialAbility;
    rec.player.raceAbility = m_RaceAbility;
    rec.player.ffb8 = static_cast<int16_t>(m_MagickaBonusRace);
    rec.player.ffba = static_cast<uint16_t>(m_MagickaBonusClass);

    // --- the two hand queues (+0xf4c / +0xf68) and their selections ---
    //
    // The real lists hold five item ids each and the stats block's +0x48 /
    // +0x4c point at the one visibly wielded in that hand -- saved as an
    // *index into the list*, which is why the selection is an i16 in
    // [0,5) and not an id. This port models one item per hand rather than
    // the full five-deep queue (see UpdateEquipStatus's own comment), so
    // it fills slot 0 of each list and selects it.
    for (auto& list : rec.player.quickSlots) list.fill(-1);
    rec.player.selectedQuick0 = -1;
    rec.player.selectedQuick1 = -1;
    if (m_LeftItem) {
        rec.player.quickSlots[0][0] = static_cast<int16_t>(m_LeftItem->templateId());
        rec.player.selectedQuick0 = 0;
    }
    if (m_RightItem) {
        rec.player.quickSlots[1][0] = static_cast<int16_t>(m_RightItem->templateId());
        rec.player.selectedQuick1 = 0;
    }

    // --- the eight equipment slots (+0xf8c), saved by typeId ---
    //
    // A caveat this port cannot write its way out of: the format names an
    // equipped item by its entities.txt typeId, and an ItemExecutable
    // loaded straight from a .s file has none (templateId() is -1 unless
    // it came through Level.CreateEntity -- see item_executable.h). So a
    // save this port writes records equipment and hand selection only for
    // items that were spawned from a real template, and a load restores
    // only those. Faithful to the field; a real gap in the round trip.
    rec.player.equipTypeIds.fill(0);
    {
        size_t slot = 0;
        for (const auto& item : m_Inventory) {
            if (!item || !item->equipped()) continue;
            if (slot >= sk::SavedPlayer::kEquipSlots) {
                std::printf("PlayerExecutable: more than %d equipped items; the save has no slot\n",
                            sk::SavedPlayer::kEquipSlots);
                break;
            }
            rec.player.equipTypeIds[slot++] = static_cast<uint16_t>(item->templateId());
        }
    }

    // --- the inventory, as InventoryHolder children ---
    //
    // Each child is `i32 typeId` then that entity's own record. Two
    // port-specific decisions here, both called out because neither is
    // what the engine does:
    //
    // **Every child is written as a Stackable**, not as the Weapon /
    // Wearable / Stackable its category would suggest. Nothing in the
    // format says which class a record belongs to -- the real loader
    // resolves it from the typeId through entities.txt, and this port's
    // script-loaded items have no typeId to resolve (see the equipment
    // note above). Writing one class throughout is the only rule a reader
    // with no template table can follow, and it loses nothing here: a
    // Weapon record's extra payload is its damage pair, which this port
    // re-derives by re-running the item's own script on load. The other
    // classes are still implemented and exercised (save_records.h, and
    // m50's resolver checks) -- they are just not what *this* writer
    // emits.
    //
    // **The child's script path goes in the entity root's `scriptName`.**
    // The real field (+0xcb) is the object's name in the SimKin registry
    // and the real loader rebuilds an item from its typeId; this port
    // builds items by running their .s file, so the path is what it needs
    // to get back.
    for (const auto& item : m_Inventory) {
        if (!item || item->markedForRemoval()) continue;
        sk::SavedEntity e;
        e.kind = kInventoryChildKind;
        e.typeId = item->templateId();
        e.stackable.quantity = item->quantity();
        e.item.usesRangedPath = item->usesRangedPath() ? 1 : 0;
        e.entity.objectId = item->scriptPath();
        e.entity.artName = item->name();
        rec.inventory.push_back(std::move(e));
    }

    return rec;
}

void PlayerExecutable::ApplySaveRecord(const sk::SavedEntity& record, MenuStack& stack) {
    if (record.kind != sk::SavedEntityKind::Player) {
        std::printf("PlayerExecutable: refusing to load a record that is not a Player record\n");
        return;
    }

    m_Name = record.player.characterName;
    m_QuestAssigned.clear();
    m_QuestCompleted.clear();
    m_QuestSolved.clear();
    m_MonstersKilled.clear();
    for (int id = 0; id < static_cast<int>(sk::SavedPlayer::kQuestSlots); ++id) {
        const size_t i = static_cast<size_t>(id);
        if (record.player.questAssigned[i]) m_QuestAssigned.insert(id);
        if (record.player.questCompleted[i]) m_QuestCompleted.insert(id);
        if (record.player.questSolved[i]) m_QuestSolved.insert(id);
        if (record.player.monstersKilled[i]) {
            m_MonstersKilled[id] = record.player.monstersKilled[i];
        }
    }

    const sk::SavedStats& st = record.stats;
    m_BaseAttack = st.attack;
    m_BaseDefense = st.defense;
    m_Spellcast = st.spellcast;
    m_MagicResistance = st.magicResistance;
    m_Strength = st.strength;
    m_StrengthBonus = st.strengthBonus;
    m_Intelligence = st.intelligence;
    m_Agility = st.agility;
    m_Will = st.willpower;
    m_Speed = st.speed;
    m_Endurance = st.endurance;
    m_Personality = st.personality;
    m_Luck = st.luck;
    m_MaxHealth = st.maxHealth;
    m_MaxFatigue = st.maxFatigue;
    m_MaxMagicka = st.maxMagicka;
    m_Health = st.health;
    m_Fatigue = st.fatigue;
    m_Magicka = st.magicka;
    m_Experience = st.experience;
    m_Level = st.level;
    m_Gold = st.gold;

    m_HasCreatedCharacter = record.player.hasCreatedCharacter != 0;
    m_Race = record.player.race;
    m_PortraitId = record.player.portraitId;
    m_Sex = record.player.sex;
    // M64. The attributes and the three maxima came off the stats block
    // above, so nothing here re-derives -- a load restores the character
    // as saved rather than recomputing it, which is what the real loader
    // does too (FUN_10043bb0 reads all of these and calls nothing).
    m_LevelUpPoints = record.player.levelUpPoints;
    m_CharacterClass = record.player.characterClass;
    m_SpecialAbility = record.player.specialAbility;
    m_RaceAbility = record.player.raceAbility;
    m_MagickaBonusRace = record.player.ffb8;
    m_MagickaBonusClass = record.player.ffba;

    // Rebuild the inventory from scratch. Everything the port had before
    // is dropped -- a load replaces the character, it does not merge.
    m_Inventory.clear();
    m_LeftItem = nullptr;
    m_RightItem = nullptr;
    for (const sk::SavedEntity& child : record.inventory) {
        const std::string& path = child.entity.objectId;
        if (path.empty()) continue;
        skExecutableContext loadCtxt(&stack.interpreter());
        try {
            auto item = std::make_unique<ItemExecutable>(skString(path.c_str()), loadCtxt, stack);
            // M74: a restored item is the same object the factory would
            // have built, so it gets the same category-derived type -- and
            // both it and the typeId land before Init(), the order
            // CreateItem() now uses too.
            const int category = stack.level().EntityCategoryOf(child.typeId);
            if (category >= 0) item->SetEntityCategory(category);
            item->SetTemplateId(child.typeId);
            skExecutableContext callCtxt(&stack.interpreter());
            RunEntityInit(*item, stack.scriptRoot(), callCtxt);
            const int quantity = child.kind == sk::SavedEntityKind::Weapon
                                     ? child.weapon.quantity
                                     : child.stackable.quantity;
            if (quantity > 0) item->SetQuantity(quantity);
            m_Inventory.push_back(std::move(item));
        } catch (skParseException& e) {
            std::printf("PlayerExecutable: PARSE ERROR restoring %s: %s\n", path.c_str(),
                        e.toString().ptr());
        } catch (skRuntimeException& e) {
            std::printf("PlayerExecutable: RUNTIME ERROR restoring %s: %s\n", path.c_str(),
                        e.toString().ptr());
        }
    }

    // The equipment slots and the two hand queues both name items by
    // typeId, so they are resolved against the inventory just rebuilt --
    // which is exactly what FUN_10043bb0 does (it looks each id up in the
    // container the base chain restored a moment earlier).
    auto findByTemplate = [this](int typeId) -> ItemExecutable* {
        if (typeId <= 0) return nullptr;
        for (const auto& item : m_Inventory) {
            if (item && item->templateId() == typeId) return item.get();
        }
        return nullptr;
    };
    for (uint16_t typeId : record.player.equipTypeIds) {
        if (ItemExecutable* item = findByTemplate(typeId)) item->SetEquipped(true);
    }
    if (record.player.selectedQuick0 >= 0 &&
        record.player.selectedQuick0 < sk::SavedPlayer::kQuickSlots) {
        m_LeftItem = findByTemplate(
            record.player.quickSlots[0][static_cast<size_t>(record.player.selectedQuick0)]);
    }
    if (record.player.selectedQuick1 >= 0 &&
        record.player.selectedQuick1 < sk::SavedPlayer::kQuickSlots) {
        m_RightItem = findByTemplate(
            record.player.quickSlots[1][static_cast<size_t>(record.player.selectedQuick1)]);
    }
}

}  // namespace sk_bindings
