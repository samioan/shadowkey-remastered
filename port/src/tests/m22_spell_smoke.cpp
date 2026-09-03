// M22 (spellcasting) smoke test: proves the real cast chain end to end
// against real script data -- equipping a real spell via its own OnUse(),
// then casting it at a real monster via the same host-triggered
// HitTarget()/DoAttackRoll() path main.cpp's tryAttack uses.
//
// Real data, read directly off the install image:
//   spells/blind.s:
//     Init(s) { SetUseText(2401); SetName(2401); SetRating(3);
//               SetItemDescription(2402); RestrictUse(2,4,6,7);
//               SetIcon(209); SetCost(1600); SetMarketValue(560);
//               if (GetPlayer().IsItemEnabledFor(self)=true)
//                 SetUseText(2403); else SetUseText(2404); }
//     OnUse(s) { GetPlayer().EquipItem(0, self); }
//     HitTarget(target) { DoAttackRoll(target, 3); }
//   (blind.s, a sibling spell script, has real Init()/HitTarget() but no
//   OnUse() at all -- it's equipped directly by a debug menu script
//   instead, not via clicking "use" -- blind.s was picked specifically
//   because it has all three real handlers this test needs.)
//   monsters/Azra_Rat.s: SetMagicResistance(3) (already verified real by
//     M12's own combat_smoke test, reused here for the damage formula).
#include <cstdio>
#include <memory>
#include <string>

#include "assets/string_table.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/player_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"

namespace {

skRValueArray InitArgs() {
    skRValueArray args;
    args.append(skRValue(0));  // placeholder for Init's "(s)" parameter
    return args;
}

}  // namespace

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    if (!strings.Load(std::string(scriptRoot) + "/stringtable.eng")) {
        std::printf("m22_spell_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }

    bool ok = true;
    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);

    // --- Part 1: real blind.s Init() ---
    skExecutableContext loadCtxt(&interpreter);
    std::unique_ptr<sk_bindings::ItemExecutable> blind;
    try {
        blind = std::make_unique<sk_bindings::ItemExecutable>(
            skString((std::string(scriptRoot) + "/spells/blind.s").c_str()), loadCtxt, stack);
        skRValueArray args = InitArgs();
        skRValue ret;
        skExecutableContext callCtxt(&interpreter);
        blind->method(skString("Init"), args, ret, callCtxt);
    } catch (skParseException& e) {
        std::printf("m22_spell_smoke: FAILED -- PARSE ERROR: %s\n", e.toString().ptr());
        return 1;
    } catch (skRuntimeException& e) {
        std::printf("m22_spell_smoke: FAILED -- RUNTIME ERROR: %s\n", e.toString().ptr());
        return 1;
    }
    bool ratingOk = blind->rating() == 3;
    std::printf("spells/blind.s rating(): %d (expected 3) %s\n", blind->rating(),
                ratingOk ? "OK" : "FAILED");
    if (!ratingOk) ok = false;

    // --- Part 2: real OnUse() -> GetPlayer().EquipItem(0, self) ---
    sk_bindings::ItemExecutable* blindRaw = blind.get();
    stack.player().AddItem(std::move(blind));  // matches how a spell would already be owned
    blindRaw->InvokeOnUse();
    bool equippedOk = stack.player().leftItem() == blindRaw;
    std::printf("player.leftItem() after blind.s's real OnUse() (EquipItem(0,self)): %s %s\n",
                equippedOk ? "matches" : "does not match", equippedOk ? "OK" : "FAILED");
    if (!equippedOk) ok = false;

    // --- Part 3: real Azra_Rat.s, real magicResistance() ---
    skExecutableContext ratCtxt(&interpreter);
    std::unique_ptr<sk_bindings::MonsterExecutable> rat;
    try {
        rat = std::make_unique<sk_bindings::MonsterExecutable>(
            skString((std::string(scriptRoot) + "/monsters/Azra_Rat.s").c_str()), ratCtxt, &strings,
            stack.player(), stack);
        skRValueArray args = InitArgs();
        skRValue ret;
        skExecutableContext callCtxt(&interpreter);
        rat->method(skString("Init"), args, ret, callCtxt);
    } catch (skParseException& e) {
        std::printf("m22_spell_smoke: FAILED -- PARSE ERROR: %s\n", e.toString().ptr());
        return 1;
    } catch (skRuntimeException& e) {
        std::printf("m22_spell_smoke: FAILED -- RUNTIME ERROR: %s\n", e.toString().ptr());
        return 1;
    }
    bool magicResistOk = rat->magicResistance() == 3;
    std::printf("Azra_Rat.s magicResistance(): %d (expected 3) %s\n", rat->magicResistance(),
                magicResistOk ? "OK" : "FAILED");
    if (!magicResistOk) ok = false;

    // --- Part 4: the payoff -- real HitTarget()/DoAttackRoll() ---
    //
    // CORRECTED in M33. This used to assert that blind.s deals
    // RollSpellDamage() damage, which was only ever true because the port
    // treated *every* spell as a damage spell. Decompiling the real
    // status-effect dispatcher (FUN_100458e4) showed the effect is chosen
    // by the spell entity's own entities.txt typeId, and Blind (4010) is
    // not a damage spell at all: it applies -10 to attack and -10 to
    // defense and sets the blind flag. So the right assertion is that it
    // deals *no* damage and applies exactly those modifiers.
    // M37: the real dispatcher gates the whole effect behind a hit roll
    // (`power * 256 / (power + resistance)` against rand(0, 0x100)), so
    // with the rat's real resistance of 3 this assertion would hold only
    // ~97% of the time. Zeroing it makes the chance exactly 0x100, which
    // the engine's own `== 0x100` special case turns into a certainty --
    // the gate's own arithmetic is pinned in m37_spellpower_smoke instead,
    // where the real resistance value can be asserted without a cast.
    {
        skRValueArray zero;
        zero.append(skRValue(0));
        skRValue r;
        skExecutableContext c(&interpreter);
        rat->method(skString("SetMagicResistance"), zero, r, c);
    }
    int healthBefore = rat->currentHealth();
    blindRaw->InvokeHitTarget(rat.get());
    int healthAfter = rat->currentHealth();
    bool noDamageOk = healthBefore == healthAfter;
    std::printf("Azra_Rat health after spells/blind.s's real HitTarget(): %d -> %d (expected "
                "unchanged -- Blind is not a damage spell) %s\n",
                healthBefore, healthAfter, noDamageOk ? "OK" : "FAILED");
    if (!noDamageOk) ok = false;

    bool blindEffectOk =
        rat->blinded() &&
        rat->statModifier(sk_bindings::MonsterExecutable::kStatAttack) == -10 &&
        rat->statModifier(sk_bindings::MonsterExecutable::kStatDefense) == -10;
    std::printf("...and instead applies the real Blind effect (-10 attack, -10 defense, blind "
                "flag): %s\n",
                blindEffectOk ? "OK" : "FAILED");
    if (!blindEffectOk) ok = false;

    // --- Part 5: robustness -- a real non-spell item's InvokeHitTarget()
    // must be a harmless no-op (nothing defines HitTarget, so DoAttackRoll
    // never fires) -- confirms tryAttack's "anything non-weapon might be a
    // spell" design doesn't misfire against an ordinary item.
    skExecutableContext clubCtxt(&interpreter);
    std::unique_ptr<sk_bindings::ItemExecutable> club;
    try {
        club = std::make_unique<sk_bindings::ItemExecutable>(
            skString((std::string(scriptRoot) + "/weapons/club.s").c_str()), clubCtxt, stack);
        skRValueArray args = InitArgs();
        skRValue ret;
        skExecutableContext callCtxt(&interpreter);
        club->method(skString("Init"), args, ret, callCtxt);
    } catch (skParseException& e) {
        std::printf("m22_spell_smoke: FAILED -- PARSE ERROR: %s\n", e.toString().ptr());
        return 1;
    } catch (skRuntimeException& e) {
        std::printf("m22_spell_smoke: FAILED -- RUNTIME ERROR: %s\n", e.toString().ptr());
        return 1;
    }
    int healthBeforeClub = rat->currentHealth();
    club->InvokeHitTarget(rat.get());  // club.s defines no HitTarget -- must no-op
    bool noopOk = rat->currentHealth() == healthBeforeClub;
    std::printf("Azra_Rat health unchanged after a non-spell item's InvokeHitTarget(): %s %s\n",
                noopOk ? "true" : "false", noopOk ? "OK" : "FAILED");
    if (!noopOk) ok = false;

    std::printf("\nm22_spell_smoke: %s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
