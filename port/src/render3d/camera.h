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

}  // namespace sk
