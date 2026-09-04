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

#include "assets/sprite_archive.h"
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

    // --- Part 3: a real club.s swing runs the state machine to
    // completion. ---
    //
    // M47 replaced this port's invented Swinging/Hold/Idle phases with the
    // real accumulator (`player+0x234`), so the shape checked here changed:
    // a swing starts at `(animationFrames + 2) << 8` for a melee weapon and
    // ends when the accumulator drops below 0x200. The exact sprite
    // sequence is asserted in m47_weapon_swing_smoke; this only confirms
    // the machine still starts, runs and stops.
    sk_bindings::StartWeaponSwing(vm, club.get());
    bool swingStarted = vm.item == club.get() && vm.swinging() &&
                         vm.swingAccum == ((club->animationFrames() + 2) << 8);
    std::printf("StartWeaponSwing(club): item set, accumulator = (5+2)<<8 = %d -- %s\n",
                vm.swingAccum, swingStarted ? "OK" : "FAILED");
    if (!swingStarted) ok = false;

    int swingTicks = 0;
    while (vm.swinging() && swingTicks < 1000) {
        sk_bindings::TickWeaponViewmodel(vm, 0);
        ++swingTicks;
    }
    // (5+2)*256 = 1792, falling by 40 a tick, stops being drawn below 512.
    bool swingRan = swingTicks == 33;
    std::printf("the club's swing runs %d ticks (~1.3s at 25Hz) then stops -- %s\n", swingTicks,
                swingRan ? "OK" : "FAILED");
    if (!swingRan) ok = false;

    // Ticking with the player standing still must not start the bob.
    for (int i = 0; i < 5; ++i) sk_bindings::TickWeaponViewmodel(vm, 0);
    bool idleStable = !vm.swinging() && !vm.swapping() && vm.item == club.get();
    std::printf("idle ticking leaves the equipped weapon on screen and nothing running -- %s\n",
                idleStable ? "OK" : "FAILED");
    if (!idleStable) ok = false;

    // --- Part 4 (M30): the real club's viewmodel art actually resolves.
    // The bug this guards: the viewmodel used to be attached to the
    // WeaponViewmodel only inside StartWeaponSwing(), so equipping a weapon
    // showed nothing until you attacked -- and nothing at all if the weapon
    // sat in the hand whose attack key you weren't pressing. main.cpp now
    // points vm.item at the equipped weapon every idle tick, which is only
    // useful if the sprite the script names really decodes.
    bool spriteOk = club->weaponSprite() == 88 && club->animationFrames() == 5;
    std::printf("real weapons/club.s: SetWeaponSprite(88)/SetAnimationFrames(5) stored -- %s\n",
                spriteOk ? "OK" : "FAILED");
    if (!spriteOk) ok = false;

    sk::SpriteArchive sprites;
    if (sprites.Load(scriptRoot)) {
        // The weapon-sprite slots live in the per-zone manifest, which is
        // what the game loads on entering a zone.
        sprites.LoadCategory(scriptRoot, "azra");
        const sk::Sprite* base = sprites.GetSprite(club->weaponSprite());
        bool baseOk = base != nullptr && base->width > 0 && base->height > 0;
        std::printf("global.spr slot %d (the club's viewmodel art) decodes -- %s\n",
                    club->weaponSprite(), baseOk ? "OK" : "FAILED");
        if (!baseOk) ok = false;

        // M47: the real swing draws base+1..base+5 for variant 0, and the
        // variant-5/variant-10 swings run to base+15 -- a melee weapon owns
        // a 16-slot strip, not `animationFrames()` slots from the base.
        bool framesOk = true;
        for (int f = 0; f <= 15; ++f) {
            if (!sprites.GetSprite(club->weaponSprite() + f)) framesOk = false;
        }
        std::printf("the club's whole 16-slot strip (slots %d..%d) decodes -- %s\n",
                    club->weaponSprite(), club->weaponSprite() + 15, framesOk ? "OK" : "FAILED");
        if (!framesOk) ok = false;
    } else {
        std::printf("global.spr not loadable -- skipping viewmodel art checks\n");
    }

    std::printf("\nm25_weapon_viewmodel_smoke: %s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
