#pragma once

// M130: the player's walk, turn and look, transcribed -- replacing the
// fixed `kMoveSpeed = 40`, `kTurnSpeed = 0.06` and `kPitchSpeed = 0.05`
// this port carried since M7.
//
// ---- Where it lives in the engine --------------------------------------
//
// `FUN_1001c9c0` is the player's per-frame input poll. For the walk it
// does, in this order:
//
//     step = vtable[0x1e4]()            FUN_100445d4, below
//     (held item's +0x198, `SetWielderMovement`, scales it; no shipped
//      script calls that native, so it is always 0x100 = unscaled)
//     look keys  -> vtable[0x1c4/0x1c8] set the look target  (+0x1a8)
//                   vtable[0x1c0]       ease the look rate    (+0xaa)
//     turn keys  -> vtable[0x1b8/0x1bc] set the turn target  (+0x1a4)
//                   vtable[0x1b4]       ease the turn rate    (+0xb8)
//     move keys  -> vtable[0x1cc..0x1d8](step): add `step` along / against
//                   / across the heading to the velocity (+0x98, +0xa0)
//     FUN_10000498: friction (0x50/0x100) on the velocity, then
//     vtable[0x1ec] = FUN_10000c64, the actor move.
//
// The six turn/look slots are reachable only through the player's vtable
// (0x100fd044), which is why they were missing from the decompile until
// the 2026-11-11 audit dumped the vtable-only functions.
//
// ---- The actor move, for the player ------------------------------------
//
//     v >>= 2 (towards zero)                 quartered for the sub-steps
//     pos += v * delta >> 8                  step 1
//     heading += turnRate * delta >> 8
//     pitch   += lookRate * delta >> 8, clamped to +-0x2000 (45 degrees)
//     clamp pos to the map by the radius
//     ghost:  six more steps, nothing tested, v stays quartered (M95)
//     else:   push-out (FUN_100017c8); four more steps, each clamped and
//             pushed out (FUN_100016e0); v <<= 2;
//             on the ground: the tile rule (FUN_10000ab8) at the centre --
//             blocked puts the position back where the tick began, and a
//             velocity component the push-out zeroed restarts at half its
//             old value.
//
// The push-out is what a wall is: four probes, `x - r`, `x + r`, `y - r`,
// `y + r` with `r = +0x8e = 0x80` (written by the player's init,
// FUN_1001d3e0), each asking the tile rule about the tile it lands in. A
// blocked probe zeroes that axis of the velocity and snaps the position so
// the probe sits on the tile boundary. Then the entities linked into the
// mover's own tile are box-tested and resolved one axis at a time.
//
// The tile rule also refuses a tile that has a solid, non-passable entity
// linked into it. So a solid thing owns its whole tile: the player's box
// cannot enter it.
//
// ---- The numbers --------------------------------------------------------
//
// Speed 30 / 35 / 40 / 50 settle at 50 / 55 / 65 / 85 world units a tick
// (half that with an empty fatigue pool), reached over a few ticks and
// coasting to a stop over a few more. The turn eases from 0x400 by x1.5 a
// frame to `2 * +0x18c`; `+0x18c` is 0x3c00 -- the player's init
// (FUN_1001d3e0 -> FUN_10006938) runs after the constructor that wrote
// 0x2ee00, and 0x7800 is the largest doubling that still fits the s16 rate
// field. That is 1200 heading units a tick, 165 degrees a second. The look
// rate eases an eighth of the way to 0x1e00 a frame (0x2d00 in a
// multiplayer session), 41 degrees a second.

#include <cstdint>
#include <functional>
#include <vector>

namespace sk_bindings {

int SwaySine(int index2048);  // weapon_viewmodel.h: the engine's sine table

struct PlayerMotion {
    int x = 0, y = 0;      // +0x94 / +0x9c, world units (256 a tile)
    int vx = 0, vy = 0;    // +0x98 / +0xa0
    int turnRate = 0;      // +0xb8, s16
    int turnTarget = 0;    // +0x1a4
    int lookRate = 0;      // +0xaa, s16
    int lookTarget = 0;    // +0x1a8
};

constexpr int kPlayerRadiusUnits = 0x80;     // FUN_1001d3e0: +0x8e
constexpr int kPlayerTurnBase = 0x3c00;      // FUN_10006938: +0x18c
constexpr int kPlayerLookRate = 0x1e00;      // FUN_10006938: +0x190, single player
constexpr int kPlayerLookRateSession = 0x2d00;  // ... inside engine+0x5c0
constexpr int kPitchLimitUnits = 0x2000;     // FUN_10000c64's clamp on +0xa8
constexpr int kPlayerMoveFriction = 0x50;    // FUN_10000498

// `FUN_100445d4`: the step every move slot is handed.
inline int PlayerMoveStep(int speed, int fatigue) {
    int step = static_cast<int16_t>(speed) << 8;
    step = fatigue < 1 ? step >> 2 : step >> 1;
    return step > 0x3fff ? 0x4000 : step;
}

// `VFUN_1001f43c` / `VFUN_1001f4b4`: the turn rate a held key asks for.
// `zoomed` is `engine+0x608 != 0x100 && player+0x234 == 0`.
inline int PlayerTurnTarget(bool zoomed) {
    return zoomed ? kPlayerTurnBase >> 1 : kPlayerTurnBase * 2;
}

// `VFUN_1001f530` (vtable +0x1b4): ease the turn rate to its target.
inline int RampTurnRate(int rate, int target) {
    rate = static_cast<int16_t>(rate);
    int out = rate;
    if (rate != target) {
        if (target == 0) {
            const int mag = rate < 0 ? -rate : rate;
            out = 0;
            if (mag > 1) out = rate < 1 ? -(mag >> 1) : rate >> 1;
        } else if (rate < target) {
            if (rate < 0x400) rate = 0x400;
            out = (rate * 0x180) >> 8;
            if (target <= out) out = target;
        } else {
            if (rate > -0x400) rate = -0x400;
            if (rate < 0) rate = -rate;
            out = -((rate * 0x180) >> 8);
            if (out < target) out = target;
        }
    }
    return static_cast<int16_t>(out);
}

// `VFUN_1001f3e0` (vtable +0x1c0): ease the look rate to its target.
inline int RampLookRate(int rate, int target) {
    rate = static_cast<int16_t>(rate);
    if (rate == target) return rate;
    if (target == 0) {
        const int mag = rate < 0 ? -rate : rate;
        if (mag < 2) return 0;
        return static_cast<int16_t>(rate + ((-rate) >> 1));
    }
    return static_cast<int16_t>(rate + ((target - rate) >> 3));
}

enum class MoveDir { Forward, Backward, StrafeLeft, StrafeRight };

// The four base move slots: `FUN_100065b4` (forward), `FUN_10006510`
// (backward), `FUN_10006480` (heading + 0x4000, subtracted: left) and
// `FUN_100063f0` (the same, added: right). The table value is taken
// `>> 3`, the product `>> 8`.
inline void PlayerThrust(PlayerMotion& m, int headingUnits, int step, MoveDir dir) {
    int h = headingUnits;
    if (dir == MoveDir::StrafeLeft || dir == MoveDir::StrafeRight) h += 0x4000;
    h &= 0xffff;
    const int s = SwaySine(h >> 5) >> 3;
    const int c = SwaySine(((h + 0x4000) & 0xffff) >> 5) >> 3;
    const int ax = ((step & 0xffff) * s) >> 8;
    const int ay = ((step & 0xffff) * c) >> 8;
    if (dir == MoveDir::Forward || dir == MoveDir::StrafeRight) {
        m.vx += ax;
        m.vy += ay;
    } else {
        m.vx -= ax;
        m.vy -= ay;
    }
}

// `FUN_10068814(v, 0x50)` as `FUN_10000498` stores it (through an s16).
inline int PlayerFriction(int v) {
    int p = v * kPlayerMoveFriction;
    p = p < 0 ? p - 0xff : p + 0xff;
    return static_cast<int16_t>(v - (p >> 8));
}

struct MoveBox {
    int x = 0, y = 0, halfX = 0, halfY = 0;
};

struct PlayerMoveWorld {
    int mapWidth = 0, mapHeight = 0;  // tiles
    // `FUN_10000ab8` for the tile holding (probeX, probeY); the floor and
    // ceiling are sampled at the actor's own (selfX, selfY) offsets.
    std::function<bool(int probeX, int probeY, int selfX, int selfY)> tileBlocked;
    // The solid, non-passable, non-zero-extent entities linked into a tile.
    std::function<void(int tileX, int tileY, std::vector<MoveBox>& out)> solidsInTile;
};

namespace move_detail {

inline bool Overlap(int ax, int ay, int ar, const MoveBox& b) {  // FUN_1001c48c
    return ax - ar <= b.x + b.halfX && b.x - b.halfX <= ax + ar &&
           ay - ar <= b.y + b.halfY && b.y - b.halfY <= ay + ar;
}

inline void ClampToMap(PlayerMotion& m, const PlayerMoveWorld& w, int r) {
    int lo = r + 0x100;
    int hiX = (w.mapWidth - 1) * 0x100 - r;
    int hiY = (w.mapHeight - 1) * 0x100 - r;
    int x = m.x > lo ? m.x : lo;
    if (x > hiX) x = hiX;
    int y = m.y > lo ? m.y : lo;
    if (y > hiY) y = hiY;
    m.x = x < 0 ? 0 : x;
    m.y = y < 0 ? 0 : y;
}

// `FUN_100017c8`.
inline void PushOut(PlayerMotion& m, const PlayerMoveWorld& w, int r) {
    int p = m.x - r;
    if (w.tileBlocked(p, m.y, m.x, m.y)) {
        m.vx = 0;
        m.x = (m.x + 0x100) - (p & 0xff);
    }
    p = m.x + r;
    if (w.tileBlocked(p, m.y, m.x, m.y)) {
        m.vx = 0;
        m.x -= p & 0xff;
    }
    p = m.y - r;
    if (w.tileBlocked(m.x, p, m.x, m.y)) {
        m.vy = 0;
        m.y = (m.y + 0x100) - (p & 0xff);
    }
    p = m.y + r;
    if (w.tileBlocked(m.x, p, m.x, m.y)) {
        m.vy = 0;
        m.y -= p & 0xff;
    }
    if (m.x < 0 || m.y < 0 || m.x >= w.mapWidth * 0x100 || m.y >= w.mapHeight * 0x100) return;
    if (!w.solidsInTile) return;
    std::vector<MoveBox> boxes;
    w.solidsInTile(m.x >> 8, m.y >> 8, boxes);
    for (const MoveBox& b : boxes) {
        if (!Overlap(m.x, m.y, r, b)) continue;
        // Back the whole delta out, then re-apply one axis at a time; an
        // axis that still collides is dropped and its velocity zeroed.
        m.y -= m.vy;
        m.x -= m.vx;
        m.x += m.vx;
        if (Overlap(m.x, m.y, r, b)) {
            m.x -= m.vx;
            m.vx = 0;
        }
        m.y += m.vy;
        if (Overlap(m.x, m.y, r, b)) {
            m.y -= m.vy;
            m.vy = 0;
        }
    }
}

inline int Quarter(int v) { return (v < 0 ? v + 3 : v) >> 2; }

}  // namespace move_detail

// `FUN_10000c64`'s horizontal half for the player. `airborne` is `+0x1e5`
// (a jump in flight): the centre tile rule is not asked then.
inline void PlayerActorMove(PlayerMotion& m, const PlayerMoveWorld& w, int deltaUnits,
                            bool ghost, bool airborne) {
    using namespace move_detail;
    const int r = kPlayerRadiusUnits;
    const int startX = m.x, startY = m.y;
    const int oldVx = m.vx, oldVy = m.vy;
    m.vx = Quarter(m.vx);
    m.vy = Quarter(m.vy);
    m.x += (m.vx * deltaUnits) >> 8;
    m.y += (m.vy * deltaUnits) >> 8;
    ClampToMap(m, w, r);
    if (ghost) {
        m.x += 6 * ((m.vx * deltaUnits) >> 8);
        m.y += 6 * ((m.vy * deltaUnits) >> 8);
        return;
    }
    PushOut(m, w, r);
    for (int i = 0; i < 4; ++i) {  // FUN_100016e0
        m.x += (deltaUnits * m.vx) >> 8;
        m.y += (deltaUnits * m.vy) >> 8;
        ClampToMap(m, w, r);
        PushOut(m, w, r);
    }
    m.vx <<= 2;
    m.vy <<= 2;
    if (!airborne && w.tileBlocked(m.x, m.y, m.x, m.y)) {
        m.x = startX;
        m.y = startY;
        if (m.vx == 0) m.vx = oldVx / 2;
        if (m.vy == 0) m.vy = oldVy / 2;
    }
}

}  // namespace sk_bindings
