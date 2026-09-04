#pragma once

// M49: the arrow. `FUN_10005730` (spawn), `FUN_10007214` (tick) and
// `FUN_10007ca8` (the touched-by-an-entity hit handler).
//
// M47 recorded a pointer -- "the ranged branch spawns a projectile via
// FUN_10005730 with entity type id 598 or 599" -- and M48, which closed the
// spell side, established that this is a **different class**: 0x16c bytes
// built by FUN_10007da4, against the spell projectile's 0x198 from
// FUN_1005f0b4. They share the actor base's position/velocity layout
// (+0x94/+0x9c/+0xa4 and +0x98/+0xa0/+0xa6) and nothing above it. This is
// that class.
//
// ---- Who shoots, and with what ----
//
// The player's ranged branch (FUN_100425bc) fires when the equipped
// weapon's `+0x1a7` is set, and `+0x1a7` is not a bow flag: the Weapon
// class's SetRange handler (FUN_1006ca90 case 2) writes the range to
// `+0x1a0` and then sets `+0x1a7` **only when the value is above 0x400**.
// The shipped corpus splits on that threshold exactly -- all 16 weapons
// with SetRange(16384) are precisely the 16 that also call
// SetBow/SetCrossbow/SetThrowingWeapon, and all 65 with SetRange(384) call
// none of the three. So "is this a ranged weapon" is a *range* test in the
// real engine, and the port's own long-standing corpus finding that the
// values are bimodal 384/16384 is what makes it work.
//
// Which projectile is then a second, separate flag: `+0x17a`
// (SetBow/SetCrossbow/SetIsLaunched) picks entities.txt typeId **599**,
// otherwise **598**. Those two ids name themselves in the shipped data:
//
//     598 176 1 !throwing      models.txt 176 = throw_dagger.bin
//     599 175 1 !arrow         models.txt 175 = arrow.bin
//
// and the corpus agrees on which weapons take which -- the 7 bows and
// crossbows against the 9 darts and throwing knives.
//
// A creature shoots when its own `+0x2d8` is not -1, which is set by the
// Monster class's `SetProjectile` binding. Twelve shipped scripts call it
// (archer.s, ace_archer.s, arrow_shade.s, deadeye.s, elite_bowman.s, ...),
// all twelve with the literal 175 -- the models.txt index, not the
// entities.txt typeId, because the engine hardcodes the type to 599 in the
// spawn call and resolves the model from *that*. So `SetProjectile`'s
// number never reaches the art; the call is load-bearing purely as "this
// creature shoots", and its argument is dead. All twelve also share
// SetAttackRange(12000), SetAttachedWeapon(225) (models.txt 225 =
// bow.bin -- the visible bow, M46's field) and SetAttackNoise(1), which is
// `barch_firebow.wav` in 21 of the 22 shipped `*_sounds.txt` -- the same
// slot the player's ranged branch plays as a bare literal.
//
// ---- What the object does ----
//
// The spawn copies the shooter's x/y verbatim, takes the shooter's yaw and
// pitch, and sets z to the player's own eye-height field (`player+0x224`)
// or, for a creature, its z plus a whole tile. Velocity is the engine's
// sine table scaled by `* 0xa00 >> 11`, i.e. **1.25 tiles a tick** -- an
// arrow is a quarter faster than a fireball, and unlike a fireball it has
// no lifetime at all. It flies until something stops it.
//
// Per tick: integrate, look up the tile (off-map keeps flying), then --
// **for the player's shot only** -- take a first look at any creature
// sharing the arrow's own tile; then four wall probes at +-0x20 on each
// axis; then a general entity sweep over three tiles. Both hit paths run
// the same to-hit roll the melee code already uses.
//
// ---- Three real quirks, reproduced ----
//
//   * Ranged damage is `rand() % damageMax` on **both** sides -- the
//     player's branch and the creature's -- so the weapon's SetDamageMin is
//     simply ignored and a 3..9 bow rolls 0..8. (The creature's code even
//     writes `(rand % max) - min + min`.) Melee, by contrast, goes through
//     the real RandomRange(min, max).
//   * A ranged hit does **not** subtract the target's armour. The melee
//     path fetches the armour rating and subtracts it before calling
//     DoDamage; the arrow's two impact paths pass the rolled damage
//     straight through.
//   * A blocked wall probe reflects the arrow's velocity *and* despawns it,
//     and then keeps probing -- so a shot can be marked dead and still hit
//     something in the same tick's sweep.
//
// ---- Not reproduced ----
//
// The multiplayer mirror (FUN_1003c45c, which replays a remote player's
// shot with zero damage and zero skill, purely as visible art), and
// `+0xc6`, a per-entity draw parameter forwarded into the rasterizer
// dispatch which is 175 for a creature's arrow and the typeId for the
// player's -- the model itself comes from `+0xc8` through entities.txt in
// both cases, so both draw the right thing regardless.

#include <cstdint>
#include <functional>
#include <vector>

#include "simkin_bindings/spell_actor.h"

namespace sk_bindings {

// entities.txt, and the models.txt entries they resolve to.
constexpr int kThrownProjectileTypeId = 598;
constexpr int kBowProjectileTypeId = 599;
constexpr int kThrownProjectileModel = 176;  // throw_dagger.bin
constexpr int kBowProjectileModel = 175;     // arrow.bin

// FUN_1006ca90 case 2's own threshold: SetRange(v) sets the ranged-path
// flag `+0x1a7` when `v > 0x400`.
constexpr int kRangedPathMinRange = 0x401;

// `FUN_1001b198(engine, 1, ...)` at the head of the player's ranged branch
// -- slot 1, `barch_firebow.wav`.
constexpr int kBowFireSound = 1;

// The spawn's velocity scale: `sine[i] * 0xa00`, stored `>> 8` and then
// shifted `>> 3` again, so `>> 11` overall. Against the table's 8.8 unit
// scale that is 320 raw units a tick -- 1.25 tiles.
constexpr int kArrowSpeedScale = 0xa00;
constexpr int kArrowSpeedShift = 11;

// A creature's arrow leaves one whole tile above its feet. The player's
// leaves at the eye-height field instead, so this does not apply there.
constexpr int kArrowSpawnZOffset = 0x100;

// The four wall probes' offset, and the extra two columns the final entity
// sweep looks in.
constexpr int kArrowWallProbe = 0x20;
constexpr int kArrowSweepOffset = 0x80;

struct ArrowProjectile {
    SpellActor* owner = nullptr;   // +0x160
    bool ownerIsPlayer = false;    // decides both the spawn z and the extra sweep
    int typeId = 0;                // +0xc8 -- 598 or 599
    int modelIndex = -1;           // what entities.txt resolves that to
    int damage = 0;                // +0x164
    int attackSkill = 0;           // +0x168, an i16
    int x = 0, y = 0, z = 0;       // +0x94 / +0x9c / +0xa4
    int vx = 0, vy = 0, vz = 0;    // +0x98 / +0xa0 / +0xa6
    int yaw = 0, pitch = 0;        // +0xb6 / +0xa8, both copied from the shooter
    bool alive = true;
};

// FUN_10005730. `yaw`/`pitch` are engine angle units (spell_projectile.h's
// EngineYawFromPortYaw / EngineAngleFromRadians produce them). `shooterZ`
// is a creature's feet; `shooterEyeZ` is the player's eye-height field --
// only one of the two is read, chosen by `ownerIsPlayer`.
ArrowProjectile SpawnArrowProjectile(SpellActor* owner, bool ownerIsPlayer, int shooterX,
                                     int shooterY, int shooterZ, int shooterEyeZ, int yaw,
                                     int pitch, int typeId, int damage, int attackSkill);

// One candidate for the per-tick sweeps. The real code walks the entity
// list hanging off the tile grid, so the test is **tile-granular**: an
// actor is a candidate when the arrow is in its tile, with no bounding box
// anywhere (the spell projectile's AABB has no counterpart here).
struct ArrowTarget {
    SpellActor* actor = nullptr;
    int x = 0, y = 0;
};

// What `Map_GetTileAt` plus FUN_1001beac answer for one world position.
// `floorHeight` is FUN_1001beac's own return: the tile's floor, or -- for a
// two-storey tile whose upper surface is solid and whose ceiling the actor
// is already above -- that ceiling, which is what you stand on up there.
// The blocked test is then simply `z < floorHeight`, so the arrow is
// stopped by geometry it cannot clear rather than by any wall flag.
struct ArrowCell {
    bool onMap = false;
    int floorHeight = 0;
};
using ArrowCellQuery = std::function<ArrowCell(int worldX, int worldY, int z)>;

struct ArrowImpact {
    bool hit = false;             // the to-hit roll landed and damage was applied
    bool struck = false;          // an actor stopped the arrow, hit or miss
    bool stopped = false;         // a wall probe or a missing tile stopped it
    SpellActor* target = nullptr; // whoever took the damage
    int damage = 0;
};

// FUN_10007214, in order: integrate; look up the tile (missing = keep
// flying, no despawn); the player's first look at its own tile; four wall
// probes; the general three-tile sweep.
ArrowImpact TickArrowProjectile(ArrowProjectile& arrow, const ArrowCellQuery& cellAt,
                                const std::vector<ArrowTarget>& targets);

}  // namespace sk_bindings
