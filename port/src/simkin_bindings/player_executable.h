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

#include "assets/save_records.h"
#include "simkin_bindings/actor_stats.h"
#include "simkin_bindings/native_stub_executable.h"
#include "simkin_bindings/spell_actor.h"
#include "skRValue.h"

class skInterpreter;

namespace sk {
class StringTable;
class SoundArchive;
class AudioEngine;
}

namespace sk_bindings {

class ItemExecutable;
class MenuStack;

class PlayerExecutable : public NativeStubExecutable, public SpellActor {
public:
    // M27: `sounds`/`audio` may be null (every test constructs a
    // PlayerExecutable without them, via MenuStack's own matching
    // defaults) -- PlaySound() then silently no-ops. See method()'s
    // PlaySound handler and MenuStack::MenuStack()'s own comment for why
    // this is a constructor param here (not read from a MenuStack&,
    // unlike DoorExecutable/MonsterExecutable/ItemExecutable/
    // LevelExecutable -- PlayerExecutable predates the MenuStack that
    // owns it and holds no reference back to it).
    explicit PlayerExecutable(const sk::StringTable* strings, sk::SoundArchive* sounds = nullptr,
                               sk::AudioEngine* audio = nullptr);

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

    // M23: a generic scalar-field fallback -- real scripts (azra.s alone:
    // saved_EndGame, saved_Birgidda, saved_Skelos, saved_Rescue1-4,
    // saved_Heather, saved_SkelosDead, ...) read and write arbitrary
    // "saved_X" flags directly on GetPlayer() as ad-hoc storage, not
    // through any declared native method. An ordinary skScriptedExecutable
    // -backed class (Item/Door/Monster/Menu) gets this for free -- it's
    // TreeNode-backed, so an undeclared field read/write just works.
    // PlayerExecutable derives from NativeStubExecutable instead (a
    // native-only singleton, no `.s` file of its own), whose setValue()/
    // getValue() both hardcode `return false` -- meaning every one of
    // these real field accesses would throw "Field ... not found" without
    // this override. A never-written field reads back as a benign
    // skRValue(0) (found+defaulted, not "not found") -- matches every
    // real `if (GetPlayer().saved_X = 1)` check's implied assumption that
    // an unset save flag is falsy, not an error; the real engine's actual
    // native player-state object almost certainly works the same way
    // (a save-flag bucket defaulting unset entries to 0), inferred from
    // this real usage pattern, not decompiled.
    bool setValue(const skString& fieldName, const skString& attribute,
                  const skRValue& value) override;
    bool getValue(const skString& fieldName, const skString& attribute,
                  skRValue& value) override;

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
    // real armor/weapon/item .s files through `stack`'s interpreter,
    // appending each successfully-loaded one to the inventory. Logs (to
    // stdout) and skips any that fail to parse/run rather than aborting;
    // not fatal if the whole set is unavailable. Called once by MenuStack
    // when a New Game starts.
    //
    // M21: takes `MenuStack&` (was `scriptRoot`/`interpreter` separately)
    // -- ItemExecutable's own constructor needs a MenuStack now too (see
    // its header comment), and `stack` already has everything this method
    // itself needs (scriptRoot(), interpreter()).
    void LoadStartingInventory(MenuStack& stack);

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

    // M48: `FUN_1006d1c0(owner+0x1f8, typeId)` -- does this actor's
    // inventory already hold an entity of this type? The real cast's
    // DaedricWeapon branch is guarded on it, so casting it twice does not
    // hand out two swords.
    bool HasItemOfTemplate(int typeId) const;
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

    // M37: the caster side of the real magic to-hit model (see combat.h).
    //
    // level() is the real `magnitude` behind every status-effect branch --
    // FUN_100458e4 reads the caster's stats block `+0x34`, which
    // FUN_10048244's own GetLevel/SetLevel bindings identify as the
    // character level, and clamps it to 25. It is not the spell's
    // SetRating(), which the same pass showed is a per-spell *ordinal*
    // (absorb 1, blind 3, curedisease 6, ... azrasustenance 28 -- a dense
    // run with duplicates, and absent entirely from the u_*_lvlN.s and
    // scroll variants, which set SetLevel instead).
    int level() const { return m_Level; }
    int willpower() const { return m_Will; }
    int spellToHit() const;
    int spellResistance() const;

    // M34: the real stats-block SetHealth (FUN_1004bb88), which is three
    // instructions long and does exactly one interesting thing -- it
    // clamps into [0, maxHealth], so nothing can ever overheal:
    //
    //   health = value;
    //   if (maxHealth < value) health = maxHealth;
    //   if (health < 0)        health = 0;
    //
    // Needed by the Absorb spell (ItemExecutable::statusEffect()'s
    // kEffectAbsorb), whose whole point is to *raise* the caster's health,
    // and the script-facing SetHealth handler now routes through it too --
    // it used to assign straight through, which let a script hand the
    // player more health than their maximum.
    void SetHealth(int value);

    // ---- M43: SpellActor (spell_actor.h) ----
    //
    // The player is the `vtable+0xcc` side of FUN_1002fd30, with its stats
    // block at actor+0x3ac. Being a SpellActor is what makes the player a
    // legal *target*: 32 shipped creature scripts call AddSpell, and every
    // one of those spells lands here.
    //
    // The stats block is the same class the creatures use, so a poisoned,
    // blinded, drained or burning player is modelled by the same code that
    // already modelled a poisoned creature -- which is exactly the sharing
    // the real engine has. attack()/defense()/armorRating() below fold in
    // any live modifier the way MonsterExecutable's already did.
    bool isPlayerActor() const override { return true; }
    bool isMonsterActor() const override { return false; }
    ActorStats& actorStats() override { return m_Stats; }
    int actorLevel() const override { return m_Level; }
    int actorHealth() const override { return m_Health; }
    void SetActorHealth(int value) override { SetHealth(value); }
    void ApplyActorDamage(int amount) override { ApplyDamage(amount); }
    bool actorAlive() const override { return m_Health > 0; }
    // M48: the two pools the real cast reads and writes (spell_cast.h).
    // Both setters are the real clamping ones -- FUN_1004bb20 against
    // maxMagicka and FUN_1004bb54 against maxFatigue, each with a floor of
    // zero, which is what makes "spend more than you have" simply empty the
    // pool rather than go negative.
    int actorMagicka() const override { return m_Magicka; }
    int actorMaxMagicka() const override { return m_MaxMagicka; }
    void SetActorMagicka(int value) override {
        m_Magicka = (std::min)((std::max)(0, value), m_MaxMagicka);
    }
    int actorFatigue() const override { return m_Fatigue; }
    int actorMaxFatigue() const override { return m_MaxFatigue; }
    void SetActorFatigue(int value) override {
        m_Fatigue = (std::min)((std::max)(0, value), m_MaxFatigue);
    }
    // FUN_1003e6f4. The real walk covers both equipped slots and eight
    // more; this port checks the two hands, which is where a real
    // discount item would have to be. No shipped entity has typeId 808, so
    // this never fires -- see spell_actor.h.
    bool actorHasSpellCostDiscount() const override;

    // The effective values, with any active timed status modifier folded
    // in -- the same treatment MonsterExecutable::attack()/defense()/
    // armorValue() already give a creature, now that a creature can cast
    // Drain, Weakness, FeebleBlade, Blind, Disease and HarmArmor at the
    // player. Clamped at 0.
    int attack() const {
        return (std::max)(0, m_BaseAttack + m_Stats.statModifier(ActorStats::kStatAttack));
    }
    int defense() const {
        return (std::max)(0, m_BaseDefense + m_Stats.statModifier(ActorStats::kStatDefense));
    }
    // M49: SpellActor's ranged-combat ratings, which an arrow's impact
    // resolves through -- the same two numbers melee already uses.
    int actorAttackRating() const override { return attack(); }
    int actorDefenseRating() const override { return defense(); }

    // The stats block's own per-frame tick -- the poison/burn damage
    // channels and every timed expiry. main.cpp calls this once per game
    // tick with the same kAiFrameDeltaUnits every creature uses.
    void TickStatusEffects(int deltaUnits);

    bool paralyzed() const { return m_Stats.paralyzed(); }
    bool blinded() const { return m_Stats.blinded(); }
    bool poisoned() const { return m_Stats.poisoned(); }
    bool burning() const { return m_Stats.burning(); }

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

    // ---- M50: character.dat ----
    //
    // The real save's player record (`FUN_10043308`, see
    // assets/save_records.h) is one `SavedEntity` of kind Player: the
    // quest arrays and the character name, then the stats block, then the
    // whole Character/InventoryHolder/Spellbook/Drawable/Entity chain,
    // then the tail with class/race/portrait and the equipment slots.
    //
    // These two fill in and read back the parts this port actually
    // models. Every field the port has no counterpart for is left at the
    // value it was constructed with -- zero for a fresh record, and the
    // loaded value for a record read off disk -- rather than being
    // invented, so a round trip through the port never fabricates state
    // it does not understand.
    //
    // `levelName` is the field the real loader uses to decide *which*
    // level to load (SavedCharacter::levelName), so it belongs to the
    // caller, not to the player object.
    // Every inventory child this port writes uses this one record class,
    // and a loader must resolve it back the same way -- the format itself
    // does not say (see player_save.cpp's own note on why).
    static constexpr sk::SavedEntityKind kInventoryChildKind = sk::SavedEntityKind::Stackable;
    sk::SavedEntity BuildSaveRecord(const std::string& levelName) const;
    // Restores the modelled fields. Inventory is rebuilt from each
    // child's script path (see BuildSaveRecord's own note on where that
    // is stored) through `stack`; a child whose script will not load is
    // skipped with a message rather than aborting the load.
    void ApplySaveRecord(const sk::SavedEntity& record, MenuStack& stack);

private:
    const sk::StringTable* m_Strings;
    sk::SoundArchive* m_Sounds;  // M27: see the constructor's own comment
    sk::AudioEngine* m_Audio;
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
    // M43 raised these from 10 to 50, to sit on the same scale as the rest
    // of this placeholder block (every attribute above is 50) -- and, more
    // to the point, on the same scale as the creature side, which stopped
    // being a placeholder this milestone. SetMob's recovered stat template
    // gives the very first rat in the game a defense of 62, and the
    // recovered melee gate is a ratio, `attack * 256 / (attack + defense)`:
    // at 10 the player would have hit that rat 14% of the time. Still a
    // documented placeholder, not a recovered value -- the real numbers
    // come from a character-creation stat system this port does not have --
    // but a placeholder that is at least the right order of magnitude for
    // the formula now consuming it.
    int m_BaseDefense = 50, m_BaseAttack = 50;
    // M37: the two *base* stats the real spell to-hit / resistance
    // formulas are built from (`+0x04` and `+0x06` of the stats block) --
    // what statsscreen.s shows is the derived value, computed in
    // spellToHit()/spellResistance() from these plus willpower, not a
    // stored number. Same fixed-placeholder footing as the rest of the
    // block (see the class comment); the *formulas* are recovered.
    int m_Spellcast = 50, m_MagicResistance = 0;
    // M43: the player's own stats block (actor+0x3ac) -- see actor_stats.h.
    ActorStats m_Stats;

    std::vector<std::unique_ptr<ItemExecutable>> m_Inventory;
    ItemExecutable* m_LeftItem = nullptr;
    ItemExecutable* m_RightItem = nullptr;
    // M19: see TakePendingPickupItem()'s comment.
    skiExecutable* m_PendingPickupItem = nullptr;

    // M23: see setValue()/getValue()'s comment above.
    std::map<std::string, skRValue> m_Fields;

    // M17: quest state -- see questAssigned()/questSolved()/
    // questCompleted()/monstersKilled()'s comments above.
    std::set<int> m_QuestAssigned;
    std::set<int> m_QuestSolved;
    std::set<int> m_QuestCompleted;
    std::map<int, int> m_MonstersKilled;
};

}  // namespace sk_bindings
