#pragma once

// M94: the trap / magic-damage mixin -- SimKin class `0x14e10`, dispatcher
// `FUN_1002de24`, four bindings and 19 call sites:
//
//   0x00  SetMagicDamage(templateId)   -- entity+0x184
//   0x01  SetSpellLevel(level)         -- entity+0x188, 16-bit
//   0x02  SetDormant(bool)             -- entity+0x190
//   0x03  DoMagicDamage()              -- cast it at the player
//
// It is a *mixin*, not a class of its own: the dispatcher's miss path
// forwards to `entity+0x18c` and then on to `FUN_10028594`, so a trapped
// thing is an ordinary door or container with these four bindings bolted
// on. In this port it goes on `DoorExecutable` and `ItemExecutable`, the
// two classes the shipped traps actually are.
//
// ---- `SetMagicDamage` is a spell template, not a damage number --------
//
// Case 0 stores its argument at `entity+0x184`, and `DoMagicDamage` hands
// that straight to the **item factory** (`FUN_100715a8`) as a typeId. The
// corpus agrees: the four values passed are 50 (the ignite template
// `spells/u_blaze_lvl10.s` names in its own comment) and 4017 / 4021 /
// 4023, which are template-id shaped and nothing like damage.
//
// ---- `SetDormant` is what stops every trap firing on contact ----------
//
// `FUN_1002e5ec` is the trapped entity's own use handler, and it is three
// lines:
//
//     if (entity->dormant /* +0x190 */ == 0) { FireTrap(entity); }
//     else                                   { RunOnUse(entity); }
//
// -- `FUN_100646a8` being "if `entity+0xd8` is set, call the script's
// `OnUse`". All six shipped `SetDormant` calls pass **true**, in an
// `Init()`, which is exactly why every trapped door in the game opens its
// lock-picking screen instead of exploding when you touch it. The trap
// then only fires through the explicit `DoMagicDamage()` its own
// `MagicDamage[]` handler makes.
//
// ---- What `DoMagicDamage` does (`FUN_1002e024`) ----------------------
//
//     if (!AnyTriggerClaims(engine, /*mode*/ 1, entity)) {
//         RunOnUse(entity);                       // FUN_100646a8
//     } else {
//         PlaySound(61);
//         spell = ItemFactory(engine, entity->magicTemplate, 0, 0, 0);
//         if (spell) {
//             spell->+0x1d0 = entity->spellLevel; // FUN_10047540
//             spell->+0x170 = entity;             // the caster is the trap
//             spell->method("HitTarget", { player });
//             Destroy(spell);                     // FUN_1001b484
//         }
//     }
//
// The method name is the wide literal at `0x100adf7c`, **`HitTarget`** --
// so the trap casts a real spell script at the player and the spell's own
// `HitTarget` handler does the damage. The spell object exists for exactly
// that one call.
//
// **The trigger gate is real and it is satisfied.** `FUN_1007307c` walks
// the zone's trigger list calling `FUN_10090818(trigger, 1, entity)` --
// mode 1 is `kNotifyDoorOpened`, and the trigger matches on the entity's
// id string at `entity+0xcb`, which is the `.ent` placement name M92
// wired up. The three scripts that call `DoMagicDamage` are
// `lockeddoor_dh.s`, `_fb.s` and `_ha.s`, `crypt1.ent` places them as
// `door1`..`door5`, and `crypt1.s`'s `Init()` registers exactly those five
// names with `AddTrigger("doorN"); SetTrap(...)`. So the gate passes for
// every shipped caller -- which is why the fallback branch is a logged
// no-op here rather than a re-entry into `OnUse` (the call arrives from
// inside `UsePicks.s`, which `OnUse` would re-open).
//
// ---- Which scripts set it, and which actually use it -----------------
//
// Worth being precise, because the 19 sites are not 19 working traps.
// `lockeddoor_bl.s` and the two delfhide chests set the template and the
// level and then damage the player directly (`GetPlayer().DoDamage(20)`,
// `SetHealth(GetHealth() - 6)`); only `lockeddoor_dh/_fb/_ha` -- crypt1's
// five doors -- ever call `DoMagicDamage()`. The other sites are still
// load-bearing through `SetDormant`.

#include <string>

#include "skRValue.h"
#include "skRValueArray.h"
#include "skString.h"

class skiExecutable;

namespace sk_bindings {

class MenuStack;

class TrapRef {
public:
    virtual ~TrapRef() = default;

    // `entity+0x184`: the spell template this trap casts, 0 for none.
    int magicDamageTemplate() const { return m_MagicTemplate; }
    // `entity+0x188`: the level that spell is cast at.
    int trapSpellLevel() const { return m_TrapSpellLevel; }
    // `entity+0x190`: armed but asleep -- a plain Use runs the script's
    // own OnUse instead of firing. True for every shipped trap.
    bool dormant() const { return m_Dormant; }

    // How many times this trap has actually gone off, so a test can tell
    // "fired" from "the handler ran and the gate said no".
    int magicDamageCount() const { return m_MagicDamageCount; }

protected:
    // Returns false if `methodName` is none of the four, so each class can
    // carry on to its own methods and then soft-fail -- the same "try,
    // then fall through" shape EntityBaseRef uses.
    bool HandleTrapNative(const skString& methodName, skRValueArray& args, skRValue& returnValue);

    // What the mixin cannot see for itself. Both mixing classes already
    // have all three.
    virtual MenuStack* trapStack() const = 0;
    virtual const std::string& trapEntityId() const = 0;
    virtual skiExecutable* trapSelf() = 0;

private:
    int m_MagicTemplate = 0;
    int m_TrapSpellLevel = 0;
    bool m_Dormant = false;
    int m_MagicDamageCount = 0;
};

}  // namespace sk_bindings
