#include "simkin_bindings/trap_ref.h"

#include <cstdio>
#include <memory>

#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/zone_script_executable.h"

namespace sk_bindings {

bool TrapRef::HandleTrapNative(const skString& methodName, skRValueArray& args,
                               skRValue& returnValue) {
    if (methodName == skString("SetMagicDamage") && args.entries() == 1) {
        m_MagicTemplate = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetSpellLevel") && args.entries() == 1) {
        // 16-bit in the engine (`*(u16 *)(entity + 0x188)`); the four
        // shipped values are 8, 10, 15 and 15.
        m_TrapSpellLevel = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetDormant") && args.entries() == 1) {
        m_Dormant = args[0].boolValue();
        return true;
    }
    if (methodName == skString("DoMagicDamage")) {
        // The corpus calls this both bare and as `DoMagicDamage(4017)` --
        // passing the same template id it already gave SetMagicDamage.
        // Case 3 takes no arguments at all and reads the stored field, so
        // the argument is ignored here too rather than quietly becoming a
        // second way to set the template.
        MenuStack* stack = trapStack();
        if (!stack) return true;

        // The gate: tell the zone's trigger list that this entity's door
        // opened (mode 1) and see whether any trap claims it. This is the
        // same notification a real door open makes, so a claiming trigger
        // also rolls its own physical damage and runs its callback --
        // which is what `FUN_1007307c` -> `FUN_10090818` does.
        ZoneScriptExecutable* zone = stack->level().zoneScript();
        const bool claimed =
            zone != nullptr && zone->Notify(TriggerExecutable::kNotifyDoorOpened, -1, trapEntityId(),
                          trapSelf());
        if (!claimed) {
            // The real branch re-runs the entity's own `OnUse`. Every
            // shipped caller is claimed (see the header), and the call
            // arrives from inside `UsePicks.s`, which `OnUse` would
            // re-open -- so this says so instead of looping.
            std::printf(
                "  [trap] DoMagicDamage() on \"%s\" -- no trap trigger claims it, nothing "
                "cast\n",
                trapEntityId().c_str());
            return true;
        }
        if (m_MagicTemplate == 0) return true;

        // `FUN_100715a8(factory, template, 0, 0, 0)`. requireItemCategory
        // is off for the same reason M-era `CreateItem` has the flag at
        // all: a spell template's entities.txt category is not a weapon's.
        std::unique_ptr<ItemExecutable> spell =
            stack->level().CreateItem(m_MagicTemplate, /*requireItemCategory=*/false);
        if (!spell) {
            std::printf("  [trap] DoMagicDamage() -- template %d would not build\n",
                        m_MagicTemplate);
            return true;
        }
        spell->SetSpellLevel(m_TrapSpellLevel);  // FUN_10047540, spell+0x1d0
        spell->SetOwner(trapSelf());             // FUN_1006d510, spell+0x170
        ++m_MagicDamageCount;
        std::printf("  [trap] \"%s\" casts template %d at level %d\n", trapEntityId().c_str(),
                    m_MagicTemplate, m_TrapSpellLevel);
        spell->InvokeHitTarget(static_cast<skiExecutable*>(&stack->player()));
        // `FUN_1001b484(engine, spell)` -- the spell exists for exactly
        // that one call. The unique_ptr going out of scope is that.
        return true;
    }
    return false;
}

}  // namespace sk_bindings
