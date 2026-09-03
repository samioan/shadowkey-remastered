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

#include <string>

#include "simkin_bindings/native_stub_executable.h"
#include "skScriptedExecutable.h"

class skInterpreter;

namespace sk {
class StringTable;
}

namespace sk_bindings {

class PlayerExecutable;

class ItemExecutable : public skScriptedExecutable {
public:
    // `strings` may be null (e.g. a context with no real stringtable.eng
    // loaded, like some smoke tests) -- GetName()/GetItemDescription()
    // fall back to a placeholder rather than crash.
    //
    // M19: `player` -- a real world-pickup script's OnUse() calls
    // GetPlayer() bare (self-receiver, same convention every other
    // binding class's own OnUse()/Init() already relies on), needed to
    // resolve `GetPlayer().PickupItem(self)` (snowline/foxglove.s etc.).
    // Every existing call site (PlayerExecutable::LoadStartingInventory)
    // already has a real PlayerExecutable to pass (itself).
    ItemExecutable(const skString& filename, skExecutableContext& ctxt,
                    const sk::StringTable* strings, PlayerExecutable& player);

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

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

private:
    const sk::StringTable* m_Strings;
    PlayerExecutable& m_Player;
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
    int m_WeaponType = 0;
    bool m_Equipped = false;
    skiExecutable* m_Owner = nullptr;
    bool m_MarkedForRemoval = false;
};

}  // namespace sk_bindings
