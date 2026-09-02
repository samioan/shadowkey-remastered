#pragma once

namespace sk {

// World units match the game's own 8.8-fixed-point convention (256
// units/tile, docs/WORLD_MODEL.md) but stored as plain floats here --
// the port targets "behaviorally faithful," not byte-exact fixed-point
// arithmetic (see the Phase 2 decision in docs/ROADMAP.md).
struct Camera {
    float x = 0.0f, y = 0.0f, z = 0.0f;  // world position, z = height
    float yaw = 0.0f;                    // radians, 0 = +x axis, increases toward +y
    float fovY = 1.0f;                   // radians, vertical field of view
};

// How far above a .ent placement's raw Z (e.g. playerStartZ) the camera's
// eye sits, in the same raw world units as everything else. NOT the real
// engine's exact constant -- traced further this session (see
// docs/RENDERER_3D.md's eye-height-constant follow-up) without landing on
// a definite answer: the real field (player-object offset 0x224 =
// player+0xa4 (Z) + CMap+0x1a, docs/ZONE_FORMAT.md) is read by
// SurfaceFace_BuildAndProject, but an exhaustive search (the ~85KB CMap
// constructor decompiled in full, plus every strh-to-offset-0x1a
// instruction in the binary cross-checked against known CMap fields)
// found no write to CMap+0x1a anywhere. Working hypothesis: CMap's
// ~85KB allocation is never bulk-zeroed but also never explicitly writes
// this field, so if EKA1 hands out zeroed heap pages the real constant
// may simply be 0 -- i.e. the real game's camera might render from
// literal floor height, not human eye height. Unconfirmed either way.
//
// This value (800) is a deliberate, documented **deviation** from that
// uncertain original, not a recovered constant: calibrated from two
// independent real 3D-model measurements (a real door model's local-
// space height span is ~1036 raw units, a real barrel's ~373 -- both
// consistent with a person roughly 800-900 raw units tall) to give a
// conventional, comfortable first-person camera height for the port,
// since a literal 0 offset -- while possibly more original-accurate --
// would put the camera awkwardly at floor level for a modern control
// scheme. Real .zcp floor/ceiling height data confirms this doesn't
// matter for *room* proportions either way: room heights of several
// thousand raw units are the norm across a whole zone (median ~6800,
// ~27 tile-widths) -- verified structurally that X/Y/Z share one uniform
// scale via SurfaceFace_BuildAndProject's rotation-matrix code, so
// that's real level geometry, not a units bug.
constexpr float kEyeHeightOffset = 800.0f;

}  // namespace sk
