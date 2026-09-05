#pragma once

// M57: the in-game map, recovered from `FUN_1002b430`.
//
// It is **not a screen** -- there is no screen mode for it, no `.s` script
// and no menu. It is a HUD overlay drawn straight over the 3D view, gated
// on one boolean on the player object (`player+0x3a4`) that a single
// three-line function (`FUN_1001ee50`) toggles:
//
//     *(bool *)(player + 0x3a4) = *(char *)(player + 0x3a4) != 1;
//
// and that toggle has exactly one caller -- the player's per-frame input
// poll `FUN_1001c9c0`, on the edge of logical action **9**:
//
//     if (GetBoundButton(input, 9) && !GetBoundButtonPrev(input, 9))
//         FUN_1001ee50(player);
//
// Action 9 is `"Map Toggle"` (string 0xd04), bound by default to Key 9 --
// see docs/INPUT_HANDLING.md, which now carries the real action-index
// column this milestone recovered from `FUN_1001a220`.
//
// The drawing is a 64x64-tile window centred on the player's tile, blown
// up to 2x2 pixels per tile at screen (24, 40), so 128x128 pixels on the
// 176x208 screen. Larger world Y is *up* on screen: the tile loop counts
// world Y **down** from `py + 32` while the screen row counts up.
//
// What makes it a map rather than a minimap is that it is masked by a
// persistent "explored" bitmap -- see ExploredTiles below.

#include <cstdint>
#include <vector>

#include "graphics/backbuffer.h"
#include "world/zone.h"

namespace sk {

// ---- Screen geometry, all verbatim from the decompile ----
inline constexpr int kAutomapOriginX = 24;        // 0x18
inline constexpr int kAutomapOriginY = 40;        // 0x28
inline constexpr int kAutomapRadiusTiles = 32;    // 0x20, the loop bound
inline constexpr int kAutomapTilesAcross = 64;
inline constexpr int kAutomapPixelsPerTile = 2;
inline constexpr int kAutomapSizePixels = 128;
// The marker origin the three player-arrow lines share, 0x5800/0x6800 in
// the line drawer's 8.8 fixed point. Note this is the centre of the
// *drawn area*, which is where the player's own tile lands.
inline constexpr int kAutomapCenterX = 88;   // 0x58
inline constexpr int kAutomapCenterY = 104;  // 0x68
// The zone-name caption, drawn before the grid.
inline constexpr int kAutomapCaptionY = 10;

// The full-screen backdrop blitted first, when the slot has a sprite.
// `engine + 0x44b0`, and the sprite-slot array base is `engine + 0x4460`
// (docs/RENDER_LOOP.md pinned that down in M54 via slots 205/206), so
// (0x44b0 - 0x4460) / 4 = 20. Requested by every zone's own
// `<zone>_sprites.txt`.
inline constexpr int kAutomapBackdropSlot = 20;

// ---- Colours ----
//
// These are the engine's own literals, and they are the reason this port's
// long-open "what is the exact 16-bit pixel format" question is now
// settled: the map writes them **straight into the framebuffer**, and they
// only make sense as 0x0RGB 4-bit-per-channel (Symbian's EColor4K). Read
// as RGB565, 0x0ca5 would be a dark green rather than the tan it plainly
// is. See docs/GRAPHICS_FORMAT.md.
//
// Each is the literal the switch assigns plus the `+ 5` every in-bounds
// arm adds afterwards, folded in.
inline constexpr uint16_t kAutomapUnknownRgb444 = 0x0ca5;  // 0xca0 + 5, and the
                                                            // out-of-bounds default
                                                            // is 0xca5 outright
inline constexpr uint16_t kAutomapBlockedRgb444 = 0x0868;  // 0x863 + 5
inline constexpr uint16_t kAutomapFlatRgb444 = 0x0db5;     // 0xdb0 + 5
inline constexpr uint16_t kAutomapStepRgb444 = 0x0a85;     // 0xa80 + 5
inline constexpr uint16_t kAutomapMarkerRgb444 = 0x0fff;   // white

// The mask the "draw this as solid" test uses, applied to the cell's
// first **two** bytes read as one u16 -- so bit 1 is ZmpCell::flags bit 1
// (wall) and bits 10 and 13 are ZmpCell::blockFlags bits 2 and 5. It is
// the only use of the literal 0x2402 in the whole image.
//
// Both blockFlags bits are richly authored on disk, which was worth
// checking rather than assuming (the smoke test counts them across all
// 21 zones and would fail if this stopped being true):
//
//   * **bit 2** -- 40,517 cells, every zone, and 36,332 of those are
//     *not* walls. This is the same bit M44 found `LockZone` assigning
//     and M55 found the entity tile-stamp ORing, so authored blocking,
//     a locked gate and a table too wide for its own tile all converge
//     on one flag and all draw as wall here.
//   * **bit 5** -- 13,064 cells across six zones (fearfrst 9859,
//     erthcave 1681, broken2 588, lothcav 433, delfhide 324, ffarena
//     179), in compact regions, and **never on a wall cell** in any of
//     them. Its one other reader in the image is `FUN_100001ac`, which
//     tests it during movement and, when the actor's own Z is at or
//     below the cell's floor height, fires a vtable call -- i.e. a
//     surface you sink into rather than geometry you bump against. Water
//     or a hazard pool is the obvious reading and the distribution
//     supports it, but naming it is not this milestone's job; what
//     matters here is that the map paints it solid, because it is not
//     ground you can stand on.
inline constexpr uint16_t kAutomapBlockingMask = 0x2402;
inline constexpr uint8_t kAutomapBlockedFlagsBits = 0x02;       // flags bit 1
inline constexpr uint8_t kAutomapBlockedBlockBits = 0x04 | 0x20;  // blockFlags bits 2, 5

enum class AutomapTile {
    Unknown,  // never seen, or off the edge of the grid
    Blocked,  // wall, locked region, or a tile-stamped prop
    Flat,     // seen, open, level with its diagonal neighbour
    Step,     // seen, open, floor height changes across the diagonal
};

uint16_t AutomapColorRgb444(AutomapTile tile);

// The persistent reveal-as-you-go bitmap: `registry + 0x45c`, one bit per
// tile, allocated `width * height / 8` bytes and zeroed
// (`FUN_1002f890`). Byte layout verbatim:
//
//     bitmap[(width >> 3) * y + (x >> 3)] & (1 << (x & 7))
//
// **What sets it is the renderer, not the map.**
// `TileGrid_RaycastVisibility` -- the same per-frame fan of rays that
// decides which tile faces to draw (docs/WORLD_MODEL.md) -- ORs a bit in
// for every tile a ray passes through. So the map shows exactly the set
// of tiles that have at some point been drawn, which is what makes it
// reveal as you walk rather than showing the whole level.
//
// The original allocates it **once** (`if (registry+0x45c == 0)`) and
// never resizes it, so a later, larger zone would index past the end of a
// buffer sized for the first one; and it is written to and read back from
// the save file (`FUN_1002fa5c` / `FUN_1002f934`) next to the key-item
// flag word M56 recovered. This port sizes it per zone instead -- see
// Reset() -- which is the one deliberate departure here.
class ExploredTiles {
public:
    // Sizes and clears for a zone. The original only did this once per
    // session; doing it per zone is the departure noted above.
    void Reset(int width, int height);

    void Mark(int tileX, int tileY);
    bool IsExplored(int tileX, int tileY) const;

    // Feeds a whole frame's worth of visible tiles in one call -- pair
    // this with Zone::RaycastVisibleTiles(), which is this port's
    // reimplementation of the very function that sets these bits.
    void MarkVisible(const std::vector<std::pair<int, int>>& tiles);

    int width() const { return width_; }
    int height() const { return height_; }
    int exploredCount() const;

    // The packed bytes, in the original's own layout -- what the save
    // stream writes and reads back.
    const std::vector<uint8_t>& bytes() const { return bits_; }
    std::vector<uint8_t>& bytes() { return bits_; }
    // Bytes per tile row: `width >> 3`. Integer division, so a width that
    // is not a multiple of 8 loses its last few columns -- the original
    // has the same truncation, in both the allocation and the index.
    int strideBytes() const { return width_ >> 3; }

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<uint8_t> bits_;
};

AutomapTile ClassifyAutomapTile(const Zone& zone, const ExploredTiles& explored, int tileX,
                                 int tileY);

// ---- The player marker ----
//
// Three white lines from the centre of the drawn area. The engine builds
// them from a 2048-entry sine table (`0x100f4954`) whose every entry is
// exactly `round(256 * sin(2*pi*i/2048))` -- checked against all 2048.
int AutomapSine(int index);  // index is masked to 0..2047

struct AutomapRay {
    int x0, y0, x1, y1;
};

// Fills `out` with the three rays for a heading in the engine's angle
// unit (0x10000 per turn, the same unit Entity+0xb6 carries). The base
// angle is `-0x8000 - heading`; ray 0 runs at that angle with radius
// `sin >> 5` (so 8 pixels at full scale) and rays 1 and 2 at +/- 45
// degrees with radius `sin >> 6` (4 pixels) -- a small arrow whose long
// leg is the facing.
void BuildAutomapMarker(int headingUnits, AutomapRay out[3]);

// Draws the whole overlay: caption band left to the caller, grid, marker.
// `headingUnits` as above.
void RenderAutomap(Backbuffer& backbuffer, const Zone& zone, const ExploredTiles& explored,
                    int playerTileX, int playerTileY, int headingUnits);

}  // namespace sk
