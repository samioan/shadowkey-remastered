#include "render3d/zone_renderer.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace sk {

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
constexpr float kNearPlane = 0.05f;  // tile units

int ClipNear(const ViewVertex* in, int count, ViewVertex* out, int capacity) {
    int n = 0;
    for (int i = 0; i < count; ++i) {
        const ViewVertex& a = in[i];
        const ViewVertex& b = in[(i + 1) % count];
        bool aIn = a.forward >= kNearPlane;
        bool bIn = b.forward >= kNearPlane;
        if (n + 2 > capacity) break;  // unreachable for a planar input; see above
        if (aIn) out[n++] = a;
        if (aIn != bIn) {
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
    float depthWorld = 0;  // view-forward distance, raw world units
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

void RasterizeTriangle(Backbuffer& backbuffer, std::vector<float>& depthBuffer,
                        const ProjectedVertex& a, const ProjectedVertex& b,
                        const ProjectedVertex& c, const Zone& zone, int textureIndex,
                        uint8_t hueGroup) {
    float area = (b.sx - a.sx) * (c.sy - a.sy) - (b.sy - a.sy) * (c.sx - a.sx);
    if (std::fabs(area) < 1e-6f) return;
    const int detailMask = SurfaceDetailMask(a.depthWorld, b.depthWorld, c.depthWorld);

    int minX = std::max(0, static_cast<int>(std::floor(std::min({a.sx, b.sx, c.sx}))));
    int maxX =
        std::min(Backbuffer::kWidth - 1, static_cast<int>(std::ceil(std::max({a.sx, b.sx, c.sx}))));
    int minY = std::max(0, static_cast<int>(std::floor(std::min({a.sy, b.sy, c.sy}))));
    int maxY = std::min(Backbuffer::kHeight - 1,
                         static_cast<int>(std::ceil(std::max({a.sy, b.sy, c.sy}))));
    if (minX > maxX || minY > maxY) return;

    for (int py = minY; py <= maxY; ++py) {
        for (int px = minX; px <= maxX; ++px) {
            float sx = static_cast<float>(px) + 0.5f;
            float sy = static_cast<float>(py) + 0.5f;
            float w0 = (b.sx - sx) * (c.sy - sy) - (b.sy - sy) * (c.sx - sx);
            float w1 = (c.sx - sx) * (a.sy - sy) - (c.sy - sy) * (a.sx - sx);
            float w2 = (a.sx - sx) * (b.sy - sy) - (a.sy - sy) * (b.sx - sx);
            bool inside = (w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0);
            if (!inside) continue;
            float l0 = w0 / area, l1 = w1 / area, l2 = w2 / area;

            float invW = l0 * a.invW + l1 * b.invW + l2 * c.invW;
            if (invW <= 0.0f) continue;
            int depthIndex = py * Backbuffer::kWidth + px;
            if (invW <= depthBuffer[static_cast<size_t>(depthIndex)]) continue;  // farther, skip

            float u = (l0 * a.u + l1 * b.u + l2 * c.u) / invW;
            float v = (l0 * a.v + l1 * b.v + l2 * c.v) / invW;
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

            depthBuffer[static_cast<size_t>(depthIndex)] = invW;
            backbuffer.SetPixel(px, py, ExpandRGB444(raw444, 1.0f));
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
void RasterizeModelTriangle(Backbuffer& backbuffer, std::vector<float>& depthBuffer,
                             const ProjectedVertex& a, const ProjectedVertex& b,
                             const ProjectedVertex& c, const Model& model, int skinIndex) {
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
    // lothcav and twilite -- load a `.zsk` room mesh whose texture header
    // reads skinCount=256, width=256, **height=0**, so entering any of
    // them killed a Debug build on the first frame. See the M66 entry in
    // docs/PORT_ROADMAP.md for what that `.zsk` actually turns out to be.
    if (model.width <= 0 || model.height <= 0 || model.skinCount <= 0) return;

    float area = (b.sx - a.sx) * (c.sy - a.sy) - (b.sy - a.sy) * (c.sx - a.sx);
    if (std::fabs(area) < 1e-6f) return;

    int minX = std::max(0, static_cast<int>(std::floor(std::min({a.sx, b.sx, c.sx}))));
    int maxX =
        std::min(Backbuffer::kWidth - 1, static_cast<int>(std::ceil(std::max({a.sx, b.sx, c.sx}))));
    int minY = std::max(0, static_cast<int>(std::floor(std::min({a.sy, b.sy, c.sy}))));
    int maxY = std::min(Backbuffer::kHeight - 1,
                         static_cast<int>(std::ceil(std::max({a.sy, b.sy, c.sy}))));
    if (minX > maxX || minY > maxY) return;

    for (int py = minY; py <= maxY; ++py) {
        for (int px = minX; px <= maxX; ++px) {
            float sx = static_cast<float>(px) + 0.5f;
            float sy = static_cast<float>(py) + 0.5f;
            float w0 = (b.sx - sx) * (c.sy - sy) - (b.sy - sy) * (c.sx - sx);
            float w1 = (c.sx - sx) * (a.sy - sy) - (c.sy - sy) * (a.sx - sx);
            float w2 = (a.sx - sx) * (b.sy - sy) - (a.sy - sy) * (b.sx - sx);
            bool inside = (w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0);
            if (!inside) continue;
            float l0 = w0 / area, l1 = w1 / area, l2 = w2 / area;

            float invW = l0 * a.invW + l1 * b.invW + l2 * c.invW;
            if (invW <= 0.0f) continue;
            int depthIndex = py * Backbuffer::kWidth + px;
            if (invW <= depthBuffer[static_cast<size_t>(depthIndex)]) continue;  // farther, skip

            float u = (l0 * a.u + l1 * b.u + l2 * c.u) / invW;
            float v = (l0 * a.v + l1 * b.v + l2 * c.v) / invW;
            int tx = std::clamp(static_cast<int>(u), 0, model.width - 1);
            int ty = std::clamp(static_cast<int>(v), 0, model.height - 1);

            uint16_t raw444 = model.TexelAt(skinIndex, tx, ty);
            if (raw444 == 0x0f0f) continue;  // chroma-key cutout

            depthBuffer[static_cast<size_t>(depthIndex)] = invW;
            // M35: models are lit and fogged now, instead of being drawn
            // at a flat 1.0 forever. That flat factor is why creatures,
            // trees and props stayed fully bright at any distance while
            // the walls around them faded -- they read as floating in
            // front of the fog rather than being in it.
            //
            // The real actor pipeline does fog them, by its own route:
            // `Poly3D_ClipAndDispatch` picks between 10 rasterizer
            // variants, and the five "fade" ones compute a per-vertex
            // `intensity = clamp(vertexZ * engine[+0x5c8] >> 8, 0,
            // 0xffff)` -- depth scaled by a global factor -- interpolate
            // it across the polygon, and OR its top nibble into the
            // output colour word, which CompositeSceneBufferToScreen then
            // runs through the engine+0x5c4 lookup table
            // (docs/RENDERER_3D.md). Neither that factor nor that table
            // was extracted, so the exact curve isn't reproducible; what
            // is reproducible is the term the surface pipeline already
            // uses (cell light minus half the view depth), applied here
            // as a straight RGB scale because a model carries its own
            // RGB444 skin rather than indexing a .zlu palette rung.
            // Approximation in the mapping, right in the behaviour: the
            // two pipelines now agree about distance.
            float lightF = l0 * a.light + l1 * b.light + l2 * c.light;
            float brightness = std::clamp(lightF / static_cast<float>(kMaxLightLevel), 0.0f, 1.0f);
            backbuffer.SetPixel(px, py, ExpandRGB444(raw444, brightness));
        }
    }
}

// Transforms and rasterizes every face of one model instance, positioned
// by a tile-space X/Y offset plus a raw-world-unit Z offset -- shared by
// M8's per-entity placement loop and M11's whole-room `.zsk` mesh (which
// simply passes a zero offset, see this file's header comment). See
// RasterizeModelTriangle's own header comment for the local-axis/scale
// assumptions baked into the per-vertex transform below.
// entityYaw (M15): local rotation around the vertical axis, radians --
// applied to each vertex's local X/Z before the camera transform, so a
// door instance (see zone_renderer.h's PlacedEntity::yaw) visibly swings
// open. Zero for every caller before M15 (room mesh, and every entity
// but a live door), so this is purely additive -- cosEntityYaw=1/
// sinEntityYaw=0 reduces the new rotation to a no-op.
void SubmitModel(Backbuffer& backbuffer, std::vector<float>& depthBuffer, const Zone& zone,
                  const Model& model, int skinIndex, float offsetTileX, float offsetTileY,
                  float offsetZ, float camX, float camY, float camZ, float cosYaw, float sinYaw,
                  float cosPitch, float sinPitch, float focalX, float focalY,
                  float entityYaw = 0.0f, float scale = 1.0f, int frameIndex = 0) {
    // M35: see kModelForwardYawOffset -- a model's forward is its local
    // +Z, so a heading measured from world +X needs a quarter turn taken
    // out of it before it can be used as a rotation of the local axes.
    float modelYaw = entityYaw + kModelForwardYawOffset;
    float cosEntityYaw = std::cos(modelYaw), sinEntityYaw = std::sin(modelYaw);
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
            float lx = sxv * cosEntityYaw - szv * sinEntityYaw;
            float lz = sxv * sinEntityYaw + szv * cosEntityYaw;

            float rx = offsetTileX + lx / kTileScale - camX;
            float ry = offsetTileY + lz / kTileScale - camY;
            float rz = (offsetZ + syv) / kTileScale - camZ;

            vv3[i].right = rx * sinYaw - ry * cosYaw;
            float flatForward = rx * cosYaw + ry * sinYaw;
            // Pitch rotates the (forward, up) pair -- see Camera::pitch.
            vv3[i].forward = flatForward * cosPitch - rz * sinPitch;
            vv3[i].up = rz * cosPitch + flatForward * sinPitch;
            vv3[i].u = uv[i]->u / 256.0f;
            vv3[i].v = uv[i]->v / 256.0f;

            // M35: the same per-vertex light the tile-grid pipeline uses
            // (its own comment has the decompiled formula) -- the cell's
            // baked light where this vertex stands, less half the view
            // depth. See RasterizeModelTriangle for why a model applies it
            // as an RGB scale rather than a palette rung.
            float worldVertX = (offsetTileX + lx / kTileScale) * kTileScale;
            float worldVertY = (offsetTileY + lz / kTileScale) * kTileScale;
            int cellTx = static_cast<int>(std::floor(worldVertX / kTileScale));
            int cellTy = static_cast<int>(std::floor(worldVertY / kTileScale));
            float cellLight = static_cast<float>(zone.CellAt(cellTx, cellTy).lightLevel);
            vv3[i].light = std::clamp(cellLight - vv3[i].forward * kTileScale * 0.5f,
                                       static_cast<float>(kMinLightLevel),
                                       static_cast<float>(kMaxLightLevel));
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
            pvc[i].sx = Backbuffer::kWidth * 0.5f + clipped[i].right * focalX * pvc[i].invW;
            pvc[i].sy = Backbuffer::kHeight * 0.5f - clipped[i].up * focalY * pvc[i].invW;
            pvc[i].depthWorld = clipped[i].forward * kTileScale;
            pvc[i].u = clipped[i].u * pvc[i].invW;
            pvc[i].v = clipped[i].v * pvc[i].invW;
            pvc[i].light = clipped[i].light;  // M35
        }
        for (int i = 1; i + 1 < clippedCount; ++i) {
            RasterizeModelTriangle(backbuffer, depthBuffer, pvc[0], pvc[i], pvc[i + 1], model,
                                    skinIndex);
        }
    }
}

}  // namespace

void ZoneRenderer::Render(Backbuffer& backbuffer, const Zone& zone, const Camera& camera,
                           const std::vector<PlacedEntity>& entities, ModelArchive* models) const {
    backbuffer.Fill(PackRGB565(8, 8, 16));
    std::vector<float> depthBuffer(static_cast<size_t>(Backbuffer::kWidth) * Backbuffer::kHeight,
                                    0.0f);

    // M41: the real visible-tile set. `TileGrid_RaycastVisibility` runs
    // once per frame in the original, immediately before its face-drawing
    // loop, and this pipeline is that loop -- so the two are called in the
    // same order and for the same reason. Replaces M6's fixed-radius
    // square scan, the last placeholder left in this pipeline.
    std::vector<std::pair<int, int>> visibleTiles =
        zone.RaycastVisibleTiles(camera.x, camera.y, camera.yaw);
    std::vector<Face> faces = CollectFaces(zone, visibleTiles, camera.x, camera.y, camera.z);

    float camX = camera.x / kTileScale, camY = camera.y / kTileScale, camZ = camera.z / kTileScale;
    float cosYaw = std::cos(camera.yaw), sinYaw = std::sin(camera.yaw);
    float cosPitch = std::cos(camera.pitch), sinPitch = std::sin(camera.pitch);
    // forward = (cosYaw, sinYaw, 0); right = (sinYaw, -cosYaw, 0); up = (0,0,1)
    float aspect = static_cast<float>(Backbuffer::kWidth) / static_cast<float>(Backbuffer::kHeight);
    float focalY = (Backbuffer::kHeight * 0.5f) / std::tan(camera.fovY * 0.5f);
    float focalX = focalY;  // square-ish texel assumption; aspect handled via screen center only
    (void)aspect;

    for (const Face& face : faces) {
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
            vv[i].light = std::clamp(cellLight - depthWorld * 0.5f,
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
                pv[i].sx = Backbuffer::kWidth * 0.5f + clipped[i].right * focalX * pv[i].invW;
                pv[i].sy = Backbuffer::kHeight * 0.5f - clipped[i].up * focalY * pv[i].invW;
                pv[i].depthWorld = clipped[i].forward * kTileScale;
                pv[i].u = clipped[i].u * pv[i].invW;
                pv[i].v = clipped[i].v * pv[i].invW;
                pv[i].light = clipped[i].light;
            }
            for (int i = 1; i + 1 < clippedCount; ++i) {
                RasterizeTriangle(backbuffer, depthBuffer, pv[0], pv[i], pv[i + 1], zone,
                                   sur.textureIndex, face.hueGroup);
            }
        }
    }

    // M8: placed entities (props, monsters, doors, ...) resolved via
    // world/entity_types.h + world/model_archive.h -- see this class's
    // header comment for what's simplified here (frame 0/skin 0 only,
    // no orientation, no distance culling).
    if (models) {
        for (const PlacedEntity& pe : entities) {
            const Model* model = models->GetModel(pe.modelArchiveIndex);
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
            float baseZ = pe.z;

            // Real per-instance skin/scale (PlacedEntity), clamped to what
            // this resource actually has -- a script naming a skin the
            // model doesn't carry falls back to skin 0 rather than reading
            // past the pixel buffer.
            int skin = (pe.skinIndex >= 0 && pe.skinIndex < model->skinCount) ? pe.skinIndex : 0;
            SubmitModel(backbuffer, depthBuffer, zone, *model, skin, baseTileX, baseTileY, baseZ,
                        camX, camY, camZ, cosYaw, sinYaw, cosPitch, sinPitch, focalX, focalY,
                        pe.yaw, pe.scale, pe.frameIndex);
        }
    }

    // M11: the room's own static mesh baked into <zone>.zsk -- a real,
    // separate render step in the original engine alongside the
    // tile-grid pipeline above, not a replacement for it (see
    // world/zone.h's RoomMesh() comment). No per-instance offset (unlike
    // .ent-placed entities): the mesh's own vertex coordinates already
    // sit in the zone's world-unit frame. Same simplifications as M8's
    // entities: skin 0 only. (M35: no longer unlit -- SubmitModel now
    // applies the same per-vertex light/fog term the tile grid does.)
    if (const Model* room = zone.RoomMesh()) {
        SubmitModel(backbuffer, depthBuffer, zone, *room, 0, 0.0f, 0.0f, 0.0f, camX, camY, camZ,
                    cosYaw, sinYaw, cosPitch, sinPitch, focalX, focalY);
    }
}

}  // namespace sk
