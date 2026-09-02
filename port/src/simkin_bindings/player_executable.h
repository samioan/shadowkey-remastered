#pragma once

// Native binding for the object GetPlayer() returns. M3-M5 tracked only
// the main-menu/character-creation fields (name, sex, race, portrait,
// gold); M10 adds real combat/inventory state -- vitals, the stat block
// statsscreen.s reads, and a real inventory of ItemExecutable objects
// (armor/weapon/item .s scripts actually loaded and run through the
// interpreter, see LoadStartingInventory()) -- backing
// charactermanager.s/inventory.s/statsscreen.s as real, navigable screens
// rather than soft-failing placeholders.
//
// Stat block values are fixed, documented placeholders (strength=50 etc.)
// -- no character-creation stat-rolling system exists (M5's race/class
// picker doesn't feed into these), so there was nothing to derive them
// from. GetArmorRating()/GetAttack() *do* reflect real state though: they
// sum the equipped armor's real SetArmorValue()/weapon's real
// SetDamageMin/Max() on top of the flat base, so equipping something
// through the real inventory screen visibly changes these numbers.

#include <memory>
#include <string>
#include <vector>

#include "simkin_bindings/native_stub_executable.h"

class skInterpreter;

namespace sk {
class StringTable;
}

namespace sk_bindings {

class ItemExecutable;

class PlayerExecutable : public NativeStubExecutable {
public:
    explicit PlayerExecutable(const sk::StringTable* strings);

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

    // Name-entry buffer that OpenEditText's on-screen keyboard would fill
    // character by character on the real device; namechar.s's Done()/
    // NameCharBack()/OnRightSoftKey() all read it back via GetCharName()
    // and then commit it with SetPlayerName(). On PC, main.cpp fills this
    // directly from WM_CHAR (see MenuExecutable::textEntryActive()).
    void AppendCharNameChar(char c) { m_CharNameBuffer.push_back(c); }
    void BackspaceCharName() {
        if (!m_CharNameBuffer.empty()) m_CharNameBuffer.pop_back();
    }
    const std::string& charNameBuffer() const { return m_CharNameBuffer; }

    // M10: real-data verification step for this milestone, mirroring M8's
    // real .ent -> models.idx resolution -- runs a small curated set of
    // real armor/weapon/item .s files through `interpreter`, appending
    // each successfully-loaded one to the inventory. Logs (to stdout) and
    // skips any that fail to parse/run rather than aborting; not fatal if
    // the whole set is unavailable. Called once by MenuStack when a New
    // Game starts.
    void LoadStartingInventory(const std::string& scriptRoot, skInterpreter& interpreter);

    const std::vector<std::unique_ptr<ItemExecutable>>& inventory() const { return m_Inventory; }
    // Marks `item` for removal -- see item_executable.h's
    // markedForRemoval() comment for why this doesn't erase immediately.
    void RemoveItem(ItemExecutable* item);
    // Erases every inventory entry marked for removal (and clears
    // m_LeftItem/m_RightItem if either pointed at one of them) -- call
    // once per tick, after any in-flight script call chain for that tick
    // has fully returned (main.cpp's tick handler, right after menu
    // input dispatch).
    void PurgeRemovedItems();

    // inventory.s's PerformEquipAction() real implementation: toggles
    // `item`'s equipped state (armor) or assigns it to the right hand
    // slot (weapon/other, since this port doesn't have the real
    // left/right-hand *choice* UI -- actionqueue.s's full assignment flow
    // is out of scope, see docs/PORT_ROADMAP.md). Returns the same
    // ret-code convention the script branches on: 0 = ok, 3 = not
    // equippable (silently closes the action popup).
    int UpdateEquipStatus(ItemExecutable* item, bool equipping);

    ItemExecutable* leftItem() const { return m_LeftItem; }
    ItemExecutable* rightItem() const { return m_RightItem; }

    int health() const { return m_Health; }
    int maxHealth() const { return m_MaxHealth; }
    int magicka() const { return m_Magicka; }
    int maxMagicka() const { return m_MaxMagicka; }
    int fatigue() const { return m_Fatigue; }
    int maxFatigue() const { return m_MaxFatigue; }

private:
    const sk::StringTable* m_Strings;
    std::string m_Name;
    std::string m_CharNameBuffer;
    int m_Sex = 0;
    int m_Race = 0;
    int m_PortraitId = 0;
    int m_Gold = 0;
    int m_Temp = 0;
    bool m_HasCreatedCharacter = false;

    // Vitals (M10) -- fixed baseline defaults, see class comment.
    int m_Health = 100, m_MaxHealth = 100;
    int m_Magicka = 50, m_MaxMagicka = 50;
    int m_Fatigue = 100, m_MaxFatigue = 100;
    int m_Level = 1;
    int m_Experience = 0;
    int m_ExpToNextLevel = 1000;

    // Stat block (M10), matching statsscreen.s's ShowStats()/ShowSkills().
    int m_Strength = 50, m_StrengthBonus = 0;
    int m_Will = 50, m_Speed = 50, m_Personality = 50;
    int m_Intelligence = 50, m_Agility = 50, m_Endurance = 50, m_Luck = 50;
    int m_BaseDefense = 10, m_BaseAttack = 10;
    int m_SpellToHit = 50, m_SpellResistance = 0;

    std::vector<std::unique_ptr<ItemExecutable>> m_Inventory;
    ItemExecutable* m_LeftItem = nullptr;
    ItemExecutable* m_RightItem = nullptr;
};

}  // namespace sk_bindings
