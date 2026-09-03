#include "simkin_bindings/item_executable.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "assets/string_table.h"
#include "simkin_bindings/combat.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/native_binding_common.h"
#include "simkin_bindings/player_executable.h"
#include "skExecutableContext.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"

namespace sk_bindings {

ItemExecutable::ItemExecutable(const skString& filename, skExecutableContext& ctxt,
                                MenuStack& stack)
    : skScriptedExecutable(filename, ctxt), m_Stack(stack), m_Interpreter(ctxt.getInterpreter()) {
    m_ScriptPath = ToStdString(filename);
    for (char& c : m_ScriptPath) {
        if (c == '\\') c = '/';
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
}

ItemExecutable::StatusEffect ItemExecutable::statusEffect() const {
    // See the header: the real engine keys this off the spell entity's
    // entities.txt typeId; this port loads spells by path, which
    // entities.txt maps to those same typeIds.
    auto endsWith = [&](const char* suffix) {
        std::string s(suffix);
        return m_ScriptPath.size() >= s.size() &&
               m_ScriptPath.compare(m_ScriptPath.size() - s.size(), s.size(), s) == 0;
    };
    if (endsWith("spells/fear.s")) return kEffectFear;
    if (endsWith("spells/paralyze.s")) return kEffectParalyze;
    if (endsWith("spells/poison.s")) return kEffectPoison;
    if (endsWith("spells/disease.s")) return kEffectDisease;
    if (endsWith("spells/drain.s")) return kEffectDrain;
    if (endsWith("spells/blind.s")) return kEffectBlind;
    if (endsWith("spells/harmarmor.s")) return kEffectHarmArmor;
    // Still unmodelled: Absorb (4009, a health transfer from target to
    // caster) and IgniteFoe (4024, which drives the stats block's *second*
    // periodic-effect channel at +0x78/+0x7c rather than the +0x72/+0x76
    // one the others share).
    return kEffectNone;
}

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

void ItemExecutable::InvokeHitTarget(skiExecutable* target) {
    if (!m_Interpreter) return;
    skRValueArray args;
    args.append(skRValue(target, false));
    skRValue ret;
    skExecutableContext ctxt(m_Interpreter);
    try {
        method(skString("HitTarget"), args, ret, ctxt);
    } catch (skParseException& e) {
        std::printf("ItemExecutable: PARSE ERROR in HitTarget(): %s\n", e.toString().ptr());
    } catch (skRuntimeException& e) {
        std::printf("ItemExecutable: RUNTIME ERROR in HitTarget(): %s\n", e.toString().ptr());
    }
}

std::string ItemExecutable::name() const {
    if (m_Stack.strings() && m_NameId >= 0) return m_Stack.strings()->Get(m_NameId);
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
    // M20: real ranged-weapon markers -- SetClipSize/SetFireRate/
    // SetReloadFrames are deliberately NOT handled here (soft-fail is
    // correct): a full corpus grep found zero real call sites for any of
    // the three anywhere in the whole game, matching docs/
    // INPUT_HANDLING.md's separate finding that "Shoot"/"Reload" exist as
    // logical actions but were never wired into the real default control
    // scheme -- ammo/rate-of-fire is dead weight in the shipped game, not
    // a gap this port is missing.
    if (methodName == skString("SetBow") && args.entries() == 1) {
        m_Ranged = args[0].boolValue();
        m_ItemType = kItemTypeWeapon;
        return true;
    }
    if (methodName == skString("SetCrossbow") && args.entries() == 1) {
        m_Ranged = args[0].boolValue();
        m_ItemType = kItemTypeWeapon;
        return true;
    }
    if (methodName == skString("SetThrowingWeapon") && args.entries() == 1) {
        m_Ranged = args[0].boolValue();
        m_ItemType = kItemTypeWeapon;
        return true;
    }
    if (methodName == skString("SetRating") && args.entries() == 1) {
        // M22: deliberately does NOT set m_ItemType -- see rating()'s
        // header comment for the real corpus counter-example
        // (misc/ring_of_fangs.s) that ruled this out as a category
        // signal.
        m_Rating = args[0].intValue();
        return true;
    }
    if (methodName == skString("RestrictUse")) {
        // M22: a real class/race restriction list, never read back by any
        // script and with no restriction system in this port to apply it
        // to (PlayerExecutable::IsItemEnabledFor()'s own comment) --
        // accepted as a no-op so its 100+ real call sites don't log soft-
        // fail noise.
        return true;
    }
    if (methodName == skString("DoAttackRoll") && args.entries() >= 1) {
        // M22: called bare (self-receiver) from within a real spell
        // script's own HitTarget(target) handler (InvokeHitTarget() above)
        // -- args[0] is the target HitTarget() itself received; args[1],
        // when present, is a real per-spell status-effect id (e.g.
        // blaze.s's own DoAttackRoll(target,1)) this port doesn't model
        // (RollSpellDamage()'s own comment) -- only damage applies.
        auto* target = static_cast<MonsterExecutable*>(args[0].obj());
        if (target && target->alive()) {
            // M33: the effect's magnitude is NOT args[1].
            //
            // FUN_100458e4 derives it from the *caster's* own spell-power
            // stat (the stats block's +0x34, stat index 0x18) and clamps it
            // to 25 -- it never looks at the script's argument. The proof
            // is in the scripts themselves: real Poison.s and Disease.s
            // call `DoAttackRoll(target)` with no second argument at all,
            // yet both apply a fully-parameterised effect.
            //
            // This port has no spell-power stat on the player, so the
            // spell's own SetRating() stands in: it is a per-spell power
            // number (poison 8, blind 3, drain 12, fear 14, harmarmor 17,
            // paralyze 20), it is what RollSpellDamage() already scales
            // damage by, and it is defined for every spell including the
            // two that pass no argument. Documented substitution, not a
            // recovered value -- and the real clamp is reproduced.
            int magnitude = (std::min)(m_Rating, 25);
            switch (statusEffect()) {
                case kEffectFear:
                    // FUN_100458e4's Fear branch:
                    // FUN_10086b98(target, 4, magnitude * 5).
                    target->SetAiPackageTimed(MonsterExecutable::kAiFlee, magnitude * 5);
                    break;
                case kEffectParalyze:
                    // The Paralyze branch arms the target's action lockout
                    // (monster+0x294) through a vtable call; the same
                    // magnitude-scaled duration shape is used here.
                    target->SetParalyzed(magnitude * 5 * 256);
                    break;
                // ---- M33: the remaining real branches, transcribed from
                // FUN_100458e4. Each is a combination of the two decompiled
                // primitives: a timed stat modifier (FUN_1004aa28) and/or a
                // timed effect flag (FUN_1004bae8).
                case kEffectDrain:
                    // `iVar3 = -10; duration = magnitude + 8; stat = 1`
                    // falling into the shared FUN_1004aa28 tail.
                    target->ApplyStatModifier(MonsterExecutable::kStatAttack, -10,
                                               magnitude + 8);
                    break;
                case kEffectHarmArmor:
                    // `iVar3 = -magnitude; duration = magnitude + 8; stat = 7`.
                    target->ApplyStatModifier(MonsterExecutable::kStatArmor, -magnitude,
                                               magnitude + 8);
                    break;
                case kEffectBlind: {
                    // Two -10 modifiers (attack and defense) for
                    // `magnitude + 5`, plus effect flag 4 -- which the real
                    // code applies only when the target is not the player.
                    // Every target this port can cast at is a creature, so
                    // the flag always applies here.
                    int blindDuration = magnitude + 5;
                    target->ApplyStatModifier(MonsterExecutable::kStatAttack, -10, blindDuration);
                    target->ApplyStatModifier(MonsterExecutable::kStatDefense, -10, blindDuration);
                    target->ApplyEffectFlag(MonsterExecutable::kEffectFlagBlind, 0, blindDuration);
                    break;
                }
                case kEffectDisease:
                    // `delta = (magnitude < 7) ? -2 : -3` on attack, a flat
                    // -3 on defense, both for a fixed 0x1e (30) -- the only
                    // effect with a duration that ignores magnitude.
                    target->ApplyStatModifier(MonsterExecutable::kStatAttack,
                                               magnitude < 7 ? -2 : -3, 30);
                    target->ApplyStatModifier(MonsterExecutable::kStatDefense, -3, 30);
                    break;
                case kEffectPoison:
                    // `FUN_1004bae8(target, 8, magnitude)` -- effect flag 8
                    // with damage-over-time kind 3, which the DoT tick
                    // passes straight to DoDamage: 3 points every 256 delta
                    // units for `magnitude` seconds.
                    target->ApplyEffectFlag(MonsterExecutable::kEffectFlagPoison, 3, magnitude);
                    break;
                case kEffectNone: {
                    int dmg = RollSpellDamage(m_Rating, target->magicResistance());
                    target->ApplyDamage(dmg);
                    break;
                }
            }
        }
        return true;
    }
    if (methodName == skString("GetName") && args.entries() == 0) {
        returnValue = skRValue(skString(name().c_str()));
        return true;
    }
    if (methodName == skString("GetItemDescription") && args.entries() == 0) {
        std::string desc = m_Stack.strings() && m_DescriptionId >= 0
                                ? m_Stack.strings()->Get(m_DescriptionId)
                                : "";
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
        returnValue = skRValue(static_cast<skiExecutable*>(&m_Stack.player()), false);
        return true;
    }
    if (TryHandleRandom(methodName, args, returnValue)) {
        // M21: loot_gold6-10.s's own Init() calls
        // `Item.SetQuantity(Random(6,10))` -- bare, so resolved as a call
        // on whichever ItemExecutable is running (native_binding_common.h's
        // TryHandleRandom() comment has the full real-corpus justification).
        return true;
    }
    if (methodName == skString("SetQuantity") && args.entries() == 1) {
        m_Quantity = args[0].intValue();
        return true;
    }
    if (methodName == skString("GetQuantity") && args.entries() == 0) {
        returnValue = skRValue(m_Quantity);
        return true;
    }
    if (methodName == skString("AddObject") && args.entries() == 1) {
        // M21: called as "AddObject(Item)" from a real loot-bag script's
        // own Init() (loot_ratseye.s etc.), where `Item` is exactly what
        // `Level.CreateEntity(...)` (LevelExecutable::method()'s own
        // handler) just returned -- takes real ownership of it out of
        // Level's one-slot pending holder (see
        // LevelExecutable::TakePendingCreatedEntity()'s comment) and into
        // this bag's own Collection, only if the passed reference actually
        // matches (defends against a script somehow passing something
        // else, though no real corpus script does).
        std::unique_ptr<ItemExecutable> created = m_Stack.level().TakePendingCreatedEntity();
        if (created && static_cast<skiExecutable*>(created.get()) == args[0].obj()) {
            m_Contents.push_back(std::move(created));
        }
        return true;
    }
    if (methodName == skString("GetFirst") && args.entries() == 0) {
        // M21: lootmenu.s's own Collection-walk convention (GetFirst() then
        // repeated GetNext() until null) -- see docs/SIMKIN_NATIVE_API.md's
        // "Collection" class research.
        m_ContentsIter = 0;
        if (!m_Contents.empty()) {
            returnValue = skRValue(static_cast<skiExecutable*>(m_Contents[0].get()), false);
        }
        // else: leave returnValue at its default blank skRValue() -- the
        // "null" global (game_constants.cpp's own comment) -- lootmenu.s's
        // own `if (Opener.GetFirst() = null)` compares against exactly
        // this. Correct as long as a real found ItemExecutable's own
        // strValue() is never blank -- see this class's strValue()
        // override below.
        return true;
    }
    if (methodName == skString("GetNext") && args.entries() == 0) {
        ++m_ContentsIter;
        if (m_ContentsIter < m_Contents.size()) {
            returnValue =
                skRValue(static_cast<skiExecutable*>(m_Contents[m_ContentsIter].get()), false);
        }
        return true;
    }
    if (methodName == skString("RemoveObject") && args.entries() == 1) {
        // M21: lootmenu.s's SelectItem() calls
        // "GetPlayer().PickupItem(Object); GetOpener().RemoveObject(Object);"
        // in that order -- PickupItem() (PlayerExecutable's own handler)
        // only records which object asked (see its comment), so this is
        // where the real ownership transfer actually completes: unlike
        // M19's world-pickup flow (deferred to main.cpp, because that real
        // call site is still inside a host-triggered InvokeOnUse() frame
        // holding the item in a different container), a loot-menu
        // selection's whole round trip happens within one ordinary script-
        // to-script call chain, so the transfer can finish synchronously
        // right here, with no dangling-pointer window to defer past.
        skiExecutable* target = args[0].obj();
        auto it = std::find_if(m_Contents.begin(), m_Contents.end(),
                                [&](const std::unique_ptr<ItemExecutable>& item) {
                                    return static_cast<skiExecutable*>(item.get()) == target;
                                });
        if (it != m_Contents.end()) {
            std::unique_ptr<ItemExecutable> removed = std::move(*it);
            m_Contents.erase(it);
            if (m_Stack.player().TakePendingPickupItem() ==
                static_cast<skiExecutable*>(removed.get())) {
                m_Stack.player().AddItem(std::move(removed));
            }
            // else: no real corpus script calls RemoveObject() without a
            // matching PickupItem() first -- the removed item is simply
            // dropped (freed) rather than silently kept somewhere, same
            // "soft-fail rather than guess" spirit as everywhere else.
        }
        return true;
    }
    if (methodName == skString("OpenMenu") && args.entries() == 1) {
        // M21: a real loot bag's OnUse() calls this bare (self-receiver,
        // loot_ratseye.s's own `OpenMenu("LootMenu")`) -- ReopenMenu(), not
        // OpenMenu(), for the same M17 reason NPC dialogue needs it
        // (lootmenu.s's own UpdateMenu() re-walks this bag's live Collection
        // in Init(), which has to rerun on every visit as the bag's
        // contents actually change, not just the first). Passes `this` as
        // the new menu's opener so lootmenu.s's real GetOpener() calls
        // resolve back to this exact bag, not some other one.
        m_Stack.ReopenMenu(ToStdString(args[0].str()), static_cast<skiExecutable*>(this));
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
