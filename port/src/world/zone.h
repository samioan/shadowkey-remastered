#pragma once

// Loads the subset of a zone's per-zone files (docs/ZONE_FORMAT.md) needed
// to render its static tile-grid wall/floor/ceiling geometry: the
// TileGrid_RaycastVisibility + SurfaceFace_* pipeline documented in
// docs/RENDERER_3D.md's "The tile-grid wall/surface-face renderer"
// section, plus (M9) the Bullseye per-cell lighting bake (docs/
// ZONE_FORMAT.md's "Bullseye subsystem" -- Bullseye_BakeLighting/
// Bullseye_PropagateLight), plus (M11) the .zsk-baked whole-room static
// mesh (see RoomMesh() below). Explicitly still out of scope: the .zfg
// fog LUT (a separate, distance-driven effect layered on top of this
// per-cell bake in the real engine, not reproduced here).

#include <cstdint>
#include <string>
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

    // One non-player-start .ent record (docs/ZONE_FORMAT.md's
    // EntPlacement, typeId > 1) -- resolving typeId to an actual model
    // is the caller's job (world/entity_types.h + world/model_archive.h),
    // matching the real engine's own two-step chain (entities.txt's
    // global type table, looked up per-placement).
    struct EntPlacement {
        int32_t x = 0, y = 0, z = 0;
        int32_t typeId = 0;
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
    };
    const std::vector<EntPlacement>& entities() const { return entities_; }

    // M11: the room's own static mesh baked into <zone>.zsk -- a real,
    // separate render step in the original engine
    // (`RoomGeometry_TransformAndSort`, docs/RENDERER_3D.md's "Open
    // follow-ups") alongside the tile-grid wall/surface pipeline above,
    // not a replacement for it. `.zsk` decompresses (via
    // LoadCompressedZoneFile, same as every other per-zone file) into an
    // ordinary MODEL_FORMAT.md resource -- confirmed against real
    // azra.zsk this session (its header decodes exactly per that spec,
    // including the H5==H2*3 invariant). Positioned directly in the
    // zone's own world-unit frame with no per-instance offset (unlike
    // .ent-placed entities' world/entity_types.h chain) -- **unconfirmed**,
    // see render3d/zone_renderer.h's fuller M11 writeup on why this is
    // plausible but not independently verified against a real screenshot.
    // Returns nullptr if the zone has no .zsk or it failed to parse --
    // not fatal to Load() either way, since not every zone necessarily
    // has one.
    const Model* RoomMesh() const { return roomMeshValid_ ? &roomMesh_ : nullptr; }

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

private:
    int width_ = 0, height_ = 0;
    std::vector<ZmpCell> cells_;
    std::vector<ZcpEntry> zcpEntries_;
    std::vector<SurfaceRecord> surfaces_;   // the whole decoded .sur table -- see surface()
    std::vector<uint8_t> ztxData_;          // 1 header byte + N*0x4000 texture slots
    std::vector<uint8_t> zluData_;          // N*2048-byte palette sets
    std::vector<EntPlacement> entities_;
    Model roomMesh_;         // M11: parsed <zone>.zsk, see RoomMesh() above
    bool roomMeshValid_ = false;

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
