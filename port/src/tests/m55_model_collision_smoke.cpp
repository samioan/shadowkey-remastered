// M55 (the two unnamed entity scalars = the collision system) smoke test.
//
// Covers, in four parts:
//   1. the manifest columns, read straight off the real install image and
//      checked against the models they describe;
//   2. the two derived predicates -- `blocks()` (Entity+0x8c's guard) and
//      `tileStamped()` (what Entity+0x92 records);
//   3. the decompiled box overlap (`FUN_1001c48c`) and its inclusive
//      edges;
//   4. a real azra placement resolved end to end -- entities.txt typeId
//      -> model archive index -> collision box -- and a survey of how much
//      of the zone is actually solid.
//
// Real data, read directly off the install image:
//   azra_models.txt 4 = "2 64 64 bottle.bin", 7 = "2 64 256 door.bin",
//     9 = "0 1500 1500 roof.bin", 12 = "2 64 512 rail.bin",
//     25 = "0 0 0 dagger.bin", 18 = "2 128 128 rat.bin"
#include <cstdio>
#include <string>

#include "world/entity_types.h"
#include "world/model_collision.h"
#include "world/zone.h"

namespace {

bool g_Ok = true;
int g_Checks = 0;

void Check(bool cond, const std::string& what) {
    ++g_Checks;
    std::printf("%-76s %s\n", what.c_str(), cond ? "OK" : "FAILED");
    if (!cond) g_Ok = false;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    // --- Part 1: the manifest columns are a collision box. ---
    std::printf("\n-- Part 1: <zone>_models.txt columns 2-4 --\n");
    sk::ModelCollisionTable table;
    Check(table.Load(scriptRoot, "azra"), "azra_models.txt loads");

    struct Row {
        int index;
        int solid, halfX, halfY;
        const char* model;
    };
    // Chosen to span the whole range of shapes the format expresses.
    const Row kRows[] = {
        {4, 2, 64, 64, "bottle.bin -- a small prop, quarter-tile box"},
        {5, 2, 128, 128, "sbarrel.bin -- half a tile"},
        {7, 2, 64, 256, "door.bin -- thin one way, wide the other"},
        {8, 2, 256, 256, "table.bin -- a full tile each way"},
        {9, 0, 1500, 1500, "roof.bin -- huge extents, deliberately NOT solid"},
        {12, 2, 64, 512, "rail.bin -- a fence run: thin and long"},
        {18, 2, 128, 128, "rat.bin -- a creature has one too"},
        {25, 0, 0, 0, "dagger.bin -- a pickup: nothing at all"},
    };
    for (const Row& r : kRows) {
        const sk::ModelCollision& c = table.At(r.index);
        bool ok = c.solid == r.solid && c.halfExtentX == r.halfX && c.halfExtentY == r.halfY;
        std::printf("  slot %-3d -> solid %d, %d x %d   %s\n", r.index, c.solid, c.halfExtentX,
                    c.halfExtentY, r.model);
        Check(ok, std::string("  ...matches the shipped row for ") +
                       std::string(r.model).substr(0, std::string(r.model).find(' ')));
    }
    // Column 2 is an enum, not a bool: the only non-zero value the shipped
    // data ever uses is 2. That is why the save stream writes this
    // one-byte field through its i32 overload.
    Check(table.At(4).solid == 2 && table.At(5).solid == 2,
          "column 2's solid value is 2, never 1 -- an enum, not a TBool");

    // --- Part 2: the two predicates the unnamed scalars stand for. ---
    std::printf("\n-- Part 2: Entity+0x8c's guard, and Entity+0x92 --\n");
    Check(table.At(4).blocks() && table.At(7).blocks() && table.At(12).blocks(),
          "blocks(): bottle, door and rail all stop you");
    Check(!table.At(9).blocks(),
          "blocks(): roof.bin does not -- solid is 0 even with 1500-unit extents");
    Check(!table.At(25).blocks(), "blocks(): a dagger on the floor does not");
    Check(!table.At(-1).blocks() && !table.At(9999).blocks(),
          "blocks(): an unresolved placement (index -1) never blocks");
    // Entity+0x92 == "wider than the tile it stands on", set by
    // GameEngine_InitLevel on exactly this test.
    Check(!table.At(4).tileStamped() && !table.At(5).tileStamped(),
          "tileStamped(): a 64- or 128-unit prop is not -- it fits its own tile");
    Check(table.At(7).tileStamped() && table.At(8).tileStamped() && table.At(12).tileStamped(),
          "tileStamped(): a door, a table and a fence are");
    Check(sk::kTileStampThreshold == 128,
          "the threshold is 128 -- half a tile, tiles being 256 units");

    // --- Part 3: the decompiled overlap predicate. ---
    std::printf("\n-- Part 3: FUN_1001c48c, the box overlap --\n");
    sk::CollisionBox actor = sk::MakeBox(1000.0f, 1000.0f, sk::kActorHalfExtent,
                                          sk::kActorHalfExtent);
    Check(sk::kActorHalfExtent == 128.0f,
          "the mover's half-extent is 128 (0x80) on both axes, as the stance code writes");
    // A bottle (64 x 64) centred 191 units away overlaps a 128-half-extent
    // actor; at 193 it does not. The real predicate compares <=, so the
    // exact-touch case at 192 counts as a hit.
    auto bottleAt = [&](float dx) { return sk::MakeBox(1000.0f + dx, 1000.0f, 64.0f, 64.0f); };
    Check(sk::BoxesOverlap(actor, bottleAt(191.0f)), "overlaps a bottle 191 units away");
    Check(sk::BoxesOverlap(actor, bottleAt(192.0f)),
          "  ...and at exactly 192, where the boxes only touch (the real test is <=)");
    Check(!sk::BoxesOverlap(actor, bottleAt(193.0f)), "  ...but not at 193");
    // The X/Y asymmetry is real and matters: a door is 64 one way and 256
    // the other, so which side you approach from changes where it stops
    // you.
    sk::CollisionBox door = sk::MakeBox(1000.0f, 1000.0f, 64.0f, 256.0f);
    Check(!sk::BoxesOverlap(sk::MakeBox(1000.0f + 193.0f, 1000.0f, sk::kActorHalfExtent,
                                         sk::kActorHalfExtent),
                             door),
          "a door's 64-unit axis lets you within 193 units");
    Check(sk::BoxesOverlap(sk::MakeBox(1000.0f, 1000.0f + 320.0f, sk::kActorHalfExtent,
                                        sk::kActorHalfExtent),
                            door),
          "  ...while its 256-unit axis still blocks at 320");

    // --- Part 4: a real placement, end to end, and the zone survey. ---
    std::printf("\n-- Part 4: real azra placements --\n");
    sk::EntityTypeTable types;
    Check(types.Load(scriptRoot), "entities.txt loads");
    sk::Zone zone;
    Check(zone.Load(scriptRoot, "azra"), "azra loads");

    int placed = 0, solid = 0, stamped = 0, unresolved = 0;
    for (const sk::Zone::EntPlacement& e : zone.entities()) {
        ++placed;
        const sk::EntityTypeDescriptor* t = types.Lookup(e.typeId);
        if (!t) {
            ++unresolved;
            continue;
        }
        const sk::ModelCollision& c = table.At(t->modelArchiveIndex);
        if (c.blocks()) ++solid;
        if (c.blocks() && c.tileStamped()) ++stamped;
    }
    std::printf("azra: %d placements, %d solid, %d of those stamped into the tile grid, "
                "%d with no entities.txt row\n",
                placed, solid, stamped, unresolved);
    Check(placed > 0, "azra has placements");
    Check(solid > 0,
          "  ...and some of them are solid -- before M55 every one was walk-through");
    Check(stamped > 0 && stamped < solid,
          "  ...with the multi-tile ones a strict subset (what Entity+0x92 marks)");

    std::printf("azra_models.txt: %d of %d slots are solid\n", table.solidCount(),
                sk::ModelCollisionTable::kSlotCount);
    Check(table.solidCount() > 0 && table.solidCount() < sk::ModelCollisionTable::kSlotCount,
          "the manifest marks some but not all model slots solid");

    // A zone with no manifest must degrade to "nothing is solid" rather
    // than fail, exactly as the real loader bails without touching the
    // table.
    sk::ModelCollisionTable missing;
    Check(!missing.Load(scriptRoot, "no_such_zone"), "a missing manifest reports failure");
    Check(missing.solidCount() == 0, "  ...and leaves every slot non-solid rather than crashing");

    std::printf("\nm55_model_collision_smoke: %s (%d checks)\n",
                g_Ok ? "PASSED (all checks)" : "FAILED", g_Checks);
    return g_Ok ? 0 : 1;
}
