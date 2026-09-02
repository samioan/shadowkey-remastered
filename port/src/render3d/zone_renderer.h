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
// Also surfaced (not fixed) while investigating the above: real .zcp
// floor/ceiling height data near the player start implies room heights
// around 19 tile-widths tall under the existing single kTileScale
// divisor applied uniformly to X/Y/Z -- implausible for a dungeon
// corridor, and consistent with what a rendered frame looks like (floor
// visible, ceiling/opposite walls out of frame, looking like a very
// tall shaft). Whether world Z actually needs its own, different scale
// divisor from X/Y's tile-grid addressing is an open question predating
// M8, not resolved here.

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
