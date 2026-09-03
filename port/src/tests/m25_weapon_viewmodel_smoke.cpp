// M25 (weapon viewmodel) smoke test: proves ItemExecutable's real
// SetWeaponSprite()/SetAnimationFrames() values (stored since M10, never
// read back until now -- see item_executable.h's weaponSprite()/
// animationFrames() comment) against real weapon scripts, and exercises
// the port-only WeaponViewmodel state machine (simkin_bindings/
// weapon_viewmodel.h) that turns those values into an animated swing.
//
// Real scripts, read directly off the install image:
//   weapons/club.s:            SetWeaponSprite(88);  SetAnimationFrames(5);
//   weapons/bandit_longbow.s:  SetWeaponSprite(175); SetAnimationFrames(4);
// A real spell (spells/blind.s, already used by m22_spell_smoke) never
// calls SetWeaponSprite() at all -- confirms StartWeaponSwing()'s no-op
// guard against a non-weapon item is exercised against a real script, not
// a synthetic one.
#include <cstdio>
#include <memory>
#include <string>

#include "assets/string_table.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/weapon_viewmodel.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"

namespace {

std::unique_ptr<sk_bindings::ItemExecutable> LoadAndInit(const std::string& fullPath,
                                                           skInterpreter& interpreter,
                                                           sk_bindings::MenuStack& stack) {
    skExecutableContext loadCtxt(&interpreter);
    auto obj =
        std::make_unique<sk_bindings::ItemExecutable>(skString(fullPath.c_str()), loadCtxt, stack);
    skRValueArray args;
    args.append(skRValue(0));  // placeholder for Init's "(s)" parameter
    skRValue ret;
    skExecutableContext callCtxt(&interpreter);
    obj->method(skString("Init"), args, ret, callCtxt);
    return obj;
}

}  // namespace

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    if (!strings.Load(std::string(scriptRoot) + "/stringtable.eng")) {
        std::printf("m25_weapon_viewmodel_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }

    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
    bool ok = true;

    // --- Part 1: real Init() values ---
    std::unique_ptr<sk_bindings::ItemExecutable> club, bow, spell;
    try {
        club = LoadAndInit(std::string(scriptRoot) + "/weapons/club.s", interpreter, stack);
        bow = LoadAndInit(std::string(scriptRoot) + "/weapons/bandit_longbow.s", interpreter, stack);
        spell = LoadAndInit(std::string(scriptRoot) + "/spells/blind.s", interpreter, stack);
    } catch (skParseException& e) {
        std::printf("m25_weapon_viewmodel_smoke: FAILED -- PARSE ERROR: %s\n", e.toString().ptr());
        return 1;
    } catch (skRuntimeException& e) {
        std::printf("m25_weapon_viewmodel_smoke: FAILED -- RUNTIME ERROR: %s\n", e.toString().ptr());
        return 1;
    }

    auto checkInt = [&](const char* label, int actual, int expected) {
        bool pass = actual == expected;
        std::printf("%s: %d (expected %d) %s\n", label, actual, expected, pass ? "OK" : "FAILED");
        if (!pass) ok = false;
    };
    checkInt("club.s weaponSprite()", club->weaponSprite(), 88);
    checkInt("club.s animationFrames()", club->animationFrames(), 5);
    checkInt("bandit_longbow.s weaponSprite()", bow->weaponSprite(), 175);
    checkInt("bandit_longbow.s animationFrames()", bow->animationFrames(), 4);
    bool spellNoSprite = spell->weaponSprite() < 0;
    std::printf("spells/blind.s weaponSprite() (never set by a real spell): %d (expected < 0) %s\n",
                spell->weaponSprite(), spellNoSprite ? "OK" : "FAILED");
    if (!spellNoSprite) ok = false;

    // --- Part 2: StartWeaponSwing() no-ops for a non-weapon item ---
    sk_bindings::WeaponViewmodel vm;
    sk_bindings::StartWeaponSwing(vm, spell.get());
    bool spellSwingNoOp = vm.item == nullptr;
    std::printf("StartWeaponSwing(spell): vm.item still null -- %s %s\n",
                spellSwingNoOp ? "true" : "false", spellSwingNoOp ? "OK" : "FAILED");
    if (!spellSwingNoOp) ok = false;

    // --- Part 3: the real payoff -- a real club.s swing runs through
    // every phase of the state machine and lands back at Idle. ---
    sk_bindings::StartWeaponSwing(vm, club.get());
    bool swingStarted = vm.item == club.get() &&
                         vm.phase == sk_bindings::WeaponViewmodel::Phase::Swinging && vm.frame == 0;
    std::printf("StartWeaponSwing(club): item set, phase=Swinging, frame=0 -- %s\n",
                swingStarted ? "OK" : "FAILED");
    if (!swingStarted) ok = false;

    // club.s's real animationFrames()==5 -- each frame takes
    // kViewmodelFrameTicks ticks, so 5*kViewmodelFrameTicks ticks later the
    // swing must have advanced through every real frame and transitioned
    // to Hold (never skipping straight to Idle -- a real swing always
    // pauses on its last frame first).
    int ticksToFinishSwing = club->animationFrames() * sk_bindings::kViewmodelFrameTicks;
    for (int i = 0; i < ticksToFinishSwing; ++i) sk_bindings::TickWeaponViewmodel(vm);
    bool reachedHold = vm.phase == sk_bindings::WeaponViewmodel::Phase::Hold;
    std::printf(
        "after %d ticks (club.s's real animationFrames()=%d * frame-tick duration): phase=Hold -- "
        "%s\n",
        ticksToFinishSwing, club->animationFrames(), reachedHold ? "OK" : "FAILED");
    if (!reachedHold) ok = false;

    for (int i = 0; i < sk_bindings::kViewmodelHoldTicks; ++i) sk_bindings::TickWeaponViewmodel(vm);
    bool reachedIdle = vm.phase == sk_bindings::WeaponViewmodel::Phase::Idle;
    std::printf("after the post-swing hold expires: phase=Idle -- %s\n",
                reachedIdle ? "OK" : "FAILED");
    if (!reachedIdle) ok = false;

    // Idle ticking must not touch item/phase -- only idleSwayTick advances.
    int swayBefore = vm.idleSwayTick;
    sk_bindings::TickWeaponViewmodel(vm);
    bool idleAdvances = vm.idleSwayTick == swayBefore + 1 &&
                         vm.phase == sk_bindings::WeaponViewmodel::Phase::Idle && vm.item == club.get();
    std::printf("idle ticking advances idleSwayTick without leaving Idle -- %s\n",
                idleAdvances ? "OK" : "FAILED");
    if (!idleAdvances) ok = false;

    std::printf("\nm25_weapon_viewmodel_smoke: %s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
