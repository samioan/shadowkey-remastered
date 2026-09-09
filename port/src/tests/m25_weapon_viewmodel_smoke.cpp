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
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/weapon_viewmodel.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"

namespace {

// M79: the real creation path first -- an item built straight from a script
// path never learns its entities.txt category, and the category is what
// picks the C++ class whose constructor writes a weapon's `+0x184` and a
// spell's `+0x19c`/`+0x180`. See simkin_bindings/game_constants.h.
std::unique_ptr<sk_bindings::ItemExecutable> LoadAndInit(const std::string& relPath,
                                                           skInterpreter& interpreter,
                                                           sk_bindings::MenuStack& stack) {
    const int typeId = stack.level().TypeIdForScript(relPath);
    if (typeId >= 0) {
        std::unique_ptr<sk_bindings::ItemExecutable> made = stack.level().CreateItem(typeId, true);
        if (made) return made;
    }
    const std::string fullPath = stack.scriptRoot() + "/" + relPath;
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

    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot)) {
        std::printf("m25_weapon_viewmodel_smoke: FAILED to load entities.txt\n");
        return 1;
    }

    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
    stack.level().SetEntityTypes(&entityTypes);
    bool ok = true;

    // --- Part 1: real Init() values ---
    std::unique_ptr<sk_bindings::ItemExecutable> club, bow, spell, armour;
    try {
        club = LoadAndInit("weapons/club.s", interpreter, stack);
        bow = LoadAndInit("weapons/bandit_longbow.s", interpreter, stack);
        spell = LoadAndInit("spells/blind.s", interpreter, stack);
        armour = LoadAndInit("armor/chain_coif.s", interpreter, stack);
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
    // M79: a spell *does* have a viewmodel, and this line used to assert
    // the opposite. No spell script calls SetWeaponSprite -- but the Spell
    // class constructor `FUN_10047740` writes slot 152 and five frames
    // before Init() ever runs, which is why a cast shows a pair of hands.
    checkInt("spells/blind.s weaponSprite() (the Spell ctor's 0x98)", spell->weaponSprite(), 152);
    checkInt("spells/blind.s animationFrames() (the Spell ctor's 5)", spell->animationFrames(), 5);
    checkInt("club.s reloadSpeed() (the Weapon ctor's 0x300)", club->reloadSpeed(), 0x300);
    checkInt("spells/blind.s reloadSpeed() (the base ctor's 0x100)", spell->reloadSpeed(), 0x100);

    // --- Part 2: StartWeaponSwing() no-ops for an item with no viewmodel ---
    //
    // Armour, not a spell: `FUN_1002e954` leaves `+0x19c`/`+0x180` at the
    // base item constructor's zeros, so there is nothing to animate. (This
    // check used a spell until M79, when spells turned out to have art.)
    sk_bindings::WeaponViewmodel vm;
    sk_bindings::StartWeaponSwing(vm, armour.get());
    bool armourSwingNoOp = vm.item == nullptr;
    std::printf("StartWeaponSwing(chain_coif): vm.item still null -- %s %s\n",
                armourSwingNoOp ? "true" : "false", armourSwingNoOp ? "OK" : "FAILED");
    if (!armourSwingNoOp) ok = false;

    // --- Part 3: a real club.s swing runs the state machine to
    // completion. ---
    //
    // M47 replaced this port's invented Swinging/Hold/Idle phases with the
    // real accumulator (`player+0x234`), so the shape checked here changed:
    // a swing starts at `(animationFrames + 2) << 8` for a melee weapon and
    // stops *drawing* once the accumulator reaches 0x200. The exact sprite
    // sequence is asserted in m47_weapon_swing_smoke; this only confirms
    // the machine still starts, runs and stops.
    //
    // M78: `swinging()` is the engine's own `0 < player->+0x234` now, which
    // is the question the attack gate asks -- so it stays true through the
    // half second after the last frame is drawn, while the accumulator
    // finishes draining and no new attack may start.
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
    // (5+2)*256 = 1792, falling by 120 a tick -- the frame delta times
    // four, times the Weapon constructor's `+0x184 = 0x300`. That is 11
    // ticks of visible frames and 4 more before the accumulator reaches 0
    // and the next attack is allowed: 15 ticks, 0.6s at 25Hz.
    //
    // **M79 corrected this from 45.** M78 read the base item constructor's
    // `+0x184 = 0x100` and missed the Weapon class constructor overwriting
    // it three instructions later, so every swing in this port ran at a
    // third of its real speed -- the reported "the weapon swinging
    // animation is really slow".
    bool swingRan = swingTicks == 15;
    std::printf("the club's swing locks out %d ticks (0.6s of engine clock) -- %s\n", swingTicks,
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
