// M130 smoke test: the player's walk, turn and look as the engine runs them
// (simkin_bindings/player_move.h). Needs no game data.
//
//   1. FUN_100445d4's step, and the speeds the whole chain settles at.
//   2. The Speed attribute and an empty fatigue pool both change the walk --
//      the thing the fixed 40 units a tick could not do.
//   3. The turn ramp (vtable +0x1b4) and the look ramp (+0x1c0).
//   4. The push-out against a wall: stop at the radius, slide along it.
//   5. A solid entity closes its tile; one sharing the mover's tile is
//      resolved by the box test.
//   6. The centre tile rule puts a blocked move back where it began.
//   7. A ghost takes the other arm.
#include <cstdio>
#include <string>
#include <vector>

#include "simkin_bindings/ghost_mode.h"
#include "simkin_bindings/player_move.h"
#include "simkin_bindings/weapon_viewmodel.h"

namespace {

int g_Failures = 0;
int g_Checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_Checks;
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what.c_str());
    if (!ok) ++g_Failures;
}

using namespace sk_bindings;

constexpr int kDelta = 10;

// A 32x32 map, open except for the tiles listed.
struct Map {
    std::vector<std::pair<int, int>> walls;
    std::vector<MoveBox> solids;
    PlayerMoveWorld World() const {
        PlayerMoveWorld w;
        w.mapWidth = 32;
        w.mapHeight = 32;
        w.tileBlocked = [this](int px, int py, int, int) {
            if (px < 0 || py < 0) return true;
            for (const auto& t : walls) {
                if (t.first == (px >> 8) && t.second == (py >> 8)) return true;
            }
            for (const MoveBox& b : solids) {
                if ((b.x >> 8) == (px >> 8) && (b.y >> 8) == (py >> 8)) return true;
            }
            return false;
        };
        w.solidsInTile = [this](int tx, int ty, std::vector<MoveBox>& out) {
            for (const MoveBox& b : solids) {
                if ((b.x >> 8) == tx && (b.y >> 8) == ty) out.push_back(b);
            }
        };
        return w;
    }
};

// One tick of holding `dir` at `heading`: thrust, friction, move.
int Tick(PlayerMotion& m, const PlayerMoveWorld& w, int heading, int speed, int fatigue,
         bool held, MoveDir dir = MoveDir::Forward, bool ghost = false) {
    const int x0 = m.x, y0 = m.y;
    if (held) PlayerThrust(m, heading, PlayerMoveStep(speed, fatigue), dir);
    m.vx = PlayerFriction(m.vx);
    m.vy = PlayerFriction(m.vy);
    PlayerActorMove(m, w, kDelta, ghost, false);
    const int dx = m.x - x0, dy = m.y - y0;
    return (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
}

int Settled(int speed, int fatigue, bool ghost = false) {
    Map map;
    const PlayerMoveWorld w = map.World();
    PlayerMotion m;
    int moved = 0;
    for (int i = 0; i < 40; ++i) {
        m.x = 16 * 256;  // keep it clear of the map edge
        m.y = 4 * 256;
        moved = Tick(m, w, 0, speed, fatigue, true, MoveDir::Forward, ghost);
    }
    return moved;
}

}  // namespace

int main() {
    std::printf("m130_player_move_smoke\n");

    // ---- 1 / 2 ----
    Check(PlayerMoveStep(35, 100) == 4480 && PlayerMoveStep(35, 0) == 2240,
          "FUN_100445d4: Speed << 8 >> 1, one more bit with the fatigue pool empty");
    Check(PlayerMoveStep(200, 100) == 0x4000, "and capped at 0x4000");
    const int s30 = Settled(30, 100), s35 = Settled(35, 100), s40 = Settled(40, 100),
              s50 = Settled(50, 100);
    std::printf("    settled units/tick: Speed 30=%d 35=%d 40=%d 50=%d\n", s30, s35, s40, s50);
    Check(s30 == 50 && s35 == 55 && s40 == 65 && s50 == 85,
          "the walk settles at 50 / 55 / 65 / 85 units a tick at Speed 30 / 35 / 40 / 50");
    Check(Settled(50, 0) < s50 * 6 / 10 && Settled(50, 0) > s50 * 4 / 10,
          "exhausted, about half that");
    {
        Map map;
        const PlayerMoveWorld w = map.World();
        PlayerMotion m;
        m.x = 16 * 256;
        m.y = 4 * 256;
        const int first = Tick(m, w, 0, 50, 100, true);
        Check(first > 0 && first < s50, "the first tick is slower than the settled walk (it accelerates)");
        for (int i = 0; i < 20; ++i) Tick(m, w, 0, 50, 100, true);
        const int coast = Tick(m, w, 0, 50, 100, false);
        Check(coast > 0 && coast < s50, "and letting go coasts rather than stopping dead");
        int ticks = 0;
        while ((m.vx != 0 || m.vy != 0) && ticks < 100) {
            Tick(m, w, 0, 50, 100, false);
            ++ticks;
        }
        Check(m.vx == 0 && m.vy == 0 && ticks < 40, "the friction reaches exactly zero");
    }
    {
        // Heading 0 is +y (cos), 0x4000 is +x (sin): yaw = pi/2 - heading.
        Map map;
        const PlayerMoveWorld w = map.World();
        PlayerMotion m;
        m.x = m.y = 16 * 256;
        PlayerThrust(m, 0, 4480, MoveDir::Forward);
        Check(m.vx == 0 && m.vy > 0, "heading 0 walks +y");
        m = PlayerMotion{};
        PlayerThrust(m, 0x4000, 4480, MoveDir::Forward);
        Check(m.vx > 0 && m.vy == 0, "heading 0x4000 walks +x");
        m = PlayerMotion{};
        PlayerThrust(m, 0, 4480, MoveDir::StrafeRight);
        Check(m.vx > 0 && m.vy == 0, "FUN_100063f0 at heading 0 is +x: the right-hand side");
        m = PlayerMotion{};
        PlayerThrust(m, 0, 4480, MoveDir::StrafeLeft);
        Check(m.vx < 0 && m.vy == 0, "FUN_10006480 is its mirror");
        m = PlayerMotion{};
        PlayerThrust(m, 0, 4480, MoveDir::Backward);
        Check(m.vx == 0 && m.vy < 0, "FUN_10006510 walks backwards");
    }

    // ---- 3 ----
    {
        const int target = PlayerTurnTarget(false);
        Check(target == 0x7800 && PlayerTurnTarget(true) == 0x1e00,
              "the turn target is 2 * 0x3c00, or half of it zoomed");
        int rate = 0, ticks = 0;
        while (rate != target && ticks < 50) {
            rate = RampTurnRate(rate, target);
            ++ticks;
        }
        Check(rate == target && ticks == 9, "x1.5 a frame from 0x400 reaches it in nine frames");
        Check(((rate * kDelta) >> 8) == 1200, "which is 1200 heading units a tick (165 degrees a second)");
        Check(RampTurnRate(0, -target) == -0x600, "the other way starts at -0x400 x 1.5");
        int back = rate, n = 0;
        while (back != 0 && n < 50) {
            back = RampTurnRate(back, 0);
            ++n;
        }
        Check(back == 0 && n > 5 && n < 20, "released, it halves back to zero");
        int look = 0;
        for (int i = 0; i < 60; ++i) look = RampLookRate(look, kPlayerLookRate);
        Check(look > kPlayerLookRate - 8 && look <= kPlayerLookRate,
              "the look rate closes an eighth of the gap a frame");
        Check(RampLookRate(0x1000, 0) == 0x800 && RampLookRate(1, 0) == 0,
              "and halves on release");
    }

    // ---- 4 ----
    {
        // A wall across y = tile 10; walk +y into it from tile 8.
        Map map;
        for (int x = 0; x < 32; ++x) map.walls.push_back({x, 10});
        const PlayerMoveWorld w = map.World();
        PlayerMotion m;
        m.x = 16 * 256 + 0x80;
        m.y = 8 * 256;
        for (int i = 0; i < 40; ++i) Tick(m, w, 0, 50, 100, true);
        Check(m.y + kPlayerRadiusUnits == 10 * 256,
              "walking into a wall stops with the 0x80 probe on the tile boundary");
        Check(m.x == 16 * 256 + 0x80, "and no sideways drift");
        // Diagonally into it: the blocked axis stops, the other keeps going.
        const int x0 = m.x;
        for (int i = 0; i < 10; ++i) Tick(m, w, 0x2000, 50, 100, true);
        Check(m.y + kPlayerRadiusUnits == 10 * 256 && m.x > x0 + 200,
              "at 45 degrees the player slides along it");
    }

    // ---- 5 ----
    {
        Map map;
        map.solids.push_back({16 * 256 + 0x80, 10 * 256 + 0x80, 0x40, 0x40});  // a barrel
        const PlayerMoveWorld w = map.World();
        PlayerMotion m;
        m.x = 16 * 256 + 0x80;
        m.y = 7 * 256;
        for (int i = 0; i < 40; ++i) Tick(m, w, 0, 50, 100, true);
        Check(m.y + kPlayerRadiusUnits == 10 * 256,
              "a solid entity closes its whole tile to the player's box");
        // Sharing its tile (which the engine never allows): the box test.
        m = PlayerMotion{};
        m.x = 16 * 256 + 0x80;
        m.y = 10 * 256 + 0x10;
        m.vy = 400;
        std::vector<MoveBox> in;
        w.solidsInTile(16, 10, in);
        Check(in.size() == 1 && move_detail::Overlap(m.x, m.y, kPlayerRadiusUnits, in[0]),
              "an overlapping entity in the mover's tile is found");
        move_detail::PushOut(m, w, kPlayerRadiusUnits);
        Check((m.x >> 8) != 16 || (m.y >> 8) != 10,
              "and the probes, which land in that closed tile, shove the player out of it");
        // The box resolve itself: a tile whose rule is open but holds a box.
        PlayerMoveWorld boxOnly = w;
        boxOnly.tileBlocked = [](int, int, int, int) { return false; };
        m = PlayerMotion{};
        m.x = 16 * 256 + 0x80;
        m.y = 10 * 256 + 0x10;
        m.vy = 40;
        move_detail::PushOut(m, boxOnly, kPlayerRadiusUnits);
        Check(m.vy == 0 && m.y == 10 * 256 + 0x10 - 40,
              "FUN_100017c8's box test backs the colliding axis out and zeroes it");
    }

    // ---- 6 ----
    {
        Map map;
        const PlayerMoveWorld open = map.World();
        PlayerMoveWorld w = open;
        // Only the *centre* test refuses: the probes see nothing.
        w.tileBlocked = [](int px, int py, int sx, int sy) {
            return px == sx && py == sy && (py >> 8) >= 9;
        };
        PlayerMotion m;
        m.x = 16 * 256;
        m.y = 9 * 256 - 20;
        m.vy = 1760;
        PlayerActorMove(m, w, kDelta, false, false);
        Check(m.y == 9 * 256 - 20, "a centre tile the rule refuses puts the player back where the tick began");
        m.vy = 1760;
        PlayerActorMove(m, w, kDelta, false, /*airborne=*/true);
        Check(m.y > 9 * 256, "in the air (+0x1e5) the centre rule is not asked");
    }

    // ---- 7 ----
    {
        const int ghost = Settled(50, 100, true);
        const float ratio = static_cast<float>(ghost) / static_cast<float>(s50);
        std::printf("    ghost %d units/tick, ratio %.3f (M95 derived %.3f)\n", ghost, ratio,
                    GhostMoveSpeedScale());
        Check(ratio > 0.45f && ratio < 0.60f, "a ghost walks at about half speed (M95), now from the move itself");
        Map map;
        for (int x = 0; x < 32; ++x) map.walls.push_back({x, 10});
        const PlayerMoveWorld w = map.World();
        PlayerMotion m;
        m.x = 16 * 256;
        m.y = 9 * 256;
        for (int i = 0; i < 30; ++i) Tick(m, w, 0, 50, 100, true, MoveDir::Forward, true);
        Check(m.y > 11 * 256, "and through the wall");
    }

    std::printf("\nm130_player_move_smoke: %d/%d checks passed\n", g_Checks - g_Failures, g_Checks);
    return g_Failures == 0 ? 0 : 1;
}
