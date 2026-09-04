#include "simkin_bindings/spell_projectile.h"

#include <cmath>

#include "simkin_bindings/weapon_viewmodel.h"

namespace sk_bindings {

namespace {

// The two rectangles FUN_1001c48c compares, laid out the way the engine
// stores them: {minX, minY, maxX, maxY} at +0x184..+0x190 on the
// projectile, and four consecutive stack words for the target.
struct Rect {
    int minX = 0, minY = 0, maxX = 0, maxY = 0;
};

bool Overlaps(const Rect& a, const Rect& b) {
    return a.minX < b.maxX && b.minX < a.maxX && a.minY < b.maxY && b.minY < a.maxY;
}

Rect BoundsOf(int x, int y, int halfWidth, int halfDepth) {
    return Rect{x - halfWidth, y - halfDepth, x + halfWidth, y + halfDepth};
}

// FUN_1005f3c8, both halves. The first is the whole reason a projectile
// exists: it builds a one-element SimKin argument array holding the target
// and calls the *spell script's* `HitTarget` through the standard scripted-
// call slot -- the UTF-16 literal at 0x100b105c. The second applies the
// projectile's own flat damage, which only the greater blaze carries.
void RunImpact(SpellProjectile& projectile, const ProjectileTarget& target) {
    if (projectile.invokeHitTarget && projectile.spell) {
        projectile.spell->InvokeHitTarget(target.script);
    }
    if (projectile.impactDamage > 0 && projectile.owner && target.actor) {
        target.actor->ApplyActorDamage(projectile.impactDamage);
    }
}

}  // namespace

namespace {
constexpr double kPi = 3.14159265358979323846;
}  // namespace

int EngineAngleFromRadians(float radians) {
    const double turns = static_cast<double>(radians) / (2.0 * kPi);
    const double units = turns * static_cast<double>(kEngineTurn);
    long long wrapped = static_cast<long long>(std::llround(units)) % kEngineTurn;
    if (wrapped < 0) wrapped += kEngineTurn;
    return static_cast<int>(wrapped);
}

int EngineYawFromPortYaw(float radians) {
    return EngineAngleFromRadians(static_cast<float>(kPi * 0.5) - radians);
}

SpellProjectile SpawnSpellProjectile(ItemExecutable* spell, SpellActor* owner, int casterX,
                                     int casterY, int casterZ, int yaw, int pitch, int sprite,
                                     int impactDamage) {
    SpellProjectile p;
    p.spell = spell;
    p.owner = owner;
    p.sprite = sprite;
    p.impactDamage = impactDamage;

    // The constructor copies the caster's x/y verbatim and lifts z:
    //
    //   this->x = caster->x;  this->y = caster->y;
    //   this->z = caster->z + 0x20;
    p.x = casterX;
    p.y = casterY;
    p.z = casterZ + kProjectileSpawnZOffset;

    // Velocity straight off the engine's sine table, unscaled -- so the
    // speed is 0x100 units a tick, exactly one tile, which is what makes
    // the twelve-tick lifetime a twelve-tile range. The vertical component
    // adds a constant 0x32 on top of the pitch term.
    //
    // The vertical component is the one part with no way to check its sign:
    // the impact test is **purely 2D** (an x/y AABB, with no height test
    // anywhere in FUN_1005f928), so nothing in the engine's own behaviour
    // depends on which way z goes. It is carried because the real
    // constructor computes it, not because it decides anything.
    const int yawIndex = (yaw >> 5) & 0x7ff;
    const int cosIndex = ((yaw + 0x4000) >> 5) & 0x7ff;
    const int pitchIndex = ((-pitch) >> 5) & 0x7ff;
    p.vx = SwaySine(yawIndex);
    p.vy = SwaySine(cosIndex);
    p.vz = SwaySine(pitchIndex) + kProjectilePitchBias;
    return p;
}

ProjectileImpact TickSpellProjectile(SpellProjectile& projectile, int mapWidthTiles,
                                     int mapHeightTiles,
                                     const std::function<bool(int, int)>& blocked,
                                     const std::vector<ProjectileTarget>& targets) {
    ProjectileImpact result;
    if (!projectile.alive) return result;

    // 1. move, then clamp into the map. The real clamps are against
    //    `engine->mapWidth * 0x100` and `engine->mapHeight * 0x100`, i.e.
    //    the world rectangle in tile-scaled units, with a floor of 0.
    projectile.x += projectile.vx;
    projectile.y += projectile.vy;
    projectile.z += projectile.vz;
    if (projectile.x < 0) projectile.x = 0;
    if (projectile.x > mapWidthTiles * 256) projectile.x = mapWidthTiles * 256;
    if (projectile.y < 0) projectile.y = 0;
    if (projectile.y > mapHeightTiles * 256) projectile.y = mapHeightTiles * 256;

    // 2. age. The increment happens before the test, and the whole
    //    collision body sits inside `if (age < 0xd)` -- so the thirteenth
    //    tick moves the projectile and then despawns it without ever
    //    looking for a target.
    ++projectile.age;
    if (projectile.age >= kProjectileLifetimeTicks) {
        projectile.alive = false;
        return result;
    }

    const int tileX = projectile.x / 256;
    const int tileY = projectile.y / 256;
    // `if (!tile) return;` -- off the map, the projectile keeps flying and
    // simply runs out its clock. Reproduced, including the fact that it
    // skips the actor sweep for that tick.
    if (tileX < 0 || tileY < 0 || tileX >= mapWidthTiles || tileY >= mapHeightTiles) return result;

    const bool wall = blocked && blocked(tileX, tileY);

    // 3. the actor sweep, which runs every tick whether or not a wall was
    //    hit. The real filter, in order: not the projectile itself, is a
    //    creature or the player, is not the caster, and -- for a creature
    //    only -- fails a monster-side predicate at vtable +0x170, which is
    //    not identified. This port uses "still alive", which is what every
    //    other targeting path here already means by it.
    const Rect bounds = BoundsOf(projectile.x, projectile.y, kProjectileRadius, kProjectileRadius);
    for (const ProjectileTarget& target : targets) {
        if (!target.actor || target.actor == projectile.owner) continue;
        if (!target.actor->actorAlive()) continue;
        if (!Overlaps(bounds, BoundsOf(target.x, target.y, target.halfWidth, target.halfDepth))) {
            continue;
        }
        RunImpact(projectile, target);
        projectile.alive = false;
        result.hit = true;
        result.target = target.actor;
        return result;
    }

    // 4. a wall ends the flight. The real code sweeps the blocked tile's
    //    neighbourhood once more first, for **creatures only** -- the
    //    player is excluded by name from the wall splash -- but the sweep
    //    is over the same AABB the pass above already covered, so anything
    //    it could find has been found. Recorded; the effect is the same
    //    despawn.
    if (wall) {
        projectile.alive = false;
        result.hitWall = true;
    }
    return result;
}

}  // namespace sk_bindings
