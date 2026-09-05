#include "simkin_bindings/game_constants.h"

#include "simkin_bindings/effects.h"
#include "skInterpreter.h"
#include "skRValue.h"
#include "skString.h"

namespace sk_bindings {

void RegisterGameConstants(skInterpreter& interpreter) {
    auto add = [&](const char* name, int value) {
        interpreter.addGlobalVariable(skString(name), skRValue(value));
    };

    // M58: every named integer constant the engine registers, recovered from
    // the two `SIMKIN_MakeIntAtom` + `SIMKIN_MakeStringAtom` +
    // `SIMKIN_RegisterConstant` runs (GameEngine_FirstTickBootstrap and the
    // per-menu FUN_10073d3c). The table, and what each family means, is in
    // effects.cpp.
    //
    // This replaces three hand-written blocks. `IPT_*` was already right;
    // `AR_*` and `WR_*` were placeholders carrying sequential ordinals and
    // are now the real **bit flags** (AR_Light 2, AR_Medium 4, AR_Heavy 8;
    // WR_Melee 4 through WR_EnchantedBlade 1024), and `SR_Small`/`SR_Medium`
    // are two the port never had at all.
    for (int i = 0; i < kEffectConstantCount; ++i) {
        add(kEffectConstants[i].name, kEffectConstants[i].value);
    }

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
