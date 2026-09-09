#include "world/zone.h"

#include <algorithm>
#include <cctype>
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
// M44: `.zon`'s room record -- four u16 then a 64-byte name.
// GameEngine_InitLevel reads it as `FUN_1009ec80(&rec, 0x48, 1, handle)`.
constexpr size_t kZonRecordSize = 0x48;

// The engine's own room-name comparisons are a mix of `strcmp` (the
// UnlockZone scan) and `strcasecmp` (a leftover in the EnterZone path),
// and the shipped names are inconsistently cased even within one zone
// (`azra.zon` has both "YouSure" and "start"). Matching case-insensitively
// is the tolerant reading, and matches how every other name lookup in this
// port already behaves.
bool EqualsIgnoreCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

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
        // M44: the second byte, which Lock/UnlockZone toggle. M67: and
        // which already arrives carrying every solid tile-stamped
        // placement's baked footprint -- see ZmpCell::blockFlags.
        cells_[i].blockFlags = p[1];
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
        e.floorBandThreshold = ReadI16(p + 2);
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
        e.cornerNudge = p[0x23];
    }

    // M9: replace the on-disk lightLevel (an unused editor leftover, see
    // zone.h's ZmpCell comment) with the real bake -- needs both cells_
    // and zcpEntries_, so this is the earliest point both are ready.
    BakeLighting();

    // --- .sur: u8 count + 8-byte records, ALL SIX fields now parsed and
    // used. Layout is decompiled ground truth (SurfaceFace_BuildAndProject,
    // 0x1005d784, shadowkey/extracted/decomp_1005d784.c -- read via
    // `iVar4 = surIndex*8 + tableBase` then `*(char*)(iVar4+N)`):
    // byte[0]/[1] = U/V bit-shift (signed char), byte[2..3]/[4..5] = two
    // signed-16-bit UV offsets in raw world units, byte[6] = flags,
    // byte[7] = texture index. Until this session only byte[6]/[7] were
    // used and the UV fields were approximated away entirely (a flat
    // 0..1 UV per quad) -- see zone.h's SurfaceUv() for the real formula
    // and why that approximation was the cause of visibly wrong/misaligned
    // wall textures.
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
    surfaces_.resize(surCount);
    for (uint8_t i = 0; i < surCount; ++i) {
        const uint8_t* rec = &sur[1 + static_cast<size_t>(i) * 8];
        SurfaceRecord& s = surfaces_[i];
        s.uShift = static_cast<int8_t>(rec[0]);
        s.vShift = static_cast<int8_t>(rec[1]);
        s.uOffset = ReadI16(rec + 2);
        s.vOffset = ReadI16(rec + 4);
        s.flags = rec[6];
        s.textureIndex = rec[7];
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

    // --- .zsk: the zone's skybox mesh (M70), see zone.h's SkyMesh()
    // comment -- an ordinary MODEL_FORMAT.md resource once decompressed,
    // with the engine's fixed 256x256 skin addressing. Not fatal if
    // missing/unparsed: the frame falls back to a flat background fill,
    // which is the real engine's own fallback (engine+0xbe0e).
    std::vector<uint8_t> zsk = LoadCompressedZoneFile(base + ".zsk");
    skyMeshValid_ = !zsk.empty() && ParseSkyboxResource(zsk.data(), zsk.size(), skyMesh_);
    if (!skyMeshValid_) {
        std::printf("Zone: %s.zsk missing or unparsed -- no skybox\n", zoneName.c_str());
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
            // M61: the same three orientation channels a placed entity
            // gets, in the same record order -- see playerStartYawRaw.
            playerStartRollRaw = ReadU16(p + 0x0c);
            playerStartPitchRaw = ReadU16(p + 0x10);
            playerStartYawRaw = ReadU16(p + 0x14);
            foundStart = true;
        } else if (typeId > 1) {
            // M35: the record tail is TWO NUL-terminated strings, not one
            // 40-byte name -- `char name[8]` at 0x20 and `char script[32]`
            // at 0x28. See EntPlacement::scriptPath for the evidence.
            // Reading the name as 40 bytes ran the two together for every
            // record whose name filled its field.
            auto readFixedString = [](const uint8_t* base, size_t cap) {
                const char* s = reinterpret_cast<const char*>(base);
                size_t len = 0;
                while (len < cap && s[len] != '\0') ++len;
                return std::string(s, len);
            };
            EntPlacement placement;
            placement.x = x;
            placement.y = y;
            placement.z = z;
            placement.typeId = typeId;
            placement.yawRaw = ReadU16(p + 0x14);  // see EntPlacement::yawRaw
            // M71: the other two orientation channels and the per-instance
            // model scale -- see EntPlacement::rotARaw.
            placement.rotARaw = ReadU16(p + 0x10);
            placement.rotBRaw = ReadU16(p + 0x0c);
            placement.scaleRaw = ReadU16(p + 0x18);
            placement.name = readFixedString(p + 0x20, 8);
            placement.scriptPath = readFixedString(p + 0x28, 32);
            entities_.push_back(std::move(placement));
        }
    }
    if (!foundStart) {
        std::printf("Zone: %s.ent has no player-start (typeId==1) record\n", zoneName.c_str());
        return false;
    }

    // --- M44: .zon, the named-region list. See zone.h's Region. ---
    //
    // Uncompressed (the same WholeFile_Load path as .sur/.pth/.ent), and
    // the loop below is GameEngine_InitLevel's own, including the Y flip:
    // fields b and d are stored as `gridHeight - y` and converted back
    // with the grid height that came out of the .zmp header a few dozen
    // lines above. The engine has room for 40 regions and reads exactly
    // `count` of them, so a zone with more would silently overrun; every
    // shipped zone is well under (crypt1's 31 is the largest).
    {
        std::vector<uint8_t> zon;
        if (ReadWholeFile(base + ".zon", zon) && zon.size() >= 2) {
            uint32_t regionCount = ReadU16(&zon[0]);
            if (zon.size() < 2 + static_cast<size_t>(regionCount) * kZonRecordSize) {
                std::printf("Zone: %s.zon too short for %u regions\n", zoneName.c_str(),
                            regionCount);
            } else {
                regions_.reserve(regionCount);
                for (uint32_t i = 0; i < regionCount; ++i) {
                    const uint8_t* p = &zon[2 + static_cast<size_t>(i) * kZonRecordSize];
                    const char* s = reinterpret_cast<const char*>(p + 8);
                    size_t len = 0;
                    while (len < 64 && s[len] != '\0') ++len;
                    Region r;
                    r.x0 = ReadU16(p + 0);
                    r.y1 = height_ - static_cast<int>(ReadU16(p + 2));  // b -> bottom edge
                    r.x1 = ReadU16(p + 4);
                    r.y0 = height_ - static_cast<int>(ReadU16(p + 6));  // d -> top edge
                    r.name.assign(s, len);
                    regions_.push_back(std::move(r));
                }
            }
        }
    }

    std::printf(
        "Zone: loaded %s -- %dx%d tiles, %u zcp entries, %u surfaces, player start (%d,%d), "
        "%zu placed entities, %zu named regions\n",
        zoneName.c_str(), width_, height_, zcpCount, surCount, playerStartX >> 8, playerStartY >> 8,
        entities_.size(), regions_.size());
    return true;
}

// ---- M44: the named-region API. See zone.h. ----

std::vector<const Zone::Region*> Zone::RegionsNamed(const std::string& name) const {
    std::vector<const Region*> out;
    for (const Region& r : regions_) {
        if (EqualsIgnoreCase(r.name, name)) out.push_back(&r);
    }
    return out;
}

std::vector<const Zone::Region*> Zone::RegionsAt(int tileX, int tileY) const {
    std::vector<const Region*> out;
    for (const Region& r : regions_) {
        if (r.Contains(tileX, tileY)) out.push_back(&r);
    }
    return out;
}

void Zone::SetBlockFlags(int tileX, int tileY, uint8_t value) {
    if (!InBounds(tileX, tileY)) return;
    size_t index = static_cast<size_t>(tileY) * static_cast<size_t>(width_) +
                    static_cast<size_t>(tileX);
    cells_[index].blockFlags = value;
    // FUN_1006d7bc: find an existing journal entry for this tile and
    // overwrite its saved value, else append -- capped at 100, past which
    // the real function silently drops the change.
    for (TileChange& c : tileChanges_) {
        if (c.x == static_cast<int16_t>(tileX) && c.y == static_cast<int16_t>(tileY)) {
            c.blockFlags = value;
            return;
        }
    }
    if (tileChanges_.size() >= kMaxTileChanges) return;
    tileChanges_.push_back({static_cast<int16_t>(tileX), static_cast<int16_t>(tileY), value});
}

void Zone::LightRect(int x0, int y0, int x1, int y1, int level) {
    // The real loop, in its own order: `for (y = y0; y < y1; ++y) for
    // (x = x0; x < x1; ++x) Map_GetTileAt(map, x << 8, y << 8)->light =
    // level << 8`. Half-open in both axes and **not** Y-flipped -- unlike
    // `.zon`'s stored rectangles, the script hands these in directly.
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            if (!InBounds(x, y)) continue;
            size_t index = static_cast<size_t>(y) * static_cast<size_t>(width_) +
                            static_cast<size_t>(x);
            cells_[index].lightLevel = static_cast<uint16_t>(level << 8);
        }
    }
}

int Zone::LockRegion(const std::string& name) {
    // FUN_10073470. The name-to-room map holds one room per name, so this
    // affects a single rectangle even when several share the name -- the
    // asymmetry with UnlockRegion below is the real engine's, not a
    // simplification. Taking the first match reproduces "whichever one the
    // map holds" as closely as an ordered list can.
    int changed = 0;
    for (const Region& r : regions_) {
        if (!EqualsIgnoreCase(r.name, name)) continue;
        for (int x = r.x0; x < r.x1; ++x) {
            for (int y = r.y0; y < r.y1; ++y) {
                // An assignment, not an OR: locking wipes whatever else
                // the byte held. Transcribed as written.
                SetBlockFlags(x, y, ZmpCell::kBlockSolid);
                ++changed;
            }
        }
        break;
    }
    return changed;
}

int Zone::StampEntityBox(int worldX, int worldY, int headingRaw, int halfExtentX, int halfExtentY,
                          uint8_t mask, bool set, bool journal) {
    // The real guard, shared by both functions: a zero extent on either
    // axis walks nothing. (FUN_10066204's other two conditions -- solid,
    // and not already passable -- belong to the caller, because the
    // *clearing* half deliberately does not test passability: a door has
    // already been marked passable by the time it clears its own stamp.)
    if (halfExtentX == 0 || halfExtentY == 0) return 0;

    // The engine reads its trig from a 2048-entry table indexed by
    // `angle >> 5`, and picks `sin(0x4000 - h)` and `sin(0x8000 - h)` --
    // i.e. cos(h) and sin(h). Written as the trig it is.
    const double radians = static_cast<double>(headingRaw & 0xffff) * (2.0 * 3.14159265358979323846)
                            / 65536.0;
    const double cosH = std::cos(radians);
    const double sinH = std::sin(radians);

    int written = 0;
    // Both loops are do-whiles stepping half a tile, so an extent smaller
    // than 128 still stamps its own centre row/column exactly once.
    for (int dy = -halfExtentY;; dy += 128) {
        for (int dx = -halfExtentX;; dx += 128) {
            // The rotation as the decompile composes it: the X offset
            // takes (sin, cos) and the Y offset (cos, sin), which is a
            // rotation by heading with the engine's own axis convention
            // baked in.
            const double offX = sinH * dx - cosH * dy;
            const double offY = cosH * dx + sinH * dy;
            int tx = static_cast<int>(std::floor((worldX + offX) / kTileScale));
            int ty = static_cast<int>(std::floor((worldY + offY) / kTileScale));
            tx = std::clamp(tx, 0, width_ - 1);
            ty = std::clamp(ty, 0, height_ - 1);
            if (width_ > 0 && height_ > 0) {
                size_t index = static_cast<size_t>(ty) * static_cast<size_t>(width_) +
                                static_cast<size_t>(tx);
                uint8_t before = cells_[index].blockFlags;
                uint8_t after = set ? static_cast<uint8_t>(before | mask)
                                    : static_cast<uint8_t>(before & ~mask);
                if (journal) {
                    // SetBlockFlags writes *and* journals, which is what
                    // the real `param_3 != 0` path does per cell.
                    SetBlockFlags(tx, ty, after);
                } else {
                    cells_[index].blockFlags = after;
                }
                if (after != before) ++written;
            }
            if (dx + 128 > halfExtentX) break;
        }
        if (dy + 128 > halfExtentY) break;
    }
    return written;
}

int Zone::UnlockRegion(const std::string& name) {
    // FUN_10073380: a linear walk of every room slot, so all regions
    // sharing the name unlock together.
    int changed = 0;
    for (const Region& r : regions_) {
        if (!EqualsIgnoreCase(r.name, name)) continue;
        for (int x = r.x0; x < r.x1; ++x) {
            for (int y = r.y0; y < r.y1; ++y) {
                if (!InBounds(x, y)) continue;
                size_t index = static_cast<size_t>(y) * static_cast<size_t>(width_) +
                                static_cast<size_t>(x);
                SetBlockFlags(x, y, static_cast<uint8_t>(cells_[index].blockFlags &
                                                           ~ZmpCell::kBlockSolid));
                ++changed;
            }
        }
    }
    return changed;
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

const SurfaceRecord& Zone::surface(int surIndex) const {
    // Matches SurfaceFace_BuildAndProject's own `engine+0x6918 == 0`
    // fallback: shift 5/5, zero offsets, no flags.
    static const SurfaceRecord kDefault{};
    if (surIndex < 0 || static_cast<size_t>(surIndex) >= surfaces_.size()) return kDefault;
    return surfaces_[static_cast<size_t>(surIndex)];
}

uint8_t Zone::surfaceTextureIndex(int surIndex) const { return surface(surIndex).textureIndex; }

bool Zone::surfaceDisabled(int surIndex) const { return surface(surIndex).disabled(); }

void Zone::SurfaceUv(const SurfaceRecord& sur, FaceOrient orient, float worldX, float worldY,
                     float worldZ, float* outU, float* outV) const {
    float uRaw = 0.0f;
    float vAxis = worldZ;
    switch (orient) {
        case FaceOrient::UPlusY:  uRaw = worldY + sur.uOffset; break;
        case FaceOrient::UMinusY: uRaw = sur.uOffset - worldY; break;
        case FaceOrient::UPlusX:  uRaw = worldX + sur.uOffset; break;
        case FaceOrient::UMinusX: uRaw = sur.uOffset - worldX; break;
        case FaceOrient::Ceiling:
        case FaceOrient::Floor:
            uRaw = worldX + sur.uOffset;
            vAxis = worldY;
            break;
    }
    float vRaw = vAxis + sur.vOffset;
    // `<< shift` on the raw world value, then the consumer's `>> 8` to
    // reach texels -- i.e. a net scale of 2^(shift-8). See zone.h.
    float u = std::ldexp(uRaw, sur.uShift - 8);
    float v = std::ldexp(vRaw, sur.vShift - 8);
    if (sur.flipU()) u = -u;
    if (sur.flipV()) v = -v;
    *outU = u;
    *outV = v;
}

void Zone::ApplyCornerNudge(float* worldX, float* worldY) const {
    int tx = static_cast<int>(std::floor(*worldX / kTileScale));
    int ty = static_cast<int>(std::floor(*worldY / kTileScale));
    if (!InBounds(tx, ty)) return;
    // The real engine indexes the grid with a plain `pos >> 8` on the
    // vertex's own raw world coordinate, exactly as above.
    constexpr float kHalfTile = 128.0f;  // 0x80, the real constant
    switch (TypeOf(CellAt(tx, ty)).cornerNudge) {
        case 1: *worldX += kHalfTile; *worldY += kHalfTile; break;
        case 2: *worldX -= kHalfTile; *worldY += kHalfTile; break;
        case 3: *worldX -= kHalfTile; *worldY -= kHalfTile; break;
        case 4: *worldX += kHalfTile; *worldY -= kHalfTile; break;
        case 5: *worldX += kHalfTile; break;
        case 6: *worldX -= kHalfTile; break;
        case 7: *worldY += kHalfTile; break;
        case 8: *worldY -= kHalfTile; break;
        default: break;
    }
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
            //
            // M44: a tile whose second byte carries the block bit stops
            // you as well -- that is what `UnlockZone("swdoor")` lifts
            // after a key is used or a lever pulled, on regions that are
            // barriers until then. It is kept out of the *sightline* test
            // below deliberately: the engine's blocking test is a separate
            // virtual from the one the renderer and the visibility raycast
            // use, and nothing about this byte says it stops light or line
            // of sight.
            //
            // M67: this test was right and its *input* was wrong. The bit
            // is also where a closed door's own footprint lives (see
            // ZmpCell::blockFlags and StampEntityBox), and nothing in this
            // port ever cleared it -- so every door in the game stayed a
            // wall after it opened. Doors clear it through SetPassable now.
            if (!InBounds(tx, ty) || CellAt(tx, ty).IsWall() || CellAt(tx, ty).IsBlocked()) {
                float closestX = std::clamp(worldX, tx * kTileScale, (tx + 1) * kTileScale);
                float closestY = std::clamp(worldY, ty * kTileScale, (ty + 1) * kTileScale);
                float dx = worldX - closestX, dy = worldY - closestY;
                if (dx * dx + dy * dy < radius * radius) return true;
            }
        }
    }
    return false;
}

bool Zone::HasLineOfSight(float x0, float y0, float x1, float y1) const {
    // Amanatides-Woo grid traversal: visits exactly the tiles the segment
    // crosses, no more, so a sightline that only clips a wall's corner
    // isn't wrongly reported as blocked (and none is ever skipped, which a
    // fixed-step march can do at glancing angles).
    int tx = static_cast<int>(std::floor(x0 / kTileScale));
    int ty = static_cast<int>(std::floor(y0 / kTileScale));
    const int endTx = static_cast<int>(std::floor(x1 / kTileScale));
    const int endTy = static_cast<int>(std::floor(y1 / kTileScale));
    if (!InBounds(tx, ty) || !InBounds(endTx, endTy)) return false;

    float dx = x1 - x0, dy = y1 - y0;
    int stepX = dx > 0 ? 1 : (dx < 0 ? -1 : 0);
    int stepY = dy > 0 ? 1 : (dy < 0 ? -1 : 0);
    // Parametric distance (in [0,1] along the segment) to the next tile
    // boundary on each axis, and the increment per whole tile crossed.
    constexpr float kInf = 1e30f;
    float tMaxX = kInf, tDeltaX = kInf;
    if (stepX != 0) {
        float nextBoundary = (stepX > 0 ? (tx + 1) : tx) * kTileScale;
        tMaxX = (nextBoundary - x0) / dx;
        tDeltaX = kTileScale / std::fabs(dx);
    }
    float tMaxY = kInf, tDeltaY = kInf;
    if (stepY != 0) {
        float nextBoundary = (stepY > 0 ? (ty + 1) : ty) * kTileScale;
        tMaxY = (nextBoundary - y0) / dy;
        tDeltaY = kTileScale / std::fabs(dy);
    }

    // Bounded so a degenerate input can never spin: the longest possible
    // traversal of a w*h grid crosses at most w+h tiles.
    const int maxSteps = width_ + height_ + 2;
    for (int i = 0; i < maxSteps; ++i) {
        if (tx == endTx && ty == endTy) return true;
        if (tMaxX < tMaxY) {
            tx += stepX;
            tMaxX += tDeltaX;
        } else {
            ty += stepY;
            tMaxY += tDeltaY;
        }
        if (!InBounds(tx, ty)) return false;
        if (CellAt(tx, ty).IsWall()) return false;
    }
    return false;
}

namespace {

// Shared by FloorHeightAt/CeilingHeightAt: bilinear blend of a tile's 4
// stored corner values at fractional position (u,v) within the tile,
// u/v in [0,1] measured from the tile's (x0,y0) corner.
//
// CORRECTED this session against the decompile (see
// render3d/zone_renderer.cpp's kCornerDx/kCornerDy): the real corner
// order is 0=(x0,y1), 1=(x1,y1), 2=(x1,y0), 3=(x0,y0) -- the exact
// reverse of the 0=NW,1=NE,2=SE,3=SW this previously assumed. The old
// order mirrored every sloped tile about its diagonal, both in the
// renderer and here, where it decides the ground height the player
// actually stands on.
float BilinearCorner(const int16_t h[4], float u, float v) {
    return (1.0f - u) * (1.0f - v) * h[3] + u * (1.0f - v) * h[2] + u * v * h[1] +
           (1.0f - u) * v * h[0];
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

float Zone::SnapSpawnedObjectToGround(float worldX, float worldY) const {
    // M81 -- see the header. The Level dispatcher's case 0x21 writes the
    // tile's own authored floor-band threshold into the object's z and
    // then runs FUN_100686e0 on it, with no trailing lift.
    const float tx = worldX / kTileScale, ty = worldY / kTileScale;
    const int itx = static_cast<int>(std::floor(tx)), ity = static_cast<int>(std::floor(ty));
    if (!InBounds(itx, ity)) return 0.0f;
    const float probe = static_cast<float>(TypeOf(CellAt(itx, ity)).floorBandThreshold);
    return CollisionFloorHeightAt(worldX, worldY, probe + 384.0f);
}

float Zone::CollisionFloorHeightAt(float worldX, float worldY, float worldZ) const {
    float tx = worldX / kTileScale, ty = worldY / kTileScale;
    int itx = static_cast<int>(std::floor(tx)), ity = static_cast<int>(std::floor(ty));
    if (!InBounds(itx, ity)) return 0.0f;
    const ZmpCell& cell = CellAt(itx, ity);
    const ZcpEntry& t = TypeOf(cell);
    const float u = tx - itx, v = ty - ity;
    // FUN_1001bd50 / FUN_1001bcac: the corner arrays only when the cell says
    // so, otherwise the flat authored band value.
    const float floorHeight = (cell.flags & 0x04) ? BilinearCorner(t.floorHeight, u, v)
                                                  : static_cast<float>(t.floorBandThreshold);
    const float ceilingHeight = (cell.flags & 0x40)
                                    ? BilinearCorner(t.ceilingHeight, u, v)
                                    : static_cast<float>(t.ceilingBandThreshold);
    // FUN_1001beac's upper-storey branch. `bVar1` in the decompile is "this
    // tile's ceiling surface exists and is not disabled", i.e. there is
    // something solid to stand on up there.
    const bool solidCeiling =
        t.surIndexCeilingA != 0xff && !surface(t.surIndexCeilingA).disabled();
    if ((cell.blockFlags & 0x02) && solidCeiling && worldZ >= ceilingHeight) return ceilingHeight;
    return floorHeight;
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


// M41: the tier table, in the real branch order.
Zone::VisibilityTier Zone::TierFor(int zoomScale) {
    VisibilityTier tier;
    if (zoomScale < 0x201) {
        if (zoomScale < 0x101) {
            tier.maxSteps = 0x19;
            tier.rayCount = 0x96;
            tier.angleStep = 0xaa;
        } else {
            tier.rayCount = 0xb1;
            tier.maxSteps = 0x5d;
            tier.angleStep = 0x60;
        }
    } else {
        tier.rayCount = 0xb2;
        tier.maxSteps = 0xac;
        tier.angleStep = 0x34;
    }
    return tier;
}

// M41: TileGrid_RaycastVisibility (FUN_1000f694), transcribed. See
// zone.h's declaration for the three details that matter and for the
// zoom-vs-quality correction.
std::vector<std::pair<int, int>> Zone::RaycastVisibleTiles(float cameraWorldX, float cameraWorldY,
                                                            float cameraYawRadians,
                                                            int zoomScale) const {
    std::vector<std::pair<int, int>> visible;
    if (width_ <= 0 || height_ <= 0) return visible;

    const VisibilityTier tier = TierFor(zoomScale);
    const int rayCount = tier.rayCount;
    const int maxSteps = tier.maxSteps;
    const int angleStep = tier.angleStep;

    // The real angle unit is 1/65536 of a turn; the sin/cos table is
    // indexed by `(angle >> 5) & 0x7ff`, i.e. 2048 entries per turn, with
    // an amplitude of 0x200 so that `value >> 1` is a unit step of exactly
    // one tile (0x100) in the 8.8 world coordinates below. Reproduced in
    // floating point here -- this port's renderer is float throughout (see
    // zone_renderer.h) and the fixed-point table would buy nothing but a
    // rounding difference in which tiles land on a ray's edge.
    constexpr double kTwoPi = 6.283185307179586;
    const double angleUnit = kTwoPi / 65536.0;
    // `startAngle = (rayCount >> 1) * angleStep + cameraYaw`, then one
    // `angleStep` subtracted per ray -- so the fan is centred on the
    // camera's heading.
    double angle = cameraYawRadians + static_cast<double>((rayCount >> 1) * angleStep) * angleUnit;

    // The real code clamps the ray origin into [0x100, (grid-2)*0x100]
    // before offsetting it, so a camera standing on the very edge of the
    // grid still produces rays that start inside it.
    auto clampStart = [](float v, int extent) {
        int fixed = static_cast<int>(v);
        int hi = (extent - 2) * 256;
        if (fixed < 256) fixed = 256;
        if (fixed > hi) fixed = hi;
        return static_cast<double>(fixed);
    };
    const double startX = clampStart(cameraWorldX, width_);
    const double startY = clampStart(cameraWorldY, height_);

    struct Ray {
        double x = 0, y = 0, dx = 0, dy = 0;
        int step = 0;
        bool done = false;
    };
    std::vector<Ray> rays(static_cast<size_t>(rayCount));
    for (int i = rayCount - 1; i >= 0; --i) {
        Ray& r = rays[static_cast<size_t>(i)];
        // The real code takes sin at `angle` for dx and cos (the table
        // read 0x4000 further along, i.e. a quarter turn) for dy.
        // The port's camera basis is forward = (cos yaw, sin yaw) (see
        // render3d/zone_renderer.cpp); the real engine's table read is
        // sin for one axis and cos (a quarter turn along) for the other.
        // Matched to this port's convention so the fan actually points
        // where the camera does.
        r.dx = std::cos(angle) * 256.0;
        r.dy = std::sin(angle) * 256.0;
        // Four steps *behind* the camera -- this is what puts the tiles
        // under and just behind the player into the set.
        r.x = startX - r.dx * 4.0;
        r.y = startY - r.dy * 4.0;
        angle -= static_cast<double>(angleStep) * angleUnit;
    }

    // A per-call stamp standing in for the real per-cell frame byte
    // (tile[6] against engine+0x478) -- same job, without mutating the
    // loaded zone.
    std::vector<uint8_t> seen(static_cast<size_t>(width_) * static_cast<size_t>(height_), 0);

    int active = rayCount;
    // The real outer loop runs while any ray is live and fewer than 0x19c
    // tiles have been collected; the inner append hard-breaks at 0x200.
    while (active != 0 && visible.size() < 0x19c) {
        for (Ray& r : rays) {
            if (r.done) continue;
            const int fx = static_cast<int>(r.x);
            const int fy = static_cast<int>(r.y);
            // The real bounds test is on the fixed-point coordinates, one
            // tile in from each edge, and a failure means "no tile here"
            // rather than "stop".
            const bool inside = fx >= 0x101 && fx < (width_ - 1) * 256 && fy >= 0x101 &&
                                 fy < (height_ - 1) * 256;
            const int tx = fx >> 8;
            const int ty = fy >> 8;
            if (!inside || !InBounds(tx, ty)) {
                // Outside the grid: keep marching for the first five
                // steps, then give up on this ray.
                if (r.step < 5) {
                    r.x += r.dx;
                    r.y += r.dy;
                    ++r.step;
                } else {
                    r.done = true;
                    --active;
                }
                continue;
            }

            const ZmpCell& cell = CellAt(tx, ty);
            // `(flags & 0b1010) == 0b0010` -- a wall stops the ray, but
            // only from the sixth step on, and only when bit3
            // (force-draw) is clear.
            if (r.step > 4 && (cell.flags & 0x0a) == 0x02) {
                r.done = true;
                --active;
                continue;
            }

            uint8_t& stamp = seen[static_cast<size_t>(ty) * static_cast<size_t>(width_) +
                                   static_cast<size_t>(tx)];
            if (!stamp) {
                stamp = 1;
                visible.emplace_back(tx, ty);
                if (visible.size() > 0x1ff) break;
            }
            r.x += r.dx;
            r.y += r.dy;
            ++r.step;
            if (r.step >= maxSteps) {
                r.done = true;
                --active;
            }
        }
    }
    return visible;
}

}  // namespace sk
