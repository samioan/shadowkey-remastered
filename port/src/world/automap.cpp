#include "world/automap.h"

#include <cmath>

namespace sk {
namespace {

// This port's Backbuffer stores RGB565 (graphics/backbuffer.h), and every
// asset path already converts on the way in -- sprite_archive.cpp's
// Rgb444ToRgb565 does exactly this for `global.spr` texels. The map's
// colours are engine literals in the same 0x0RGB space, so they take the
// same route rather than being written raw.
//
// The one difference worth recording: the engine's own text path
// (`FUN_1008f97c`) expands a nibble as `n << 4`, while this port uses
// `n * 17`, which is the more correct expansion and what every other
// asset here already went through. The gap is at most one LSB per
// channel and staying consistent with the rest of the port matters more.
uint16_t Rgb444(uint16_t v) {
    uint8_t r = static_cast<uint8_t>(((v >> 8) & 0xF) * 17);
    uint8_t g = static_cast<uint8_t>(((v >> 4) & 0xF) * 17);
    uint8_t b = static_cast<uint8_t>((v & 0xF) * 17);
    return PackRGB565(r, g, b);
}

}  // namespace

uint16_t AutomapColorRgb444(AutomapTile tile) {
    switch (tile) {
        case AutomapTile::Blocked: return kAutomapBlockedRgb444;
        case AutomapTile::Flat: return kAutomapFlatRgb444;
        case AutomapTile::Step: return kAutomapStepRgb444;
        case AutomapTile::Unknown: break;
    }
    return kAutomapUnknownRgb444;
}

void ExploredTiles::Reset(int width, int height) {
    width_ = width > 0 ? width : 0;
    height_ = height > 0 ? height : 0;
    // `param_2 * param_3 >> 3` -- the original's own truncating size.
    bits_.assign(static_cast<size_t>((width_ * height_) >> 3), 0);
}

void ExploredTiles::Mark(int tileX, int tileY) {
    if (tileX < 0 || tileY < 0 || tileX >= width_ || tileY >= height_) return;
    size_t index = static_cast<size_t>(strideBytes()) * static_cast<size_t>(tileY) +
                    static_cast<size_t>(tileX >> 3);
    if (index >= bits_.size()) return;
    bits_[index] |= static_cast<uint8_t>(1 << (tileX & 7));
}

bool ExploredTiles::IsExplored(int tileX, int tileY) const {
    if (tileX < 0 || tileY < 0 || tileX >= width_ || tileY >= height_) return false;
    size_t index = static_cast<size_t>(strideBytes()) * static_cast<size_t>(tileY) +
                    static_cast<size_t>(tileX >> 3);
    if (index >= bits_.size()) return false;
    return (bits_[index] >> (tileX & 7)) & 1;
}

void ExploredTiles::MarkVisible(const std::vector<std::pair<int, int>>& tiles) {
    for (const std::pair<int, int>& tile : tiles) Mark(tile.first, tile.second);
}

int ExploredTiles::exploredCount() const {
    int total = 0;
    for (uint8_t byte : bits_) {
        for (int bit = 0; bit < 8; ++bit) {
            if ((byte >> bit) & 1) ++total;
        }
    }
    return total;
}

AutomapTile ClassifyAutomapTile(const Zone& zone, const ExploredTiles& explored, int tileX,
                                 int tileY) {
    // The bounds test comes first and is the *engine's*, which checks the
    // signs separately from the extents.
    if (tileX < 0 || tileY < 0 || tileX >= zone.width() || tileY >= zone.height()) {
        return AutomapTile::Unknown;
    }
    if (!explored.IsExplored(tileX, tileY)) return AutomapTile::Unknown;

    const ZmpCell& cell = zone.CellAt(tileX, tileY);
    if ((cell.flags & kAutomapBlockedFlagsBits) != 0 ||
        (cell.blockFlags & kAutomapBlockedBlockBits) != 0) {
        return AutomapTile::Blocked;
    }

    // The height comparison is against the tile diagonally *ahead* --
    // `Map_GetTileAt(engine, (x+1)<<8, (y+1)<<8)` -- not a cardinal
    // neighbour, and the value is the .zcp entry's signed 16-bit at byte
    // 2, which world/zone.h already parses as floorBandThreshold. The
    // engine does no bounds check on this second lookup at all; clamping
    // is this port's, and only bites on the two outer edges.
    int nx = tileX + 1 < zone.width() ? tileX + 1 : tileX;
    int ny = tileY + 1 < zone.height() ? tileY + 1 : tileY;
    int here = zone.TypeOf(cell).floorBandThreshold;
    int ahead = zone.TypeOf(zone.CellAt(nx, ny)).floorBandThreshold;
    int delta = here - ahead;
    if (delta < 0) delta = -delta;
    return delta < 1 ? AutomapTile::Flat : AutomapTile::Step;
}

int AutomapSine(int index) {
    // Every one of the real table's 2048 entries equals this expression
    // exactly, verified against the shipped image.
    constexpr double kTwoPi = 6.283185307179586;
    int i = index & 0x7ff;
    return static_cast<int>(std::lround(256.0 * std::sin(kTwoPi * i / 2048.0)));
}

void BuildAutomapMarker(int headingUnits, AutomapRay out[3]) {
    // `(-0x8000 - heading)` truncated to 16 bits, then `>> 5` to index the
    // 2048-entry table (0x10000 / 32 == 2048).
    int base = static_cast<int16_t>(-0x8000 - (headingUnits & 0xffff));
    auto sinAt = [&](int offset, int shift) {
        return AutomapSine((base + offset) >> 5) >> shift;
    };
    // Long leg: radius `sin >> 5`, at the base angle. The table's
    // amplitude is 0x100, so this reaches 8 pixels.
    out[0] = {kAutomapCenterX, kAutomapCenterY, kAutomapCenterX - sinAt(0, 5),
              kAutomapCenterY - sinAt(0x4000, 5)};
    // Two short legs at +/- 45 degrees, radius `sin >> 6` (4 pixels). The
    // engine shares one subexpression between them -- sin(a + 45) is the
    // first one's X and the second one's Y -- which is what makes their
    // arithmetic look asymmetric in the decompile.
    out[1] = {kAutomapCenterX, kAutomapCenterY, kAutomapCenterX - sinAt(0x2000, 6),
              kAutomapCenterY - sinAt(0x6000, 6)};
    out[2] = {kAutomapCenterX, kAutomapCenterY, kAutomapCenterX - sinAt(-0x2000, 6),
              kAutomapCenterY - sinAt(0x2000, 6)};
}

namespace {

// `FUN_1005e310`: a DDA that takes `max(|dx|, |dy|)` whole-pixel steps,
// stepping both axes by a fixed 8.8 increment. Reproduced in the same
// shape (integer steps, both axes advanced every step) rather than as a
// Bresenham, because the pixel set differs slightly between the two and
// this one is what the original draws.
void DrawAutomapLine(Backbuffer& backbuffer, const AutomapRay& ray, uint16_t color) {
    int dx = (ray.x1 - ray.x0) << 8;
    int dy = (ray.y1 - ray.y0) << 8;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    int steps = (ady < adx ? adx : ady) >> 8;
    if (steps == 0) return;
    int stepX = dx / steps;
    int stepY = dy / steps;
    int x = ray.x0 << 8;
    int y = ray.y0 << 8;
    for (int i = 0; i < steps; ++i) {
        // The original clips only the bottom edge (`y >> 8 <= 0xd0`) and
        // trusts X; SetPixel clips everything, which cannot change the
        // result for a marker anchored at the centre of the screen.
        backbuffer.SetPixel(x >> 8, y >> 8, color);
        x += stepX;
        y += stepY;
    }
}

}  // namespace

void RenderAutomap(Backbuffer& backbuffer, const Zone& zone, const ExploredTiles& explored,
                    int playerTileX, int playerTileY, int headingUnits) {
    // World Y counts **down** from `py + 32` while the screen row counts
    // up, so larger world Y is nearer the top of the map.
    int row = kAutomapOriginY;
    for (int ty = playerTileY + kAutomapRadiusTiles; ty > playerTileY - kAutomapRadiusTiles; --ty) {
        int col = kAutomapOriginX;
        for (int tx = playerTileX - kAutomapRadiusTiles; tx < playerTileX + kAutomapRadiusTiles;
             ++tx) {
            uint16_t color = Rgb444(AutomapColorRgb444(ClassifyAutomapTile(zone, explored, tx, ty)));
            backbuffer.SetPixel(col, row, color);
            backbuffer.SetPixel(col + 1, row, color);
            backbuffer.SetPixel(col, row + 1, color);
            backbuffer.SetPixel(col + 1, row + 1, color);
            col += kAutomapPixelsPerTile;
        }
        row += kAutomapPixelsPerTile;
    }

    AutomapRay marker[3];
    BuildAutomapMarker(headingUnits, marker);
    for (const AutomapRay& ray : marker) {
        DrawAutomapLine(backbuffer, ray, Rgb444(kAutomapMarkerRgb444));
    }
}

}  // namespace sk
