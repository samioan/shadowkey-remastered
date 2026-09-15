#pragma once

// M95: `SetGhost(true)` -- what the player's ghost flag (`player+0x10a1`)
// actually changes, from its only two readers.
//
// ---- Reader 1: the entity push-out ------------------------------------
//
// Player vtable slot `+0x1f4` is `FUN_100455c8`, five instructions:
//
//     mov   r3, #0x1080
//     add   r3, r3, #0x21        ; +0x10a1
//     ldrb  r2, [r0, r3]
//     cmp   r2, #0
//     bxne  lr                   ; a ghost returns here
//     b     FUN_100017c8         ; the entity-vs-entity overlap resolve
//
// So a ghost is never pushed out of a door, a creature or a prop.
//
// ---- Reader 2: the actor move, `FUN_10000c64` --------------------------
//
// The move function the player's own tick ends in. For the player it
// quarters the velocity before moving (`v >>= 2`, towards zero), moves once
// by it, and then splits:
//
//     if (this == player && ghost) {
//         clamp z between the floor (FUN_1001beac) and the ceiling
//             (FUN_1001bdf4) of the tile it is over, probing at z + 0x180;
//         x += 6 * step;  y += 6 * step;        // six more, no tests at all
//     } else {
//         push-out (slot 0x1f4);
//         if (player) { FUN_100016e0: four more steps, each clamped to the
//                        map and pushed out;  v <<= 2; }
//         wall test (slot 0x1e8) -- blocked puts x/y back where they began
//     }
//
// Two things fall out of that, and the second is not what the cheat's name
// suggests.
//
//  * **No wall test and no entity test.** Walls, closed doors and props are
//    all walk-through; the floor and ceiling still hold, so a ghost follows
//    the ground rather than flying. This port's own vertical block already
//    clamps to exactly those two surfaces, so only the horizontal tests
//    stand down.
//
//  * **A ghost is slower, not faster.** Seven steps instead of five looks
//    like a speed-up, but the ghost arm never undoes the `v >>= 2`, and the
//    velocity is *persistent*: the forward move (`FUN_100065b4`) adds to it
//    and the friction in `FUN_10000498` (`v -= (v * 0x50 + 0xff) >> 8`, i.e.
//    x 0.6875) runs every tick before the move. So the quartering compounds.
//    Normal walking settles at `v = f*A / (1 - f)`, a ghost at
//    `v = f*A / (1 - f/4)`, and the distance ratio is
//
//        (7/4 * ghostV) / (5/4 * normalV) = 7/5 * (1 - f) / (1 - f/4) = 0.53
//
//    -- a ghost walks at about half speed. GhostMoveSpeedScale() below runs
//    the engine's own integer chain rather than the closed form, so the
//    rounding is the engine's.
//
// This is a derivation, not a device measurement: it assumes the friction
// arm is taken on every walking tick (it is gated on the actor being on
// something, `vtable[0x22c]`, which a ghost clamped to the floor always
// is). This port's own movement is a fixed step with no stored velocity
// (main.cpp, M7/M73), so like M73's exhaustion halving only the ratio is
// taken.

namespace sk_bindings {

namespace ghost_detail {

// One walking tick of the engine's player velocity chain, in the engine's
// own integer arithmetic. `v` carries across ticks; returns the distance
// moved this tick.
inline int WalkTick(int& v, int accel, int deltaUnits, bool ghost) {
    v += accel;                               // FUN_100065b4
    int p = v * 0x50;                          // FUN_10068814(v, 0x50)
    p = p < 0 ? p - 0xff : p + 0xff;
    v -= p >> 8;
    v = (v < 0 ? v + 3 : v) >> 2;              // FUN_10000c64's quartering
    const int step = (v * deltaUnits) >> 8;
    const int moved = (ghost ? 7 : 5) * step;  // 1 + six, or 1 + FUN_100016e0's four
    if (!ghost) v <<= 2;                       // only the normal arm restores it
    return moved;
}

}  // namespace ghost_detail

// Steady-state distance per tick of a ghost over a normal walker, holding
// forward. `accel` defaults to a Speed-50 character facing straight down an
// axis -- step `(50 << 8) >> 1`, times the sine table's top value `>> 3`,
// `>> 8` -- and the ratio barely moves with it (0.527..0.533 over two
// orders of magnitude), which is why a single number is fair to take.
inline float GhostMoveSpeedScale(int accel = 102336, int deltaUnits = 10) {
    int vNormal = 0, vGhost = 0;
    int dNormal = 0, dGhost = 0;
    constexpr int kSettleTicks = 64;  // normal settles in ~10, a ghost in 2
    for (int i = 0; i < kSettleTicks; ++i) {
        dNormal = ghost_detail::WalkTick(vNormal, accel, deltaUnits, false);
        dGhost = ghost_detail::WalkTick(vGhost, accel, deltaUnits, true);
    }
    return dNormal > 0 ? static_cast<float>(dGhost) / static_cast<float>(dNormal) : 1.0f;
}

}  // namespace sk_bindings
