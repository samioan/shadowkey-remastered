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
// dropped whole if any corner is behind the camera), one wall band per
// direction (the "upper band" stepped-height second segment is
// skipped), always ceiling band A (the eye-height-vs-threshold
// comparison that picks A/B is skipped), and no Bullseye lighting bake
// (unlit, raw palette/texture color).
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
