#pragma once

// Loads the subset of a zone's per-zone files (docs/ZONE_FORMAT.md) needed
// to render its static tile-grid wall/floor/ceiling geometry: the
// TileGrid_RaycastVisibility + SurfaceFace_* pipeline documented in
// docs/RENDERER_3D.md's "The tile-grid wall/surface-face renderer"
// section. Explicitly out of scope for this first pass (see the port's
// M6 scope): the .zsk-baked whole-room mesh, actors/entities beyond the
// player start position, and the Bullseye lighting bake (.zfg fog LUT,
// per-cell light propagation) -- walls/floor/ceiling render unlit with
// their raw palette colors.

#include <cstdint>
#include <string>
#include <vector>

namespace sk {

// World units per tile (docs/WORLD_MODEL.md's Map_GetTileAt fixed-point
// convention -- 8.8-ish world coordinates, 256 units/tile).
constexpr float kTileScale = 256.0f;

// One .zcp entry, 36 bytes on disk (docs/ZONE_FORMAT.md's ZcpEntry).
// CORRECTION found and verified against real azra.zcp this session: the
// entry count is a u32 at offset 0, not the u8 the doc's first pass
// guessed from a partial decompile -- azra.zcp's decompressed size is
// exactly 4 + 36*11828 bytes, and its .zmp cells' zcpIndex values go up
// to 11827, both consistent only with a 4-byte count. Worth fixing in
// ZONE_FORMAT.md itself alongside this parser landing.
struct ZcpEntry {
    int8_t lightDelta = 0;
    int16_t ceilingBandThreshold = 0;
    int16_t floorHeight[4] = {0, 0, 0, 0};
    int16_t ceilingHeight[4] = {0, 0, 0, 0};
    uint8_t surIndexE_lo = 0xff, surIndexW_lo = 0xff, surIndexS_lo = 0xff, surIndexN_lo = 0xff;
    uint8_t surIndexE_hi = 0xff, surIndexW_hi = 0xff, surIndexS_hi = 0xff, surIndexN_hi = 0xff;
    uint8_t surIndexCeilingA = 0xff, surIndexCeilingB = 0xff, surIndexFloor = 0xff;
};

// One .zmp cell, 6 bytes on disk (docs/ZONE_FORMAT.md's ZmpCell).
struct ZmpCell {
    uint8_t flags = 0;  // bit0 light source, bit1 wall, bit3 force-draw, bit6 ceiling-band-select
    uint16_t lightLevel = 0;
    uint16_t zcpIndex = 0;

    bool IsWall() const { return (flags & 0x02) != 0; }
};

class Zone {
public:
    // scriptRoot/zoneName e.g. ".../system/apps/6r51", "azra".
    bool Load(const std::string& scriptRoot, const std::string& zoneName);

    int width() const { return width_; }
    int height() const { return height_; }
    bool InBounds(int tileX, int tileY) const {
        return tileX >= 0 && tileY >= 0 && tileX < width_ && tileY < height_;
    }
    const ZmpCell& CellAt(int tileX, int tileY) const;
    const ZcpEntry& TypeOf(const ZmpCell& cell) const;

    // Player start, 8.8 fixed-point world units (256 units/tile, per
    // docs/WORLD_MODEL.md's Map_GetTileAt).
    int32_t playerStartX = 0;
    int32_t playerStartY = 0;
    int32_t playerStartZ = 0;

    // One non-player-start .ent record (docs/ZONE_FORMAT.md's
    // EntPlacement, typeId > 1) -- resolving typeId to an actual model
    // is the caller's job (world/entity_types.h + world/model_archive.h),
    // matching the real engine's own two-step chain (entities.txt's
    // global type table, looked up per-placement).
    struct EntPlacement {
        int32_t x = 0, y = 0, z = 0;
        int32_t typeId = 0;
    };
    const std::vector<EntPlacement>& entities() const { return entities_; }

    // 128x128 8bpp-palettized texel at (x,y) within .ztx's texture slot
    // `surfaceTextureIndex` (0x4000 bytes = 128*128 -- confirmed: every
    // .ztx slot is exactly that size). Returns 0 if the index/coords are
    // out of range.
    uint8_t TexelAt(int surfaceTextureIndex, int x, int y) const;

    // Resolves a palettized texel to a real 16bpp-ish color.
    // SIMPLIFICATION (see zone.cpp): .zlu's exact per-face palette-chunk
    // selection wasn't fully traced (docs/RENDERER_3D.md's writeup
    // describes 4 *globally* fixed chunks stashed once at zone-load time,
    // which doesn't obviously square with the 131072-byte real file this
    // session found -- 64 candidate 2048-byte sets, not 1). This uses
    // `surfaceTextureIndex` to pick one of those sets (a reasonable, but
    // unverified, guess) and always chunk 0 within it -- good enough for
    // "real texture content, plausible color," not a byte-exact match.
    uint16_t PaletteColor(int surfaceTextureIndex, uint8_t texel) const;

    uint8_t surfaceTextureIndex(int surIndex) const;

    // Circle-vs-wall-tile collision test, in world units. SIMPLIFICATION:
    // this is an ordinary closest-point-on-square circle test against
    // every wall tile (and the grid boundary) the circle's bounding box
    // touches -- not a port of the real engine's actor bounding-radius
    // mechanics (docs/WORLD_MODEL.md notes these exist -- fixed-point
    // positions, per-tile-type collision behavior -- but they were never
    // traced to the byte level). Good enough to stop the player walking
    // through walls; not guaranteed to match the original's exact
    // wall-sliding feel.
    bool CircleHitsWall(float worldX, float worldY, float radius) const;

private:
    int width_ = 0, height_ = 0;
    std::vector<ZmpCell> cells_;
    std::vector<ZcpEntry> zcpEntries_;
    std::vector<uint8_t> surTextureIndex_;  // .sur's byte[7] per record -- see zone.cpp
    std::vector<uint8_t> ztxData_;          // 1 header byte + N*0x4000 texture slots
    std::vector<uint8_t> zluData_;          // N*2048-byte palette sets
    std::vector<EntPlacement> entities_;
};

}  // namespace sk
