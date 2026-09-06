// M66 smoke test: the near-plane clipper's real output bound, and the
// long-standing `ZoneRenderer::Render` crash that came from getting it
// wrong (docs/PORT_ROADMAP.md carried it as "pre-existing ... still
// unchased" from M56 onward).
//
// The claims this milestone makes, and what checks each:
//
//   * **The renderer's floor/ceiling quads are not planar.** A tile's
//     floor is four independent corner heights over a unit square -- a
//     bilinear patch -- and it is planar only when the two diagonals'
//     height sums agree. Part 1 measures how many tiles in each shipped
//     zone actually break that, from the real `.zcp` data.
//
//   * **Two separate things in the shipped data break the textbook
//     `count + 1` clip bound, and each is enough on its own.** A plane
//     cuts a *planar convex* polygon in exactly two edges -- that is the
//     whole justification for `count + 1`, and Part 2 confirms it holds
//     wherever its precondition does. What breaks it: (a) four
//     independent corner heights, once the camera pitches, and (b) M28's
//     half-tile corner nudge, which moves the four XY positions off the
//     corners of their square and can turn the ring non-convex, with no
//     pitch involved at all. Part 2 replicates the renderer's own view
//     transform, classifies every (tile, camera) it looks at by those two
//     properties, and reports the worst output count in each class.
//
//   * **The crash is gone.** Part 3 sweeps whole zones -- every sampled
//     open tile, eight yaws, seven pitches -- through the real
//     `ZoneRenderer::Render`, including the room mesh and every resolved
//     placed entity. Before the fix this faulted with an access violation
//     inside the projection loop within a few hundred frames.
//
//   * **And a second crash on the same function, found by that sweep.**
//     Nine of the twenty-one zones load a `.zsk` room mesh whose texture
//     header says `height = 0`, which handed `std::clamp` an inverted
//     range in the model rasterizer -- undefined behaviour, and an
//     outright `abort()` under a Debug MSVC STL, on the first frame of
//     those zones. Part 3 names them from the loaded data before it
//     renders, so the census stays honest if the `.zsk` question is ever
//     reopened.
//
//   * **Flat geometry renders exactly as it did.** Part 4 asserts the
//     invariant that makes the fix safe to take: within a plane the
//     interpolated attributes are affine, so it cannot matter whether the
//     quad is clipped whole and fanned or split into its two triangles
//     first. (The repo's tracked `.ppm` render dumps are the other half
//     of this check -- see the M66 roadmap entry for the byte-identical
//     before/after comparison.)
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "graphics/backbuffer.h"
#include "render3d/camera.h"
#include "render3d/zone_renderer.h"
#include "world/entity_types.h"
#include "world/model_archive.h"
#include "world/zone.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%-88s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    if (!ok) ++g_failures;
}

const char* kScriptRoot =
    "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
    "EnFrDeEsIt-26102004/system/apps/6r51";

// Every shipped zone, so nothing here rests on azra happening to be the
// zone with the awkward geometry.
const char* kZones[] = {"azra",     "broken1",  "broken2",      "crypt1", "crypt2",
                        "crypt3",   "delfhide", "drgnfld",      "dstar_e", "dstar_w",
                        "erthcave", "fearfrst", "ffarena",      "ghstpass",
                        "glaciercrawl", "lakvan", "lothcav",    "raiders", "snowline",
                        "stouttp",  "twilite"};

// zone_renderer.cpp's own corner convention (its kCornerDx/kCornerDy,
// decompiled from Render3DScene in M28) -- duplicated here because it
// lives in that file's anonymous namespace, and because Part 2 has to
// reproduce the renderer's transform exactly for its answer to mean
// anything.
constexpr float kCornerDx[4] = {0.0f, 1.0f, 1.0f, 0.0f};
constexpr float kCornerDy[4] = {1.0f, 1.0f, 0.0f, 0.0f};

// A quad over the unit square with corner heights h[] is planar exactly
// when its two diagonals meet: corners 0 and 2 are diagonally opposite
// under the convention above, as are 1 and 3.
bool FloorIsPlanar(const int16_t h[4]) { return h[0] + h[2] == h[1] + h[3]; }

// The view-space `forward` of one tile corner, transcribed from
// ZoneRenderer::Render's per-vertex block. The whole point of Part 2 is
// that `forward` picks up a height term as soon as `pitch != 0`.
float CornerForward(const sk::Zone& zone, int tx, int ty, int corner, int16_t height,
                    const sk::Camera& camera) {
    float worldX = (static_cast<float>(tx) + kCornerDx[corner]) * sk::kTileScale;
    float worldY = (static_cast<float>(ty) + kCornerDy[corner]) * sk::kTileScale;
    zone.ApplyCornerNudge(&worldX, &worldY);

    float rx = worldX / sk::kTileScale - camera.x / sk::kTileScale;
    float ry = worldY / sk::kTileScale - camera.y / sk::kTileScale;
    float rz = static_cast<float>(height) / sk::kTileScale - camera.z / sk::kTileScale;

    float flatForward = rx * std::cos(camera.yaw) + ry * std::sin(camera.yaw);
    return flatForward * std::cos(camera.pitch) - rz * std::sin(camera.pitch);
}

// zone_renderer.cpp's kNearPlane. How many of the four corners sit in
// front of it, and how many of the four ring edges cross it -- the two
// numbers that between them decide ClipNear's output count
// (`kept + crossings`).
constexpr float kNearPlane = 0.05f;

// Whether M28's half-tile vertex nudge (ZcpEntry::cornerNudge, applied
// through Zone::ApplyCornerNudge from the cell each *vertex* falls in)
// moves any of this tile's four corners. When it does, the quad's four
// XY positions are no longer the corners of a convex square -- they can
// even cross over into a bowtie -- and a straight level set can then cut
// all four edges of the ring. That is the second, pitch-independent way
// the old five-vertex bound was violated.
bool TileIsCornerNudged(const sk::Zone& zone, int tx, int ty) {
    for (int c = 0; c < 4; ++c) {
        float x = (static_cast<float>(tx) + kCornerDx[c]) * sk::kTileScale;
        float y = (static_cast<float>(ty) + kCornerDy[c]) * sk::kTileScale;
        float nx = x, ny = y;
        zone.ApplyCornerNudge(&nx, &ny);
        if (nx != x || ny != y) return true;
    }
    return false;
}

int RingCrossings(const float forward[4], int* keptOut) {
    int kept = 0, crossings = 0;
    for (int i = 0; i < 4; ++i) {
        bool aIn = forward[i] >= kNearPlane;
        bool bIn = forward[(i + 1) % 4] >= kNearPlane;
        if (aIn) ++kept;
        if (aIn != bIn) ++crossings;
    }
    *keptOut = kept;
    return crossings;
}

std::vector<sk::PlacedEntity> ResolveEntities(const sk::Zone& zone,
                                              const sk::EntityTypeTable& types) {
    std::vector<sk::PlacedEntity> out;
    for (const auto& e : zone.entities()) {
        const sk::EntityTypeDescriptor* desc = types.Lookup(e.typeId);
        if (!desc) continue;
        sk::PlacedEntity pe;
        pe.x = static_cast<float>(e.x);
        pe.y = static_cast<float>(e.y);
        pe.z = static_cast<float>(e.z);
        pe.modelArchiveIndex = desc->modelArchiveIndex;
        out.push_back(pe);
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    // Part 2 walks every tile that can possibly break the clip bound, and
    // Part 3 renders every pose it found; the stride only controls Part 3's
    // extra *broad* sweep and Part 2's sampling of the ordinary flat
    // majority. Pass 1 for the whole grid, and an optional zone name to
    // narrow the run to one map, when chasing something. Parts 1 and 2 are
    // effectively free (a fraction of a second between them); the broad
    // sweep is the whole cost of this test, which is why its default is
    // coarse and the targeted pass carries the regression.
    int stride = argc > 1 ? std::atoi(argv[1]) : 48;
    const char* onlyZone = argc > 2 ? argv[2] : nullptr;

    // Unbuffered: this test exists because ZoneRenderer::Render used to
    // die mid-sweep, and a buffered stdout throws away the last few
    // hundred lines -- exactly the ones naming where it got to.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    auto phaseStart = std::chrono::steady_clock::now();
    auto phaseSeconds = [&phaseStart]() {
        auto now = std::chrono::steady_clock::now();
        double s = std::chrono::duration<double>(now - phaseStart).count();
        phaseStart = now;
        return s;
    };

    sk::EntityTypeTable types;
    sk::ModelArchive models;
    if (!types.Load(kScriptRoot) || !models.Load(kScriptRoot)) {
        std::printf("m66_render_clip_smoke: FAILED to load entity/model tables\n");
        return 1;
    }

    std::vector<std::string> zoneNames;
    for (const char* name : kZones) {
        if (onlyZone && std::string(name) != onlyZone) continue;
        zoneNames.push_back(name);
    }
    if (zoneNames.empty()) {
        std::printf("m66_render_clip_smoke: no such zone '%s'\n", onlyZone);
        return 1;
    }

    std::vector<sk::Zone> zones;
    zones.resize(zoneNames.size());
    for (size_t i = 0; i < zones.size(); ++i) {
        if (!zones[i].Load(kScriptRoot, zoneNames[i])) {
            std::printf("m66_render_clip_smoke: FAILED to load zone %s\n", zoneNames[i].c_str());
            return 1;
        }
    }

    // ---------------------------------------------------------------
    // Part 1 -- the precondition, measured from the shipped .zcp data:
    // how much of the world is genuinely non-planar floor/ceiling.
    // ---------------------------------------------------------------
    std::printf("[%.1fs] loaded all zones\n", phaseSeconds());
    std::printf("\n=== Part 1: how many tiles are non-planar quads (real .zcp data) ===\n");
    long long totalOpen = 0, totalNonPlanarFloor = 0, totalNonPlanarCeiling = 0;
    int zonesWithNonPlanar = 0;
    for (size_t z = 0; z < zones.size(); ++z) {
        const sk::Zone& zone = zones[z];
        long long open = 0, nonPlanarFloor = 0, nonPlanarCeiling = 0;
        for (int ty = 0; ty < zone.height(); ++ty) {
            for (int tx = 0; tx < zone.width(); ++tx) {
                const sk::ZmpCell& cell = zone.CellAt(tx, ty);
                if (cell.IsWall()) continue;
                const sk::ZcpEntry& type = zone.TypeOf(cell);
                ++open;
                if (!FloorIsPlanar(type.floorHeight)) ++nonPlanarFloor;
                if (!FloorIsPlanar(type.ceilingHeight)) ++nonPlanarCeiling;
            }
        }
        if (nonPlanarFloor + nonPlanarCeiling > 0) ++zonesWithNonPlanar;
        std::printf("  %-13s %6lld open tiles, %5lld non-planar floors (%4.1f%%), "
                    "%5lld non-planar ceilings\n",
                    zoneNames[z].c_str(), open, nonPlanarFloor,
                    open ? 100.0 * static_cast<double>(nonPlanarFloor) / static_cast<double>(open)
                          : 0.0,
                    nonPlanarCeiling);
        totalOpen += open;
        totalNonPlanarFloor += nonPlanarFloor;
        totalNonPlanarCeiling += nonPlanarCeiling;
    }
    std::printf("  total: %lld open tiles, %lld non-planar floors, %lld non-planar ceilings\n",
                totalOpen, totalNonPlanarFloor, totalNonPlanarCeiling);

    Check(totalNonPlanarFloor + totalNonPlanarCeiling > 0,
          "the shipped world really does contain non-planar floor/ceiling quads");
    Check(zonesWithNonPlanar >= 2,
          "and not just in one zone -- this is ordinary level geometry, not a one-off");

    // ---------------------------------------------------------------
    // Part 2 -- the bound, and the two independent things in the shipped
    // data that break it. Replicates the renderer's own view transform
    // and classifies every (tile, camera) it looks at by the two
    // properties that decide whether the textbook `count + 1` bound
    // holds: whether the tile's four heights are coplanar, and whether
    // the M28 corner nudge has moved any of its four corners.
    // ---------------------------------------------------------------
    std::printf("[%.1fs]\n", phaseSeconds());
    std::printf("\n=== Part 2: what actually breaks the five-vertex clip bound ===\n");
    const float kProbePitches[] = {0.0f, 0.85f, 0.5f, 0.25f, -0.25f, -0.5f, -0.85f};
    const int kProbeYaws = 16;

    // Worst `kept + crossings` seen in each category, and the case that
    // produced it. Kept as plain numbers rather than a formatted string so
    // the inner loop stays cheap -- it runs tens of millions of times.
    struct Category {
        const char* name;
        int worst = 0;
        int kept = 0, crossings = 0;
        size_t zone = 0;
        int tx = 0, ty = 0, yawStep = 0;
        float pitch = 0.0f;
        int16_t heights[4] = {0, 0, 0, 0};
    };
    Category flatAndUnnudged{"planar heights, no corner nudge  "};
    Category slopedUnnudged{"non-planar heights, no nudge     "};
    Category nudgedFlatCamera{"corner-nudged tile, pitch == 0   "};
    Category everything{"anything at all                  "};

    // Every (tile, camera) that actually overflows the old five-entry
    // buffer, kept so Part 3 can render exactly those instead of hoping a
    // blind sweep lands on one. Capped: there are far more than a test
    // wants to draw, and the count is reported either way.
    struct OverflowCase {
        size_t zone;
        int tx, ty, yawStep;
        float pitch;
    };
    std::vector<OverflowCase> overflowCases;
    long long overflowTotal = 0;
    const size_t kMaxOverflowCases = 4000;

    long long probes = 0, exhaustiveTiles = 0, sampledTiles = 0;
    for (size_t z = 0; z < zones.size(); ++z) {
        const sk::Zone& zone = zones[z];
        for (int ty = 0; ty < zone.height(); ++ty) {
            for (int tx = 0; tx < zone.width(); ++tx) {
                const sk::ZmpCell& cell = zone.CellAt(tx, ty);
                if (cell.IsWall()) continue;
                const sk::ZcpEntry& type = zone.TypeOf(cell);
                bool planar = FloorIsPlanar(type.floorHeight);
                bool nudged = TileIsCornerNudged(zone, tx, ty);

                // Every tile that can possibly break the bound gets looked
                // at; the ordinary flat, un-nudged majority -- which is what
                // the `<= 5` claim below is about -- is sampled on the
                // stride, because there are a quarter of a million of them.
                if (planar && !nudged) {
                    if (tx % stride || ty % stride) continue;
                    ++sampledTiles;
                } else {
                    ++exhaustiveTiles;
                }

                // The four corner positions in tile units, with M28's nudge
                // already applied -- hoisted out of the camera loops
                // because they do not depend on the camera.
                float cornerX[4], cornerY[4], cornerZ[4];
                for (int c = 0; c < 4; ++c) {
                    float wx = (static_cast<float>(tx) + kCornerDx[c]) * sk::kTileScale;
                    float wy = (static_cast<float>(ty) + kCornerDy[c]) * sk::kTileScale;
                    zone.ApplyCornerNudge(&wx, &wy);
                    cornerX[c] = wx / sk::kTileScale;
                    cornerY[c] = wy / sk::kTileScale;
                    cornerZ[c] = static_cast<float>(type.floorHeight[c]) / sk::kTileScale;
                }
                // Stand on this very tile -- the near plane only ever cuts
                // a tile you are standing on or beside.
                float camX = static_cast<float>(tx) + 0.5f;
                float camY = static_cast<float>(ty) + 0.5f;
                float camZ = (static_cast<float>(type.floorHeight[0]) + sk::kEyeHeightOffset) /
                             sk::kTileScale;

                for (int yawStep = 0; yawStep < kProbeYaws; ++yawStep) {
                    float yaw = static_cast<float>(yawStep) * 2.0f * 3.14159265f /
                                static_cast<float>(kProbeYaws);
                    float cosYaw = std::cos(yaw), sinYaw = std::sin(yaw);
                    for (float pitch : kProbePitches) {
                        float cosPitch = std::cos(pitch), sinPitch = std::sin(pitch);
                        float forward[4];
                        for (int c = 0; c < 4; ++c) {
                            float rx = cornerX[c] - camX;
                            float ry = cornerY[c] - camY;
                            float rz = cornerZ[c] - camZ;
                            forward[c] = (rx * cosYaw + ry * sinYaw) * cosPitch - rz * sinPitch;
                        }
                        ++probes;
                        int kept = 0;
                        int crossings = RingCrossings(forward, &kept);
                        int total = kept + crossings;

                        auto record = [&](Category& cat) {
                            if (total <= cat.worst) return;
                            cat.worst = total;
                            cat.kept = kept;
                            cat.crossings = crossings;
                            cat.zone = z;
                            cat.tx = tx;
                            cat.ty = ty;
                            cat.yawStep = yawStep;
                            cat.pitch = pitch;
                            for (int c = 0; c < 4; ++c) cat.heights[c] = type.floorHeight[c];
                        };
                        record(everything);
                        if (planar && !nudged) record(flatAndUnnudged);
                        if (!planar && !nudged) record(slopedUnnudged);
                        if (nudged && pitch == 0.0f) record(nudgedFlatCamera);

                        if (total > 5) {
                            ++overflowTotal;
                            if (overflowCases.size() < kMaxOverflowCases) {
                                overflowCases.push_back({z, tx, ty, yawStep, pitch});
                            }
                        }
                    }
                }
            }
        }
    }

    std::printf("  %lld probes over %lld exhaustively-walked tiles + %lld sampled flat ones\n",
                probes, exhaustiveTiles, sampledTiles);
    for (const Category* cat :
         {&flatAndUnnudged, &slopedUnnudged, &nudgedFlatCamera, &everything}) {
        std::printf("  %s worst %d output vertices\n", cat->name, cat->worst);
        if (cat->worst > 0) {
            std::printf("      %s tile (%d,%d) heights [%d %d %d %d] yaw=%d/%d turn pitch=%.2f"
                        " -> %d kept + %d crossings\n",
                        zoneNames[cat->zone].c_str(), cat->tx, cat->ty, cat->heights[0],
                        cat->heights[1], cat->heights[2], cat->heights[3], cat->yawStep,
                        kProbeYaws, static_cast<double>(cat->pitch), cat->kept, cat->crossings);
        }
    }

    // The textbook bound is not wrong, it is conditional. A planar quad
    // whose four corners really are the corners of a convex square gives an
    // affine `forward` over a convex ring: its level set is a straight
    // line, which can only cross two of the four edges. `kept + 2 <= 5`.
    Check(flatAndUnnudged.worst <= 5,
          "a planar, un-nudged tile never exceeds 5 -- the old `count + 1` bound was right there");

    // Two separate things in the shipped data violate that precondition,
    // and each is on its own enough to produce a sixth vertex.
    Check(slopedUnnudged.worst > 5,
          "four independent corner heights break it once the camera pitches");
    Check(nudgedFlatCamera.worst > 5,
          "and so does M28's half-tile corner nudge, with no pitch at all");
    Check(everything.worst > 5,
          "so the five-entry buffer the old code used was genuinely too small");
    Check(everything.worst <= 8,
          "and never above the real 2*count bound, which is what the clipper is sized for now");


    // ---------------------------------------------------------------
    // Part 3 -- the crash itself. This is the regression test: the exact
    // sweep that used to fault now has to complete.
    // ---------------------------------------------------------------
    std::printf("[%.1fs]\n", phaseSeconds());
    std::printf("\n=== Part 3: sweeping real zones through ZoneRenderer::Render ===\n");

    // The second crash's precondition, taken straight off the loaded
    // resources: a model whose declared skin has no area. Every such model
    // used to hand `std::clamp(v, 0, width - 1)` an inverted range in
    // RasterizeModelTriangle, which a Debug MSVC STL turns into abort().
    int zonesWithSkinlessRoomMesh = 0, skinlessEntityModels = 0;
    for (size_t z = 0; z < zones.size(); ++z) {
        const sk::Model* room = zones[z].RoomMesh();
        bool skinless = room && (room->width <= 0 || room->height <= 0 || room->skinCount <= 0);
        if (skinless) {
            ++zonesWithSkinlessRoomMesh;
            std::printf("  %-13s room mesh has no drawable skin: %dx%d, skinCount=%d, %zu faces\n",
                        zoneNames[z].c_str(), room->width, room->height, room->skinCount,
                        room->faces.size());
        }
        for (const auto& pe : ResolveEntities(zones[z], types)) {
            const sk::Model* m = models.GetModel(pe.modelArchiveIndex);
            if (m && (m->width <= 0 || m->height <= 0 || m->skinCount <= 0)) ++skinlessEntityModels;
        }
    }
    std::printf("  %d of %zu zones ship a skinless room mesh; %d skinless entity placements\n",
                zonesWithSkinlessRoomMesh, zones.size(), skinlessEntityModels);
    if (!onlyZone) {
        Check(zonesWithSkinlessRoomMesh == 9,
              "nine shipped zones load a room mesh with a zero-area skin (see the M66 entry)");
    }

    sk::Backbuffer backbuffer;
    sk::ZoneRenderer renderer;
    std::vector<std::vector<sk::PlacedEntity>> entitiesPerZone;
    for (const sk::Zone& zone : zones) entitiesPerZone.push_back(ResolveEntities(zone, types));

    // 3a -- the pointed half. Render exactly the poses Part 2 identified as
    // producing more than five clipped vertices; before M66 each of these
    // wrote past the end of two stack arrays in the projection loop. A blind
    // sweep finds these only by luck, and only if it happens to sample the
    // right tile at the right angle.
    long long targetedFrames = 0;
    for (const OverflowCase& c : overflowCases) {
        const sk::Zone& zone = zones[c.zone];
        const sk::ZcpEntry& type = zone.TypeOf(zone.CellAt(c.tx, c.ty));
        sk::Camera camera;
        camera.x = (static_cast<float>(c.tx) + 0.5f) * sk::kTileScale;
        camera.y = (static_cast<float>(c.ty) + 0.5f) * sk::kTileScale;
        camera.z = static_cast<float>(type.floorHeight[0]) + sk::kEyeHeightOffset;
        camera.yaw = static_cast<float>(c.yawStep) * 2.0f * 3.14159265f /
                     static_cast<float>(kProbeYaws);
        camera.pitch = c.pitch;
        camera.fovY = 1.2f;
        renderer.Render(backbuffer, zone, camera, entitiesPerZone[c.zone], &models);
        ++targetedFrames;
    }
    std::printf("  %lld overflowing poses exist in the shipped data; rendered %lld of them\n",
                overflowTotal, targetedFrames);
    Check(overflowTotal > 0, "the shipped world really does contain poses that overflow the old buffer");
    Check(targetedFrames == static_cast<long long>(overflowCases.size()),
          "and every collected one renders without faulting");

    // 3b -- the broad half, for everything the targeted list does not
    // reach: whole zones on a stride, all eight compass yaws, every pitch.
    const float kSweepPitches[] = {0.0f, 0.25f, 0.5f, 0.85f, -0.25f, -0.5f, -0.85f};
    long long frames = 0;
    for (size_t z = 0; z < zones.size(); ++z) {
        const sk::Zone& zone = zones[z];
        long long zoneFrames = 0;
        for (int ty = 0; ty < zone.height(); ty += stride) {
            for (int tx = 0; tx < zone.width(); tx += stride) {
                const sk::ZmpCell& cell = zone.CellAt(tx, ty);
                if (cell.IsWall()) continue;
                const sk::ZcpEntry& type = zone.TypeOf(cell);
                for (int yawStep = 0; yawStep < 8; ++yawStep) {
                    for (float pitch : kSweepPitches) {
                        sk::Camera camera;
                        camera.x = (static_cast<float>(tx) + 0.5f) * sk::kTileScale;
                        camera.y = (static_cast<float>(ty) + 0.5f) * sk::kTileScale;
                        camera.z = static_cast<float>(type.floorHeight[0]) + sk::kEyeHeightOffset;
                        camera.yaw = static_cast<float>(yawStep) * 3.14159265f / 4.0f;
                        camera.pitch = pitch;
                        camera.fovY = 1.2f;
                        renderer.Render(backbuffer, zone, camera, entitiesPerZone[z], &models);
                        ++zoneFrames;
                    }
                }
            }
        }
        frames += zoneFrames;
        std::printf("  %-13s %6lld frames, %zu entities\n", zoneNames[z].c_str(), zoneFrames,
                    entitiesPerZone[z].size());
    }
    std::printf("  %lld targeted + %lld swept = %lld frames rendered\n", targetedFrames, frames,
                targetedFrames + frames);
    Check(frames > 1000, "the broad sweep really did cover the zones as well");
    Check(true, "ZoneRenderer::Render survived every frame (it faulted, and later aborted, before M66)");


    // ---------------------------------------------------------------
    // Part 4 -- why clipping the two triangles instead of the quad is
    // free on the geometry that is not broken. Within a plane every
    // attribute the rasterizer interpolates is affine, and a barycentric
    // sample of an affine attribute does not depend on which triangle of
    // which triangulation the sample point happened to land in.
    // ---------------------------------------------------------------
    std::printf("[%.1fs]\n", phaseSeconds());
    std::printf("\n=== Part 4: on a planar quad, the triangulation is not observable ===\n");
    {
        // A planar quad in the renderer's own corner order: corners 0 and
        // 2 are diagonally opposite, so planar means qz[0] + qz[2] ==
        // qz[1] + qz[3]. This one is the plane z = 12 + 2x - 2y.
        const float qx[4] = {0.0f, 1.0f, 1.0f, 0.0f};
        const float qy[4] = {1.0f, 1.0f, 0.0f, 0.0f};
        const float qz[4] = {10.0f, 12.0f, 14.0f, 12.0f};
        Check(std::fabs((qz[0] + qz[2]) - (qz[1] + qz[3])) < 1e-6f,
              "the reference quad is planar by the same diagonal-sum test Part 1 uses");

        // Barycentric sample of the attribute inside triangle (a, b, c),
        // the same weights RasterizeTriangle computes. Returns false when
        // the point is outside that triangle.
        auto sample = [&](int a, int b, int c, float px, float py, float* out) {
            float d = (qy[b] - qy[c]) * (qx[a] - qx[c]) + (qx[c] - qx[b]) * (qy[a] - qy[c]);
            if (std::fabs(d) < 1e-9f) return false;
            float l0 = ((qy[b] - qy[c]) * (px - qx[c]) + (qx[c] - qx[b]) * (py - qy[c])) / d;
            float l1 = ((qy[c] - qy[a]) * (px - qx[c]) + (qx[a] - qx[c]) * (py - qy[c])) / d;
            float l2 = 1.0f - l0 - l1;
            if (l0 < -1e-4f || l1 < -1e-4f || l2 < -1e-4f) return false;
            *out = l0 * qz[a] + l1 * qz[b] + l2 * qz[c];
            return true;
        };
        // Either triangulation covers the whole quad; take whichever
        // triangle of the pair contains the point.
        auto sampleVia = [&](int a0, int b0, int c0, int a1, int b1, int c1, float px, float py,
                             float* out) {
            return sample(a0, b0, c0, px, py, out) || sample(a1, b1, c1, px, py, out);
        };

        // The true plane, as an independent third opinion.
        auto planeZ = [](float px, float py) { return 12.0f + 2.0f * px - 2.0f * py; };

        int sampled = 0;
        float worstBetweenSplits = 0.0f, worstAgainstPlane = 0.0f;
        for (int iy = 1; iy < 20; ++iy) {
            for (int ix = 1; ix < 20; ++ix) {
                float px = static_cast<float>(ix) / 20.0f;
                float py = static_cast<float>(iy) / 20.0f;
                // Split along the 0-2 diagonal -- what the renderer now
                // does explicitly, and what the old code's fan over an
                // unclipped quad produced.
                float viaFan = 0.0f;
                if (!sampleVia(0, 1, 2, 0, 2, 3, px, py, &viaFan)) continue;
                // Split along the *other* diagonal. If the interpolated
                // value depended on where the split ran, this would differ.
                float viaOther = 0.0f;
                if (!sampleVia(1, 2, 3, 1, 3, 0, px, py, &viaOther)) continue;

                worstBetweenSplits = std::max(worstBetweenSplits, std::fabs(viaFan - viaOther));
                worstAgainstPlane = std::max(worstAgainstPlane, std::fabs(viaFan - planeZ(px, py)));
                ++sampled;
            }
        }
        std::printf("  %d interior points; worst disagreement between the two triangulations "
                    "%.6f, worst deviation from the plane %.6f\n",
                    sampled, static_cast<double>(worstBetweenSplits),
                    static_cast<double>(worstAgainstPlane));
        Check(sampled > 300, "the sample grid really covered the quad's interior");
        Check(worstBetweenSplits < 1e-4f,
              "the two triangulations agree everywhere -- where the split runs is not observable");
        Check(worstAgainstPlane < 1e-4f,
              "and both reproduce the quad's own plane, so nothing is being smoothed away");
    }


    std::printf("\n=========================================================\n");
    if (g_failures == 0) {
        std::printf("m66_render_clip_smoke: PASSED (all checks) (%d checks)\n", g_checks);
    } else {
        std::printf("m66_render_clip_smoke: %d/%d CHECKS FAILED\n", g_failures, g_checks);
    }
    return g_failures == 0 ? 0 : 1;
}
