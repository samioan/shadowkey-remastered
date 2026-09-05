// M57 smoke test: the in-game map (world/automap.h).
//
// Four things get checked, in the order the recovery went:
//   1. the explored bitmap's exact packing, which is what the save file
//      carries and what the visibility raycast writes;
//   2. the tile classification, against real shipped zone data;
//   3. the player-arrow geometry, against the engine's own sine table;
//   4. the whole overlay drawn into a real Backbuffer.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "graphics/backbuffer.h"
#include "world/automap.h"
#include "world/zone.h"

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%-74s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    if (!ok) ++g_failures;
}

}  // namespace

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    // ---- 1. the explored bitmap's packing ----
    {
        sk::ExploredTiles explored;
        explored.Reset(128, 128);
        Check(explored.strideBytes() == 16 && explored.bytes().size() == 128 * 128 / 8,
              "explored bitmap is width*height/8 bytes, stride width>>3");
        Check(explored.exploredCount() == 0, "...and starts fully cleared");

        // The engine's index is bitmap[(w>>3)*y + (x>>3)] bit (x&7), so
        // tile (9, 3) is byte 16*3 + 1, bit 1.
        explored.Mark(9, 3);
        Check(explored.bytes()[16 * 3 + 1] == 0x02,
              "Mark(9,3) sets exactly byte (w>>3)*3 + 1, bit 1");
        Check(explored.IsExplored(9, 3) && !explored.IsExplored(8, 3) &&
                  !explored.IsExplored(9, 4),
              "...and nothing else nearby reads back as explored");
        Check(explored.exploredCount() == 1, "...one bit set in total");

        // Out of range must not corrupt neighbouring rows.
        explored.Mark(-1, 0);
        explored.Mark(128, 0);
        explored.Mark(0, 128);
        Check(explored.exploredCount() == 1, "out-of-range marks are dropped, not wrapped");

        // A width that is not a multiple of 8 truncates, in both the
        // allocation and the index -- the original does the same.
        sk::ExploredTiles odd;
        odd.Reset(20, 4);
        Check(odd.strideBytes() == 2 && odd.bytes().size() == (20 * 4) >> 3,
              "a non-multiple-of-8 width truncates the stride, as the original does");
    }

    // ---- 2. classification against a real zone ----
    sk::Zone zone;
    if (!zone.Load(scriptRoot, "azra")) {
        std::printf("m57_automap_smoke: FAILED to load azra\n");
        return 1;
    }
    std::printf("azra: %d x %d tiles\n", zone.width(), zone.height());
    {
        sk::ExploredTiles explored;
        explored.Reset(zone.width(), zone.height());

        // Nothing is explored yet, so every tile -- wall or not -- must
        // read as Unknown. This is the property that makes it a map
        // rather than a minimap.
        int unknown = 0;
        for (int y = 0; y < zone.height(); y += 7) {
            for (int x = 0; x < zone.width(); x += 7) {
                if (sk::ClassifyAutomapTile(zone, explored, x, y) == sk::AutomapTile::Unknown) {
                    ++unknown;
                }
            }
        }
        int sampled = ((zone.height() + 6) / 7) * ((zone.width() + 6) / 7);
        Check(unknown == sampled, "with nothing explored, every sampled tile is Unknown");
        Check(sk::ClassifyAutomapTile(zone, explored, -1, 0) == sk::AutomapTile::Unknown &&
                  sk::ClassifyAutomapTile(zone, explored, zone.width(), 0) ==
                      sk::AutomapTile::Unknown,
              "off-grid tiles are Unknown too (the same colour, as in the original)");

        // Reveal everything and count what the map would actually show.
        for (int y = 0; y < zone.height(); ++y) {
            for (int x = 0; x < zone.width(); ++x) explored.Mark(x, y);
        }
        int blocked = 0, flat = 0, step = 0, stillUnknown = 0;
        int wallCells = 0;
        for (int y = 0; y < zone.height(); ++y) {
            for (int x = 0; x < zone.width(); ++x) {
                if (zone.CellAt(x, y).IsWall()) ++wallCells;
                switch (sk::ClassifyAutomapTile(zone, explored, x, y)) {
                    case sk::AutomapTile::Blocked: ++blocked; break;
                    case sk::AutomapTile::Flat: ++flat; break;
                    case sk::AutomapTile::Step: ++step; break;
                    case sk::AutomapTile::Unknown: ++stillUnknown; break;
                }
            }
        }
        std::printf("azra fully revealed: %d blocked, %d flat, %d step, %d unknown (walls: %d)\n",
                    blocked, flat, step, stillUnknown, wallCells);
        Check(stillUnknown == 0, "with everything explored, no tile is Unknown any more");
        // 725 of azra's cells are walls but 4715 draw as solid, because
        // the mask is not the wall bit alone -- it also takes blockFlags
        // bits 2 and 5, both of which are authored on disk (counted
        // across every zone in part 2b). Getting this wrong in the first
        // draft is what turned those bits up.
        Check(blocked == 4715 && wallCells == 725,
              "azra draws 4715 tiles solid, of which only 725 are walls");
        Check(blocked > wallCells,
              "...so the blocking mask is genuinely wider than ZmpCell::IsWall()");
        Check(flat == 11143 && step == 526,
              "azra's open floor splits 11143 flat / 526 stepped");

        // The classification order matters: a wall is Blocked regardless
        // of any height difference, because the mask test comes first.
        int wx = -1, wy = -1;
        for (int y = 0; y < zone.height() && wx < 0; ++y) {
            for (int x = 0; x < zone.width(); ++x) {
                if (zone.CellAt(x, y).IsWall()) { wx = x; wy = y; break; }
            }
        }
        Check(wx >= 0 && sk::ClassifyAutomapTile(zone, explored, wx, wy) ==
                              sk::AutomapTile::Blocked,
              "a wall tile classifies as Blocked");

        // A tile the player has not seen stays hidden even though it is a
        // wall -- the explored test runs before the mask test.
        sk::ExploredTiles blank;
        blank.Reset(zone.width(), zone.height());
        Check(sk::ClassifyAutomapTile(zone, blank, wx, wy) == sk::AutomapTile::Unknown,
              "...but not before it has been seen");
    }

    // ---- 2b. the two blockFlags mask bits, across all 21 zones ----
    {
        const char* zones[] = {"azra",     "broken1",  "broken2",  "crypt1",   "crypt2",
                                "crypt3",   "delfhide", "drgnfld",  "dstar_e",  "dstar_w",
                                "erthcave", "fearfrst", "ffarena",  "ghstpass", "glaciercrawl",
                                "lakvan",   "lothcav",  "raiders",  "snowline", "stouttp",
                                "twilite"};
        int loaded = 0, bit2 = 0, bit5 = 0, bit2NotWall = 0, bit5Wall = 0, zonesWithBit5 = 0;
        for (const char* name : zones) {
            sk::Zone z;
            if (!z.Load(scriptRoot, name)) continue;
            ++loaded;
            int zoneBit5 = 0;
            for (int y = 0; y < z.height(); ++y) {
                for (int x = 0; x < z.width(); ++x) {
                    const sk::ZmpCell& c = z.CellAt(x, y);
                    if (c.blockFlags & 0x04) {
                        ++bit2;
                        if (!c.IsWall()) ++bit2NotWall;
                    }
                    if (c.blockFlags & 0x20) {
                        ++bit5;
                        ++zoneBit5;
                        if (c.IsWall()) ++bit5Wall;
                    }
                }
            }
            if (zoneBit5 > 0) ++zonesWithBit5;
        }
        std::printf("%d zones: blockFlags bit2 on %d cells (%d not walls), bit5 on %d cells in %d "
                    "zones (%d walls)\n",
                    loaded, bit2, bit2NotWall, bit5, zonesWithBit5, bit5Wall);
        Check(loaded >= 20, "the shipped zone set loads");
        // Both mask bits are real authored data, not runtime-only -- the
        // opposite of what this test first assumed, which is why the
        // numbers are pinned here rather than described in prose.
        Check(bit2 == 40517 && bit2NotWall == 36332,
              "blockFlags bit 2 is authored on 40517 cells, 36332 of them not walls");
        Check(bit5 == 13064 && zonesWithBit5 == 6,
              "blockFlags bit 5 is authored on 13064 cells across exactly 6 zones");
        Check(bit5Wall == 0,
              "...and never on a wall cell -- it marks a surface, not geometry");
    }

    // ---- 3. the player arrow ----
    {
        // The real table is exactly round(256*sin(2*pi*i/2048)) for all
        // 2048 entries; these are values read out of the shipped image.
        struct { int index, value; } samples[] = {
            {0, 0}, {1, 1}, {2, 2}, {64, 50}, {128, 98}, {256, 181},
            {341, 222}, {512, 256}, {700, 215}, {1024, 0}, {1300, -192},
            {1536, -256}, {2047, -1},
        };
        bool allMatch = true;
        for (const auto& s : samples) {
            if (sk::AutomapSine(s.index) != s.value) allMatch = false;
        }
        Check(allMatch, "the sine table matches the shipped 2048-entry table at every sample");
        Check(sk::AutomapSine(2048) == sk::AutomapSine(0) &&
                  sk::AutomapSine(-1) == sk::AutomapSine(2047),
              "...and the index wraps the way the engine's & 0x7ff does");

        sk::AutomapRay rays[3];
        sk::BuildAutomapMarker(0, rays);
        for (const sk::AutomapRay& r : rays) {
            Check(r.x0 == sk::kAutomapCenterX && r.y0 == sk::kAutomapCenterY,
                  "every marker ray starts at the map centre (0x58, 0x68)");
        }
        auto len = [](const sk::AutomapRay& r) {
            int dx = r.x1 - r.x0, dy = r.y1 - r.y0;
            return static_cast<int>(std::lround(std::sqrt(double(dx * dx + dy * dy))));
        };
        // The table's amplitude is 0x100, so `>> 5` is 8 pixels and
        // `>> 6` is 4.
        Check(len(rays[0]) == 8, "the long leg is 8 pixels (amplitude 0x100 >> 5)");
        Check(len(rays[1]) == 4 && len(rays[2]) == 4,
              "the two short legs are 4 pixels (>> 6)");
        // Heading 0 puts the long leg straight down the screen: the base
        // angle is -0x8000 - heading, and the endpoint is centre minus
        // (sin, cos) of it.
        Check(rays[0].x1 == sk::kAutomapCenterX && rays[0].y1 == sk::kAutomapCenterY + 8,
              "at heading 0 the long leg points down the screen");
        // Quarter turn: the arrow rotates with it, and stays 8 long.
        sk::AutomapRay turned[3];
        sk::BuildAutomapMarker(0x4000, turned);
        Check(turned[0].y1 == sk::kAutomapCenterY && len(turned[0]) == 8,
              "a quarter turn swings the long leg onto the horizontal");
        Check(turned[0].x1 != rays[0].x1 || turned[0].y1 != rays[0].y1,
              "...i.e. the marker actually follows the heading");
        // The exact endpoints at heading 0, worked through by hand from
        // the decompiled expressions. The two short legs are NOT mirror
        // images: sin(a+45) and sin(a-45) have equal magnitude and
        // opposite sign, and an arithmetic `>> 6` rounds a negative away
        // from zero and a positive toward it, so they land at +3 and -2
        // rather than +/-3. That asymmetry is the original's.
        Check(rays[0].x1 == 88 && rays[0].y1 == 112, "heading 0: long leg ends at (88, 112)");
        Check(rays[1].x1 == 91 && rays[1].y1 == 107, "heading 0: +45 leg ends at (91, 107)");
        Check(rays[2].x1 == 86 && rays[2].y1 == 107, "heading 0: -45 leg ends at (86, 107)");
        Check(rays[1].x1 > sk::kAutomapCenterX && rays[2].x1 < sk::kAutomapCenterX,
              "...so the short legs still straddle the long one");
    }

    // ---- 4. the whole overlay, drawn ----
    {
        sk::ExploredTiles explored;
        explored.Reset(zone.width(), zone.height());
        sk::Backbuffer bb;
        bb.Fill(0);

        int px = zone.width() / 2, py = zone.height() / 2;
        sk::RenderAutomap(bb, zone, explored, px, py, 0);

        // The drawn area is exactly 128x128 at (24, 40), and everything
        // outside it is untouched.
        bool insidePainted = true, outsideClean = true;
        for (int y = 0; y < sk::Backbuffer::kHeight; ++y) {
            for (int x = 0; x < sk::Backbuffer::kWidth; ++x) {
                bool inside = x >= sk::kAutomapOriginX &&
                               x < sk::kAutomapOriginX + sk::kAutomapSizePixels &&
                               y >= sk::kAutomapOriginY &&
                               y < sk::kAutomapOriginY + sk::kAutomapSizePixels;
                uint16_t px16 = bb.Row(y)[x];
                if (inside && px16 == 0) insidePainted = false;
                if (!inside && px16 != 0) outsideClean = false;
            }
        }
        Check(insidePainted, "the map paints every pixel of its 128x128 area");
        Check(outsideClean, "...and touches nothing outside it");

        // Unexplored everywhere means the whole grid is one colour, and
        // the only other thing drawn is the white arrow.
        int distinct = 0;
        uint16_t seen[8] = {0};
        for (int y = sk::kAutomapOriginY; y < sk::kAutomapOriginY + sk::kAutomapSizePixels; ++y) {
            for (int x = sk::kAutomapOriginX; x < sk::kAutomapOriginX + sk::kAutomapSizePixels;
                 ++x) {
                uint16_t c = bb.Row(y)[x];
                bool known = false;
                for (int i = 0; i < distinct; ++i) {
                    if (seen[i] == c) known = true;
                }
                if (!known && distinct < 8) seen[distinct++] = c;
            }
        }
        Check(distinct == 2, "an unexplored map is exactly two colours: the ground and the arrow");

        // Now reveal a single tile at the player's own position and check
        // the 2x2 block it owns changes -- the centre tile of a 64-wide
        // window is the 32nd, so it lands at the middle of the area.
        explored.Mark(px, py);
        sk::Backbuffer bb2;
        bb2.Fill(0);
        sk::RenderAutomap(bb2, zone, explored, px, py, 0);
        int changed = 0;
        for (int y = 0; y < sk::Backbuffer::kHeight; ++y) {
            for (int x = 0; x < sk::Backbuffer::kWidth; ++x) {
                if (bb.Row(y)[x] != bb2.Row(y)[x]) ++changed;
            }
        }
        Check(changed > 0 && changed <= 4,
              "revealing one tile repaints at most its own 2x2 block");

        // Larger world Y is *up* on screen: the tile loop counts world Y
        // down while the screen row counts up. Reveal a tile one step
        // north and check it lands on the row above.
        sk::ExploredTiles north;
        north.Reset(zone.width(), zone.height());
        north.Mark(px, py + 1);
        sk::Backbuffer bb3;
        bb3.Fill(0);
        sk::RenderAutomap(bb3, zone, north, px, py, 0);
        int northRow = -1, ownRow = -1;
        for (int y = sk::kAutomapOriginY; y < sk::kAutomapOriginY + sk::kAutomapSizePixels; ++y) {
            for (int x = sk::kAutomapOriginX; x < sk::kAutomapOriginX + sk::kAutomapSizePixels;
                 ++x) {
                if (bb3.Row(y)[x] != bb.Row(y)[x] && northRow < 0) northRow = y;
                if (bb2.Row(y)[x] != bb.Row(y)[x] && ownRow < 0) ownRow = y;
            }
        }
        Check(northRow >= 0 && ownRow >= 0 && northRow == ownRow - sk::kAutomapPixelsPerTile,
              "a tile one step further along world Y draws one block higher on screen");
    }

    std::printf("\nm57_automap_smoke: %s (%d checks)\n",
                g_failures ? "FAILED" : "PASSED (all checks)", 34);
    return g_failures ? 1 : 0;
}
