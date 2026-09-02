// M6 smoke test: loads a real zone's tile grid + wall/floor/ceiling
// material data (world/zone.h) and dumps enough of it to check by hand
// against the real files -- grid dimensions, the player start tile and
// its neighbors' wall/floor/ceiling surface indices, and a couple of
// decoded textures written out as PPM images (viewable directly) to
// confirm the .ztx/.zlu palette decoding actually produces a sane
// image, not just plausible-looking numbers.
//
// This session also turned this into a regression guard for the .zlu
// palette-selection fix (world/zone.h/.cpp's PaletteColor() comment):
// dumps a real wall texture at a bright rung and asserts it isn't
// near-black -- the exact symptom a real screenshot comparison caught
// (every wall rendering near-black/banded because the old texture-
// index-keyed guess picked .zlu's darkest rung for several real
// surfaces, regardless of the tile's actual light level).
#include <cstdio>
#include <fstream>
#include <string>

#include "world/zone.h"

namespace {

// `lightLevel` is the raw ZmpCell::lightLevel-range value PaletteColor()
// expects (see its header comment) -- pass sk::kMaxLightLevel for a
// "fully lit" reference dump. `hueGroup` is a real per-*tile* property
// now (ZmpCell::flags bits 4-5, not a `.sur` field -- see PaletteColor()'s
// comment), so these dumps use a fixed reference of 0 (family 0, the
// brown/tan family every real azra tile checked this session actually
// has) rather than trying to resolve the "real" tile for an arbitrary
// surIndex here.
void WriteTexturePpm(const sk::Zone& zone, int surIndex, const std::string& path,
                      uint16_t lightLevel, uint8_t hueGroup = 0) {
    uint8_t texIdx = zone.surfaceTextureIndex(surIndex);
    std::ofstream f(path, std::ios::binary);
    f << "P6\n128 128\n255\n";
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x) {
            uint8_t texel = zone.TexelAt(texIdx, x, y);
            uint16_t c = zone.PaletteColor(hueGroup, lightLevel, texel);
            // 4-bit-per-channel (docs/GRAPHICS_FORMAT.md): 0x0RGB, one
            // nibble unused. Confirmed against real palette data this
            // session (0xfff/white, 0xf0f/magenta chroma-key literal
            // present in every rung -- an RGB565 read produced garish
            // cyan/blue nonsense instead).
            uint8_t r = static_cast<uint8_t>(((c >> 8) & 0xf) * 17);
            uint8_t g = static_cast<uint8_t>(((c >> 4) & 0xf) * 17);
            uint8_t b = static_cast<uint8_t>((c & 0xf) * 17);
            f.put(static_cast<char>(r));
            f.put(static_cast<char>(g));
            f.put(static_cast<char>(b));
        }
    }
    std::printf("  wrote %s\n", path.c_str());
}

// Sum of RGB444 channel values across a whole 128x128 dump, skipping the
// 0x0f0f chroma-key entries -- a cheap "is this actually visibly lit, not
// near-black" check without needing to load the PPM back in.
uint64_t TextureBrightnessSum(const sk::Zone& zone, int surIndex, uint16_t lightLevel,
                               uint8_t hueGroup = 0) {
    uint8_t texIdx = zone.surfaceTextureIndex(surIndex);
    uint64_t sum = 0;
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x) {
            uint8_t texel = zone.TexelAt(texIdx, x, y);
            uint16_t c = zone.PaletteColor(hueGroup, lightLevel, texel);
            if (c == 0x0f0f) continue;
            sum += ((c >> 8) & 0xf) + ((c >> 4) & 0xf) + (c & 0xf);
        }
    }
    return sum;
}

}  // namespace

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";
    const char* zoneName = argc > 2 ? argv[2] : "azra";

    sk::Zone zone;
    if (!zone.Load(scriptRoot, zoneName)) {
        std::printf("zone_load_smoke: FAILED to load zone %s\n", zoneName);
        return 1;
    }

    int px = zone.playerStartX >> 8;
    int py = zone.playerStartY >> 8;
    std::printf("\nplayer start tile: (%d, %d)\n", px, py);

    const sk::ZmpCell& startCell = zone.CellAt(px, py);
    const sk::ZcpEntry& startType = zone.TypeOf(startCell);
    std::printf("start cell: flags=0x%02x zcpIndex=%u wall=%s\n", startCell.flags,
                startCell.zcpIndex, startCell.IsWall() ? "true" : "false");
    std::printf("start type: floorSurIndex=%u ceilingA=%u ceilingB=%u\n", startType.surIndexFloor,
                startType.surIndexCeilingA, startType.surIndexCeilingB);
    std::printf("  wall lo E/W/S/N: %u/%u/%u/%u\n", startType.surIndexE_lo, startType.surIndexW_lo,
                startType.surIndexS_lo, startType.surIndexN_lo);

    // Walk outward in a small radius, count walls, and dump the first
    // few walls' per-direction surface indices -- confirms the wall
    // lookups produce real, varied .sur indices rather than all-0xff.
    std::printf("\nscanning a 15-tile radius around the start for wall faces:\n");
    int wallCells = 0, facesFound = 0;
    for (int dy = -15; dy <= 15 && facesFound < 8; ++dy) {
        for (int dx = -15; dx <= 15 && facesFound < 8; ++dx) {
            int x = px + dx, y = py + dy;
            if (!zone.InBounds(x, y)) continue;
            const sk::ZmpCell& cell = zone.CellAt(x, y);
            if (!cell.IsWall()) continue;
            ++wallCells;
            const sk::ZcpEntry& t = zone.TypeOf(cell);
            bool any = t.surIndexE_lo != 0xff || t.surIndexW_lo != 0xff ||
                       t.surIndexS_lo != 0xff || t.surIndexN_lo != 0xff;
            if (any && facesFound < 8) {
                std::printf("  tile (%d,%d): E=%u W=%u S=%u N=%u floor=%u ceilA=%u\n", x, y,
                            t.surIndexE_lo, t.surIndexW_lo, t.surIndexS_lo, t.surIndexN_lo,
                            t.surIndexFloor, t.surIndexCeilingA);
                ++facesFound;
            }
        }
    }
    std::printf("wall cells in radius: %d\n", wallCells);

    // Dump the floor texture at the start tile and the start tile's own
    // east wall face (surIndexE_lo, per zone_renderer.cpp's convention
    // for an open tile's own edge index) as viewable PPMs, both at a
    // bright reference light level.
    bool ok = true;
    if (startType.surIndexFloor != 0xff) {
        WriteTexturePpm(zone, startType.surIndexFloor, "zone_floor_texture.ppm", sk::kMaxLightLevel);
    }
    if (startType.surIndexCeilingA != 0xff) {
        WriteTexturePpm(zone, startType.surIndexCeilingA, "zone_ceiling_texture.ppm",
                         sk::kMaxLightLevel);
    }
    if (startType.surIndexE_lo != 0xff) {
        std::printf("  wall surIndex=%u disabled=%s (byte6 bit5, this session's decompiled find)\n",
                    startType.surIndexE_lo,
                    zone.surfaceDisabled(startType.surIndexE_lo) ? "true" : "false");
        WriteTexturePpm(zone, startType.surIndexE_lo, "zone_wall_texture.ppm", sk::kMaxLightLevel);
        uint64_t sum = TextureBrightnessSum(zone, startType.surIndexE_lo, sk::kMaxLightLevel);
        std::printf("  wall surIndex=%u brightness sum (fully lit): %llu\n", startType.surIndexE_lo,
                    static_cast<unsigned long long>(sum));
        // A real, visibly-lit texture has thousands of nonzero 4-bit
        // channel values across 128x128 texels; a near-black dump (the
        // .zlu palette-selection bug this test now guards against) sums
        // to a few hundred at most.
        if (sum < 5000) {
            std::printf("  FAIL: wall texture is suspiciously dark at full brightness\n");
            ok = false;
        }
    }

    std::printf("\nzone_load_smoke: %s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
