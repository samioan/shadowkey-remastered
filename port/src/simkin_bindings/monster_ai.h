#pragma once

// M77 -- the creature AI tick, transcribed.
//
// M31/M32/M35 recovered the AI *parameters* (the scaled-squared distance
// convention, the package field, the attack cadence, the paralysis
// lockout) and M76 recovered the OnDetect arm. What none of them recovered
// is the single fact that decides whether any of it ever runs:
//
//     A PLACED CREATURE DOES NOT START ASLEEP. IT STARTS IN PACKAGE 2.
//
// `FUN_100815e0`, the actor constructor, sets `monster+0x2a8 = -1`, and
// this port copied that as `m_AiPackage = kAiAsleep`. But the constructor
// is not the last word. Entity vtable slot `+0x10` is the "attach to the
// engine" virtual, and the creature classes override it:
//
//     vtable 0x100fe2b8 (entities.txt category 2, every creature)  -> FUN_10086f9c
//     vtable 0x100fee0c (category 13, no shipped placement)        -> FUN_10086f9c
//     vtable 0x100ffaf4 (category 7)                               -> FUN_100866c0
//
// (Slot +0x10 resolved the usual way: `vtable+0x3c` holds FUN_10064c60 in
// all 31 entity vtables, which fixes each address point; only these three
// of the 31 name either function.) Both overrides end:
//
//     engine->actorRegistry[self->slot] = self;      // engine+0x14620
//     self->+0x5e  = 0x100;
//     self->+0x2a8 = 2;                              // <-- the AI package
//     self->+0x1d4 = 10;
//
// and `GameEngine_InitLevel` calls it on every single placement, one line
// after the factory allocates the entity and *before* the placement record
// or the entity's script are loaded (0x10018b58-ish: `factory(...)`, then
// `vtable[0x10](entity, engine)`, then `vtable[0x134](entity, reader)`).
// So the script's own `Init()` always runs on a creature that is already
// in package 2, and `-1` is only ever seen by an actor that was
// constructed and never placed.
//
// That one word is the difference between a level full of monsters and a
// level full of statues. Package 2 is the *only* state in which the tick
// evaluates perception, and 318 shipped scripts call `SetAggressive(true)`
// while only 38 call `AiDetect()` -- so 280 of them have no line anywhere
// that would have woken this port's creature up. The one family that did
// work (the Azra rats) worked by accident: `monsters/Azra_Rat.s` happens
// to call `AiDetect()`, which sets the same 2 the spawn already set.
//
// ---------------------------------------------------------------------
// The tick's own shape (FUN_10082224), in its own order
// ---------------------------------------------------------------------
//
//   if (self->+0x48 /*destroyed*/)           return;
//   if (self->+0x1e4 /*dead*/)               { play the death clip; return; }
//   ... the timed-effect list, the fear countdown (+0x300 -> restore
//       +0x2fc), the lifespan countdown (+0x2ec, at 3x the frame delta),
//       the paralysis countdown (+0x294) ...
//   if (self->+0x2ee /*SetCanTeleport*/) {
//       if (!target && self->+0x266 /*SetBoss*/) target = engine->player;
//       if (dist(self, target) > self->+0x2b8) FUN_10086a18(self);   // teleport to it
//   }
//   if (target && !self->+0x2bc /*SetImmobile*/) { moveGoal = target position; }
//   if (!self->+0x2bc) vtable[0x1b4](self);          // the movement step
//   self->+0x2c4 += FUN_1001afa4(engine);            // the attack cadence, EVERY tick
//
//   if (package == 3 && target) {                    // ---- pursue/attack
//       if (self->+0x294 == 0) vtable[0x208](self, target->x, target->y);   // face it
//       d = vtable[0x5c](self, target);
//       if (d < self->+0x2dc) { stop moving; return to the idle pose; }
//       if (0x100 < self->+0x2c4) {                  // <-- once a second
//           dz = |self->z - target->z|;
//           ranged = equippedWeapon->isLongRange || self->+0x310 /*a spell*/
//                    || self->+0x2c2 == 0xe1 /*a bow*/;
//           if (d < self->+0x2dc && (dz < 0x200 || ranged)) {
//               reach = ranged ? true
//                              : FUN_10082004(self, target, +0x2dc >> 8, ..., 1);
//               if (reach) {
//                   if (self->+0x266 /*boss*/) {          // the second throttle
//                       now = engine->clock;
//                       if (self->+0x2c8 == 0 ||
//                           self->+0x2c8 + self->+0x2cc * 0x100 < now) self->+0x2c8 = now;
//                       else                                            suppress;
//                   }
//                   vtable[0x240](self, target);          // FUN_100835b8 -- the swing
//               }
//           }
//           else if (self->+0x2b8 < d) { target = 0; package = 2; idle pose; }
//           else                       { self->+0x1b9 = 1;  /* keep closing */ }
//           self->+0x2c4 = rand & 0x1f;
//       }
//   }
//   else if (package == 2 || (package == 3 && !target)) {   // ---- look
//       candidate = engine->player;                  // the only one in singleplayer
//       d = vtable[0x5c](self, candidate);
//       if (package == 2) package = 3;               // (target is still 0 -- same arm)
//       noticed = true;
//       if (d < self->+0x2b8) noticed = the perception roll (see on_detect.h);
//       if (d < self->+0x2b8 && noticed) {
//           if (!self->+0x2ac) { ... OnDetect, see on_detect.h ... }
//           else               { target = candidate; package = 3; walk clip; }
//           self->+0x2c4 = 0;
//       }
//   }
//
// Packages 4 (`AiFlee`), 5 (`AiPursue`) and 6 (`AiSpellAssistTarget`) have
// no arm at all, and neither does -1 (`AiSleep`) -- a creature left in any
// of them keeps its move goal and its timers and does nothing else. That
// is reproduced, not invented.
//
// ---------------------------------------------------------------------
// Three things the port had backwards, and one it never had
// ---------------------------------------------------------------------
//
// 1. **Acquisition is not sight-gated.** WORLD_MODEL.md's older "Aggro is
//    gated on line of sight, not distance alone" was wrong, and this port
//    followed it. The reach raycast (`FUN_10082004`) is in the *attack*
//    test, and the eye-to-eye march (`vtable[0x21c]`, M76) is in the
//    *OnDetect* arm; the aggressive acquire arm has neither. Its only
//    gates are the scaled-squared distance against `SetChaseRadius` and
//    the perception roll. A creature within 8.4 tiles (the corpus's
//    dominant `SetChaseRadius(18000)`) comes for you through a wall, and
//    that is the original's behaviour, not a bug in it.
//
// 2. **Giving up is one line.** `if (chaseRadius < d) { target = 0;
//    package = 2; }`, evaluated only on a cadence tick. There is no
//    lost-sight grace period; this port's `kLoseInterestTicks` was its
//    own invention and is gone.
//
// 3. **The cadence gates the decision, not the damage.** This port
//    accumulated `+0x2c4` only while already standing in range and used
//    it purely to rate-limit the damage roll, while re-asserting the
//    swing clip on every one of the 25 ticks in between -- which is why
//    creatures looked like they were attacking continuously even though
//    they only landed a blow once a second. In the engine the swing clip
//    is played *by the attack*, once, with `PlayAnimation(swing, 1, idle,
//    0xf00)` -- mode 1 being "play through once, then hand over to this
//    other clip". Between swings a creature in range stands in its idle
//    pose.
//
// 4. **The boss throttle.** `SetBoss` (`monster+0x266`) arms a second,
//    independent gate on the game clock: a boss may swing only once every
//    `SetAttackSpeed` seconds, `monster+0x2cc`, defaulting to 2 from the
//    constructor. No shipped script calls `SetAttackSpeed`, so all 15
//    `SetBoss(true)` creatures use the default -- a boss attacks half as
//    often as everything else. This port had neither binding.

namespace sk_bindings {

// The AI package `vtable+0x10` writes into every placed creature, before
// its script's Init() gets a say. See the header comment.
constexpr int kSpawnAiPackage = 2;

// `if (dz < 0x200 || ranged)` -- how far above or below a creature its
// target may be and still be swung at, in raw world units (2 tiles).
constexpr int kAttackVerticalLimitUnits = 0x200;

// `self->+0x2c2 == 0xe1`: SetAttachedWeapon(225) is the bow. All 27
// shipped archers set it, and it is one of the three terms that exempt a
// creature from the melee reach raycast and the vertical limit above.
constexpr int kBowAttachedWeaponModel = 225;

// `monster+0x2cc`, the constructor's own value. Multiplied by 0x100 (the
// clock's units-per-second) it is the minimum gap between a boss's swings.
constexpr int kDefaultAttackSpeedSeconds = 2;

// The boss gate, exactly as the tick writes it:
//
//     if (last == 0 || last + speed * 0x100 < now) { last = now; attack; }
//     else                                         { do not attack; }
//
// `nowUnits` is the same 1/256-second clock every other timer here uses.
// Returns true when the swing is allowed; `lastUnits` is updated in place.
inline bool BossAttackDue(int& lastUnits, int attackSpeedSeconds, int nowUnits) {
    if (lastUnits == 0 || lastUnits + attackSpeedSeconds * 0x100 < nowUnits) {
        lastUnits = nowUnits;
        return true;
    }
    return false;
}

}  // namespace sk_bindings
