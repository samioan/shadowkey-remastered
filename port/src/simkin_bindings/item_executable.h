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
    void InvokeOnUse();

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

private:
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
};

}  // namespace sk_bindings
