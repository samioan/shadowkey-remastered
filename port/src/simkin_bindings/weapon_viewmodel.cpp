#include "simkin_bindings/weapon_viewmodel.h"

#include <cmath>
#include <cstdlib>

namespace sk_bindings {

namespace {

// `item+0x184` (SetReloadSpeed) scales the swing's per-frame decay:
//
//   delta = frameDelta * 4;
//   if (weapon->0x184 != 0x100) delta = weapon->0x184 * delta >> 8;
//
// **CORRECTED IN M79, and this is the reported "the swinging animation is
// really slow".** No shipped script calls SetReloadSpeed, so the value is
// whatever the constructor left -- and M78 read the wrong constructor. The
// *base item* one, `FUN_1006c960`, does write `+0x184 = 0x100`; the **Weapon
// class** constructor `FUN_1002ce9c` runs immediately after it and
// overwrites that with `0x300` (`1002cf7c: mov r3, #0x300; str r3, [r4,
// #0x184]`, read off the disassembly rather than the decompiler). Every
// weapon in the game is built by that constructor or by the conjured
// sword's, which derives from it, so **the scaled path is the only one a
// weapon ever takes** and a swing drains three times as fast as this
// constant alone says.
//
// The scale is a per-item field, so the answer differs by class:
//
//   weapon  0x300  ->  120/tick, (5+2)<<8 = 1792 units in **15 ticks, 0.6s**
//   spell   0x100  ->   40/tick, the same 1792 units in **45 ticks, 1.8s**
//
// Both are wall-clock constants rather than frame counts, because the drain
// is a multiple of the measured frame delta. A weapon's last ~3 ticks and a
// spell's last 13 draw nothing at all (see ResolveViewmodelDraw), and it is
// the accumulator reaching zero -- not the last drawn frame -- that lets the
// next attack through.
constexpr int kSwingDeltaPerFrame = kViewmodelFrameDeltaUnits * 4;  // 40

// The swap transition's own rate is this port's choice, and the one number
// here that is not decompiled.
//
// `FUN_1002b1b0` decrements `+0x238` by exactly 1 per call, and
// `FUN_1001d778` seeds it with 0x800 or 0x400. At the real 25Hz tick that
// is 82 or 41 seconds of showing the wrong weapon, which cannot be what
// was intended and would be very visible here. Either the draw hook runs
// far more often than the game tick, or the seed is in the engine's usual
// 0x100-per-second units and the decrement should be the frame delta.
// Unresolved; the port keeps the real seeds and picks a decrement that
// makes 0x800 last about a second.
constexpr int kSwapDeltaPerFrame = 0x800 / 25;

// Bob-phase units per world unit moved. A port constant -- see
// TickWeaponViewmodel(). 40 units/tick (this port's walk speed) times 6 is
// 240, so kSwayCycle (0x1400) takes ~21 ticks, a little under a second.
constexpr int kSwayUnitsPerWorldUnit = 6;

}  // namespace

int SwaySine(int index2048) {
    // The real table at 0x100f4954: 2048 int32 entries, `sin(2*pi*k/2048)`
    // in 8.8 fixed point. Regenerated rather than embedded (the binary is
    // not in this repo), and checked against the real bytes at four points
    // -- k=0 -> 0, k=256 -> 181, k=384 -> 237, k=512 -> 256, k=1536 -> -256.
    int k = index2048 & 2047;
    double s = std::sin(6.283185307179586 * static_cast<double>(k) / 2048.0);
    return static_cast<int>(std::lround(s * 256.0));
}

bool StartWeaponSwing(WeaponViewmodel& vm, ItemExecutable* item) {
    // `FUN_100425bc`'s gate, in its own order: a swap locks out a swing,
    // and so does a swing already in flight. Note there is no "hold" to
    // interrupt -- M25's post-swing hold was a misreading of the swap
    // timer, so the previous port's deliberately-interruptible Hold has no
    // counterpart in the real machine and is gone.
    if (vm.swapTimer > 0) return false;
    if (vm.swingAccum > 0) return false;
    if (!item || item->weaponSprite() < 0) return false;
    // `if (weapon != 0 && weapon->+0x180 != 0)` -- a script that never
    // called SetAnimationFrames has nothing to play.
    int frames = item->animationFrames();
    if (frames <= 0) return false;

    vm.item = item;
    vm.currentSprite = item->weaponSprite();
    if (!item->ranged()) {
        // The real roll, and the reason a melee weapon owns 16 slots: two
        // coin flips give variant 0 half the time, 10 a quarter, 5 a
        // quarter. Each variant is its own `frames`-long swing after the
        // base (idle) slot.
        if ((std::rand() & 1) == 0) {
            vm.swingVariant = (std::rand() & 1) == 0 ? 10 : 5;
        } else {
            vm.swingVariant = 0;
        }
        vm.swingAccum = (frames + 2) << 8;
    } else {
        // SetBow / SetCrossbow (`item+0x17a`) and SetThrowingWeapon
        // (`item+0x179`) both take this branch: no variant roll, and no
        // `+2`. See ResolveViewmodelDraw() for what dropping the `+2`
        // does to the frames a ranged weapon actually shows.
        vm.swingAccum = frames << 8;
    }
    vm.swayPhase = 0;
    return true;
}

void StartSpellSwing(WeaponViewmodel& vm) {
    // `FUN_10042394` case 2, after the cast has succeeded and its 3
    // fatigue is spent:
    //
    //   if (player->+0x204 && (n = weapon->+0x180) != 0)
    //       player->+0x234 = (n + 2) << 8;
    //
    // -- the plain melee seed, on whatever weapon is on screen, with no
    // gate in front of it.
    if (!vm.item) return;
    int frames = vm.item->animationFrames();
    if (frames <= 0) return;
    vm.swingAccum = (frames + 2) << 8;
}

PlayerAttackGate CheckPlayerAttackGate(WeaponViewmodel& vm, const ItemExecutable* activeWeapon,
                                       int speedStat, int frameDeltaUnits) {
    // `FUN_100425bc`'s first eleven instructions, in their own order.
    if (vm.attackCooldown > 0) {
        // No floor: the real subtraction can leave this negative and the
        // test above is `0 < x`, not `x != 0`.
        vm.attackCooldown -= frameDeltaUnits;
        return PlayerAttackGate::CoolingDown;
    }
    // Re-seeded before the two busy tests, so a key held through a swing
    // pays the cadence again on the far side of it.
    vm.attackCooldown = speedStat;
    if (activeWeapon != nullptr && activeWeapon->usesRangedPath()) {
        vm.attackCooldown = speedStat * 6;
    }
    if (vm.swapTimer > 0) return PlayerAttackGate::Swapping;
    if (vm.swingAccum > 0) return PlayerAttackGate::Swinging;
    return PlayerAttackGate::Allowed;
}

void NotifyWeaponChanged(WeaponViewmodel& vm, ItemExecutable* item) {
    // `FUN_1001d778(player, force=0)`: a swing in flight blocks the swap
    // (the real test also covers three other busy flags this port has no
    // equivalent for).
    if (vm.swingAccum > 0) return;
    int resolved = (item && item->weaponSprite() >= 0) ? item->weaponSprite() : -1;
    if (resolved == vm.currentSprite) {
        vm.item = item;
        return;
    }
    vm.prevSprite = vm.currentSprite;
    vm.item = item;
    vm.currentSprite = resolved;
    // `(+0x238 == 0 && +0x230 != -1) ? 0x800 : 0x400` -- the long form only
    // for a clean swap out of a weapon that was actually on screen.
    vm.swapTimer = (vm.swapTimer == 0 && vm.prevSprite != -1) ? kSwapTimerFull : kSwapTimerShort;
    vm.swayPhase = 0;
}

int SwingDrainPerFrame(const ItemExecutable* item) {
    // `if (weapon->+0x184 != 0x100) delta = weapon->+0x184 * delta >> 8;`
    // -- the engine's own test, including the fact that it reads the field
    // off `player+0x204` without a null check. Here a null item keeps the
    // unscaled rate rather than dereferencing nothing.
    int delta = kSwingDeltaPerFrame;
    if (item == nullptr) return delta;
    const int scale = item->reloadSpeed();
    if (scale != kDefaultReloadSpeed) delta = (scale * delta) >> 8;
    return delta;
}

void TickWeaponViewmodel(WeaponViewmodel& vm, int speed) {
    if (vm.swingAccum != 0) {
        vm.swingAccum -= SwingDrainPerFrame(vm.item);
        if (vm.swingAccum < 0) vm.swingAccum = 0;
        vm.swayPhase = 0;
        return;
    }
    if (vm.swapTimer != 0) {
        vm.swapTimer -= kSwapDeltaPerFrame;
        if (vm.swapTimer < 0) vm.swapTimer = 0;
        vm.swayPhase = 0;
        return;
    }
    // `FUN_1001f230(player, speed)`: the bob phase runs down at a rate
    // proportional to how fast the player is moving and wraps by adding a
    // whole cycle, so standing still freezes it wherever it stopped.
    //
    // The real decrement is `(frameDelta * (speed & 0xffff)) >> 8`. Its
    // `speed` argument's units are **not** recovered -- the call site was
    // not identified, and this port's own 40-world-units-per-tick walk fed
    // through that expression gives one bob every 5120 ticks (three and a
    // half minutes), while the constructor's plausible-looking speed field
    // (0x5a00) gives one every six. Neither is a walk cycle, so the rate
    // here is a port constant chosen to put a full cycle at a bit under a
    // second at 40 units/tick. Everything else about the bob -- the 0x1400
    // cycle, the wrap, the reset on swing and swap, the triangle x and the
    // sine y -- is decompiled.
    vm.swayPhase -= speed * kSwayUnitsPerWorldUnit;
    if (vm.swayPhase <= 0) vm.swayPhase += kSwayCycle;
}

ViewmodelDraw ResolveViewmodelDraw(const WeaponViewmodel& vm) {
    ViewmodelDraw draw;
    if (!vm.item) return draw;
    int base = vm.item->weaponSprite();
    if (base < 0) return draw;
    draw.spriteSlot = base;
    draw.visible = true;

    if (vm.swingAccum != 0) {
        // The swing branch. Nothing is drawn once the accumulator reaches
        // 0x200 -- that, not a frame counter, is what ends the *visible*
        // swing (the accumulator itself keeps draining to 0, and until it
        // gets there no new attack may start).
        //
        // **M78: `<=`, where the engine writes `if (iVar3 < 0x200)`.** The
        // frame index below is `(a * 0x100 - (acc - 0x200)) >> 8` with
        // `a = frames + variant + 1`, so it reaches `a` -- one slot past
        // the variant's own last frame -- at exactly `acc == 0x200` and
        // nowhere else. On the device the drain is the *measured* frame
        // delta (`app+0xd4`, clamped to 4..0x40) times its scale, so
        // landing on that single value is a coin toss. What it draws is a
        // real sprite from the *next* weapon's strip: a melee weapon owns
        // 16 consecutive slots (bases 72/88/104/120/136), the variant-10
        // run ends at base+15, and the overrun frame is base+16. That is
        // the reported "different weapon equipped on the last frame", and
        // it is why this is a `<=` and not a clamp -- one unit earlier,
        // every variant plays exactly its own frames.
        //
        // M79: which items can land on it moved when the drain rate did.
        // At a fixed frame delta the question is whether `1792 - 512 =
        // 1280` is a whole number of drains: it is for a **spell** (1280 /
        // 40 = 32 exactly, so every cast would show slot 158, the frame
        // after the spell strip's own five) and it is not for a weapon
        // (1280 / 120 is not an integer). So the guard's beneficiary is now
        // the cast animation rather than the sword swing -- it was needed
        // both times, for the same reason.
        if (vm.swingAccum <= kSwingAccumFloor) {
            draw.visible = false;
            return draw;
        }
        int a = vm.item->animationFrames() + vm.swingVariant + 1;
        draw.spriteSlot = base + (((a << 8) - (vm.swingAccum - kSwingAccumFloor)) >> 8);
        // x and y stay 0: a swing draws the frame flush at the top-left,
        // with no bob at all.
        //
        // Worked through for `weapons/club.s` (base 88, 5 frames), the
        // sequence is exactly 89,90,91,92,93 for variant 0, 94..98 for
        // variant 5 and 99..103 for variant 10 -- three five-frame swings
        // filling the weapon's 16-slot strip after the idle pose at 88.
        // Confirmed against the decoded sprites: each run is a visibly
        // distinct swing.
        //
        // A **ranged** weapon starts two frames in, because it does not get
        // the `+2`: `weapons/bandit_longbow.s` (base 175, 4 frames) shows
        // only 178 and 179, the last two slots of its 5-slot strip. Whether
        // that is a bug or a deliberate "no windup, straight to the
        // release" is not something the code says, so it is reproduced as
        // found rather than corrected.
        return draw;
    }

    if (vm.swapTimer != 0) {
        if (vm.swapTimer > kSwapShowOld) {
            // Still showing the weapon being put away.
            draw.spriteSlot = vm.prevSprite;
            if (draw.spriteSlot < 0) draw.visible = false;
        } else {
            // The new weapon, held low -- the real code's only use of a
            // non-zero blit y outside the bob.
            draw.y = kSwapRaiseY;
        }
        return draw;
    }

    // Idle/walking: the base (idle) frame, bobbed.
    //
    // x is a triangle wave off the phase, peaking at 10px halfway through
    // the cycle. y is |sin| of the same phase scaled 5/128, so also 0..10px.
    //
    // The real y index is built by a chain of shifts and masks
    // (`((((n - (n >> 0x10)) * 0x100 & 0xffff0000) + 0x40000000) >> 0x10)
    // + 0x4000 >> 3 & 0x1ffc`) that this port does **not** reproduce
    // literally: evaluating it by hand over the phase's actual range lands
    // within a few entries of the table's zero crossings for every input,
    // i.e. a bob of essentially zero, which cannot be the intent. What is
    // certain is the table (a real 2048-entry 8.8 sine, verified) and the
    // `* 5 >> 7` scale; the port takes the straightforward reading --
    // normalised phase into the table, quarter-turn shifted -- and this is
    // the one part of the viewmodel that is an interpretation.
    int phase = vm.swayPhase;
    if (phase <= 0) return draw;
    int x = phase >> 8;
    if (phase > 0xa00) x = 0x14 - x;
    draw.x = x;
    int index = ((phase * 2048) / kSwayCycle + 512) & 2047;
    int y = (SwaySine(index) * 5) >> 7;
    draw.y = y < 0 ? -y : y;
    return draw;
}

}  // namespace sk_bindings
