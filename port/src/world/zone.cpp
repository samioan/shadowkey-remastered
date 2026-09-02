#include "world/zone.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

#include "assets/zone_file.h"

namespace sk {

namespace {

constexpr int kZtxSlotSize = 0x4000;   // 128*128, 8bpp
constexpr size_t kZcpEntrySize = 36;
constexpr size_t kZmpCellSize = 6;
constexpr size_t kEntRecordSize = 0x48;

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::printf("Zone: could not open %s\n", path.c_str());
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

uint16_t ReadU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
int16_t ReadI16(const uint8_t* p) { return static_cast<int16_t>(ReadU16(p)); }
uint32_t ReadU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
int32_t ReadI32(const uint8_t* p) { return static_cast<int32_t>(ReadU32(p)); }

}  // namespace

bool Zone::Load(const std::string& scriptRoot, const std::string& zoneName) {
    std::string base = scriptRoot + "/" + zoneName;

    // --- .zmp: header (field80 x zmpTotal grid dims) + per-cell records ---
    std::vector<uint8_t> zmp = LoadCompressedZoneFile(base + ".zmp");
    if (zmp.size() < 0x84) {
        std::printf("Zone: %s.zmp missing/too short\n", zoneName.c_str());
        return false;
    }
    width_ = ReadU16(&zmp[0x80]);
    height_ = ReadU16(&zmp[0x82]);
    size_t cellCount = static_cast<size_t>(width_) * static_cast<size_t>(height_);
    if (zmp.size() < 0x84 + cellCount * kZmpCellSize) {
        std::printf("Zone: %s.zmp too short for %dx%d cell grid\n", zoneName.c_str(), width_,
                    height_);
        return false;
    }
    cells_.resize(cellCount);
    for (size_t i = 0; i < cellCount; ++i) {
        const uint8_t* p = &zmp[0x84 + i * kZmpCellSize];
        cells_[i].flags = p[0];
        cells_[i].lightLevel = ReadU16(p + 2);
        cells_[i].zcpIndex = ReadU16(p + 4);
    }

    // --- .zcp: u32 entryCount (see zone.h's correction note) + 36-byte entries ---
    std::vector<uint8_t> zcp = LoadCompressedZoneFile(base + ".zcp");
    if (zcp.size() < 4) {
        std::printf("Zone: %s.zcp missing/too short\n", zoneName.c_str());
        return false;
    }
    uint32_t zcpCount = ReadU32(&zcp[0]);
    if (zcp.size() < 4 + static_cast<size_t>(zcpCount) * kZcpEntrySize) {
        std::printf("Zone: %s.zcp too short for %u entries\n", zoneName.c_str(), zcpCount);
        return false;
    }
    zcpEntries_.resize(zcpCount);
    for (uint32_t i = 0; i < zcpCount; ++i) {
        const uint8_t* p = &zcp[4 + static_cast<size_t>(i) * kZcpEntrySize];
        ZcpEntry& e = zcpEntries_[i];
        e.lightDelta = static_cast<int8_t>(p[0]);
        e.ceilingBandThreshold = ReadI16(p + 4);
        for (int c = 0; c < 4; ++c) e.floorHeight[c] = ReadI16(p + 6 + c * 2);
        for (int c = 0; c < 4; ++c) e.ceilingHeight[c] = ReadI16(p + 0xe + c * 2);
        e.surIndexE_lo = p[0x16];
        e.surIndexW_lo = p[0x17];
        e.surIndexS_lo = p[0x18];
        e.surIndexN_lo = p[0x19];
        e.surIndexE_hi = p[0x1a];
        e.surIndexW_hi = p[0x1b];
        e.surIndexS_hi = p[0x1c];
        e.surIndexN_hi = p[0x1d];
        e.surIndexCeilingA = p[0x1e];
        e.surIndexCeilingB = p[0x1f];
        e.surIndexFloor = p[0x20];
    }

    // M9: replace the on-disk lightLevel (an unused editor leftover, see
    // zone.h's ZmpCell comment) with the real bake -- needs both cells_
    // and zcpEntries_, so this is the earliest point both are ready.
    BakeLighting();

    // --- .sur: u8 count + 8-byte records. Byte[7] (the .ztx texture slot
    // index) and byte[6] (flags -- bit0 flip V, bit1 flip U, bit5
    // disable-this-face, see zone.h's surfaceDisabled()/PaletteColor()
    // comments) are used here; bytes[0..5] (UV shift/offset) are still
    // approximated rather than reproduced exactly (each wall/floor quad
    // already spans a full 0..1 UV range over its dedicated 128x128 atlas
    // slot). Field layout confirmed this session by fully decompiling
    // SurfaceFace_BuildAndProject (0x1005d784,
    // shadowkey/extracted/decomp_1005d784.c): byte[0]/[1] = U/V
    // bit-shift, byte[2..3]/[4..5] = two signed-16-bit UV offsets,
    // byte[6] = flags, byte[7] = texture index (all read via
    // `iVar4 = surIndex*8 + tableBase` then `*(char*)(iVar4+N)`).
    std::vector<uint8_t> sur;
    if (!ReadWholeFile(base + ".sur", sur) || sur.empty()) {
        std::printf("Zone: %s.sur missing/empty\n", zoneName.c_str());
        return false;
    }
    uint8_t surCount = sur[0];
    if (sur.size() < static_cast<size_t>(1) + static_cast<size_t>(surCount) * 8) {
        std::printf("Zone: %s.sur too short for %u records\n", zoneName.c_str(), surCount);
        return false;
    }
    surTextureIndex_.resize(surCount);
    surFlags_.resize(surCount);
    for (uint8_t i = 0; i < surCount; ++i) {
        const uint8_t* rec = &sur[1 + static_cast<size_t>(i) * 8];
        surTextureIndex_[i] = rec[7];
        surFlags_[i] = rec[6];
    }

    // --- .ztx: 1 header byte + N * 0x4000-byte 128x128 8bpp texture slots ---
    ztxData_ = LoadCompressedZoneFile(base + ".ztx");
    if (ztxData_.size() < 1) {
        std::printf("Zone: %s.ztx missing/empty\n", zoneName.c_str());
        return false;
    }

    // --- .zlu: N * 2048-byte (4x512) palette sets ---
    zluData_ = LoadCompressedZoneFile(base + ".zlu");
    if (zluData_.empty()) {
        std::printf("Zone: %s.zlu missing/empty\n", zoneName.c_str());
        return false;
    }

    // --- .zsk: whole-room static mesh (M11), see zone.h's RoomMesh()
    // comment -- an ordinary MODEL_FORMAT.md resource once decompressed.
    // Not fatal if missing/unparsed: falls back to no room mesh drawn.
    std::vector<uint8_t> zsk = LoadCompressedZoneFile(base + ".zsk");
    roomMeshValid_ = !zsk.empty() && ParseModelResource(zsk.data(), zsk.size(), roomMesh_);
    if (!roomMeshValid_) {
        std::printf("Zone: %s.zsk missing or unparsed -- no room mesh\n", zoneName.c_str());
    }

    // --- .ent: find the player start record (typeId == 1) ---
    std::vector<uint8_t> ent;
    if (!ReadWholeFile(base + ".ent", ent) || ent.size() < 4) {
        std::printf("Zone: %s.ent missing/too short\n", zoneName.c_str());
        return false;
    }
    uint32_t entCount = ReadU32(&ent[0]);
    bool foundStart = false;
    entities_.clear();
    for (uint32_t i = 0; i < entCount && 4 + (i + 1) * kEntRecordSize <= ent.size(); ++i) {
        const uint8_t* p = &ent[4 + static_cast<size_t>(i) * kEntRecordSize];
        int32_t typeId = ReadI32(p + 0x1c);
        int32_t x = ReadI32(p + 0x00);
        int32_t y = ReadI32(p + 0x04);
        int32_t z = ReadI32(p + 0x08);
        if (typeId == 1) {
            playerStartX = x;
            playerStartY = y;
            playerStartZ = z;
            foundStart = true;
        } else if (typeId > 1) {
            entities_.push_back({x, y, z, typeId});
        }
    }
    if (!foundStart) {
        std::printf("Zone: %s.ent has no player-start (typeId==1) record\n", zoneName.c_str());
        return false;
    }

    std::printf(
        "Zone: loaded %s -- %dx%d tiles, %u zcp entries, %u surfaces, player start (%d,%d), "
        "%zu placed entities\n",
        zoneName.c_str(), width_, height_, zcpCount, surCount, playerStartX >> 8, playerStartY >> 8,
        entities_.size());
    return true;
}

const ZmpCell& Zone::CellAt(int tileX, int tileY) const {
    static const ZmpCell kOutOfBounds{};
    if (!InBounds(tileX, tileY)) return kOutOfBounds;
    return cells_[static_cast<size_t>(tileY) * static_cast<size_t>(width_) + static_cast<size_t>(tileX)];
}

const ZcpEntry& Zone::TypeOf(const ZmpCell& cell) const {
    static const ZcpEntry kEmpty{};
    if (cell.zcpIndex >= zcpEntries_.size()) return kEmpty;
    return zcpEntries_[cell.zcpIndex];
}

uint8_t Zone::surfaceTextureIndex(int surIndex) const {
    if (surIndex < 0 || static_cast<size_t>(surIndex) >= surTextureIndex_.size()) return 0;
    return surTextureIndex_[static_cast<size_t>(surIndex)];
}

bool Zone::surfaceDisabled(int surIndex) const {
    if (surIndex < 0 || static_cast<size_t>(surIndex) >= surFlags_.size()) return false;
    return (surFlags_[static_cast<size_t>(surIndex)] & 0x20) != 0;
}

uint8_t Zone::TexelAt(int surfaceTextureIndex, int x, int y) const {
    if (x < 0 || y < 0 || x >= 128 || y >= 128) return 0;
    size_t slotOffset = 1 + static_cast<size_t>(surfaceTextureIndex) * kZtxSlotSize;
    size_t offset = slotOffset + static_cast<size_t>(y) * 128 + static_cast<size_t>(x);
    if (offset >= ztxData_.size()) return 0;
    return ztxData_[offset];
}

uint16_t Zone::PaletteColor(uint8_t hueGroup, uint16_t lightLevel, uint8_t texel) const {
    // See zone.h's header comment: .zlu is 4 hue-families x 64 brightness
    // rungs x 512 bytes/rung (256 rungs total), not a per-texture table --
    // and hueGroup is a per-*tile* property (ZmpCell::flags bits 4-5, the
    // caller's job to resolve to the right tile -- see
    // render3d/zone_renderer.cpp), not a per-.sur-record one.
    constexpr int kRungSize = 512;
    constexpr int kRungsPerFamily = 64;
    constexpr int kMinRung = 4;   // RENDERER_3D.md's real [0x400, 0x3f00] clamp, >>8
    constexpr int kMaxRung = 63;

    hueGroup &= 0x3;
    int rung = std::clamp(static_cast<int>(lightLevel >> 8), kMinRung, kMaxRung);
    size_t offset = (static_cast<size_t>(hueGroup) * kRungsPerFamily + static_cast<size_t>(rung)) *
                         static_cast<size_t>(kRungSize) +
                     static_cast<size_t>(texel) * 2;
    if (offset + 1 >= zluData_.size()) return 0;
    return ReadU16(&zluData_[offset]);
}

bool Zone::CircleHitsWall(float worldX, float worldY, float radius) const {
    int minTx = static_cast<int>(std::floor((worldX - radius) / kTileScale));
    int maxTx = static_cast<int>(std::floor((worldX + radius) / kTileScale));
    int minTy = static_cast<int>(std::floor((worldY - radius) / kTileScale));
    int maxTy = static_cast<int>(std::floor((worldY + radius) / kTileScale));
    for (int ty = minTy; ty <= maxTy; ++ty) {
        for (int tx = minTx; tx <= maxTx; ++tx) {
            // Out-of-bounds tiles block movement too, matching the
            // renderer's neighborBlocks() treatment of the grid edge
            // (render3d/zone_renderer.cpp).
            if (!InBounds(tx, ty) || CellAt(tx, ty).IsWall()) {
                float closestX = std::clamp(worldX, tx * kTileScale, (tx + 1) * kTileScale);
                float closestY = std::clamp(worldY, ty * kTileScale, (ty + 1) * kTileScale);
                float dx = worldX - closestX, dy = worldY - closestY;
                if (dx * dx + dy * dy < radius * radius) return true;
            }
        }
    }
    return false;
}

namespace {

// Shared by FloorHeightAt/CeilingHeightAt: bilinear blend of a tile's 4
// stored corner values (0=NW,1=NE,2=SE,3=SW) at fractional position
// (u,v) within the tile, u/v in [0,1].
float BilinearCorner(const int16_t h[4], float u, float v) {
    return (1.0f - u) * (1.0f - v) * h[0] + u * (1.0f - v) * h[1] + u * v * h[2] +
           (1.0f - u) * v * h[3];
}

}  // namespace

float Zone::FloorHeightAt(float worldX, float worldY) const {
    float tx = worldX / kTileScale, ty = worldY / kTileScale;
    int itx = static_cast<int>(std::floor(tx)), ity = static_cast<int>(std::floor(ty));
    if (!InBounds(itx, ity)) return 0.0f;
    const ZcpEntry& t = TypeOf(CellAt(itx, ity));
    return BilinearCorner(t.floorHeight, tx - itx, ty - ity);
}

float Zone::CeilingHeightAt(float worldX, float worldY) const {
    float tx = worldX / kTileScale, ty = worldY / kTileScale;
    int itx = static_cast<int>(std::floor(tx)), ity = static_cast<int>(std::floor(ty));
    if (!InBounds(itx, ity)) return 0.0f;
    const ZcpEntry& t = TypeOf(CellAt(itx, ity));
    return BilinearCorner(t.ceilingHeight, tx - itx, ty - ity);
}

namespace {

constexpr int kLightAddPerCell = 0x40;  // Bullseye_PropagateLight's flat add per crossed cell

// Reproduces Bullseye_PropagateLight (docs/ZONE_FORMAT.md): a 2D ray-cast
// from a light-source cell in 256 directions, adding kLightAddPerCell to
// every cell each ray crosses (clamped, saturating at kMaxLightLevel),
// bouncing off the first wall it hits on each axis (sign-flipping that
// axis' step direction), and stopping once it has bounced on both axes or
// left the grid.
//
// SIMPLIFICATION: the original steps its rays using the same integer
// sin/cos LUT the rotation-matrix/automap code shares (RENDERER_3D.md) --
// not reproduced here bit-for-bit. This instead marches each ray in small
// fixed world-unit steps (kStepSize) and only credits a cell the first
// time the ray enters it, which visits the same sequence of cells a
// simple grid DDA would for reasonable step sizes -- close enough for a
// "torches spread warm light, walls block/bounce it" result without
// matching the original's exact per-ray footprint.
void PropagateLight(std::vector<ZmpCell>& cells, int width, int height, int startTx, int startTy) {
    constexpr int kRayCount = 256;
    constexpr float kStepSize = 48.0f;     // world units per march step (< 1 tile)
    constexpr float kMaxDistance = 20.0f * kTileScale;  // ray travel cap
    constexpr float kTwoPi = 6.28318530718f;

    auto inBounds = [&](int tx, int ty) { return tx >= 0 && ty >= 0 && tx < width && ty < height; };
    auto addLight = [&](int tx, int ty) {
        ZmpCell& c = cells[static_cast<size_t>(ty) * static_cast<size_t>(width) + static_cast<size_t>(tx)];
        int v = static_cast<int>(c.lightLevel) + kLightAddPerCell;
        c.lightLevel = static_cast<uint16_t>(std::min(v, static_cast<int>(kMaxLightLevel)));
    };

    float originX = startTx * kTileScale + kTileScale * 0.5f;
    float originY = startTy * kTileScale + kTileScale * 0.5f;

    for (int r = 0; r < kRayCount; ++r) {
        float angle = static_cast<float>(r) * (kTwoPi / static_cast<float>(kRayCount));
        float dx = std::cos(angle), dy = std::sin(angle);
        float x = originX, y = originY;
        int lastTx = startTx, lastTy = startTy;
        bool bouncedX = false, bouncedY = false;

        for (float traveled = 0.0f; traveled < kMaxDistance; traveled += kStepSize) {
            float stepDx = dx * kStepSize, stepDy = dy * kStepSize;
            x += stepDx;
            y += stepDy;
            int tx = static_cast<int>(std::floor(x / kTileScale));
            int ty = static_cast<int>(std::floor(y / kTileScale));
            if (!inBounds(tx, ty)) break;  // left the grid
            if (tx == lastTx && ty == lastTy) continue;

            const ZmpCell& cell = cells[static_cast<size_t>(ty) * static_cast<size_t>(width) +
                                         static_cast<size_t>(tx)];
            if (cell.IsWall()) {
                bool crossedX = tx != lastTx, crossedY = ty != lastTy;
                bool bounced = false;
                if (crossedX && !bouncedX) {
                    dx = -dx;
                    bouncedX = true;
                    bounced = true;
                }
                if (crossedY && !bouncedY) {
                    dy = -dy;
                    bouncedY = true;
                    bounced = true;
                }
                if (!bounced) break;  // both axes already bounced (or re-hit) -- stop this ray
                // Undo the step that walked into the wall cell (using the
                // pre-flip delta) so x/y land back at a safe position just
                // outside it -- otherwise x/y stay inside the wall cell and
                // the next iteration's tx/ty may not change enough to look
                // like a fresh crossing, causing spurious repeated "bounce"
                // detection against the same wall.
                x -= stepDx;
                y -= stepDy;
                continue;
            }
            addLight(tx, ty);
            lastTx = tx;
            lastTy = ty;
        }
    }
}

}  // namespace

void Zone::BakeLighting() {
    for (ZmpCell& c : cells_) c.lightLevel = 0;

    for (int ty = 0; ty < height_; ++ty) {
        for (int tx = 0; tx < width_; ++tx) {
            if (cells_[static_cast<size_t>(ty) * static_cast<size_t>(width_) + static_cast<size_t>(tx)]
                    .IsLightSource()) {
                PropagateLight(cells_, width_, height_, tx, ty);
            }
        }
    }

    for (ZmpCell& c : cells_) {
        const ZcpEntry& t = TypeOf(c);
        int v = static_cast<int>(c.lightLevel) + static_cast<int>(t.lightDelta) * 256;
        c.lightLevel = static_cast<uint16_t>(std::clamp(v, 0, static_cast<int>(kMaxLightLevel)));
    }
}

float LightLevelToBrightness(uint16_t lightLevel) {
    // Small ambient floor -- a deliberate port-only tweak, not part of the
    // original algorithm (which has none): a torch-lit dungeon with zero
    // ambient light anywhere unlit is authentic to the source data, but a
    // literal RGB(0,0,0) reads as a rendering bug more than "dark" on a
    // modern display. 0.12 keeps unlit geometry dim but still visible.
    constexpr float kAmbientFloor = 0.12f;
    float t = std::clamp(static_cast<float>(lightLevel) / static_cast<float>(kMaxLightLevel), 0.0f, 1.0f);
    return kAmbientFloor + (1.0f - kAmbientFloor) * t;
}

}  // namespace sk
