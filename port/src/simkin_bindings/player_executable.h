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

#include <map>
#include <memory>
#include <set>
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
    // M19: takes ownership of a real world pickup's ItemExecutable --
    // main.cpp's Action::Use handling calls this (moving it out of
    // gamePickups) once a real OnUse() call has both invoked PickupItem()
    // (see TakePendingPickupItem()'s comment) and marked itself for
    // removal from the world (MirrorDestroyObject()).
    void AddItem(std::unique_ptr<ItemExecutable> item);
    // Erases every inventory entry marked for removal (and clears
    // m_LeftItem/m_RightItem if either pointed at one of them) -- call
    // once per tick, after any in-flight script call chain for that tick
    // has fully returned (main.cpp's tick handler, right after menu
    // input dispatch).
    void PurgeRemovedItems();

    // inventory.s's PerformEquipAction() real implementation: toggles
    // `item`'s equipped state (armor) or assigns it to a hand slot
    // (weapon/other). Decompiled the real native (FUN_10033660 case 1,
    // "UpdateEquipStatus") this session: unequip-if-already-worn happens
    // unconditionally in either hand, and a newly-equipped item only
    // becomes the *visibly wielded* item in a hand whose slot is
    // currently empty -- the real engine doesn't just always evict
    // whatever's in the right hand the way this port's earlier stub did.
    // The real function backs that with a genuine multi-item queue per
    // hand (so a second weapon equipped while both hands are full still
    // joins an invisible waitlist) -- this port doesn't model that queue
    // (see MenuExecutable's SetLeftActionQueue()/ShowActionQueue()
    // comment for why: the screen that would let a player browse it,
    // actionqueue.s, is confirmed non-functional in the real shipped
    // game), so here that same case is just a no-op return-success:
    // nothing visibly changes. Returns the same ret-code convention the
    // script branches on: 0 = ok, 3 = not equippable (silently closes the
    // action popup).
    int UpdateEquipStatus(ItemExecutable* item, bool equipping);

    ItemExecutable* leftItem() const { return m_LeftItem; }
    ItemExecutable* rightItem() const { return m_RightItem; }

    int health() const { return m_Health; }
    int maxHealth() const { return m_MaxHealth; }
    int magicka() const { return m_Magicka; }
    int maxMagicka() const { return m_MaxMagicka; }
    int fatigue() const { return m_Fatigue; }
    int maxFatigue() const { return m_MaxFatigue; }

    // Combat vertical-slice (docs/PORT_ROADMAP.md): host-side reads of
    // the same stat fields GetAttack()/GetDefense() answer over script
    // calls, so main.cpp's combat loop can read a number directly
    // instead of faking a skRValueArray call into the dispatcher.
    int baseAttack() const { return m_BaseAttack; }
    int baseDefense() const { return m_BaseDefense; }
    // Sum of every equipped armor item's real SetArmorValue() -- same
    // loop GetArmorRating()'s native handler runs, factored out here so
    // it isn't duplicated between the script-facing path and this one.
    int armorRating() const;
    // Clamps m_Health at 0 -- mirrors MonsterExecutable::ApplyDamage().
    void ApplyDamage(int amount);

    // M17: real quest-state tracking, so dialogue trees like
    // snowline/tanyinconvo.s (M16) progress across repeated visits
    // instead of always soft-failing into their first-visit branch. A
    // per-quest-id 3-flag monotonic state machine (assigned -> solved ->
    // completed, each independently settable/clearable) -- confirmed
    // against the real script corpus (a dedicated research pass this
    // session grepped every real QuestAssigned/QuestSolved/
    // QuestCompleted call site across the whole corpus, ~94/68/70 getter
    // calls and ~57/75/70 setter calls): setters are a bare
    // `SetQuestX(id)` (== `SetQuestX(id, true)`, the overwhelming
    // majority) or an explicit `SetQuestX(id, false)` to retract (only
    // ever seen on SetQuestAssigned, e.g. tanyinconvo.s's
    // `SetQuestAssigned(26, false)` when declining one branch to take
    // another) -- no counter-example to the monotonic-flags model found
    // anywhere in the corpus. host-side accessors exposed for tests, not
    // because any current host code needs to read them directly.
    bool questAssigned(int id) const { return m_QuestAssigned.count(id) != 0; }
    bool questSolved(int id) const { return m_QuestSolved.count(id) != 0; }
    bool questCompleted(int id) const { return m_QuestCompleted.count(id) != 0; }
    // AddMonsterKilled/MonstersKilled(id): confirmed (same research
    // pass) to be a shared per-quest kill-tally bucket keyed by its own
    // id, distinct from both the killed entity's typeId and any
    // SetQuestX id -- e.g. monsters/azra_rat.s's OnKilled() calls
    // AddMonsterKilled(203)/MonstersKilled(203), unrelated to its own
    // typeId (202) or the quest id (0) it separately gates on.
    int monstersKilled(int id) const {
        auto it = m_MonstersKilled.find(id);
        return it != m_MonstersKilled.end() ? it->second : 0;
    }
    int experience() const { return m_Experience; }

    // M19: PickupItem()'s handler only records *which* live object asked
    // (a raw, non-owning skiExecutable*) -- it's called from mid-way
    // through a script call chain main.cpp itself triggered
    // (ItemExecutable::InvokeOnUse()), which still holds the real
    // std::unique_ptr<ItemExecutable> in gamePickups at that point, so the
    // actual ownership transfer into m_Inventory has to happen back on the
    // host side, after InvokeOnUse() returns -- same "defer the tricky
    // move to a safe point outside the live call frame" precedent
    // markedForRemoval()/PurgeRemovedItems() already established for a
    // different (but structurally identical) reason. Clears on read, so a
    // stale value can never leak into a later, unrelated Action::Use.
    skiExecutable* TakePendingPickupItem() {
        skiExecutable* p = m_PendingPickupItem;
        m_PendingPickupItem = nullptr;
        return p;
    }

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
    // M19: see TakePendingPickupItem()'s comment.
    skiExecutable* m_PendingPickupItem = nullptr;

    // M17: quest state -- see questAssigned()/questSolved()/
    // questCompleted()/monstersKilled()'s comments above.
    std::set<int> m_QuestAssigned;
    std::set<int> m_QuestSolved;
    std::set<int> m_QuestCompleted;
    std::map<int, int> m_MonstersKilled;
};

}  // namespace sk_bindings
