// M84/M85 smoke test: how big an enemy looks, and how far you can hit it.
//
// One report, two causes: "enemies are not scaled correctly, they're
// supposed to be larger. As a result, I have to move really close to
// attack them properly, which breaks the automatic camera pan system as
// well."
//
// Neither cause is the model transform. `Actor3D_TransformAndSubmitModel`
// scales a creature by `actor+0x5e` and by nothing else, and part 1 below
// shows that field is 0x100 for essentially every shipped creature, which
// this port already reproduced. The two that are wrong are the camera's
// eye height (M84) and the melee reach (M85).
//
// Six parts:
//
//  1. **Nothing scales a creature.** Every `.ent` placement of every
//     category-2 typeId across the 21 zones, read for real: the scale
//     field's distribution, and the handful of authored exceptions.
//
//  2. **The measured humanoid.** The real models.huge decoded through the
//     same parser the port uses, to get the height the projection below
//     is arguing about.
//
//  3. **M84's first derivation** -- the creature height table already in
//     world/entity_types.h says a humanoid is 0x200 tall, and the player
//     is a humanoid.
//
//  4. **M84's second derivation, and the one that settles it** -- the
//     five melee probe points are laid out for a specific eye height.
//     Reproducing the projection shows 512 puts all five on a creature's
//     body at melee range, 800 wastes the centre one at every distance,
//     and 0 misses entirely -- which disproves the "CMap+0x1a is never
//     written so it must be 0" hypothesis camera.h carried since M11.
//
//  5. **What the old eye height did to the frame** -- the arithmetic
//     behind "enemies are too small" and behind the auto-aim pan giving
//     up, both as numbers.
//
//  6. **M85's reach and gates**, and the object-ID buffer actually
//     working: a model submitted through the real renderer stamps its id
//     at the pixels it covers, an occluded one does not, and the probe
//     walk reads back what was drawn.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "graphics/backbuffer.h"
#include "render3d/camera.h"
#include "render3d/zone_renderer.h"
#include "simkin_bindings/combat.h"
#include "simkin_bindings/monster_ai.h"
#include "world/entity_types.h"
#include "world/model_archive.h"
#include "world/zone.h"

namespace {

int gChecks = 0;
int gFailures = 0;

void Check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) {
        ++gFailures;
        std::printf("  FAIL: %s\n", what.c_str());
    } else {
        std::printf("  ok:   %s\n", what.c_str());
    }
}

// The five probe points `FUN_100425bc` reads out of `engine+0x5b8`, in the
// order it reads them.
constexpr int kProbeX = 0x58;
constexpr int kProbeY[] = {0x68, 0x7c, 0x90, 0xa4, 0xb8};

// The port's own projection, reduced to the one question this test asks:
// what world height does screen row `screenY` see, `distance` units ahead
// of an eye at `eyeZ`? `sy = kHeight/2 - (h - eye)/d * focalY`, inverted.
double WorldHeightAtProbe(double eyeZ, double distance, int screenY) {
    const double focalY = sk::Backbuffer::kHeight * 0.5 / std::tan(sk::kEngineFovY * 0.5);
    const double centre = sk::Backbuffer::kHeight * 0.5;
    return eyeZ - (screenY - centre) / focalY * distance;
}

// How many of the five probes land between `feetZ` and `headZ`.
int ProbesOnBody(double eyeZ, double distance, double feetZ, double headZ) {
    int hits = 0;
    for (int y : kProbeY) {
        const double h = WorldHeightAtProbe(eyeZ, distance, y);
        if (h >= feetZ && h <= headZ) ++hits;
    }
    return hits;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot)) {
        std::printf("m84_melee_reach_smoke: FAILED to load entities.txt\n");
        return 1;
    }

    // ---------------------------------------------------------------
    std::printf("\n1. nothing in the shipped data scales a creature\n");
    // Every zone's .ent, every category-2 record, the `unkB` low halfword
    // that becomes `object+0x5e`. If the port were drawing creatures at
    // the wrong size, this is the only field in the game that could be
    // telling it to.
    // The 21 shipped `.ent` files, i.e. every playable zone.
    static const char* kZones[] = {"azra",     "broken1",  "broken2",      "crypt1",  "crypt2",
                                    "crypt3",   "delfhide", "drgnfld",      "dstar_e", "dstar_w",
                                    "erthcave", "fearfrst", "ffarena",      "ghstpass",
                                    "glaciercrawl",         "lakvan",       "lothcav", "raiders",
                                    "snowline", "stouttp",  "twilite"};
    std::map<int, int> scaleHistogram;
    int creaturePlacements = 0;
    int zonesRead = 0;
    for (const char* zoneName : kZones) {
        sk::Zone zone;
        if (!zone.Load(scriptRoot, zoneName)) continue;
        ++zonesRead;
        for (const sk::Zone::EntPlacement& p : zone.entities()) {
            const sk::EntityTypeDescriptor* d = entityTypes.Lookup(p.typeId);
            if (!d || d->category != 2) continue;
            ++creaturePlacements;
            ++scaleHistogram[p.scaleRaw];
        }
    }
    Check(zonesRead == 21, "all 21 playable zones' .ent files read");
    Check(creaturePlacements == 1532,
          "1532 creature placements in the whole game (" +
              std::to_string(creaturePlacements) + ")");
    const int identityScale = scaleHistogram.count(256) ? scaleHistogram[256] : 0;
    Check(identityScale * 100 / creaturePlacements >= 97,
          "97%+ of creature placements are scale 0x100, i.e. 1:1 (" +
              std::to_string(identityScale) + "/" + std::to_string(creaturePlacements) + ")");
    Check(scaleHistogram.size() <= 8,
          "and the authored exceptions are a handful of distinct values, not a spread");
    // The exceptions that do exist all shrink rather than grow, bar one --
    // so no reading of this field could make creatures *bigger* on
    // average, which is what the report asked for.
    int larger = 0;
    for (const auto& kv : scaleHistogram) {
        if (kv.first > 256) larger += kv.second;
    }
    Check(larger <= 2, "and at most two placements in the game are authored larger than 1:1");

    // ---------------------------------------------------------------
    std::printf("\n2. how tall a humanoid actually is\n");
    sk::ModelArchive models;
    if (!models.Load(scriptRoot)) {
        std::printf("m84_melee_reach_smoke: FAILED to load models.idx/models.huge\n");
        return 1;
    }
    // Bandit_Thug is entities.txt's model 23, and it is the rig the
    // player's own body shares (144 frames, the humanoid export).
    auto modelHeight = [&](int archiveIndex) -> int {
        const sk::Model* m = models.GetModel(archiveIndex);
        if (!m || m->vertices.empty()) return -1;
        int lo = 32767, hi = -32768;
        const int perFrame = m->vertsPerFrame > 0 ? m->vertsPerFrame
                                                   : static_cast<int>(m->vertices.size());
        for (int i = 0; i < perFrame && i < static_cast<int>(m->vertices.size()); ++i) {
            lo = (std::min)(lo, static_cast<int>(m->vertices[static_cast<size_t>(i)].y));
            hi = (std::max)(hi, static_cast<int>(m->vertices[static_cast<size_t>(i)].y));
        }
        return hi - lo;
    };
    const int thugHeight = modelHeight(23);
    Check(thugHeight > 600 && thugHeight < 720,
          "Bandit_Thug's model is about 668 raw units tall (" + std::to_string(thugHeight) + ")");
    // Local Y is up and the model stands on its own origin, so its feet
    // are at the placement's Z. That is what makes the projection below
    // legitimate.
    const sk::Model* thug = models.GetModel(23);
    Check(thug != nullptr && thug->vertsPerFrame > 0, "and it decodes with a real per-frame count");

    // ---------------------------------------------------------------
    std::printf("\n3. M84, first derivation: the creature height table\n");
    // world/entity_types.h already carries `0x1008679c` verbatim (M72).
    // Every humanoid model in it is 0x200; the player is a humanoid.
    Check(sk::MonsterCollisionHeight(23) == 0x200, "Bandit_Thug's collision height is 0x200");
    Check(sk::MonsterCollisionHeight(22) == 0x200, "Skelos_Undriel's is 0x200");
    Check(sk::MonsterCollisionHeight(20) == 0x200, "Tanyin Aldwyr's is 0x200");
    Check(sk::MonsterCollisionHeight(18) == 0x100, "and a rat's is 0x100, so the table discriminates");
    Check(static_cast<int>(sk::kEyeHeightOffset) == 0x200,
          "so the player's own standing height, and its eye offset, is 0x200 == 512");

    // ---------------------------------------------------------------
    std::printf("\n4. M84, second derivation: what the probe layout wants\n");
    // A humanoid drawn at its placement Z, less the render pipeline's own
    // -0x40 (kEntityDrawZOffset), at a representative melee distance.
    const double feetZ = 0.0 + sk::kEntityDrawZOffset;
    const double headZ = feetZ + thugHeight;
    const double meleeDistance = 400.0;

    Check(ProbesOnBody(512.0, meleeDistance, feetZ, headZ) == 5,
          "at eye 512 all five probes land on a humanoid at 400 units");
    Check(ProbesOnBody(800.0, meleeDistance, feetZ, headZ) < 5,
          "at eye 800 at least one does not");
    // The centre probe is the important one -- it is read first, and it is
    // the only one that looks straight ahead.
    Check(WorldHeightAtProbe(800.0, meleeDistance, kProbeY[0]) > headZ,
          "and specifically the centre probe is above the creature's head at eye 800");
    Check(WorldHeightAtProbe(800.0, 5.0, kProbeY[0]) > headZ,
          "which is true at any distance, since the centre probe is the eye height itself");
    // The hypothesis camera.h carried from M11 to M83: that CMap+0x1a is
    // never written, so the real camera renders from the floor. At eye 0
    // the centre probe looks along the ground and can still clip a
    // creature's ankles, but the other four are aimed *below* the floor
    // at every distance, so the fan of five degenerates into one -- which
    // is not a layout anyone would write.
    double zeroEyeHighest = -1e9;
    for (double d = 1.0; d <= 1900.0; d += 1.0) {
        for (int y : kProbeY) {
            zeroEyeHighest = (std::max)(zeroEyeHighest, WorldHeightAtProbe(0.0, d, y));
        }
    }
    Check(zeroEyeHighest <= 0.5,
          "at eye 0 no probe ever sees higher than ground level -- not one of the five can "
          "reach a creature's torso, let alone its head, at any distance");
    int zeroEyeAtReach = 0;
    for (double d = 400.0; d <= 1900.0; d += 100.0) {
        zeroEyeAtReach = (std::max)(zeroEyeAtReach, ProbesOnBody(0.0, d, feetZ, headZ));
    }
    Check(zeroEyeAtReach == 1,
          "and past 400 units only the centre probe reaches even the ankles, the other four "
          "being aimed into the floor -- so 'CMap+0x1a is 0' is out");
    // Where 512 puts them, for contrast: spread up a humanoid's body.
    const double lowest = WorldHeightAtProbe(512.0, meleeDistance, kProbeY[4]);
    const double highest = WorldHeightAtProbe(512.0, meleeDistance, kProbeY[0]);
    Check(highest > headZ * 0.7 && lowest > feetZ,
          "where at eye 512 the five span the creature's chest down to its thighs");
    // And 512 is not a lucky single point: it is the middle of a broad
    // plateau where the layout works.
    int plateau = 0;
    for (int eye = 380; eye <= 660; eye += 20) {
        if (ProbesOnBody(eye, meleeDistance, feetZ, headZ) == 5) ++plateau;
    }
    Check(plateau >= 10, "and eye heights around 512 all work, so the derivation is not brittle");

    // ---------------------------------------------------------------
    std::printf("\n5. what eye 800 did to the picture\n");
    const double focalY = sk::Backbuffer::kHeight * 0.5 / std::tan(sk::kEngineFovY * 0.5);
    const double centre = sk::Backbuffer::kHeight * 0.5;
    auto screenRow = [&](double eye, double d, double worldZ) {
        return centre - (worldZ - eye) / d * focalY;
    };
    // At the reach this port used to allow, with the eye it used to use.
    const double oldReach = 384.0;
    const double headRowOld = screenRow(800.0, oldReach, headZ);
    const double feetRowOld = screenRow(800.0, oldReach, feetZ);
    Check(headRowOld > centre,
          "at eye 800 and 384 units, a bandit's head renders BELOW the screen centre");
    Check(feetRowOld > sk::Backbuffer::kHeight,
          "and its feet are off the bottom of the frame entirely");
    const double visibleOld = sk::Backbuffer::kHeight - headRowOld;
    const double fullHeight = feetRowOld - headRowOld;
    Check(visibleOld < fullHeight * 0.45,
          "so under half of it is on screen -- the whole of 'enemies are too small'");
    // The same shot with the corrected eye.
    const double headRowNew = screenRow(512.0, oldReach, headZ);
    Check(headRowNew < centre, "at eye 512 the head is above the centre instead");
    Check(headRowNew < headRowOld, "i.e. the creature moved up the frame, not down");
    // The auto-aim pan, whose clamp is what the report noticed second.
    const double pitchNeededOld = std::atan2(800.0 - (feetZ + headZ) * 0.5, oldReach);
    const double pitchNeededNew = std::atan2(512.0 - (feetZ + headZ) * 0.5, oldReach);
    Check(pitchNeededOld > sk::kMaxCameraPitch,
          "centring that target needed more downward pitch than kMaxCameraPitch allows");
    Check(pitchNeededNew < sk::kMaxCameraPitch,
          "and at eye 512 it is comfortably inside the clamp");

    // ---------------------------------------------------------------
    std::printf("\n6. M85: the reach, the gates, and the object-ID buffer\n");
    Check(sk_bindings::kMeleeReachUnits == 0x76c,
          "the melee reach is 0x76c == 1900 units, straight off FUN_100425bc");
    Check(sk_bindings::kMeleeReachUnits > 4 * 384,
          "which is over four times the 384 this port was using");
    Check(sk_bindings::kPlayerMeleeVerticalLimit == 0x180,
          "the player's vertical gate is 0x180");
    Check(sk_bindings::kPlayerMeleeVerticalLimit != sk_bindings::kAttackVerticalLimitUnits,
          "and it is NOT the creature side's 0x200 -- two constants, two functions");
    Check(kProbeX == sk::Backbuffer::kWidth / 2,
          "the probe column 0x58 is the exact horizontal centre of a 176-wide frame");
    Check(kProbeY[0] == sk::Backbuffer::kHeight / 2,
          "the first probe row 0x68 is the exact vertical centre");
    bool descending = true;
    for (int i = 1; i < 5; ++i) {
        if (kProbeY[i] != kProbeY[i - 1] + 20) descending = false;
    }
    Check(descending, "and the other four step 20 pixels down from it, never up");
    Check(kProbeY[4] < sk::Backbuffer::kHeight, "the last one is still on screen");

    // Now the buffer itself, through the real renderer. A creature-sized
    // model dead ahead must stamp its id where it is drawn; the same model
    // behind a wall must not.
    sk::Zone zone;
    if (!zone.Load(scriptRoot, "azra")) {
        std::printf("m84_melee_reach_smoke: FAILED to load azra\n");
        return 1;
    }
    sk::Camera camera;
    camera.x = (zone.playerStartX);
    camera.y = (zone.playerStartY);
    camera.z = zone.playerStartZ + sk::kEyeHeightOffset;
    camera.yaw = 0.0f;
    camera.fovY = sk::kEngineFovY;

    sk::Backbuffer backbuffer;
    sk::ZoneRenderer renderer;
    renderer.drawSkybox = false;

    // Straight ahead along +x at this port's yaw convention.
    sk::PlacedEntity subject;
    subject.x = camera.x + 400.0f;
    subject.y = camera.y;
    subject.z = zone.playerStartZ;
    subject.modelArchiveIndex = 23;  // Bandit_Thug
    subject.objectId = 7;
    std::vector<sk::PlacedEntity> list{subject};
    renderer.Render(backbuffer, zone, camera, list, &models);

    int stamped = 0;
    for (int y = 0; y < sk::Backbuffer::kHeight; ++y) {
        for (int x = 0; x < sk::Backbuffer::kWidth; ++x) {
            if (renderer.ObjectIdAt(x, y) == 7) ++stamped;
        }
    }
    Check(stamped > 200, "a creature drawn dead ahead stamps its id over hundreds of pixels (" +
                             std::to_string(stamped) + ")");
    uint8_t probed = 0;
    for (int y : kProbeY) {
        probed = renderer.ObjectIdAt(kProbeX, y);
        if (probed != 0) break;
    }
    Check(probed == 7, "and the real probe walk finds it");

    // The same model, unchanged except for the id: id 0 is the engine's
    // "nothing here", which is what keeps props and doors unpickable.
    list[0].objectId = 0;
    renderer.Render(backbuffer, zone, camera, list, &models);
    int anyStamp = 0;
    for (int y = 0; y < sk::Backbuffer::kHeight; ++y) {
        for (int x = 0; x < sk::Backbuffer::kWidth; ++x) {
            if (renderer.ObjectIdAt(x, y) != 0) ++anyStamp;
        }
    }
    Check(anyStamp == 0, "an entity with id 0 draws normally but stamps nothing");

    // Behind the camera: drawn nowhere, so stamped nowhere. This is the
    // occlusion property the buffer gives the target search for free.
    list[0].objectId = 7;
    list[0].x = camera.x - 400.0f;
    renderer.Render(backbuffer, zone, camera, list, &models);
    uint8_t behind = 0;
    for (int y : kProbeY) {
        behind = renderer.ObjectIdAt(kProbeX, y);
        if (behind != 0) break;
    }
    Check(behind == 0, "a creature behind the camera is never picked, because it is never drawn");

    // Two creatures on the same line: the near one owns the probe, because
    // the stamp happens after the depth test.
    list[0].x = camera.x + 400.0f;
    sk::PlacedEntity far = list[0];
    far.x = camera.x + 900.0f;
    far.objectId = 9;
    std::vector<sk::PlacedEntity> pair{far, list[0]};  // far submitted first
    renderer.Render(backbuffer, zone, camera, pair, &models);
    uint8_t nearest = 0;
    for (int y : kProbeY) {
        nearest = renderer.ObjectIdAt(kProbeX, y);
        if (nearest != 0) break;
    }
    Check(nearest == 7, "and with one creature behind another, the probe finds the near one");

    std::printf("\nm84_melee_reach_smoke: %s (%d checks, %d failures)\n",
                gFailures == 0 ? "PASSED" : "FAILED", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
