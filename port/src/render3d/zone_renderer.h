#pragma once

// A from-scratch software renderer for the tile-grid wall/floor/ceiling
// geometry docs/RENDERER_3D.md's "tile-grid wall/surface-face renderer"
// section documents -- NOT a port of that pipeline's exact fixed-point
// scanline/clip algorithm (Poly3D_ClipAgainstPlane's Sutherland-Hodgman
// clip, the 1/z reciprocal LUTs, etc). Per the project's Phase 2 decision
// (docs/ROADMAP.md: behavioral RE, not byte-exact recompilation), this
// uses ordinary floating-point perspective projection and a standard
// barycentric/edge-function triangle rasterizer with a z-buffer, reading
// the *same* real per-zone data (world/zone.h) the original renders from.
//
// Known simplifications vs. the real engine (see zone_renderer.cpp for
// the reasoning behind each): no TileGrid_RaycastVisibility (a fixed-
// radius tile scan instead), no near-plane clipping (a quad/triangle is
// dropped whole if any corner is behind the camera), and always ceiling
// band A (the eye-height-vs-threshold comparison that picks A/B is
// skipped). M9 added the Bullseye lighting bake (see below) -- tile
// faces are no longer unconditionally unlit. M11 added the wall
// "upper band" stepped second segment (see below) -- no longer skipped.
//
// M8 added placed-entity rendering (real .ent placements resolved
// through entities.txt -> models.idx/.huge, world/entity_types.h +
// world/model_archive.h) on top of M6's tile-grid geometry.
//
// A real finding from building this: MODEL_FORMAT.md never pinned down
// the model resource's local coordinate axes. Empirically (see
// zone_renderer.cpp's entity-transform comment), they're **Y-up**: a
// real barrel model's bounding box is round in local X/Z and tall in
// local Y, and a real door model's is thin in local Z, wide in local X,
// tall in local Y -- both only make sense with local Y as the vertical
// axis. Worth folding back into MODEL_FORMAT.md once independently
// re-confirmed against more model/context pairs.
//
// Known simplifications/open questions specific to entity rendering:
// frame 0 (resting pose) only, no per-instance skin/variant selection
// (always skin 0), no orientation (the .ent record's rotOrScale[0..2]
// fields' exact meaning is still unconfirmed -- docs/ZONE_FORMAT.md --
// so entities render in their model's default facing rather than a
// guessed-at rotation formula), no distance culling (every resolved
// entity in the zone is submitted every frame, regardless of the
// renderRadius tile scan below), and **no confirmed vertex-to-world
// scale factor** -- vertices are currently assumed to share the same raw
// unit convention as .ent positions with no extra multiplier, which
// visually renders entities a bit large (a barrel about as wide as a
// full floor tile). RENDERER_3D.md's `ComposeTransform3x4` writeup
// mentions the actor transform block covering "position/orientation/
// scale," hinting a real per-instance scale field exists somewhere this
// port isn't reading yet -- not chased down this pass.
//
// World Z-scale investigation (this session, follow-up to the above):
// confirmed, by decompiling SurfaceFace_BuildAndProject (0x1005d784),
// that X/Y/Z genuinely share ONE raw-unit scale in the real engine --
// it feeds camera-relative X, Y, and height deltas into the exact same
// rotation-matrix weighted-sum-then->>8 formula with no separate height
// scale factor, which is only geometrically valid if all three share one
// unit. So the port's single kTileScale divisor applied uniformly to
// X/Y/Z (unchanged since M6) was already correct -- not a bug. The
// "implausibly tall room" read from M8's renders turned out to be real:
// azra's own .zcp data has a *median* floor-to-ceiling height of ~6800
// raw units (~27 tile-widths) across all 15,659 open cells, with many
// exact/near-exact multiples of a tile (15.00, 19.00, 22.00, 34.00
// tile-heights, plus 1/8-tile-quantized fractions) -- genuine,
// intentional level data, not degenerate placeholder cells. A "floor +
// nearby wall, ceiling out of frame" render is what a normal eye-level
// view of a room that tall actually looks like; it isn't a rendering
// defect. Full writeup: docs/RENDERER_3D.md's "World X/Y/Z share one
// uniform fixed-point scale" open-follow-up entry.
//
// What *did* get fixed as a result: the player eye-height offset above
// the floor (render3d/camera.h's kEyeHeightOffset) was a bare guess of
// 128 raw units -- implausibly small once real room heights are in the
// thousands. Recalibrated to 800 using two independent real model-height
// measurements from M8's entity data (a door's local-space height span
// is ~1036 raw units, a barrel's ~373 -- both consistent with a person
// roughly 800-900 raw units tall). The real engine's own equivalent
// field (docs/ZONE_FORMAT.md: `player+0x224`, set once in
// `GameEngine_InitLevel`, 0x10024dec) wasn't recovered -- it reads a
// per-zone/per-something constant this pass didn't trace to its source.
//
// M9 -- lighting. world/zone.cpp's Zone::BakeLighting() reproduces
// Bullseye_BakeLighting/Bullseye_PropagateLight (docs/ZONE_FORMAT.md): a
// 2D ray-cast light-propagation-with-wall-bounce from every light-source
// cell, plus each cell's .zcp lightDelta, baked once at zone load exactly
// like the real engine (not per-frame). Applied here as a **flat per-face**
// value (still a simplification vs. the real per-vertex blend across
// tiles) driving the *actual* mechanism this session corrected: each
// tile's baked light level directly selects one of `.zlu`'s 64 brightness
// rungs (zone.h's PaletteColor() comment has the full writeup) -- not a
// post-hoc RGB multiply on an already-resolved color, which is what an
// earlier pass here did and which produced near-black/banded walls once
// a real screenshot comparison caught it (a texture-index-keyed guess at
// which `.zlu` bytes to read picked the *darkest* rung for several real
// wall textures, regardless of the tile's actual light level). Entity
// models (M8) are still **not** lit by any of this -- unconditionally
// full-bright, matching M8's own scope. The ray-cast itself is also an
// approximation of the original's exact integer stepping (see zone.cpp's
// PropagateLight comment); LightLevelToBrightness()'s [0,1] float and its
// small ambient floor still exist for other callers (test/debug output)
// but the tile-grid renderer no longer goes through it -- clamping the
// raw baked value into `.zlu`'s real [4,63] rung range (RENDERER_3D.md's
// documented per-vertex scalar clamp) already keeps fully-unlit geometry
// visible without a separate stylized floor.
//
// M11 -- second wall band + .zsk room mesh. Two pieces:
//
// 1. The wall "upper band" (docs/ZONE_FORMAT.md: each wall direction can
//    fire up to two stacked draws, a floor-anchored "lower band" and a
//    ceiling-anchored "upper band", when a neighboring open tile's floor
//    or ceiling height differs from this tile's own -- a stepped ledge
//    or lintel). zone_renderer.cpp's CollectFaces() now compares each
//    open tile's edge-corner heights against its open neighbors' mirrored
//    edge (using this renderer's own corner-index convention, since the
//    real per-corner NE/SE/SW/NW mapping was never independently
//    confirmed -- see the "SIMPLIFICATION" comment there) and draws a
//    kick-wall segment (this tile's own `*_lo` index) up to the
//    neighbor's floor and/or a lintel segment (`*_hi`) down from this
//    tile's ceiling to the neighbor's ceiling, only when that neighbor's
//    edge height actually differs -- an ordinary flat corridor draws
//    nothing extra. Tiles whose neighbor is a genuine wall/out-of-bounds
//    are unchanged (single full-span wall, as before).
//
// 2. `.zsk`-baked whole-room static mesh (docs/RENDERER_3D.md's
//    `RoomGeometry_TransformAndSort` writeup) -- a real, separate render
//    step in the original engine alongside the tile-grid pipeline above,
//    not a replacement for it. `world/zone.h`'s `Zone::RoomMesh()`
//    decompresses `<zone>.zsk` and parses it as an ordinary
//    `MODEL_FORMAT.md` resource (the same parser M8's entities use,
//    factored into `world/model_archive.h`'s free `ParseModelResource()`
//    -- confirmed against real `azra.zsk`, whose header decodes exactly
//    per that spec including the `H5==H2*3` invariant). Drawn here with
//    no per-instance offset (its own vertices already sit in the zone's
//    world-unit frame, unlike `.ent`-placed entities, the simplest
//    hypothesis absent contrary evidence -- same reasoning M8 applied to
//    entity scale), via the same `SubmitModel()` helper M8's entity loop
//    was refactored to share. Same simplifications as M8: skin 0/frame 0
//    only, unlit.
//
//    **Open item, not resolved this pass**: `azra.zsk` is a small mesh
//    (30 vertices/56 faces) whose local bounding box sits almost
//    symmetric around its own origin (`x[-252,254] y[-290,40]
//    z[-254,252]` raw units, i.e. roughly a 2x2-tile footprint centered
//    on tile-grid coordinate (0,0)) -- too small to be a "whole room" in
//    the sense of covering the explorable dungeon; more likely a single
//    architectural set-piece. Whether drawing it at that raw local
//    origin with no offset is actually correct, or whether the real
//    engine applies some per-zone translation this pass didn't find
//    (RENDERER_3D.md's `ComposeTransform3x4`/actor-transform-block
//    parallel, per M8's own open item above), is **unconfirmed** --
//    tile (0,0) in `azra` turns out to be real (non-degenerate) interior
//    space bordering the grid edge, so the raw-origin placement is at
//    least *plausible*, but this wasn't independently verified with a
//    matching visual (the debug camera used to investigate,
//    `tests/m9_render_at_smoke.cpp`, has no pitch control, and this
//    corner's real floor/ceiling heights put the mesh well outside a
//    level yaw-only view from any nearby open tile).

#include <vector>

#include "graphics/backbuffer.h"
#include "render3d/camera.h"
#include "world/model_archive.h"
#include "world/zone.h"

namespace sk {

// A real .ent placement (world/zone.h's Zone::EntPlacement) already
// resolved to a models.idx archive index via entities.txt
// (world/entity_types.h) -- the renderer only needs the resolved index,
// not the typeId or category.
struct PlacedEntity {
    float x = 0, y = 0, z = 0;  // world units, same convention as Camera
    int modelArchiveIndex = -1;
};

class ZoneRenderer {
public:
    // Tile radius (in tiles) around the camera to scan for faces to
    // draw -- a stand-in for the real TileGrid_RaycastVisibility.
    int renderRadius = 20;

    // `entities`/`models` are optional (default: none drawn) so M6/M7
    // call sites and smoke tests that only care about tile-grid geometry
    // don't need to change. `models` is non-const because Model parsing
    // is lazy/cached (world/model_archive.h).
    void Render(Backbuffer& backbuffer, const Zone& zone, const Camera& camera,
                const std::vector<PlacedEntity>& entities = {}, ModelArchive* models = nullptr) const;
};

}  // namespace sk
