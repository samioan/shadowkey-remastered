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
// **M78 wired the swing to the attack.** M47 recovered the swing's state
// machine but left it a decoration: main.cpp started a swing on the attack
// key and then attacked regardless of what the swing said. In the real
// engine the swing *is* the attack's rate limit, and the fields below are
// the whole of it. `FUN_100425bc` opens with an eleven-instruction gate --
//
//     if (0 < player->+0xf48) { player->+0xf48 -= frameDelta(); return; }
//     player->+0xf48 = (s16)stats->+0x1c;                 // the Speed stat
//     if (player->+0x204 && player->+0x204->+0x1a7)       // SetRange > 0x400
//         player->+0xf48 = (s16)stats->+0x1c * 6;
//     if (0 < player->+0x238) return;                     // mid weapon-swap
//     if (0 < player->+0x234) return;                     // mid swing
//
// -- and everything a player attack does (the 4 fatigue, the swing, the
// target search, the sound, the damage, the arrow) is below it, in one
// call. See CheckPlayerAttackGate() and docs/WORLD_MODEL.md, "The weapon
// swing".

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
    // player+0xf48. M78. Lives on the player object in the real engine
    // like every other field here; kept with them for the same reason.
    // Counts down in the engine's usual 0x100-per-second units and is
    // **not** clamped at zero -- the real decrement runs it negative and
    // the gate's test is `0 < x`, so overshoot is simply free time.
    int attackCooldown = 0;
    // item+0x17f. Lives on the *Item* in the real engine, but nothing else
    // reads it and an Item is only ever swung by the player, so keeping it
    // here avoids widening ItemExecutable for one transient.
    int swingVariant = 0;

    // The engine's own test, `if (0 < player->+0x234)`: a swing is
    // running until its accumulator reaches zero, which is a good half
    // second after the last frame it draws (see ResolveViewmodelDraw).
    bool swinging() const { return swingAccum > 0; }
    // The narrower question the draw asks -- is a swing frame on screen?
    bool swingVisible() const { return swingAccum > kSwingAccumFloor; }
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

// `FUN_10042394` case 2's tail: a spell that casts successfully re-arms
// the swing from whatever weapon is currently on screen. No gate of any
// kind (it overwrites a swing in flight), no variant roll, and the `+ 2`
// unconditionally -- a bow in hand does not take the ranged seed here the
// way StartWeaponSwing gives it. Reproduced as found.
void StartSpellSwing(WeaponViewmodel& vm);

// `FUN_100425bc`'s opening gate, which is the whole of the player's attack
// cadence. Call it once per tick per attack key that is **held** -- the
// real input handler reads bound buttons 0xf (right hand) and 0xe (left)
// with the plain `InputState_GetBoundButton`, not the edge form the
// jump/use keys beside it use, so the cooldown below advances in real time
// only while the key is down.
//
// `activeWeapon` is `player+0x204` as the caller has just set it: the item
// in the hand whose key is down (`FUN_10042e44`, player vtable +0x274,
// runs one line before the attack). Only its ranged flag is read, and it
// is `+0x1a7` (SetRange above 0x400) -- **not** the `+0x179`/`+0x17a`
// bow/thrown pair StartWeaponSwing tests. The two disagree for nothing in
// the shipped corpus, but they are different fields and the engine picks
// deliberately between them.
//
// The cooldown is re-seeded on every call that gets past it, before the
// swap and swing tests, so holding the key through a swing keeps re-arming
// it: the tick that finally sees an empty accumulator still owes one
// cooldown before it swings again.
enum class PlayerAttackGate {
    CoolingDown,  // `0 < +0xf48` -- the cadence, one Speed's worth
    Swapping,     // `0 < +0x238` -- a weapon change is on screen
    Swinging,     // `0 < +0x234` -- the previous swing has not run out
    Allowed,
};
PlayerAttackGate CheckPlayerAttackGate(WeaponViewmodel& vm, const ItemExecutable* activeWeapon,
                                       int speedStat, int frameDeltaUnits);

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
