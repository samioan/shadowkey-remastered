// M71 smoke test: the placement transform and the projection.
//
// Four things this milestone changed, each checked here against either the
// decompiled formula or the shipped level data:
//
//  1. **The projection.** `SurfaceFace_BuildAndProject` and
//     `Actor3D_TransformAndSubmitModel` both divide by `0x5800`/`0x6800`
//     (= 176/2 and 208/2 in 8.8), and `Render3DScene` pre-scales the camera
//     matrix's screen-right row by 208/176 -- so the real focal length is
//     104 px both ways and `fovY` is exactly pi/2. The port had 1.2 rad
//     (focal 152), a ~1.46x over-zoom. See render3d/camera.h.
//
//  2. **The heading convention.** `yaw = pi/2 - heading`, not `-heading`.
//     Checked three ways: against the engine's forward-move function, against
//     the camera matrix's own depth row, and -- the part that needs real data
//     -- against 180 shipped door placements whose heading is near 0 or 180
//     degrees, which is exactly where the old and new conventions disagree by
//     a half turn.
//
//  3. **The `-0x40` draw offset**, straight out of the actor transform.
//
//  4. **`.ent`'s two other orientation channels and its per-instance scale**,
//     decoded in docs/ZONE_FORMAT.md since M52 and never read until now.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "graphics/backbuffer.h"
#include "render3d/camera.h"
#include "render3d/zone_renderer.h"
#include "simkin_bindings/spell_projectile.h"
#include "world/entity_types.h"
#include "world/model_archive.h"
#include "world/zone.h"

namespace {

int gChecks = 0;
int gFailures = 0;

void Check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) {
        ++gFailures;
        std::printf("  FAIL: %s\n", what.c_str());
    } else {
        std::printf("  ok:   %s\n", what.c_str());
    }
}

const char* kZones[] = {"azra",     "broken1",      "broken2", "crypt1",   "crypt2",
                        "crypt3",   "delfhide",     "drgnfld", "dstar_e",  "dstar_w",
                        "erthcave", "ffarena",      "fearfrst", "ghstpass", "glaciercrawl",
                        "lakvan",   "lothcav",      "raiders", "snowline", "stouttp",
                        "twilite"};

constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530718f;

void WriteBackbufferPpm(const sk::Backbuffer& bb, const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    f << "P6\n" << sk::Backbuffer::kWidth << " " << sk::Backbuffer::kHeight << "\n255\n";
    for (int y = 0; y < sk::Backbuffer::kHeight; ++y) {
        const uint16_t* row = bb.Row(y);
        for (int x = 0; x < sk::Backbuffer::kWidth; ++x) {
            uint16_t c = row[x];
            f.put(static_cast<char>(((c >> 11) & 0x1f) << 3));
            f.put(static_cast<char>(((c >> 5) & 0x3f) << 2));
            f.put(static_cast<char>((c & 0x1f) << 3));
        }
    }
    std::printf("wrote %s\n", path.c_str());
}

// The engine's own model rotation, `BuildRotationMatrix3x4` (0x10073a70)
// transcribed independently of the renderer's copy, so the two can be
// compared. Angles in radians; returns the world (X, up, Y) image of a local
// (x, y, z) vertex.
struct Vec3f {
    float x, up, y;
};
Vec3f EngineRotate(float a, float b, float c, float vx, float vy, float vz) {
    const float cA = std::cos(a), sA = std::sin(a);
    const float cB = std::cos(b), sB = std::sin(b);
    const float cC = std::cos(c), sC = std::sin(c);
    Vec3f o;
    o.x = (cA * cC - sC * sB * sA) * vx + (sC * sB * cA + sA * cC) * vy + (sC * cB) * vz;
    o.up = (-cB * sA) * vx + (cB * cA) * vy + (-sB) * vz;
    o.y = (-cC * sB * sA - sC * cA) * vx + (cC * sB * cA - sC * sA) * vy + (cB * cC) * vz;
    return o;
}

bool NearlyEqual(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

// Resolves a zone's placements into the renderer's own PlacedEntity list the
// same way main.cpp's zone-load loop does -- including the M71 fields, which
// is the point of the exercise.
std::vector<sk::PlacedEntity> ResolvePlacements(const sk::Zone& zone,
                                                 const sk::EntityTypeTable& types) {
    std::vector<sk::PlacedEntity> out;
    for (const sk::Zone::EntPlacement& e : zone.entities()) {
        const sk::EntityTypeDescriptor* desc = types.Lookup(e.typeId);
        if (!desc) continue;
        sk::PlacedEntity pe{static_cast<float>(e.x), static_cast<float>(e.y),
                             static_cast<float>(e.z), desc->modelArchiveIndex};
        pe.yaw = sk_bindings::PortYawFromEngineYaw(static_cast<int>(e.yawRaw));
        pe.rotA = static_cast<float>(e.rotARaw) / 65536.0f * kTwoPi;
        pe.rotB = static_cast<float>(e.rotBRaw) / 65536.0f * kTwoPi;
        pe.scale = e.scaleRaw > 0 ? static_cast<float>(e.scaleRaw) / 256.0f : 1.0f;
        out.push_back(pe);
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    // ---------------------------------------------------------------
    // Part 1 -- the projection.
    // ---------------------------------------------------------------
    std::printf("\n-- part 1: the real projection --\n");
    {
        // `screenY = 0x6800 - viewUp * 0x6800 / viewDepth`, and 0x6800 >> 8
        // is 104 -- half of 208. A focal length equal to the half-height is
        // a 90 degree vertical field of view, which is what kEngineFovY is.
        const float focalY = (sk::Backbuffer::kHeight * 0.5f) / std::tan(sk::kEngineFovY * 0.5f);
        Check(NearlyEqual(focalY, 104.0f, 0.01f),
              "kEngineFovY reproduces the engine's 0x6800 >> 8 == 104 vertical focal length");
        // And the horizontal one is 88 (0x5800 >> 8) times the 208/176 row-0
        // scale `Render3DScene` applies -- also 104, i.e. isotropic.
        const float focalX = (sk::Backbuffer::kWidth * 0.5f) *
                              (static_cast<float>(sk::Backbuffer::kHeight) /
                               static_cast<float>(sk::Backbuffer::kWidth));
        Check(NearlyEqual(focalX, focalY, 0.01f),
              "0x5800 >> 8 scaled by 208/176 equals the vertical focal length (focalX == focalY)");
        Check(sk::Camera().fovY == sk::kEngineFovY,
              "a default-constructed Camera uses the engine's field of view");
    }

    // ---------------------------------------------------------------
    // Part 2 -- the heading convention, from the decompiled formulas.
    // ---------------------------------------------------------------
    std::printf("\n-- part 2: yaw = pi/2 - heading --\n");
    {
        bool forwardOk = true, roundTripOk = true;
        for (int raw = 0; raw < 0x10000; raw += 0x40) {
            const float h = static_cast<float>(raw) / 65536.0f * kTwoPi;
            const float yaw = sk_bindings::PortYawFromEngineYaw(raw);
            // `FUN_100065b4` (move forward) advances by (sin h, cos h); the
            // camera matrix `FUN_10073760(m, -roll, -pitch, -h)`'s depth row
            // is the same vector. This port's forward is (cos yaw, sin yaw).
            if (!NearlyEqual(std::cos(yaw), std::sin(h)) ||
                !NearlyEqual(std::sin(yaw), std::cos(h))) {
                forwardOk = false;
            }
            if (((sk_bindings::EngineYawFromPortYaw(yaw) - raw) & 0xffff) != 0) roundTripOk = false;
        }
        Check(forwardOk,
              "PortYawFromEngineYaw(h) turns the engine's forward (sin h, cos h) into this "
              "port's (cos yaw, sin yaw), for all 1024 sampled headings");
        Check(roundTripOk, "EngineYawFromPortYaw inverts it exactly, so the compass cannot drift");

        // The renderer composes `modelYaw = yaw + kModelForwardYawOffset` and
        // then rotates by `C = -modelYaw`. The engine passes
        // `heading + 0x8000`. Those must be the same angle.
        bool composeOk = true;
        for (int raw = 0; raw < 0x10000; raw += 0x40) {
            const float h = static_cast<float>(raw) / 65536.0f * kTwoPi;
            const float modelYaw =
                sk_bindings::PortYawFromEngineYaw(raw) + sk::kModelForwardYawOffset;
            const float engineC = h + kPi;  // heading + 0x8000
            if (!NearlyEqual(std::cos(-modelYaw), std::cos(engineC)) ||
                !NearlyEqual(std::sin(-modelYaw), std::sin(engineC))) {
                composeOk = false;
            }
        }
        Check(composeOk,
              "the renderer's C = -(yaw + pi/2) is exactly the engine's `heading + 0x8000`");

        // And the transform itself: with the two extra channels at zero, the
        // engine's matrix must send local -Z to the entity's facing.
        bool facingOk = true;
        for (int raw = 0; raw < 0x10000; raw += 0x100) {
            const float h = static_cast<float>(raw) / 65536.0f * kTwoPi;
            const Vec3f f = EngineRotate(0.0f, 0.0f, h + kPi, 0.0f, 0.0f, -1.0f);
            if (!NearlyEqual(f.x, std::sin(h)) || !NearlyEqual(f.y, std::cos(h))) facingOk = false;
        }
        Check(facingOk,
              "BuildRotationMatrix3x4 sends the model's local -Z to (sin h, cos h) -- the same "
              "forward vector the camera and the move code use, confirming M36's local -Z");
    }

    // ---------------------------------------------------------------
    // Part 3 -- the transform against real doors in real doorways.
    //
    // A door model is a slab hinged at one end of its local X, so where its
    // panel lands is a directional fact the level data can adjudicate: it
    // has to lie in the gap in the wall, not in the wall. The old and new
    // conventions differ by a half turn, so they only disagree for headings
    // near 0 and 180 -- which is both why this test filters to those and why
    // the old one survived (doors cluster at 90 and 270, where the two
    // agree, and a slab looks the same either way round anyway).
    // ---------------------------------------------------------------
    std::printf("\n-- part 3: real door panels land in real doorways --\n");
    {
        sk::EntityTypeTable types;
        sk::ModelArchive models;
        if (!types.Load(scriptRoot) || !models.Load(scriptRoot)) {
            std::printf("m71_placement_smoke: FAILED to load entity/model tables\n");
            return 1;
        }
        int newBetter = 0, oldBetter = 0, tie = 0;
        for (const char* zoneName : kZones) {
            sk::Zone zone;
            if (!zone.Load(scriptRoot, zoneName)) continue;
            for (const sk::Zone::EntPlacement& e : zone.entities()) {
                const sk::EntityTypeDescriptor* desc = types.Lookup(e.typeId);
                if (!desc || desc->category != 11) continue;
                const sk::Model* model = models.GetModel(desc->modelArchiveIndex);
                if (!model || model->vertices.empty()) continue;

                const float deg = static_cast<float>(e.yawRaw) * 360.0f / 65536.0f;
                const float off = std::fmod(deg, 180.0f);
                if ((std::min)(off, 180.0f - off) > 20.0f) continue;  // conventions agree here

                int16_t minX = 32767, maxX = -32768, minZ = 32767, maxZ = -32768;
                for (const sk::ModelVertex& v : model->vertices) {
                    minX = (std::min)(minX, v.x);
                    maxX = (std::max)(maxX, v.x);
                    minZ = (std::min)(minZ, v.z);
                    maxZ = (std::max)(maxZ, v.z);
                }
                if (maxX - minX < 3 * (maxZ - minZ)) continue;  // not a slab
                float far = std::abs(minX) > std::abs(maxX) ? minX : maxX;
                if (std::fabs(far) < 200.0f) continue;  // hinge not at one end
                far *= e.scaleRaw > 0 ? static_cast<float>(e.scaleRaw) / 256.0f : 1.0f;

                const float h = static_cast<float>(e.yawRaw) / 65536.0f * kTwoPi;
                // Where the engine's transform puts local +X, and where the
                // pre-M71 port put it.
                const float dirs[2][2] = {{-std::cos(h), -std::sin(h)}, {std::cos(h), std::sin(h)}};
                int walls[2] = {0, 0};
                for (int k = 0; k < 2; ++k) {
                    for (float t : {0.4f, 0.7f, 1.0f}) {
                        int tx = static_cast<int>(e.x + far * dirs[k][0] * t) / 256;
                        int ty = static_cast<int>(e.y + far * dirs[k][1] * t) / 256;
                        if (!zone.InBounds(tx, ty) || zone.CellAt(tx, ty).IsWall()) ++walls[k];
                    }
                }
                if (walls[0] < walls[1]) ++newBetter;
                else if (walls[1] < walls[0]) ++oldBetter;
                else ++tie;
            }
        }
        std::printf("  discriminating door placements: %d new / %d old / %d tie\n", newBetter,
                    oldBetter, tie);
        Check(newBetter > 100, "the corrected convention clears the wall for 100+ real doors");
        Check(newBetter > 20 * oldBetter,
              "and it beats the old convention by more than 20:1 on the placements where the "
              "two actually differ");
    }

    // ---------------------------------------------------------------
    // Part 4 -- the .ent fields this port had never read.
    // ---------------------------------------------------------------
    std::printf("\n-- part 4: rotOrScale[0]/[2] and the +0x5e scale --\n");
    {
        int total = 0, withRotA = 0, withRotB = 0, scaled = 0, zeroScale = 0;
        int minScale = 0x7fffffff, maxScale = 0;
        for (const char* zoneName : kZones) {
            sk::Zone zone;
            if (!zone.Load(scriptRoot, zoneName)) continue;
            for (const sk::Zone::EntPlacement& e : zone.entities()) {
                ++total;
                if (e.rotARaw != 0) ++withRotA;
                if (e.rotBRaw != 0) ++withRotB;
                if (e.scaleRaw != 256) ++scaled;
                if (e.scaleRaw == 0) ++zeroScale;
                minScale = (std::min<int>)(minScale, e.scaleRaw);
                maxScale = (std::max<int>)(maxScale, e.scaleRaw);
            }
        }
        std::printf("  %d placements: rotA!=0 %d, rotB!=0 %d, scale!=256 %d (range %d..%d)\n", total,
                    withRotA, withRotB, scaled, minScale, maxScale);
        Check(zeroScale == 0, "no shipped placement carries a zero model scale");
        Check(scaled > 500,
              "hundreds of shipped placements carry a non-identity scale, so ignoring it was a "
              "real visual bug, not a rounding one");
        Check(withRotA > 100 && withRotB > 100,
              "and both extra orientation channels are genuinely used by real placements");
        Check(minScale > 0 && maxScale > 256,
              "the scale spans both smaller and larger than 1:1, as an 8.8 multiplier should");
    }

    // ---------------------------------------------------------------
    // Part 5 -- the -0x40 draw offset, and a real frame from the real
    // player start to eyeball against a device screenshot.
    // ---------------------------------------------------------------
    std::printf("\n-- part 5: the -0x40 draw offset and a start-position frame --\n");
    {
        sk::EntityTypeTable types;
        sk::ModelArchive models;
        sk::Zone zone;
        if (!types.Load(scriptRoot) || !models.Load(scriptRoot) ||
            !zone.Load(scriptRoot, "azra")) {
            std::printf("m71_placement_smoke: FAILED to load azra\n");
            return 1;
        }
        Check(sk::kEntityDrawZOffset == -64.0f,
              "kEntityDrawZOffset is the actor transform's literal -0x40");

        // And what it does to the shipped data, which is the actual claim:
        // measure every floor-standing scenery placement's lowest vertex
        // against the floor height of the tile it stands on, once with the
        // offset and once without. Placements more than two tiles off the
        // floor are wall fittings, roofs and hanging props -- they have no
        // ground contact to check, so they are excluded rather than counted
        // as failures.
        {
            std::vector<float> gaps;
            for (const char* zoneName : kZones) {
                sk::Zone z;
                if (!z.Load(scriptRoot, zoneName)) continue;
                for (const sk::Zone::EntPlacement& e : z.entities()) {
                    const sk::EntityTypeDescriptor* desc = types.Lookup(e.typeId);
                    if (!desc || desc->category != 1) continue;  // scenery only
                    const sk::Model* model = models.GetModel(desc->modelArchiveIndex);
                    if (!model || model->vertices.empty()) continue;
                    int tx = e.x / 256, ty = e.y / 256;
                    if (!z.InBounds(tx, ty)) continue;
                    const sk::ZcpEntry& t = z.TypeOf(z.CellAt(tx, ty));
                    float floor = static_cast<float>(
                        *std::max_element(t.floorHeight, t.floorHeight + 4));
                    int16_t lowest = 32767;
                    for (const sk::ModelVertex& v : model->vertices) {
                        lowest = (std::min)(lowest, v.y);
                    }
                    const float scale =
                        e.scaleRaw > 0 ? static_cast<float>(e.scaleRaw) / 256.0f : 1.0f;
                    const float gap = static_cast<float>(e.z) + lowest * scale - floor;
                    if (std::fabs(gap) > 512.0f) continue;
                    gaps.push_back(gap);
                }
            }
            int floatingBefore = 0, floatingAfter = 0;
            for (float g : gaps) {
                if (g > 0.0f) ++floatingBefore;
                if (g + sk::kEntityDrawZOffset > 0.0f) ++floatingAfter;
            }
            const int n = static_cast<int>(gaps.size());
            std::printf("  %d floor-standing scenery placements: %.1f%% clear of the floor "
                        "without the offset, %.1f%% with it\n",
                        n, 100.0f * floatingBefore / n, 100.0f * floatingAfter / n);
            Check(n > 3000, "the census covers thousands of real floor props");
            Check(floatingBefore * 2 > n,
                  "without the offset a clear majority of floor props hang above their tile's "
                  "floor -- the reported 'tables hovering a few inches off the ground'");
            Check(floatingAfter * 2 < n,
                  "and with the engine's own -0x40 a majority instead sit at or just below it, "
                  "which is what a planted prop looks like");
        }

        std::vector<sk::PlacedEntity> entities = ResolvePlacements(zone, types);
        Check(!entities.empty(), "azra's placements resolve to renderable models");

        sk::Camera camera;
        camera.x = static_cast<float>(zone.playerStartX);
        camera.y = static_cast<float>(zone.playerStartY);
        camera.z = static_cast<float>(zone.playerStartZ) + sk::kEyeHeightOffset;
        camera.yaw = sk_bindings::PortYawFromEngineYaw(zone.playerStartYawRaw);
        camera.pitch =
            static_cast<float>(static_cast<int16_t>(zone.playerStartPitchRaw)) / 65536.0f * kTwoPi;

        sk::Backbuffer backbuffer;
        sk::ZoneRenderer renderer;
        renderer.Render(backbuffer, zone, camera, entities, &models);
        WriteBackbufferPpm(backbuffer, "placement_azra_start.ppm");

        // The frame has to actually contain the room: a view that is all
        // background would pass every geometric check above while showing
        // nothing.
        int nonBackground = 0;
        for (int y = 0; y < sk::Backbuffer::kHeight; ++y) {
            const uint16_t* row = backbuffer.Row(y);
            for (int x = 0; x < sk::Backbuffer::kWidth; ++x) {
                if (row[x] != sk::kBackgroundFill) ++nonBackground;
            }
        }
        const int totalPixels = sk::Backbuffer::kWidth * sk::Backbuffer::kHeight;
        std::printf("  azra start frame: %d/%d non-background pixels, yaw %.1f deg\n",
                    nonBackground, totalPixels, camera.yaw * 180.0f / kPi);
        Check(nonBackground > totalPixels / 2,
              "the azra start frame is mostly real geometry, not empty background");

        // Widening the field of view must show strictly more of the room, and
        // the engine's is wider than the 1.2 rad this port used to use -- the
        // "why is the starting room so much bigger in the original" symptom.
        Check(sk::kEngineFovY > 1.2f,
              "the engine's field of view is wider than the port's old 1.2 rad guess");
        const float oldFocal = (sk::Backbuffer::kHeight * 0.5f) / std::tan(1.2f * 0.5f);
        const float newFocal = (sk::Backbuffer::kHeight * 0.5f) / std::tan(sk::kEngineFovY * 0.5f);
        std::printf("  focal length: was %.1f px, now %.1f px (%.2fx less zoomed)\n", oldFocal,
                    newFocal, oldFocal / newFocal);
        Check(oldFocal / newFocal > 1.4f,
              "which is a ~1.46x de-zoom, the scale of the difference in the screenshots");
    }

    std::printf("\nm71_placement_smoke: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
