// M72 smoke test: the automatic aim-assist pitch.
//
// The reported bug was that the port panned the camera down for every
// enemy, where the original only does it for the small ones. The cause was
// that M30's version had no ground truth and approximated "small" as a
// continuous function of the drawn model's height; the real engine has a
// hard gate on a discrete per-model collision height.
//
// Three things are checked here:
//
//  1. **The height table itself**, transcribed from the jump table at
//     0x100867dc (CMonster::Height, vtable slot 0x108): 0x100 for model
//     indices 18/55/56/66/68 and 0x200 for the other 46 cases, the switch
//     default, and the descriptor-not-found path.
//
//  2. **What that means on the shipped corpus.** entities.txt is read for
//     real and every category-2 row -- every creature in the game -- is
//     classified. Named creatures are then asserted individually, so this
//     fails loudly if the table or entities.txt ever drifts: the rats,
//     spiders, wormmouths, stingers and wolves must pan and the bandits,
//     mages and the rest of the humanoids must not.
//
//  3. **The engagement geometry**, reproducing the block's own arithmetic:
//     the whole-tile distance quantisation, the +-45 degree engage window,
//     the halve-the-gap-per-tick ease and the moving-only pitch decay.
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "render3d/camera.h"
#include "render3d/zone_renderer.h"
#include "world/entity_types.h"

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

// The gate at 0x10017ca4: `cmp r0, #0x200 / bge <skip>`.
constexpr int32_t kAutoAimMaxTargetHeight = 0x200;

bool PansFor(int32_t modelArchiveIndex) {
    return sk::MonsterCollisionHeight(modelArchiveIndex) < kAutoAimMaxTargetHeight;
}

// The aim pitch exactly as the block computes it: the horizontal distance
// floored to whole tiles, then atan2 against the eye/collision-centre gap.
// Returns false when the +-45 degree window rejects it, which is also what
// happens inside one tile (quantised distance 0 -> atan2 gives +-90).
bool AimPitch(float eyeZ, float creatureFeetZ, int32_t modelIndex, float distance,
              float* outPitch) {
    const float horizontal =
        std::floor(distance / sk::kTileScale) * sk::kTileScale;
    const float centreZ =
        creatureFeetZ + static_cast<float>(sk::MonsterCollisionHeight(modelIndex)) * 0.5f;
    const float pitch = std::atan2(eyeZ - centreZ, horizontal);
    *outPitch = pitch;
    return std::fabs(pitch) <= 0.78539816339f;  // 0x2000 of 65536
}

}  // namespace

int main(int argc, char** argv) {
    const std::string scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    // ---------------------------------------------------------------
    // Part 1 -- the transcribed jump table.
    // ---------------------------------------------------------------
    std::printf("\n-- part 1: CMonster::Height (0x1008679c) --\n");
    {
        const int short_[] = {18, 55, 56, 66, 68};
        for (int m : short_) {
            Check(sk::MonsterCollisionHeight(m) == 0x100,
                  "model " + std::to_string(m) + " -> 0x100");
        }
        // The switch covers 18..68; everything inside it that is not one of
        // the five, plus everything outside it, takes the 0x200 path.
        int tall = 0;
        for (int m = 18; m <= 68; ++m) {
            if (sk::MonsterCollisionHeight(m) == 0x200) ++tall;
        }
        Check(tall == 46, "46 of the switch's 51 cases return 0x200 (got " +
                              std::to_string(tall) + ")");
        Check(sk::MonsterCollisionHeight(17) == 0x200, "below the switch -> default 0x200");
        Check(sk::MonsterCollisionHeight(69) == 0x200, "above the switch -> default 0x200");
        Check(sk::MonsterCollisionHeight(-1) == 0x200,
              "no descriptor -> 0x200 (the 0x100868c0 path)");
    }

    // ---------------------------------------------------------------
    // Part 2 -- the same table against the real entities.txt.
    // ---------------------------------------------------------------
    std::printf("\n-- part 2: every creature in the shipped entities.txt --\n");
    {
        sk::EntityTypeTable types;
        if (!types.Load(scriptRoot)) {
            std::printf("  (entities.txt not found under %s -- skipping)\n", scriptRoot.c_str());
        } else {
            // Category 2 is the creature class -- the one whose factory case
            // (0x1002aa14) runs the constructor whose vtable carries the
            // height override this milestone is about.
            int creatures = 0, pans = 0;
            std::map<int32_t, int> byModel;
            for (const auto& [typeId, desc] : types.all()) {
                if (desc.category != 2) continue;
                ++creatures;
                ++byModel[desc.modelArchiveIndex];
                if (PansFor(desc.modelArchiveIndex)) ++pans;
            }
            std::printf("  %d creature rows across %d distinct models; %d pan, %d do not\n",
                        creatures, static_cast<int>(byModel.size()), pans, creatures - pans);
            Check(creatures > 100, "entities.txt really loaded its creature rows");
            Check(pans > 0 && pans < creatures,
                  "the gate splits the corpus rather than passing or failing all of it");

            // Named checks, so this test fails if either the table or the
            // shipped data moves under it. Script paths are matched
            // case-insensitively on the tail, the way the port's own
            // resolution does.
            auto modelOf = [&](const std::string& scriptTail) -> int32_t {
                for (const auto& [typeId, desc] : types.all()) {
                    if (desc.category != 2) continue;
                    std::string n = desc.name;
                    for (char& c : n) c = static_cast<char>(std::tolower(c));
                    std::string t = scriptTail;
                    for (char& c : t) c = static_cast<char>(std::tolower(c));
                    if (n.size() >= t.size() && n.compare(n.size() - t.size(), t.size(), t) == 0) {
                        return desc.modelArchiveIndex;
                    }
                }
                return -2;
            };
            struct Case {
                const char* script;
                bool shouldPan;
            };
            const Case cases[] = {
                // The user's own examples first.
                {"azra_rat.s", true},
                {"cave_spider.s", true},
                {"bandit_brawler.s", false},
                {"bandit_thug.s", false},
                // The rest of each short-model family...
                {"field_rat.s", true},
                {"mountain_rat.s", true},
                {"spider_guardian.s", true},
                {"wormmouth.s", true},
                {"cavern_stinger.s", true},
                {"alpha_wolf.s", true},
                {"dire_wolf.s", true},
                // ...and more humanoids, which must not.
                {"bandit_mage.s", false},
                {"bandit_swordsman.s", false},
            };
            for (const Case& c : cases) {
                const int32_t model = modelOf(c.script);
                if (model == -2) {
                    Check(false, std::string("entities.txt has a row for ") + c.script);
                    continue;
                }
                Check(PansFor(model) == c.shouldPan,
                      std::string(c.script) + " (model " + std::to_string(model) + ", height 0x" +
                          (sk::MonsterCollisionHeight(model) == 0x100 ? "100" : "200") + ") " +
                          (c.shouldPan ? "pans" : "does not pan"));
            }
        }
    }

    // ---------------------------------------------------------------
    // Part 3 -- the engagement geometry.
    // ---------------------------------------------------------------
    std::printf("\n-- part 3: the engage window and the ease --\n");
    {
        const float eyeZ = sk::kEyeHeightOffset;  // creature and player on one floor
        float pitch = 0.0f;

        // Inside one tile the engine's `isqrt(d2 >> 8) * 256` floors the
        // distance to 0, atan2 returns a right angle and the +-45 degree
        // window drops the target. The camera holds instead of staring at
        // the floor.
        Check(!AimPitch(eyeZ, 0.0f, 18, 200.0f, &pitch),
              "a rat inside one tile does not engage (quantised distance 0)");

        // Far enough out that the down-angle fits the window, the assist
        // engages and looks *down* (positive pitch in this port's sense).
        bool engagedFar = AimPitch(eyeZ, 0.0f, 18, 4.0f * sk::kTileScale, &pitch);
        Check(engagedFar && pitch > 0.0f,
              "a rat four tiles out engages and pitches down (" +
                  std::to_string(pitch * 57.2957795f) + " deg)");

        // ...and the further away it is, the shallower the tilt.
        float near_ = 0.0f, far_ = 0.0f;
        AimPitch(eyeZ, 0.0f, 18, 4.0f * sk::kTileScale, &near_);
        AimPitch(eyeZ, 0.0f, 18, 9.0f * sk::kTileScale, &far_);
        Check(far_ < near_, "the tilt shallows out with distance");

        // The gate is on the height, not the geometry: a bandit at the very
        // same spot is rejected before any of this runs.
        Check(!PansFor(22) && PansFor(18),
              "at an identical position a bandit is gated out and a rat is not");

        // The ease: halve the gap per tick, clamped to 500 raw units, snap
        // inside 5. It must converge and must never overshoot.
        constexpr float kTurn = 6.28318530718f;
        constexpr float kMaxStep = 500.0f / 65536.0f * kTurn;
        constexpr float kSnap = 5.0f / 65536.0f * kTurn;
        const float goal = near_;
        float cam = 0.0f;
        int ticks = 0;
        bool overshot = false;
        for (; ticks < 400; ++ticks) {
            const float delta = goal - cam;
            if (std::fabs(delta) < kSnap) {
                cam = goal;
                break;
            }
            const float step = std::fmax(-kMaxStep, std::fmin(kMaxStep, delta * 0.5f));
            cam += step;
            if ((goal > 0.0f && cam > goal + 1e-4f) || (goal < 0.0f && cam < goal - 1e-4f)) {
                overshot = true;
            }
        }
        Check(!overshot, "the ease never overshoots its target pitch");
        Check(cam == goal, "the ease reaches and snaps to the target pitch");
        Check(ticks > 1 && ticks < 200,
              "it takes " + std::to_string(ticks) + " ticks to get there (a pan, not a cut)");
        // 500 raw units per tick at the engine's own 25 Hz.
        Check(kMaxStep * 25.0f < 1.21f && kMaxStep * 25.0f > 1.19f,
              "the clamp caps the pan at ~1.2 rad/second");

        // The decay path, flag and all: `player+0xae` is set while the
        // assist is engaged, cleared only on a tick where the player moved
        // with no target, and the pitch decays by `v -= v * 20/256` only
        // while it is clear.
        auto settle = [](float startPitch, bool moving, int ticks) {
            float p = startPitch;
            bool pitchHeld = true;  // as if the assist had just let go
            for (int i = 0; i < ticks; ++i) {
                if (moving) pitchHeld = false;
                if (!pitchHeld) p -= p * (20.0f / 256.0f);
            }
            return p;
        };
        Check(std::fabs(settle(0.7f, /*moving=*/true, 100)) < 0.001f,
              "moving with no target levels the camera off");
        Check(settle(0.7f, /*moving=*/false, 100) == 0.7f,
              "standing still holds the pan exactly where the assist left it");
    }

    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
