#include "simkin_bindings/arrow_projectile.h"

#include "simkin_bindings/combat.h"
#include "simkin_bindings/weapon_viewmodel.h"

namespace sk_bindings {

namespace {

// One sine-table read at the spawn's own scale. The table is the engine's
// 2048-entry 8.8 one at 0x100f4954, which M47 recovered and
// weapon_viewmodel.h exposes.
int ArrowVelocityTerm(int index2048) {
    return (SwaySine(index2048 & 0x7ff) * kArrowSpeedScale) >> kArrowSpeedShift;
}

int TileOf(int world) { return world >> 8; }

// The two tile-list sweeps both come down to "is this actor standing in
// that tile", because the real code walks the per-cell entity list rather
// than testing any bounds.
bool InTile(const ArrowTarget& target, int worldX, int worldY) {
    return TileOf(target.x) == TileOf(worldX) && TileOf(target.y) == TileOf(worldY);
}

enum class ProbeResult { Clear, Blocked, OffMap };

// One of the four +-0x20 probes.
//
// The real call is `this->vtable[0x168](this, sectorAhead, tileAhead,
// sectorBack, tileBack, 0, 0)` -- and the function behind that slot,
// FUN_1008977c, reads only the *first* pair. The "one frame back" lookups
// exist purely so that their being null can veto the probe, which is worth
// knowing because it is where the shipped code's one transposition lives
// (see the -y probe in TickArrowProjectile).
ProbeResult Probe(const ArrowProjectile& arrow, const ArrowCellQuery& cellAt, int aheadX,
                  int aheadY, int backX, int backY) {
    const ArrowCell ahead = cellAt(aheadX, aheadY, arrow.z);
    const ArrowCell back = cellAt(backX, backY, arrow.z);
    if (!ahead.onMap || !back.onMap) return ProbeResult::OffMap;
    return arrow.z < ahead.floorHeight ? ProbeResult::Blocked : ProbeResult::Clear;
}

// FUN_1004b620's own shape, reached through the port's existing melee gate:
// `attack * 256 / (attack + defense)`, rolled against rand % 0x100.
//
// Two details of the real call are worth recording rather than modelling.
// The caller computes the target's defense with FUN_1004801c and passes it
// in -- and FUN_1004b620 ignores that argument and recomputes it from the
// target's stats itself. And the two impact paths disagree about where the
// attack rating comes from: the player's first-look pass uses the value
// stamped into the arrow at spawn time (`+0x168`), while the general sweep
// re-reads the owner's live rating. They only differ if the shooter's stats
// changed mid-flight.
bool RollArrowHit(int attackSkill, const ArrowTarget& target) {
    return RollMeleeHit(attackSkill, target.actor->actorDefenseRating());
}

}  // namespace

ArrowProjectile SpawnArrowProjectile(SpellActor* owner, bool ownerIsPlayer, int shooterX,
                                     int shooterY, int shooterZ, int shooterEyeZ, int yaw,
                                     int pitch, int typeId, int damage, int attackSkill) {
    ArrowProjectile arrow;
    arrow.owner = owner;
    arrow.ownerIsPlayer = ownerIsPlayer;
    arrow.typeId = typeId;
    arrow.modelIndex = (typeId == kBowProjectileTypeId) ? kBowProjectileModel
                                                        : kThrownProjectileModel;
    arrow.damage = damage;
    arrow.attackSkill = static_cast<int16_t>(attackSkill);

    // x and y are copied verbatim -- there is no muzzle offset at all, so an
    // arrow starts inside its shooter and the first tick is what carries it
    // clear. z is the one thing that differs by shooter: the player's own
    // eye-height field, or a creature's feet plus a whole tile.
    arrow.x = shooterX;
    arrow.y = shooterY;
    arrow.z = ownerIsPlayer ? shooterEyeZ : shooterZ + kArrowSpawnZOffset;
    arrow.yaw = yaw;
    arrow.pitch = pitch;

    const int yaw16 = static_cast<int16_t>(yaw);
    const int pitch16 = static_cast<int16_t>(pitch);
    arrow.vx = ArrowVelocityTerm((yaw & 0xffff) >> 5);
    arrow.vy = ArrowVelocityTerm((yaw16 + 0x4000) >> 5);
    // The vertical term is the odd one. It is shifted `>> 6` rather than the
    // `>> 5` every other sine index in the engine uses -- including the
    // spell projectile's own pitch term, which M48 read as `>> 5` from the
    // same shaped code. So this class reads pitch as a half-scale angle.
    // Left as found; it halves an arrow's elevation rather than breaking it.
    // The store is also 16-bit before the final `>> 3`, so it truncates.
    const int vzWide = (SwaySine(((-pitch16) >> 6) & 0x7ff) * kArrowSpeedScale) >> 8;
    arrow.vz = static_cast<int16_t>(vzWide) >> 3;
    return arrow;
}

ArrowImpact TickArrowProjectile(ArrowProjectile& arrow, const ArrowCellQuery& cellAt,
                                const std::vector<ArrowTarget>& targets) {
    ArrowImpact out;
    if (!arrow.alive) return out;

    // 1. Integrate. The real code also adds the three angular rates
    //    (+0xaa pitch, +0xb4 roll, +0xb8 yaw), but the base constructor
    //    zeroes all three and the spawn never writes them -- the spawn does
    //    contain a `p->pitchRate >>= 3`, which is a no-op on zero and looks
    //    like a copy-paste of the line below it. So an arrow flies dead
    //    straight, and the rates are omitted here rather than carried.
    arrow.z += arrow.vz;
    arrow.x += arrow.vx;
    arrow.y += arrow.vy;

    // 2. The tile under the new position. `if (!tile) return;` -- off the
    //    map the arrow is neither stopped nor tested, it simply keeps going
    //    and will be caught by a probe once it comes back over the grid.
    if (!cellAt(arrow.x, arrow.y, arrow.z).onMap) return out;

    // 3. The player's shot gets a first look at whatever creature shares its
    //    own tile, before any wall test -- a pass a creature's arrow does
    //    not get. The filter is "is a monster" (vtable +0xe4), not the
    //    "monster or player" the general sweep uses, so a player's arrow can
    //    never be stopped here by another player. The arrow is spent on the
    //    first candidate whether the roll lands or not: the despawn sits
    //    outside the hit branch.
    if (arrow.ownerIsPlayer) {
        for (const ArrowTarget& target : targets) {
            if (!target.actor || target.actor == arrow.owner) continue;
            if (target.actor->isPlayerActor()) continue;
            // The real filter also rejects on a monster-side predicate at
            // vtable +0x170, which is not identified; "still alive" is what
            // the surrounding code means by it everywhere else in this port.
            if (!target.actor->actorAlive()) continue;
            if (!InTile(target, arrow.x, arrow.y)) continue;
            arrow.alive = false;
            out.struck = true;
            if (RollArrowHit(arrow.attackSkill, target)) {
                // M97: this pass hard-codes its source as `engine+0x618`'s
                // stats -- the player, which is who owns any arrow that
                // gets here. M98: and passes p5 = 0, the only arrow site
                // that does (moot for snowray: the source is the player).
                target.actor->ApplyActorDamage(arrow.damage, arrow.owner, /*ranged=*/false);
                out.hit = true;
                out.target = target.actor;
                out.damage = arrow.damage;
            }
            return out;
        }
    }

    // 4. The four wall probes. Each tests a point 0x20 ahead on one axis
    //    against the *floor height* there -- FUN_1001beac -- so what stops
    //    an arrow is geometry taller than the arrow, not a wall flag.
    //
    //    A blocked probe pushes the arrow back out of the offending cell,
    //    reflects that axis' velocity and despawns it -- and then the code
    //    carries straight on into the next probe with the mutated position,
    //    which is reproduced here. A probe whose tiles are missing despawns
    //    and returns outright.
    {
        const int probe = arrow.x - kArrowWallProbe;
        const ProbeResult r = Probe(arrow, cellAt, probe, arrow.y,
                                    (arrow.x - arrow.vx) - kArrowWallProbe, arrow.y - arrow.vy);
        if (r == ProbeResult::OffMap) {
            arrow.alive = false;
            out.stopped = true;
            return out;
        }
        if (r == ProbeResult::Blocked) {
            arrow.x = (arrow.x + 0x100) - (probe & 0xff);
            arrow.vx = -arrow.vx;
            arrow.alive = false;
            out.stopped = true;
        }
    }
    {
        const int probe = arrow.x + kArrowWallProbe;
        const ProbeResult r = Probe(arrow, cellAt, probe, arrow.y,
                                    (arrow.x - arrow.vx) + kArrowWallProbe, arrow.y - arrow.vy);
        if (r == ProbeResult::OffMap) {
            arrow.alive = false;
            out.stopped = true;
            return out;
        }
        if (r == ProbeResult::Blocked) {
            arrow.x = arrow.x - (probe & 0xff);
            arrow.vx = -arrow.vx;
            arrow.alive = false;
            out.stopped = true;
        }
    }
    {
        // The -y probe, and the one place the shipped code is transposed:
        // the "one frame back" lookup is `GetTileAt(engine, (y - vy) - 0x20,
        // x - vx)` -- the y expression in the x slot and vice versa.
        // Verified in the disassembly, not a decompiler artifact, and the
        // other three probes are all in the right order. It only decides
        // whether the probe is vetoed for being off the map, so its effect
        // is that an arrow near the grid's edge sometimes gets one probe
        // waived on the wrong axis. Reproduced.
        const int probe = arrow.y - kArrowWallProbe;
        const ProbeResult r = Probe(arrow, cellAt, arrow.x, probe,
                                    (arrow.y - arrow.vy) - kArrowWallProbe, arrow.x - arrow.vx);
        if (r == ProbeResult::OffMap) {
            arrow.alive = false;
            out.stopped = true;
            return out;
        }
        if (r == ProbeResult::Blocked) {
            arrow.y = (arrow.y + 0x100) - (probe & 0xff);
            arrow.vy = -arrow.vy;
            arrow.alive = false;
            out.stopped = true;
        }
    }
    {
        const int probe = arrow.y + kArrowWallProbe;
        const ProbeResult r = Probe(arrow, cellAt, arrow.x, probe, arrow.x - arrow.vx,
                                    (arrow.y - arrow.vy) + kArrowWallProbe);
        if (r == ProbeResult::OffMap) {
            arrow.alive = false;
            out.stopped = true;
            return out;
        }
        if (r == ProbeResult::Blocked) {
            arrow.y = arrow.y - (probe & 0xff);
            arrow.vy = -arrow.vy;
            arrow.alive = false;
            out.stopped = true;
            return out;
        }
    }

    // 5. The general sweep. The real code takes the entity list of the first
    //    of three columns that has *any* entity in it -- (x), then (x+0x80),
    //    then (x-0x80), all at the same y -- and walks only that one. This
    //    port's candidate list is actors only, so an ordinary world object
    //    sitting in the first column cannot shadow a creature in the second
    //    the way it can in the real engine; recorded, not modelled.
    const int sweepX[3] = {arrow.x, arrow.x + kArrowSweepOffset, arrow.x - kArrowSweepOffset};
    int chosen = -1;
    for (int i = 0; i < 3 && chosen < 0; ++i) {
        for (const ArrowTarget& target : targets) {
            if (!target.actor) continue;
            if (InTile(target, sweepX[i], arrow.y)) {
                chosen = i;
                break;
            }
        }
    }
    if (chosen < 0) return out;

    for (const ArrowTarget& target : targets) {
        if (!target.actor || target.actor == arrow.owner) continue;
        if (!target.actor->actorAlive()) continue;
        if (!InTile(target, sweepX[chosen], arrow.y)) continue;
        // The roll despawns the arrow either way -- but on a miss the real
        // loop carries on to the next entity in the same list and rolls
        // again, which is reproduced by simply not returning.
        arrow.alive = false;
        out.struck = true;
        const int liveSkill = arrow.owner ? arrow.owner->actorAttackRating() : arrow.attackSkill;
        if (!RollArrowHit(liveSkill, target)) continue;
        // A zero-damage arrow -- which is what the multiplayer mirror spawns
        // -- stops the flight and does nothing else.
        if (arrow.damage == 0) return out;
        // Note what is *not* here: the melee path fetches the target's
        // armour rating and subtracts it before applying damage. The arrow's
        // impact passes the rolled damage straight through.
        //
        // M97: and it names the shooter as the source -- `shooter + 0x3ac`
        // if the shooter is the player, `shooter + 0x224` otherwise -- so
        // an archer's stray arrow that kills another creature pays the
        // archer.
        //
        // M98: with p5 = 1, so an archer's arrow is one of the hits the
        // snowray ward refuses; and the arrow's damage is the bow's roll
        // alone, so the shooter's Strength term is added in the stats
        // DoDamage and nowhere earlier.
        target.actor->ApplyActorDamage(arrow.damage, arrow.owner, /*ranged=*/true);
        out.hit = true;
        out.target = target.actor;
        out.damage = arrow.damage;
        return out;
    }
    return out;
}

}  // namespace sk_bindings
