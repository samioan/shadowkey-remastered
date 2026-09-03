// M41 smoke test: the real per-frame visible-tile set,
// `TileGrid_RaycastVisibility` (FUN_1000f694), against real azra data.
//
// See world/zone.h's RaycastVisibleTiles() for the transcription and for
// the correction it carries (the three tiers are a *zoom* setting, not a
// quality knob -- rays x angleStep gives arcs of 140/93/51 degrees, and
// the same engine field is a 0x100-is-identity render scale elsewhere).
//
// What is asserted here is the shape of the set rather than an exact tile
// list: the three tier boundaries, that rays stop at walls, that the
// four-steps-behind origin really does include the tiles behind the
// player, and that the result is a genuine visibility set rather than the
// square neighbourhood it replaces.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include "world/zone.h"

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%-74s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    if (!ok) ++g_failures;
}

}  // namespace

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::Zone zone;
    if (!zone.Load(scriptRoot, "azra")) {
        std::printf("m41_visibility_smoke: FAILED to load azra\n");
        return 1;
    }

    std::printf("=== M41: TileGrid_RaycastVisibility ===\n\n");
    std::printf("azra: %dx%d tiles, player start (%d, %d)\n\n", zone.width(), zone.height(),
                zone.playerStartX >> 8, zone.playerStartY >> 8);

    const float px = static_cast<float>(zone.playerStartX);
    const float py = static_cast<float>(zone.playerStartY);

    // ---- the set is non-empty, in bounds, and de-duplicated ----
    std::vector<std::pair<int, int>> vis = zone.RaycastVisibleTiles(px, py, 0.0f);
    Check(!vis.empty(), "a raycast from the real player start returns a non-empty tile set");
    {
        std::set<std::pair<int, int>> unique(vis.begin(), vis.end());
        bool inBounds = true;
        for (const auto& t : vis) {
            if (!zone.InBounds(t.first, t.second)) inBounds = false;
        }
        Check(unique.size() == vis.size(),
              "...with no duplicates -- the real per-cell frame stamp, reproduced");
        Check(inBounds, "...and every tile inside the grid");
        Check(vis.size() <= 0x200,
              "...never past the real 512-entry cap");
    }

    // ---- the player's own tile is in the set ----
    //
    // This is the point of starting each ray four steps *behind* the
    // camera: a fan that started at the camera would miss the tile the
    // player is standing on for every ray that immediately leaves it.
    {
        const int ptx = zone.playerStartX >> 8;
        const int pty = zone.playerStartY >> 8;
        bool found = false;
        for (const auto& t : vis) {
            if (t.first == ptx && t.second == pty) found = true;
        }
        Check(found, "the tile the player is standing on is in the set (the -4 step origin)");
    }

    // ---- tiles *behind* the player are included too ----
    {
        // Facing +X (yaw 0), so anything with a smaller x is behind. The
        // fan itself only spans +/-70 degrees at this zoom, which never
        // points backwards -- so a tile strictly behind the camera can
        // only be in the set because every ray starts four steps back.
        const int ptx = zone.playerStartX >> 8;
        int behind = 0;
        for (const auto& t : vis) {
            if (t.first < ptx) ++behind;
        }
        Check(behind > 0,
              "tiles strictly behind the camera are in the set -- only the -4 origin puts them "
              "there");
    }

    // ---- the three tiers are three fan *arcs* ----
    //
    // The constants themselves, asserted directly. rayCount x angleStep in
    // the engine's 65536-per-turn angle unit is the fan's total arc:
    // 140, 93 and 51 degrees. That is what makes `engine+0x608` a zoom
    // setting rather than the "quality knob" earlier notes read it as --
    // a detail level would not change the field of view, and the same
    // field is a 0x100-is-identity render scale elsewhere in the engine.
    {
        sk::Zone::VisibilityTier wide = sk::Zone::TierFor(0x080);
        sk::Zone::VisibilityTier identity = sk::Zone::TierFor(0x100);
        sk::Zone::VisibilityTier mid = sk::Zone::TierFor(0x180);
        sk::Zone::VisibilityTier deep = sk::Zone::TierFor(0x300);
        std::printf("   (tier arcs: %.0f deg / %.0f deg / %.0f deg, ranges %d / %d / %d steps)\n",
                    wide.arcDegrees(), mid.arcDegrees(), deep.arcDegrees(), wide.maxSteps,
                    mid.maxSteps, deep.maxSteps);
        Check(wide.rayCount == 150 && wide.maxSteps == 25 && wide.angleStep == 170,
              "the zoomed-out tier is 150 rays x 25 steps, angle step 170");
        Check(mid.rayCount == 177 && mid.maxSteps == 93 && mid.angleStep == 96,
              "the middle tier is 177 rays x 93 steps, angle step 96");
        Check(deep.rayCount == 178 && deep.maxSteps == 172 && deep.angleStep == 52,
              "the zoomed-in tier is 178 rays x 172 steps, angle step 52");
        Check(identity.rayCount == wide.rayCount,
              "0x100 takes the same branch as 0x080 -- the real test is `< 0x101`, not `<= 0x100`");
        Check(wide.arcDegrees() > mid.arcDegrees() && mid.arcDegrees() > deep.arcDegrees() &&
                  wide.maxSteps < mid.maxSteps && mid.maxSteps < deep.maxSteps,
              "zooming in narrows the arc and pushes the range out -- a zoom, not a quality knob");

        // And the behaviour follows: a narrower fan sees fewer tiles from
        // the same spot.
        size_t nWide = zone.RaycastVisibleTiles(px, py, 0.0f, 0x080).size();
        size_t nMid = zone.RaycastVisibleTiles(px, py, 0.0f, 0x180).size();
        size_t nDeep = zone.RaycastVisibleTiles(px, py, 0.0f, 0x300).size();
        std::printf("   (tiles seen from azra's start: %zu / %zu / %zu)\n", nWide, nMid, nDeep);
        Check(nWide > nMid && nMid > nDeep,
              "...and inside a room that shows up directly as fewer tiles per tier");
    }

    // ---- walls stop rays ----
    //
    // The real test is `(flags & 0b1010) == 0b0010`, applied only from the
    // sixth step on. azra's own player start is inside a walled building
    // (a 10x7 room), which is the case that matters: the old fixed-radius
    // scan built faces for the whole 41x41 neighbourhood around it, walls
    // or no walls.
    {
        int maxReach = 0;
        const int ptx = zone.playerStartX >> 8;
        const int pty = zone.playerStartY >> 8;
        for (const auto& t : vis) {
            maxReach = (std::max)(maxReach, (std::max)(std::abs(t.first - ptx),
                                                        std::abs(t.second - pty)));
        }
        std::printf("   (azra's player start sees %zu tiles, max chebyshev reach %d -- the old "
                    "scan was 41x41 = %d)\n",
                    vis.size(), maxReach, 41 * 41);
        Check(vis.size() < static_cast<size_t>(41 * 41) / 8,
              "from inside azra's starting room the visible set is a small fraction of the old "
              "41x41 scan");
        Check(maxReach <= 10,
              "...and does not reach past the walls that enclose it");
    }

    // ---- the fan follows the camera ----
    {
        std::vector<std::pair<int, int>> east = zone.RaycastVisibleTiles(px, py, 0.0f);
        std::vector<std::pair<int, int>> west =
            zone.RaycastVisibleTiles(px, py, 3.14159265358979f);
        std::set<std::pair<int, int>> e(east.begin(), east.end());
        int shared = 0;
        for (const auto& t : west) {
            if (e.count(t)) ++shared;
        }
        bool differ = east != west;
        Check(differ && shared < static_cast<int>(west.size()),
              "turning around produces a different set -- the fan is centred on the heading");
    }

    std::printf("\nm41_visibility_smoke: %s\n", g_failures ? "FAILED" : "PASSED (all checks)");
    return g_failures ? 1 : 0;
}
