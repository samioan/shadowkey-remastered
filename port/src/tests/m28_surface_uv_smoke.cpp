// M28 smoke test: the real .sur UV pipeline, the .zcp corner-nudge byte,
// the ceiling A/B band selection, and the tile-grid line-of-sight test --
// all against real azra data, no windowed game loop needed.
//
// Everything asserted here is decompiled ground truth from
// SurfaceFace_BuildAndProject (0x1005d784), SurfaceFace_ClipAndDispatch
// (0x1005d074), SurfaceFace_RasterizeTextured_v3 (0x1005bbc8) and
// Render3DScene (0x100166c8) -- see docs/ZONE_FORMAT.md's .sur section and
// world/zone.h's SurfaceUv() comment.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "world/zone.h"

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%-72s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    if (!ok) ++g_failures;
}

bool NearlyEqual(float a, float b, float tol = 0.01f) { return std::fabs(a - b) <= tol; }

}  // namespace

int main() {
    const char* scriptRoot =
        "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
        "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::Zone zone;
    if (!zone.Load(scriptRoot, "azra")) {
        std::printf("m28_surface_uv_smoke: FAILED to load zone azra\n");
        return 1;
    }

    // --- .sur records: all six fields, against the real azra.sur bytes ---
    // Record 0 is the plain shift-6 case (14 of azra's 23 records use it),
    // record 4 is the only real mixed-shift + V-offset case, record 5 is
    // the real disabled-face case, and records 14/15 are the real mural
    // pair (same texture, shift 4, large opposite U offsets).
    const sk::SurfaceRecord& s0 = zone.surface(0);
    Check(s0.uShift == 6 && s0.vShift == 6 && s0.uOffset == 0 && s0.vOffset == 0 &&
              s0.flags == 0x00 && s0.textureIndex == 0,
          "azra.sur[0] decodes as shift 6/6, no offset, no flags, texture 0");

    const sk::SurfaceRecord& s4 = zone.surface(4);
    Check(s4.uShift == 6 && s4.vShift == 5 && s4.vOffset == 765 && s4.textureIndex == 3,
          "azra.sur[4] decodes as uShift 6 / vShift 5 / vOffset 765 / texture 3");

    Check(zone.surface(5).disabled() && !zone.surface(0).disabled(),
          "azra.sur[5]'s flags bit5 marks the face disabled; record 0 is not");

    const sk::SurfaceRecord& s14 = zone.surface(14);
    const sk::SurfaceRecord& s15 = zone.surface(15);
    Check(s14.uShift == 4 && s15.uShift == 4 && s14.textureIndex == s15.textureIndex &&
              s14.uOffset == -720 && s15.uOffset == 830,
          "azra.sur[14]/[15] are the real shared-texture mural pair (shift 4, +-U offset)");

    // An index past the end falls back to the same defaults the real
    // engine uses when a zone has no .sur table at all (shift 5/5).
    const sk::SurfaceRecord& sBad = zone.surface(9999);
    Check(sBad.uShift == 5 && sBad.vShift == 5 && sBad.flags == 0 && sBad.uOffset == 0,
          "out-of-range .sur index falls back to the engine's shift-5 defaults");

    // --- The real UV formula ---
    // One texel = 2^(8 - shift) raw world units, so a shift-6 surface puts
    // 64 texels across a 256-unit tile: walking one whole tile along the
    // driving axis must advance U by exactly 64.
    {
        sk::SurfaceRecord sur;
        sur.uShift = 6;
        sur.vShift = 6;
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
        zone.SurfaceUv(sur, sk::FaceOrient::UPlusX, 0.0f, 0.0f, 0.0f, &u0, &v0);
        zone.SurfaceUv(sur, sk::FaceOrient::UPlusX, 256.0f, 0.0f, 0.0f, &u1, &v1);
        Check(NearlyEqual(u0, 0.0f) && NearlyEqual(u1, 64.0f),
              "shift 6 puts exactly 64 texels across one 256-unit tile (U from world X)");
    }
    {
        // shift 7 = one full 128-texel texture per tile; shift 4 stretches
        // one texture across 8 tiles (the mural case above).
        sk::SurfaceRecord hi;
        hi.uShift = 7;
        sk::SurfaceRecord lo;
        lo.uShift = 4;
        float u = 0, v = 0, u2 = 0, v2 = 0;
        zone.SurfaceUv(hi, sk::FaceOrient::UPlusX, 256.0f, 0.0f, 0.0f, &u, &v);
        zone.SurfaceUv(lo, sk::FaceOrient::UPlusX, 256.0f, 0.0f, 0.0f, &u2, &v2);
        Check(NearlyEqual(u, 128.0f) && NearlyEqual(u2, 16.0f),
              "shift 7 = one texture per tile; shift 4 = one texture per 8 tiles");
    }
    {
        // Wall faces take V from world Z (height); floor/ceiling take it
        // from world Y. This is the `case 4: case 5:` fall-through in the
        // real switch.
        sk::SurfaceRecord sur;
        sur.uShift = 6;
        sur.vShift = 6;
        float wu = 0, wv = 0, fu = 0, fv = 0;
        zone.SurfaceUv(sur, sk::FaceOrient::UPlusX, 0.0f, 512.0f, 1024.0f, &wu, &wv);
        zone.SurfaceUv(sur, sk::FaceOrient::Floor, 0.0f, 512.0f, 1024.0f, &fu, &fv);
        Check(NearlyEqual(wv, 256.0f) && NearlyEqual(fv, 128.0f),
              "wall V comes from world Z, floor/ceiling V from world Y");
    }
    {
        // The two U-negating orientations, and the flip bits.
        sk::SurfaceRecord sur;
        sur.uShift = 6;
        sur.vShift = 6;
        sur.uOffset = 100;
        float plus = 0, minus = 0, v = 0;
        zone.SurfaceUv(sur, sk::FaceOrient::UPlusY, 0.0f, 256.0f, 0.0f, &plus, &v);
        zone.SurfaceUv(sur, sk::FaceOrient::UMinusY, 0.0f, 256.0f, 0.0f, &minus, &v);
        Check(NearlyEqual(plus, (256.0f + 100.0f) / 4.0f) &&
                  NearlyEqual(minus, (100.0f - 256.0f) / 4.0f),
              "UPlusY/UMinusY apply the real (coord+off) vs (off-coord) forms");

        sk::SurfaceRecord flipped = sur;
        flipped.flags = 0x02 | 0x01;  // flip U and flip V
        float fu = 0, fv = 0;
        zone.SurfaceUv(flipped, sk::FaceOrient::UPlusY, 0.0f, 256.0f, 512.0f, &fu, &fv);
        Check(fu < 0.0f && fv < 0.0f && flipped.flipU() && flipped.flipV(),
              "flags bit1/bit0 negate U/V exactly as BuildAndProject's tail does");
    }

    // --- .zcp corner-nudge byte (0x23) ---
    // Real azra data has 982 non-zero entries out of 11828; find one and
    // confirm ApplyCornerNudge actually moves a vertex by the real 0x80
    // half-tile, and that an unnudged tile is left alone.
    {
        int nudgedTx = -1, nudgedTy = -1;
        for (int ty = 0; ty < zone.height() && nudgedTx < 0; ++ty) {
            for (int tx = 0; tx < zone.width(); ++tx) {
                if (zone.TypeOf(zone.CellAt(tx, ty)).cornerNudge != 0) {
                    nudgedTx = tx;
                    nudgedTy = ty;
                    break;
                }
            }
        }
        Check(nudgedTx >= 0, "real azra.zcp has at least one non-zero cornerNudge tile");
        if (nudgedTx >= 0) {
            float x = nudgedTx * 256.0f + 128.0f;
            float y = nudgedTy * 256.0f + 128.0f;
            float nx = x, ny = y;
            zone.ApplyCornerNudge(&nx, &ny);
            float moved = std::fabs(nx - x) + std::fabs(ny - y);
            Check(NearlyEqual(moved, 128.0f) || NearlyEqual(moved, 256.0f),
                  "ApplyCornerNudge shifts a nudged vertex by the real 0x80 half-tile");
        }
        // A tile whose nudge code is 0 must be untouched.
        int plainTx = -1, plainTy = -1;
        for (int ty = 0; ty < zone.height() && plainTx < 0; ++ty) {
            for (int tx = 0; tx < zone.width(); ++tx) {
                if (zone.TypeOf(zone.CellAt(tx, ty)).cornerNudge == 0) {
                    plainTx = tx;
                    plainTy = ty;
                    break;
                }
            }
        }
        float px = plainTx * 256.0f + 128.0f, py = plainTy * 256.0f + 128.0f;
        float ox = px, oy = py;
        zone.ApplyCornerNudge(&px, &py);
        Check(NearlyEqual(px, ox) && NearlyEqual(py, oy),
              "ApplyCornerNudge leaves a nudge-code-0 tile exactly where it was");
    }

    // --- Line of sight ---
    {
        // A cell can always see itself, and a wall between two open cells
        // must block. Find a real open cell with a wall directly to one
        // side and an open cell beyond it.
        Check(zone.HasLineOfSight(static_cast<float>(zone.playerStartX),
                                   static_cast<float>(zone.playerStartY),
                                   static_cast<float>(zone.playerStartX),
                                   static_cast<float>(zone.playerStartY)),
              "a position always has line of sight to itself");

        bool foundBlocked = false;
        for (int ty = 1; ty + 1 < zone.height() && !foundBlocked; ++ty) {
            for (int tx = 1; tx + 2 < zone.width(); ++tx) {
                if (zone.CellAt(tx, ty).IsWall()) continue;
                if (!zone.CellAt(tx + 1, ty).IsWall()) continue;
                if (zone.CellAt(tx + 2, ty).IsWall()) continue;
                float ax = tx * 256.0f + 128.0f, ay = ty * 256.0f + 128.0f;
                float bx = (tx + 2) * 256.0f + 128.0f, by = ay;
                if (!zone.HasLineOfSight(ax, ay, bx, by)) foundBlocked = true;
                break;
            }
        }
        Check(foundBlocked, "a real wall tile between two open cells blocks line of sight");

        // Out-of-bounds endpoints are never visible.
        Check(!zone.HasLineOfSight(-1000.0f, -1000.0f,
                                    static_cast<float>(zone.playerStartX),
                                    static_cast<float>(zone.playerStartY)),
              "an out-of-bounds endpoint reports no line of sight rather than reading off-grid");
    }

    // --- The two eye-height threshold fields (ZcpEntry +2 / +4) ---
    // Decompiled: the floor gate reads +2, the ceiling A/B selector reads
    // +4. Against real data, +4 equals min(ceilingHeight[4]) for *every*
    // entry in every shipped zone, while +2 is a genuinely separate
    // authored value -- if the port had (as it used to) read +4 for both,
    // the floor would be gated on the ceiling's height and vanish.
    {
        int ceilNearCeiling = 0, differ = 0, total = 0;
        for (int ty = 0; ty < zone.height(); ++ty) {
            for (int tx = 0; tx < zone.width(); ++tx) {
                const sk::ZcpEntry& e = zone.TypeOf(zone.CellAt(tx, ty));
                int16_t minCeil = (std::min)((std::min)(e.ceilingHeight[0], e.ceilingHeight[1]),
                                              (std::min)(e.ceilingHeight[2], e.ceilingHeight[3]));
                if (e.ceilingBandThreshold == minCeil) ++ceilNearCeiling;
                if (e.floorBandThreshold != e.ceilingBandThreshold) ++differ;
                ++total;
            }
        }
        // The ceiling threshold tracks the tile's own ceiling almost
        // perfectly (a single real azra tile, zcp entry 3365, is authored
        // 1408 against a 1024 ceiling -- so it IS a separate field, not a
        // derived one).
        Check(total > 0 && ceilNearCeiling >= total - 8,
              "ZcpEntry+4 tracks the tile's own min(ceilingHeight) for effectively every tile");
        // The point that matters for the renderer: the two thresholds are
        // genuinely different values, so gating the floor on +4 (which is
        // what this port did before the field at +2 was identified) makes
        // the floor disappear wherever they disagree.
        Check(differ > total / 2,
              "ZcpEntry+2 and +4 are distinct fields, differing for most real tiles");
    }

    // --- The corner-index convention ---
    // Real order is 0=(x0,y1) 1=(x1,y1) 2=(x1,y0) 3=(x0,y0). Verified
    // through the public API: sampling FloorHeightAt at each corner of a
    // real sloped tile must return that corner's own stored height.
    {
        int slopeTx = -1, slopeTy = -1;
        for (int ty = 0; ty < zone.height() && slopeTx < 0; ++ty) {
            for (int tx = 0; tx < zone.width(); ++tx) {
                const sk::ZcpEntry& e = zone.TypeOf(zone.CellAt(tx, ty));
                const int16_t* f = e.floorHeight;
                if (f[0] != f[1] || f[1] != f[2] || f[2] != f[3]) {
                    slopeTx = tx;
                    slopeTy = ty;
                    break;
                }
            }
        }
        Check(slopeTx >= 0, "real azra.zcp has at least one sloped (non-flat) floor tile");
        if (slopeTx >= 0) {
            const sk::ZcpEntry& e = zone.TypeOf(zone.CellAt(slopeTx, slopeTy));
            // Sample just inside each corner so the bilinear weight is
            // ~1 for that corner and floor() lands on this tile.
            const float eps = 0.5f;
            float x0 = slopeTx * 256.0f, x1 = x0 + 256.0f;
            float y0 = slopeTy * 256.0f, y1 = y0 + 256.0f;
            struct { float x, y; int corner; } probes[4] = {
                {x0 + eps, y1 - eps, 0},
                {x1 - eps, y1 - eps, 1},
                {x1 - eps, y0 + eps, 2},
                {x0 + eps, y0 + eps, 3},
            };
            bool allMatch = true;
            for (const auto& p : probes) {
                float got = zone.FloorHeightAt(p.x, p.y);
                if (std::fabs(got - static_cast<float>(e.floorHeight[p.corner])) > 2.0f) {
                    allMatch = false;
                }
            }
            Check(allMatch,
                  "FloorHeightAt at each corner returns that corner's real stored height "
                  "(0=(x0,y1) 1=(x1,y1) 2=(x1,y0) 3=(x0,y0))");
        }
    }

    std::printf("\nm28_surface_uv_smoke: %s (%d failure(s))\n", g_failures == 0 ? "OK" : "FAILED",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
