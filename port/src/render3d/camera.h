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
// Until M84 this was **800**, a deliberate documented deviation from
// that uncertain original rather than a recovered constant: calibrated by
// eye off two real 3D-model spans (a door's ~1036 raw units, a barrel's
// ~373) to give a conventional first-person camera height, since a
// literal 0 -- while possibly more original-accurate -- would render from
// floor level. Real .zcp data confirms the choice never mattered for
// *room* proportions either way: room heights of several thousand raw
// units are the norm across a whole zone (median ~6800, ~27 tile-widths),
// and X/Y/Z were verified to share one uniform scale via
// SurfaceFace_BuildAndProject's rotation-matrix code, so that is real
// level geometry and not a units bug. What it did matter for is how big
// the thing standing in front of you looks, which is the report below.
//
// M84 -- **settled, and the value changed.** Reported: enemies are too
// small, you have to walk almost into one to attack it, and the auto-aim
// pan misbehaves when you do. The model transform is not the cause --
// `Actor3D_TransformAndSubmitModel` scales a creature by `actor+0x5e`
// and nothing else, that field is 0x100 for 1500 of the 1532 shipped
// creature placements, and this port already reproduces both. The cause
// is this constant, and 800 is too tall by half a creature.
//
// A shipped humanoid model is ~668 raw units from sole to crown
// (Bandit_Thug 668, Skelos_Undriel 668, Penelope 666 -- measured off the
// real models.huge). With the eye at 800 the player stands taller than
// every humanoid in the game and looks down on the top of its head: at
// 384 units -- the reach this port used -- a bandit's crown projects to
// screen y 157 and its feet to y 338, so **51 of its 181 pixels are on
// screen**, jammed into the bottom quarter of the frame. That is the
// "enemies are too small" report exactly, and it is also why the auto-aim
// pan gives up: centring that target needs a 66-degree downward pitch and
// camera.h's own kMaxCameraPitch stops at 49.
//
// The value is 0x200 = 512, derived twice and neither derivation is a
// preference:
//
//   1. **The creature height table.** `0x1008679c` (docs/ZONE_FORMAT.md)
//      returns 0x200 for every humanoid model in the game and 0x100 only
//      for the five short families (rats, spiders, wormmouths, stingers,
//      wolves). The player-side override `0x10006230` returns
//      `CMap+0x18`/`+0x1a`/`+0x1c` by stance byte, and the standing one is
//      the field `GameEngine_InitLevel` adds to the player's feet to get
//      its eye. The player is built on the same 144-frame humanoid rig as
//      Bandit_Thug, so its standing height is the same quantity that table
//      spells out for that body type.
//
//   2. **The melee target probes**, which are geometry, not opinion.
//      `FUN_100425bc` picks its target out of the object-ID buffer at
//      screen (88, 104), (88, 124), (88, 144), (88, 164) and (88, 184) --
//      the exact vertical centre and four points below it, never one
//      above. At this projection a probe at screen y sees world height
//      `eye - (y - 104)/104 * distance`. For all five to land on a
//      humanoid at melee distance the eye has to sit inside the creature's
//      own height band, in its upper half: at 512 and 400 units away the
//      five probes read z 512 down to 204 of a 0..604 body, every one of
//      them on the target. At 800 the centre probe -- the one that matters
//      most -- is above the creature's head at every distance. At 0 (the
//      "CMap+0x1a is never written, so EKA1 zeroing makes it 0" hypothesis
//      the note above floated) every probe is below the creature's feet
//      and melee could never acquire anything at all, which **disproves
//      that hypothesis outright.**
//
// So this is still not a constant read out of the binary -- the write to
// `CMap+0x1a` has never been found and this milestone did not find it
// either -- but it is no longer calibrated by eye. Two independent
// consequences of the shipped code both land on 0x200.
constexpr float kEyeHeightOffset = 512.0f;

}  // namespace sk
