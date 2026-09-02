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
// engine's exact constant -- that field (player-object offset 0x224,
// "playerZ + a fixed eye-height offset", docs/ZONE_FORMAT.md) exists but
// its precise value wasn't recovered from the binary. Calibrated instead
// from two independent real 3D-model measurements this session: a real
// door model's local-space height span is ~1036 raw units, a real
// barrel's is ~373 -- both consistent with a person roughly 800-900 raw
// units tall (a barrel waist/chest-high on a person, a door a bit taller
// than a person). This also matters more than it first looks: real
// .zcp floor/ceiling height data shows room heights of several thousand
// raw units are the *norm* across a whole zone (median ~6800, i.e. ~27
// "tile-widths" -- verified structurally that X/Y/Z share one uniform
// scale via SurfaceFace_BuildAndProject's rotation-matrix code, so this
// isn't a units bug), so the old 128-unit guess put the camera's eye at
// a implausibly ant's-eye height relative to the actual room scale.
constexpr float kEyeHeightOffset = 800.0f;

}  // namespace sk
