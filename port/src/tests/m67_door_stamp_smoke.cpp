// M67 (the entity tile stamp, and why an opened door was still a wall)
// smoke test.
//
// The reported bug was "at the first room when I open the door, I can't go
// through it, it's like the door is a wall no matter if it's open or
// closed". It is a data-reading bug, not a door bug, and this test is
// built the way the finding was: from the shipped `.zmp`/`.ent`/
// `<zone>_models.txt` rather than from a fixture.
//
// The chain:
//
//  * A door is `2 64 256` in `<zone>_models.txt` -- solid, and *wider*
//    than the half tile the engine tests per-entity. Anything that wide is
//    baked into the tile grid instead and blocks like a wall
//    (world/model_collision.h, `Entity+0x92`).
//  * That bake is `FUN_10066204`, which ORs bit 2 into the second byte of
//    every cell the entity's rotated box covers. `Zone::CircleHitsWall`
//    has read that bit since M44.
//  * `SetPassable(bool)` (Object/Entity dispatch case 0x17) is what puts
//    the footprint in and takes it out again. This port did the
//    assignment and none of the stamping, so the bit -- which arrives
//    *already set* in the shipped `.zmp` -- was never cleared by anything.
//
// Parts 1-3 establish the data facts the fix rests on; Part 4 is the bug
// itself, end to end through the real `door.s`.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "simkin_bindings/door_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"
#include "world/model_collision.h"
#include "world/zone.h"

namespace {

int g_Checks = 0;
bool g_Ok = true;

void Check(bool cond, const char* what) {
    ++g_Checks;
    std::printf("  [%s] %s\n", cond ? "OK" : "FAILED", what);
    if (!cond) g_Ok = false;
}

const char* const kZones[] = {"azra",     "broken1",  "broken2",      "crypt1",  "crypt2",
                              "crypt3",   "delfhide", "drgnfld",      "dstar_e", "dstar_w",
                              "erthcave", "fearfrst", "ffarena",      "ghstpass",
                              "glaciercrawl", "lakvan", "lothcav",    "raiders", "snowline",
                              "stouttp",  "twilite"};

// A door's own placement, resolved the way main.cpp's zone-load loop
// resolves it: `.ent` typeId -> entities.txt -> model index -> the zone's
// collision manifest.
struct DoorPlacement {
    const sk::Zone::EntPlacement* ent = nullptr;
    sk::ModelCollision collision;
    std::string scriptName;
};

std::vector<DoorPlacement> ResolveDoors(const sk::Zone& zone, const sk::EntityTypeTable& types,
                                        const sk::ModelCollisionTable& collision) {
    std::vector<DoorPlacement> out;
    for (const sk::Zone::EntPlacement& e : zone.entities()) {
        const sk::EntityTypeDescriptor* desc = types.Lookup(e.typeId);
        if (!desc || desc->category != 11) continue;
        DoorPlacement d;
        d.ent = &e;
        d.collision = collision.At(desc->modelArchiveIndex);
        d.scriptName = desc->name;
        out.push_back(d);
    }
    return out;
}

// Every solid, tile-stamped placement in a zone -- the exact set
// `GameEngine_InitLevel`'s startup pass walks.
std::vector<std::pair<const sk::Zone::EntPlacement*, sk::ModelCollision>> ResolveStampers(
    const sk::Zone& zone, const sk::EntityTypeTable& types,
    const sk::ModelCollisionTable& collision) {
    std::vector<std::pair<const sk::Zone::EntPlacement*, sk::ModelCollision>> out;
    for (const sk::Zone::EntPlacement& e : zone.entities()) {
        const sk::EntityTypeDescriptor* desc = types.Lookup(e.typeId);
        if (!desc) continue;
        const sk::ModelCollision& c = collision.At(desc->modelArchiveIndex);
        if (!c.blocks() || !c.tileStamped()) continue;
        out.emplace_back(&e, c);
    }
    return out;
}

// A TileStamp that just forwards to a live zone -- the same thing
// main.cpp's LiveTileStamp does, restated here so this test does not have
// to link main.cpp.
class ZoneTileStamp : public sk_bindings::DoorExecutable::TileStamp {
public:
    explicit ZoneTileStamp(sk::Zone* zone) : m_Zone(zone) {}
    void StampEntityBox(int worldX, int worldY, int headingRaw, int halfExtentX, int halfExtentY,
                        uint8_t mask, bool set) override {
        ++calls;
        cellsWritten += m_Zone->StampEntityBox(worldX, worldY, headingRaw, halfExtentX, halfExtentY,
                                                mask, set, /*journal=*/true);
    }
    int calls = 0;
    int cellsWritten = 0;

private:
    sk::Zone* m_Zone;
};

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const char* scriptRoot =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot)) {
        std::printf("m67_door_stamp_smoke: FAILED -- could not load entities.txt\n");
        return 1;
    }

    // ---- Part 1: the block bit is *already set* in the shipped data, on
    // open floor, in every zone. That is the fact that makes "never
    // cleared" fatal rather than merely incomplete. ------------------------
    std::printf("Part 1: the shipped .zmp's block bit\n");
    int totalBlocked = 0, totalBlockedOpen = 0, zonesWithAny = 0;
    for (const char* name : kZones) {
        sk::Zone zone;
        if (!zone.Load(scriptRoot, name)) {
            std::printf("m67_door_stamp_smoke: FAILED -- could not load %s\n", name);
            return 1;
        }
        int blocked = 0, blockedOpen = 0;
        for (int ty = 0; ty < zone.height(); ++ty) {
            for (int tx = 0; tx < zone.width(); ++tx) {
                const sk::ZmpCell& c = zone.CellAt(tx, ty);
                if (!c.IsBlocked()) continue;
                ++blocked;
                if (!c.IsWall()) ++blockedOpen;
            }
        }
        totalBlocked += blocked;
        totalBlockedOpen += blockedOpen;
        if (blocked > 0) ++zonesWithAny;
    }
    std::printf("  %d cells carry the block bit across the 21 zones; %d of them are open floor\n",
                totalBlocked, totalBlockedOpen);
    Check(zonesWithAny == 21, "every zone ships cells with the block bit already set");
    Check(totalBlockedOpen > 30000,
          "and tens of thousands of them are open floor, not walls -- so the bit is not "
          "'wall', and leaving it set is not harmless");

    // ---- Part 2: re-running the stamp over the shipped grid changes
    // nothing. That is the strongest available statement that
    // Zone::StampEntityBox is a faithful transcription of FUN_10066204
    // rather than a plausible one: the walk, its half-tile step, its
    // rotation and its edge clamp all have to agree with whatever baked
    // these cells, or a disagreeing step lands on a clear cell and the
    // call reports a write. -----------------------------------------------
    std::printf("Part 2: re-stamping the shipped grid\n");
    int totalStampers = 0, totalSpurious = 0, cleanZones = 0;
    for (const char* name : kZones) {
        sk::Zone zone;
        zone.Load(scriptRoot, name);
        sk::ModelCollisionTable collision;
        collision.Load(scriptRoot, name);
        auto stampers = ResolveStampers(zone, entityTypes, collision);
        // `GameEngine_InitLevel`'s own startup pass, verbatim:
        //     if (getStamped(e)) FUN_10066204(e, 4, 0);
        int spurious = 0;
        for (const auto& s : stampers) {
            spurious += zone.StampEntityBox(s.first->x, s.first->y, s.first->yawRaw,
                                             s.second.halfExtentX, s.second.halfExtentY,
                                             sk::ZmpCell::kBlockSolid, /*set=*/true,
                                             /*journal=*/false);
        }
        totalStampers += static_cast<int>(stampers.size());
        totalSpurious += spurious;
        if (spurious == 0) ++cleanZones;
        std::printf("  %-13s %4zu solid tile-stamped placement(s), %d cell(s) not already set\n",
                    name, stampers.size(), spurious);
    }
    std::printf("  overall: %d stampers across the 21 zones set %d previously-clear cell(s)\n",
                totalStampers, totalSpurious);
    Check(totalStampers > 2000, "the 21 zones really do carry thousands of tile-stamped placements");
    Check(cleanZones >= 19,
          "at least 19 of the 21 zones re-stamp with no previously-clear cell at all");
    Check(totalSpurious <= 4,
          "and across every zone the recomputed footprints land on cells the shipped .zmp had "
          "already set -- so the baked bits ARE these stamps");

    // ---- Part 3: stamp/unstamp is an exact round trip at a fixed heading.
    std::printf("Part 3: stamp/unstamp round trip\n");
    {
        sk::Zone zone;
        zone.Load(scriptRoot, "azra");
        sk::ModelCollisionTable collision;
        collision.Load(scriptRoot, "azra");
        std::vector<uint8_t> before;
        auto snapshot = [&](std::vector<uint8_t>& out) {
            out.clear();
            for (int ty = 0; ty < zone.height(); ++ty)
                for (int tx = 0; tx < zone.width(); ++tx)
                    out.push_back(zone.CellAt(tx, ty).blockFlags);
        };
        snapshot(before);

        // Somewhere with no blocking bits anywhere near it, so the round
        // trip is measuring the walk and not colliding with a neighbour's
        // baked footprint -- see the shared-bit check at the end of this
        // part for why that distinction is real.
        int freeX = -1, freeY = -1;
        for (int ty = 4; ty + 4 < zone.height() && freeX < 0; ++ty) {
            for (int tx = 4; tx + 4 < zone.width(); ++tx) {
                bool clean = true;
                for (int dy = -4; dy <= 4 && clean; ++dy)
                    for (int dx = -4; dx <= 4 && clean; ++dx)
                        if (zone.CellAt(tx + dx, ty + dy).blockFlags != 0) clean = false;
                if (clean) {
                    freeX = tx;
                    freeY = ty;
                    break;
                }
            }
        }
        Check(freeX >= 0, "azra has open ground with no blocking bits in it to test the walk on");
        // A door-shaped box at a heading no shipped door uses, so this
        // exercises the rotation rather than an axis-aligned special case.
        const int x = freeX * 256 + 128, y = freeY * 256 + 128, heading = 0x2000;  // 45 degrees
        int set = zone.StampEntityBox(x, y, heading, 64, 256, sk::ZmpCell::kBlockSolid, true, false);
        std::vector<uint8_t> stamped;
        snapshot(stamped);
        zone.StampEntityBox(x, y, heading, 64, 256, sk::ZmpCell::kBlockSolid, false, false);
        std::vector<uint8_t> after;
        snapshot(after);
        std::printf("  a 64x256 box at tile (%d,%d) heading 0x%04x set %d cell(s)\n", freeX, freeY,
                    heading, set);
        Check(set > 0, "stamping at a fresh heading blocks cells that were clear");
        Check(stamped != before, "...so the grid really changed");
        Check(after == before, "and unstamping at the same heading restores it exactly");

        // The reason SetPassable has to stamp *inside* the call: clearing
        // at the wrong heading does not undo the set.
        zone.StampEntityBox(x, y, heading, 64, 256, sk::ZmpCell::kBlockSolid, true, false);
        zone.StampEntityBox(x, y, heading - 0x4000, 64, 256, sk::ZmpCell::kBlockSolid, false, false);
        std::vector<uint8_t> mismatched;
        snapshot(mismatched);
        Check(mismatched != before,
              "clearing at a heading the box was not stamped at leaves cells behind -- which is "
              "why door.s brackets its own turn with SetPassable");
        zone.StampEntityBox(x, y, heading - 0x4000, 64, 256, sk::ZmpCell::kBlockSolid, false, false);

        // One bit, no reference count: `tile[1] &= ~mask` does not ask who
        // set it. Two overlapping stamps therefore do not survive one of
        // them clearing, and that is the engine's own behaviour, kept.
        zone.StampEntityBox(x, y, 0, 64, 256, sk::ZmpCell::kBlockSolid, true, false);
        zone.StampEntityBox(x, y, 0, 64, 64, sk::ZmpCell::kBlockSolid, true, false);
        zone.StampEntityBox(x, y, 0, 64, 64, sk::ZmpCell::kBlockSolid, false, false);
        Check(!zone.CellAt(freeX, freeY).IsBlocked(),
              "the block bit is one bit and not a refcount: a smaller overlapping box clearing "
              "itself unblocks cells the bigger one still wants -- reproduced, not fixed");
        zone.StampEntityBox(x, y, 0, 64, 256, sk::ZmpCell::kBlockSolid, false, false);
    }

    // ---- Part 4: the bug. azra's nearest door to the player start, its
    // real door.s, and Zone::CircleHitsWall at the doorway. ----------------
    std::printf("Part 4: the reported bug, end to end\n");
    sk::Zone zone;
    zone.Load(scriptRoot, "azra");
    sk::ModelCollisionTable collision;
    collision.Load(scriptRoot, "azra");
    auto doors = ResolveDoors(zone, entityTypes, collision);
    Check(!doors.empty(), "azra has resolvable category-11 door placements");
    if (doors.empty()) return 1;

    // The one the player meets first -- the same ranking the investigation
    // used, so this test guards the actual reported case.
    const DoorPlacement* nearest = nullptr;
    double nearestDist = 0;
    for (const DoorPlacement& d : doors) {
        if (d.scriptName.empty() || d.scriptName[0] == '!') continue;  // no real script
        if (!d.collision.blocks() || !d.collision.tileStamped()) continue;
        double dx = d.ent->x - static_cast<double>(zone.playerStartX);
        double dy = d.ent->y - static_cast<double>(zone.playerStartY);
        double dist = std::sqrt(dx * dx + dy * dy);
        if (!nearest || dist < nearestDist) {
            nearest = &d;
            nearestDist = dist;
        }
    }
    Check(nearest != nullptr, "at least one is a scripted, solid, tile-stamped door");
    if (!nearest) return 1;
    std::printf("  nearest scripted door to the player start: \"%s\" (%s) at (%d,%d), %.1f units "
                "away (%.1f tiles), box %dx%d\n",
                nearest->ent->name.c_str(), nearest->scriptName.c_str(), nearest->ent->x,
                nearest->ent->y, nearestDist, nearestDist / 256.0, nearest->collision.halfExtentX,
                nearest->collision.halfExtentY);

    // Probe the *doorway*, not the door's own origin. A door sits on the
    // seam of the wall it fills, so its centre is within a player radius
    // of genuine wall tiles and would report blocked no matter what the
    // stamp did. The doorway is the open, currently-blocked cell its
    // footprint covers -- i.e. exactly the square the player is trying to
    // walk through and cannot.
    const int doorTileX = nearest->ent->x / 256;
    const int doorTileY = nearest->ent->y / 256;
    int gapX = -1, gapY = -1;
    for (int dy = -2; dy <= 2 && gapX < 0; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            int tx = doorTileX + dx, ty = doorTileY + dy;
            if (!zone.InBounds(tx, ty)) continue;
            const sk::ZmpCell& c = zone.CellAt(tx, ty);
            if (c.IsWall() || !c.IsBlocked()) continue;
            gapX = tx;
            gapY = ty;
            break;
        }
    }
    Check(gapX >= 0, "the shut door's footprint covers an open tile -- its doorway");
    if (gapX < 0) return 1;
    const float doorX = static_cast<float>(gapX * 256 + 128);
    const float doorY = static_cast<float>(gapY * 256 + 128);
    constexpr float kPlayerRadius = 48.0f;  // main.cpp's own
    std::printf("  the doorway is tile (%d,%d), centre (%.0f,%.0f)\n", gapX, gapY, doorX, doorY);
    Check(zone.CircleHitsWall(doorX, doorY, kPlayerRadius),
          "with the door shut, its doorway blocks -- the shipped bit doing its job");

    // Now the real script, attached to the real zone.
    sk::StringTable strings;
    strings.Load(std::string(scriptRoot) + "/stringtable.eng");
    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
    std::string relPath = nearest->scriptName;
    std::replace(relPath.begin(), relPath.end(), '\\', '/');
    std::string fullPath = std::string(scriptRoot) + "/" + relPath;

    ZoneTileStamp stamper(&zone);
    std::unique_ptr<sk_bindings::DoorExecutable> door;
    try {
        skExecutableContext loadCtxt(&interpreter);
        door = std::make_unique<sk_bindings::DoorExecutable>(skString(fullPath.c_str()), loadCtxt,
                                                              stack.player());
        door->SetWorldPosition(nearest->ent->x, nearest->ent->y, nearest->ent->z);
        door->AttachTileStamp(&stamper, nearest->collision.halfExtentX,
                               nearest->collision.halfExtentY, nearest->ent->yawRaw);
        skRValueArray args;
        args.append(skRValue(0));
        skRValue ret;
        skExecutableContext callCtxt(&interpreter);
        door->method(skString("Init"), args, ret, callCtxt);
    } catch (skParseException& e) {
        std::printf("m67_door_stamp_smoke: FAILED -- PARSE ERROR in %s: %s\n", fullPath.c_str(),
                    e.toString().ptr());
        return 1;
    } catch (skRuntimeException& e) {
        std::printf("m67_door_stamp_smoke: FAILED -- RUNTIME ERROR in %s: %s\n", fullPath.c_str(),
                    e.toString().ptr());
        return 1;
    }
    Check(door->isTileStamped(), "the door reports itself tile-stamped, so SetPassable will act");

    // Open it. door.s: SetPassable(true); AddRotationTurn(-64*256).
    door->InvokeOnUse();
    Check(door->passable(), "OnUse() opens it");
    std::printf("  the open cleared cells through %d SetPassable-driven stamp call(s)\n",
                stamper.calls);
    Check(stamper.calls > 0, "and that ran the tile stamp rather than only setting a bool");
    bool walkable = !zone.CircleHitsWall(doorX, doorY, kPlayerRadius);
    Check(walkable, "**the reported bug**: the opened doorway is walk-through-able");

    // Close it again: SetPassable(true); AddRotationTurn(64*256);
    // SetPassable(false) -- the footprint has to come back, at the closed
    // heading.
    door->InvokeOnUse();
    Check(!door->passable(), "a second OnUse() shuts it");
    Check(zone.CircleHitsWall(doorX, doorY, kPlayerRadius),
          "and the closed door blocks the doorway again");

    // Every touched cell is journalled, which is what makes an opened door
    // survivable across a save (Zone::tileChanges()).
    std::printf("  tile-change journal holds %zu entry/entries\n", zone.tileChanges().size());
    Check(!zone.tileChanges().empty(), "the stamp changes are journalled, as param_3=1 asks");

    std::printf("m67_door_stamp_smoke: %d checks, %s\n", g_Checks, g_Ok ? "PASSED" : "FAILED");
    return g_Ok ? 0 : 1;
}
