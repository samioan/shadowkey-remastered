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
}

}  // namespace sk_bindings
