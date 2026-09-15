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
// Stat block values *were* fixed, documented placeholders (strength=50
// etc.) because no character-creation stat-rolling system had been found:
// M5's race/class picker didn't feed into them. M64 found the system --
// `UpdateAttributes` (character_progression.h), which `chooseportraitmenu
// .s` calls the moment the sex is chosen -- so a character created through
// the real menus now gets real per-race, per-sex attributes and real
// derived vitals. The 50s below are what a PlayerExecutable that never
// went through character creation still starts at; they are the
// placeholder, and they are now only the *initial* value of a field the
// game overwrites. GetArmorRating()/GetAttack() *do* reflect real state: they
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
#include "simkin_bindings/amulet_flags.h"
#include "simkin_bindings/entity_base_ref.h"
#include "simkin_bindings/store.h"
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

class PlayerExecutable : public NativeStubExecutable, public SpellActor,
                          public EntityBaseRef {
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

    // M56: the back-pointer the comment above says this class never had.
    // Three of the Player natives that audit found missing genuinely need
    // the rest of the game -- `OpenMenu()` needs the menu stack and
    // `GiveItem()` needs LevelExecutable's entities.txt-driven item
    // factory -- and there is no way to answer either from player state
    // alone. MenuStack, which owns this object, calls this from its own
    // constructor body; a PlayerExecutable built standalone (every test
    // that doesn't want a whole game) leaves it null and those two
    // handlers soft-fail exactly as they did before, rather than
    // pretending to work.
    void AttachStack(MenuStack& stack) { m_Stack = &stack; }
    // M75: null until AttachStack() -- every caller has to handle that,
    // the same way this class's own natives already do. DoorExecutable
    // reaches the menu stack through here rather than holding its own
    // reference, because a door is constructed with a PlayerExecutable&
    // and nothing else and that shape is load-bearing in five call sites.
    MenuStack* stack() const { return m_Stack; }

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

    // M56: `FindInventory(id)`/`HasItem(id)`/`CountInventory(id)` all match
    // on the item script's own `SetID()` tag, not its display name -- the
    // real Actor handler walks the inventory chain comparing `item+0xcb`.
    // Returns the first live match, or null.
    ItemExecutable* FindInventoryById(const std::string& id) const;

    // M56: the four-bit key-item word (see amulet_flags.h). Public because
    // the save layer and the smoke test both need to see it; maintained
    // by AddItem()/RemoveItem() rather than by any caller.
    const KeyItemFlags& keyItemFlags() const { return m_KeyItemFlags; }
    KeyItemFlags& keyItemFlags() { return m_KeyItemFlags; }

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
    // nothing visibly changes.
    //
    // M74 corrects two things. The hand is the item's **own** preferred
    // slot (`+0x1c0`, ItemExecutable::equipSlot()), not "whichever one is
    // free" -- which is what lets a spell equip at all, since a spell is a
    // right-hand item exactly like a sword. And the "3" return is the
    // *class* gate (IsItemEnabledFor below), never the item's type; this
    // port answered 3 for anything that was not armour or a weapon, which
    // is why the Blaze spell stayed unequippable even once it was on the
    // right page. The codes are now the real ones -- **1 = ok**, 2 = the
    // hand queue is full (unreachable here), 3 = refused, and 0 is never
    // returned -- rather than the 0-for-success this port had used.
    // `inventory.s` only ever tests for 2 and 3, so the change is
    // invisible to every shipped script.
    int UpdateEquipStatus(ItemExecutable* item, bool equipping);

    // ---- M74: `FUN_1001f82c` -- "may this character use this item?" ----
    //
    // The one gate in front of every equip and every auto-equip, and what
    // `IsItemEnabledFor(item)` answers for a script. It is a switch on the
    // item's type against the player's class row (character_progression.h,
    // whose three otherwise-unexplained `field0`/`field2`/`field4` words
    // turn out to be exactly the three masks it tests):
    //
    //   * **Spell** (type 2). A scroll is readable by anyone. Otherwise the
    //     class must have magic *and*, if the spell set a `RestrictUse`
    //     mask, that mask must name the class (`2 << classId`).
    //   * **Weapon** (type 1). Unrestricted when the class's own
    //     `field2` is 2 -- five of the nine classes -- and otherwise the
    //     weapon's `SetWeaponType` bit must be in it. Two weapon bits are
    //     exempt for any magic class: `WR_EnchantedBlade` (0x400) and
    //     0x800.
    //   * **Armor** (type 3). Armour type 7 is always allowed. A shield
    //     (the separate category-15 class, whose `vtable+0x180` returns 1
    //     where ordinary armour's returns 0) is tested against `field4`;
    //     everything else is `field0 & SetArmorConstraint`.
    //   * Misc and consumables have no arm, so they are always allowed.
    //
    // Returns true when nothing objects, which is also what an unknown
    // class id gets -- the real lookup returns a null row for one, and the
    // shipped code then reads through it rather than refusing.
    bool IsItemEnabledFor(const ItemExecutable& item) const;

    // ---- M59: the merchant the player is currently trading with ----
    //
    // `player+0xf84`, and it is a pointer straight into a creature
    // (`SetMerchant(self)` stores `monster+0x330`), not a copy. Null
    // outside a shop, and every buy/sell native answers with nothing at
    // all while it is -- not zero, *nothing*: the real handlers
    // `return 1` without writing a return value, so the script's variable
    // keeps whatever it already held. Reproduced.
    void SetMerchant(Store* store) { m_Merchant = store; }
    Store* merchant() const { return m_Merchant; }

    // ---- M95: the GameState odds and ends ----
    //
    // `DropGold(amount)`, case 0 -- the whole of it:
    //
    //     if (amount < gold) {
    //         gold -= amount;
    //         item = CreateEntity(0x34 /* gold.s */);
    //         item->owner    /* +0x170 */ = this;
    //         item->quantity /* +0x1c4 */ = amount;
    //         DropObject(item, 1);          // FUN_1002c3a8, M81/M93
    //     }
    //
    // The comparison is **strict**: you can never drop your last coin, and
    // `dropgoldmenu.s` is written around exactly that -- it offers 50 only
    // when `GetGold() > 50`, and `charactermanager.s` enables the row only
    // above 25. The dropped purse is a real `Loot_Dropped` bag at your feet,
    // the same one M93's inventory drop makes, and picking the coins back up
    // folds them into the purse through M82's template-52 test. Returns
    // false when the gate refused (nothing changes).
    bool DropGold(int amount);

    // `VisitStore(category)`, case 0x1a -- the cheat menu's **"God Vendor"**
    // (the engine's own debug line is "God Vendor contains %d items").
    // `player+0xf88` is a second store the player owns; the case frees any
    // previous one, builds a fresh one, and walks every entities.txt typeId
    // below 8000 whose category matches (a spell visit, 5, also takes the
    // scrolls, 14), stocking 99 of each that `products.dat` knows. Every line
    // it stocks gets **price 0** -- written onto the catalogue record itself,
    // which is shared (store.h), so those items stay free at every merchant
    // stocked afterwards in the session. Then `player+0xf84 = f88` and the
    // store screen opens on its buy page, exactly as `BuyFromMerchant` does.
    void VisitStore(int category);
    const Store* godVendor() const { return m_GodVendor.get(); }

    // `EnableCoords()`, case 0x18: `player+0xfc0 ^= 1`. While it is set the
    // in-game HUD draws the player's raw x/y (`"%d %d"`, `FUN_1002c104`).
    bool coordsEnabled() const { return m_CoordsEnabled; }

    // `SetGhost` / `IsGhost`, cases 0x1c / 0x1b: `player+0x10a1`. Read in
    // exactly two places (the split `#0x1080 + #0x21` is why only one shows
    // up as a Ghidra xref): the player's entity push-out slot returns before
    // doing anything, and the actor move function takes a separate arm that
    // never tests a wall. See ghost_mode.h for what that arm does to speed.
    bool ghost() const { return m_Ghost; }

    // FUN_1003e030. Returns the code buysell.s branches on:
    //   0 not enough gold (or no store / no such product / stock gone)
    //   1 bought
    //   2 the merchant does not have that many
    //   3 no room in the inventory (the cap is 100 items)
    int BuyProduct(const std::string& productName, int count);
    // FUN_1003f130 case 0x3b. Returns the number sold, 0 if the merchant
    // will not take it.
    int SellItemToMerchant(ItemExecutable* item, int count);
    // Case 0x3c: the same, for every copy of that item the player owns.
    int SellAllOfItem(ItemExecutable* item);
    // How many items the inventory holds, against FUN_1003e030's cap.
    int inventoryCount() const { return static_cast<int>(m_Inventory.size()); }
    // `player+0x3e4` -- the stats block's Gold at +0x38. Every purchase and
    // sale moves this one field.
    int gold() const { return m_Gold; }
    // M60: player+0xf38, the index the products.dat class-flag array is
    // read with (ProductRecord::enabledForClass) -- both the store
    // table's name tint and the cell's own IsItemEnabledFor() use it.
    int characterClass() const { return m_CharacterClass; }

    // M62: the player is an actor, so a scripted `SetPosition` snaps them
    // to the surface of the tile they land on. See
    // EntityBaseRef's header for the `vtable[0xc8]` predicate this is.
    bool isActorForPositioning() const override { return true; }

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
    // M98: the attacker and the site's p5 (`ranged`) feed FUN_10049e78's
    // terms -- the Knight's halving, a creature's Strength (always 0), the
    // snowray gate. See stats_damage.h.
    void ApplyDamage(int amount, SpellActor* attacker = nullptr, bool ranged = false);

    // M86: the real `+0x17c` hit timer -- see ApplyDamage's own comment
    // and main.cpp's RenderHud. Counted down by the HUD draw, exactly
    // where `FUN_1002ae88` counts it down.
    static constexpr int kHurtTimerUnits = 0x40;
    int hurtTimer() const { return m_HurtTimer; }
    void TickHurtTimer(int deltaUnits) {
        if (m_HurtTimer > 0) m_HurtTimer -= deltaUnits;
        if (m_HurtTimer < 0) m_HurtTimer = 0;
    }

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
    // The other seven attributes and the race, exposed the same way
    // willpower() already is -- these return exactly what the matching
    // GetStrength/GetAgility/.../GetRace natives return. Read-only; every
    // write still goes through the real setters and UpdateAttributes.
    int strength() const { return m_Strength; }
    int intelligence() const { return m_Intelligence; }
    int agility() const { return m_Agility; }

    // ---- M94: the two trap saving throws (GameState 0x2f / 0x30) ----
    //
    // `FUN_1003e438` and the dispatcher's own case 0x30. M38 recorded both
    // as "not reproduced ... this port has no stance model and no
    // equipped-effect array"; M56/M60/M64 have since given it both, so
    // here they are.
    //
    //     bool CanDisarmTrap(resist) {
    //         int bonus = 0;
    //         if (class == 8)              bonus  = specialAbility;
    //         if (stats.periodic2Kind == 3) bonus += 2;
    //         if (holds(618))               bonus += 2;
    //         if (holds(4500))              bonus += (class == 4 || class == 8) ? 8 : 3;
    //         int skill     = agility / 5 + bonus;
    //         int effective = (class == 4 || class == 8) ? skill : skill / 2;
    //         int roll = Random(0, 0x100); if (roll > 0xe5) roll = 0xe6;
    //         int ratio = (effective << 8) / (skill + resist);
    //         return !(level < resist / 2 || ratio < 0x41 || ratio < roll);
    //     }
    //
    // The two template ids are real items and they read exactly right:
    // **618** is `armor/Bandit_Gloves.s` and **4500** is
    // `misc/silver_picks.s`, and the picks are worth 8 instead of 3 to a
    // Thief (class 8) or a Nightblade (class 4) -- who are also the only
    // two classes that do not have their skill halved. `holds()` is the
    // engine's own three-place search: the left hand, the right hand, and
    // the eight equipment slots at `player+0xf8c`.
    //
    // `stats.periodic2Kind == 3` is transcribed and can never fire: the
    // only kinds anything in the binary ever arms are 4, 6, 7 and 8
    // (actor_stats.h's PeriodicKind), so 3 is a leftover. Kept because it
    // is in the formula, marked because it is dead.
    //
    // `level < resist / 2` is a hard gate ahead of the roll -- every
    // shipped door declares `resistDisarm[5]`, so you must be level 2 to
    // pick any lock in the game at all.
    bool CanDisarmTrap(int resistDisarm) const;

    // Case 0x30, the cheaper roll, and the only shipped caller is
    // `traploot_gold.s`:
    //
    //     n = min(arg, 0x12);
    //     v = Random(1, luck);
    //     if (class == 8)               v += specialAbility;
    //     v /= 5;
    //     if (stats.periodic2Kind == 3) v += 2;
    //     return n < v;
    bool CanAvoidTrap(int chance) const;
    int speed() const { return m_Speed; }
    int endurance() const { return m_Endurance; }
    int personality() const { return m_Personality; }
    int luck() const { return m_Luck; }
    int race() const { return m_Race; }
    int spellToHit() const;
    int spellResistance() const;

    // ---- M64: the level-up screen (`levelup.s`) ----
    //
    // `GetLevelUpPoints` / `DecreaseLevelUpPoints` (player +0xf44). The
    // screen quits immediately when this is below 1, and every one of its
    // eight buttons spends one point for five points of one attribute.
    // The real decrement has **no floor** -- `*(int*)(p+0xf44) -= 1`, and
    // nothing else clamps it -- so a script that spends a point it does
    // not have leaves the counter negative. Reproduced.
    int levelUpPoints() const { return m_LevelUpPoints; }
    void DecreaseLevelUpPoints() { m_LevelUpPoints -= 1; }

    // Player dispatcher case 4, `LevelUp`: bump the level, then take the
    // level-gained hook below. `cheatmenu.s` is its only shipped caller.
    void LevelUp();

    // FUN_10044618 -- the stats block's `vtable[0x24]` "a level was
    // gained" slot, which the player overrides with this. Awards the
    // point, plays the power-up sting (sound slot 87, `pl_cast_powerup
    // .wav`) and re-derives the character. It is what both the `LevelUp`
    // native and the experience threshold below go through.
    void GrantLevelUpPoint();

    // FUN_1001fc24, the whole of it. Two quite different jobs behind one
    // name, selected by the character's *current level*:
    //
    //   level == 1  seed all eight attributes from race and sex, then
    //               refill health, fatigue and magicka to their new
    //               maxima. This is character creation --
    //               `chooseportraitmenu.s` calls it the moment the
    //               portrait (and therefore the sex) is picked.
    //   level > 1   raise the class and race ability ranks by one, rescale
    //               the class magicka bonus and, for a High Elf, add five
    //               to the racial one; re-derive; and refill health and
    //               magicka only if `restoreVitals`.
    //
    // Both paths force max magicka to zero for a class with no magic.
    // `levelup.s`'s back key passes false, `chooseportraitmenu.s` true.
    void UpdateAttributes(bool restoreVitals);

    // FUN_1004a104. The experience add is also the level-up trigger: if
    // the new total would pass the threshold the level goes up *first*,
    // by exactly one, and then the experience is banked. The argument
    // reaches the engine through a 16-bit conversion, so a huge award
    // wraps -- reproduced, since `StatModXP` is script-facing.
    void AddExperience(int amount);
    // FUN_1004a020: the same threshold, minus the experience already
    // held. Negative once the threshold is passed and before the next
    // AddExperience re-levels, which the real one is too.
    int experienceToNextLevel() const;

    // Player +0xfb0 / +0xfb4, the two ranks `UpdateAttributes` raises
    // together on every level. Both are script-visible in their own right
    // (`SetSpecialAbility` / `SetRaceAbility`, which shrines and trainers
    // use to grant a rank outright) and both start at **1**, not 0.
    // The class rank also scales the magicka bonus and is the Thief's
    // trap-avoidance bonus; the race rank is the Argonian's haggling one.
    int specialAbility() const { return m_SpecialAbility; }
    int raceAbility() const { return m_RaceAbility; }

    // The two magicka bonus words the derived-stat recompute adds to
    // intelligence (spell_actor.h). Neither has a script binding of its
    // own -- both are written only by UpdateAttributes.
    bool actorClassHasMagic() const override;
    int actorMagickaBonus() const override { return m_MagickaBonusRace + m_MagickaBonusClass; }

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
    // M58: FUN_1004ad40's field map -- see spell_actor.h. The player has
    // every field the stats block has, which is why this is the long one.
    int* EffectStatSlot(int stat) override;
    int actorLevel() const override { return m_Level; }
    int actorHealth() const override { return m_Health; }
    void SetActorHealth(int value) override { SetHealth(value); }
    // M97: the player's death slot is its own routine, not FUN_10083c04, and
    // pays nobody -- the attacker matters to a player victim only through
    // FUN_10049e78's damage terms (M98).
    void ApplyActorDamage(int amount, SpellActor* attacker, bool ranged) override {
        ApplyDamage(amount, attacker, ranged);
    }
    // M98: `+0xf38` and `+0xfb0`, for the Knight and Assassin terms.
    int actorCharacterClass() const override { return m_CharacterClass; }
    int actorSpecialAbility() const override { return m_SpecialAbility; }
    void AddActorExperience(int amount) override { AddExperience(amount); }
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
    // M58: no longer `base + modifier`. The effect applier writes the field
    // itself, exactly as the engine's does, so there is one number here and
    // a script's own GetAttack()/GetDefense() see the same one. Still
    // clamped at 0 -- the real setters are signed shorts and a debuff can
    // take either below zero, which the damage roll has no meaning for.
    int attack() const { return (std::max)(0, m_BaseAttack); }
    int defense() const { return (std::max)(0, m_BaseDefense); }
    // M49: SpellActor's ranged-combat ratings, which an arrow's impact
    // resolves through -- the same two numbers melee already uses.
    int actorAttackRating() const override { return attack(); }
    int actorDefenseRating() const override { return defense(); }

    // The stats block's own per-frame tick -- the poison/burn damage
    // channels and every timed expiry. main.cpp calls this once per game
    // tick with the same kAiFrameDeltaUnits every creature uses.
    void TickStatusEffects(int deltaUnits);

    // M73: `FUN_10049b64` -- the slow refill of all three pools, and the
    // other half of the player tick `FUN_10045294` runs. It sits directly
    // after TickStatusEffects there, and main.cpp calls it in the same
    // order for the same reason: a poison tick that lands this frame should
    // be what regeneration then works against.
    //
    // Player-only, and that is the engine's own arrangement rather than a
    // simplification -- see vitals.h.
    void TickVitalRegeneration(int deltaUnits);

    // `FUN_10045474`: is an entity of this typeId equipped? The real walk
    // covers the two hand slots (`stats+0x48`/`+0x4c`) and the eight
    // equipment slots at `player+0xf8c`; this port has the two hands and a
    // per-item `equipped()` flag instead of numbered armour slots, so it
    // checks those. Same set for every case that matters -- an item has to
    // be worn or held to be found either way.
    bool HasEquippedTemplate(int typeId) const;

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
    // M51: SetSex's own field (player+0xfac, M50). The native jump picks
    // pl_jump_female.wav for 0 and pl_jump_male.wav otherwise, which is
    // what fixes the polarity.
    int sex() const { return m_Sex; }

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

    // M93: the same channel in the other direction -- items the inventory
    // screen dropped this tick, waiting to become a loot bag on the floor.
    // PurgeRemovedItems() fills it (a dropped item is moved out of
    // m_Inventory rather than destroyed) and main.cpp drains it, for the
    // same reason the pickup above is deferred: the drop happens inside a
    // live `inventory.s` call frame that is still holding references, and
    // the object has to outlive it.
    // Out of line, like every other member here that touches a
    // unique_ptr<ItemExecutable>: the class only forward-declares it.
    std::vector<std::unique_ptr<ItemExecutable>> TakePendingDrops();

    // The other way in: one unit split off a stack, which was never in
    // m_Inventory in the first place and so never passes through
    // PurgeRemovedItems(). See TableExecutable::DropRow().
    void QueueDrop(std::unique_ptr<ItemExecutable> item);

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

protected:
    // M92: PlaySound is a base binding. The player is the busiest receiver
    // in the corpus (`GetPlayer().PlaySound(63)` on every door), and
    // `GetOwner().PlaySound(id)` from an item's script lands here too --
    // GetOwner() returns the player object directly.
    sk::SoundArchive* entitySounds() const override { return m_Sounds; }
    sk::AudioEngine* entityAudio() const override { return m_Audio; }

private:
    const sk::StringTable* m_Strings;
    sk::SoundArchive* m_Sounds;  // M27: see the constructor's own comment
    sk::AudioEngine* m_Audio;
    std::string m_Name;
    std::string m_CharNameBuffer;
    int m_Sex = 0;
    // M74: the real constructor's own defaults. `FUN_1003d670` -- the
    // player class's constructor, the same one M64 read the two ability
    // ranks out of -- writes `+0xf3c = 5` and `+0xf38 = 2` before anything
    // else touches them, i.e. a player object that has never been through
    // character creation is a **Nord Battlemage**, not the 0/0 Argonian
    // Assassin this port had been defaulting to. That stopped being
    // cosmetic in M74, when the class row became the input to every
    // equip check.
    int m_Race = 5;      // +0xf3c, kRaceNord
    int m_PortraitId = 0;
    int m_Gold = 0;
    int m_Temp = 0;
    // M56: player+0xf38, the field GetCharacter() reads -- a separate
    // field from m_Temp in the real engine, which is why sharing the
    // scratch field would have been wrong here.
    int m_CharacterClass = 2;  // +0xf38, kClassBattlemage -- see m_Race
    bool m_HasCreatedCharacter = false;

    // Vitals (M10) -- fixed baseline defaults, see class comment.
    int m_Health = 100, m_MaxHealth = 100;
    int m_Magicka = 50, m_MaxMagicka = 50;
    int m_HurtTimer = 0;  // M86
    int m_Fatigue = 100, m_MaxFatigue = 100;
    int m_Level = 1;
    int m_Experience = 0;
    // M64: player +0xf44 / +0xfb0 / +0xfb4 / +0xfb8 / +0xfba. The three
    // non-zero defaults are the real ones -- FUN_1003d670 (the new-game
    // reset) writes `fb0 = 1; fb4 = 1;` next to `f44 = 0`, so a fresh
    // character already holds rank 1 of both abilities.
    int m_LevelUpPoints = 0;    // +0xf44
    int m_SpecialAbility = 1;   // +0xfb0, the class ability rank
    int m_RaceAbility = 1;      // +0xfb4, the race ability rank
    int m_MagickaBonusRace = 0;   // +0xfb8, the High Elf's +5 a level
    int m_MagickaBonusClass = 0;  // +0xfba, intelligence x rank x class

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
    // M58: the last four stats-block fields the effect table can reach that
    // this port had no storage for. `m_ArmorValue` is stats+0x0c, the
    // player's *own* armour number -- separate from the equipped-item sum
    // GetArmorRating() walks, and the one AddEffect(ArmorValue, ...) and
    // every HarmArmor move; armorRating() adds the two. `m_HealthBonus` is
    // stats+0x12, which only the derived-stat recompute reads.
    int m_ArmorValue = 0;
    int m_HealthBonus = 0;
    int m_DamageMin = 0, m_DamageMax = 0;
    int m_ExpWorth = 0;
    // M43: the player's own stats block (actor+0x3ac) -- see actor_stats.h.
    ActorStats m_Stats;

    std::vector<std::unique_ptr<ItemExecutable>> m_Inventory;
    // M56: registry+0x468/+0x478 -- see amulet_flags.h.
    KeyItemFlags m_KeyItemFlags;
    // M59: player+0xf84 -- see SetMerchant() above.
    Store* m_Merchant = nullptr;
    // M95: player+0xf88, the God Vendor -- see VisitStore().
    std::unique_ptr<Store> m_GodVendor;
    bool m_CoordsEnabled = false;  // M95: +0xfc0, EnableCoords
    bool m_Ghost = false;          // M95: +0x10a1, SetGhost
    // M56: see AttachStack(). Null in every standalone construction.
    MenuStack* m_Stack = nullptr;
    ItemExecutable* m_LeftItem = nullptr;
    ItemExecutable* m_RightItem = nullptr;
    // M19: see TakePendingPickupItem()'s comment.
    skiExecutable* m_PendingPickupItem = nullptr;
    // M93: see TakePendingDrops().
    std::vector<std::unique_ptr<ItemExecutable>> m_PendingDrops;

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
