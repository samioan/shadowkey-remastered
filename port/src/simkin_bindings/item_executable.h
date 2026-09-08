#pragma once

#include <map>
#include <string>

// M10: native binding for a real armor/weapon/item/consumable .s script
// object -- e.g. armor/chain_coif.s, weapons/club.s, items/bread.s. Same
// role TestArmorExecutable played for M2's throwaway round-trip proof, but
// now backed by real, readable-back state (SIMKIN_NATIVE_API.md's Item
// (0x14d74), Weapon (0x14d44), and Armor (0x14e04) classes -- this port
// doesn't reproduce the real trie-class split, one object answers whatever
// subset of setters/getters a given script's Init() and its owner
// (inventory.s/charactermanager.s) actually use).
//
// GetItemType() isn't stored as a fixed field set by the loader -- it's
// *inferred* from which category-specific setter actually fired (armor's
// SetArmorValue/SetArmorType/SetArmorConstraint vs. weapon's
// SetDamageMin/SetDamageMax/SetWeaponType/SetWeaponSprite/SetRange vs.
// item's SetUsable(true)), matching the corpus's own directory-per-
// category convention without needing to know a script's file path.
// Defaults to kItemTypeMisc if none of those fire.

#include <memory>
#include <string>
#include <vector>

#include "simkin_bindings/entity_position_ref.h"
#include "simkin_bindings/native_stub_executable.h"
#include "simkin_bindings/script_delay.h"
#include "simkin_bindings/spell_actor.h"
#include "skScriptedExecutable.h"

class skInterpreter;

namespace sk {
class StringTable;
}

namespace sk_bindings {

class MenuStack;

class ItemExecutable : public skScriptedExecutable, public EntityPositionRef {
public:
    // M21: `stack` -- replaces M19's separate `strings`/`PlayerExecutable&`
    // parameters (a real script now also needs `GetPlayer()` *and*
    // `Level.CreateEntity`'s result routed through `AddObject()`, i.e. both
    // MenuStack::player() and MenuStack::level() -- MenuStack itself
    // already owns both plus the real stringtable/scriptRoot/interpreter,
    // same single-reference shape MenuExecutable's own constructor already
    // takes, so this is one reference instead of accumulating a third/
    // fourth separate one). May reference a MenuStack constructed with a
    // null stringtable (some smoke tests) -- GetName()/GetItemDescription()
    // fall back to a placeholder rather than crash.
    ItemExecutable(const skString& filename, skExecutableContext& ctxt, MenuStack& stack);

    // The script path this item was loaded from, lowercased -- see
    // statusEffect(). Kept because skScriptedExecutable doesn't expose it.
    const std::string& scriptPath() const { return m_ScriptPath; }

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

    // M38: keep an object assigned to a *pre-declared* script field --
    // see native_binding_common.h's StoreScriptObjectField() for the
    // vendored-Simkin behaviour this works around and the real script
    // (lothcav.s) that exposed it.
    bool setValue(const skString& fieldName, const skString& attribute,
                  const skRValue& value) override;
    bool getValue(const skString& fieldName, const skString& attribute,
                  skRValue& value) override;


    // M21: skTreeNodeObject::strValue() (skScriptedExecutable's own base)
    // defaults to the TreeNode's own root data -- empty for an ordinary
    // loaded .s script, since nothing ever assigns it one. That's a real
    // bug source: `skRValue::operator==`'s T_Object-vs-T_String branch
    // compares a found object's strValue() against a blank string, so a
    // real, live ItemExecutable could accidentally compare equal to the
    // blank `null` global (game_constants.cpp) -- caught for real by this
    // session's own lootmenu.s test (`if (Opener.GetFirst() = null)`
    // wrongly took its "empty bag" branch for a bag that had one real
    // item in it). A guaranteed-non-blank override fixes it at the
    // source, for every future comparison, not just this one call site.
    skString strValue() const override { return skString("Item"); }

    // M19: runs the real script's OnUse() handler -- same host-triggered-
    // call convention (placeholder "(s)" arg, catch+log
    // skParseException/skRuntimeException) DoorExecutable/
    // MonsterExecutable's own InvokeOnUse() already establish, for a real
    // world pickup's Action::Use (main.cpp).
    // M35: returns whether the script actually defined an OnUse handler.
    // Most world items do not -- every weapon (83 scripts), every armour
    // piece (89), every shield (10), every spell scroll (7) and half the
    // spells (14) have Init/HitTarget only -- and for those the engine
    // runs its own native default instead (see main.cpp's Use handling).
    bool InvokeOnUse();

    // M22: runs the real script's HitTarget(target) handler -- a real
    // spell's own damage/effect entry point, never called from any
    // script (native-triggered only, same "opaque native combat"
    // footing M12 already established for melee) -- main.cpp's tryAttack
    // calls this for whichever equipped item *isn't* a real weapon
    // (kItemTypeWeapon), so casting reuses the same UseLeftAction/
    // UseRightAction keys ranged weapons do (M20) rather than a new
    // input action. Harmless no-op for a non-spell item (nothing defines
    // HitTarget, soft-fails through skScriptedExecutable::method()'s own
    // "no such handler" fallback -- same TryInvoke()-style tolerance
    // MenuExecutable already relies on).
    void InvokeHitTarget(skiExecutable* target);

    // M32: which real status effect this item's script *is*.
    //
    // The real selector is decompiled: FUN_100458e4 switches on the spell
    // entity's own entities.txt typeId -- 4020 = spells\Fear.s applies AI
    // package 4 (flee) for `magnitude * 5`, 4025 = spells\Paralyze.s arms
    // the target's action lockout, and there are further branches for
    // Absorb/Blind/Drain/HarmArmor/IgniteFoe/Disease/Poison. That resolves
    // docs/PORT_ROADMAP.md's long-standing "no scripted table has been
    // found anywhere to decode DoAttackRoll's effect from" open item: the
    // table is entities.txt, keyed by typeId.
    //
    // This port loads a spell by script *path* rather than by typeId, but
    // that is the same relation -- entities.txt maps one to the other --
    // so the effect is resolved from the script filename, unless the
    // script set its own typeId with SetSpellType() (M37), which is both
    // more direct and the only way to tell apart the four typeIds that
    // share a script file (50/4006 blaze.s, 4012/4017 DoomHammer.s).
    enum StatusEffect {
        kEffectNone,
        kEffectFear,       // 4020 spells\Fear.s
        kEffectParalyze,   // 4025 spells\Paralyze.s
        kEffectPoison,     // 4034 spells\Poison.s
        kEffectDisease,    // 4033 spells\Disease.s
        kEffectDrain,      // 4018 spells\Drain.s
        kEffectBlind,      // 4010 spells\Blind.s
        kEffectHarmArmor,  // 4023 spells\HarmArmor.s
        kEffectAbsorb,     // 4009 spells\Absorb.s      (M34)
        kEffectIgniteFoe,  // 4024 spells\IgniteFoe.s   (M34)
        // ---- M37: the dispatcher's four remaining branches, all
        // damage-only (they set the shared min/max pair and then fall off
        // the end of the status switch without applying anything further).
        // Naming each one took entities.txt: the typeIds are what
        // FUN_100458e4 switches on, and entities.txt is what maps them to
        // a shipped script.
        kEffectBlaze,          // 50   blaze.s
        kEffectBlazeGreater,   // 4006 blaze.s   (flat 45..50, no scaling)
        kEffectDeadToDust,     // 4002 spells\DeadToDust.s
        kEffectDoomHammer,     // 4012 + 4017 spells\DoomHammer.s
        kEffectDeathHowl,      // 4035 spells\DeathHowl.s
        // ---- M43: the last two status branches, both found by asking
        // which spells *creatures* cast. Neither is in the player's own
        // spell list, which is why nine passes over this dispatcher went by
        // without them: 4008 is cast only by tunnel_wight.s and 4021 only
        // by highwaymage.s / highwaymage_cskye.s / shadow_tentacle.s.
        kEffectWeakness,       // 4008 spells\Weakness.s
        kEffectFeebleBlade,    // 4021 spells\FeebleBlade.s
    };
    StatusEffect statusEffect() const;

    // M37: the spell entity's own entities.txt typeId, when the script set
    // one with SetSpellType() -- 0 when it did not (every plain spells\*.s
    // is loaded here by path, and only the scroll/unique wrappers such as
    // spells\IgniteScroll.s carry an explicit SetSpellType).
    int spellType() const { return m_SpellType; }

    // M37: the Spell class's own SetLevel/GetLevel (`spell+0x1d0`), and
    // SetScroll (`spell+0x1d4`). Together they are the *fallback* half of
    // the real magnitude rule in FUN_100458e4:
    //
    //   if (!scroll && (caster == null || casterIsCharacter))
    //        magnitude = casterLevel;      // stats+0x34
    //   else magnitude = spell->level;     // spell+0x1d0
    //
    // which is why the shipped scroll and "unique" spell items
    // (spells\IgniteScroll.s, spells\U_Blaze_lvl5.s, ...) are exactly the
    // scripts that call SetLevel and no plain spell does: a scroll's power
    // is fixed by the scroll, everything else scales with who cast it.
    int spellLevel() const { return m_SpellLevel; }
    bool scroll() const { return m_Scroll; }

    // M48: the Spell class's fourth field, `SetRefireRate` (`+0x1d2`, the
    // sibling of SetLevel's `+0x1d0` in the same dispatcher). It is the
    // per-spell cast cooldown FUN_10046680 gates on, and exactly one
    // shipped script sets it:
    //
    //     spells\Sanctuary.s:  SetRefireRate(768);  //3 secs
    //
    // whose own comment is an independent confirmation that engine
    // durations are `seconds * 0x100`. Every other spell leaves it 0, which
    // makes the gate inert -- so the "you can only cast so often" rule the
    // engine appears to have is really a Sanctuary-only rule. The one
    // runtime writer is the cast itself: AzraWrath sets 400 as it fires.
    int refireRate() const { return m_RefireRate; }
    void SetRefireRate(int units) { m_RefireRate = units; }

    // M48: the spell entity's real entities.txt typeId -- what
    // FUN_10046764 and FUN_100458e4 both switch on. Resolved exactly the
    // way statusEffect() already resolves it (a script's own SetSpellType,
    // else the typeId Level.CreateEntity() stamped on the object, else the
    // script path), but returning the id itself so the cast table can be
    // keyed on it. 0 when this item is not a spell at all.
    int spellTypeId() const;

    // M43: `spell+0x170` -- **who is casting this**. The real
    // status-effect dispatcher reads the caster off the spell itself, never
    // off a global "the player"; the Monster class's AddSpell binding sets
    // it with `FUN_1006d510(spell, monster)`, and the cast path refuses to
    // fire a spell with no owner at all (`FUN_1006d518(spell) != 0`).
    //
    // Null means "the player's", which is what every ItemExecutable in this
    // port that a creature did not explicitly claim actually is -- the
    // player's inventory owns them. DoAttackRoll falls back accordingly.
    void SetSpellOwner(SpellActor* owner) { m_SpellOwner = owner; }
    SpellActor* spellOwner() const { return m_SpellOwner; }

    // Host-side accessors -- used by MenuExecutable's inventory-table
    // population (DisplayWeaponsPage etc.) and PlayerExecutable's equip
    // logic, which need real values without going through a script call.
    int itemType() const { return m_ItemType; }
    std::string name() const;
    int icon() const { return m_Icon; }
    // M19: a world pickup's real SetUseText() (e.g. snowline/foxglove.s's
    // "Pick up"-style prompt) -- main.cpp's interact-prompt line reads this
    // the same way it already reads DoorExecutable/MonsterExecutable's.
    int useTextId() const { return m_UseTextId; }
    // M75: `entity+0xd8` -- whether this world placement offers a prompt
    // and answers Use at all. Written by the entities.txt category's
    // constructor default, by `SetUsable(b)`, and by `SetUseText(id)`.
    // See use_prompt.h.
    bool usable() const { return m_Usable; }
    int cost() const { return m_Cost; }
    // M59: SetMarketValue()'s stored number (item+0x1b8), which is what a
    // merchant pays for the item -- see PlayerExecutable::SellItem.
    int marketValue() const { return m_MarketValue; }
    int armorValue() const { return m_ArmorValue; }
    // M74: the three restriction inputs PlayerExecutable::IsItemEnabledFor()
    // tests, all of them fields real scripts have always been setting and
    // nothing had ever read back.
    //
    //   `SetArmorType(n)`       -> armor `+0x1d0`; 7 is the "no slot" type
    //                              GetArmorText has no string for, and is
    //                              never refused.
    //   `SetArmorConstraint(AR_*)` -> armor `+0x1d4`, an AR_ bit.
    //   `SetWeaponType(WR_*)`   -> weapon `+0x1d0`, a WR_ bit -- despite the
    //                              name it is the weapon *class*, which is
    //                              why it takes WR_Blunt and not a damage
    //                              type.
    int armorType() const { return m_ArmorType; }
    int armorConstraint() const { return m_ArmorConstraint; }
    int weaponType() const { return m_WeaponType; }
    // Shields are their own C++ class (entities.txt category 15), and the
    // only thing that separates them from ordinary armour is a vtable slot:
    // `+0x180` returns 1 for `FUN_1002e844`'s class and 0 for
    // `FUN_1002e954`'s. All ten shipped shields are category 15.
    bool isShield() const { return m_EntityCategory == 15; }
    int damageMin() const { return m_DamageMin; }
    int damageMax() const { return m_DamageMax; }
    bool equipped() const { return m_Equipped; }
    void SetEquipped(bool equipped) { m_Equipped = equipped; }
    // M25: a real weapon script's own SetWeaponSprite()/SetAnimationFrames()
    // -- stored since M10, never read back until now (main.cpp's real
    // first-person weapon-viewmodel rendering, see its own comment for the
    // real decompiled draw/animation state machine this drives, docs/
    // PORT_ROADMAP.md's M25 entry). -1/0 (never set -- every real spell
    // script, none of which call SetWeaponSprite) means "no viewmodel to
    // draw for this item."
    int weaponSprite() const { return m_WeaponSprite; }
    int animationFrames() const { return m_AnimationFrames; }
    // M20: a real weapon script's own SetRange() -- previously stored
    // (just to flag kItemTypeWeapon) but never read back by anything.
    // Corpus-verified bimodal: every melee weapon uses exactly 384, every
    // ranged one (bow/crossbow/thrown, see ranged() below) exactly 16384
    // -- no other value appears anywhere in the whole real weapon corpus.
    // main.cpp's tryAttack now uses this directly as the real per-weapon
    // attack range instead of a single hardcoded melee constant.
    int range() const { return m_Range; }
    // M20: true if the real script called SetBow/SetCrossbow/
    // SetThrowingWeapon(true) -- informational (main.cpp's actual attack-
    // range decision uses range() directly, which already captures the
    // same real melee-vs-ranged split on its own).
    bool ranged() const { return m_Launched || m_Thrown; }

    // M49: the real gate the player's attack routine branches on is
    // `weapon+0x1a7`, and the Weapon dispatcher (FUN_1006ca90 case 2) sets
    // that from **SetRange alone** -- `+0x1a0 = value; if (value > 0x400)
    // +0x1a7 = 1;`. None of SetBow/SetCrossbow/SetThrowingWeapon touches it.
    // So M20's "range() already captures the split on its own" turns out to
    // be literally how the engine decides, not just a convenient proxy; the
    // corpus agrees exactly (16 weapons at 16384, 65 at 384, and the 16 are
    // precisely the ones flagged bow/crossbow/thrown).
    bool usesRangedPath() const { return m_Range >= 0x401; }

    // M49: `+0x17a` (SetBow / SetCrossbow / SetIsLaunched) versus `+0x179`
    // (SetThrowingWeapon / SetIsThrown) -- two distinct bytes in the real
    // object, which this port used to collapse into one flag. The ranged
    // branch reads only the first, to choose the projectile's entities.txt
    // type: 599 (`!arrow`, models.txt 175 arrow.bin) when it is set, 598
    // (`!throwing`, models.txt 176 throw_dagger.bin) when it is not. The 7
    // shipped bows and crossbows and the 9 darts and throwing knives land on
    // either side of exactly that line.
    bool launched() const { return m_Launched; }
    bool thrown() const { return m_Thrown; }
    int projectileTypeId() const { return m_Launched ? 599 : 598; }

    // Deferred-removal flag: dropping/consuming an item (TableExecutable::
    // DropRow, OnUsedBy below) can happen mid-script-call, with a live
    // script-side reference to this object still in scope (e.g.
    // inventory.s's UseItem() calls RemoveRow() then immediately
    // OnUsedBy() on the same `inv` variable) -- actually erasing it from
    // PlayerExecutable's owning vector right then would free the object
    // out from under that reference. Marking it instead and letting
    // PlayerExecutable::PurgeRemovedItems() do the real erase once per
    // tick, after any in-flight script call chain has fully returned
    // (main.cpp), avoids that use-after-free entirely.
    bool markedForRemoval() const { return m_MarkedForRemoval; }
    void MarkForRemoval() { m_MarkedForRemoval = true; }

    // M21: real loot-bag support (loot_ratseye.s/loot_gold6-10.s etc. --
    // see docs/PORT_ROADMAP.md's M21 entry). A bag script's Init() calls
    // `Item = Level.CreateEntity(typeId); Item.SetQuantity(...);
    // AddObject(Item);` -- SetQuantity()'s real value (default 1, matching
    // lootmenu.s's own `if (Item.GetQuantity() != 1)` branch), and
    // AddObject()/GetFirst()/GetNext()/RemoveObject() implement the bag's
    // own Collection of contained ItemExecutables (`m_Contents`) that
    // lootmenu.s's real UpdateMenu()/SelectItem() walk.
    // M36: lootmenu.s's own emptied-container branch,
    //   `if ((GetOpener().GetFirst() = null) and (GetOpener().GetDestroy()))`
    // -- "should this container disappear once it has been emptied?".
    // Real callers set it only on *spawned* loot bags (monsters/arat.s's
    // `Loot.SetDestroy(true)`, fearfrst/goblin_hero.s,
    // crypt1/shadowkeygate.s); a placed chest never does, so a chest stays
    // in the world (just `SetUsable(false)`) while a dropped bag vanishes.
    bool destroyWhenEmpty() const { return m_DestroyWhenEmpty; }

    // M36: lootmenu.s's `Opener.GetTemplate() != 306` (which suppresses the
    // pickup sound for one specific container type). The real value is the
    // entities.txt typeId an object was created from -- known only when it
    // came through Level.CreateEntity(), so -1 ("not created from a
    // template") for anything loaded directly from a script path.
    int templateId() const { return m_TemplateId; }
    void SetTemplateId(int typeId) { m_TemplateId = typeId; }

    // ---- M74: the item type, from where the engine actually gets it ----
    //
    // `entity+0x16c` and `entity+0x1c0` are per-C++-class constants, and
    // the class is chosen by the `entities.txt` **category** column -- see
    // the table in game_constants.h. Every item the engine ever puts in an
    // inventory is built by that factory, so every one of them has a real
    // type; this port builds some items straight from a script path
    // instead (the starting kit, a loot bag's own script, a save's
    // inventory children), and those keep the M10 setter inference below
    // as a fallback.
    //
    // Call this with the descriptor's own category as soon as the object
    // exists. It wins over the inference permanently -- a spell script
    // that happens to call `SetRating` must not be re-typed by it.
    void SetEntityCategory(int category);
    int entityCategory() const { return m_EntityCategory; }
    // `entity+0x1c0`: kEquipSlotLeft / kEquipSlotRight / kEquipSlotNone.
    int equipSlot() const { return m_EquipSlot; }

    // `spell+0x1cc`, the bitmask `RestrictUse(...)` ORs its arguments into
    // -- one bit per character class, `2 << classId` (`FUN_10020904`).
    // `blaze.s`'s own `RestrictUse(2, 4, 6, 7)` is Battlemage, Nightblade,
    // Spellsword and Sorcerer, which is exactly the four classes the class
    // table marks `HasMagic`. Read by
    // PlayerExecutable::IsItemEnabledFor().
    int restrictUseMask() const { return m_RestrictUseMask; }

    // M39: a real script's own SetID() (e.g. "stooth") -- the key
    // PlayerExecutable::CountInventory() matches on.
    const std::string& id() const { return m_Id; }
    int quantity() const { return m_Quantity; }
    // M50: restoring a saved stack (SavedStackable/SavedWeapon's +0x1c4).
    void SetQuantity(int quantity) { m_Quantity = quantity; }
    const std::vector<std::unique_ptr<ItemExecutable>>& contents() const { return m_Contents; }

    // M22: a real spell script's own SetRating() (e.g. blaze.s's
    // SetRating(2)) -- confirmed NOT an exclusive spell-category signal
    // (misc/ring_of_fangs.s, a real armor-category ring, also calls it,
    // *after* its own SetArmorValue/SetArmorType -- deliberately not used
    // to infer itemType() the way SetDamageMin/SetArmorValue/SetUsable
    // already do, unlike every other category setter in this class).
    // Never read back by any real script either (no GetRating() call
    // anywhere in the corpus) -- host-side only, RollSpellDamage()'s
    // from-scratch fallback for spells outside the real dispatcher.
    //
    // M37 retired it as the status-effect magnitude, which is what it used
    // to stand in for. Reading the whole spells/ directory at once made
    // the substitution untenable: the values are a dense, near-sequential
    // run in alphabetical order (absorb 1, blind 3, bodytomind 5,
    // curedisease 6, curepoison 7, ... azrasustenance 28) with duplicates
    // across unrelated spells (bodytomind and disease are both 5, poison
    // and daedricweapon both 8), and absent entirely from the scroll and
    // u_*_lvlN.s variants -- an ordinal, not a power. The real magnitude
    // is the caster's level; see spellLevel() above.
    int rating() const { return m_Rating; }
    // M59: a bought item takes its rating from the products.dat row rather
    // than from its own script -- FUN_1003e030 calls the same setter the
    // SetRating binding does on every item it creates.
    void SetRating(int rating) { m_Rating = rating; }

    // M53: the entity script timer -- `Delay(seconds, tag)` arms it and
    // `DelayReached(tag)` is called back when it expires (script_delay.h).
    // Categories 3 and 8 are half of where the shipped `DelayReached`
    // scripts live: crypt2/controller.s (the Umbra arrival sequence),
    // crypt1's nine sarcophagi, crypt2/sarc_entity.s (11 placements) and
    // drgnfld's two loot chests.
    ScriptDelay& delay() { return m_Delay; }
    const ScriptDelay& delay() const { return m_Delay; }

private:
    // M74: the M10 setter inference, demoted to a fallback. It runs only
    // for an object built straight from a script path, where no
    // entities.txt category was ever available -- once SetEntityCategory()
    // has spoken, the type is the engine's and nothing re-derives it.
    //
    // It sets the preferred hand too, from the same type-to-slot pairing
    // the category table has (weapons and spells are right-hand items,
    // consumables left, armour and misc neither) -- otherwise a
    // path-loaded weapon would report "no slot" and never equip.
    void InferItemType(int type);

    ScriptDelay m_Delay;  // M53
    // M38: object-valued script fields -- see setValue() above.
    std::map<std::string, skRValue> m_ObjectFields;
    std::string m_ScriptPath;  // M32: see scriptPath()/statusEffect()
    MenuStack& m_Stack;
    skInterpreter* m_Interpreter;
    int m_ItemType = 0;  // +0x16c -- kItemTypeMisc until the category, or a
                         // setter, says otherwise (see SetEntityCategory)
    // M74. -1 = built from a script path, so no category was ever
    // available and the setter inference is live for this object.
    int m_EntityCategory = -1;
    int m_EquipSlot = 2;        // +0x1c0, kEquipSlotNone
    int m_RestrictUseMask = 0;  // +0x1cc, RestrictUse's OR'd class bits
    int m_NameId = -1;
    int m_ShortNameId = -1;
    int m_DescriptionId = -1;
    int m_UseTextId = -1;
    std::string m_Id;
    int m_Cost = 0;
    int m_MarketValue = 0;
    int m_Icon = -1;
    bool m_Usable = false;
    int m_ArmorValue = 0;
    int m_ArmorType = 0;
    int m_ArmorConstraint = 0;
    int m_DamageMin = 0;
    int m_DamageMax = 0;
    int m_WeaponSprite = -1;
    int m_AnimationFrames = 0;
    int m_Range = 0;
    // M49: split out of M20's single m_Ranged -- see launched()/thrown().
    bool m_Launched = false;  // +0x17a: SetBow / SetCrossbow / SetIsLaunched
    bool m_Thrown = false;    // +0x179: SetThrowingWeapon / SetIsThrown
    int m_WeaponType = 0;
    bool m_Equipped = false;
    skiExecutable* m_Owner = nullptr;
    bool m_MarkedForRemoval = false;

    // M21: see quantity()/contents()'s comment above.
    int m_Quantity = 1;
    std::vector<std::unique_ptr<ItemExecutable>> m_Contents;
    size_t m_ContentsIter = 0;

    // M22: see rating()'s comment above.
    int m_Rating = 0;
    // M37: see spellType()/spellLevel()/scroll().
    int m_SpellType = 0;
    int m_SpellLevel = 0;
    bool m_Scroll = false;
    int m_RefireRate = 0;  // M48, +0x1d2 -- see refireRate()
    SpellActor* m_SpellOwner = nullptr;  // M43: see SetSpellOwner()
    // M36: see destroyWhenEmpty()/templateId() above.
    bool m_DestroyWhenEmpty = false;
    int m_TemplateId = -1;
};

}  // namespace sk_bindings
