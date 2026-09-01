// M6 smoke test: loads a real zone's tile grid + wall/floor/ceiling
// material data (world/zone.h) and dumps enough of it to check by hand
// against the real files -- grid dimensions, the player start tile and
// its neighbors' wall/floor/ceiling surface indices, and a couple of
// decoded textures written out as PPM images (viewable directly) to
// confirm the .ztx/.zlu palette decoding actually produces a sane
// image, not just plausible-looking numbers.
#include <cstdio>
#include <fstream>
#include <string>

#include "world/zone.h"

namespace {

void WriteTexturePpm(const sk::Zone& zone, int surfaceTextureIndex, const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    f << "P6\n128 128\n255\n";
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x) {
            uint8_t texel = zone.TexelAt(surfaceTextureIndex, x, y);
            uint16_t c = zone.PaletteColor(surfaceTextureIndex, texel);
            // 4-bit-per-channel (docs/GRAPHICS_FORMAT.md): 0x0RGB, one
            // nibble unused. Confirmed against real palette data this
            // session (0xfff/white, 0xf0f/magenta chroma-key literal
            // present in every chunk -- an RGB565 read produced garish
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

const char* DirName(int i) {
    switch (i) {
        case 0: return "E";
        case 1: return "W";
        case 2: return "S";
        case 3: return "N";
    }
    return "?";
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

    // Dump the floor texture at the start tile (if it has one) and the
    // first found wall's east-face texture as viewable PPMs.
    if (startType.surIndexFloor != 0xff) {
        uint8_t texIdx = zone.surfaceTextureIndex(startType.surIndexFloor);
        WriteTexturePpm(zone, texIdx, "zone_floor_texture.ppm");
    }
    if (startType.surIndexCeilingA != 0xff) {
        uint8_t texIdx = zone.surfaceTextureIndex(startType.surIndexCeilingA);
        WriteTexturePpm(zone, texIdx, "zone_ceiling_texture.ppm");
    }
    // Also just dump raw texture slot 0 unconditionally, as a baseline.
    WriteTexturePpm(zone, 0, "zone_texture_slot0.ppm");

    std::printf("\nzone_load_smoke: OK\n");
    return 0;
}
