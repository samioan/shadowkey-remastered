#pragma once

namespace sk {

// M71 -- the real engine's field of view, recovered rather than chosen.
//
// Both 3D pipelines end in the same two lines. `SurfaceFace_BuildAndProject`
// (0x1005d784) for tile faces and `Actor3D_TransformAndSubmitModel`
// (0x10056eb0) for models each finish a vertex with
//
//     screenX = 0x5800 + (viewRight * 0x5800) / viewDepth
//     screenY = 0x6800 - (viewUp    * 0x6800) / viewDepth
//
// in 8.8 fixed point, so `0x5800 >> 8 == 88 == 176/2` and
// `0x6800 >> 8 == 104 == 208/2`: the half-extents of the real 176x208
// screen. Taken alone that is an *anisotropic* projection -- focal 88
// across, 104 down.
//
// It does not stand alone. Right after `Render3DScene` rebuilds the camera
// matrix it scales **row 0 only** -- the screen-right row, `engine+0x5d8/
// +0x5dc/+0x5e0` -- by `(recip[176] >> 15) * 0xd0 / 0x10000`, where
// `recip[n] ~= 2^31/n` is the reciprocal table at 0x100b4954. That is
// `(65536/176) * 208 / 65536 == 208/176` to the fixed-point rounding, i.e.
// the aspect ratio. So the effective horizontal focal length is
// `88 * 208/176 == 104`, the same as the vertical one, and the projection
// is isotropic with a focal length of **104 pixels**:
//
//     fovY = 2 * atan(104 / 104) = pi/2      (90 degrees)
//     fovX = 2 * atan( 88 / 104) = ~80.5 degrees
//
// This port had `fovY = 1.2` (69 degrees, focal 152) at every call site --
// a guess that predates the projection ever being read out of the binary.
// The visible effect was a view roughly 1.46x too zoomed in: rooms looked
// cramped and props looked oversized next to a device screenshot of the
// same spot, which is exactly the symptom that sent this milestone
// looking.
constexpr float kEngineFovY = 1.57079632679f;  // pi/2

// World units match the game's own 8.8-fixed-point convention (256
// units/tile, docs/WORLD_MODEL.md) but stored as plain floats here --
// the port targets "behaviorally faithful," not byte-exact fixed-point
// arithmetic (see the Phase 2 decision in docs/ROADMAP.md).
struct Camera {
    float x = 0.0f, y = 0.0f, z = 0.0f;  // world position, z = height
    float yaw = 0.0f;                    // radians, 0 = +x axis, increases toward +y
    // Radians, positive = looking down. The real engine genuinely supports
    // this: SurfaceFace_BuildAndProject's vertex transform has two paths --
    // when `engine+0x5d4 == 0` the vertical component comes out of the full
    // 3x3 rotation matrix, otherwise it passes the raw height through
    // unrotated (a yaw-only fast path). This port had only ever implemented
    // the yaw-only path, so the real default control scheme's own LookUp /
    // LookDown actions (Key2/Key8, docs/INPUT_HANDLING.md) had nothing to
    // drive and were left unbound.
    float pitch = 0.0f;
    // Radians, vertical field of view. Defaults to the real engine's own
    // projection -- see kEngineFovY, which is a decompiled constant, not a
    // taste call.
    float fovY = kEngineFovY;
};

// Clamp for Camera::pitch. No original limit was recovered; this keeps the
// horizon in frame rather than allowing a full somersault.
constexpr float kMaxCameraPitch = 0.85f;  // radians, ~49 degrees

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
//
// M72 narrows what the field *is* without settling its value. The
// player-side actor classes override entity `Height()` (vtable slot
// 0x108) with 0x10006230, a switch on the actor's stance byte `+0x1e1`
// that returns `CMap+0x18`, `+0x1a` or `+0x1c` -- so `CMap+0x1a` is not a
// camera constant at all, it is the *standing collision height* of a
// person, which the engine then reuses as the eye offset. That makes a
// literal 0 much less likely than the note above assumed (a zero-height
// collision cylinder for every humanoid), but the write still has not been
// found, so 800 stays a calibrated stand-in. Note this is a separate
// number from the per-creature `MonsterCollisionHeight` table in
// world/entity_types.h: creatures are a different class with a different
// Height() override.
constexpr float kEyeHeightOffset = 800.0f;

}  // namespace sk
