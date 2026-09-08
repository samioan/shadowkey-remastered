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

// M74: the factory table's item arms -- see the header for where each
// row comes from and how it was checked against the shipped entities.txt.
namespace {

struct ItemCategoryRow {
    int category;
    int itemType;
    int equipSlot;
};

const ItemCategoryRow kItemCategories[] = {
    {3, kItemTypeMisc, kEquipSlotNone},         // FUN_1002eeb8
    {4, kItemTypeWeapon, kEquipSlotRight},      // FUN_1002ce9c
    {5, kItemTypeSpell, kEquipSlotRight},       // FUN_10047740
    {6, kItemTypeArmor, kEquipSlotNone},        // FUN_1002e954
    {9, kItemTypeConsumable, kEquipSlotLeft},   // FUN_1002e78c
    {14, kItemTypeSpell, kEquipSlotRight},      // FUN_1002e5ac : FUN_10047740
    {15, kItemTypeArmor, kEquipSlotNone},       // FUN_1002e844
    {16, kItemTypeWeapon, kEquipSlotRight},     // FUN_10047500 : FUN_1002ce9c
};

const ItemCategoryRow* FindItemCategory(int category) {
    for (const ItemCategoryRow& row : kItemCategories) {
        if (row.category == category) return &row;
    }
    return nullptr;
}

}  // namespace

int ItemTypeForCategory(int entityCategory) {
    const ItemCategoryRow* row = FindItemCategory(entityCategory);
    // A category with no arm never reaches an item constructor at all, so
    // it has no `+0x16c`. Misc is the honest answer for "not an item".
    return row ? row->itemType : kItemTypeMisc;
}

int EquipSlotForCategory(int entityCategory) {
    const ItemCategoryRow* row = FindItemCategory(entityCategory);
    return row ? row->equipSlot : kEquipSlotNone;
}

bool IsInventoryItemCategory(int entityCategory) {
    return FindItemCategory(entityCategory) != nullptr;
}

}  // namespace sk_bindings
