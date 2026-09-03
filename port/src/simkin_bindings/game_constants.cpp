#include "simkin_bindings/game_constants.h"

#include "skInterpreter.h"
#include "skRValue.h"
#include "skString.h"

namespace sk_bindings {

void RegisterGameConstants(skInterpreter& interpreter) {
    auto add = [&](const char* name, int value) {
        interpreter.addGlobalVariable(skString(name), skRValue(value));
    };

    add("IPT_Misc", kItemTypeMisc);
    add("IPT_Weapon", kItemTypeWeapon);
    add("IPT_Spell", kItemTypeSpell);
    add("IPT_Armor", kItemTypeArmor);
    add("IPT_Consumable", kItemTypeConsumable);

    // Armor weight class -- see game_constants.h's header comment: real
    // identifiers, unconfirmed values (nothing in this port reads them
    // back, only stores/round-trips whatever a script assigns).
    add("AR_Light", 0);
    add("AR_Medium", 1);
    add("AR_Heavy", 2);

    // Weapon damage/handling category -- same caveat as AR_* above.
    add("WR_Dagger", 0);
    add("WR_ShortBlade", 1);
    add("WR_LongBlade", 2);
    add("WR_Axe", 3);
    add("WR_Blunt", 4);
    add("WR_Melee", 5);
    add("WR_LightBow", 6);
    add("WR_MediumBow", 7);
    add("WR_EnchantedBlade", 8);

    // M18: `null` isn't a language literal in this Simkin dialect (checked
    // the vendored grammar -- no such keyword), but real scripts (e.g.
    // azra.s's `if (M1 != null)` after `M1 = Level.GetEntity("m1")`,
    // lootmenu.s's `if (Opener.GetFirst() = null)`) reference it as an
    // ordinary bare global. A blank default skRValue() (T_String) is
    // deliberate, not just the path of least resistance: it makes real
    // scripts' equality checks against a *found* object correctly false
    // (skRValue::operator=='s T_Object-vs-T_String branch), while a
    // script that mistakenly calls a *method* on a genuinely-null result
    // still throws a real "Cannot call Method ... on a non object" error
    // (the vendored interpreter's makeMethodCall() only proceeds for
    // T_Object) -- exactly the "comparison is safe, calling a method on
    // null is still an error" behavior a real null should have. M21 found
    // (and fixed at the source, see ItemExecutable/DoorExecutable/
    // MonsterExecutable's own strValue() overrides) a real bug in the
    // *other* half of this: skTreeNodeObject::strValue() -- the base
    // every skScriptedExecutable-derived class inherits -- defaults to
    // the TreeNode's own empty root data, so a *found*, real Item/Door/
    // Monster object could accidentally compare equal to this blank
    // "null" too (lootmenu.s's own `if (Opener.GetFirst() = null)`
    // caught this for real, wrongly taking its "empty bag" branch for a
    // bag that had one real item in it) -- fixed by giving those classes
    // a real, non-blank strValue(), not by changing what `null` itself
    // is.
    interpreter.addGlobalVariable(skString("null"), skRValue());
}

}  // namespace sk_bindings
