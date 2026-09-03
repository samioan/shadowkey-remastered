#include "simkin_bindings/item_executable.h"

#include <cstdio>

#include "assets/string_table.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/native_binding_common.h"
#include "simkin_bindings/player_executable.h"
#include "skExecutableContext.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"

namespace sk_bindings {

ItemExecutable::ItemExecutable(const skString& filename, skExecutableContext& ctxt,
                                const sk::StringTable* strings, PlayerExecutable& player)
    : skScriptedExecutable(filename, ctxt),
      m_Strings(strings),
      m_Player(player),
      m_Interpreter(ctxt.getInterpreter()) {}

void ItemExecutable::InvokeOnUse() {
    if (!m_Interpreter) return;
    skRValueArray args;
    args.append(skRValue(0));  // placeholder for OnUse's "(s)" parameter, same convention every
                                // other InvokeOnUse() in this codebase already uses.
    skRValue ret;
    skExecutableContext ctxt(m_Interpreter);
    try {
        method(skString("OnUse"), args, ret, ctxt);
    } catch (skParseException& e) {
        std::printf("ItemExecutable: PARSE ERROR in OnUse(): %s\n", e.toString().ptr());
    } catch (skRuntimeException& e) {
        std::printf("ItemExecutable: RUNTIME ERROR in OnUse(): %s\n", e.toString().ptr());
    }
}

std::string ItemExecutable::name() const {
    if (m_Strings && m_NameId >= 0) return m_Strings->Get(m_NameId);
    return m_Id.empty() ? std::string("?") : m_Id;
}

bool ItemExecutable::method(const skString& methodName, skRValueArray& args,
                             skRValue& returnValue, skExecutableContext& context) {
    if (methodName == skString("SetName") && args.entries() == 1) {
        m_NameId = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetShortName") && args.entries() == 1) {
        m_ShortNameId = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetItemDescription") && args.entries() == 1) {
        m_DescriptionId = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetUseText") && args.entries() == 1) {
        m_UseTextId = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetID") && args.entries() == 1) {
        m_Id = ToStdString(args[0].str());
        return true;
    }
    if (methodName == skString("SetCost") && args.entries() == 1) {
        m_Cost = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetMarketValue") && args.entries() == 1) {
        m_MarketValue = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetIcon") && args.entries() == 1) {
        m_Icon = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetUsable") && args.entries() == 1) {
        m_Usable = args[0].boolValue();
        if (m_Usable) m_ItemType = kItemTypeConsumable;
        return true;
    }
    if (methodName == skString("SetArmorValue") && args.entries() == 1) {
        m_ArmorValue = args[0].intValue();
        m_ItemType = kItemTypeArmor;
        return true;
    }
    if (methodName == skString("SetArmorType") && args.entries() == 1) {
        m_ArmorType = args[0].intValue();
        m_ItemType = kItemTypeArmor;
        return true;
    }
    if (methodName == skString("SetArmorConstraint") && args.entries() == 1) {
        m_ArmorConstraint = args[0].intValue();
        m_ItemType = kItemTypeArmor;
        return true;
    }
    if (methodName == skString("SetDamageMin") && args.entries() == 1) {
        m_DamageMin = args[0].intValue();
        m_ItemType = kItemTypeWeapon;
        return true;
    }
    if (methodName == skString("SetDamageMax") && args.entries() == 1) {
        m_DamageMax = args[0].intValue();
        m_ItemType = kItemTypeWeapon;
        return true;
    }
    if (methodName == skString("SetWeaponSprite") && args.entries() == 1) {
        m_WeaponSprite = args[0].intValue();
        m_ItemType = kItemTypeWeapon;
        return true;
    }
    if (methodName == skString("SetAnimationFrames") && args.entries() == 1) {
        m_AnimationFrames = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetRange") && args.entries() == 1) {
        m_Range = args[0].intValue();
        m_ItemType = kItemTypeWeapon;
        return true;
    }
    if (methodName == skString("SetWeaponType") && args.entries() == 1) {
        m_WeaponType = args[0].intValue();
        m_ItemType = kItemTypeWeapon;
        return true;
    }
    if (methodName == skString("GetName") && args.entries() == 0) {
        returnValue = skRValue(skString(name().c_str()));
        return true;
    }
    if (methodName == skString("GetItemDescription") && args.entries() == 0) {
        std::string desc = m_Strings && m_DescriptionId >= 0 ? m_Strings->Get(m_DescriptionId) : "";
        returnValue = skRValue(skString(desc.c_str()));
        return true;
    }
    if (methodName == skString("GetIcon") && args.entries() == 0) {
        returnValue = skRValue(m_Icon);
        return true;
    }
    if (methodName == skString("GetCost") && args.entries() == 0) {
        returnValue = skRValue(m_Cost);
        return true;
    }
    if (methodName == skString("GetItemType") && args.entries() == 0) {
        returnValue = skRValue(m_ItemType);
        return true;
    }
    if (methodName == skString("CanDrop") && args.entries() == 0) {
        // No quest-critical/undroppable item flag exists in this port's
        // model -- every real, owned item can be dropped.
        returnValue = skRValue(true);
        return true;
    }
    if (methodName == skString("CanTravel") && args.entries() == 0) {
        // Travel-document-style misc items aren't part of this milestone's
        // curated starting inventory (see PlayerExecutable::
        // LoadStartingInventory) -- correctly false for every item this
        // port can currently own.
        returnValue = skRValue(false);
        return true;
    }
    if (methodName == skString("GetArmorText") && args.entries() == 0) {
        // Real armor-rating display text format was never RE'd -- a plain
        // number is the simplest thing that reads correctly in the
        // Equip/UnEquip popup line ("Equip 18"), see inventory.s.
        returnValue = skRValue(skString(std::to_string(m_ArmorValue).c_str()));
        return true;
    }
    if (methodName == skString("GetOwner") && args.entries() == 0) {
        if (m_Owner) returnValue = skRValue(m_Owner, false);
        return true;
    }
    if (methodName == skString("OnUsedBy") && args.entries() == 1) {
        // Native entry point inventory.s's UseItem() calls
        // ("inv.OnUsedBy(GetPlayer())") -- binds the passed-in player as
        // this item's GetOwner() and then runs the script's own OnUse
        // handler (items/bread.s etc.), if it defines one. Marks the item
        // for removal so the host (PlayerExecutable::PurgeRemovedItems)
        // erases it from the inventory once this tick's script calls have
        // fully returned -- matches items/bread.s's own
        // DestroyObject(self) call, which this port doesn't model as a
        // generic native (no general object-destruction registry exists),
        // just this one well-understood consumable-item case.
        m_Owner = args[0].obj();
        skRValueArray noArgs;
        skRValue ret;
        skScriptedExecutable::method(skString("OnUse"), noArgs, ret, context);
        m_MarkedForRemoval = true;
        return true;
    }
    if (methodName == skString("DestroyObject") && args.entries() == 1) {
        // Called as "DestroyObject(self)" from within the item's own
        // OnUse handler -- already covered by OnUsedBy() marking
        // m_Consumed above, this just needs to not throw.
        return true;
    }
    if (methodName == skString("GetPlayer") && args.entries() == 0) {
        // M19: a world pickup's real OnUse() calls this bare (self-
        // receiver) -- same handler shape Door/Monster/Menu already have.
        returnValue = skRValue(static_cast<skiExecutable*>(&m_Player), false);
        return true;
    }
    if (methodName == skString("MirrorDestroyObject") && args.entries() == 1) {
        // M19: called as "MirrorDestroyObject(self)" from a real world
        // pickup's own OnUse() (snowline/foxglove.s etc.), right after
        // GetPlayer().PickupItem(self) -- network/replication bookkeeping
        // in the original (this port has no multiplayer, same DoorOpened()
        // precedent), but the removal-from-world intent is real: reuses
        // the same markedForRemoval flag OnUsedBy() sets for a consumed
        // inventory item -- main.cpp's Action::Use handling reads it to
        // erase this instance from gamePickups after InvokeOnUse() returns.
        m_MarkedForRemoval = true;
        return true;
    }
    if (skScriptedExecutable::method(methodName, args, returnValue, context)) {
        return true;
    }
    return SoftFailNativeCall("Item", methodName, args, returnValue);
}

}  // namespace sk_bindings
