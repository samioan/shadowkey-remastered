#include "render3d/zone_renderer.h"

#include "render3d/gpu_frame.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <cmath>
#include <vector>

#include "render3d/band_pool.h"

namespace sk {

// ---- M86: the hit-flash ramps and their timer ----
//
// `FUN_1000f304(engine, row, r0, g0, b0, r1, g1, b1)`, transcribed. The
// real one does the interpolation in floats and truncates each channel
// back to an int per step, which is why this does too rather than folding
// the divide into the pack -- the two disagree by a rung at several steps.
HitFlashRamps::HitFlashRamps() {
    struct Row {
        int r0, g0, b0, r1, g1, b1;
    };
    // `FUN_1000f4b4`, verbatim and in order: seven red rows then seven
    // green ones, each family four copies of the same dark-floored ramp
    // followed by three with progressively brighter floors.
    static const Row kRows[14] = {
        {0x40, 0, 0, 0xff, 0, 0}, {0x40, 0, 0, 0xff, 0, 0}, {0x40, 0, 0, 0xff, 0, 0},
        {0x40, 0, 0, 0xff, 0, 0}, {0x80, 0, 0, 0xff, 0, 0}, {0xc0, 0, 0, 0xff, 0, 0},
        {0xff, 0, 0, 0xff, 0, 0}, {0, 0x40, 0, 0, 0xff, 0}, {0, 0x40, 0, 0, 0xff, 0},
        {0, 0x40, 0, 0, 0xff, 0}, {0, 0x40, 0, 0, 0xff, 0}, {0, 0x80, 0, 0, 0xff, 0},
        {0, 0xc0, 0, 0, 0xff, 0}, {0, 0xff, 0, 0, 0xff, 0},
    };
    for (int row = 0; row < 14; ++row) {
        const Row& d = kRows[row];
        // The real step is `(end - start) * DAT_1000f4a0` with that
        // constant 1/16 -- sixteen entries spanning the range exclusive of
        // the endpoint, not fifteen steps inclusive of it.
        const float dr = static_cast<float>(d.r1 - d.r0) / 16.0f;
        const float dg = static_cast<float>(d.g1 - d.g0) / 16.0f;
        const float db = static_cast<float>(d.b1 - d.b0) / 16.0f;
        float r = static_cast<float>(d.r0), g = static_cast<float>(d.g0),
              b = static_cast<float>(d.b0);
        for (int i = 0; i < 16; ++i) {
            const int ri = static_cast<int>(r), gi = static_cast<int>(g),
                      bi = static_cast<int>(b);
            // `uVar1 & 0xfff0 | (r >> 4) << 8 | (b >> 4)` -- green keeps
            // its own bits 4..7 in place rather than being shifted down
            // and back, which is the same thing for a 0..255 channel.
            entry[row][i] = static_cast<uint16_t>((gi & 0xF0) | ((ri >> 4) << 8) | (bi >> 4));
            r += dr;
            g += dg;
            b += db;
        }
    }
}

const HitFlashRamps& FlashRamps() {
    static const HitFlashRamps ramps;
    return ramps;
}

// `FUN_10067c3c`.
void HitFlash::Arm(int periodUnits, int minRow, int maxRow) {
    period = periodUnits;
    rowMin = minRow;
    rowMax = maxRow;
    stepTimer = periodUnits;
    total = (maxRow - minRow) * periodUnits * 2;
    dir = 1;
    row = minRow;
}

// `FUN_10064f08`. The real one is called from the draw path rather than
// the tick, which is why it takes the frame delta rather than a tick
// count; the port calls it once per world tick with the same
// kAiFrameDeltaUnits every other engine timer in this port uses.
void HitFlash::Tick(int deltaUnits) {
    if (total == -1) return;
    total -= deltaUnits;
    if (total < 1) {
        total = -1;
        row = -1;
        return;
    }
    stepTimer -= deltaUnits;
    if (stepTimer >= 1) return;
    row += dir;
    stepTimer = period;
    if (row > rowMax) {
        row = rowMax;
        dir = -1;
    }
    if (row < rowMin) {
        row = rowMin;
        dir = 1;
        // The real line, and the reason a flash that survives a full
        // round trip speeds up rather than repeating at one rate.
        period >>= 1;
    }
}

namespace {

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

struct Face {
    Vec3 corners[4];  // winding: 0,1,2,3 around the quad
    int surIndex = 0xff;
    uint8_t hueGroup = 0;      // This session (decompiled confirmation): the
                               // relevant tile's own ZmpCell::flags bits 4-5
                               // -- the *blocking neighbor's* cell for a wall
                               // face, the current tile's own cell for
                               // floor/ceiling. See zone.h's PaletteColor().
    // Which world axis drives this face's U (and V) -- SurfaceFace_
    // BuildAndProject's `param_5`, chosen per wall direction by
    // Render3DScene. See zone.h's FaceOrient.
    FaceOrient orient = FaceOrient::Floor;
};

// Real corner-index -> corner-position convention, decompiled this
// session from Render3DScene (0x100166c8) by reading which world X/Y each
// vertex slot is assigned in the floor block and in all four wall blocks:
//
//     index 0 = (x0, y1)   index 1 = (x1, y1)
//     index 2 = (x1, y0)   index 3 = (x0, y0)
//
// i.e. starting at the tile's -X/+Y corner and going clockwise in
// (X right, Y down) terms. This port previously assumed the exact
// *reverse* order (0 = (x0,y0) ... 3 = (x0,y1)), which mirrors every
// sloped tile's floor and ceiling about its diagonal. Invisible on the
// flat tiles that make up most of a zone -- which is why it survived this
// long -- but wrong wherever ZcpEntry::floorHeight[4] genuinely varies,
// and it also flowed into Zone::FloorHeightAt()'s bilinear sample, so the
// ground the player physically stood on was mirrored too.
constexpr float kCornerDx[4] = {0.0f, 1.0f, 1.0f, 0.0f};
constexpr float kCornerDy[4] = {1.0f, 1.0f, 0.0f, 0.0f};

// A floor or ceiling quad: the tile's four stored corner heights placed at
// the four corner positions above.
void AddFloorCeiling(std::vector<Face>& faces, const Zone& zone, int tx, int ty,
                      const int16_t heights[4], uint8_t surIndex, uint8_t hueGroup,
                      FaceOrient orient) {
    if (surIndex == 0xff || zone.surfaceDisabled(surIndex)) return;
    Face f;
    f.surIndex = surIndex;
    f.hueGroup = hueGroup;
    f.orient = orient;
    for (int i = 0; i < 4; ++i) {
        f.corners[i] = {static_cast<float>(tx) + kCornerDx[i],
                        static_cast<float>(ty) + kCornerDy[i],
                        static_cast<float>(heights[i])};
    }
    faces.push_back(f);
}

// One vertical wall band spanning the shared edge between two tiles. The
// edge has two endpoints (each a corner position both tiles share); at
// each, the band runs from `lo` up to `hi`. Skipped entirely when it has
// no vertical extent at either endpoint, matching the real code's own
// "both edges degenerate" early-out.
void AddWallBand(std::vector<Face>& faces, const Zone& zone, float ax, float ay, float bx, float by,
                  float loA, float hiA, float loB, float hiB, uint8_t surIndex, uint8_t hueGroup,
                  FaceOrient orient) {
    if (surIndex == 0xff || zone.surfaceDisabled(surIndex)) return;
    if (hiA <= loA && hiB <= loB) return;
    Face f;
    f.surIndex = surIndex;
    f.hueGroup = hueGroup;
    f.orient = orient;
    f.corners[0] = {ax, ay, loA};
    f.corners[1] = {bx, by, loB};
    f.corners[2] = {bx, by, hiB};
    f.corners[3] = {ax, ay, hiA};
    faces.push_back(f);
}

// One of the four wall directions, as Render3DScene's four near-identical
// blocks actually implement them.
struct WallDirection {
    int dx, dy;
    // The two shared corner positions along this edge, given as
    // (current tile's corner index, neighbouring tile's corner index).
    // Both indices name the same physical point under the corner
    // convention above.
    int curA, nbrA;
    int curB, nbrB;
    // Byte offsets into the *neighbour's* ZcpEntry. `main` is the band
    // that runs from the neighbour's ceiling up to this tile's ceiling
    // (the bulk of an ordinary wall); `step` is the one from this tile's
    // floor up to a raised neighbour floor (a ledge/kick wall).
    //
    // NOTE the roles are the opposite way round from what
    // docs/ZONE_FORMAT.md's field names suggest: `*_lo` (0x16-0x19) is the
    // *main/upper* band and `*_hi` (0x1a-0x1d) is the *lower* floor-step
    // band. Confirmed directly -- the 0x1a-family index is the one passed
    // for the floor-height-comparison draw in every one of the four
    // blocks.
    int mainOffset, stepOffset;
};

// Direction -> (edge corners, neighbour byte offsets). Derived corner by
// corner from the decompile, not by assumption: e.g. the +X block pairs
// this tile's floor[2]/floor[1] against the neighbour's floor[3]/floor[0],
// which under the corner convention above are exactly the two points
// (x1,y0) and (x1,y1) that the two tiles share.
constexpr WallDirection kWallDirections[4] = {
    // +X: neighbour cell is `pbVar36 + 8`, indices 0x16 / 0x1a.
    {1, 0, 2, 3, 1, 0, 0x16, 0x1a},
    // -X: neighbour cell is `pbVar36 - 8`, indices 0x17 / 0x1b.
    {-1, 0, 0, 1, 3, 2, 0x17, 0x1b},
    // +Y: neighbour row +1, indices 0x18 / 0x1c.
    {0, 1, 1, 2, 0, 3, 0x18, 0x1c},
    // -Y: neighbour row -1, indices 0x19 / 0x1d.
    {0, -1, 3, 0, 2, 1, 0x19, 0x1d},
};

// The per-direction `.sur` index bytes, addressed the way the real code
// does (a raw byte offset into the 36-byte entry) rather than by field
// name, so kWallDirections above can stay a plain table.
uint8_t SurIndexAt(const ZcpEntry& e, int byteOffset) {
    switch (byteOffset) {
        case 0x16: return e.surIndexE_lo;
        case 0x17: return e.surIndexW_lo;
        case 0x18: return e.surIndexS_lo;
        case 0x19: return e.surIndexN_lo;
        case 0x1a: return e.surIndexE_hi;
        case 0x1b: return e.surIndexW_hi;
        case 0x1c: return e.surIndexS_hi;
        case 0x1d: return e.surIndexN_hi;
        default: return 0xff;
    }
}

// Collects the faces to draw around the camera's tile.
//
// **Rewritten this session against a full decompile of Render3DScene's
// (0x100166c8) tile-grid traversal**, replacing an approximation that had
// stood since M6. What the port used to do: for each open tile, if a
// neighbour was a wall tile (or out of bounds), draw one full-height quad
// from that tile's *own* `surIndex<dir>_lo`. What the real engine
// actually does, and what this now does:
//
//  * A face's `.sur` index comes from the **neighbouring** tile's `.zcp`
//    entry, never the current tile's. Measured against real shipped data,
//    the two disagree for 100% of azra's wall boundaries, 98% of
//    snowline's, 92% of ghstpass's and 12% of crypt1's -- so most walls in
//    most zones were being drawn with the wrong material outright. (Real
//    example: azra's wall tiles hand back a surface whose flags bit5 marks
//    it *disabled*, i.e. the real game deliberately draws nothing there
//    and shows the open sky beyond a canyon edge; the port was filling it
//    in with the grass texture off the tile the player was standing on.)
//  * Faces are not gated on "is the neighbour a wall". Every tile
//    boundary can carry a face; `0xff` in the neighbour's index byte means
//    "no face here", and the geometry is gated purely by comparing the two
//    tiles' corner heights. A flat corridor's boundaries produce
//    zero-height bands and simply draw nothing, which is why this doesn't
//    fill open floors with walls.
//  * Each direction fires up to two bands: a floor **step** band (this
//    tile's floor up to a raised neighbour floor) and a **main** band
//    (from `max(this floor, neighbour ceiling)` up to this tile's
//    ceiling). A solid wall neighbour -- floor high, ceiling low -- makes
//    the two together span the full opening, which is where ordinary
//    walls come from.
//  * Wall tiles are iterated too (only their floor/ceiling is suppressed).
//    The height comparisons make this self-consistent rather than
//    double-drawing: for a boundary between an open tile and a solid one,
//    every band on the solid tile's side comes out degenerate.
//  * A face only draws when the camera is on the current tile's own side
//    of that edge (an implicit already-passed/backface cull), unless the
//    tile's `flags` bit3 forces it.
//  * The floor draws only when `ceilingBandThreshold < cameraZ` or
//    `flags` bit2 is set; the ceiling picks band A or B by the same
//    comparison. Both were previously unconditional/always-A.
//
// M41: `visibleTiles` is now the real per-frame set from
// `TileGrid_RaycastVisibility` (Zone::RaycastVisibleTiles) rather than a
// fixed-radius square scan -- which is both the real shape (rays stop at
// walls, so a closed room no longer builds faces for the whole
// neighbourhood behind it) and the real order (the original runs its
// raycast immediately before this loop). An out-of-bounds neighbour is
// still skipped; the real code reads past the grid there, and its own
// raycast never reaches those tiles in practice.
std::vector<Face> CollectFaces(const Zone& zone,
                                const std::vector<std::pair<int, int>>& visibleTiles,
                                float cameraWorldX, float cameraWorldY, float cameraWorldZ) {
    std::vector<Face> faces;
    for (const std::pair<int, int>& tile : visibleTiles) {
        {
            const int tx = tile.first;
            const int ty = tile.second;
            if (!zone.InBounds(tx, ty)) continue;
            const ZmpCell& cell = zone.CellAt(tx, ty);
            const ZcpEntry& t = zone.TypeOf(cell);
            // Floor/ceiling take their `.zlu` hue family from this tile's
            // own cell; a wall face takes it from the neighbour whose
            // material it is drawing (see zone.h's PaletteColor()).
            uint8_t ownHueGroup = static_cast<uint8_t>((cell.flags >> 4) & 0x3);

            const float x0 = static_cast<float>(tx) * kTileScale;
            const float x1 = x0 + kTileScale;
            const float y0 = static_cast<float>(ty) * kTileScale;
            const float y1 = y0 + kTileScale;

            if (!cell.IsWall()) {
                // Real floor gate: `(flags & 2) == 0 && (ZcpEntry+2 <
                // cameraEyeZ || (flags & 4))`. Note it reads the entry's
                // *floor* threshold at offset 2, NOT the ceiling one at
                // offset 4 -- see ZcpEntry's comment. bit2 forces the draw
                // regardless of the height comparison, mechanically like
                // bit3's wall force-draw.
                bool floorVisible = cameraWorldZ > static_cast<float>(t.floorBandThreshold) ||
                                     (cell.flags & 0x04) != 0;
                if (floorVisible) {
                    AddFloorCeiling(faces, zone, tx, ty, t.floorHeight, t.surIndexFloor,
                                     ownHueGroup, FaceOrient::Floor);
                }
                // Real ceiling A/B band selection: the threshold is this
                // tile's ceilingBandThreshold, or its ceilingHeight[3]
                // when flags bit6 is set. Below it you are under the
                // ceiling and see band A; at or above it you are on top of
                // it and see band B.
                int16_t ceilThreshold =
                    (cell.flags & 0x40) ? t.ceilingHeight[3] : t.ceilingBandThreshold;
                uint8_t ceilSur = cameraWorldZ < static_cast<float>(ceilThreshold)
                                       ? t.surIndexCeilingA
                                       : t.surIndexCeilingB;
                AddFloorCeiling(faces, zone, tx, ty, t.ceilingHeight, ceilSur, ownHueGroup,
                                 FaceOrient::Ceiling);
            }

            for (const WallDirection& d : kWallDirections) {
                int nx = tx + d.dx, ny = ty + d.dy;
                if (!zone.InBounds(nx, ny)) continue;

                // Camera-side gate: the face only draws when the camera is
                // still on this tile's side of the shared edge. flags bit3
                // forces it regardless (plausibly "always double-sided",
                // exact intent unconfirmed -- docs/RENDERER_3D.md).
                bool cameraSide;
                if (d.dx > 0) {
                    cameraSide = cameraWorldX < x1;
                } else if (d.dx < 0) {
                    cameraSide = cameraWorldX > x0;
                } else if (d.dy > 0) {
                    cameraSide = cameraWorldY < y1;
                } else {
                    cameraSide = cameraWorldY > y0;
                }
                if (!cameraSide && (cell.flags & 0x08) == 0) continue;

                const ZmpCell& nCell = zone.CellAt(nx, ny);
                const ZcpEntry& n = zone.TypeOf(nCell);
                uint8_t mainIdx = SurIndexAt(n, d.mainOffset);
                // The real code gates the *whole* direction on the main
                // index alone -- a 0xff there skips the step band too.
                if (mainIdx == 0xff) continue;
                uint8_t stepIdx = SurIndexAt(n, d.stepOffset);
                uint8_t nbrHueGroup = static_cast<uint8_t>((nCell.flags >> 4) & 0x3);

                // Both bands share one orientation code, taken from the
                // *main* index's flip-U bit (the real code computes
                // `uVar9` once, before either draw). For the two X-facing
                // directions that bit swaps which of the two U forms is
                // used; the Y-facing directions pass a literal 2/3.
                FaceOrient orient;
                if (d.dx > 0) {
                    orient = zone.surface(mainIdx).flipU() ? FaceOrient::UPlusY
                                                            : FaceOrient::UMinusY;
                } else if (d.dx < 0) {
                    orient = zone.surface(mainIdx).flipU() ? FaceOrient::UMinusY
                                                            : FaceOrient::UPlusY;
                } else if (d.dy > 0) {
                    orient = FaceOrient::UPlusX;
                } else {
                    orient = FaceOrient::UMinusX;
                }

                // The two shared corner positions along this edge.
                float ax = static_cast<float>(tx) + kCornerDx[d.curA];
                float ay = static_cast<float>(ty) + kCornerDy[d.curA];
                float bx = static_cast<float>(tx) + kCornerDx[d.curB];
                float by = static_cast<float>(ty) + kCornerDy[d.curB];

                float curFloorA = static_cast<float>(t.floorHeight[d.curA]);
                float curFloorB = static_cast<float>(t.floorHeight[d.curB]);
                float curCeilA = static_cast<float>(t.ceilingHeight[d.curA]);
                float curCeilB = static_cast<float>(t.ceilingHeight[d.curB]);
                float nbrFloorA = static_cast<float>(n.floorHeight[d.nbrA]);
                float nbrFloorB = static_cast<float>(n.floorHeight[d.nbrB]);
                float nbrCeilA = static_cast<float>(n.ceilingHeight[d.nbrA]);
                float nbrCeilB = static_cast<float>(n.ceilingHeight[d.nbrB]);

                // Step band: this tile's floor up to a raised neighbour
                // floor. Real condition is an OR across the two edge
                // endpoints, so a band that only rises at one end still
                // draws.
                if (curFloorA < nbrFloorA || curFloorB < nbrFloorB) {
                    AddWallBand(faces, zone, ax, ay, bx, by, curFloorA, nbrFloorA, curFloorB,
                                 nbrFloorB, stepIdx, nbrHueGroup, orient);
                }
                // Main band: from max(this floor, neighbour ceiling) up to
                // this tile's ceiling.
                float loA = std::max(curFloorA, nbrCeilA);
                float loB = std::max(curFloorB, nbrCeilB);
                AddWallBand(faces, zone, ax, ay, bx, by, loA, curCeilA, loB, curCeilB, mainIdx,
                             nbrHueGroup, orient);
            }
        }
    }
    return faces;
}

// A vertex in camera space, before the perspective divide -- the stage the
// real engine's Poly3D_ClipAgainstPlane operates on. Keeping this separate
// from ProjectedVertex lets the near plane actually *clip* a polygon
// (producing new vertices along the plane) instead of dropping it whole,
// which is what M6-M27 did and what left a hole in the floor/ceiling
// around the camera's own tile: those quads always have corners behind the
// eye, so they were culled entirely every frame.
struct ViewVertex {
    float right = 0, forward = 0, up = 0;
    float u = 0, v = 0;   // texel units, NOT yet divided by w
    float light = 0;
};

// Near-plane clip, Sutherland-Hodgman against `forward >= kNearPlane` --
// the same operation Poly3D_ClipAgainstPlane performs in the original
// (which this port deliberately does not reproduce instruction-for-
// instruction, per docs/ROADMAP.md's Phase 2 decision). Returns the
// clipped vertex count.
//
// **M66 -- the bound, and the crash the old one caused.** This used to be
// documented as "`out` must have room for `count + 1`", which is the
// textbook figure and is only true for a **planar** input polygon: a plane
// meets a planar convex polygon in a line, so exactly two edges cross it,
// giving `kept + 2 <= count + 1` outputs. Feed it a polygon whose vertices
// are *not* coplanar and that reasoning collapses -- the in/out pattern can
// alternate all the way around the ring, and every one of the `count` edges
// can contribute a crossing vertex on top of the kept ones. The real bound
// is therefore `2 * count`, and this port had a real non-planar caller: a
// tile's floor/ceiling quad is built from four independent corner heights
// (AddFloorCeiling above), i.e. a bilinear patch, not a plane.
//
// That caller passed a 5-entry buffer. It only ever overflowed once the
// camera could pitch (M30) -- with `pitch == 0` a vertex's view `forward`
// does not depend on its height at all, so `forward` is an affine function
// of (x, y) over a flat unit square and its in/out pattern can never
// alternate. Add pitch and the height term enters, and a sloped tile
// straddling the near plane produces 6 vertices into room for 5. See
// `Render`'s own comment for what the port does about it now, and
// docs/PORT_ROADMAP.md's M66 entry for the reproduction.
//
// `capacity` is not defensive dressing: it is the invariant the old code
// stated in prose and got wrong, now stated somewhere the compiler and the
// running program can both see it.
//
// M125 -- **the guard is per write, not per input vertex.** M66's version
// checked `n + 2 > capacity` before every vertex, reserving room for a kept
// vertex *and* a crossing even where only one of them is written. A
// triangle clips to at most 4 vertices, and every caller passes exactly 4,
// so the case "first vertex in, second out, third in" -- out, crossing,
// crossing, then the third vertex -- reached the third vertex with n == 3,
// failed `3 + 2 > 4` and dropped it. The polygon came back as a triangle
// instead of a quad and half the clipped area was never drawn. That is the
// wall that vanished as the player walked up to it (one of a face's two
// triangles straddles the near plane whenever a wall comes within about a
// tile of the eye at an angle), with the level showing through the gap,
// and the same chunks missing from any model close to the camera.
constexpr float kNearPlane = 0.05f;  // tile units

int ClipNear(const ViewVertex* in, int count, ViewVertex* out, int capacity) {
    int n = 0;
    for (int i = 0; i < count; ++i) {
        const ViewVertex& a = in[i];
        const ViewVertex& b = in[(i + 1) % count];
        bool aIn = a.forward >= kNearPlane;
        bool bIn = b.forward >= kNearPlane;
        if (aIn) {
            if (n >= capacity) break;  // unreachable for a planar input; see above
            out[n++] = a;
        }
        if (aIn != bIn) {
            if (n >= capacity) break;
            float t = (kNearPlane - a.forward) / (b.forward - a.forward);
            ViewVertex m;
            m.right = a.right + (b.right - a.right) * t;
            m.forward = kNearPlane;
            m.up = a.up + (b.up - a.up) * t;
            m.u = a.u + (b.u - a.u) * t;
            m.v = a.v + (b.v - a.v) * t;
            m.light = a.light + (b.light - a.light) * t;
            out[n++] = m;
        }
    }
    return n;
}

struct ProjectedVertex {
    float sx = 0, sy = 0;  // screen space
    float invW = 0;        // 1/viewForward, for perspective-correct interpolation
    float u = 0, v = 0;    // texture-space, already divided by w (i.e. u/w, v/w)
    // Real per-vertex light/fog scalar (SurfaceFace_BuildAndProject writes
    // it to `vertex+6`; SurfaceFace_RasterizeTextured_v3 interpolates it
    // across each scanline to pick the `.zlu` brightness rung per pixel).
    // Kept in the raw ZmpCell::lightLevel fixed-point, already clamped to
    // the real [0x400, 0x3f00] range. Interpolated affinely in screen
    // space, matching the original -- not perspective-divided like u/v.
    float light = 0;
    // M89: the engine's *other* per-vertex scalar, and the one that was
    // missing entirely. Both fade rasterizer families compute
    // `fog = min(viewDepth * engine[0x5c8] >> 8, 0xffff)` in their
    // prologue (SurfaceFace_RasterizeTextured_v2's is the clearest:
    // `rec[0x10] = rec[0x08] * engine[0x5c8] >> 8`, capped at 0xffff),
    // interpolate it across the span and OR `fog & 0xf000` into the pixel
    // word -- so its **top nibble** is a 0..15 fog level, and
    // CompositeSceneBufferToScreen resolves that through `<zone>.zfg`.
    // Interpolated affinely in screen space, exactly like `light`.
    float fog = 0;
    float depthWorld = 0;  // view-forward distance, raw world units
};

// M89: the per-vertex fog scalar and the level its top nibble encodes.
// `kDefaultZoomScale` is the engine's own boot value for `engine+0x608`
// (0x100, set in the engine constructor and floored there by the only
// code that moves it), which picks the 25-tile visibility tier -- so fog
// saturates at ~12 tiles.
constexpr int kDefaultZoomScale = 0x100;

float FogScalar(float depthWorld, int fogPercent) {
    static const float kScale = static_cast<float>(Zone::FogScaleFor(kDefaultZoomScale));
    if (fogPercent == 100) return std::clamp(depthWorld * kScale / 256.0f, 0.0f, 65535.0f);
    // M123: the Fog setting scales `engine+0x5c8` and nothing else.
    const float scale = kScale * static_cast<float>(fogPercent) / 100.0f;
    return std::clamp(depthWorld * scale / 256.0f, 0.0f, 65535.0f);
}

// M123: how far the visibility march reaches for a fog density. At the
// device's density the fog is opaque at half the default tier's 25 steps,
// well inside what is drawn; lighter fog moves that point out, so the march
// is lengthened to keep the drawn edge behind it -- same 140-degree fan, the
// rays multiplied so their spacing at the far end is unchanged, and both
// tile caps grown with the area. Capped at 64 tiles (half a map), which is
// what Off draws.
Zone::VisibilityTier VisibilityTierForFog(int fogPercent, size_t& softCap, size_t& hardCap) {
    Zone::VisibilityTier tier = Zone::TierFor(kDefaultZoomScale);
    softCap = Zone::kVisibleSoftCap;
    hardCap = Zone::kVisibleHardCap;
    if (fogPercent >= 100) return tier;
    constexpr int kMaxSteps = 64;
    const int base = tier.maxSteps;
    int steps = fogPercent <= 0 ? kMaxSteps : (base * 100 + fogPercent - 1) / fogPercent;
    steps = std::clamp(steps, base, kMaxSteps);
    if (steps == base) return tier;
    tier.rayCount = tier.rayCount * steps / base;
    tier.angleStep = tier.angleStep * base / steps;
    tier.maxSteps = steps;
    const size_t area = static_cast<size_t>(steps) * static_cast<size_t>(steps);
    const size_t baseArea = static_cast<size_t>(base) * static_cast<size_t>(base);
    softCap = Zone::kVisibleSoftCap * area / baseArea;
    hardCap = Zone::kVisibleHardCap * area / baseArea;
    return tier;
}

int FogLevelOf(float fogScalar) {
    return std::clamp(static_cast<int>(fogScalar) >> 12, 0, 15);
}

// M119: the rows [begin, end) of the target one pass may write. The whole
// frame is one span at the original Resolution; above it the frame is split
// into horizontal bands drawn on separate threads (see ZoneRenderer::Render),
// and every rasterizer clips its row range to its band. Nothing in any of
// them reads or writes a pixel outside the row it is on, so the bands never
// touch, and each pixel is computed by exactly the arithmetic it would have
// been in one pass.
struct RowSpan {
    int begin;
    int end;
};

// 4-bit-per-channel color (docs/GRAPHICS_FORMAT.md), 0x0RGB -> RGB565 for
// the backbuffer. `brightness` (M9, [0,1]-ish) scales each channel before
// packing -- see Zone::BakeLighting()/LightLevelToBrightness().
uint16_t ExpandRGB444(uint16_t raw444, float brightness) {
    uint8_t r = static_cast<uint8_t>((raw444 >> 8) & 0xf);
    uint8_t g = static_cast<uint8_t>((raw444 >> 4) & 0xf);
    uint8_t b = static_cast<uint8_t>(raw444 & 0xf);
    auto shade = [&](uint8_t c) {
        return static_cast<uint8_t>(std::clamp(static_cast<float>(c) * 17.0f * brightness, 0.0f, 255.0f));
    };
    return PackRGB565(shade(r), shade(g), shade(b));
}

// M123: every rasterizer writes `ExpandRGB444(raw, 1.0f)`, a float multiply
// and clamp per channel per pixel. At brightness 1 it is a pure function of
// the 12-bit colour, so it is a 4096-entry table filled by that same
// function -- the same values, without the arithmetic.
uint16_t ExpandRGB444Full(uint16_t raw444) {
    static const std::array<uint16_t, 4096> table = [] {
        std::array<uint16_t, 4096> t{};
        for (int i = 0; i < 4096; ++i) t[static_cast<size_t>(i)] = ExpandRGB444(static_cast<uint16_t>(i), 1.0f);
        return t;
    }();
    return table[raw444 & 0x0fff];
}

// M123: **which pixels of a row can be inside a triangle.** The rasterizers
// below test every pixel of the triangle's bounding box with three edge
// functions; a large near-camera triangle leaves half its box empty, and at
// a high Resolution that is most of the frame's work. On one row each edge
// function is linear in x, `w(x) = A*x + K`, so the inside run is an
// interval this solves for -- in double precision, and widened by the
// worst-case rounding of the float test the loop still runs on every pixel
// it visits. The narrowing only ever skips pixels that test would reject,
// so the output is identical bit for bit (the tracked .ppm dumps, and
// m119's narrowed-vs-full comparison at 3x, check exactly that). An edge
// too close to horizontal to bound a row reliably is simply not used.
bool g_SpanNarrowing = true;

void ClipRowToTriangle(const ProjectedVertex& a, const ProjectedVertex& b,
                       const ProjectedVertex& c, float area, int py, int boxW, int boxH,
                       int& lo, int& hi) {
    if (!g_SpanNarrowing) return;
    const double sy = static_cast<double>(py) + 0.5;
    const double sign = area > 0 ? 1.0 : -1.0;
    // The float test's error is a few ulps of its largest product, which is
    // bounded by the box's area; 4e-6 of it is a generous multiple of that.
    const double err = 4e-6 * (static_cast<double>(boxW) + 2.0) * (static_cast<double>(boxH) + 2.0);
    double xmin = static_cast<double>(lo), xmax = static_cast<double>(hi) + 1.0;
    auto edge = [&](const ProjectedVertex& p, const ProjectedVertex& q) {
        // w = (p.sx - x) * (q.sy - y) - (p.sy - y) * (q.sx - x)
        //   = x * (p.sy - q.sy) + [p.sx * (q.sy - y) - (p.sy - y) * q.sx]
        const double A = (static_cast<double>(p.sy) - q.sy) * sign;
        const double K = (static_cast<double>(p.sx) * (q.sy - sy) -
                          (static_cast<double>(p.sy) - sy) * q.sx) * sign;
        if (std::fabs(A) < 0.25) return;  // too flat to bound a row safely
        const double root = -K / A;
        const double slack = 1.0 + err / std::fabs(A);
        if (A > 0) {
            xmin = std::max(xmin, root - slack);
        } else {
            xmax = std::min(xmax, root + slack);
        }
    };
    edge(b, c);
    edge(c, a);
    edge(a, b);
    // Pixel px is sampled at px + 0.5.
    const double first = std::floor(xmin - 0.5);
    const double last = std::ceil(xmax - 0.5);
    if (first > lo) lo = static_cast<int>(std::min(first, static_cast<double>(hi) + 1.0));
    if (last < hi) hi = static_cast<int>(std::max(last, static_cast<double>(lo) - 1.0));
}

}  // namespace

void SetRasterSpanNarrowingForTest(bool enabled) { g_SpanNarrowing = enabled; }

namespace {

// The real detail-reduction mask (SurfaceFace_ClipAndDispatch, 0x1005d074):
// the texel index is AND-ed with 0x7f/0x7e/0x7c/0x78 depending on the
// triangle's own average view depth (its three vertices weighted 1:2:1,
// >>2), thresholded at 0x400/0x800/0xc00 raw world units -- i.e. 4/8/12
// tiles. Far geometry therefore samples a coarser texel grid, which is
// both what the original looks like and a cheap anti-aliasing measure at
// this resolution.
int SurfaceDetailMask(float depthA, float depthB, float depthC) {
    float avg = (depthA + 2.0f * depthB + depthC) * 0.25f;
    if (avg < 1024.0f) return 0x7f;
    if (avg < 2048.0f) return 0x7e;
    if (avg < 3072.0f) return 0x7c;
    return 0x78;
}

// M123: `kFast` is the above-native variant -- see RasterizeTriangle below.
template <bool kFast>
void RasterizeTriangleT(Backbuffer& backbuffer, std::vector<float>& depthBuffer, const RowSpan& rows,
                        const ProjectedVertex& a, const ProjectedVertex& b,
                        const ProjectedVertex& c, const Zone& zone, int textureIndex,
                        uint8_t hueGroup) {
    float area = (b.sx - a.sx) * (c.sy - a.sy) - (b.sy - a.sy) * (c.sx - a.sx);
    if (std::fabs(area) < 1e-6f) return;
    const float invArea = 1.0f / area;  // M123, kFast only
    const int detailMask = SurfaceDetailMask(a.depthWorld, b.depthWorld, c.depthWorld);

    int minX = std::max(0, static_cast<int>(std::floor(std::min({a.sx, b.sx, c.sx}))));
    int maxX =
        std::min(backbuffer.width() - 1, static_cast<int>(std::ceil(std::max({a.sx, b.sx, c.sx}))));
    int minY = std::max(rows.begin, static_cast<int>(std::floor(std::min({a.sy, b.sy, c.sy}))));
    int maxY = std::min(rows.end - 1,
                         static_cast<int>(std::ceil(std::max({a.sy, b.sy, c.sy}))));
    if (minX > maxX || minY > maxY) return;

    for (int py = minY; py <= maxY; ++py) {
        int rowLo = minX, rowHi = maxX;  // M123: see ClipRowToTriangle
        ClipRowToTriangle(a, b, c, area, py, maxX - minX + 1, maxY - minY + 1, rowLo, rowHi);
        // M123: above native, the edge functions along a row are a start and
        // a slope (`w(x) = w(minX) + (x - minX) * dw`) instead of the full
        // form. Anchored at the box's own left edge, not the narrowed span's,
        // so a pixel's value never depends on where the narrowing began.
        const float rowSy = static_cast<float>(py) + 0.5f;
        const float rowSx = static_cast<float>(minX) + 0.5f;
        const float w0Lo = (b.sx - rowSx) * (c.sy - rowSy) - (b.sy - rowSy) * (c.sx - rowSx);
        const float w1Lo = (c.sx - rowSx) * (a.sy - rowSy) - (c.sy - rowSy) * (a.sx - rowSx);
        const float w2Lo = (a.sx - rowSx) * (b.sy - rowSy) - (a.sy - rowSy) * (b.sx - rowSx);
        const float dw0 = b.sy - c.sy, dw1 = c.sy - a.sy, dw2 = a.sy - b.sy;
        for (int px = rowLo; px <= rowHi; ++px) {
            float w0, w1, w2;
            if constexpr (kFast) {
                const float dx = static_cast<float>(px - minX);
                w0 = w0Lo + dx * dw0;
                w1 = w1Lo + dx * dw1;
                w2 = w2Lo + dx * dw2;
            } else {
                float sx = static_cast<float>(px) + 0.5f;
                float sy = static_cast<float>(py) + 0.5f;
                w0 = (b.sx - sx) * (c.sy - sy) - (b.sy - sy) * (c.sx - sx);
                w1 = (c.sx - sx) * (a.sy - sy) - (c.sy - sy) * (a.sx - sx);
                w2 = (a.sx - sx) * (b.sy - sy) - (a.sy - sy) * (b.sx - sx);
            }
            bool inside = (w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0);
            if (!inside) continue;
            float l0, l1, l2;
            if constexpr (kFast) {
                l0 = w0 * invArea;
                l1 = w1 * invArea;
                l2 = w2 * invArea;
            } else {
                l0 = w0 / area;
                l1 = w1 / area;
                l2 = w2 / area;
            }

            float invW = l0 * a.invW + l1 * b.invW + l2 * c.invW;
            if (invW <= 0.0f) continue;
            int depthIndex = py * backbuffer.width() + px;
            if (invW <= depthBuffer[static_cast<size_t>(depthIndex)]) continue;  // farther, skip

            float u, v;
            if constexpr (kFast) {
                const float rcpW = 1.0f / invW;
                u = (l0 * a.u + l1 * b.u + l2 * c.u) * rcpW;
                v = (l0 * a.v + l1 * b.v + l2 * c.v) * rcpW;
            } else {
                u = (l0 * a.u + l1 * b.u + l2 * c.u) / invW;
                v = (l0 * a.v + l1 * b.v + l2 * c.v) / invW;
            }
            // Real texel addressing: `mask & (uv >> 8)` -- a *wrap*, not a
            // clamp, so a surface whose real .sur shift makes its texture
            // repeat several times across a face genuinely tiles (see
            // zone.h's SurfaceUv()). u/v already carry texel units here,
            // so the >>8 is folded into that conversion; floor() handles
            // negative coordinates (real uOffset/vOffset values are often
            // negative) correctly before the mask wraps them.
            int tx = static_cast<int>(std::floor(u)) & detailMask;
            int ty = static_cast<int>(std::floor(v)) & detailMask;

            uint8_t texel = zone.TexelAt(textureIndex, tx, ty);
            // Per-pixel light, affinely interpolated across the triangle
            // exactly like the original's per-scanline interpolation of
            // the same value -- this is what selects the `.zlu` brightness
            // rung, so brightness is baked into the palette lookup rather
            // than applied as a post-hoc RGB scale (ExpandRGB444 gets a
            // flat 1.0 below, same as the unlit model rasterizer).
            float lightF = l0 * a.light + l1 * b.light + l2 * c.light;
            uint16_t lightLevel = static_cast<uint16_t>(
                std::clamp(lightF, 0.0f, static_cast<float>(kMaxLightLevel)));
            uint16_t raw444 = zone.PaletteColor(hueGroup, lightLevel, texel);
            if (raw444 == 0x0f0f) continue;  // chroma-key cutout (docs/GRAPHICS_FORMAT.md)

            // M89: the fog remap, last, on the finished colour word --
            // exactly where CompositeSceneBufferToScreen applies it. The
            // chroma-key test stays *above* it because the engine's
            // rasterizer tests the palette read, not the fogged result.
            const float fogF = l0 * a.fog + l1 * b.fog + l2 * c.fog;
            raw444 = zone.FogColor(FogLevelOf(fogF), raw444);

            depthBuffer[static_cast<size_t>(depthIndex)] = invW;
            backbuffer.SetPixel(px, py, ExpandRGB444Full(raw444));
        }
    }
}

// Same rasterizer shape as RasterizeTriangle above, but samples a
// model's own raw pixel data directly (no zone .sur/.ztx/.zlu
// palette/atlas indirection -- see world/model_archive.h) and u/v are
// already texel-space pixel coordinates rather than the tile renderer's
// 0..1 UVs (docs/MODEL_FORMAT.md's UV table is itself an 8.8-ish
// fixed-point pixel coordinate, so no extra *width/*height scale is
// needed here, just a clamp).
// `asSkybox` (M70) switches this to the behaviour of the engine's separate
// skybox rasterizer, `RoomFace_RasterizeTextured` (0x10055f38), whose whole
// inner loop is one store:
//
//     *dst = texel | 0x7fff0000;
//
// -- the same far-depth word `Render3DScene`'s flat background fill writes.
// No depth compare, no depth write, no light term, no chroma-key cutout.
// Here that means: don't read or write depthBuffer (so every wall and actor
// drawn afterwards passes its own test over the sky), and don't scale by
// the M35 light/fog factor. Skipping the cutout is faithful rather than
// load-bearing -- no shipped `.zsk` skin contains a single 0x0f0f texel.
template <bool kFast>
void RasterizeModelTriangleT(Backbuffer& backbuffer, std::vector<float>& depthBuffer,
                            const RowSpan& rows,
                             const ProjectedVertex& a, const ProjectedVertex& b,
                             const ProjectedVertex& c, const Model& model, int skinIndex,
                             bool asSkybox, uint8_t* objectIds,
                             uint8_t objectId, int flashLevel,
                             const Zone* zone) {
    // M66: a model with no skin pixels has nothing to sample. Every texel
    // would come back as the chroma key anyway (Model::TexelAt returns
    // 0x0f0f for any out-of-range coordinate), so this changes nothing
    // that reaches the screen -- but without it the `std::clamp(..., 0,
    // model.width - 1)` calls below are handed `hi < lo`, which is
    // undefined behaviour and which a Debug MSVC STL turns into an
    // outright `abort()` ("invalid bounds arguments passed to std::clamp").
    //
    // This is not hypothetical: nine of the twenty-one shipped zones --
    // broken1, broken2, crypt1, crypt2, crypt3, erthcave, ffarena,
    // lothcav and twilite -- ship a `.zsk` whose texture header reads
    // skinCount=256, width=256, **height=0**, so entering any of them killed
    // a Debug build on the first frame. M70 answers why those nine say that
    // and stops them arriving here at all (ParseSkyboxResource); this guard
    // stays because it is about the rasterizer's own precondition.
    if (model.width <= 0 || model.height <= 0 || model.skinCount <= 0) return;

    float area = (b.sx - a.sx) * (c.sy - a.sy) - (b.sy - a.sy) * (c.sx - a.sx);
    if (std::fabs(area) < 1e-6f) return;
    const float invArea = 1.0f / area;  // M123, kFast only

    int minX = std::max(0, static_cast<int>(std::floor(std::min({a.sx, b.sx, c.sx}))));
    int maxX =
        std::min(backbuffer.width() - 1, static_cast<int>(std::ceil(std::max({a.sx, b.sx, c.sx}))));
    int minY = std::max(rows.begin, static_cast<int>(std::floor(std::min({a.sy, b.sy, c.sy}))));
    int maxY = std::min(rows.end - 1,
                         static_cast<int>(std::ceil(std::max({a.sy, b.sy, c.sy}))));
    if (minX > maxX || minY > maxY) return;

    for (int py = minY; py <= maxY; ++py) {
        int rowLo = minX, rowHi = maxX;  // M123: see ClipRowToTriangle
        ClipRowToTriangle(a, b, c, area, py, maxX - minX + 1, maxY - minY + 1, rowLo, rowHi);
        // M123: above native, the edge functions along a row are a start and
        // a slope (`w(x) = w(minX) + (x - minX) * dw`) instead of the full
        // form. Anchored at the box's own left edge, not the narrowed span's,
        // so a pixel's value never depends on where the narrowing began.
        const float rowSy = static_cast<float>(py) + 0.5f;
        const float rowSx = static_cast<float>(minX) + 0.5f;
        const float w0Lo = (b.sx - rowSx) * (c.sy - rowSy) - (b.sy - rowSy) * (c.sx - rowSx);
        const float w1Lo = (c.sx - rowSx) * (a.sy - rowSy) - (c.sy - rowSy) * (a.sx - rowSx);
        const float w2Lo = (a.sx - rowSx) * (b.sy - rowSy) - (a.sy - rowSy) * (b.sx - rowSx);
        const float dw0 = b.sy - c.sy, dw1 = c.sy - a.sy, dw2 = a.sy - b.sy;
        for (int px = rowLo; px <= rowHi; ++px) {
            float w0, w1, w2;
            if constexpr (kFast) {
                const float dx = static_cast<float>(px - minX);
                w0 = w0Lo + dx * dw0;
                w1 = w1Lo + dx * dw1;
                w2 = w2Lo + dx * dw2;
            } else {
                float sx = static_cast<float>(px) + 0.5f;
                float sy = static_cast<float>(py) + 0.5f;
                w0 = (b.sx - sx) * (c.sy - sy) - (b.sy - sy) * (c.sx - sx);
                w1 = (c.sx - sx) * (a.sy - sy) - (c.sy - sy) * (a.sx - sx);
                w2 = (a.sx - sx) * (b.sy - sy) - (a.sy - sy) * (b.sx - sx);
            }
            bool inside = (w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0);
            if (!inside) continue;
            float l0, l1, l2;
            if constexpr (kFast) {
                l0 = w0 * invArea;
                l1 = w1 * invArea;
                l2 = w2 * invArea;
            } else {
                l0 = w0 / area;
                l1 = w1 / area;
                l2 = w2 / area;
            }

            float invW = l0 * a.invW + l1 * b.invW + l2 * c.invW;
            if (invW <= 0.0f) continue;
            int depthIndex = py * backbuffer.width() + px;
            if (!asSkybox && invW <= depthBuffer[static_cast<size_t>(depthIndex)]) {
                continue;  // farther, skip
            }
            // M123: the sky fills only what nothing opaque claimed.
            if (asSkybox && depthBuffer[static_cast<size_t>(depthIndex)] != 0.0f) continue;

            float u, v;
            if constexpr (kFast) {
                const float rcpW = 1.0f / invW;
                u = (l0 * a.u + l1 * b.u + l2 * c.u) * rcpW;
                v = (l0 * a.v + l1 * b.v + l2 * c.v) * rcpW;
            } else {
                u = (l0 * a.u + l1 * b.u + l2 * c.u) / invW;
                v = (l0 * a.v + l1 * b.v + l2 * c.v) / invW;
            }
            int tx = std::clamp(static_cast<int>(u), 0, model.width - 1);
            int ty = std::clamp(static_cast<int>(v), 0, model.height - 1);

            uint16_t raw444 = model.TexelAt(skinIndex, tx, ty);
            if (asSkybox) {
                backbuffer.SetPixel(px, py, ExpandRGB444Full(raw444));
                continue;
            }
            if (raw444 == 0x0f0f) continue;  // chroma-key cutout

            depthBuffer[static_cast<size_t>(depthIndex)] = invW;
            // M89 -- **models are not lit at all**, and M35's RGB scale by
            // cell light is why every large model, every roof and every
            // loot bag in this port read far too dark.
            //
            // M35 wrote, correctly, that the actor pipeline computes
            // `intensity = clamp(vertexZ * engine[+0x5c8] >> 8, 0,
            // 0xffff)`, ORs its top nibble into the colour word, and lets
            // CompositeSceneBufferToScreen resolve that through the
            // `engine+0x5c4` table -- and then, because neither the factor
            // nor the table had been extracted, substituted the *surface*
            // pipeline's `cellLight - depth/2` as an RGB scale.
            //
            // Both are extracted now. `engine+0x5c8` is
            // `0x10000 / (visibilityTier.maxSteps / 2)`, written at the
            // top of TileGrid_RaycastVisibility; `engine+0x5c4` is the
            // whole of `<zone>.zfg`, a ninth per-zone file. And with the
            // real path in place the substitution has to go: reading
            // `Poly3D_RasterizeTextured_v3`'s inner loop, the pixel it
            // stores is
            //
            //     *dst = texel | (fog & 0xf000) | (depth << 16);
            //
            // where `texel` is a straight 16bpp read out of the model's
            // own skin. No palette, no rung, no light term anywhere in the
            // actor pipeline -- a model carries its own lit-looking RGB444
            // artwork and the engine only fogs it. So: full texture
            // colour, fog, nothing else.
            //
            // M85: the stencil half of the real actor rasterizers --
            // `*(u8 *)(engine + 0x5b8 + x + y * 0xb0) = objectId`, at
            // every pixel actually drawn, after the chroma-key cutout and
            // after the depth test, which is where the engine has it too.
            // That ordering is the whole point: what the buffer holds is
            // what you can *see*, so a creature behind a wall or behind
            // another creature cannot be the one the melee search finds.
            if (objectIds != nullptr && objectId != 0) {
                objectIds[static_cast<size_t>(depthIndex)] = objectId;
            }
            // M86: `Poly3D_RasterizeTextured_v8`'s replacement for the
            // texel -- a read out of the ramp row `flashLevel`, indexed by
            // the texel's own red nibble, with no light term (v8 is one of
            // the fade=N variants). See zone_renderer.h's HitFlashRamps.
            if (flashLevel >= 0) {
                const HitFlashRamps& ramps = FlashRamps();
                const int row = std::clamp(flashLevel, 0, 13);
                const int step = (raw444 >> 8) & 0xF;
                backbuffer.SetPixel(px, py, ExpandRGB444Full(ramps.entry[row][step]));
                continue;
            }
            const float fogF = l0 * a.fog + l1 * b.fog + l2 * c.fog;
            if (zone != nullptr) raw444 = zone->FogColor(FogLevelOf(fogF), raw444);
            backbuffer.SetPixel(px, py, ExpandRGB444Full(raw444));
        }
    }
}

// M123: **the above-native rasterizers trade the last bit for speed.** Five
// float divides a pixel (three barycentric `w / area`, two perspective
// `/ invW`) become one reciprocal and multiplies. That moves results in the
// last bit, so it is used only when the frame is larger than the device's:
// at Resolution Original every pixel is still the arithmetic the tracked
// .ppm dumps pin. Chosen by the target's size, never by the band schedule,
// so a banded frame and a one-pass frame of the same size stay identical.
void RasterizeTriangle(Backbuffer& backbuffer, std::vector<float>& depthBuffer, const RowSpan& rows,
                        const ProjectedVertex& a, const ProjectedVertex& b,
                        const ProjectedVertex& c, const Zone& zone, int textureIndex,
                        uint8_t hueGroup) {
    if (backbuffer.width() > Backbuffer::kWidth) {
        RasterizeTriangleT<true>(backbuffer, depthBuffer, rows, a, b, c, zone, textureIndex,
                                 hueGroup);
    } else {
        RasterizeTriangleT<false>(backbuffer, depthBuffer, rows, a, b, c, zone, textureIndex,
                                  hueGroup);
    }
}

void RasterizeModelTriangle(Backbuffer& backbuffer, std::vector<float>& depthBuffer,
                            const RowSpan& rows, const ProjectedVertex& a,
                            const ProjectedVertex& b, const ProjectedVertex& c,
                            const Model& model, int skinIndex, bool asSkybox = false,
                            uint8_t* objectIds = nullptr, uint8_t objectId = 0,
                            int flashLevel = -1, const Zone* zone = nullptr) {
    if (backbuffer.width() > Backbuffer::kWidth) {
        RasterizeModelTriangleT<true>(backbuffer, depthBuffer, rows, a, b, c, model, skinIndex,
                                      asSkybox, objectIds, objectId, flashLevel, zone);
    } else {
        RasterizeModelTriangleT<false>(backbuffer, depthBuffer, rows, a, b, c, model, skinIndex,
                                       asSkybox, objectIds, objectId, flashLevel, zone);
    }
}

// M119: a billboard after projection -- what FillBillboard needs to draw it.
struct BillboardQuad {
    const Sprite* sprite;
    float x0, x1, y0, y1, spanX, spanY, invW;
    int blendMode, level;
};

// M119: the second half of the billboard blit -- everything after the quad
// is projected -- split out so a recorded frame can replay it per band.
void FillBillboard(Backbuffer& backbuffer, std::vector<float>& depthBuffer, const RowSpan& rows,
                   const BillboardQuad& q) {
    const Sprite* sprite = q.sprite;
    const float x0 = q.x0, x1 = q.x1, y0 = q.y0, y1 = q.y1;
    const float spanX = q.spanX, spanY = q.spanY, invW = q.invW;
    const int blendMode = q.blendMode, level = q.level;
    const int px0 = std::max(0, static_cast<int>(std::floor(x0)));
    const int px1 = std::min(backbuffer.width(), static_cast<int>(std::ceil(x1)));
    const int py0 = std::max(rows.begin, static_cast<int>(std::floor(y0)));
    const int py1 = std::min(rows.end, static_cast<int>(std::ceil(y1)));

    for (int py = py0; py < py1; ++py) {
        const int sy = static_cast<int>((static_cast<float>(py) + 0.5f - y0) / spanY *
                                         static_cast<float>(sprite->height));
        if (sy < 0 || sy >= sprite->height) continue;
        uint16_t* row = backbuffer.Row(py);
        for (int px = px0; px < px1; ++px) {
            const int sx = static_cast<int>((static_cast<float>(px) + 0.5f - x0) / spanX *
                                             static_cast<float>(sprite->width));
            if (sx < 0 || sx >= sprite->width) continue;
            const size_t si = static_cast<size_t>(sy) * static_cast<size_t>(sprite->width) +
                              static_cast<size_t>(sx);
            // The real blit's own colour-key test (`!= 0x0f0f`), which the
            // decoder already turned into this mask.
            if (!sprite->opaque[si]) continue;
            const size_t di = static_cast<size_t>(py) * backbuffer.width() + static_cast<size_t>(px);
            if (invW <= depthBuffer[di]) continue;  // something nearer is already there
            const uint16_t src = sprite->pixels[si];
            uint16_t out = src;
            if (blendMode == 1) {
                // `(src >> k & mask) + (dst >> k & mask)` on 4-bit channels
                // in the original; the same weights on this port's RGB565.
                const uint16_t dst = row[px];
                auto mix = [&](int shift, int mask) {
                    const int s = (src >> shift) & mask;
                    const int d = (dst >> shift) & mask;
                    int v = 0;
                    if (level == 1) v = (s + 3 * d) / 4;
                    else if (level == 2) v = (s + d) / 2;
                    else v = (3 * s + d) / 4;
                    return v > mask ? mask : v;
                };
                out = static_cast<uint16_t>((mix(11, 0x1f) << 11) | (mix(5, 0x3f) << 5) |
                                             mix(0, 0x1f));
            }
            row[px] = out;
            depthBuffer[di] = invW;
        }
    }
}

// M119: where the submit functions below send each primitive once it is
// projected.
//
// A *direct* sink rasterizes it on the spot over the whole frame -- the
// single pass this renderer always was, and still is at the original
// 176x208. A *recording* sink stores it instead, in submission order, so
// that at a higher Resolution the transform, clip and projection happen
// once and the horizontal bands (see ZoneRenderer::Render) only rasterize:
// each band replays the list over its own rows. Replaying calls exactly the
// rasterizer the direct sink would have, with exactly the values it would
// have passed, so a band's pixels are the one-pass frame's pixels.
class TriangleSink {
public:
    TriangleSink(Backbuffer& backbuffer, std::vector<float>& depthBuffer, bool recording)
        : backbuffer_(&backbuffer), depthBuffer_(&depthBuffer), recording_(recording),
          width_(backbuffer.width()), height_(backbuffer.height()) {}

    // M129: a sink that fills a GpuFrame (render3d/gpu_frame.h) -- the same
    // projected triangles, for a graphics card to rasterize. There is no
    // backbuffer and no depth plane on this side at all.
    TriangleSink(GpuFrame& gpu, int width, int height)
        : backbuffer_(nullptr), depthBuffer_(nullptr), recording_(true), gpu_(&gpu),
          width_(width), height_(height) {}

    int width() const { return width_; }
    int height() const { return height_; }

    void Surface(const ProjectedVertex& a, const ProjectedVertex& b, const ProjectedVertex& c,
                 const Zone& zone, int textureIndex, uint8_t hueGroup) {
        if (gpu_) {
            const uint32_t params =
                (static_cast<uint32_t>(textureIndex) & 0xffu) |
                (static_cast<uint32_t>(hueGroup & 0x3) << 8) |
                (static_cast<uint32_t>(SurfaceDetailMask(a.depthWorld, b.depthWorld, c.depthWorld))
                 << 16);
            GpuBatch& batch = GpuBatchFor(GpuBatch::Kind::Surface, nullptr, 0, -1);
            GpuEmit(batch, a, params);
            GpuEmit(batch, b, params);
            GpuEmit(batch, c, params);
            return;
        }
        if (!recording_) {
            RasterizeTriangle(*backbuffer_, *depthBuffer_, FullFrame(), a, b, c, zone, textureIndex,
                              hueGroup);
            return;
        }
        surfaces_.push_back(SurfaceTri{a, b, c, &zone, textureIndex, hueGroup});
        Record(Kind::Surface, surfaces_.size() - 1, a, b, c);
    }

    void ModelTriangle(const ProjectedVertex& a, const ProjectedVertex& b,
                       const ProjectedVertex& c, const Model& model, int skinIndex, bool asSkybox,
                       uint8_t* objectIds, uint8_t objectId, int flashLevel, const Zone* zone) {
        if (gpu_) {
            GpuBatch& batch = GpuBatchFor(asSkybox ? GpuBatch::Kind::Sky : GpuBatch::Kind::Model,
                                          &model, skinIndex, asSkybox ? -1 : flashLevel);
            GpuEmit(batch, a, 0);
            GpuEmit(batch, b, 0);
            GpuEmit(batch, c, 0);
            return;
        }
        if (!recording_) {
            RasterizeModelTriangle(*backbuffer_, *depthBuffer_, FullFrame(), a, b, c, model,
                                   skinIndex, asSkybox, objectIds, objectId, flashLevel, zone);
            return;
        }
        models_.push_back(
            ModelTri{a, b, c, &model, skinIndex, asSkybox, objectIds, objectId, flashLevel, zone});
        Record(Kind::Model, models_.size() - 1, a, b, c);
    }

    void Billboard(const BillboardQuad& q) {
        if (gpu_) {
            // Two triangles over the quad. `u`/`v` run 0..1 across the sprite
            // and the depth is the one value the software blit tests with.
            GpuBatch batch;
            batch.kind = GpuBatch::Kind::Billboard;
            batch.firstVertex = static_cast<uint32_t>(gpu_->vertices.size());
            batch.sprite = q.sprite;
            batch.blendMode = q.blendMode;
            batch.blendLevel = q.level;
            const float xs[2] = {q.x0, q.x1}, ys[2] = {q.y0, q.y1};
            constexpr int kCorners[6][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}};
            for (const auto& corner : kCorners) {
                GpuVertex v;
                v.x = xs[corner[0]];
                v.y = ys[corner[1]];
                v.invW = q.invW;
                v.uOverW = static_cast<float>(corner[0]) * q.invW;
                v.vOverW = static_cast<float>(corner[1]) * q.invW;
                gpu_->vertices.push_back(v);
            }
            batch.vertexCount = 6;
            gpu_->batches.push_back(batch);
            return;
        }
        if (!recording_) {
            FillBillboard(*backbuffer_, *depthBuffer_, FullFrame(), q);
            return;
        }
        quads_.push_back(q);
        commands_.push_back(Command{Kind::Billboard, quads_.size() - 1,
                                    static_cast<int>(std::floor(q.y0)),
                                    static_cast<int>(std::ceil(q.y1))});
    }

    // Draws every recorded primitive, in order, clipped to `rows`.
    void Replay(const RowSpan& rows) const {
        for (const Command& cmd : commands_) {
            if (cmd.maxY < rows.begin || cmd.minY >= rows.end) continue;
            switch (cmd.kind) {
                case Kind::Surface: {
                    const SurfaceTri& t = surfaces_[cmd.index];
                    RasterizeTriangle(*backbuffer_, *depthBuffer_, rows, t.a, t.b, t.c, *t.zone,
                                      t.textureIndex, t.hueGroup);
                    break;
                }
                case Kind::Model: {
                    const ModelTri& t = models_[cmd.index];
                    RasterizeModelTriangle(*backbuffer_, *depthBuffer_, rows, t.a, t.b, t.c,
                                           *t.model, t.skinIndex, t.asSkybox, t.objectIds,
                                           t.objectId, t.flashLevel, t.zone);
                    break;
                }
                case Kind::Billboard:
                    FillBillboard(*backbuffer_, *depthBuffer_, rows, quads_[cmd.index]);
                    break;
            }
        }
    }

private:
    enum class Kind : uint8_t { Surface, Model, Billboard };
    struct Command {
        Kind kind;
        size_t index;
        int minY, maxY;  // a conservative row range, for skipping whole bands
    };
    struct SurfaceTri {
        ProjectedVertex a, b, c;
        const Zone* zone;
        int textureIndex;
        uint8_t hueGroup;
    };
    struct ModelTri {
        ProjectedVertex a, b, c;
        const Model* model;
        int skinIndex;
        bool asSkybox;
        uint8_t* objectIds;
        uint8_t objectId;
        int flashLevel;
        const Zone* zone;
    };

    RowSpan FullFrame() const { return RowSpan{0, height_}; }

    // M129: the batch a GPU triangle of this kind joins -- the last one when
    // it draws with the same state, else a new one. Surfaces carry their
    // texture, hue and detail mask per vertex, so every run of them is one
    // batch whatever they are textured with.
    GpuBatch& GpuBatchFor(GpuBatch::Kind kind, const Model* model, int skinIndex, int flashLevel) {
        if (!gpu_->batches.empty()) {
            GpuBatch& last = gpu_->batches.back();
            if (last.kind == kind && last.model == model && last.skinIndex == skinIndex &&
                last.flashLevel == flashLevel && kind != GpuBatch::Kind::Billboard) {
                return last;
            }
        }
        GpuBatch batch;
        batch.kind = kind;
        batch.firstVertex = static_cast<uint32_t>(gpu_->vertices.size());
        batch.model = model;
        batch.skinIndex = skinIndex;
        batch.flashLevel = flashLevel;
        gpu_->batches.push_back(batch);
        return gpu_->batches.back();
    }

    void GpuEmit(GpuBatch& batch, const ProjectedVertex& p, uint32_t params) {
        GpuVertex v;
        v.x = p.sx;
        v.y = p.sy;
        v.invW = p.invW;
        v.uOverW = p.u;
        v.vOverW = p.v;
        v.light = p.light;
        v.fog = p.fog;
        v.params = params;
        gpu_->vertices.push_back(v);
        ++batch.vertexCount;
    }

    void Record(Kind kind, size_t index, const ProjectedVertex& a, const ProjectedVertex& b,
                const ProjectedVertex& c) {
        commands_.push_back(Command{kind, index,
                                    static_cast<int>(std::floor(std::min({a.sy, b.sy, c.sy}))),
                                    static_cast<int>(std::ceil(std::max({a.sy, b.sy, c.sy})))});
    }

    Backbuffer* backbuffer_;
    std::vector<float>* depthBuffer_;
    bool recording_;
    GpuFrame* gpu_ = nullptr;  // M129
    int width_ = 0, height_ = 0;
    std::vector<Command> commands_;
    std::vector<SurfaceTri> surfaces_;
    std::vector<ModelTri> models_;
    std::vector<BillboardQuad> quads_;
};

// Transforms and rasterizes every face of one model instance, positioned
// by a tile-space X/Y offset plus a raw-world-unit Z offset -- shared by
// M8's per-entity placement loop and M70's skybox (which passes the
// camera's own position as its offset -- see the skybox block at the top of
// ZoneRenderer::Render, and zone_renderer.h's header comment for why).
// See RasterizeModelTriangle's own header comment for the local-axis/scale
// assumptions baked into the per-vertex transform below.
// asSkybox (M70): draw with the engine's skybox rasterizer semantics --
// unlit, and neither tested against nor written into the depth buffer.
// The per-vertex cell-light lookup is skipped with it, since a skybox
// vertex has no meaningful tile to stand on.
// entityYaw (M15): local rotation around the vertical axis, radians --
// applied to each vertex's local X/Z before the camera transform, so a
// door instance (see zone_renderer.h's PlacedEntity::yaw) visibly swings
// open. Zero for every caller before M15 (the skybox, and every entity
// but a live door), so this is purely additive -- cosEntityYaw=1/
// sinEntityYaw=0 reduces the new rotation to a no-op.
void SubmitModel(TriangleSink& sink, const Zone& zone, const Model& model, int skinIndex, float offsetTileX, float offsetTileY,
                  float offsetZ, float camX, float camY, float camZ, float cosYaw, float sinYaw,
                  float cosPitch, float sinPitch, float focalX, float focalY,
                  float entityYaw = 0.0f, float scale = 1.0f, int frameIndex = 0,
                  bool asSkybox = false, float rotA = 0.0f, float rotB = 0.0f,
                  uint8_t* objectIds = nullptr, uint8_t objectId = 0, int flashLevel = -1,
                  int fogPercent = 100) {
    // M35: see kModelForwardYawOffset -- a model's forward is its local
    // +Z, so a heading measured from world +X needs a quarter turn taken
    // out of it before it can be used as a rotation of the local axes.
    // M70: the skybox's rotation isn't a heading at all -- it is the fixed
    // -0x4000 the engine hands its own transform (see kSkyboxYaw), applied
    // in the same place a placed entity's converted heading lands.
    float modelYaw = asSkybox ? kSkyboxYaw : entityYaw + kModelForwardYawOffset;

    // M71 -- the placement transform, now `BuildRotationMatrix3x4`
    // (0x10073a70) verbatim instead of the yaw-only special case that
    // stood since M15.
    //
    // The engine builds an actor's rotation from **three** angles, read off
    // the object at `+0xa8`, `+0xb2` and `+0xb6` -- which
    // docs/ZONE_FORMAT.md already traced back to the `.ent` record's
    // `rotOrScale[2]`, `rotOrScale[0]` and `unkA`'s low halfword. Only the
    // third was ever plumbed through here, so the 232 placements carrying a
    // real `+0xb2` and the 204 carrying a real `+0xa8` (out of 8,258 across
    // the 21 zones) all drew bolt upright.
    //
    // Transcribing the matrix means naming its angles the way the *call*
    // does, not the way a human would: `A` is the object's `+0xa8` channel,
    // `B` its `+0xb2`, and `C` its heading. The third argument the engine
    // passes is `heading + 0x8000` -- a **half turn** on top of the stored
    // heading, which is the other half of the facing bug M71 fixed (see
    // main.cpp's PlacementYawRadians). Expressed against this port's own
    // `entityYaw`, that whole chain collapses to `C = -modelYaw`:
    //
    //     entityYaw = pi/2 - heading           (main.cpp, engine -> port)
    //     modelYaw  = entityYaw + pi/2         (kModelForwardYawOffset)
    //               = pi - heading
    //     C         = heading + pi == -modelYaw   (mod 2pi)
    //
    // so the yaw-only case below reduces to exactly the two lines this
    // function used to have, and A == B == 0 (97% of all placements, and
    // every door, monster, projectile and the skybox) costs nothing.
    const float cA = std::cos(rotA), sA = std::sin(rotA);
    const float cB = std::cos(rotB), sB = std::sin(rotB);
    const float cC = std::cos(-modelYaw), sC = std::sin(-modelYaw);
    // dst[0..2] / [4..6] / [8..10] of BuildRotationMatrix3x4, in the
    // engine's own (worldX, up, worldY) row order.
    const float m0 = cA * cC - sC * sB * sA;
    const float m1 = sC * sB * cA + sA * cC;
    const float m2 = sC * cB;
    const float m4 = -cB * sA;
    const float m5 = cB * cA;
    const float m6 = -sB;
    const float m8 = -cC * sB * sA - sC * cA;
    const float m9 = cC * sB * cA - sC * sA;
    const float m10 = cB * cC;
    for (const ModelFace& face : model.faces) {
        if (face.vA < 0 || face.vB < 0 || face.vC < 0 ||
            face.vC >= model.vertsPerFrame || face.vB >= model.vertsPerFrame ||
            face.vA >= model.vertsPerFrame || face.uA < 0 || face.uB < 0 ||
            face.uC < 0 || static_cast<size_t>(face.uC) >= model.uvs.size() ||
            static_cast<size_t>(face.uB) >= model.uvs.size() ||
            static_cast<size_t>(face.uA) >= model.uvs.size()) {
            continue;  // shouldn't happen (MODEL_FORMAT.md's index invariant), guard anyway
        }

        // M28: sample the *current animation frame's* vertex block, not
        // always frame 0 -- see world/model_archive.h's AnimationClip.
        const ModelVertex* mv[3] = {&model.VertexAt(frameIndex, face.vA),
                                     &model.VertexAt(frameIndex, face.vB),
                                     &model.VertexAt(frameIndex, face.vC)};
        const ModelUv* uv[3] = {&model.uvs[static_cast<size_t>(face.uA)],
                                 &model.uvs[static_cast<size_t>(face.uB)],
                                 &model.uvs[static_cast<size_t>(face.uC)]};

        ViewVertex vv3[3];
        for (int i = 0; i < 3; ++i) {
            // Local X/Z rotated about the model's own origin before the
            // world-space offset/camera transform -- local Y (up) is
            // untouched, matching a door swinging on a vertical hinge.
            // Uniform per-instance scale about the model's own origin
            // (SetScale's 8.8 value, PlacedEntity::scale) applied before
            // the yaw rotation and world offset -- so a scaled creature
            // still stands on the same ground point.
            float sxv = mv[i]->x * scale, syv = mv[i]->y * scale, szv = mv[i]->z * scale;
            // The three rows above, applied in the engine's own operand
            // order: local (x, y, z) -> world (X, up, Y).
            float lx = m0 * sxv + m1 * syv + m2 * szv;
            float lup = m4 * sxv + m5 * syv + m6 * szv;
            float lz = m8 * sxv + m9 * syv + m10 * szv;

            float rx = offsetTileX + lx / kTileScale - camX;
            float ry = offsetTileY + lz / kTileScale - camY;
            float rz = (offsetZ + lup) / kTileScale - camZ;

            vv3[i].right = rx * sinYaw - ry * cosYaw;
            float flatForward = rx * cosYaw + ry * sinYaw;
            // Pitch rotates the (forward, up) pair -- see Camera::pitch.
            vv3[i].forward = flatForward * cosPitch - rz * sinPitch;
            vv3[i].up = rz * cosPitch + flatForward * sinPitch;
            vv3[i].u = uv[i]->u / 256.0f;
            vv3[i].v = uv[i]->v / 256.0f;

            // M89: no per-vertex cell light here any more. The actor
            // pipeline has no light term at all -- see
            // RasterizeModelTriangle -- so M35's tile-grid light lookup
            // and the RGB scale it fed are both gone, and the only thing
            // distance does to a model is fog it. `light` stays full for
            // any caller that still reads it.
            vv3[i].light = static_cast<float>(kMaxLightLevel);
        }

        // Same real near-plane clip the tile-grid pipeline above uses --
        // previously a model triangle with any vertex behind the eye was
        // dropped whole, which made a model visibly vanish in chunks the
        // moment the player walked up to it.
        ViewVertex clipped[4];
        int clippedCount = ClipNear(vv3, 3, clipped, 4);
        if (clippedCount < 3) continue;

        ProjectedVertex pvc[4];
        for (int i = 0; i < clippedCount; ++i) {
            pvc[i].invW = 1.0f / clipped[i].forward;
            pvc[i].sx = sink.width() * 0.5f + clipped[i].right * focalX * pvc[i].invW;
            pvc[i].sy = sink.height() * 0.5f - clipped[i].up * focalY * pvc[i].invW;
            pvc[i].depthWorld = clipped[i].forward * kTileScale;
            pvc[i].u = clipped[i].u * pvc[i].invW;
            pvc[i].v = clipped[i].v * pvc[i].invW;
            pvc[i].light = clipped[i].light;  // M35
            pvc[i].fog = FogScalar(pvc[i].depthWorld, fogPercent);  // M89, M123
        }
        for (int i = 1; i + 1 < clippedCount; ++i) {
            sink.ModelTriangle(pvc[0], pvc[i], pvc[i + 1], model, skinIndex, asSkybox, objectIds,
                               objectId, flashLevel, &zone);
        }
    }
}

// M79 -- `FUN_1008b25c` + `FUN_1004f91c` + `FUN_1004f218`'s blit, for one
// screen-aligned `global.spr` quad. See zone_renderer.h's SpriteBillboard
// block for where every constant here comes from.
//
// Depth: the engine's framebuffer word packs the quad's depth into its high
// half and the blit's per-pixel test is `myDepth < storedDepth`, writing the
// whole word on success -- i.e. it both tests and writes, like the mesh
// pipeline. Reproduced with this port's own `invW` convention (larger is
// nearer), which makes the test the same comparison the model rasteriser
// already uses.
void SubmitBillboard(TriangleSink& sink,
                      const SpriteBillboard& bb, float camX, float camY, float camZ,
                      float cosYaw, float sinYaw, float cosPitch, float sinPitch, float focalX,
                      float focalY) {
    const Sprite* sprite = bb.sprite;
    if (!sprite || sprite->width <= 0 || sprite->height <= 0) return;

    // `FUN_1004f218`'s opening: the blend level decides whether there is
    // anything to draw at all, and a level of 4 collapses back to the plain
    // copy. The rounding (`+ 0x1f`, or `+ 0x5e` when negative) is the
    // engine's own.
    int blendMode = bb.blendMode;
    int level = bb.blendLevel;
    if (blendMode == 1) {
        int rounded = level + 0x1f;
        if (rounded < 0) rounded = level + 0x5e;
        level = rounded >> 6;
        if (level == 0) return;
        if (level == 4) blendMode = 0;
    }

    // Camera space, in the tile units the rest of this file works in.
    const float rx = bb.x / kTileScale - camX;
    const float ry = bb.y / kTileScale - camY;
    const float rz = bb.z / kTileScale - camZ;
    const float right = rx * sinYaw - ry * cosYaw;
    const float flatForward = rx * cosYaw + ry * sinYaw;
    const float forward = flatForward * cosPitch - rz * sinPitch;
    const float up = rz * cosPitch + flatForward * sinPitch;
    const float depthWorld = forward * kTileScale;
    if (depthWorld < kBillboardNearDepth) return;  // FUN_1004f91c's own cull

    const float halfW = static_cast<float>(bb.halfWidth) / kTileScale;
    const float halfH = static_cast<float>(bb.halfHeight) / kTileScale;
    const float invW = 1.0f / forward;
    // The two corners the engine projects, and only those two: a billboard
    // is axis-aligned on screen, so everything between them is a stretch.
    const float x0 = sink.width() * 0.5f + (right - halfW) * focalX * invW;
    const float x1 = sink.width() * 0.5f + (right + halfW) * focalX * invW;
    const float y1 = sink.height() * 0.5f - up * focalY * invW;  // bottom
    const float y0 = sink.height() * 0.5f - (up + 2.0f * halfH) * focalY * invW;  // top
    const float spanX = x1 - x0, spanY = y1 - y0;
    if (spanX <= 0.0f || spanY <= 0.0f) return;

    sink.Billboard(BillboardQuad{sprite, x0, x1, y0, y1, spanX, spanY, invW, blendMode, level});
}

}  // namespace

void ZoneRenderer::Render(Backbuffer& backbuffer, const Zone& zone, const Camera& camera,
                           const std::vector<PlacedEntity>& entities, ModelArchive* models,
                           const std::vector<SpriteBillboard>& billboards) const {
    RenderInternal(&backbuffer, nullptr, backbuffer.width(), backbuffer.height(), zone, camera,
                   entities, models, billboards);
}

// M129: the same frame, recorded for a graphics card instead of rasterized
// here. Everything up to the rasterizer is the code Render() runs -- the
// raycast, the faces, the transform, clip and projection -- so the two paths
// cannot drift apart; see render3d/gpu_frame.h.
int ZoneRenderer::SceneLightBonus(int tx, int ty, uint32_t salt, bool* pinned) const {
    int bonus = 0;
    if (lights.playerGlow) {
        const int dx = tx > lights.playerTileX ? tx - lights.playerTileX : lights.playerTileX - tx;
        const int dy = ty > lights.playerTileY ? ty - lights.playerTileY : lights.playerTileY - ty;
        const int d = dx > dy ? dx : dy;
        if (d <= 3) bonus += (3 - d) * 0x400;
    }
    uint32_t n = 0;
    for (const std::pair<int, int>& e : lights.emitterTiles) {
        ++n;
        if (e.first != tx || e.second != ty) continue;
        // One draw of the engine's generator per emitter per vertex.
        uint32_t h = lights.seed * 2654435761u ^ (static_cast<uint32_t>(tx) * 73856093u) ^
                     (static_cast<uint32_t>(ty) * 19349663u) ^ (salt * 83492791u) ^ (n * 2246822519u);
        h ^= h >> 15;
        h *= 2246822519u;
        h ^= h >> 13;
        bonus += 2 + static_cast<int>(h & 3u) * 0x100;
    }
    for (const std::pair<int, int>& e : lights.fullTiles) {
        if (e.first == tx && e.second == ty) {
            if (pinned) *pinned = true;
            break;
        }
    }
    return bonus;
}

void ZoneRenderer::RenderGpu(GpuFrame& frame, int width, int height, const Zone& zone,
                              const Camera& camera, const std::vector<PlacedEntity>& entities,
                              ModelArchive* models,
                              const std::vector<SpriteBillboard>& billboards) const {
    frame.Clear();
    frame.width = width;
    frame.height = height;
    frame.zone = &zone;
    frame.background565 = kBackgroundFill;
    if (width <= 0 || height <= 0) return;
    RenderInternal(nullptr, &frame, width, height, zone, camera, entities, models, billboards);
}

void ZoneRenderer::RenderInternal(Backbuffer* backbufferPtr, GpuFrame* gpuFrame, int targetWidth,
                                   int targetHeight, const Zone& zone, const Camera& camera,
                                   const std::vector<PlacedEntity>& entities, ModelArchive* models,
                                   const std::vector<SpriteBillboard>& billboards) const {
    // The engine's flat background fill: `Render3DScene` writes
    // `engine+0x630 | 0x7fff0000` into all 176*208 scene words when there
    // is no skybox to draw. Here it also backstops the skybox itself, which
    // in practice covers the frame -- the mesh is a closed dome around the
    // camera. (The colour is this port's own; `engine+0x630`'s value was
    // never traced.)
    const auto frameStart = std::chrono::steady_clock::now();  // M126: lastTimings()
    // M119: at the original 176x208 the frame is drawn in one pass on this
    // thread, exactly as it always was. A higher Resolution multiplies the
    // pixels by the square of its scale, so there the passes are recorded
    // and the rows split into bands rasterized on the shared worker pool --
    // same pixels, same arithmetic, a fraction of the time.
    const int height = targetHeight;
    int bands = 1;
    if (backbufferPtr && targetWidth > Backbuffer::kWidth) {
        bands = std::max(1, std::min(height / 16, BandPool::Shared().threadCount() * 3));
        if (bandLimit > 0) bands = std::min(bands, bandLimit);
    }
    // M126: and in bands, each band clears its own rows of all three planes
    // just before it draws them (see the dispatch below), instead of this
    // thread clearing ~19MB at 6x before anything else can start.
    const bool clearInBands = bands > 1;
    if (backbufferPtr && !clearInBands) backbufferPtr->Fill(kBackgroundFill);
    // M85: the object-ID buffer starts every frame empty, the way
    // `FUN_10015354`'s `memset(engine+0x5b8, 0, 0x8f00)` clears the real
    // one (0x8f00 == 176 * 208) before the scene is drawn.
    //
    // M119: both per-pixel planes are the size of whatever this frame is
    // drawn into -- the native 176x208, or a whole multiple of it when the
    // player picked a higher Resolution -- and the depth plane is kept
    // between frames instead of reallocated. Every entry of both is reset
    // here, so a frame never sees the previous one's values either way.
    //
    // M129: a GPU frame touches none of them. The object-ID buffer in
    // particular keeps describing the last frame the *software* path drew,
    // which is what the melee probes read (main.cpp draws one at the native
    // size on every gameplay tick for exactly that).
    const size_t pixelCount = static_cast<size_t>(targetWidth) * targetHeight;
    std::vector<float>& depthBuffer = m_DepthBuffer;
    if (backbufferPtr) {
        m_IdWidth = targetWidth;
        m_IdHeight = targetHeight;
        if (clearInBands) {
            m_ObjectIds.resize(pixelCount);
            depthBuffer.resize(pixelCount);
        } else {
            m_ObjectIds.assign(pixelCount, static_cast<uint8_t>(0));
            depthBuffer.assign(pixelCount, 0.0f);
        }
    }

    // M41: the real visible-tile set. `TileGrid_RaycastVisibility` runs
    // once per frame in the original, immediately before its face-drawing
    // loop, and this pipeline is that loop -- so the two are called in the
    // same order and for the same reason. Replaces M6's fixed-radius
    // square scan, the last placeholder left in this pipeline.
    std::vector<std::pair<int, int>> visibleTiles;
    if (fogPercent >= 100) {
        visibleTiles = zone.RaycastVisibleTiles(camera.x, camera.y, camera.yaw);
    } else {
        size_t softCap = 0, hardCap = 0;  // M123
        const Zone::VisibilityTier tier = VisibilityTierForFog(fogPercent, softCap, hardCap);
        visibleTiles =
            zone.RaycastVisibleTiles(camera.x, camera.y, camera.yaw, tier, softCap, hardCap);
    }
    std::vector<Face> faces = CollectFaces(zone, visibleTiles, camera.x, camera.y, camera.z);

    float camX = camera.x / kTileScale, camY = camera.y / kTileScale, camZ = camera.z / kTileScale;
    float cosYaw = std::cos(camera.yaw), sinYaw = std::sin(camera.yaw);
    float cosPitch = std::cos(camera.pitch), sinPitch = std::sin(camera.pitch);
    // forward = (cosYaw, sinYaw, 0); right = (sinYaw, -cosYaw, 0); up = (0,0,1)
    // M71: the real engine's projection. `focalX == focalY` is not a
    // "square-ish texel assumption" as this line used to say -- it is what
    // the binary does, and now for the decompiled reason: the two divides
    // use different constants (0x5800 = 88 across, 0x6800 = 104 down) but
    // `Render3DScene` pre-scales the camera matrix's screen-right row by
    // 208/176, which makes the horizontal focal length 88 * 208/176 = 104
    // as well. At the real `fovY` (kEngineFovY, pi/2) the expression below
    // evaluates to exactly 104, i.e. `0x6800 >> 8`. See camera.h.
    float focalY = (targetHeight * 0.5f) / std::tan(camera.fovY * 0.5f);
    float focalX = focalY;

    // M119: every model the entities name, resolved here and not in the
    // passes below. The archive parses a model the first time it is asked
    // for it, which is a write, and the passes may run on several threads.
    std::vector<const Model*> entityModels(entities.size(), nullptr);
    if (models) {
        for (size_t i = 0; i < entities.size(); ++i) {
            entityModels[i] = models->GetModel(entities[i].modelArchiveIndex);
        }
    }

    // The frame's four passes -- sky, tile faces, models, billboards -- in
    // the engine's order, into `sink` (see TriangleSink).
    //
    // M126: over a range of the faces and of the entities, and the sky and
    // billboards only when `tail` is set -- so the recording can be split
    // into chunks and run on the worker pool. Replaying the chunks in the
    // order they cover rebuilds exactly the command list one call over
    // everything would have recorded, so the frame is the same pixel for
    // pixel. See the dispatch at the bottom of Render().
    auto submitAll = [&](TriangleSink& sink, size_t faceBegin, size_t faceEnd,
                         size_t entityBegin, size_t entityEnd, bool tail) {
        // M70: the skybox (<zone>.zsk), first and underneath everything.
        //
        // In `Render3DScene` this draw and the flat background fill above are
        // the two arms of one `if` -- the sky *is* the frame clear -- so it runs
        // before any wall or actor, writes no depth, and takes no light term.
        // zone_renderer.h's M70 block has the four independent lines of
        // evidence that `.zsk` is a skybox, and its kSkyboxScale block the three
        // placement constants.
        //
        // Anchoring it to the camera is the entire behavioural difference from
        // M11, and it is a translation the port has to write explicitly because
        // it transforms world-space vertices, where the original transforms
        // camera-space ones and simply never adds a camera position: passing the
        // camera's own X/Y/Z as the instance offset makes every sky vertex
        // camera-relative, which is the same thing.
        // M123: submitted further down -- see "the sky, last".

        for (size_t faceIndex = faceBegin; faceIndex < faceEnd; ++faceIndex) {
            const Face& face = faces[faceIndex];
            const SurfaceRecord& sur = zone.surface(face.surIndex);

            ViewVertex vv[4];
            for (int i = 0; i < 4; ++i) {
                // Raw world units -- the frame the real engine does all of
                // this in (positions, the .sur UV offsets, and the half-tile
                // corner nudge are all expressed in it).
                float worldX = face.corners[i].x * kTileScale;
                float worldY = face.corners[i].y * kTileScale;
                float worldZ = face.corners[i].z;
                // Real half-tile vertex nudge (ZcpEntry::cornerNudge) -- moves
                // the vertex itself, so it feeds both the projection below and
                // the UV computation, exactly as in BuildAndProject.
                zone.ApplyCornerNudge(&worldX, &worldY);

                float rx = worldX / kTileScale - camX;
                float ry = worldY / kTileScale - camY;
                float rz = worldZ / kTileScale - camZ;

                vv[i].right = rx * sinYaw - ry * cosYaw;
                float flatForward = rx * cosYaw + ry * sinYaw;
                vv[i].forward = flatForward * cosPitch - rz * sinPitch;
                vv[i].up = rz * cosPitch + flatForward * sinPitch;

                zone.SurfaceUv(sur, face.orient, worldX, worldY, worldZ, &vv[i].u, &vv[i].v);

                // Real per-vertex light (SurfaceFace_BuildAndProject):
                //     vertex.light = engine+0x484 + cellLight - (viewDepth >> 1)
                // clamped to [0x400, 0x3f00]. `engine+0x484` is read exactly
                // once in the entire binary (this one site) and written
                // nowhere -- a whole-program decompiled grep this session
                // found zero writers -- so on EKA1's zeroed heap it is
                // effectively 0, the same "never explicitly initialised"
                // pattern already established for the eye-height field
                // (render3d/camera.h). The `- depth/2` term is the engine's
                // real distance fog; the port had no distance falloff at all
                // before this.
                int cellTx = static_cast<int>(std::floor(worldX / kTileScale));
                int cellTy = static_cast<int>(std::floor(worldY / kTileScale));
                float cellLight = static_cast<float>(zone.CellAt(cellTx, cellTy).lightLevel);
                float depthWorld = vv[i].forward * kTileScale;
                // M130: plus the player's glow and the tile's emitters.
                bool pinned = false;
                const int bonus = SceneLightBonus(
                    cellTx, cellTy,
                    static_cast<uint32_t>(i) + static_cast<uint32_t>(face.orient) * 4u, &pinned);
                // M131: the Blind status quarters the base term.
                const float baseLight = lights.blind ? (cellLight - depthWorld * 0.5f) * 0.25f
                                                     : cellLight - depthWorld * 0.5f;
                vv[i].light = pinned ? static_cast<float>(kMaxLightLevel)
                                     : std::clamp(baseLight + static_cast<float>(bonus),
                                                  static_cast<float>(kMinLightLevel),
                                                  static_cast<float>(kMaxLightLevel));
            }

            // M66: clip and draw the quad as the two triangles it has always
            // *been* drawn as, instead of clipping it once as a four-sided
            // polygon and fanning the result.
            //
            // This is the fix for the long-standing `ZoneRenderer::Render`
            // crash (docs/PORT_ROADMAP.md). The quad handed to the clipper is
            // not planar -- a tile's floor and ceiling each carry four
            // independent corner heights -- so once the camera could pitch, the
            // near-plane test could alternate in/out around its four corners
            // and ClipNear could return **six** vertices into a five-entry
            // buffer, writing past the end of both stack arrays below. The
            // overrun lands on this function's own frame, so the fault surfaces
            // as a wild read a line or two later rather than at the write that
            // caused it, which is why the recorded symptom was an access
            // violation inside this projection loop.
            //
            // Splitting first also makes the clip *correct* rather than merely
            // safe. Triangles are planar by construction, so ClipNear's
            // `count + 1` bound genuinely holds for them, and the two triangles
            // below are the exact pair the old fan produced for an unclipped
            // quad -- so a flat tile renders identically (u, v and light are
            // affine within a plane, so barycentric interpolation does not care
            // where the split runs). For a *sloped* tile the old code clipped
            // a surface that neither triangle actually covered; this clips the
            // geometry that gets rasterized.
            //
            // What visibly changes, measured rather than assumed: twelve of the
            // fourteen tracked `.ppm` render dumps come out byte-identical
            // across this change, and the two that move (the entity-render
            // views at yaw 0 and 270) move *entirely* because of
            // SurfaceDetailMask. That mask is chosen per triangle from its
            // three vertex depths, so changing which triangles exist near the
            // near plane can move a face into a different detail band. Pinning
            // the mask to a constant makes even those two byte-identical --
            // which is the check that says the difference is texel precision on
            // near-camera surfaces and nothing else. It is also the better
            // answer: the band is now picked from the triangles actually being
            // rasterized.
            constexpr int kQuadTriangles[2][3] = {{0, 1, 2}, {0, 2, 3}};
            for (const auto& tri : kQuadTriangles) {
                ViewVertex vv3[3] = {vv[tri[0]], vv[tri[1]], vv[tri[2]]};

                ViewVertex clipped[4];
                int clippedCount = ClipNear(vv3, 3, clipped, 4);
                if (clippedCount < 3) continue;

                ProjectedVertex pv[4];
                for (int i = 0; i < clippedCount; ++i) {
                    pv[i].invW = 1.0f / clipped[i].forward;
                    pv[i].sx = targetWidth * 0.5f + clipped[i].right * focalX * pv[i].invW;
                    pv[i].sy = targetHeight * 0.5f - clipped[i].up * focalY * pv[i].invW;
                    pv[i].depthWorld = clipped[i].forward * kTileScale;
                    pv[i].u = clipped[i].u * pv[i].invW;
                    pv[i].v = clipped[i].v * pv[i].invW;
                    pv[i].light = clipped[i].light;
                    pv[i].fog = FogScalar(pv[i].depthWorld, fogPercent);  // M89, M123
                }
                for (int i = 1; i + 1 < clippedCount; ++i) {
                    sink.Surface(pv[0], pv[i], pv[i + 1], zone, sur.textureIndex, face.hueGroup);
                }
            }
        }

        // M8: placed entities (props, monsters, doors, ...) resolved via
        // world/entity_types.h + world/model_archive.h -- see this class's
        // header comment for what's simplified here (frame 0/skin 0 only,
        // no orientation, no distance culling).
        if (models) {
            for (size_t entityIndex = entityBegin; entityIndex < entityEnd; ++entityIndex) {
                const PlacedEntity& pe = entities[entityIndex];
                const Model* model = entityModels[entityIndex];
                if (!model) continue;

                // Position/axis assumptions (undocumented in MODEL_FORMAT.md,
                // see zone_renderer.h): the model's local-space vertex units
                // match the .ent record's world-unit position scale
                // directly (no extra per-instance scale factor -- unverified,
                // simplest hypothesis absent contrary evidence), so the same
                // /kTileScale conversion used for x/y tile-float coordinates
                // above applies to vertex x/y too; z stays in raw world
                // units like the tile faces' heights do.
                //
                // The model's local axes are Y-up (local Y -> world Z),
                // *not* a direct X/Y/Z passthrough -- confirmed empirically
                // this session: a real barrel model's bounding box is
                // roughly circular in local X/Z (a barrel's round footprint)
                // and elongated in local Y (its height), which only makes
                // sense if local Y is "up." A real door model's bounding box
                // is thin in local Z, wide in local X, and tall in local Y --
                // consistent with the same convention (a door is a thin
                // panel, tall along its up axis).
                float baseTileX = pe.x / kTileScale;
                float baseTileY = pe.y / kTileScale;
                // M71: `Actor3D_TransformAndSubmitModel`'s own `-0x40` -- see
                // kEntityDrawZOffset. The offset is a *render* constant, not a
                // position one, so it deliberately lives here rather than in
                // the placement: collision, tile stamps, use-range and the
                // automap all keep reading the .ent Z the engine stores.
                float baseZ = pe.z + kEntityDrawZOffset;

                // Real per-instance skin/scale (PlacedEntity), clamped to what
                // this resource actually has -- a script naming a skin the
                // model doesn't carry falls back to skin 0 rather than reading
                // past the pixel buffer.
                int skin =
                    (pe.skinIndex >= 0 && pe.skinIndex < model->skinCount) ? pe.skinIndex : 0;
                SubmitModel(sink, zone, *model, skin, baseTileX,
                            baseTileY, baseZ, camX, camY, camZ, cosYaw, sinYaw, cosPitch, sinPitch, focalX, focalY,
                            pe.yaw, pe.scale, pe.frameIndex, /*asSkybox=*/false, pe.rotA, pe.rotB,
                            backbufferPtr ? m_ObjectIds.data() : nullptr,
                            static_cast<uint8_t>(pe.objectId & 0xFF),
                            pe.flashLevel, fogPercent);
            }
        }

        if (!tail) return;

        // M123: **the sky, last of the opaque passes, and only where nothing
        // opaque landed.** The engine draws it first as the frame clear: it
        // writes no depth and everything else covers it. Every surface and
        // model pixel that is drawn writes depth, so after them a pixel
        // still at depth 0 is exactly a pixel the sky would have shown --
        // and drawing it there now gives the identical frame while skipping
        // every pixel that was going to be overdrawn (all of them, indoors).
        // Still before the billboards, which blend over whatever is beneath.
        {
            const Model* sky = drawSkybox ? zone.SkyMesh() : nullptr;
            if (sky) {
                SubmitModel(sink, zone, *sky, /*skinIndex=*/0, camX, camY,
                            camera.z + kSkyboxUpOffset, camX, camY, camZ, cosYaw, sinYaw,
                            cosPitch, sinPitch, focalX, focalY, /*entityYaw=*/0.0f, kSkyboxScale,
                            /*frameIndex=*/0, /*asSkybox=*/true, 0.0f, 0.0f, nullptr, 0, -1,
                            fogPercent);
            }
        }

        // M79: the animated-sprite entities -- spell projectiles and scripted
        // effects. Last, like the engine's own draw order, and depth-tested
        // against everything above.
        for (const SpriteBillboard& bb : billboards) {
            SubmitBillboard(sink, bb, camX, camY, camZ, cosYaw, sinYaw,
                            cosPitch, sinPitch, focalX, focalY);
        }    };

    using Clock = std::chrono::steady_clock;
    const auto msSince = [](Clock::time_point t) {
        return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
    };
    if (gpuFrame) {
        // M129: recorded in the same chunks as a banded frame, on the pool,
        // then stitched in chunk order -- sky first, because on a card the
        // sky really is drawn first (no depth, under everything), where the
        // software path draws it last into whatever is still empty (M123).
        const int threads = std::max(1, BandPool::Shared().threadCount());
        const size_t faceChunks =
            std::min<size_t>(static_cast<size_t>(threads), std::max<size_t>(1, faces.size() / 64));
        const size_t entityChunks =
            models ? std::min<size_t>(static_cast<size_t>(threads) * 2,
                                      std::max<size_t>(1, entities.size() / 8))
                   : 0;
        const size_t chunkCount = faceChunks + entityChunks + 1;
        std::vector<GpuFrame> parts(chunkCount);
        BandPool::Shared().Run(static_cast<int>(chunkCount), [&](int c) {
            const size_t i = static_cast<size_t>(c);
            TriangleSink sink(parts[i], targetWidth, targetHeight);
            if (i < faceChunks) {
                submitAll(sink, faces.size() * i / faceChunks, faces.size() * (i + 1) / faceChunks,
                          0, 0, /*tail=*/false);
            } else if (i < faceChunks + entityChunks) {
                const size_t e = i - faceChunks;
                submitAll(sink, 0, 0, entities.size() * e / entityChunks,
                          entities.size() * (e + 1) / entityChunks, /*tail=*/false);
            } else {
                submitAll(sink, 0, 0, 0, 0, /*tail=*/true);
            }
        });
        size_t total = 0;
        for (const GpuFrame& part : parts) total += part.vertices.size();
        gpuFrame->vertices.reserve(total);
        for (int pass = 0; pass < 2; ++pass) {
            for (const GpuFrame& part : parts) {
                for (const GpuBatch& batch : part.batches) {
                    if ((batch.kind == GpuBatch::Kind::Sky) != (pass == 0)) continue;
                    GpuBatch merged = batch;
                    merged.firstVertex = static_cast<uint32_t>(gpuFrame->vertices.size());
                    gpuFrame->vertices.insert(
                        gpuFrame->vertices.end(),
                        part.vertices.begin() + static_cast<std::ptrdiff_t>(batch.firstVertex),
                        part.vertices.begin() +
                            static_cast<std::ptrdiff_t>(batch.firstVertex + batch.vertexCount));
                    GpuBatch* last = gpuFrame->batches.empty() ? nullptr : &gpuFrame->batches.back();
                    if (last && last->kind == merged.kind && last->model == merged.model &&
                        last->skinIndex == merged.skinIndex &&
                        last->flashLevel == merged.flashLevel &&
                        merged.kind != GpuBatch::Kind::Billboard) {
                        last->vertexCount += merged.vertexCount;
                    } else {
                        gpuFrame->batches.push_back(merged);
                    }
                }
            }
        }
        m_Timings = Timings{msSince(frameStart), 0.0};
        return;
    }
    Backbuffer& backbuffer = *backbufferPtr;
    if (bands == 1) {
        TriangleSink direct(backbuffer, depthBuffer, /*recording=*/false);
        submitAll(direct, 0, faces.size(), 0, entities.size(), /*tail=*/true);
        m_Timings = Timings{msSince(frameStart), 0.0};
        return;
    }

    // M126: the recording on the pool too. It was a single thread's work
    // ahead of the bands -- the visibility raycast and face build are still
    // (they are cheap, and the raycast is a serial march), but transforming,
    // clipping and projecting every triangle is split into chunks: the
    // faces in contiguous runs, the entities in contiguous runs, and the
    // sky and billboards as one last chunk. Each chunk records into its own
    // sink; every band then replays the sinks in chunk order, which is
    // exactly the command sequence a single sink would have held. Nothing a
    // chunk reads is written during recording: the zone and models are
    // const (and resolved above), and the object-ID buffer is only written
    // by the replay.
    const int threads = std::max(1, BandPool::Shared().threadCount());
    const size_t faceChunks =
        std::min<size_t>(static_cast<size_t>(threads), std::max<size_t>(1, faces.size() / 64));
    // Entities vary far more in cost (most are behind the camera and leave
    // at the first test, a few are big models), so they get more, smaller
    // chunks for the pool to balance.
    const size_t entityChunks = models ? std::min<size_t>(static_cast<size_t>(threads) * 2,
                                                          std::max<size_t>(1, entities.size() / 8))
                                       : 0;
    const size_t chunkCount = faceChunks + entityChunks + 1;
    std::vector<std::unique_ptr<TriangleSink>> chunks(chunkCount);
    for (auto& chunk : chunks) {
        chunk = std::make_unique<TriangleSink>(backbuffer, depthBuffer, /*recording=*/true);
    }
    BandPool::Shared().Run(static_cast<int>(chunkCount), [&](int c) {
        const size_t i = static_cast<size_t>(c);
        TriangleSink& sink = *chunks[i];
        if (i < faceChunks) {
            submitAll(sink, faces.size() * i / faceChunks, faces.size() * (i + 1) / faceChunks, 0, 0,
                      /*tail=*/false);
        } else if (i < faceChunks + entityChunks) {
            const size_t e = i - faceChunks;
            submitAll(sink, 0, 0, entities.size() * e / entityChunks,
                      entities.size() * (e + 1) / entityChunks, /*tail=*/false);
        } else {
            submitAll(sink, 0, 0, 0, 0, /*tail=*/true);
        }
    });
    m_Timings.recordMs = msSince(frameStart);
    const Clock::time_point rasterStart = Clock::now();
    BandPool::Shared().Run(bands, [&](int band) {
        const RowSpan rows{height * band / bands, height * (band + 1) / bands};
        const size_t width = static_cast<size_t>(backbuffer.width());
        const size_t first = static_cast<size_t>(rows.begin) * width;
        const size_t count = static_cast<size_t>(rows.end - rows.begin) * width;
        std::fill_n(backbuffer.Row(rows.begin), count, kBackgroundFill);
        std::fill_n(depthBuffer.begin() + static_cast<std::ptrdiff_t>(first), count, 0.0f);
        std::fill_n(m_ObjectIds.begin() + static_cast<std::ptrdiff_t>(first), count,
                    static_cast<uint8_t>(0));
        for (const auto& chunk : chunks) chunk->Replay(rows);
    });
    m_Timings.rasterMs = msSince(rasterStart);

}

uint16_t FlashRamp565(int row, int step) {
    const int r = std::clamp(row, 0, 13);
    const int i = std::clamp(step, 0, 15);
    return ExpandRGB444(FlashRamps().entry[r][i], 1.0f);
}

}  // namespace sk
