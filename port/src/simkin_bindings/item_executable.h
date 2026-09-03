#pragma once

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

#include "simkin_bindings/native_stub_executable.h"
#include "skScriptedExecutable.h"

class skInterpreter;

namespace sk {
class StringTable;
}

namespace sk_bindings {

class MenuStack;

class ItemExecutable : public skScriptedExecutable {
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
    // so the effect is resolved from the script filename.
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
    };
    StatusEffect statusEffect() const;

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
    int cost() const { return m_Cost; }
    int armorValue() const { return m_ArmorValue; }
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
    bool ranged() const { return m_Ranged; }

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
    int quantity() const { return m_Quantity; }
    const std::vector<std::unique_ptr<ItemExecutable>>& contents() const { return m_Contents; }

    // M22: a real spell script's own SetRating() (e.g. blaze.s's
    // SetRating(2)) -- confirmed NOT an exclusive spell-category signal
    // (misc/ring_of_fangs.s, a real armor-category ring, also calls it,
    // *after* its own SetArmorValue/SetArmorType -- deliberately not used
    // to infer itemType() the way SetDamageMin/SetArmorValue/SetUsable
    // already do, unlike every other category setter in this class).
    // Never read back by any real script either (no GetRating() call
    // anywhere in the corpus) -- host-side only, DoAttackRoll()'s own
    // damage formula.
    int rating() const { return m_Rating; }

private:
    std::string m_ScriptPath;  // M32: see scriptPath()/statusEffect()
    MenuStack& m_Stack;
    skInterpreter* m_Interpreter;
    int m_ItemType = 0;  // kItemTypeMisc until a category setter fires
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
    bool m_Ranged = false;  // M20: SetBow/SetCrossbow/SetThrowingWeapon(true)
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
};

}  // namespace sk_bindings
