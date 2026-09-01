#include "render3d/zone_renderer.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace sk {

namespace {

constexpr float kTileScale = 256.0f;  // world units per tile (docs/WORLD_MODEL.md)

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

struct Face {
    Vec3 corners[4];  // winding: 0,1,2,3 around the quad
    int surIndex = 0xff;
};

// One quad per direction/band; corner order matches a consistent
// counter-clockwise winding when viewed from the outward-facing side --
// exact winding doesn't matter here since the rasterizer below doesn't
// backface-cull (small faces are cheap enough at this screen/tile scale
// to just always draw and let the z-buffer sort it out).
void AddFloorCeiling(std::vector<Face>& faces, int tx, int ty, const int16_t heights[4],
                      uint8_t surIndex) {
    if (surIndex == 0xff) return;
    Face f;
    f.surIndex = surIndex;
    float x0 = static_cast<float>(tx), x1 = x0 + 1.0f;
    float y0 = static_cast<float>(ty), y1 = y0 + 1.0f;
    f.corners[0] = {x0, y0, heights[0] / 1.0f};
    f.corners[1] = {x1, y0, heights[1] / 1.0f};
    f.corners[2] = {x1, y1, heights[2] / 1.0f};
    f.corners[3] = {x0, y1, heights[3] / 1.0f};
    faces.push_back(f);
}

// Wall face spanning the boundary edge (ex0,ey0)-(ex1,ey1), from
// floorZ0/floorZ1 (the two edge-endpoint floor heights) up to
// ceilZ0/ceilZ1 -- lets a wall follow a sloped/stepped neighbor rather
// than assuming a flat rectangle.
void AddWall(std::vector<Face>& faces, float ex0, float ey0, float ex1, float ey1, float floorZ0,
             float floorZ1, float ceilZ0, float ceilZ1, uint8_t surIndex) {
    if (surIndex == 0xff) return;
    if (floorZ0 >= ceilZ0 && floorZ1 >= ceilZ1) return;  // degenerate (no vertical extent)
    Face f;
    f.surIndex = surIndex;
    f.corners[0] = {ex0, ey0, floorZ0};
    f.corners[1] = {ex1, ey1, floorZ1};
    f.corners[2] = {ex1, ey1, ceilZ1};
    f.corners[3] = {ex0, ey0, ceilZ0};
    faces.push_back(f);
}

// Collects the faces to draw around the camera's tile.
//
// SIMPLIFICATION (see zone_renderer.h): the real engine reads a wall
// face's .sur index from the *neighboring* tile's ZcpEntry (per
// docs/RENDERER_3D.md's traversal writeup) with a camera-side gate; here
// every open (non-wall) tile just draws its own E/W/S/N "lower band"
// index toward any neighbor that's a wall or out of bounds, using that
// edge's two real corner heights from the current tile's own
// floorHeight/ceilingHeight arrays. This produces a real, textured,
// correctly-occluded dungeon corridor from the same source data; it
// just isn't a guaranteed pixel-for-pixel match to the original's exact
// per-direction indexing convention (untraced in this pass -- corners 0-3's
// mapping to NE/SE/SW/NW isn't independently confirmed either, so getting
// this "exactly" right isn't possible yet without more RE work).
std::vector<Face> CollectFaces(const Zone& zone, int centerX, int centerY, int radius) {
    std::vector<Face> faces;
    for (int ty = centerY - radius; ty <= centerY + radius; ++ty) {
        for (int tx = centerX - radius; tx <= centerX + radius; ++tx) {
            if (!zone.InBounds(tx, ty)) continue;
            const ZmpCell& cell = zone.CellAt(tx, ty);
            if (cell.IsWall()) continue;  // walls themselves aren't walked into/drawn from
            const ZcpEntry& t = zone.TypeOf(cell);

            AddFloorCeiling(faces, tx, ty, t.floorHeight, t.surIndexFloor);
            AddFloorCeiling(faces, tx, ty, t.ceilingHeight, t.surIndexCeilingA);

            float x0 = static_cast<float>(tx), x1 = x0 + 1.0f;
            float y0 = static_cast<float>(ty), y1 = y0 + 1.0f;

            auto neighborBlocks = [&](int nx, int ny) {
                return !zone.InBounds(nx, ny) || zone.CellAt(nx, ny).IsWall();
            };

            if (neighborBlocks(tx + 1, ty)) {  // east edge: corners 1 (NE) / 2 (SE)
                AddWall(faces, x1, y0, x1, y1, t.floorHeight[1], t.floorHeight[2],
                        t.ceilingHeight[1], t.ceilingHeight[2], t.surIndexE_lo);
            }
            if (neighborBlocks(tx - 1, ty)) {  // west edge: corners 0 (NW) / 3 (SW)
                AddWall(faces, x0, y1, x0, y0, t.floorHeight[3], t.floorHeight[0],
                        t.ceilingHeight[3], t.ceilingHeight[0], t.surIndexW_lo);
            }
            if (neighborBlocks(tx, ty + 1)) {  // south edge: corners 2 (SE) / 3 (SW)
                AddWall(faces, x1, y1, x0, y1, t.floorHeight[2], t.floorHeight[3],
                        t.ceilingHeight[2], t.ceilingHeight[3], t.surIndexS_lo);
            }
            if (neighborBlocks(tx, ty - 1)) {  // north edge: corners 0 (NW) / 1 (NE)
                AddWall(faces, x0, y0, x1, y0, t.floorHeight[0], t.floorHeight[1],
                        t.ceilingHeight[0], t.ceilingHeight[1], t.surIndexN_lo);
            }
        }
    }
    return faces;
}

struct ProjectedVertex {
    float sx = 0, sy = 0;  // screen space
    float invW = 0;        // 1/viewForward, for perspective-correct interpolation
    float u = 0, v = 0;    // texture-space, already divided by w (i.e. u/w, v/w)
};

// 4-bit-per-channel color (docs/GRAPHICS_FORMAT.md), 0x0RGB -> RGB565 for
// the backbuffer.
uint16_t ExpandRGB444(uint16_t raw444) {
    uint8_t r = static_cast<uint8_t>((raw444 >> 8) & 0xf);
    uint8_t g = static_cast<uint8_t>((raw444 >> 4) & 0xf);
    uint8_t b = static_cast<uint8_t>(raw444 & 0xf);
    return PackRGB565(static_cast<uint8_t>(r * 17), static_cast<uint8_t>(g * 17),
                       static_cast<uint8_t>(b * 17));
}

void RasterizeTriangle(Backbuffer& backbuffer, std::vector<float>& depthBuffer,
                        const ProjectedVertex& a, const ProjectedVertex& b,
                        const ProjectedVertex& c, const Zone& zone, int textureIndex) {
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
            int tx = std::clamp(static_cast<int>(u * 128.0f), 0, 127);
            int ty = std::clamp(static_cast<int>(v * 128.0f), 0, 127);

            uint8_t texel = zone.TexelAt(textureIndex, tx, ty);
            uint16_t raw444 = zone.PaletteColor(textureIndex, texel);
            if (raw444 == 0x0f0f) continue;  // chroma-key cutout (docs/GRAPHICS_FORMAT.md)

            depthBuffer[static_cast<size_t>(depthIndex)] = invW;
            backbuffer.SetPixel(px, py, ExpandRGB444(raw444));
        }
    }
}

}  // namespace

void ZoneRenderer::Render(Backbuffer& backbuffer, const Zone& zone, const Camera& camera) const {
    backbuffer.Fill(PackRGB565(8, 8, 16));
    std::vector<float> depthBuffer(static_cast<size_t>(Backbuffer::kWidth) * Backbuffer::kHeight,
                                    0.0f);

    int centerX = static_cast<int>(camera.x / kTileScale);
    int centerY = static_cast<int>(camera.y / kTileScale);
    std::vector<Face> faces = CollectFaces(zone, centerX, centerY, renderRadius);

    float camX = camera.x / kTileScale, camY = camera.y / kTileScale, camZ = camera.z / kTileScale;
    float cosYaw = std::cos(camera.yaw), sinYaw = std::sin(camera.yaw);
    // forward = (cosYaw, sinYaw, 0); right = (sinYaw, -cosYaw, 0); up = (0,0,1)
    float aspect = static_cast<float>(Backbuffer::kWidth) / static_cast<float>(Backbuffer::kHeight);
    float focalY = (Backbuffer::kHeight * 0.5f) / std::tan(camera.fovY * 0.5f);
    float focalX = focalY;  // square-ish texel assumption; aspect handled via screen center only
    (void)aspect;

    static const float kUV[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

    for (const Face& face : faces) {
        uint8_t texIdx = zone.surfaceTextureIndex(face.surIndex);

        ProjectedVertex pv[4];
        bool anyBehind = false;
        for (int i = 0; i < 4; ++i) {
            float rx = face.corners[i].x - camX;
            float ry = face.corners[i].y - camY;
            float rz = face.corners[i].z / kTileScale - camZ;

            float viewRight = rx * sinYaw - ry * cosYaw;
            float viewForward = rx * cosYaw + ry * sinYaw;
            float viewUp = rz;

            if (viewForward < 0.05f) {
                anyBehind = true;
                break;
            }
            pv[i].invW = 1.0f / viewForward;
            pv[i].sx = Backbuffer::kWidth * 0.5f + viewRight * focalX * pv[i].invW;
            pv[i].sy = Backbuffer::kHeight * 0.5f - viewUp * focalY * pv[i].invW;
            pv[i].u = kUV[i][0] * pv[i].invW;
            pv[i].v = kUV[i][1] * pv[i].invW;
        }
        if (anyBehind) continue;  // whole-quad near-plane cull, see header comment

        RasterizeTriangle(backbuffer, depthBuffer, pv[0], pv[1], pv[2], zone, texIdx);
        RasterizeTriangle(backbuffer, depthBuffer, pv[0], pv[2], pv[3], zone, texIdx);
    }
}

}  // namespace sk
