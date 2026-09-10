#include "simkin_bindings/player_executable.h"

#include <algorithm>
#include <cstdio>

#include "assets/sound_archive.h"
#include "assets/string_table.h"
#include "audio/audio_engine.h"
#include "simkin_bindings/character_progression.h"
#include "simkin_bindings/combat.h"
#include "simkin_bindings/effects.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/native_binding_common.h"
#include "simkin_bindings/quest_table.h"
#include "simkin_bindings/vitals.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"

namespace sk_bindings {

PlayerExecutable::PlayerExecutable(const sk::StringTable* strings, sk::SoundArchive* sounds,
                                    sk::AudioEngine* audio)
    : NativeStubExecutable("Player"), m_Strings(strings), m_Sounds(sounds), m_Audio(audio) {}

bool PlayerExecutable::setValue(const skString& fieldName, const skString&, const skRValue& value) {
    m_Fields[ToStdString(fieldName)] = value;
    return true;
}

bool PlayerExecutable::getValue(const skString& fieldName, const skString&, skRValue& value) {
    auto it = m_Fields.find(ToStdString(fieldName));
    value = it != m_Fields.end() ? it->second : skRValue(0);
    return true;
}

void PlayerExecutable::LoadStartingInventory(MenuStack& stack) {
    // A small, curated starting kit -- one of each real category
    // (weapon/armor/consumable) this milestone's item native binding
    // covers, not an attempt at "the real game's actual starting
    // inventory" (that would need a quest/scripted-event trigger this
    // port doesn't reproduce). Proves ItemExecutable against real game
    // data the same way M8 proved model_archive.h against real
    // models.idx/.huge entries.
    static const char* kStartingItems[] = {
        "weapons/club.s",
        "armor/chain_coif.s",
        "items/bread.s",
    };
    for (const char* relPath : kStartingItems) {
        // M79: through the real creation path first. An item built straight
        // from a script path never learns its entities.txt category, and the
        // category is what picks its C++ class -- so it gets none of that
        // class's constructor defaults. For the starting club that meant
        // `+0x184` staying at the base item's `0x100` instead of the Weapon
        // constructor's `0x300`, i.e. a swing animation three times too
        // slow. See game_constants.h's M79 table.
        const int typeId = stack.level().TypeIdForScript(relPath);
        if (typeId >= 0) {
            std::unique_ptr<ItemExecutable> item = stack.level().CreateItem(typeId, true);
            if (item) {
                std::printf("PlayerExecutable: starting item '%s' -> \"%s\" (type=%d)\n", relPath,
                            item->name().c_str(), item->itemType());
                m_Inventory.push_back(std::move(item));
                continue;
            }
        }
        std::string fullPath = stack.scriptRoot() + "/" + relPath;
        skExecutableContext loadCtxt(&stack.interpreter());
        try {
            auto item =
                std::make_unique<ItemExecutable>(skString(fullPath.c_str()), loadCtxt, stack);
            skRValueArray args;
            args.append(skRValue(0));  // placeholder for Init's "(s)" parameter
            skRValue ret;
            skExecutableContext callCtxt(&stack.interpreter());
            item->method(skString("Init"), args, ret, callCtxt);
            std::printf("PlayerExecutable: starting item '%s' -> \"%s\" (type=%d)\n", relPath,
                        item->name().c_str(), item->itemType());
            m_Inventory.push_back(std::move(item));
        } catch (skParseException& e) {
            std::printf("PlayerExecutable: PARSE ERROR loading %s: %s\n", fullPath.c_str(),
                        e.toString().ptr());
        } catch (skRuntimeException& e) {
            std::printf("PlayerExecutable: RUNTIME ERROR loading %s: %s\n", fullPath.c_str(),
                        e.toString().ptr());
        }
    }
}

void PlayerExecutable::RemoveItem(ItemExecutable* item) {
    if (item) item->MarkForRemoval();
}

void PlayerExecutable::AddItem(std::unique_ptr<ItemExecutable> item) {
    // M56: `FUN_1003d8e0` -- the real add-to-inventory path tests the
    // incoming item's template id against four specific ids and sets the
    // matching global flag bit. Doing it here rather than in each caller
    // is what the original does too: GiveItem(), a world pickup and a
    // loot bag all funnel through this one vtable slot.
    if (!item) return;
    m_KeyItemFlags.OnItemAcquired(item->templateId());

    // M82: **gold is not an inventory item.** The whole of `FUN_1003d8e0`
    // after those four flag tests is wrapped in one `if (item->+0xc8 !=
    // 0x34)`, and its else is three instructions:
    //
    //     *(int *)(player + 0x3ac + 0x38) += item->+0x1c4;   // purse += qty
    //     FUN_1001b484(engine, item);                        // and destroy it
    //
    // -- so a gold object never reaches the inventory list, is never
    // stacked, is never equipped, and does not survive the call. That
    // `player + 0x3ac + 0x38` is `player + 0x3e4`, which is the exact
    // address `FUN_1003e030` (M59's BuyProduct, already ported) subtracts
    // a purchase from, so the two agree independently.
    //
    // This is the whole of "gold from a loot bag never reaches my total":
    // the port had every other part of the chain -- `loot_gold25-35.s`
    // built its `Level.CreateEntity(52)`, rolled `Random(25, 35)` into its
    // quantity and put it in the bag; `lootmenu.s` listed it as "27 Gold"
    // and its SelectItem() ran `GetPlayer().PickupItem(Object)` and
    // `GetOpener().RemoveObject(Object)`; and RemoveObject() handed the
    // object to this function. With no id test here it simply became an
    // ordinary carried object: a "Gold" row sitting in the inventory
    // forever, weightless, unusable and unsellable, while the purse never
    // moved. Every one of the game's 48 gold drops behaved that way.
    if (item->templateId() == kTemplateGold) {
        m_Gold += item->quantity();
        std::printf("PlayerExecutable: picked up %d gold (total %d)\n", item->quantity(),
                    m_Gold);
        return;  // the unique_ptr's own destructor is FUN_1001b484's half
    }

    // M82: and the other half of the same `if` -- **a consumable stacks
    // onto one the player already carries.** The real loop walks the
    // inventory (`player+0x200`, `next` at `+0x160`) whenever the incoming
    // item's type word (`+0x16c`, `FUN_1006d508`) is 4, and on a matching
    // template id does `SetQuantity(held, held->quantity + 1)`,
    // `SetOwner(held, player)` and destroys the incoming object -- the
    // same `LAB_1003db8c` the gold branch above jumps to.
    //
    // Note it adds **one**, not the incoming item's quantity: nothing but
    // gold ever carries a stack size out of a script (all 48 SetQuantity
    // call sites in the corpus are gold), so the two are never in conflict.
    // This port already had the identical rule on the *buy* path -- the
    // engine keeps a second, separate copy of it inside `FUN_1003e030`,
    // which is why M59 implemented it there and this one stayed missing --
    // so until now buying five potions in one go made five inventory rows
    // where the original makes one row of five, and two healing potions
    // out of two loot bags made two rows instead of a stack.
    //
    // The `templateId() >= 0` guard has no counterpart in the engine and
    // needs none there: every object it ever sees came from the entity
    // factory, so every one has a real id. This port can also build an
    // item straight from a script path (LoadStartingInventory), and
    // ItemExecutable spells "no template" as -1 -- which without the guard
    // would make any two such consumables stack with each other.
    if (item->itemType() == kItemTypeConsumable && item->templateId() >= 0) {
        for (const std::unique_ptr<ItemExecutable>& held : m_Inventory) {
            if (!held || held->markedForRemoval()) continue;
            if (held->templateId() != item->templateId()) continue;
            held->SetQuantity(held->quantity() + 1);
            return;
        }
    }

    ItemExecutable* added = item.get();
    m_Inventory.push_back(std::move(item));

    // M74: and the rest of `FUN_1003d8e0`, which this port had stopped
    // short of -- **picking something up equips it**. The tail of the real
    // function is:
    //
    //     if (!IsItemEnabledFor(player, item)) return;
    //     slot = item->+0x1c0;
    //     if (slot == 1) { if (stats+0x4c) return; SetRightItem(item); }
    //     else if (slot == 0) { if (stats+0x48) return; SetLeftItem(item); }
    //     else return;
    //     appendToHandQueue(...);
    //
    // -- so a weapon or a spell goes straight into the right hand and a
    // consumable into the left, provided that hand is empty and the
    // character's class may use the thing at all. That is exactly the
    // reported behaviour: take Blaze as a caster and it is armed and
    // castable on the spot, take it as a Barbarian and it is only a scroll
    // in your bag. Armour and misc items name no hand and are unaffected.
    if (!IsItemEnabledFor(*added)) return;
    switch (added->equipSlot()) {
        case kEquipSlotRight:
            if (m_RightItem) return;
            m_RightItem = added;
            break;
        case kEquipSlotLeft:
            if (m_LeftItem) return;
            m_LeftItem = added;
            break;
        default: return;
    }
    added->SetEquipped(true);
}

ItemExecutable* PlayerExecutable::FindInventoryById(const std::string& id) const {
    for (const std::unique_ptr<ItemExecutable>& item : m_Inventory) {
        if (item && !item->markedForRemoval() && item->id() == id) return item.get();
    }
    return nullptr;
}

void PlayerExecutable::PurgeRemovedItems() {
    for (auto& item : m_Inventory) {
        if (!item->markedForRemoval()) continue;
        if (m_LeftItem == item.get()) m_LeftItem = nullptr;
        if (m_RightItem == item.get()) m_RightItem = nullptr;
    }
    m_Inventory.erase(std::remove_if(m_Inventory.begin(), m_Inventory.end(),
                                      [](const std::unique_ptr<ItemExecutable>& i) {
                                          return i->markedForRemoval();
                                      }),
                       m_Inventory.end());
}

int PlayerExecutable::armorRating() const {
    int total = 0;
    for (const auto& item : m_Inventory) {
        if (item->itemType() == kItemTypeArmor && item->equipped()) {
            total += item->armorValue();
        }
    }
    // M43: a creature's HarmArmor lands here now (shadow_tentacle.s and
    // tunnel_wight.s both cast it), so the same timed modifier a creature
    // has always carried applies to the player too. Clamped at 0.
    // M58: it is the stats block's own armour field (stats+0x0c) that both
    // HarmArmor and AddEffect(ArmorValue, ...) write, so this reads the
    // field rather than re-deriving a modifier from the effect list.
    total += m_ArmorValue;
    return total < 0 ? 0 : total;
}

// ---- M59: the three trading verbs (store.h) ----

// FUN_1003e030. The order of its five failure tests matters, because a
// script shows a different message for each: stock before room, room
// before gold.
int PlayerExecutable::BuyProduct(const std::string& productName, int count) {
    if (!m_Merchant) return 0;
    const Store::StockEntry* entry = m_Merchant->FindByName(productName);
    if (!entry || !entry->record) return 0;
    if (entry->quantity < count) return 2;
    // The real cap is a flat 100 items, tested twice -- once on the count
    // as it stands and once on the count plus the purchase -- so a player
    // already at 100 is refused even for a purchase of zero.
    if (inventoryCount() > 100 || inventoryCount() + count > 100) return 3;
    const int total = count * entry->price;
    if (total > m_Gold) return 0;

    const sk::ProductRecord* record = entry->record;
    m_Gold -= total;
    if (!m_Merchant->TakeStock(record, count)) return 0;

    // A Consumable the player already carries stacks onto the existing
    // one rather than making `count` more objects. Everything else is
    // created one at a time and given the product's own rating.
    if (record->category == kItemTypeConsumable) {
        for (const std::unique_ptr<ItemExecutable>& held : m_Inventory) {
            if (held && !held->markedForRemoval() && held->templateId() == record->templateId) {
                held->SetQuantity(held->quantity() + count);
                return 1;
            }
        }
    }
    if (!m_Stack) return 1;  // nothing to build items with; the gold is spent
    for (int i = 0; i < count; ++i) {
        std::unique_ptr<ItemExecutable> item = m_Stack->level().CreateItem(record->templateId);
        if (!item) break;
        item->SetRating(record->rating);
        AddItem(std::move(item));
    }
    return 1;
}

int PlayerExecutable::SellItemToMerchant(ItemExecutable* item, int count) {
    if (!m_Merchant || !item) return 0;
    if (!m_Merchant->AcceptItem(item->templateId(), item->itemType(), count)) return 0;
    // FUN_1002edfc. The real value is the item's market value plus a
    // mercantile percentage derived from the player's own class row and
    // stats, capped at 50%. This port has neither the class table nor that
    // formula, so it pays the flat market value and the bonus is left out
    // rather than invented -- the same call this port's HealWound branch
    // already declines to guess at (spell_cast.cpp).
    m_Gold += count * item->marketValue();
    if (count == 1) {
        RemoveItem(item);
    } else {
        // The real loop removes by *template id* until nothing matches --
        // so selling five of something removes every copy the player has,
        // not five of them. Reproduced.
        const int templateId = item->templateId();
        for (std::unique_ptr<ItemExecutable>& held : m_Inventory) {
            if (held && !held->markedForRemoval() && held->templateId() == templateId) {
                held->MarkForRemoval();
            }
        }
    }
    return count;
}

int PlayerExecutable::SellAllOfItem(ItemExecutable* item) {
    if (!item) return 0;
    const int templateId = item->templateId();
    int count = 0;
    for (const std::unique_ptr<ItemExecutable>& held : m_Inventory) {
        if (!held || held->markedForRemoval() || held->templateId() != templateId) continue;
        // A Consumable contributes its whole stack; anything else one each.
        count += held->itemType() == kItemTypeConsumable ? held->quantity() : 1;
    }
    if (count == 0) return 0;
    return SellItemToMerchant(item, count);
}

// M58: FUN_1004ad40's field map for the player -- see spell_actor.h and
// effects.h. The order is the stats block's own, and the four gaps are
// stats the engine's switch has no case for either (26..29).
int* PlayerExecutable::EffectStatSlot(int stat) {
    switch (stat) {
        case kEffectStatAttack: return &m_BaseAttack;              // +0x00
        case kEffectStatDefense: return &m_BaseDefense;            // +0x02
        case kEffectStatSpellcast: return &m_Spellcast;            // +0x04
        case kEffectStatMagicResistance: return &m_MagicResistance;// +0x06
        case kEffectStatMinDamage: return &m_DamageMin;            // +0x08
        case kEffectStatMaxDamage: return &m_DamageMax;            // +0x0a
        case kEffectStatArmorValue: return &m_ArmorValue;          // +0x0c
        case kEffectStatExpWorth: return &m_ExpWorth;              // +0x0e
        case kEffectStatStrength: return &m_StrengthBonus;         // +0x10 (!)
        case kEffectStatHealthBonus: return &m_HealthBonus;        // +0x12
        case kEffectStatStrengthProper: return &m_Strength;        // +0x14
        case kEffectStatIntelligence: return &m_Intelligence;      // +0x16
        case kEffectStatAgility: return &m_Agility;                // +0x18
        case kEffectStatWill: return &m_Will;                      // +0x1a
        case kEffectStatSpeed: return &m_Speed;                    // +0x1c
        case kEffectStatEndurance: return &m_Endurance;            // +0x1e
        case kEffectStatPersonality: return &m_Personality;        // +0x20
        case kEffectStatLuck: return &m_Luck;                      // +0x22
        case kEffectStatMaxHealth: return &m_MaxHealth;            // +0x24
        case kEffectStatMaxFatigue: return &m_MaxFatigue;          // +0x26
        case kEffectStatMaxMagicka: return &m_MaxMagicka;          // +0x28
        case kEffectStatHealth: return &m_Health;                  // +0x2a
        case kEffectStatFatigue: return &m_Fatigue;                // +0x2c
        case kEffectStatMagicka: return &m_Magicka;                // +0x2e
        case kEffectStatExperience: return &m_Experience;          // +0x30, 32-bit
        case kEffectStatLevel: return &m_Level;                    // +0x34
        case kEffectStatGold: return &m_Gold;                      // +0x38, 32-bit
        default: return nullptr;
    }
}

// M43: the stats block's own per-frame tick -- see actor_stats.h. Identical
// to the creature side, because it is literally the same code on the same
// class; the only difference is which DoDamage it routes through.
void PlayerExecutable::TickStatusEffects(int deltaUnits) {
    // M48 adds the second callback: FUN_10049780's periodic kinds 6 and 7,
    // which `spells\Energize.s` and `spells\AzraSustenance.s` arm. Both add
    // the actor's own level once a second -- kind 6 to fatigue with no
    // clamp at all, kind 7 to health through the clamping setter.
    m_Stats.Tick(
        *this, deltaUnits, [this](int damage) { ApplyDamage(damage); },
        [this](int kind) {
            if (kind == ActorStats::kPeriodicFatigueRegen) {
                m_Fatigue += m_Level;  // the real branch has no ceiling here
            } else if (kind == ActorStats::kPeriodicHealthRegen) {
                SetHealth(m_Health + m_Level);
            }
        });
}

// M73: FUN_10049b64. Three independent accumulators, three periods, three
// attribute-derived amounts -- see vitals.h for the whole derivation. The
// order here is the real one (health, then magicka, then fatigue), which
// matters only in that all three share one frame delta.
void PlayerExecutable::TickVitalRegeneration(int deltaUnits) {
    if (RegenPeriodElapsed(m_Stats.healthRegenAccum(), kHealthRegenPeriod, deltaUnits) &&
        m_Health < m_MaxHealth) {
        SetHealth(m_Health + HealthRegenAmount(m_Endurance,
                                                HasEquippedTemplate(kAzraBandageTypeId)));
    }
    if (RegenPeriodElapsed(m_Stats.magickaRegenAccum(), kMagickaRegenPeriod, deltaUnits) &&
        m_Magicka < m_MaxMagicka) {
        SetActorMagicka(m_Magicka + MagickaRegenAmount(m_Will, m_Race, m_RaceAbility));
    }
    if (RegenPeriodElapsed(m_Stats.fatigueRegenAccum(), kFatigueRegenPeriod, deltaUnits) &&
        m_Fatigue < m_MaxFatigue) {
        SetActorFatigue(m_Fatigue +
                         FatigueRegenAmount(m_Strength, m_Will, m_Race, m_RaceAbility));
    }
}

bool PlayerExecutable::HasEquippedTemplate(int typeId) const {
    const ItemExecutable* hands[] = {m_LeftItem, m_RightItem};
    for (const ItemExecutable* item : hands) {
        if (item && !item->markedForRemoval() && item->templateId() == typeId) return true;
    }
    for (const std::unique_ptr<ItemExecutable>& item : m_Inventory) {
        if (item && !item->markedForRemoval() && item->equipped() &&
            item->templateId() == typeId) {
            return true;
        }
    }
    return false;
}

bool PlayerExecutable::HasItemOfTemplate(int typeId) const {
    for (const std::unique_ptr<ItemExecutable>& item : m_Inventory) {
        if (item && item->templateId() == typeId) return true;
    }
    return false;
}

bool PlayerExecutable::actorHasSpellCostDiscount() const {
    const ItemExecutable* hands[] = {m_LeftItem, m_RightItem};
    for (const ItemExecutable* item : hands) {
        if (item && item->templateId() == 0x328) return true;
    }
    return false;
}

void PlayerExecutable::ApplyDamage(int amount) {
    // M58: FUN_10049e78's first line -- the stats block's own DoDamage
    // refuses outright while the second periodic channel's kind is 4. That
    // is Sanctuary, whose channel M48 could only describe as "purely a
    // duration"; this is the gate it was for. It sits before every other
    // check because in the engine it is the whole function's `if`.
    if (m_Stats.periodicKind() == ActorStats::kPeriodicSanctuaryTimer) return;
    if (amount <= 0) return;
    // M86: `FUN_10044814`'s other statement, and the whole of what the
    // player sees when something lands on them -- `engine->+0x28->+0x17c
    // = 0x40`, the hurt timer the HUD reads to draw the red slash and to
    // redraw the vitals frame in red. It sits under the same Sanctuary
    // gate and the same `damage != 0 && (s16)damage >= 0` test as the
    // damage itself, which is why it is here rather than at the four
    // call sites: unlike a creature's flash, the player has exactly one
    // DoDamage in this port as in the engine. See main.cpp's RenderHud.
    m_HurtTimer = kHurtTimerUnits;
    m_Health -= amount;
    if (m_Health < 0) m_Health = 0;
}

void PlayerExecutable::SetHealth(int value) {
    // FUN_1004bb88, in its own order (the real code assigns first and then
    // clamps against the ceiling, then the floor -- reproduced literally so
    // a negative maxHealth would behave the same, not that one can occur).
    m_Health = value;
    if (m_MaxHealth < value) m_Health = m_MaxHealth;
    if (m_Health < 0) m_Health = 0;
}

// ---- M64: the level-up cluster (`levelup.s`) ----

bool PlayerExecutable::actorClassHasMagic() const { return ClassHasMagic(m_CharacterClass); }

// Player dispatcher case 4. Two lines in the engine, and the second is a
// virtual call on the stats block's `+0x80` vtable, slot 0x24 -- which for
// the player resolves (through a `this -= 0x3ac` thunk) to FUN_10044618.
void PlayerExecutable::LevelUp() {
    m_Level = static_cast<int16_t>(m_Level + 1);
    GrantLevelUpPoint();
}

// FUN_10044618.
void PlayerExecutable::GrantLevelUpPoint() {
    m_LevelUpPoints += 1;
    // `FUN_1001b198(engine, 0x57, x, y, 100, 0, 0, 1, 1)` -- the player's
    // own position, default volume, one repeat. Slot 87 of every zone's
    // sound manifest is `pl_cast_powerup.wav`.
    if (m_Sounds && m_Audio) {
        if (const sk::Sound* sound = m_Sounds->GetSound(kLevelUpSoundSlot)) {
            m_Audio->PlaySfx(*sound, sk::kDefaultSoundVolume, sk::kDefaultSoundRepeats);
        }
    }
    // The award always re-derives, and never refills -- gaining a level
    // raises the maxima without healing you.
    UpdateAttributes(false);
}

// FUN_1001fc24.
void PlayerExecutable::UpdateAttributes(bool restoreVitals) {
    // The class row, looked up once. A class id with no row leaves this
    // null; the engine then dereferences it for the magic flag below, so
    // the id it reads is whatever sits at address 6. Treated as "no magic"
    // here -- ChooseCharacter already clamps every script-reachable id
    // into 0..8, so no shipped path gets here with one.
    const bool hasMagic = ClassHasMagic(m_CharacterClass);

    if (m_Level == 1) {
        // A race outside 0..7 is the switch's `default`, which writes no
        // attributes at all but still falls into the recompute and refill.
        if (m_Race >= 0 && m_Race < kRaceCount) {
            // `SetSex(true)` is the male portrait -- see M51's jump-sound
            // polarity note; the engine's branch is `if (sex == 0)` taking
            // the female arm.
            const BaseAttributes base = RaceBaseAttributes(m_Race, m_Sex != 0);
            m_Strength = base.strength;
            m_Intelligence = base.intelligence;
            m_Agility = base.agility;
            m_Will = base.willpower;
            m_Speed = base.speed;
            m_Endurance = base.endurance;
            m_Personality = base.personality;
            m_Luck = base.luck;
        }
        RecomputeDerivedStats(*this);
        // The three clamping setters, in the engine's order: fatigue,
        // health, magicka, each handed its own freshly derived maximum.
        SetActorFatigue(m_MaxFatigue);
        SetHealth(m_MaxHealth);
        SetActorMagicka(m_MaxMagicka);
    }

    // Both paths converge here. A class with no magic has its pool zeroed
    // outright rather than merely clamped, which is why a Knight shows 0/0
    // magicka instead of 0/intelligence.
    if (!hasMagic) {
        m_MaxMagicka = 0;
        SetActorMagicka(0);
    } else {
        SetActorMagicka(m_MaxMagicka);
    }

    // The character-creation flag (+0xf35) is set by *this* native, not by
    // ChooseCharacter -- which is why `HasCreatedCharacter()` only goes
    // true once the portrait screen has been through.
    m_HasCreatedCharacter = true;
    if (m_Level == 1) return;

    // ---- above level 1 ----
    //
    // Both ability ranks go up, unconditionally, on every call. Note that
    // this makes them count *UpdateAttributes calls above level 1*, not
    // levels: `levelup.s`'s back key calls it again after the player has
    // spent their points, so a normal level-up raises each rank by two.
    // That is what the shipped code does; nothing debounces it.
    m_SpecialAbility += 1;
    m_RaceAbility += 1;

    if (const int growth = ClassMagickaGrowth(m_CharacterClass)) {
        // `fba = (intelligence * 0x100 * ((rank * 0x100 * growth) >> 8)) >> 0x10`,
        // which is `(intelligence * rank * growth) >> 8` once the two
        // shifts cancel. Assigned, not accumulated.
        m_MagickaBonusClass =
            static_cast<int16_t>((m_Intelligence * m_SpecialAbility * growth) >> 8);
    }
    if (m_Race == kRaceHighElf) {
        // The one racial arm: a flat five more magicka per call, through a
        // 16-bit store.
        m_MagickaBonusRace = static_cast<int16_t>(m_MagickaBonusRace + 5);
    }

    RecomputeDerivedStats(*this);
    if (!hasMagic) {
        m_MaxMagicka = 0;
        SetActorMagicka(0);
    }
    if (restoreVitals) {
        SetHealth(m_MaxHealth);
        SetActorMagicka(m_MaxMagicka);
    }
}

int PlayerExecutable::experienceToNextLevel() const {
    return ExperienceToLeaveLevel(m_CharacterClass, m_Level) - m_Experience;
}

// FUN_1004a104.
void PlayerExecutable::AddExperience(int amount) {
    // The award is a `short` all the way down: the dispatcher converts the
    // atom with the 16-bit AtomToInt and the receiving function's parameter
    // is a short too. `StatModXP(40000)` would therefore *subtract*.
    const int delta = static_cast<int16_t>(amount);
    // Strictly less, and only ever one level per call -- award enough
    // experience for three levels at once and you get one.
    if (ExperienceToLeaveLevel(m_CharacterClass, m_Level) < m_Experience + delta) {
        m_Level = static_cast<int16_t>(m_Level + 1);
        GrantLevelUpPoint();
    }
    // Banked after the check, so the threshold is tested against the
    // pre-award total plus the delta rather than against the new total.
    m_Experience += delta;
}

// M37: the real derived stats -- see combat.h. Both are computed, never
// stored: the shipped engine's own GetSpellToHit/GetSpellResistance
// bindings recompute them on every read too.
int PlayerExecutable::spellToHit() const { return SpellToHit(m_Spellcast, m_Will); }

int PlayerExecutable::spellResistance() const {
    return SpellResistance(m_MagicResistance, m_Will);
}

// M74: `FUN_1001f82c`. See the header for the whole shape; the field
// names are character_progression.h's.
bool PlayerExecutable::IsItemEnabledFor(const ItemExecutable& item) const {
    const ClassRow* row = FindClassRow(m_CharacterClass);
    if (!row) return true;  // the real lookup's null row, read through

    const int type = item.itemType();
    if (type == kItemTypeSpell) {
        // A scroll skips the whole test -- which is what makes
        // `blaze.s`'s two use texts a real fork rather than decoration:
        // "Learn Blaze" for a caster, "Take Blaze Scroll" for everyone
        // else, and the scroll they take is readable by anyone.
        if (item.scroll()) return true;
        if (!row->hasMagic) return false;
        const int mask = item.restrictUseMask();
        if (mask != 0) return (mask & (2 << row->classId)) != 0;
        return true;
    }
    if (type == kItemTypeWeapon) {
        const int weaponClass = item.weaponType();
        // Read in the shipped order, including the two blanket exemptions
        // a magic class gets. `field2 == 2` is "no weapon restriction at
        // all" and covers Assassin, Barbarian, Battlemage, Knight and
        // Spellsword.
        if (row->field2 == 2) return true;
        if (weaponClass == 0x400 && row->hasMagic) return true;  // WR_EnchantedBlade
        if (weaponClass == 0x800 && row->hasMagic) return true;
        if (weaponClass == 0) return true;  // a weapon that named no class
        return (row->field2 & weaponClass) != 0;
    }
    if (type == kItemTypeArmor) {
        const int constraint = item.armorConstraint();
        if (item.isShield()) {
            if (row->field4 == 4) return true;  // Barbarian, Knight: any shield
            if ((row->field4 & 0x18) != 0 && constraint == 2) return true;  // AR_Light
            if (constraint == 4) return ((row->field4 >> 4) & 1) != 0;      // AR_Medium
            return false;
        }
        // Armour type 7 has no equipment slot of its own (GetArmorText
        // returns nothing for it) and is never refused.
        if (item.armorType() == 7) return true;
        return (row->field0 & constraint) != 0;
    }
    return true;  // misc and consumables have no arm
}

// M74: `FUN_10033660` case 1, the real `UpdateEquipStatus`.
//
// Two things this had wrong, and both are the reported bug. The **3**
// return -- "cannot equip, close the popup silently", which `inventory.s`
// branches on -- comes only from the class gate, never from the item's
// type; and everything that is not armour is put in the hand its own
// `+0x1c0` names, not "whichever hand is free". A spell is a right-hand
// item, so it equips exactly like a sword.
int PlayerExecutable::UpdateEquipStatus(ItemExecutable* item, bool equipping) {
    if (!item) return 3;
    if (!IsItemEnabledFor(*item)) return 3;

    if (item->itemType() == kItemTypeArmor) {
        // The real arm calls FUN_1003dbdc, which toggles the worn flag and
        // applies/removes that specific item's stat bonuses. This port has
        // the toggle; the per-item bonus table is a separate, unmodelled
        // function (see armorRating(), which sums the worn items instead).
        item->SetEquipped(equipping);
        return 1;
    }

    // Already in a hand: taking it out is unconditional, and the real code
    // returns before it ever looks at the preferred slot.
    if (m_LeftItem == item) {
        m_LeftItem = nullptr;
        item->SetEquipped(false);
        return 1;
    }
    if (m_RightItem == item) {
        m_RightItem = nullptr;
        item->SetEquipped(false);
        return 1;
    }

    // Otherwise it goes in its own hand -- and only if that hand is empty.
    // The real function then appends to that hand's queue whether or not
    // the slot was free, which is the invisible waitlist this port does
    // not model (see the header's UpdateEquipStatus comment); a full hand
    // is therefore a no-op success here, exactly as before.
    ItemExecutable** slot = nullptr;
    switch (item->equipSlot()) {
        case kEquipSlotLeft: slot = &m_LeftItem; break;
        case kEquipSlotRight: slot = &m_RightItem; break;
        default: break;  // kEquipSlotNone: armour and misc, handled above
    }
    if (slot && equipping && *slot == nullptr) {
        *slot = item;
        item->SetEquipped(true);
    }
    return 1;
}

bool PlayerExecutable::method(const skString& methodName, skRValueArray& args,
                               skRValue& returnValue, skExecutableContext& context) {
    if (methodName == skString("PlaySound") && args.entries() >= 1) {
        // M27: `id` is the currently-loaded zone's own <zone>_sounds.txt
        // slot index -- confirmed by real corpus cross-reference (e.g.
        // door.s's real `GetPlayer().PlaySound(63)` on open / `PlaySound
        // (62)` on close matches azra_sounds.txt's own `63 door_open.wav`
        // / `62 door_close.wav` one-for-one; see assets/sound_archive.h's
        // class comment for the full writeup). `GetOwner().PlaySound(id)`
        // (a real item's real owner, ~50% of the corpus's real call sites)
        // routes here too once an item's been picked up -- GetOwner()
        // returns the player object directly (item_executable.cpp).
        // M51: the real dispatcher is
        // `PlaySound(id, volume = 100, directional = false, repeats = 1)`
        // -- FUN_10061a60 case 0x25 builds three optional skRValues with
        // exactly those defaults and hands them to FUN_1001b198. The
        // corpus uses one argument 120 times and all four exactly once
        // (twilite/steamsound.s's `PlaySound(65, 75, 1, 255)`: a quieter,
        // directional, endlessly repeating steam hiss), which is what
        // pinned the meaning of each.
        //
        // `directional` is not panning -- see sound_mixing.h; it asks for
        // a small (<= 12.5%) cut based on the *emitter's* facing, and is
        // applied by the caller that knows the geometry, not here.
        if (m_Sounds && m_Audio) {
            const sk::Sound* sound = m_Sounds->GetSound(args[0].intValue());
            const int volume = args.entries() >= 2 ? args[1].intValue() : sk::kDefaultSoundVolume;
            const int repeats = args.entries() >= 4 ? args[3].intValue() : sk::kDefaultSoundRepeats;
            if (sound) m_Audio->PlaySfx(*sound, volume, repeats);
        }
        return true;
    }
    if (methodName == skString("SetPlayerName") && args.entries() == 1) {
        m_Name = ToStdString(args[0].str());
        return true;
    }
    if (methodName == skString("GetPlayerName") && args.entries() == 0) {
        returnValue = skRValue(skString(m_Name.c_str()));
        return true;
    }
    if (methodName == skString("SetSex") && args.entries() == 1) {
        m_Sex = args[0].intValue();
        return true;
    }
    if (methodName == skString("ChooseRace") && args.entries() == 1) {
        m_Race = args[0].intValue();
        // ShowRaceInfo.s reads this back via GetTemp() -- same scratch
        // field ShowCharacterClass.s reads after ChooseCharacter() below;
        // whichever was picked most recently is what a following "show
        // info about what I just picked" screen would want.
        m_Temp = m_Race;
        return true;
    }
    if (methodName == skString("GetRace") && args.entries() == 0) {
        returnValue = skRValue(m_Race);
        return true;
    }
    if (methodName == skString("SetPortraitID") && args.entries() == 1) {
        m_PortraitId = args[0].intValue();
        return true;
    }
    if (methodName == skString("GetPortraitID") && args.entries() == 0) {
        returnValue = skRValue(m_PortraitId);
        return true;
    }
    if (methodName == skString("SetGold") && args.entries() == 1) {
        m_Gold = args[0].intValue();
        return true;
    }
    if (methodName == skString("GetGold") && args.entries() == 0) {
        returnValue = skRValue(m_Gold);
        return true;
    }
    if (methodName == skString("StatModGold") && args.entries() == 1) {
        m_Gold += args[0].intValue();
        return true;
    }
    if (methodName == skString("ChooseCharacter") && args.entries() == 1) {
        // ShowCharacterClass.s reads this back via GetTemp() -- see the
        // ChooseRace() comment above for why the same field is shared.
        m_Temp = args[0].intValue();
        // M56: the real handler (case 0x2e) does *not* store the raw
        // argument. It stores `FUN_100207ec(arg)`, a nine-arm switch that
        // maps 0..8 to themselves and everything else to 0 -- an explicit
        // "unknown class becomes class 0" clamp, which matters because
        // `GetCharacter()` below is what charactermanager.s branches its
        // whole per-class table on. (It also writes `arg * 2 + 0x1f` to a
        // second field, which is a sprite slot, not modelled here.)
        int chosen = args[0].intValue();
        m_CharacterClass = (chosen >= 0 && chosen <= 8) ? chosen : 0;
        // M64: the case's third line, `*(u16*)(player + 0x3e0) = 1` --
        // stats+0x34, the character level. Picking a class resets it, and
        // that reset is load-bearing: `UpdateAttributes`, which the
        // portrait screen calls two screens later, takes its
        // seed-the-attributes branch only at level 1.
        m_Level = 1;
        // The real case does *not* raise the created-character flag
        // (+0xf35); UpdateAttributes does, one screen further on. Kept
        // here as well so nothing that already depended on it regresses,
        // and because the two orders are indistinguishable from any
        // shipped script -- no menu between the two reads it.
        m_HasCreatedCharacter = true;
        return true;
    }
    if (methodName == skString("GetCharacter") && args.entries() == 0) {
        // M56: case 0x31, a bare read of the field ChooseCharacter() sets.
        // 57 real call sites, all of the shape
        // `if (GetPlayer().GetCharacter() = 3)`, so this had been
        // soft-failing to null and every such branch was dead.
        returnValue = skRValue(m_CharacterClass);
        return true;
    }
    if (methodName == skString("HasCreatedCharacter") && args.entries() == 0) {
        returnValue = skRValue(m_HasCreatedCharacter);
        return true;
    }
    if (methodName == skString("GetTemp") && args.entries() == 0) {
        returnValue = skRValue(m_Temp);
        return true;
    }
    if (methodName == skString("GetCharName") && args.entries() == 0) {
        returnValue = skRValue(skString(m_CharNameBuffer.c_str()));
        return true;
    }
    if (methodName == skString("GetPlayerClassName") && args.entries() == 0) {
        // Real per-class name text was never RE'd (M5's character
        // creation only tracks a raw ChooseCharacter() int, no class-name
        // table) -- a fixed placeholder, not a lookup.
        returnValue = skRValue(skString("Adventurer"));
        return true;
    }

    // --- M10: vitals ---
    if (methodName == skString("GetHealth") && args.entries() == 0) {
        returnValue = skRValue(m_Health);
        return true;
    }
    if (methodName == skString("GetMaxHealth") && args.entries() == 0) {
        returnValue = skRValue(m_MaxHealth);
        return true;
    }
    if (methodName == skString("SetHealth") && args.entries() == 1) {
        SetHealth(args[0].intValue());  // M34: real clamp, see SetHealth()
        return true;
    }
    if (methodName == skString("GetMagicka") && args.entries() == 0) {
        returnValue = skRValue(m_Magicka);
        return true;
    }
    if (methodName == skString("GetMaxMagicka") && args.entries() == 0) {
        returnValue = skRValue(m_MaxMagicka);
        return true;
    }
    if (methodName == skString("SetMagicka") && args.entries() == 1) {
        m_Magicka = args[0].intValue();
        return true;
    }
    if (methodName == skString("GetFatigue") && args.entries() == 0) {
        returnValue = skRValue(m_Fatigue);
        return true;
    }
    if (methodName == skString("GetMaxFatigue") && args.entries() == 0) {
        returnValue = skRValue(m_MaxFatigue);
        return true;
    }
    if (methodName == skString("SetFatigue") && args.entries() == 1) {
        // items/bread.s's OnUse() calls this via GetOwner().SetFatigue(...)
        // -- real state, clamped to the same max the HUD/stats screen
        // reads. Parenthesized as (std::min) -- the vendored Simkin
        // headers this file also includes (skGeneral.h) '#define
        // max(a,b)'/'min(a,b)' as plain macros, which would otherwise
        // swallow the unqualified name.
        m_Fatigue = (std::min)(args[0].intValue(), m_MaxFatigue);
        return true;
    }
    if (methodName == skString("GetLevel") && args.entries() == 0) {
        returnValue = skRValue(m_Level);
        return true;
    }
    if (methodName == skString("GetExperience") && args.entries() == 0) {
        returnValue = skRValue(m_Experience);
        return true;
    }
    if (methodName == skString("GetExpToNextLevel") && args.entries() == 0) {
        // M64: was a stored flat 1000. The real one (FUN_1004a020) is
        // computed on every read from the class's own experience base and
        // the level'th triangular number -- see character_progression.h.
        returnValue = skRValue(experienceToNextLevel());
        return true;
    }
    if (methodName == skString("AddExperience") && args.entries() == 1) {
        // M64: was a bare `+=`. The real one is also the level-up
        // trigger, which is the only thing in the shipped game that
        // awards a level-up point outside `cheatmenu.s`.
        AddExperience(args[0].intValue());
        return true;
    }
    // M64: `levelup.s`'s two counters, and `cheatmenu.s`'s LevelUp().
    if (methodName == skString("GetLevelUpPoints") && args.entries() == 0) {
        returnValue = skRValue(m_LevelUpPoints);
        return true;
    }
    if (methodName == skString("DecreaseLevelUpPoints") && args.entries() == 0) {
        DecreaseLevelUpPoints();
        return true;
    }
    if (methodName == skString("LevelUp") && args.entries() == 0) {
        LevelUp();
        return true;
    }
    if (methodName == skString("UpdateAttributes") && args.entries() == 1) {
        // The real case leaves with KErrArgument on a *missing* argument
        // and converts what it gets with the boolean atom reader, so any
        // truthy value is a restore.
        UpdateAttributes(args[0].intValue() != 0);
        return true;
    }
    if (methodName == skString("TestUpdateAttributes") && args.entries() == 0) {
        // Case 0x1e, a debug native with no shipped call site: level up
        // by one *without* awarding a point, then re-derive and refill.
        m_Level = static_cast<int16_t>(m_Level + 1);
        UpdateAttributes(true);
        return true;
    }
    if (methodName == skString("HasMagic") && args.entries() == 0) {
        // Case 0xd, the class row's `+6` byte -- the same one
        // UpdateAttributes gates the magicka pool on.
        returnValue = skRValue(ClassHasMagic(m_CharacterClass));
        return true;
    }
    if (methodName == skString("CountInventory") && args.entries() == 1) {
        // M39: the real handler is a single call --
        // `FUN_1006d08c(player+0x1f8, name)` -- a count of matching
        // entries in the player's own inventory collection. Real callers
        // gate quest progress on it: azra.s and glcrcrwl/nym_convo.s both
        // branch on `GetPlayer().CountInventory("stooth") = 7`, i.e. seven
        // of the item whose script called SetID("stooth"). It matches the
        // item's *id*, not its display name, which is what makes a bare
        // ASCII tag like "stooth" the right key.
        const std::string wanted = ToStdString(args[0].str());
        int count = 0;
        for (const auto& item : m_Inventory) {
            if (item && !item->markedForRemoval() && item->id() == wanted) {
                // A stacked entry counts once per unit -- SetQuantity() is
                // how loot_gold6-10.s and friends express "six of these".
                count += (std::max)(1, item->quantity());
            }
        }
        returnValue = skRValue(count);
        return true;
    }
    // --- M56: the rest of the inventory API, all of which was
    // soft-failing. See docs/SIMKIN_NATIVE_API.md's coverage table for how
    // these were found and player_executable.h/amulet_flags.h for what
    // each real handler does. ---
    if (methodName == skString("HasItem") && args.entries() == 1) {
        // Actor dispatcher case 0x1b, not a Player binding at all -- the
        // player object composites the Actor class, which is why a real
        // script can call it on GetPlayer(). The real handler walks the
        // inventory chain (`actor+0x200`, linked through `+0x160`)
        // comparing each entry's id string at `item+0xcb`; first match
        // wins and the walk stops.
        returnValue = skRValue(FindInventoryById(ToStdString(args[0].str())) != nullptr);
        return true;
    }
    if (methodName == skString("FindInventory") && args.entries() == 1) {
        // Case 0x3f. Two arms, and the first one is easy to miss: before
        // searching anything, the real handler compares the argument
        // against the literal "frozen_key" and, if that key's global flag
        // bit is set, returns the bare integer 0x325 (the key's own
        // entities.txt template id) instead of an object. Callers only
        // ever null-check the result and hand it straight to RemoveItem(),
        // both of which work on an integer -- see amulet_flags.h.
        const std::string wanted = ToStdString(args[0].str());
        if (wanted == "frozen_key" && m_KeyItemFlags.test(kFlagFrozenKey)) {
            returnValue = skRValue(kFrozenKeyStandIn);
            return true;
        }
        ItemExecutable* found = FindInventoryById(wanted);
        if (found) {
            returnValue = skRValue(static_cast<skiExecutable*>(found), false);
        } else {
            // The real miss path leaves the return atom untouched, which
            // is what makes the corpus's `if (Key != null)` work.
            returnValue = skRValue();
        }
        return true;
    }
    if (methodName == skString("RemoveItem") && (args.entries() == 1 || args.entries() == 2)) {
        // Case 0x4a, whose whole shape is a type switch on the first
        // argument: an *object* atom is unwrapped to the item it names,
        // while an *integer* atom takes the frozen-key arm -- decrement
        // the key counter (see amulet_flags.h for the deliberate
        // off-by-one), then look the template up in the inventory and
        // remove whatever it finds, returning silently if it finds
        // nothing. The second argument is a replication flag in the
        // original (it only gates a multiplayer notify call), so it is
        // accepted and ignored here for the same reason DoorOpened() is.
        ItemExecutable* target = nullptr;
        if (args[0].type() == skRValue::T_Object) {
            target = dynamic_cast<ItemExecutable*>(args[0].obj());
        } else {
            int templateId = args[0].intValue();
            if (templateId == kTemplateFrozenKey) m_KeyItemFlags.OnFrozenKeyRemoved();
            for (const std::unique_ptr<ItemExecutable>& item : m_Inventory) {
                if (item && !item->markedForRemoval() && item->templateId() == templateId) {
                    target = item.get();
                    break;
                }
            }
            if (!target) return true;  // the real handler's early return
        }
        RemoveItem(target);
        return true;
    }
    if (methodName == skString("GiveItem") && args.entries() == 1) {
        // Case 0x2d: build the entity for this entities.txt template id
        // and push it through the same add-to-inventory path a real world
        // pickup takes (the player vtable's `+0x164`). Reuses
        // LevelExecutable's existing item factory rather than a second
        // one -- it already applies the category filter and runs the
        // script's Init().
        int typeId = args[0].intValue();
        if (!m_Stack) {
            return SoftFailNativeCall("Player", methodName, args, returnValue);
        }
        std::unique_ptr<ItemExecutable> item = m_Stack->level().CreateItem(typeId);
        if (!item) {
            std::printf("  Player: GiveItem(%d) -- no item-shaped entities.txt row\n", typeId);
            return true;
        }
        // CreateItem() already stamps the template id, which AddItem()
        // below needs in order to notice a key item going in.
        AddItem(std::move(item));
        return true;
    }
    if (methodName == skString("HasAmulet") && args.entries() == 1) {
        // Case 0x3e -- a read of the global flag word, *not* an inventory
        // search. See amulet_flags.h.
        returnValue = skRValue(m_KeyItemFlags.HasAmulet(ToStdString(args[0].str())));
        return true;
    }
    if (methodName == skString("OpenMenu") && args.entries() == 1) {
        // 332 real call sites, the single largest unhandled native in the
        // corpus. The Player class registers OpenMenu at index 3 and its
        // dispatcher's recovered switch has no case 3, so the shape came
        // from the Object/Entity class's own OpenMenu (case 0x22), which
        // is `FUN_100779b8(menuManager, name, 1, self)` -- open by name,
        // with the caller as the new menu's opener.
        //
        // ReopenMenu(), not OpenMenu(), for the M17 reason every other
        // class's OpenMenu already uses it: the target menu's own Init()
        // is where the quest-state branching lives, and it has to re-run
        // on each visit rather than once.
        if (!m_Stack) {
            return SoftFailNativeCall("Player", methodName, args, returnValue);
        }
        m_Stack->ReopenMenu(ToStdString(args[0].str()), static_cast<skiExecutable*>(this));
        return true;
    }
    if (methodName == skString("PickupItem") && args.entries() == 1) {
        // M19: a real world pickup's OnUse() calls this as
        // "GetPlayer().PickupItem(self)" -- only records which live
        // ItemExecutable asked (main.cpp still owns it via a
        // std::unique_ptr in gamePickups, mid-call here), see
        // TakePendingPickupItem()'s comment for why ownership transfer
        // itself has to happen back on the host side, not in here.
        m_PendingPickupItem = args[0].obj();
        return true;
    }

    // --- M17: quest state -- see player_executable.h's class comment on
    // questAssigned()/questSolved()/questCompleted() for the real
    // monotonic-flags model this implements. Bare SetQuestX(id) means
    // "set true"; the corpus's only real use of an explicit 2nd argument
    // is `false` (to retract), so bool-check it directly rather than
    // special-casing true/false separately.
    if (methodName == skString("QuestAssigned") && args.entries() == 1) {
        returnValue = skRValue(questAssigned(args[0].intValue()));
        return true;
    }
    if (methodName == skString("SetQuestAssigned") &&
        (args.entries() == 1 || args.entries() == 2)) {
        int id = args[0].intValue();
        bool value = args.entries() == 2 ? args[1].boolValue() : true;
        if (value) {
            m_QuestAssigned.insert(id);
        } else {
            m_QuestAssigned.erase(id);
        }
        return true;
    }
    if (methodName == skString("QuestSolved") && args.entries() == 1) {
        returnValue = skRValue(questSolved(args[0].intValue()));
        return true;
    }
    if (methodName == skString("SetQuestSolved") && (args.entries() == 1 || args.entries() == 2)) {
        int id = args[0].intValue();
        bool value = args.entries() == 2 ? args[1].boolValue() : true;
        if (value) {
            m_QuestSolved.insert(id);
        } else {
            m_QuestSolved.erase(id);
        }
        return true;
    }
    if (methodName == skString("QuestCompleted") && args.entries() == 1) {
        returnValue = skRValue(questCompleted(args[0].intValue()));
        return true;
    }
    if (methodName == skString("SetQuestCompleted") &&
        (args.entries() == 1 || args.entries() == 2)) {
        int id = args[0].intValue();
        bool value = args.entries() == 2 ? args[1].boolValue() : true;
        if (value) {
            m_QuestCompleted.insert(id);
        } else {
            m_QuestCompleted.erase(id);
        }
        return true;
    }
    if (methodName == skString("AllQuests") && args.entries() == 0) {
        // M88: index 39, and the one quest native no shipped script
        // calls -- a leftover debug switch. Its body is a plain
        // descending memset of `player+0x430..+0x52f` to 1, i.e. it
        // assigns **all 256** ids, not the 50 that have text; the extra
        // 206 are invisible in the quest log for the reason quest_table.h
        // explains. Reproduced at that width rather than at 50 so the two
        // agree if a script ever reads QuestAssigned(200) after it.
        for (int id = 0; id < kQuestStateSlots; ++id) m_QuestAssigned.insert(id);
        return true;
    }
    if (methodName == skString("AddMonsterKilled") && args.entries() == 1) {
        ++m_MonstersKilled[args[0].intValue()];
        return true;
    }
    if (methodName == skString("MonstersKilled") && args.entries() == 1) {
        returnValue = skRValue(monstersKilled(args[0].intValue()));
        return true;
    }

    // --- M10: stat block (statsscreen.s) ---
    if (methodName == skString("GetStrength") && args.entries() == 0) {
        returnValue = skRValue(m_Strength);
        return true;
    }
    if (methodName == skString("GetStrengthBonus") && args.entries() == 0) {
        returnValue = skRValue(m_StrengthBonus);
        return true;
    }
    // GetWil/GetWill/GetWillpower are three real bindings on the same
    // field (indices 0x1d/0x1e/0x3c all read stats+0x1a).
    if ((methodName == skString("GetWill") || methodName == skString("GetWil") ||
         methodName == skString("GetWillpower")) &&
        args.entries() == 0) {
        returnValue = skRValue(m_Will);
        return true;
    }
    if (methodName == skString("GetSpeed") && args.entries() == 0) {
        returnValue = skRValue(m_Speed);
        return true;
    }
    if (methodName == skString("GetPersonality") && args.entries() == 0) {
        returnValue = skRValue(m_Personality);
        return true;
    }
    if (methodName == skString("GetIntelligence") && args.entries() == 0) {
        returnValue = skRValue(m_Intelligence);
        return true;
    }
    if (methodName == skString("GetAgility") && args.entries() == 0) {
        returnValue = skRValue(m_Agility);
        return true;
    }
    if (methodName == skString("GetEndurance") && args.entries() == 0) {
        returnValue = skRValue(m_Endurance);
        return true;
    }
    if (methodName == skString("GetLuck") && args.entries() == 0) {
        returnValue = skRValue(m_Luck);
        return true;
    }

    // ---- M65: the eight attribute rolls, cases 0x0b..0x12 ----
    //
    // `if (GetPlayer().TestStrength(25) = true)`. The roll itself is
    // AttributeCheck() (character_progression.h, which carries the
    // derivation); all this arm does is pick which of the eight fields it
    // reads -- the same fields the getters just above read, and the same
    // fields the effects system writes, so a Fortify Strength really does
    // move the odds of a strength check.
    //
    // Every one of the 14 shipped call sites is in `crypt1.s`, and every
    // one has the same shape: a named trigger zone, a `saved_` flag so the
    // check is once-only, and a pass/fail popup. Four attributes are
    // tested (Strength, Agility, Endurance, Speed) at difficulty 25, 30 and
    // 35, plus two strength-only checks at 40 and 45. Intelligence,
    // Willpower, Personality and Luck have bindings and no caller.
    //
    // The engine leaves with KErrArgument on *no* arguments and ignores any
    // beyond the first, so this takes >= 1 rather than exactly 1.
    {
        const int* attribute = nullptr;
        if (methodName == skString("TestStrength")) {  // 0x0b
            attribute = &m_Strength;
        } else if (methodName == skString("TestIntelligence")) {  // 0x0c
            attribute = &m_Intelligence;
        } else if (methodName == skString("TestAgility")) {  // 0x0d
            attribute = &m_Agility;
        } else if (methodName == skString("TestWill")) {  // 0x0e
            attribute = &m_Will;
        } else if (methodName == skString("TestSpeed")) {  // 0x0f
            attribute = &m_Speed;
        } else if (methodName == skString("TestEndurance")) {  // 0x10
            attribute = &m_Endurance;
        } else if (methodName == skString("TestPersonality")) {  // 0x11
            attribute = &m_Personality;
        } else if (methodName == skString("TestLuck")) {  // 0x12
            attribute = &m_Luck;
        }
        if (attribute && args.entries() >= 1) {
            returnValue = skRValue(AttributeCheck(*attribute, args[0].intValue()));
            return true;
        }
    }

    if (methodName == skString("DoDamage") && args.entries() >= 1) {
        // M65: case 0x16, which is what *failing* one of the rolls above
        // costs. The engine reads the argument as a **short**
        // (`sVar4 = AtomToInt(...)`, sign-extended back to int at the call)
        // and then invokes the stats block's own DoDamage through its
        // secondary vtable at stats+0x80, slot 4, with three zero arguments
        // after it -- that slot is FUN_10049e78, which M58 already
        // recovered as ApplyDamage(), Sanctuary gate included. So this is a
        // route to existing machinery, not new behaviour: the milestone
        // needs it only because all 14 attribute checks pay for a failure
        // with it, and a check whose failure did nothing would be half
        // implemented.
        //
        // All 20 shipped call sites are `GetPlayer().DoDamage(...)` -- no
        // script ever damages a creature this way, which is consistent with
        // the vtable slot being reached only from the Character-stats
        // dispatcher and every scripted use being an environmental hazard
        // (crypt1's failed checks and braziers, Dragonstar's traps).
        ApplyDamage(static_cast<int16_t>(args[0].intValue()));
        return true;
    }

    // ---- M64: the eight attribute setters, cases 0x24..0x2b ----
    //
    // Each is `field = (short)AtomToInt(arg)` followed by the derived-stat
    // recompute -- *except* SetSpeed, whose arm branches straight to the
    // function epilogue instead of into the shared
    // `mov r0, r6 / bl 0x10049698` tail the other seven jump to. Verified
    // in the disassembly rather than trusted from the decompiler, because
    // it is the kind of thing a decompiler flattens. Speed feeds none of
    // the three derived maxima, so skipping it changes nothing on its own;
    // it is observable only as a *missed* recompute -- if something else
    // moved strength, endurance, willpower or intelligence without
    // recomputing, SetSpeed will not catch up for it the way its seven
    // siblings would.
    //
    // 21 shipped call sites across eight setters. Sixteen are
    // `GetPlayer().SetX(GetPlayer().GetX() + 5)` on `levelup.s` and the
    // two Dragonstar skeleton-key shrines; the rest (Lothna's chests and
    // stones, the grey mushroom) compute a value and assign it outright.
    {
        int* field = nullptr;
        bool recomputes = true;
        if (methodName == skString("SetStrength")) {  // 0x24
            field = &m_Strength;
        } else if (methodName == skString("SetIntelligence")) {  // 0x25
            field = &m_Intelligence;
        } else if (methodName == skString("SetWillpower")) {  // 0x26
            field = &m_Will;
        } else if (methodName == skString("SetAgility")) {  // 0x27
            field = &m_Agility;
        } else if (methodName == skString("SetSpeed")) {  // 0x28 -- the odd one
            field = &m_Speed;
            recomputes = false;
        } else if (methodName == skString("SetEndurance")) {  // 0x29
            field = &m_Endurance;
        } else if (methodName == skString("SetPersonality")) {  // 0x2a
            field = &m_Personality;
        } else if (methodName == skString("SetLuck")) {  // 0x2b
            field = &m_Luck;
        }
        if (field && args.entries() == 1) {
            // A halfword store in the engine, so the value truncates.
            *field = static_cast<int16_t>(args[0].intValue());
            if (recomputes) RecomputeDerivedStats(*this);
            return true;
        }
    }
    if (methodName == skString("GetDefense") && args.entries() == 0) {
        returnValue = skRValue(m_BaseDefense);
        return true;
    }
    if (methodName == skString("GetAttack") && args.entries() == 0) {
        // Real state: base attack plus each equipped hand's weapon average
        // damage, if any -- UpdateEquipStatus() fills whichever hand is
        // empty first (left, then right), not always the right hand, so
        // both are checked here.
        int total = m_BaseAttack;
        if (m_LeftItem && m_LeftItem->itemType() == kItemTypeWeapon) {
            total += (m_LeftItem->damageMin() + m_LeftItem->damageMax()) / 2;
        }
        if (m_RightItem && m_RightItem->itemType() == kItemTypeWeapon) {
            total += (m_RightItem->damageMin() + m_RightItem->damageMax()) / 2;
        }
        returnValue = skRValue(total);
        return true;
    }
    if (methodName == skString("GetArmorRating") && args.entries() == 0) {
        returnValue = skRValue(armorRating());
        return true;
    }
    // ---- M59: the merchant natives (store.h) ----
    //
    // Every one of them is a no-op while `player+0xf84` is null, and the
    // engine's own guard is `return 1` *without a return value* -- so a
    // script that reads one outside a shop keeps whatever its variable
    // already held rather than seeing a 0. Reproduced by returning true
    // and leaving `returnValue` alone.
    if (methodName == skString("SetMerchant") && args.entries() == 1) {
        m_Merchant = nullptr;
        if (args[0].type() == skRValue::T_Object) {
            if (auto* monster = dynamic_cast<MonsterExecutable*>(args[0].obj())) {
                m_Merchant = &monster->store();
            }
        }
        return true;
    }
    if (methodName == skString("GetDefaultStore") && args.entries() == 0) {
        if (!m_Merchant) return true;
        returnValue = skRValue(m_Merchant->defaultCategory());
        return true;
    }
    if (methodName == skString("GetProductCount") && args.entries() == 1) {
        if (!m_Merchant) return true;
        returnValue = skRValue(m_Merchant->productCount(args[0].intValue()));
        return true;
    }
    if ((methodName == skString("GetProduct") || methodName == skString("GetItemDescription")) &&
        args.entries() == 1) {
        // Both return the product's *description* string -- the "Long
        // blade, damage 4 - 12" line, not its name. GetItemDescription
        // additionally falls back to the player's own inventory when the
        // name is not on the shelves, which is what makes the same popup
        // work on the sell page; GetProduct has no such fallback.
        const std::string wanted = ToStdString(args[0].str());
        const sk::StringTable* strings = m_Stack ? m_Stack->strings() : m_Strings;
        if (m_Merchant) {
            const Store::StockEntry* entry = m_Merchant->FindByName(wanted);
            if (entry && entry->record && strings) {
                returnValue =
                    skRValue(skString(strings->Get(entry->record->descriptionStringId).c_str()));
                return true;
            }
        }
        if (methodName == skString("GetItemDescription")) {
            for (const std::unique_ptr<ItemExecutable>& held : m_Inventory) {
                if (!held || held->markedForRemoval() || held->name() != wanted) continue;
                skRValueArray none;
                skRValue description;
                held->method(skString("GetItemDescription"), none, description, context);
                returnValue = description;
                return true;
            }
        }
        return true;
    }
    if (methodName == skString("BuyItem") && args.entries() >= 1) {
        const int count = args.entries() > 1 ? args[1].intValue() : 1;
        returnValue = skRValue(BuyProduct(ToStdString(args[0].str()), count));
        return true;
    }
    if (methodName == skString("SellItem") && args.entries() >= 1) {
        if (!m_Merchant) {
            returnValue = skRValue(0);
            return true;
        }
        const int count = args.entries() > 1 ? args[1].intValue() : 1;
        ItemExecutable* item = args[0].type() == skRValue::T_Object
                                   ? dynamic_cast<ItemExecutable*>(args[0].obj())
                                   : nullptr;
        returnValue = skRValue(SellItemToMerchant(item, count));
        return true;
    }
    if (methodName == skString("SellAllItems") && args.entries() == 1) {
        ItemExecutable* item = args[0].type() == skRValue::T_Object
                                   ? dynamic_cast<ItemExecutable*>(args[0].obj())
                                   : nullptr;
        returnValue = skRValue(SellAllOfItem(item));
        return true;
    }
    if ((methodName == skString("BuyFromMerchant") || methodName == skString("SellToMerchant")) &&
        args.entries() == 0) {
        // The real pair fetch screen mode 5 -- the store screen -- from
        // the controller and call FUN_10034f38(screen, buying), which
        // writes `mode = buying ? 1 : 2`. This port has no screen-mode
        // table, so the mode is parked on the stack and stamped onto the
        // screen as it opens (MenuStack::OpenMenu).
        if (!m_Stack) return SoftFailNativeCall("Player", methodName, args, returnValue);
        m_Stack->SetPendingScreenMode(methodName == skString("BuyFromMerchant") ? 1 : 2);
        m_Stack->ReopenMenu("buysell");
        return true;
    }
    // M62: Entity bindings 0x30/0x31/0x34/0x35/0x36, the base class every
    // placed thing inherits (entity_position_ref.h). 44 of the corpus's 63
    // call sites are on the player -- `GetPlayer().SetPosition(...)` and
    // the bare-global `Player.SetPosition(...)` -- and they are how
    // `cheatmenu.s` teleports, how `broken2.s` moves the player between its
    // wings, and how `dstar_e/pit_boss_battle.s` places both fighters
    // before each round. All of them soft-failed until now.
    if (HandleEntityPositionNative(methodName, args, returnValue)) return true;
    if (methodName == skString("SetCameraStart") && args.entries() == 6) {
        // M61: Player binding 0x39 -- the scripted spawn override. The
        // real case reads exactly six arguments and leaves (with the
        // interpreter's own "wrong argument count" error) on the first one
        // missing, so this deliberately matches on 6 and nothing else.
        //
        // Argument order is (x, y, z, pitch, yaw, roll) and is NOT the
        // order the six values are stored in -- MenuStack::CameraStart's
        // comment has the whole explanation, and it is the interesting
        // half of this binding.
        //
        // Every one of the 49 shipped calls passes 0 for pitch and 0 for
        // roll, and a signed 65536-per-turn angle for yaw: a spawn point
        // and a facing, nothing more. The two dead channels are carried
        // anyway because the engine carries them.
        if (!m_Stack) return SoftFailNativeCall("Player", methodName, args, returnValue);
        m_Stack->ArmCameraStart(args[0].intValue(), args[1].intValue(), args[2].intValue(),
                                 args[3].intValue(), args[4].intValue(), args[5].intValue());
        return true;
    }

    // --- M58: the effects system (effects.h). The busiest unimplemented
    // native in the corpus was `GetPlayer().AddEffect` (20 sites) and
    // `GetOwner().AddEffect` (63) -- the same binding on the same class,
    // which is why both receivers route into one handler here. ---
    if (HandleEffectNative(*this, methodName, args, returnValue)) return true;

    // M37: both were flat stored numbers; both are now the real derived
    // formulas (combat.h), so statsscreen.s shows a value that actually
    // moves with willpower the way the shipped game's does.
    if (methodName == skString("GetSpellToHit") && args.entries() == 0) {
        returnValue = skRValue(spellToHit());
        return true;
    }
    if (methodName == skString("GetSpellResistance") && args.entries() == 0) {
        returnValue = skRValue(spellResistance());
        return true;
    }
    if (methodName == skString("GetSpellcast") && args.entries() == 0) {
        returnValue = skRValue(m_Spellcast);
        return true;
    }
    if (methodName == skString("GetMagicResistance") && args.entries() == 0) {
        returnValue = skRValue(m_MagicResistance);
        return true;
    }
    if (methodName == skString("SetSpellcast") && args.entries() == 1) {
        m_Spellcast = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetMagicResistance") && args.entries() == 1) {
        m_MagicResistance = args[0].intValue();
        return true;
    }
    // M64: SetWillpower moved into the eight-setter table above, which
    // adds the derived-stat recompute it was missing. Its `SetWill` alias
    // went with it -- the real trie has `SetWillpower` (0x26) and no
    // `SetWill`, and nothing in the corpus calls one.
    if (methodName == skString("SetLevel") && args.entries() == 1) {
        m_Level = args[0].intValue();
        return true;
    }
    if (methodName == skString("GetSpecialAbilityText") && args.entries() == 0) {
        returnValue = skRValue(skString("Special Ability"));
        return true;
    }
    // M64 -- CORRECTED. Both of these were returning the string "None".
    // The real cases (0x17 and 0x16) return an **int**: the class ability
    // rank at player+0xfb0 and the race ability rank at +0xfb4, the same
    // two words UpdateAttributes raises on every level. `statsscreen.s`
    // shows them as `""#GetPlayer().GetSpecialAbility()`, string-
    // concatenating a number, which is what a rank looks like and a name
    // does not.
    //
    // Neither case reads its argument count, so a stray argument is
    // ignored rather than rejected -- which matters, because
    // `dstar_e/join_thief_guild.s` calls `GetSpecialAbility(val)` with
    // one. That is why these do not test `args.entries()`.
    if (methodName == skString("GetSpecialAbility")) {
        returnValue = skRValue(m_SpecialAbility);
        return true;
    }
    if (methodName == skString("SetSpecialAbility") && args.entries() == 1) {
        // Cases 0xe / 0xf: a plain 32-bit store, no clamp. The three
        // guild-training conversations (`fighter_training.s`,
        // `thief_convo.s`, `eranthos_convo.s`) read the rank, add to it
        // and write it back -- a trainer selling a rank outright.
        m_SpecialAbility = args[0].intValue();
        return true;
    }
    if (methodName == skString("GetRaceAbilityText") && args.entries() == 0) {
        returnValue = skRValue(skString("Race Ability"));
        return true;
    }
    if (methodName == skString("GetRaceAbility")) {
        returnValue = skRValue(m_RaceAbility);
        return true;
    }
    if (methodName == skString("SetRaceAbility") && args.entries() == 1) {
        // `crypt2/shadowgate.s` is the one shipped caller: +3 ranks.
        m_RaceAbility = args[0].intValue();
        return true;
    }

    // --- M10: inventory/equip ---
    if (methodName == skString("GetLeftItem") && args.entries() == 0) {
        if (m_LeftItem) returnValue = skRValue(static_cast<skiExecutable*>(m_LeftItem), false);
        return true;
    }
    if (methodName == skString("GetRightItem") && args.entries() == 0) {
        if (m_RightItem) returnValue = skRValue(static_cast<skiExecutable*>(m_RightItem), false);
        return true;
    }
    if (methodName == skString("EquipItem") && args.entries() == 2) {
        // M22 read this as a direct hand assignment and had to guess which
        // hand `0` meant. **M74: the hand argument is not used at all.**
        // The shipped handler (Player case 0x54, 0x10041e9c) reads it with
        // `SIMKIN_AtomToInt` and throws the result away -- the disassembly
        // discards r0 immediately -- then walks the inventory and, only if
        // the item is not already in it, calls the player's own
        // add-to-inventory slot `+0x164` (`FUN_1003d8e0`). Which hand the
        // item lands in comes from the item's own `+0x1c0`, exactly as it
        // does for a world pickup or a purchase.
        //
        // That is why every spell in the game writes `EquipItem(0, self)`
        // in its `OnUse()` and yet a spell is a *right*-hand item: the 0 is
        // decoration. Reproduced, argument and all.
        //
        // The one deviation is the inventory half: this port's AddItem()
        // takes ownership, and a script reaching here holds a raw pointer
        // to an object something else already owns (`self` is an inventory
        // entry by construction), so only the equip tail runs.
        ItemExecutable* item = dynamic_cast<ItemExecutable*>(args[1].obj());
        if (item && IsItemEnabledFor(*item)) {
            switch (item->equipSlot()) {
                case kEquipSlotRight:
                    if (!m_RightItem) {
                        m_RightItem = item;
                        item->SetEquipped(true);
                    }
                    break;
                case kEquipSlotLeft:
                    if (!m_LeftItem) {
                        m_LeftItem = item;
                        item->SetEquipped(true);
                    }
                    break;
                default: break;
            }
        }
        return true;
    }
    if (methodName == skString("IsItemEnabledFor") && args.entries() == 1) {
        // M74: real, at last. M22 answered a flat `true` because "M5's
        // character creation only covers race/portrait/name, no class
        // system at all" -- M64 built that class system, and `FUN_1001f82c`
        // turns out to be the one consumer of the three unnamed mask words
        // in its class table. See IsItemEnabledFor() above.
        //
        // A non-item argument (a table cell, a null) keeps the permissive
        // answer, which is also what the real function does with a null row.
        const ItemExecutable* item = dynamic_cast<const ItemExecutable*>(args[0].obj());
        returnValue = skRValue(item ? IsItemEnabledFor(*item) : true);
        return true;
    }
    if (methodName == skString("ResetQueue") && args.entries() == 1) {
        // Dead in the real shipped game -- charactermanager.s's only call
        // site (its ResetQueue() handler) is on a popup item
        // (actionQueuePopup.AddItem(3776,"ResetQueue")) that's commented
        // out in the real script, and that popup is never even made
        // visible on the live click path anyway. Accepted so nothing logs
        // soft-fail noise if a build ever does reach it; no state to reset.
        return true;
    }
    if (methodName == skString("MoveToOtherQueue") || methodName == skString("RemoveItemFromQueue")) {
        // Reachable (DropItem/RelocateItem in actionqueue.s), but always
        // called with a null argument in the real game: both handlers read
        // `selectedItem.GetAssociatedObject()` where selectedItem came from
        // a row actionqueue.s's own UpdateTextItems()/GetLastItem() calls
        // were supposed to populate -- and those two names are absent from
        // the fully-enumerated real native binding table (confirmed this
        // session, decompiled cross-check against
        // shadowkey/simkin_native_bindings.json), so they never resolve
        // and the rows are never actually bound to a real item. Accepted
        // no-op, matching that real (non-functional) behavior faithfully.
        return true;
    }
    return SoftFailNativeCall("Player", methodName, args, returnValue);
}

}  // namespace sk_bindings
