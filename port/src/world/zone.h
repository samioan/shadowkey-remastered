#pragma once

// Loads the subset of a zone's per-zone files (docs/ZONE_FORMAT.md) needed
// to render its static tile-grid wall/floor/ceiling geometry: the
// TileGrid_RaycastVisibility + SurfaceFace_* pipeline documented in
// docs/RENDERER_3D.md's "The tile-grid wall/surface-face renderer"
// section, plus (M9) the Bullseye per-cell lighting bake (docs/
// ZONE_FORMAT.md's "Bullseye subsystem" -- Bullseye_BakeLighting/
// Bullseye_PropagateLight), plus the zone's .zsk skybox mesh (see
// SkyMesh() below). Explicitly still out of scope: the .zfg
// fog LUT (a separate, distance-driven effect layered on top of this
// per-cell bake in the real engine, not reproduced here).

#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "world/model_archive.h"

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
    // Two separate authored eye-height thresholds, decompiled this session
    // from Render3DScene (0x100166c8):
    //   +2 gates the FLOOR  -- it draws only when `floorBandThreshold <
    //      cameraZ` (or ZmpCell flags bit2 forces it).
    //   +4 gates the CEILING -- `cameraZ < ceilingBandThreshold` picks
    //      band A (its underside), otherwise band B (its top).
    // Offset 2 was previously unparsed and the floor drew unconditionally.
    // Checked against real data: `+4` equals min(ceilingHeight[4]) for
    // 100% of entries in every shipped zone, while `+2` equals
    // min(floorHeight[4]) for only 8% of azra's and 23% of snowline's --
    // i.e. it is a genuinely authored value, not a copy of the floor
    // height, which is exactly what a designer-controlled "is the player
    // above this floor" cutoff would look like.
    int16_t floorBandThreshold = 0;
    int16_t ceilingBandThreshold = 0;
    int16_t floorHeight[4] = {0, 0, 0, 0};
    int16_t ceilingHeight[4] = {0, 0, 0, 0};
    uint8_t surIndexE_lo = 0xff, surIndexW_lo = 0xff, surIndexS_lo = 0xff, surIndexN_lo = 0xff;
    uint8_t surIndexE_hi = 0xff, surIndexW_hi = 0xff, surIndexS_hi = 0xff, surIndexN_hi = 0xff;
    uint8_t surIndexCeilingA = 0xff, surIndexCeilingB = 0xff, surIndexFloor = 0xff;
    // Byte 0x23 -- a per-tile *vertex nudge* code, decompiled this session
    // from SurfaceFace_BuildAndProject (0x1005d784)'s per-vertex switch:
    // the real engine looks up the grid cell each face vertex falls in
    // (`engine+0x690c + zcpIndex*0x24 + 0x23`, i.e. exactly this byte of
    // this 36-byte record) and shifts that vertex by half a tile (0x80 =
    // 128 raw units) along X and/or Y before projecting it. That's what
    // produces the game's diagonal/bevelled wall corners -- geometry the
    // port drew as hard 90-degree corners until now. Codes:
    //   0 = none
    //   1 = +X +Y   2 = -X +Y   3 = -X -Y   4 = +X -Y
    //   5 = +X      6 = -X      7 = +Y      8 = -Y
    // Real-data frequency: ~7% of all .zcp entries across the 21 shipped
    // zones are non-zero (982 of azra's 11828), so this is a real but
    // minority feature -- see Zone::CornerNudge().
    uint8_t cornerNudge = 0;
};

// One .sur record, 8 bytes on disk. **Fully decompiled** from
// SurfaceFace_BuildAndProject (0x1005d784) -- see docs/ZONE_FORMAT.md and
// Zone::SurfaceUv() below for how these drive the real UV computation.
struct SurfaceRecord {
    int8_t uShift = 5;   // byte[0], read as a *signed* char by the real code
    int8_t vShift = 5;   // byte[1]
    int16_t uOffset = 0;  // byte[2..3], signed 16, in raw world units
    int16_t vOffset = 0;  // byte[4..5]
    uint8_t flags = 0;    // byte[6]: bit0 = flip V, bit1 = flip U, bit5 = disable face
    uint8_t textureIndex = 0;  // byte[7], index into .ztx's 0x4000-byte slot array

    bool flipU() const { return (flags & 0x02) != 0; }
    bool flipV() const { return (flags & 0x01) != 0; }
    bool disabled() const { return (flags & 0x20) != 0; }
};

// Which world axis drives U (and V) for a face, matching
// SurfaceFace_BuildAndProject's `param_5` switch exactly. Render3DScene
// (0x100166c8) picks these per wall direction: the +Y/-Y ("south"/"north")
// blocks pass a literal 2/3, the floor/ceiling blocks pass 5/4, and the
// +X/-X ("east"/"west") blocks pass 0 or 1 *chosen by the surface's own
// flip-U bit* (east: flipU ? 0 : 1; west: flipU ? 1 : 0).
enum class FaceOrient : uint8_t {
    UPlusY = 0,   // u = ( worldY + uOffset)
    UMinusY = 1,  // u = (uOffset - worldY)
    UPlusX = 2,   // u = ( worldX + uOffset)
    UMinusX = 3,  // u = (uOffset - worldX)
    Ceiling = 4,  // u = ( worldX + uOffset), v = (worldY + vOffset)
    Floor = 5,    // identical math to Ceiling -- the real switch falls through
};

// One .zmp cell, 6 bytes on disk (docs/ZONE_FORMAT.md's ZmpCell).
struct ZmpCell {
    uint8_t flags = 0;  // bit0 light source, bit1 wall, bit3 force-draw, bit6 ceiling-band-select,
                         // bits4-5 .zlu hue-family selector for faces this cell blocks/owns (this
                         // session, decompiled -- see Zone::PaletteColor()'s comment)
    // The cell record's **second** byte: the engine's per-tile "this
    // square blocks movement" bit, plus other undecoded bits.
    //
    // M44 found `LockZone`/`UnlockZone` writing bit 2 here and named it
    // `kBlockLocked`/`IsLocked()`. That name was half the story and it
    // misled: **M67 found that the bit has a second, far more common
    // writer -- the entity tile stamp** (`StampEntityBox` below). The
    // shipped `.zmp`s carry 39,908 cells with the bit already set, 36,332
    // of them open floor, and recomputing every solid tile-stamped
    // placement's footprint reproduces 12,403 of those cells with
    // **100.0% precision in 19 of the 21 zones** (99.4%/99.9%, one tile
    // each, in glaciercrawl and raiders). So the on-disk bit is not a
    // dormant "locked" flag waiting for a script -- it is the baked
    // initial blocking state, and a closed door's own footprint is part
    // of it.
    //
    // That is also why the corpus contains **thirty `UnlockZone` calls
    // and not one `LockZone`**: nothing ever needs to lock at runtime,
    // because whatever starts blocked already says so on disk.
    //
    // Renamed to `IsBlocked()` accordingly. The rest of the byte is still
    // undecoded (values 0x02/0x20/0x40/0x60 also occur, and 0x02 is the
    // upper-storey selector `CollisionFloorHeightAt` reads).
    uint8_t blockFlags = 0;
    static constexpr uint8_t kBlockSolid = 0x04;
    // The engine's own two traces (the projectile tick FUN_1005f928 and
    // the raycast at 0x100187d0) spell this `(byte & 0x1c) == 4` rather
    // than a plain bit test. Bits 3 and 4 are set in no shipped cell and
    // nothing writes them, so the two forms agree everywhere; kept as the
    // simple test the rest of this port already uses.
    bool IsBlocked() const { return (blockFlags & kBlockSolid) != 0; }
    // `lightLevel`'s on-disk value is only a leftover editor baseline --
    // the real engine zeroes it at the start of Bullseye_BakeLighting and
    // rebuilds it from scratch (propagation + .zcp's lightDelta). Zone::
    // Load() reproduces that: after parsing, this field holds the *baked*
    // result (see BakeLighting() below), not the raw disk bytes.
    uint16_t lightLevel = 0;
    uint16_t zcpIndex = 0;

    bool IsWall() const { return (flags & 0x02) != 0; }
    bool IsLightSource() const { return (flags & 0x01) != 0; }
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
    // M61: the player-start record's own three orientation channels, raw
    // 16-bit angles (65536 == one full turn), same units and fields as
    // EntPlacement::yawRaw below. `GameEngine_InitLevel`'s `typeId == 1`
    // branch writes all three onto the player exactly the way the
    // generic-entity branch writes them onto a placed object -- pitch from
    // record offset 0x10, yaw from 0x14, roll from 0x0c
    // (docs/ZONE_FORMAT.md's verified destination table: `+0xa8`, `+0xb6`,
    // `+0xb2`). This port read the position and dropped the angles, so
    // every zone entry faced due +x regardless of where the level designer
    // pointed the arrival.
    uint16_t playerStartPitchRaw = 0;
    uint16_t playerStartYawRaw = 0;
    uint16_t playerStartRollRaw = 0;

    // One non-player-start .ent record (docs/ZONE_FORMAT.md's
    // EntPlacement, typeId > 1) -- resolving typeId to an actual model
    // is the caller's job (world/entity_types.h + world/model_archive.h),
    // matching the real engine's own two-step chain (entities.txt's
    // global type table, looked up per-placement).
    struct EntPlacement {
        int32_t x = 0, y = 0, z = 0;
        int32_t typeId = 0;
        // Per-placement heading, raw 16-bit angle (65536 == one full turn)
        // -- the record's `unkA` low u16 at offset 0x14, which
        // docs/ZONE_FORMAT.md already identifies as feeding the object's
        // `+0xb6` orientation channel (the same field and format the
        // player's own compass heading uses). Same units as
        // `AddRotationTurn`'s argument, so a door's scripted -64*256
        // quarter-turn composes with it directly.
        //
        // This was decoded but never used: every entity rendered at yaw 0,
        // which is why *doors looked permanently open* -- a door in an
        // east-west wall is authored at ~90 degrees and was being drawn
        // face-on, reading as a gap in the wall. Real azra door placements
        // carry 0 / 16640 / 16768 / 33152 / 49408 / 49472 / 49536, i.e.
        // the four axis directions.
        uint16_t yawRaw = 0;
        // M71: the record's other two orientation channels and its scale,
        // all three decoded in docs/ZONE_FORMAT.md since M52 and none of
        // them read until now.
        //
        //   rotOrScale[2] @0x10 -> object +0xa8   (rotARaw)
        //   rotOrScale[0] @0x0c -> object +0xb2   (rotBRaw)
        //   unkB low u16  @0x18 -> object +0x5e   (scaleRaw, 8.8; 256 == 1x)
        //
        // The two angles are zero for ~97% of shipped placements, so the
        // renderer's yaw-only transform looked right nearly everywhere.
        // The scale is not: 958 of the 8,258 placements across the 21
        // zones carry something other than 256, spanning 32 (0.125x) to
        // 7424 (29x), 764 of them ordinary scenery -- so a port that
        // ignores it draws that many props at the wrong size.
        uint16_t rotARaw = 0;
        uint16_t rotBRaw = 0;
        uint16_t scaleRaw = 256;
        // M18: the record's own 40-byte instance name (offset 0x20) --
        // distinct from entities.txt's per-typeId descriptor name (a
        // script path). Empty for the overwhelming majority of placements
        // (only a handful of real, individually-referenced instances like
        // "m1".."m7"/"trthgar" in azra ever set this); real scripts look
        // these up via the bare global `Level.GetEntity(name)`
        // (simkin_bindings/level_executable.h) -- confirmed against real
        // azra.ent data (record 20, typeId 141/Gravel_Trothgar, name
        // "trthgar", matching monsters/azra_rat.s's own
        // `Level.GetEntity("trthgar")` call).
        std::string name;
        // M35: the record's **second** string, at offset 0x28.
        //
        // The tail of an .ent record is not one 40-byte name field, it is
        // two NUL-terminated strings in fixed buffers: `char name[8]` at
        // 0x20 and this one, `char script[32]`, at 0x28. Reading them as
        // one produced names like "ContaineRaiders\RT_A.s" -- which is
        // exactly "Containe" (a "Container" tag truncated into its 8-byte
        // field) immediately followed by "Raiders\RT_A.s". Several records
        // also carry stale editor bytes after their NUL (azra's Old_Trinket
        // reads "monsters\Old_Trinket.s\0Azra.s"), which only makes sense
        // for a string written into a reused fixed buffer.
        //
        // For gameplay entities this is **the real per-placement script
        // path**, and it is authoritative over the entities.txt entry for
        // the typeId: across all 21 shipped zones it resolves to a real
        // .s file for 1530/1532 monster placements, 9/9 merchants, 11/11
        // trapped objects, 393/570 containers and 442/559 doors -- and it
        // is frequently a *different* script than the typeId's own (azra's
        // "tanyin" placement is typeId 166, whose entities.txt script is
        // monsters\Tanyin_Aldwyr.s, but whose placement names
        // monsters\Tanyin_Aldwyr_Azra.s -- the zone-specific variant).
        //
        // For scenery it is just a label ("footlocker", "rock"), which is
        // why callers must check the path actually resolves before using
        // it. Empty when the record has no second string.
        std::string scriptPath;
    };
    const std::vector<EntPlacement>& entities() const { return entities_; }

    // ---- M44: `<zone>.zon`, the named-region list ----
    //
    // This is the answer to the roadmap's longest-standing "where does a
    // named zone region live?" -- the one M38 left open after ruling out
    // `.ent` placements. It is `.zon`, whose room records ZONE_FORMAT.md
    // already decoded byte-for-byte without recognising what they were
    // for: `u16 count` then 72-byte `{u16 a, b, c, d; char name[64]}`
    // records. The four numbers are an **axis-aligned tile rectangle**,
    // and the name is what `EnterZone(s)` receives and what the Encounter
    // spawner's region strings match.
    //
    // Three independent confirmations, since this had been guessed at
    // before:
    //   * azra's region named "start" is (118..121, 42..46) once the Y
    //     flip below is applied, and azra's own player start tile is
    //     (118, 46) -- inside it.
    //   * every shipped record has a < c, in all 21 zones.
    //   * `LockZone`/`UnlockZone` (FUN_10073470 / FUN_10073380) iterate
    //     `for (x = a; x < c; ++x) for (y = d'; y < b'; ++y)` straight
    //     over the cell grid, which only type-checks as a rectangle.
    //
    // **The Y flip.** Fields b and d are stored as `gridHeight - y`, and
    // GameEngine_InitLevel converts them back with the grid height it read
    // out of the *already-loaded* `.zmp` header (offset 0x82). So d is the
    // top edge and b the bottom one, and they swap places on conversion.
    // ZONE_FORMAT.md had noticed the `zmpTotal - field` conversion and
    // guessed the fields were indices into "some zone-wide array sized by
    // zmpTotal"; they are coordinates in a flipped axis.
    struct Region {
        std::string name;
        // Tile-space, and **inclusive at both ends**.
        //
        // That is not what LockRegion/UnlockRegion do, and the discrepancy
        // is the engine's, not a choice here: their loops are literally
        // `for (x = x0; x < x1; ++x)`, so locking a region misses its last
        // row and column.
        //
        // Containment is inclusive, and the shipped data is what says so.
        // Checking all 21 zones' `.ent` player-start tiles against their
        // own `.zon` rectangles, 12 spawn inside a region -- and every one
        // of those regions is named like an arrival point: `start`,
        // `entry`, `Entrance`, `Enter`, `Exit`, `Zvoldoor`, `vig`. Four of
        // the twelve survive only under inclusive bounds, because the
        // spawn sits exactly on the far edge: azra's `start` (spawn
        // (118,46), region y42..46), broken1's `Exit`, crypt3's
        // `Entrance` and fearfrst's `Exit`.
        //
        // What those regions *do* is the corroboration. crypt1.s,
        // delfhide.s, ghstpass.s and glaciercrawl.s each contain
        // `if (zone = "vig") Level.Vignette(n)` -- an arrival cutscene,
        // which only makes sense if standing at the spawn point counts as
        // being in the region.
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        bool Contains(int tileX, int tileY) const {
            return tileX >= x0 && tileX <= x1 && tileY >= y0 && tileY <= y1;
        }
    };
    const std::vector<Region>& regions() const { return regions_; }

    // M45: `FUN_1008ae94` -- where an encounter puts a creature it just
    // created.
    //
    //   attempts = x1 - x0;                    // the region's *width*
    //   for (i = 0; i < attempts; ++i) {
    //       tx = rand(x0, x1);  ty = rand(y0, y1);   // inclusive
    //       if (occupancyGrid[ty][tx] == 0) return tileAt(tx, ty);
    //   }
    //   return 0;
    //
    // Two details worth keeping. The retry budget is the rectangle's width
    // and nothing to do with its area, so a long thin region gets very few
    // tries. And the draw is inclusive at both ends -- `FUN_100730c8` is
    // `lo + rand % (hi - lo + 1)` -- which lines up with the inclusive
    // containment Region::Contains documents.
    //
    // `occupied(tx, ty)` stands in for the real per-tile entity grid at
    // `engine+0x6904`, which this port has no equivalent of; the caller
    // scans its live creatures instead. Wall and locked tiles are rejected
    // here on top of that: the real grid holds no entity on a wall tile,
    // so the engine would happily spawn inside one, and refusing is a port
    // safety property rather than a recovered rule.
    template <class IsOccupied>
    bool FindFreeTileInRegion(const Region& region, IsOccupied&& occupied, int& outTileX,
                               int& outTileY) const {
        const int spanX = region.x1 - region.x0 + 1;
        const int spanY = region.y1 - region.y0 + 1;
        if (spanX <= 0 || spanY <= 0) return false;
        const int attempts = region.x1 - region.x0;
        for (int i = 0; i < attempts; ++i) {
            int tx = region.x0 + std::rand() % spanX;
            int ty = region.y0 + std::rand() % spanY;
            if (!InBounds(tx, ty)) continue;
            const ZmpCell& cell = CellAt(tx, ty);
            if (cell.IsWall() || cell.IsBlocked()) continue;
            if (occupied(tx, ty)) continue;
            outTileX = tx;
            outTileY = ty;
            return true;
        }
        return false;
    }

    // Names are not unique: azra has four separate rectangles all called
    // "YouSure" and three called "queue", so a name identifies a *set* of
    // rooms. The engine's own two consumers differ on this, and the
    // difference is reproduced -- see LockRegion/UnlockRegion below.
    std::vector<const Region*> RegionsNamed(const std::string& name) const;

    // Every region containing this tile, innermost first is *not*
    // guaranteed -- they come back in file order, which is what the
    // engine's own 40-slot linear walk gives.
    std::vector<const Region*> RegionsAt(int tileX, int tileY) const;

    // `LockZone(name)` -- FUN_10073470. Looks the name up in the level's
    // name-to-room *map* (so one room, the one the map holds for that
    // name) and **assigns** `4` to every covered cell's second byte.
    // Returns the number of tiles changed.
    int LockRegion(const std::string& name);

    // `UnlockZone(name)` -- FUN_10073380. Deliberately different: it walks
    // all 40 room slots with `strcmp` and clears bit 2, so it unlocks
    // *every* region sharing the name, not just one. Reproduced rather
    // than made symmetric.
    int UnlockRegion(const std::string& name);

    // ---- M67: the entity tile stamp -------------------------------------
    //
    // `FUN_10066204` (set) and `FUN_1006640c` (clear) -- one walk, one
    // line apart (`tile[1] |= mask` vs. `tile[1] &= ~mask`), so they are
    // one function here with a `set` flag.
    //
    // The engine splits entity collision two ways (world/model_collision.h):
    // anything up to half a tile is tested per-entity from the tile's own
    // list, and anything **wider** is baked into the tile grid and blocks
    // like a wall. This is that bake. It walks the entity's box in
    // half-tile (128-unit) steps, **rotated by the entity's current
    // heading**, and OR/AND-NOTs `mask` into each covered cell's second
    // byte. Out-of-grid steps are *clamped* to the edge tile rather than
    // skipped -- the real loop does `if (t < 0) t = 0; if (dim-1 < t)
    // t = dim-1;`, and that is reproduced, edge smear included.
    //
    // Who calls it, and why it is the fix for "an open door is still a
    // wall": the `SetPassable(bool)` native (Object/Entity dispatch case
    // 0x17) is
    //
    //     entity->passable /* +0xd5 */ = arg;
    //     if (entity->vtable[0xac]())            // is it tile-stamped?
    //         arg ? FUN_1006640c(entity, 4, 1)   // opened -> clear
    //             : FUN_10066204(entity, 4, 1);  // closed -> set
    //
    // -- so a door's own SetPassable is what lifts its footprint out of
    // the grid. `door.s` swings open with `SetPassable(true);
    // AddRotationTurn(-64*256)` and closes with `SetPassable(true);
    // AddRotationTurn(64*256); SetPassable(false)`, and the reason it
    // asks to be passable *before* rotating even when closing is exactly
    // this: clear the old footprint at the old heading, turn, re-stamp at
    // the new one. A native door path in the image spells the same three
    // steps out literally, `SetPassable`'s two vtable slots included.
    //
    // `journal` mirrors the real `param_3`: record each touched cell in
    // the tile-change journal above. Every `SetPassable`-driven call
    // passes 1; `GameEngine_InitLevel`'s startup pass passes 0. Returns
    // the number of cells written.
    //
    // Angles are raw 16-bit headings (65536 per turn), the `.ent` and
    // `AddRotationTurn` convention.
    int StampEntityBox(int worldX, int worldY, int headingRaw, int halfExtentX, int halfExtentY,
                       uint8_t mask, bool set, bool journal);

    // M44: the per-level tile-change journal (`level+0xfc` / `+0x100`, a
    // fixed 100-entry array of `{i16 x, i16 y, u16 savedByte}`). Every
    // Lock/Unlock records the tile's resulting second byte here, replacing
    // an existing entry for the same tile rather than appending -- which
    // makes it exactly "the set of cells this level has diverged from its
    // .zmp in", i.e. the level-state half of a save file. Capped at 100 in
    // the real engine, and the cap is reproduced.
    struct TileChange {
        int16_t x = 0, y = 0;
        uint16_t blockFlags = 0;
    };
    static constexpr size_t kMaxTileChanges = 100;
    const std::vector<TileChange>& tileChanges() const { return tileChanges_; }

    // M44: `Level.LightRect(x0, y0, x1, y1, level)` -- the zone-effects
    // dispatcher's case 1. Overwrites the *baked* light level of every
    // cell in a half-open tile rectangle with `level << 8`, the same 8.8
    // scale PaletteColor()'s `lightLevel >> 8` rung lookup reads. Every
    // one of the 8 shipped call sites passes 64, which the rung clamp
    // saturates to the brightest rung -- crypt2/controller.s uses it to
    // light one alcove per puzzle step, and monsters/umbra_keth.s lights
    // the arena it appears in.
    void LightRect(int x0, int y0, int x1, int y1, int level);

    // The zone's **skybox** mesh, from <zone>.zsk (M70, correcting M11's
    // "the zone's own baked room geometry" reading -- see
    // render3d/zone_renderer.h and docs/ZONE_FORMAT.md).
    //
    // The engine's own name for it: the debug markers bracketing this
    // file's load in the zone loader read `"InitLevel Pre skybox load"` /
    // `"InitLevel Post skybox load"`, and the object the loaded resource is
    // stored on (`engine+0x62c`) is built by a line traced as
    // `"Bullseye constructer Post newing Skybox"`. `.zsk` = "zone skybox".
    //
    // It decompresses (via LoadCompressedZoneFile, same as every other
    // per-zone file) into an ordinary MODEL_FORMAT.md resource -- header,
    // H5==H2*3 invariant and all -- but its texture is read with the
    // engine's fixed 256x256 skybox addressing rather than from that
    // header, which is what ParseSkyboxResource() exists for.
    //
    // Returns nullptr if the zone has no .zsk or it failed to parse. Not
    // fatal to Load() either way, and it is the real engine's own fallback
    // too: a failed load clears `engine+0xbe0e`, and Render3DScene then
    // flat-fills the frame with a background colour instead of drawing
    // this mesh.
    const Model* SkyMesh() const { return skyMeshValid_ ? &skyMesh_ : nullptr; }

    // 128x128 8bpp-palettized texel at (x,y) within .ztx's texture slot
    // `surfaceTextureIndex` (0x4000 bytes = 128*128 -- confirmed: every
    // .ztx slot is exactly that size). Returns 0 if the index/coords are
    // out of range.
    uint8_t TexelAt(int surfaceTextureIndex, int x, int y) const;

    // Resolves a palettized texel to a real 16bpp-ish color.
    //
    // CORRECTED twice this session -- see git history. First pass: was
    // picking 2048-byte "set" `texIdx % 64`, always its first 512-byte
    // chunk (the near-black/banded walls a real screenshot comparison
    // caught). Dumping a real `azra.zlu` (131072 bytes) showed it's
    // actually **4 hue-family palettes of 64 brightness rungs each**
    // (512 bytes/rung, rung 0 of every family pure black rising to a
    // clipped-white top by roughly rung 8-10, each family its own hue) --
    // matching `kMaxLightLevel` (0x3f00 = 63<<8) exactly, so
    // `ZmpCell::lightLevel` (already baked per-tile) directly selects the
    // rung (a >>8, no rescale). That fixed the near-black bug but the
    // *family* selector was still a guess (read from `.sur`'s own flags
    // byte) -- confirmed wrong by fully decompiling
    // `SurfaceFace_BuildAndProject`/`SurfaceFace_ClipAndDispatch`
    // (0x1005d784/0x1005d074, `shadowkey/extracted/decomp_1005d784.c`):
    // the real 2-bit family selector (`engine+0x6b24 + (matByte&0x30)>>2`)
    // reads `*param_2`, and `param_2` is NOT the `.sur` record at all --
    // tracing `Render3DScene`'s (0x100166c8) call sites shows it's a
    // `ZmpCell*` (the runtime, 8-byte-padded form -- `pbVar36 +- 8`/row
    // stride for the E/W/S/N wall directions, always the *blocking
    // neighbor's* cell; `pbVar36` itself, i.e. the *current* tile's own
    // cell, for floor/ceiling). So the hue family is a **per-tile**
    // property (`ZmpCell::flags` bits 4-5, previously-undocumented bits
    // in an otherwise-decoded byte -- ZONE_FORMAT.md's bit0/1/3/6 =
    // light-source/wall/force-draw/ceiling-band-select), not a per-
    // material one -- callers pass it in directly now (see
    // render3d/zone_renderer.cpp's `hueGroup` plumbing), this function no
    // longer derives it from `surIndex`. `.sur`'s own flags byte (byte 6)
    // turned out to be something else entirely, confirmed by the same
    // decompile: bit0 = flip V, bit1 = flip U, bit5 = **disable this
    // face** (see `surfaceDisabled()`) -- exactly the "flip U/flip V/
    // disable" description this doc's `.sur` comment already had, just
    // not until now mapped to specific bits.
    //
    // `lightLevel` takes the *raw* baked value (ZmpCell::lightLevel
    // range), not the [0,1] LightLevelToBrightness() float, since the
    // real per-vertex scalar and rung index share that same fixed-point
    // convention directly (a >>8, no separate rescale).
    //
    // Caution re: "hue family" as a name -- dumping a *whole* 256-entry
    // rung (not just its first ~10 indices, this session's earlier
    // sampling) shows each family is only strongly a single hue at *low*
    // indices; the mid/high index range (roughly 30-230) has real
    // material-to-material color variation within the same family (e.g.
    // family 0's index 34 decodes notably more teal than its index 10) --
    // so two different `.ztx` textures sharing one hueGroup/rung can
    // still look quite different in-game depending which index range
    // their texel data actually uses. Confirming a specific real wall's
    // rendered tint (e.g. against a reference screenshot) therefore needs
    // the real texel data too, not just the family/rung math checked out
    // in isolation -- the family/rung *selection* mechanism documented
    // above is decompiled ground truth either way.
    uint16_t PaletteColor(uint8_t hueGroup, uint16_t lightLevel, uint8_t texel) const;

    uint8_t surfaceTextureIndex(int surIndex) const;

    // The whole decoded .sur record. Out-of-range indices return a default-
    // constructed record (shift 5/5, no offset, no flags) -- which is
    // exactly what the real engine falls back to when a zone has no .sur
    // table loaded at all (`engine+0x6918 == 0` in
    // SurfaceFace_BuildAndProject).
    const SurfaceRecord& surface(int surIndex) const;

    // The real per-vertex UV, in **texel units** (already divided by the
    // 8 fractional bits the real fixed-point carries -- see below), for a
    // vertex at raw world (worldX, worldY, worldZ) on a face with this
    // orientation and surface record.
    //
    // Decompiled ground truth, SurfaceFace_BuildAndProject (0x1005d784)'s
    // tail:
    //     switch (param_5) {
    //       case 0: u = (worldY + uOffset) << uShift; break;
    //       case 1: u = (uOffset - worldY) << uShift; break;
    //       case 2: u = (worldX + uOffset) << uShift; break;
    //       case 3: u = (uOffset - worldX) << uShift; break;
    //       case 4: case 5: u = (worldX + uOffset) << uShift; vAxis = worldY; break;
    //     }
    //     v = (vAxis + vOffset) << vShift;   // vAxis = worldZ for cases 0-3
    //     if (flags & 2) u = -u;             // flip U
    //     if (flags & 1) v = -v;             // flip V
    // ...and the fixed-point scale is pinned down by the consumer: the
    // rasterizer (SurfaceFace_RasterizeTextured_v3, 0x1005bbc8) fetches
    // `texture[(mask & (u >> 8)) + ((mask & (v >> 8)) << widthShift)]`,
    // with SurfaceFace_ClipAndDispatch (0x1005d074) passing widthShift = 7
    // and mask = 0x7f/0x7e/0x7c/0x78 (a distance-driven detail reduction).
    // So **one texel = 2^(8 - shift) raw world units**: the dominant real
    // shift of 6 gives 64 texels per 256-unit tile (one 128x128 texture
    // per 2x2 tiles), 7 gives one texture per tile, and 4 (used by real
    // mural surfaces, e.g. azra.sur records 14/15) stretches one texture
    // across 8 tiles.
    //
    // This replaces the port's original flat 0..1-per-quad UV, which
    // stretched one whole texture over each face regardless of its real
    // world size -- the direct cause of walls looking wrongly textured and
    // misaligned (a 26-tile-tall wall got one vertical texture repeat
    // instead of the real ~26).
    void SurfaceUv(const SurfaceRecord& sur, FaceOrient orient, float worldX, float worldY,
                   float worldZ, float* outU, float* outV) const;

    // The real per-vertex half-tile nudge (ZcpEntry::cornerNudge, see its
    // comment): given a vertex's raw world position, adds the nudge the
    // cell that vertex falls in asks for. Applied by the real engine to
    // the vertex's *position* before both projection and UV computation,
    // so both callers here pass through it.
    void ApplyCornerNudge(float* worldX, float* worldY) const;

    // `.sur` byte 6, bit 5 (decompiled confirmation above) -- when set,
    // `SurfaceFace_BuildAndProject` skips the face entirely (no vertices
    // built, no draw call). A real azra.sur record has this set (the
    // wall face this session's first .zlu fix mistakenly rendered
    // green -- it should never have been drawn at all). Flip-U/flip-V
    // (bits 0/1) are decoded but not yet wired into the port's UV
    // computation (still a flat 0..1 quad either way).
    bool surfaceDisabled(int surIndex) const;

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

    // True if a straight line between two world positions crosses no wall
    // tile (and stays in bounds) -- a grid DDA over the same
    // `ZmpCell::IsWall()` flag CircleHitsWall and the renderer already use.
    //
    // The real engine has exactly this facility as a per-frame system
    // (`TileGrid_RaycastVisibility`, docs/WORLD_MODEL.md -- it stamps a
    // per-cell "visible this frame" byte the surface pipeline then reads,
    // see SurfaceFace_BuildAndProject's `vertex+6` frame-stamp check), so
    // sight-gating is a mechanism the original demonstrably has; how its
    // *AI* consumes it was never traced, so the specific use below (gating
    // monster aggro) is this port's own design, documented as such.
    //
    // Why it matters: every real monster script in the corpus sets a
    // chase radius of 18000 raw world units (222 of the 270 SetChaseRadius
    // calls shipped) -- 70 tiles, on a 128x128 grid. Taken as a bare
    // euclidean radius that is over half the map, which is what made the
    // port's monsters aggro from across the level, through walls and
    // floors. A radius that generous only makes sense as "anywhere the
    // creature can actually see", which is what this restores.
    bool HasLineOfSight(float x0, float y0, float x1, float y1) const;

    // Bilinearly interpolated floor/ceiling height (raw world units, same
    // convention as playerStartZ/Camera::z -- NOT tile units) at a world
    // (x,y), read from the containing tile's own ZcpEntry::floorHeight/
    // ceilingHeight[4] corner array (real corner order 0=(x0,y1),
    // 1=(x1,y1), 2=(x1,y0), 3=(x0,y0) -- decompiled this session, see
    // render3d/zone_renderer.cpp's kCornerDx/kCornerDy; these are
    // the same 4 values that pipeline already draws the floor/ceiling
    // quad from, just sampled here instead of rendered). No cross-tile
    // blending: each tile's 4 corners are its own stored values, not
    // shared with neighbors, so this only ever reads the one tile
    // worldX/worldY falls in. Out-of-bounds returns 0.0f (callers only
    // query positions collision has already accepted, per
    // CircleHitsWall's own out-of-bounds-blocks convention above, so this
    // is a conservative fallback, not expected to matter in practice).
    float FloorHeightAt(float worldX, float worldY) const;
    float CeilingHeightAt(float worldX, float worldY) const;

    // M49: the height an *entity* collides against at (worldX, worldY) when
    // it is at `worldZ` -- `FUN_1001beac`, which is what the arrow's four
    // wall probes actually test (`blocked == z < this`). Three real
    // differences from FloorHeightAt above, all of them decompiled:
    //
    //   * The corner arrays are conditional. `FUN_1001bd50` interpolates the
    //     four floorHeight corners only when the ZmpCell's flags bit 2 is
    //     set, and otherwise returns the flat `floorBandThreshold` at
    //     ZcpEntry+2; `FUN_1001bcac` does the same for the ceiling against
    //     bit 6 and `ceilingBandThreshold`. The renderer here already reads
    //     both bits that way, so this is the same rule, now applied to
    //     collision too.
    //   * A two-storey tile -- ZmpCell's *second* byte, bit 1 -- whose
    //     ceiling surface exists and is not disabled reports its **ceiling**
    //     as the collision height for anything already above it. That is the
    //     floor of the upper storey.
    //   * There is no wall flag in this at all. What stops an arrow is
    //     geometry taller than the arrow.
    float CollisionFloorHeightAt(float worldX, float worldY, float worldZ) const;

    // M62: where an *actor* ends up after being teleported to (x, y, z) --
    // `FUN_100686e0` plus its one caller's trailing lift, which together
    // are the whole of `SetPosition`'s snap step:
    //
    //     entity->z = resolveSurface(x, y, entity->z + 0x180);   // FUN_100686e0
    //     entity->z += 0x80;                                      // case 0x30
    //
    // `resolveSurface` is CollisionFloorHeightAt above (`FUN_1001beac`), so
    // the two constants are the only new information: the requested z is
    // raised by **0x180** before being used as the upper-vs-lower-storey
    // probe, and the resolved surface is then lifted by **0x80** so the
    // actor stands on it rather than in it. Both are raw world units
    // (256 = one tile width).
    //
    // The probe is why a shipped `SetPosition` still passes a meaningful z
    // even though the result is snapped: on a two-storey tile the argument
    // is what chooses which storey. On an ordinary tile it is discarded.
    //
    // Only actors are snapped -- see EntityPositionRef's header for the
    // `vtable[0xc8]` predicate that decides, and for the portcullis in
    // `gate.s` that depends on doors being exempt.
    float SnapActorToGround(float worldX, float worldY, float worldZ) const {
        return CollisionFloorHeightAt(worldX, worldY, worldZ + 384.0f) + 128.0f;
    }

    // M41: the real per-frame visible-tile set --
    // `TileGrid_RaycastVisibility` (`FUN_1000f694`), transcribed. This
    // replaces the renderer's fixed-radius square scan, which was M6's
    // last surviving placeholder in that pipeline.
    //
    // The shape: a fan of rays from the camera, each marching a fixed
    // one-tile step, appending every tile it crosses to a list that is
    // de-duplicated by a per-cell frame stamp. Three details are worth
    // stating because none of them is what a from-scratch version would
    // have done:
    //
    //  * **The three tiers are a zoom setting, not a quality knob.**
    //    `engine+0x608` picks (rays, maxSteps, angleStep) from
    //    (150, 25, 170) / (177, 93, 96) / (178, 172, 52), and the same
    //    field is a `0x100`-is-identity render-scale factor elsewhere.
    //    Multiplying rays x angleStep gives the fan's total arc:
    //    140 degrees, 93 degrees, 51 degrees -- i.e. zooming in narrows
    //    the arc and pushes the range out, which is exactly what a
    //    scale factor should do and not at all what a "detail level"
    //    would. (Earlier notes read it as a 3-tier quality setting.)
    //  * **Rays start four steps *behind* the camera** (`x = camX - 4*dx`),
    //    which is how the tiles under and just behind the player get into
    //    the set at all.
    //  * **Walls only stop a ray after the fifth step**, and the stop test
    //    is `(flags & 0b1010) == 0b0010` -- so a cell with bit3
    //    (force-draw) set does *not* stop the ray even though it is a
    //    wall, which is the same bit the face pipeline already honours.
    //
    // `zoomScale` is `engine+0x608`; pass kDefaultZoomScale for identity.
    // Returns tile coordinates in visit order, capped the way the real
    // function caps: it stops adding at 412 and hard-breaks at 512.
    static constexpr int kDefaultZoomScale = 0x100;
    std::vector<std::pair<int, int>> RaycastVisibleTiles(float cameraWorldX, float cameraWorldY,
                                                          float cameraYawRadians,
                                                          int zoomScale = kDefaultZoomScale) const;

    // The tier `zoomScale` selects, exposed so the three sets of constants
    // can be asserted directly rather than inferred from a tile set. The
    // angle unit is the engine's own: 65536 to a full turn.
    struct VisibilityTier {
        int rayCount = 0;
        int maxSteps = 0;
        int angleStep = 0;
        // rayCount * angleStep, as a fraction of a turn -- 140, 93 and 51
        // degrees for the three tiers.
        double arcDegrees() const {
            return static_cast<double>(rayCount) * static_cast<double>(angleStep) * 360.0 / 65536.0;
        }
    };
    static VisibilityTier TierFor(int zoomScale);

private:
    int width_ = 0, height_ = 0;
    std::vector<ZmpCell> cells_;
    std::vector<ZcpEntry> zcpEntries_;
    std::vector<SurfaceRecord> surfaces_;   // the whole decoded .sur table -- see surface()
    std::vector<uint8_t> ztxData_;          // 1 header byte + N*0x4000 texture slots
    std::vector<uint8_t> zluData_;          // N*2048-byte palette sets
    std::vector<EntPlacement> entities_;
    std::vector<Region> regions_;        // M44: parsed <zone>.zon, see regions()
    std::vector<TileChange> tileChanges_;  // M44: see tileChanges()
    Model skyMesh_;          // parsed <zone>.zsk, see SkyMesh() above
    bool skyMeshValid_ = false;

    // M44: the shared tail of LockRegion/UnlockRegion -- write the cell's
    // second byte and record the change in the journal.
    void SetBlockFlags(int tileX, int tileY, uint8_t value);

    // M9: reproduces Bullseye_BakeLighting/Bullseye_PropagateLight
    // (docs/ZONE_FORMAT.md) -- called once from Load(), after cells_ and
    // zcpEntries_ are both populated. See zone.cpp for the algorithm and
    // its documented simplifications (ray count/step size approximated,
    // not a byte-exact port of the original's integer stepping).
    void BakeLighting();
};

// Baked light level (ZmpCell::lightLevel, range [0, kMaxLightLevel] per
// Bullseye_BakeLighting's own clamp) to a [0,1]-ish brightness multiplier
// for the renderer. A small non-zero floor is a deliberate *port-only*
// tweak (see the comment in zone.cpp's BakeLighting) -- the real engine
// has no such floor, so fully unlit cells there would render pure black.
constexpr uint16_t kMaxLightLevel = 0x3f00;
// The real per-vertex light/fog scalar's lower clamp, straight out of
// SurfaceFace_BuildAndProject (`if (light < 0x400) light = 0x400;`) --
// i.e. `.zlu` rung 4 is the darkest the tile-grid renderer ever goes.
constexpr uint16_t kMinLightLevel = 0x400;
float LightLevelToBrightness(uint16_t lightLevel);

}  // namespace sk
