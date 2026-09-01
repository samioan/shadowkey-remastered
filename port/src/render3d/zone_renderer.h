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
// radius tile scan instead), no near-plane clipping (a quad is dropped
// whole if any corner is behind the camera), one wall band per direction
// (the "upper band" stepped-height second segment is skipped), always
// ceiling band A (the eye-height-vs-threshold comparison that picks A/B
// is skipped), and no Bullseye lighting bake (unlit, raw palette color).

#include "graphics/backbuffer.h"
#include "render3d/camera.h"
#include "world/zone.h"

namespace sk {

class ZoneRenderer {
public:
    // Tile radius (in tiles) around the camera to scan for faces to
    // draw -- a stand-in for the real TileGrid_RaycastVisibility.
    int renderRadius = 20;

    void Render(Backbuffer& backbuffer, const Zone& zone, const Camera& camera) const;
};

}  // namespace sk
