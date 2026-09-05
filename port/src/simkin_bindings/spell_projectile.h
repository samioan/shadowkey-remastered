#pragma once

// M48: the spell projectile -- `FUN_1005f0b4` (spawn), `FUN_1005f928`
// (tick) and `FUN_1005f3c8` (impact).
//
// This is the other half of "the rest of a real cast", and the reason the
// port's spells were hitscan. A real offensive spell does not touch its
// target when you press the key: the cast allocates a 0x198-byte entity,
// points it where the caster is looking, and lets it fly. **The impact is
// what calls the script's `HitTarget(target)`** -- which is why
// `blaze.s`'s `HitTarget[ (target) { DoAttackRoll(target, 1); } ]` exists
// at all, and why the fifteen shipped spells with no projectile have no
// `HitTarget` handler either.
//
// The clinching detail is a commented-out line in `blaze.s`:
//
//     //Level.CreateEffect( 12, 19, 1, target.GetPositionX(), ... , 128, 16, 128, 128 );
//
// and the tail of FUN_1005f928, for a spell whose typeId is 50 or 4006:
//
//     FUN_10073528(level, 0xc, 0x13, 1, x, y, z, 0x80, 0x10, 0x80, 0x80, 0);
//
// -- the same call, with the same twelve arguments, moved into the engine
// and hardcoded to blaze. The script author commented theirs out because
// the native projectile had taken it over.
//
// ---- What the real object does, per tick ----
//
//   1. advance a single-frame animation (first == last, so it never moves)
//   2. `x += vx; y += vy; z += vz`, then clamp x/y into the map
//   3. `if (++age >= 13) despawn` -- **twelve ticks of flight, and no other
//      range limit at all**
//   4. read the tile under the new position; a wall stops the projectile
//   5. sweep every actor overlapping the projectile's AABB; the first one
//      that is a creature or the player, and is not the caster, takes the
//      impact
//   6. if a wall was hit, sweep once more for creatures only (the player is
//      explicitly excluded from the wall splash), then despawn
//
// At the spawn speed of 0x100 units a tick -- exactly one tile -- that
// twelve-tick life is a hard twelve-tile range, and it is the *only* range
// a spell has. There is no SetRange on the spell side.
//
// ---- What this port does not reproduce ----
//
// The art -- **identified in M63, still not drawn.** `+0x134` is 2 for
// blaze/Blind/DoomHammer and 5 for every other projectile spell, and it
// seeds a one-frame animation range as well as being stored directly. It
// is not a models.txt index (2 is lantern.bin and 5 is sbarrel.bin); it is
// a **`global.spr` slot**, indexed straight into the engine's loaded-
// sprite table at `engine+0x4460` by the sprite entity's own draw
// (`FUN_1008b25c`). Both slots are real 32x32 sprites -- slot 2 is a
// gold-orange fireball. See simkin_bindings/effect_entity.h, which shares
// this entity class and has the whole writeup. This port still carries the
// number without drawing anything (`render3d/zone_renderer.h` has no
// billboard pass), so the projectile is simulated, not visible.
//
// Also not reproduced: the multiplayer mirror of the spawn. The blaze-only
// impact effect above is `Level.CreateEffect`, which M63 implemented --
// the cast path does not yet raise it, since the engine's version is
// spawned from the projectile's own tick rather than from a script.

#include <functional>
#include <vector>

#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/spell_actor.h"

class skiExecutable;

namespace sk_bindings {

// `+0x8e`, set by the constructor. The projectile's half-extent on both
// axes, in world units -- so a fireball is 400 units wide, a tile and a
// half.
constexpr int kProjectileRadius = 200;

// FUN_1005f928's `if (age < 0xd)`. The counter is incremented before the
// test, so a projectile gets twelve ticks of collision and dies on the
// thirteenth: just under half a second at the 25 Hz tick.
constexpr int kProjectileLifetimeTicks = 13;

// The constructor's own two adjustments to the caster's aim: the spawn
// point is a little above the caster's eye, and the vertical velocity is
// biased downward-independent so a level shot still drifts.
constexpr int kProjectileSpawnZOffset = 0x20;
constexpr int kProjectilePitchBias = 0x32;

// The engine's angle unit: a full turn is 0x10000, which the sine table
// (2048 entries, `>> 5`) indexes directly. Same table M47 recovered at
// 0x100f4954 -- see weapon_viewmodel.h's SwaySine().
constexpr int kEngineTurn = 0x10000;
int EngineAngleFromRadians(float radians);

// The engine's heading zero is **not** this port's. The spawn writes
//
//     vx = sin(yaw);  vy = cos(yaw);
//
// so an engine heading of 0 points along +y, while `sk::Camera::yaw` is the
// ordinary `(cos, sin)` with zero along +x. The conversion is therefore a
// quarter turn and a reflection, `a = pi/2 - yaw`, and getting it wrong
// would send every projectile off at ninety degrees. Use this for a
// heading; EngineAngleFromRadians is the raw unit conversion.
int EngineYawFromPortYaw(float radians);

// The way back, for anything that has to *draw* a projectile: the same
// `a = pi/2 - yaw` reflection is its own inverse, so this is
// EngineYawFromPortYaw run backwards through the unit conversion. Added by
// M49, whose arrow is the first projectile with a model to point.
float PortYawFromEngineYaw(int engineAngle);

struct SpellProjectile {
    ItemExecutable* spell = nullptr;  // +0x17c -- whose HitTarget() the impact runs
    SpellActor* owner = nullptr;      // +0x178 -- the caster, never a valid target
    int x = 0, y = 0, z = 0;          // +0x94 / +0x9c / +0xa4
    int vx = 0, vy = 0, vz = 0;       // +0x98 / +0xa0 / +0xa6
    int sprite = 0;                   // +0x134 -- see the header note on the art
    int impactDamage = 0;             // +0x182 -- only 4006 carries any
    int age = 0;                      // +0x160
    bool invokeHitTarget = true;      // +0x180, which the cast sets to 1
    bool alive = true;
};

// FUN_1005f0b4 plus the fields the cast writes straight afterwards.
// `yaw`/`pitch` are in engine angle units.
SpellProjectile SpawnSpellProjectile(ItemExecutable* spell, SpellActor* owner, int casterX,
                                     int casterY, int casterZ, int yaw, int pitch, int sprite,
                                     int impactDamage);

// One candidate target for the per-tick sweep. The real code walks the
// entity list hanging off the tile grid and asks each entry the two vtable
// predicates FUN_1002fd30 uses; this port hands in the list it already has.
struct ProjectileTarget {
    SpellActor* actor = nullptr;
    skiExecutable* script = nullptr;  // what HitTarget(target) receives
    int x = 0, y = 0;
    int halfWidth = 0;  // the target's own vtable +0x44 / +0x48
    int halfDepth = 0;
};

struct ProjectileImpact {
    bool hit = false;
    bool hitWall = false;
    SpellActor* target = nullptr;
};

// FUN_1005f928. `blocked(tileX, tileY)` is the real
//
//     tile = Map_GetTileAt(engine, x, y);
//     if (!tile) return;                    // off-map: keeps flying
//     ((tile[1] & 0x1c) == 4) || (tile[0] & 2)
//
// -- the zone's wall flag, or a locked region's second cell byte. Note the
// locked test is an *equality* against the low three bits, not a bit test,
// which is why LockZone assigns 4 rather than OR-ing it.
//
// Runs the impact itself (FUN_1005f3c8: the script's HitTarget, then the
// flat damage) and reports what happened so the caller can do its own
// bookkeeping.
ProjectileImpact TickSpellProjectile(SpellProjectile& projectile, int mapWidthTiles,
                                     int mapHeightTiles,
                                     const std::function<bool(int, int)>& blocked,
                                     const std::vector<ProjectileTarget>& targets);

}  // namespace sk_bindings
