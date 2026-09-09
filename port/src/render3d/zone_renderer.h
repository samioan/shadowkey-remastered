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
// M8 wrote this paragraph as a list of open questions specific to entity
// rendering -- "frame 0 only", "skin 0 only", "no orientation", "no
// confirmed vertex-to-world scale factor". **All four are now closed**, the
// last two by M71: `Actor3D_TransformAndSubmitModel` (0x10056eb0) applies
// `BuildRotationMatrix3x4`'s full three-angle rotation and an 8.8
// per-instance scale from `actor+0x5e`, and both come straight off the
// `.ent` record (docs/ZONE_FORMAT.md). Frames arrived in M28, skins in M28's
// per-instance appearance work.
//
// What remains simplified here: **no distance culling** (every resolved
// entity in the zone is submitted every frame, regardless of the
// renderRadius tile scan below), and model lighting is the M35 RGB-scale
// approximation rather than the engine's own fade-rasterizer + `engine
// +0x5c4` lookup table, which was never extracted -- so a large model
// spanning several cells (a roof, a tree) reads darker than the walls
// around it.
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
// M11 -- second wall band + the .zsk mesh. Two pieces:
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
// 2. The `<zone>.zsk` mesh, drawn via the same `SubmitModel()` helper M8's
//    entity loop was refactored to share. **M11 read this as the zone's own
//    baked room geometry and M70 corrected it: it is the zone's skybox**
//    (see the M70 block at the bottom of this comment, and
//    docs/ZONE_FORMAT.md). Everything M11 established about the *format*
//    stands -- it is an ordinary `MODEL_FORMAT.md` resource, `H5==H2*3`
//    invariant and all; only what the mesh depicts, and therefore where it
//    is drawn, changed.
//
// M66 -- the crash this file carried since M56, and one behind it. Two
// corrections, both in zone_renderer.cpp with the full reasoning:
//
//  * The "no near-plane clipping" line at the top of this comment has been
//    stale since M30/M35 -- there is a clipper, and its documented output
//    bound (`count + 1`) was wrong for the quads this pipeline feeds it.
//    Floor/ceiling quads are bilinear patches (four independent corner
//    heights, non-planar for 33,363 of the 248,001 open tiles across the
//    21 zones) and M28's corner nudge can cost the projected ring its
//    convexity, so a quad could clip to six vertices into room for five
//    and smash this function's own stack frame. Quads are now split into
//    their two triangles *before* clipping, which restores the bound
//    exactly and clips the geometry that actually gets rasterized.
//
//  * A model whose skin has no area is skipped rather than clamped
//    against an inverted range. Nine of the 21 zones load a `.zsk`
//    declaring `height = 0`, which aborted a Debug build on the first frame
//    of each of them. (M70 answers *why* those nine say that, and stops
//    them reaching this guard: see ParseSkyboxResource. The guard stays --
//    it is about the rasterizer's own precondition, not about `.zsk`.)
//
// M70 -- `.zsk` is the zone's **skybox**, not its room geometry.
//
// The evidence, in the order it settles the question:
//
//  * **The engine says so.** The zone loader's debug markers around this
//    file's `WholeFile_Load` read `"InitLevel Pre skybox load"` /
//    `"InitLevel Post skybox load"`, and the object it stores the resource
//    on (`engine+0x62c`, whose `+0x54` is the model pointer) is created by
//    a line whose marker is `"Bullseye constructer Post newing Skybox"`.
//
//  * **The data says so.** Twelve of the 21 zones' skins are a fisheye sky
//    -- azra's is a night sky with two moons, drgnfld/ghstpass/snowline/
//    stouttp share a blue day sky, glaciercrawl a grey blizzard one -- and
//    the mesh is a closed 30-vertex dome (four shrinking rings around a
//    single apex) whose apex vertex maps to texel (127,129), the exact
//    centre of the 256x256 skin. The apex is the zenith.
//
//  * **The draw order says so.** In `Render3DScene` this mesh is not one
//    more thing drawn into the scene: it is the *alternative to clearing
//    the frame*. `if (!engine+0xbe0e || !skybox->model) { fill all
//    176*208 words with bgColour | 0x7fff0000 } else {
//    RoomGeometry_TransformAndSort(); }`, before any wall or actor.
//
//  * **The rasterizer says so.** `RoomFace_RasterizeTextured` writes
//    `texel | 0x7fff0000` -- the same far-depth word that flat fill writes
//    -- with no depth test, no depth compare, no lighting term and no
//    chroma-key cutout. It paints background.
//
// So this port draws it first, anchored to the camera (rotating with the
// view, never translating with it), unlit, and without touching the depth
// buffer, so every wall and actor drawn afterwards is in front of it. The
// two placement constants come from the decompiled transform: a fixed
// `+270` raw units along the model's up axis (the `(0, 270, 0)` in
// `BuildRotationMatrix3x4`) and a `0x200`/8.8 = **2x** scale, which the
// zone loader writes to `skybox+0x5e` on every load -- answering the one
// open question docs/RENDERER_3D.md left about that field.
//
// M11's "no per-instance world offset" conclusion was right about the
// original having no such field and wrong about what follows from it: with
// no world position, the mesh does not sit *anywhere* in the zone. It sits
// on the camera.

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
    // M15: local rotation around the vertical axis, radians -- only ever
    // non-zero for a live DoorExecutable instance replaying its script's
    // real AddRotationTurn() calls (main.cpp, door_executable.h). Every
    // other placed entity keeps 0, matching this file's existing "no
    // orientation" scope note above (still true for everything but doors).
    float yaw = 0.0f;
    // M71: the placement's other two orientation channels, radians, in the
    // engine's own raw sense (no port-space conversion -- they are fed
    // straight into the transcribed `BuildRotationMatrix3x4`, see
    // zone_renderer.cpp). `rotA` is the object's `+0xa8` channel
    // (`.ent`'s `rotOrScale[2]`), `rotB` its `+0xb2` (`rotOrScale[0]`).
    // Zero for 97% of shipped placements and for everything this port
    // creates at runtime, which is why yaw-only survived this long.
    float rotA = 0.0f;
    float rotB = 0.0f;
    // Real per-instance appearance, from a monster script's own
    // SetSkin()/SetScale() (simkin_bindings/monster_executable.h) -- both
    // were soft-failed and unused before, so every creature drew as skin 0
    // at 1:1. Defaults keep every other placement (props, doors, pickups,
    // the skybox) rendering exactly as before.
    int skinIndex = 0;
    float scale = 1.0f;
    // M28: which of the model's animation frames to draw. 0 (the resting
    // pose) for every static prop, and what M8-M27 drew for everything --
    // live creatures now advance it through their current clip, see
    // world/model_archive.h's AnimationClip and main.cpp's AI loop.
    int frameIndex = 0;
};

// ---- M79: the sprite billboard, `FUN_1008b25c` + `FUN_1004f91c` ----
//
// The second thing this pipeline draws that is not a mesh. The engine's
// animated-sprite entity class -- spell projectiles
// (simkin_bindings/spell_projectile.h) and scripted effects
// (simkin_bindings/effect_entity.h) are both instances of it -- has its own
// draw that submits a `global.spr` slot as a screen-aligned quad rather
// than any geometry, and this port carried the state for both since M48 and
// M63 while drawing neither. "The fireball that comes out" is that gap.
//
// `FUN_1008b25c` builds the quad in **camera space** and hands it to
// `FUN_1004f91c`:
//
//     halfW = ((i16)(sizeX * spriteW) * spriteW) >> 8      // both twice
//     halfH = ((i16)(sizeY * spriteH) * spriteH) >> 8
//     draw(x0 = cx - halfW, y0 = cy + 2*halfH,
//          x1 = cx + halfW, y1 = cy, depth = cz, ...)
//
// -- so a billboard is **bottom-anchored** at the entity's own Z and
// symmetric about it horizontally. `FUN_1004f91c` then projects the two
// corners with exactly the projection camera.h already recovered
// (`0x5800 + x*0x5800/z`, `0x6800 - y*0x6800/z`, with `engine+0x608`'s
// aspect prescale making both focal lengths 104) and culls at `depth < 5`.
//
// **The blend, and an M63 correction.** `FUN_1004f218`'s last two arguments
// are the entity's `+0x58` and `+0x138`, and Ghidra mis-typed
// `FUN_1004f91c`'s parameter list by one, which is how M63 came to read
// `+0x138` as a *size* scale. It is not: `+0x58` selects the blit mode (0 =
// straight copy, 1 = blend) and `+0x138` is the **blend level**, quantised
// by `(level + 0x1f) >> 6` into 0 = draw nothing, 1 = 25% source, 2 = 50%,
// 3 = 75%, 4 = fully opaque. So a scripted effect's "scale ramp" from
// `scaleStart` to `scaleEnd` is a **fade**, and the engine's blood spurt
// ramping `0x100 -> 0x40` fades from solid to a quarter rather than
// shrinking. A spell projectile passes `+0x58 = 0`, so a fireball is drawn
// opaque and its `0x80` never matters.
struct SpriteBillboard {
    float x = 0, y = 0, z = 0;      // world units; the quad's bottom centre
    const Sprite* sprite = nullptr;  // a decoded global.spr slot
    // `FUN_1008b25c`'s two half-extents, in world units. Computed by the
    // owning entity (simkin_bindings/effect_entity.h's EffectHalfWidth /
    // EffectHalfHeight) because the doubled sprite-dimension multiply is
    // part of that class, not of the renderer.
    int halfWidth = 0, halfHeight = 0;
    int blendMode = 0;    // entity+0x58
    int blendLevel = 0x100;  // entity+0x138
};

// `FUN_1004f91c`'s own `if ((int)depth < 5) return;`, in raw world units.
constexpr float kBillboardNearDepth = 5.0f;

// M35: a model's own forward axis is its **local +Z**, and this is the
// offset that reconciles that with `PlacedEntity::yaw`, which (like every
// other heading in this port) measures from world +X.
//
// Decompiled, not guessed. `BuildRotationMatrix3x4` (FUN_10073a70) builds
// the actor transform from three Euler angles; setting the other two to
// zero and keeping only the third leaves
//
//     row0 = [ cos, 0, sin ]      out0 = screen right
//     row1 = [   0, 1,   0 ]      out1 = screen up
//     row2 = [-sin, 0, cos ]      out2 = camera depth
//
// and `Actor3D_TransformAndSubmitModel`'s own vertex loop (FUN_10056eb0)
// applies those rows to the vertex's three int16s in file order. So the
// heading angle rotates file components 1 and 3 about component 2 --
// component 2 is up (which this port already had right), and at heading 0
// component 3 maps straight to camera *depth*: the model faces directly
// away from a camera looking the same way, i.e. **forward is local +Z**.
//
// Independently corroborated by the shipped geometry: measuring frame 0's
// bounding box, every quadruped is longest along Z (Azra_Rat 311x422x1182,
// Alpha_Wolf 238x481x893 -- the body runs along Z), every humanoid is
// *narrowest* along Z (Bandit_Thug 218x668x120 -- shoulders across X,
// depth in Z), and door.s is a slab whose thin axis is Z (519x1036x30 --
// its normal, which is exactly a door's facing).
//
// This port was rotating local +X to the heading instead, so every
// creature was drawn a quarter-turn off: they turned to track the player
// but always presented their flank.
//
// **Sign corrected in M36, from observation.** The derivation above fixes
// the axis but not which way along it a model faces, and the first
// attempt (-pi/2, reading component 3 as pointing *away* from the camera
// at heading zero) turned the reported flank into a reported back -- half
// a turn out, which is the signature of exactly this sign. Models are
// authored facing the viewer, so forward is local **-Z**: at heading zero
// component 3 grows *toward* the camera. Everything else stands.
constexpr float kModelForwardYawOffset = 1.57079632679f;  // +pi/2

// M70 -- the skybox's placement, all three values read straight out of
// `RoomGeometry_TransformAndSort` (0x10057890) and the zone loader.
//
// The engine's transform is
//
//     view = R_camera * (R_skybox * (vertex * scale) + (0, 270, 0))
//
// with **no camera position term anywhere**: the mesh turns with the view
// and never translates with it, which is the whole of what makes it a
// skybox rather than a piece of the world.
//
// * `kSkyboxUpOffset` is that literal `(0, 270, 0)` (`0x10e`), applied
//   along the model's own up axis -- local +Y, which the shipped mesh
//   confirms is the zenith: its apex vertex is the one that maps to the
//   centre of the sky texture.
// * `kSkyboxScale` is `skybox+0x5e`, which the zone loader sets to `0x200`
//   -- 2.0 in the field's 8.8 units -- immediately after every successful
//   `.zsk` load. This resolves docs/RENDERER_3D.md's open question about
//   whether that scale byte is ever non-identity: it is never anything
//   *but* 2x.
// * `kSkyboxYaw` is the fixed `-0x4000` (a quarter turn) that the same
//   function subtracts from the skybox object's own yaw -- which the
//   loader zeroes, along with its pitch and roll, on every load. This is
//   the one constant here with a purely cosmetic effect (it spins the sky
//   about the vertical axis, moving azra's moons to a different compass
//   bearing) and the one that can't be checked against a device screenshot,
//   so it is transcribed from the code rather than tuned.
// M71 -- the constant every placed model is drawn *below* its stored Z.
//
// `Actor3D_TransformAndSubmitModel` (0x10056eb0) hands its rotation builder
// the vertical translation
//
//     (actor->z /* +0xa4 */ - player->eyeZ /* +0x224 */) + -0x40
//
// for every actor that is not the player themselves (the player's own body
// model takes `+0x30` instead, on the other arm of the same `if`). So a
// placement's `.ent` Z is not where the model's origin lands: the engine
// sinks it 64 raw units -- a quarter of a tile, and at this game's scale
// (a door model is 1036 units tall, so ~2 mm/unit) about 13 cm.
//
// That is exactly the reported symptom this milestone started from: tables
// and chests floating a few inches clear of the floor. The level data puts
// most floor props' lowest vertex at or just above the tile's floor height
// -- `!table`'s single most common placement is `z == floorHeight` to the
// unit -- and the original then pushes them 64 units *into* the ground, so
// legs and bases disappear into the floor instead of hovering over it.
constexpr float kEntityDrawZOffset = -64.0f;

constexpr float kSkyboxUpOffset = 270.0f;
constexpr float kSkyboxScale = 2.0f;
constexpr float kSkyboxYaw = -1.57079632679f;  // -pi/2, i.e. -0x4000

// The flat background the frame starts as -- `Render3DScene`'s
// `engine+0x630`, the colour it fills all 176*208 scene words with when
// there is no skybox to draw. The engine's own value was never traced;
// this is the port's, in the backbuffer's RGB565. Named so a test can ask
// "is this pixel still bare background?" without hardcoding it twice.
constexpr uint16_t kBackgroundFill = PackRGB565(8, 8, 16);

class ZoneRenderer {
public:
    // Tile radius (in tiles) around the camera to scan for faces to
    // draw -- a stand-in for the real TileGrid_RaycastVisibility.
    int renderRadius = 20;

    // Draw the zone's skybox, or flat-fill the frame with a background
    // colour instead. This is the real `engine+0xbe0e`: the zone loader
    // sets it when `<zone>.zsk` loads and clears it when the load fails,
    // and `Render3DScene` picks between exactly these two arms on it.
    // Left on, like the original's, for every zone that has a `.zsk`;
    // m70_skybox_smoke turns it off to measure what the sky contributes.
    bool drawSkybox = true;

    // `entities`/`models` are optional (default: none drawn) so M6/M7
    // call sites and smoke tests that only care about tile-grid geometry
    // don't need to change. `models` is non-const because Model parsing
    // is lazy/cached (world/model_archive.h).
    void Render(Backbuffer& backbuffer, const Zone& zone, const Camera& camera,
                const std::vector<PlacedEntity>& entities = {}, ModelArchive* models = nullptr,
                const std::vector<SpriteBillboard>& billboards = {}) const;
};

}  // namespace sk
