#include "world/zone.h"

#include <cstdio>
#include <cstring>
#include <fstream>

#include "assets/zone_file.h"

namespace sk {

namespace {

constexpr int kZtxSlotSize = 0x4000;   // 128*128, 8bpp
constexpr int kZluSetSize = 2048;      // 4 * 512
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

    // --- .sur: u8 count + 8-byte records. Only byte[7] (the .ztx texture
    // slot index) is used here -- see zone.h's PaletteColor()/TexelAt()
    // comments on why the UV-shift/offset fields are approximated rather
    // than reproduced exactly for this first pass.
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
    for (uint8_t i = 0; i < surCount; ++i) {
        surTextureIndex_[i] = sur[1 + static_cast<size_t>(i) * 8 + 7];
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

    // --- .ent: find the player start record (typeId == 1) ---
    std::vector<uint8_t> ent;
    if (!ReadWholeFile(base + ".ent", ent) || ent.size() < 4) {
        std::printf("Zone: %s.ent missing/too short\n", zoneName.c_str());
        return false;
    }
    uint32_t entCount = ReadU32(&ent[0]);
    bool foundStart = false;
    for (uint32_t i = 0; i < entCount && 4 + (i + 1) * kEntRecordSize <= ent.size(); ++i) {
        const uint8_t* p = &ent[4 + static_cast<size_t>(i) * kEntRecordSize];
        int32_t typeId = ReadI32(p + 0x1c);
        if (typeId == 1) {
            playerStartX = ReadI32(p + 0x00);
            playerStartY = ReadI32(p + 0x04);
            playerStartZ = ReadI32(p + 0x08);
            foundStart = true;
            break;
        }
    }
    if (!foundStart) {
        std::printf("Zone: %s.ent has no player-start (typeId==1) record\n", zoneName.c_str());
        return false;
    }

    std::printf("Zone: loaded %s -- %dx%d tiles, %u zcp entries, %u surfaces, player start (%d,%d)\n",
                zoneName.c_str(), width_, height_, zcpCount, surCount, playerStartX >> 8,
                playerStartY >> 8);
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

uint8_t Zone::TexelAt(int surfaceTextureIndex, int x, int y) const {
    if (x < 0 || y < 0 || x >= 128 || y >= 128) return 0;
    size_t slotOffset = 1 + static_cast<size_t>(surfaceTextureIndex) * kZtxSlotSize;
    size_t offset = slotOffset + static_cast<size_t>(y) * 128 + static_cast<size_t>(x);
    if (offset >= ztxData_.size()) return 0;
    return ztxData_[offset];
}

uint16_t Zone::PaletteColor(int surfaceTextureIndex, uint8_t texel) const {
    size_t setCount = zluData_.size() / kZluSetSize;
    if (setCount == 0) return 0;
    size_t setIndex = static_cast<size_t>(surfaceTextureIndex) % setCount;
    // Always chunk 0 of the 4 -- see the PaletteColor() header comment.
    size_t offset = setIndex * kZluSetSize + static_cast<size_t>(texel) * 2;
    if (offset + 1 >= zluData_.size()) return 0;
    return ReadU16(&zluData_[offset]);
}

}  // namespace sk
