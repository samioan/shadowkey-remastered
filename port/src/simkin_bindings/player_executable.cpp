#include "simkin_bindings/player_executable.h"

#include <algorithm>
#include <cstdio>

#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/native_binding_common.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"

namespace sk_bindings {

PlayerExecutable::PlayerExecutable(const sk::StringTable* strings)
    : NativeStubExecutable("Player"), m_Strings(strings) {}

void PlayerExecutable::LoadStartingInventory(const std::string& scriptRoot,
                                              skInterpreter& interpreter) {
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
        std::string fullPath = scriptRoot + "/" + relPath;
        skExecutableContext loadCtxt(&interpreter);
        try {
            auto item = std::make_unique<ItemExecutable>(skString(fullPath.c_str()), loadCtxt,
                                                           m_Strings);
            skRValueArray args;
            args.append(skRValue(0));  // placeholder for Init's "(s)" parameter
            skRValue ret;
            skExecutableContext callCtxt(&interpreter);
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
    return total;
}

void PlayerExecutable::ApplyDamage(int amount) {
    if (amount <= 0) return;
    m_Health -= amount;
    if (m_Health < 0) m_Health = 0;
}

int PlayerExecutable::UpdateEquipStatus(ItemExecutable* item, bool equipping) {
    if (!item) return 3;
    if (item->itemType() == kItemTypeArmor) {
        item->SetEquipped(equipping);
        return 0;
    }
    if (item->itemType() == kItemTypeWeapon) {
        if (equipping) {
            // Decompiled real behavior (see player_executable.h's comment):
            // unequip-toggle if already worn in either hand, else fill
            // whichever hand is currently empty (left first), else -- both
            // hands full -- a no-op success (the item would join the real
            // engine's invisible per-hand queue; this port doesn't model
            // that queue, see ShowActionQueue()'s comment).
            if (m_LeftItem == item) {
                m_LeftItem = nullptr;
                item->SetEquipped(false);
            } else if (m_RightItem == item) {
                m_RightItem = nullptr;
                item->SetEquipped(false);
            } else if (!m_LeftItem) {
                m_LeftItem = item;
                item->SetEquipped(true);
            } else if (!m_RightItem) {
                m_RightItem = item;
                item->SetEquipped(true);
            }
            // else: both hands occupied -- no visible change, still "ok".
        } else {
            if (m_LeftItem == item) m_LeftItem = nullptr;
            if (m_RightItem == item) m_RightItem = nullptr;
            item->SetEquipped(false);
        }
        return 0;
    }
    return 3;  // consumables/misc items aren't equippable
}

bool PlayerExecutable::method(const skString& methodName, skRValueArray& args,
                               skRValue& returnValue, skExecutableContext& context) {
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
        m_HasCreatedCharacter = true;
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
        m_Health = args[0].intValue();
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
        returnValue = skRValue(m_ExpToNextLevel);
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
    if (methodName == skString("GetWill") && args.entries() == 0) {
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
    if (methodName == skString("GetSpellToHit") && args.entries() == 0) {
        returnValue = skRValue(m_SpellToHit);
        return true;
    }
    if (methodName == skString("GetSpellResistance") && args.entries() == 0) {
        returnValue = skRValue(m_SpellResistance);
        return true;
    }
    if (methodName == skString("GetSpecialAbilityText") && args.entries() == 0) {
        returnValue = skRValue(skString("Special Ability"));
        return true;
    }
    if (methodName == skString("GetSpecialAbility") && args.entries() == 0) {
        returnValue = skRValue(skString("None"));
        return true;
    }
    if (methodName == skString("GetRaceAbilityText") && args.entries() == 0) {
        returnValue = skRValue(skString("Race Ability"));
        return true;
    }
    if (methodName == skString("GetRaceAbility") && args.entries() == 0) {
        returnValue = skRValue(skString("None"));
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
