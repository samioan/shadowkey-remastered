#pragma once

// First-person weapon viewmodel.
//
// M25 decompiled the per-tick draw function (`FUN_1002b1b0`, sole callee of
// `FUN_10029cb0`, the ScreenModeController tick hook `RenderHud`'s comment
// documents) and recovered the struct *shape*, but left the actual trigger
// open: "the exact native call that starts a swing (writes +0x230/+0x234/
// +0x238) wasn't found despite tracing every reachable write site from the
// Weapon class dispatcher."
//
// M46's technique found it in one pass: grep the decompiled corpus for the
// *write* text (`"0x234) ="` and friends) rather than tracing outward from
// a dispatcher. Five functions write those fields, and the state they
// belong to is the **player object itself** (`engine+0x618`), not a
// separate view struct.
//
//   * `FUN_100425bc(player)` -- the player's real attack function, and the
//     primary trigger. Not reachable by callers-of search: it is dispatched
//     virtually.
//   * `FUN_10042394(player, item)` -- "use item at a target"; the spell
//     branch starts the same swing after the cast succeeds.
//   * `FUN_1001d880(player)` -- player vtable **+0x190**, a second, bare
//     "start a swing" entry with the same gate.
//   * `FUN_1001d778(player, force)` -- player vtable **+0x194**, the
//     **weapon swap**, which is what `+0x230`/`+0x238` really are.
//   * `FUN_1001f230(player, speed)` -- the walk-bob phase (`+0x244`).
//
// **M25's reading of two fields was wrong**, and this file now follows the
// decompiled one. `+0x230` is not an "alternate/swing sprite" and `+0x238`
// is not a "post-swing hold": they are the *previous* weapon's viewmodel
// sprite and a **weapon-swap transition timer**. A swing has no hold phase
// at all -- it ends when its accumulator runs out.
//
// The real fields, with the port's names:
//
//   player+0x204  activeWeapon      the Item whose art is drawn
//   player+0x22c  currentSprite     its SetWeaponSprite() slot, cached
//   player+0x230  prevSprite        the sprite the *previous* weapon used
//   player+0x234  swingAccum        counts DOWN; nonzero == a swing is running
//   player+0x238  swapTimer         counts down 0x800/0x400 -> 0 across a swap
//   player+0x244  swayPhase         walk bob, counts down 0x1400 -> 0, wraps
//   player+0xf48  attackCooldown    the player's attack cadence
//   item+0x17f    swingVariant      0 / 5 / 10, re-rolled per swing
//   item+0x180    SetAnimationFrames
//   item+0x184    SetReloadSpeed, 8.8 scale on the swing's decay rate
//   item+0x19c    SetWeaponSprite
//   item+0x17a    SetBow            (also SetCrossbow -- one field)
//   item+0x179    SetThrowingWeapon
//
// See docs/WORLD_MODEL.md, "The weapon swing".

#include <cstdint>

#include "simkin_bindings/item_executable.h"

namespace sk_bindings {

// The engine's per-frame delta, 0x100 units to the second at the real 25Hz
// tick -- the same `FUN_1001afa4` value the monster attack cadence and the
// AI packages already use (`kAiFrameDeltaUnits`).
constexpr int kViewmodelFrameDeltaUnits = 10;

// `if (acc < 0x200)` in the draw function: below this nothing is drawn at
// all, which is what ends a swing.
constexpr int kSwingAccumFloor = 0x200;

// `FUN_1001d778`: 0x800 when a fresh swap starts from a settled state,
// 0x400 when one is already running or there was no previous weapon. The
// draw shows the *old* weapon while the timer is above kSwapShowOld and
// the *new* one, dropped kSwapRaiseY pixels, below it.
constexpr int kSwapTimerFull = 0x800;
constexpr int kSwapTimerShort = 0x400;
constexpr int kSwapShowOld = 0x400;
constexpr int kSwapRaiseY = 0x7c;  // 124px

// `FUN_1001f230`: the bob phase counts down and wraps by adding 0x1400.
constexpr int kSwayCycle = 0x1400;

struct WeaponViewmodel {
    ItemExecutable* item = nullptr;  // player+0x204
    int currentSprite = -1;          // player+0x22c
    int prevSprite = -1;             // player+0x230
    int swingAccum = 0;              // player+0x234
    int swapTimer = 0;               // player+0x238
    int swayPhase = 0;               // player+0x244
    // item+0x17f. Lives on the *Item* in the real engine, but nothing else
    // reads it and an Item is only ever swung by the player, so keeping it
    // here avoids widening ItemExecutable for one transient.
    int swingVariant = 0;

    bool swinging() const { return swingAccum >= kSwingAccumFloor; }
    bool swapping() const { return swapTimer > 0; }
};

// `FUN_100425bc`'s swing half, which `FUN_1001d880` (vtable +0x190) and
// `FUN_10042394` share:
//
//   if (player->swapTimer > 0) return;          // mid weapon-swap
//   if (player->swingAccum > 0) return;         // already swinging
//   if (!item || item->animationFrames == 0) return;
//   if (!item->throwingWeapon && !item->bow) {
//       // 50% / 25% / 25%
//       item->swingVariant = (rand&1) ? 0 : ((rand&1) ? 5 : 10);
//       player->swingAccum = (item->animationFrames + 2) << 8;
//   } else {
//       player->swingAccum = item->animationFrames << 8;
//   }
//   player->swayPhase = 0;
//
// The three variants are the reason a melee weapon owns **16 consecutive
// global.spr slots**: the base slot is the idle pose and each variant is
// its own five-frame swing after it. For `weapons/club.s`
// (SetWeaponSprite(88), SetAnimationFrames(5)) that is 88 idle, then
// 89-93 / 94-98 / 99-103 -- three visibly different swings, confirmed by
// decoding the real sprites.
//
// Returns true if a swing actually started (the gate let it through).
bool StartWeaponSwing(WeaponViewmodel& vm, ItemExecutable* item);

// `FUN_1001d778`, player vtable +0x194. Call whenever the item whose art
// should be on screen changes (equip, unequip, swapping hands). Starts the
// swap transition only when the resolved sprite actually differs, which is
// what keeps re-equipping the same weapon from flickering.
void NotifyWeaponChanged(WeaponViewmodel& vm, ItemExecutable* item);

// The state half of `FUN_1002b1b0`, plus `FUN_1001f230`'s phase. `speed`
// is the player's current movement speed in the same 8.8 world units the
// rest of the port uses; 0 while standing still, which freezes the bob.
void TickWeaponViewmodel(WeaponViewmodel& vm, int speed);

// The draw half of `FUN_1002b1b0`, resolved to a sprite slot and a
// top-left blit position. `visible == false` means the real function's
// `iVar5 == -1` case: draw nothing this frame.
//
// Every weapon viewmodel sprite in global.spr is a full 176x208 frame, and
// the real blit puts it at (0,0) -- these are full-screen overlays, not a
// small icon in a corner.
struct ViewmodelDraw {
    bool visible = false;
    int spriteSlot = -1;
    int x = 0, y = 0;
};
ViewmodelDraw ResolveViewmodelDraw(const WeaponViewmodel& vm);

// The engine's own sine table (0x100f4954): 2048 int32 entries at stride 4,
// 8.8 fixed point, `sin(2*pi*k/2048) * 256`. Verified against the real
// bytes -- k=512 is exactly 256, k=256 is 181 (0.7071*256), k=1536 is -256.
// Exposed so the smoke test can check the bob against it directly.
int SwaySine(int index2048);

}  // namespace sk_bindings
