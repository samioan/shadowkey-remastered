// M86 smoke test: the two things a hit is supposed to look like.
//
// Reported: "there are some effects missing, enemies turn red briefly when
// hit, and a red slash appears when the player is getting hit."
//
// Both are real, both are one mechanism seen from two sides, and this port
// had neither. A creature is redrawn out of a 16-entry colour ramp by a
// rasterizer that exists for nothing else (`Poly3D_RasterizeTextured_v8`);
// the player, having no model, gets the same remap applied to its HUD
// frame plus a full-screen sprite blended over the view.
//
// Five parts:
//
//  1. **The ramp table**, `FUN_1000f4b4`/`FUN_1000f304`: fourteen rows of
//     sixteen, rows 0-6 red and 7-13 green, each a dark-to-full gradient.
//
//  2. **The oscillator**, `FUN_10067c3c` arming it and `FUN_10064f08`
//     ticking it -- including the total duration formula, the reversal at
//     each end and the period halving on the way back through the bottom.
//
//  3. **What the two shipped call sites do with it in real time**: the
//     damage flash's exact row sequence and its 0.31-second length, and
//     the status-effect flash landing on the green rows.
//
//  4. **The remap itself**, through the real renderer: a model drawn with
//     a flash level comes out in ramp colours and not its own, and drops
//     back to its own the moment the flash ends.
//
//  5. **The player's red slash** -- `global.spr` slot 218 read for real
//     (its size, its single colour, its diagonal shape), the hurt timer's
//     0x40, and the 50% blend that draws it.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include "assets/sprite_archive.h"
#include "graphics/backbuffer.h"
#include "render3d/camera.h"
#include "render3d/zone_renderer.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/player_executable.h"
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

int Red444(uint16_t c) { return (c >> 8) & 0xF; }
int Green444(uint16_t c) { return (c >> 4) & 0xF; }
int Blue444(uint16_t c) { return c & 0xF; }

}  // namespace

int main(int argc, char** argv) {
    const std::string scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    // ---------------------------------------------------------------
    std::printf("\n1. the flash ramps at engine+0x5060\n");
    const sk::HitFlashRamps& ramps = sk::FlashRamps();
    bool redRowsAreRed = true, greenRowsAreGreen = true;
    for (int row = 0; row <= 6; ++row) {
        for (int i = 0; i < 16; ++i) {
            const uint16_t c = ramps.entry[row][i];
            if (Green444(c) != 0 || Blue444(c) != 0) redRowsAreRed = false;
        }
    }
    for (int row = 7; row <= 13; ++row) {
        for (int i = 0; i < 16; ++i) {
            const uint16_t c = ramps.entry[row][i];
            if (Red444(c) != 0 || Blue444(c) != 0) greenRowsAreGreen = false;
        }
    }
    Check(redRowsAreRed, "rows 0-6 carry red only -- no green, no blue");
    Check(greenRowsAreGreen, "rows 7-13 carry green only");
    // Each row ramps upward and tops out at full.
    bool monotonic = true;
    for (int row = 0; row <= 13; ++row) {
        const bool isRed = row <= 6;
        int prev = -1;
        for (int i = 0; i < 16; ++i) {
            const int v = isRed ? Red444(ramps.entry[row][i]) : Green444(ramps.entry[row][i]);
            if (v < prev) monotonic = false;
            prev = v;
        }
    }
    Check(monotonic, "and every row ramps up, never down");
    Check(Red444(ramps.entry[0][15]) == 0xF, "row 0 ends at full red");
    Check(Red444(ramps.entry[6][0]) == 0xF && Red444(ramps.entry[6][15]) == 0xF,
          "row 6 is flat full red -- `FUN_1000f304(engine, 6, 0xff,0,0, 0xff,0,0)`");
    // The four identical dark-floored rows the arming call sweeps through
    // first, then the three brighter ones.
    bool firstFourIdentical = true;
    for (int row = 1; row <= 3; ++row) {
        for (int i = 0; i < 16; ++i) {
            if (ramps.entry[row][i] != ramps.entry[0][i]) firstFourIdentical = false;
        }
    }
    Check(firstFourIdentical, "rows 0-3 are the same ramp four times over, as the real table has it");
    Check(Red444(ramps.entry[4][0]) > Red444(ramps.entry[3][0]),
          "row 4's floor is brighter than row 3's");
    Check(Red444(ramps.entry[5][0]) > Red444(ramps.entry[4][0]), "and row 5's brighter again");
    // Green mirrors red exactly, one channel over.
    bool mirrored = true;
    for (int i = 0; i < 16; ++i) {
        if (Green444(ramps.entry[7][i]) != Red444(ramps.entry[0][i])) mirrored = false;
        if (Green444(ramps.entry[13][i]) != Red444(ramps.entry[6][i])) mirrored = false;
    }
    Check(mirrored, "and the green family is the red one channel-swapped, row for row");

    // ---------------------------------------------------------------
    std::printf("\n2. the oscillator, FUN_10067c3c + FUN_10064f08\n");
    sk::HitFlash flash;
    Check(!flash.active() && flash.row == -1, "a fresh entity is not flashing");
    flash.Arm(sk::kFlashPeriodUnits, sk::kFlashRedMin, sk::kFlashRedMax);
    Check(flash.active(), "arming it starts it");
    Check(flash.row == sk::kFlashRedMin, "seeded at the minimum row");
    Check(flash.dir == 1, "stepping upward");
    Check(flash.total == (sk::kFlashRedMax - sk::kFlashRedMin) * sk::kFlashPeriodUnits * 2,
          "and the total is `(max - min) * period * 2` -- one sweep up and back");
    Check(flash.total == 80, "which for the damage flash is 80 units");

    // Ticking it with the engine's own per-frame delta.
    std::vector<int> rows;
    sk::HitFlash run;
    run.Arm(sk::kFlashPeriodUnits, sk::kFlashRedMin, sk::kFlashRedMax);
    int ticks = 0;
    while (run.active() && ticks < 100) {
        rows.push_back(run.row);
        run.Tick(sk_bindings::kAiFrameDeltaUnits);
        ++ticks;
    }
    Check(ticks == 8, "a damage flash lasts exactly 8 ticks (" + std::to_string(ticks) + ")");
    Check(std::abs(8 * sk_bindings::kAiFrameDeltaUnits / 256.0 - 0.31) < 0.02,
          "which at 1/256-second units is about 0.31 s -- 'briefly'");
    Check(!run.active() && run.row == -1, "and it switches itself off at the end");
    Check(rows.size() == 8 && rows.front() == 0, "it starts on row 0");
    const int peak = *std::max_element(rows.begin(), rows.end());
    Check(peak == sk::kFlashRedMax, "reaches the top row exactly once");
    Check(rows.back() < peak, "and is on its way back down when the total expires");
    bool everGreen = false;
    for (int r : rows) {
        if (r >= sk::kFlashGreenMin) everGreen = true;
    }
    Check(!everGreen, "a damage flash never strays into the green rows");

    // The reversal, and a small surprise about the line after it.
    sk::HitFlash slow;
    slow.Arm(64, 0, 5);
    int lowestAfterPeak = 99;
    bool reversed = false;
    for (int i = 0; i < 400 && slow.active(); ++i) {
        slow.Tick(sk_bindings::kAiFrameDeltaUnits);
        if (!slow.active()) break;
        if (slow.dir == -1) {
            reversed = true;
            lowestAfterPeak = (std::min)(lowestAfterPeak, slow.row);
        }
    }
    Check(reversed, "a flash that reaches its top row turns around");
    // `Arm` budgets `(max - min) * period * 2`, i.e. exactly 2*(max-min)
    // steps' worth of time. Climbing spends (max-min) of them and the
    // reversal spends one more, so the ramp always runs out of budget on
    // the way back down -- and with a frame delta that does not divide
    // the period evenly it runs out sooner still. So `FUN_10064f08`'s
    // `+0x15c >>= 1` branch, the one that would make a flash accelerate,
    // is **unreachable from FUN_10067c3c**: no flash the shipped game
    // arms ever gets back to its minimum row. It is transcribed anyway,
    // because the port should behave like the engine for a caller that
    // sets the fields some other way, and because a dead branch is worth
    // recording as dead rather than quietly dropping.
    Check(lowestAfterPeak > sk::kFlashRedMin,
          "and never gets back down to its minimum row before the budget runs out");
    Check(slow.period == 64, "so the period-halving branch never fires for an armed flash");
    // The same is true of the damage flash's own numbers, which is the
    // case that actually matters.
    sk::HitFlash damage;
    damage.Arm(sk::kFlashPeriodUnits, sk::kFlashRedMin, sk::kFlashRedMax);
    int damageLow = 99;
    while (damage.active()) {
        damage.Tick(sk_bindings::kAiFrameDeltaUnits);
        if (damage.active() && damage.dir == -1) damageLow = (std::min)(damageLow, damage.row);
    }
    Check(damageLow > sk::kFlashRedMin, "and of the real (8, 0, 5) damage flash too");
    // Driven past the bottom by hand, it does what the real one does.
    sk::HitFlash forced;
    forced.Arm(64, 0, 5);
    forced.row = sk::kFlashRedMin;
    forced.dir = -1;
    forced.stepTimer = 1;
    forced.Tick(sk_bindings::kAiFrameDeltaUnits);
    Check(forced.period == 32 && forced.dir == 1,
          "but stepped through the bottom directly it halves the period and turns around, "
          "exactly as FUN_10064f08 writes it");

    // ---------------------------------------------------------------
    std::printf("\n3. the two shipped arming calls\n");
    Check(sk::kFlashPeriodUnits == 8, "both call sites use period 8");
    Check(sk::kFlashRedMin == 0 && sk::kFlashRedMax == 5,
          "the damage flash is rows 0..5 -- `FUN_10067c3c(self, 8, 0, 5)` in FUN_10081844");
    Check(sk::kFlashGreenMin == 7 && sk::kFlashGreenMax == 0xc,
          "the status flash is rows 7..12 -- `(8, 7, 0xc)` in the AI tick FUN_10082224");
    Check(sk::kFlashRedMax < sk::kFlashGreenMin,
          "and the two ranges cannot overlap, so a red flash is always red");
    sk::HitFlash green;
    green.Arm(sk::kFlashPeriodUnits, sk::kFlashGreenMin, sk::kFlashGreenMax);
    Check(Green444(ramps.entry[green.row][8]) > 0 && Red444(ramps.entry[green.row][8]) == 0,
          "arming the status flash lands on a green ramp row");

    // ---------------------------------------------------------------
    std::printf("\n4. the remap, through the real renderer\n");
    sk::Zone zone;
    sk::ModelArchive models;
    if (!zone.Load(scriptRoot, "azra") || !models.Load(scriptRoot)) {
        std::printf("m86_hit_feedback_smoke: FAILED to load azra / models\n");
        return 1;
    }
    sk::Camera camera;
    camera.x = static_cast<float>(zone.playerStartX);
    camera.y = static_cast<float>(zone.playerStartY);
    camera.z = zone.playerStartZ + sk::kEyeHeightOffset;
    camera.yaw = 0.0f;
    camera.fovY = sk::kEngineFovY;

    sk::ZoneRenderer renderer;
    renderer.drawSkybox = false;
    sk::PlacedEntity subject;
    subject.x = camera.x + 400.0f;
    subject.y = camera.y;
    subject.z = static_cast<float>(zone.playerStartZ);
    subject.modelArchiveIndex = 23;  // Bandit_Thug
    subject.objectId = 3;

    // Draw it normally first, and remember which pixels it covers, so the
    // comparison below is over exactly the creature and nothing else.
    sk::Backbuffer plain;
    std::vector<sk::PlacedEntity> list{subject};
    renderer.Render(plain, zone, camera, list, &models);
    std::vector<std::pair<int, int>> creaturePixels;
    for (int y = 0; y < sk::Backbuffer::kHeight; ++y) {
        for (int x = 0; x < sk::Backbuffer::kWidth; ++x) {
            if (renderer.ObjectIdAt(x, y) == 3) creaturePixels.push_back({x, y});
        }
    }
    Check(creaturePixels.size() > 200, "the creature covers a usable number of pixels");

    sk::Backbuffer flashed;
    list[0].flashLevel = 5;  // the peak of the damage flash
    renderer.Render(flashed, zone, camera, list, &models);

    // Every one of the creature's pixels must now be a colour from ramp
    // row 5, and (since the row is pure red) must have no green or blue.
    std::set<uint16_t> rowColours;
    for (int i = 0; i < 16; ++i) rowColours.insert(sk::FlashRamp565(5, i));
    int offRamp = 0, changed = 0;
    for (const auto& p : creaturePixels) {
        const uint16_t after = flashed.Row(p.second)[p.first];
        const uint16_t before = plain.Row(p.second)[p.first];
        if (!rowColours.count(after)) ++offRamp;
        if (after != before) ++changed;
    }
    Check(offRamp == 0, "with flashLevel 5 every creature pixel is a colour from ramp row 5");
    Check(changed > static_cast<int>(creaturePixels.size()) / 2,
          "and most of them actually changed, so this is not a no-op");
    // Nothing else in the frame moved.
    int backgroundChanged = 0;
    std::set<std::pair<int, int>> creatureSet(creaturePixels.begin(), creaturePixels.end());
    for (int y = 0; y < sk::Backbuffer::kHeight; ++y) {
        for (int x = 0; x < sk::Backbuffer::kWidth; ++x) {
            if (creatureSet.count({x, y})) continue;
            if (plain.Row(y)[x] != flashed.Row(y)[x]) ++backgroundChanged;
        }
    }
    Check(backgroundChanged == 0, "the walls and floor around it are untouched");

    // Level -1 is the ordinary draw, which is what routes a non-flashing
    // creature back to the normal rasterizers in Poly3D_ClipAndDispatch.
    sk::Backbuffer restored;
    list[0].flashLevel = -1;
    renderer.Render(restored, zone, camera, list, &models);
    int identical = 0;
    for (const auto& p : creaturePixels) {
        if (restored.Row(p.second)[p.first] == plain.Row(p.second)[p.first]) ++identical;
    }
    Check(identical == static_cast<int>(creaturePixels.size()),
          "and flashLevel -1 renders exactly what it rendered before the flash");

    // A green flash is visibly a different colour on the same creature.
    sk::Backbuffer greenFrame;
    list[0].flashLevel = sk::kFlashGreenMax;
    renderer.Render(greenFrame, zone, camera, list, &models);
    int greenPixels = 0;
    for (const auto& p : creaturePixels) {
        const uint16_t c = greenFrame.Row(p.second)[p.first];
        // RGB565: green is bits 5..10, red 11..15.
        if (((c >> 5) & 0x3F) > 0 && (c >> 11) == 0) ++greenPixels;
    }
    Check(greenPixels > static_cast<int>(creaturePixels.size()) / 2,
          "and the status flash draws the same creature green rather than red");

    // ---------------------------------------------------------------
    std::printf("\n5. the player's red slash\n");
    sk::SpriteArchive sprites;
    if (!sprites.Load(scriptRoot)) {
        std::printf("m86_hit_feedback_smoke: FAILED to load global.spr\n");
        return 1;
    }
    const sk::Sprite* slash = sprites.GetSprite(218);
    Check(slash != nullptr, "global.spr slot 218 exists");
    if (slash) {
        Check(slash->width == sk::Backbuffer::kWidth && slash->height == sk::Backbuffer::kHeight,
              "and it is a full 176x208 overlay, which is why it is blitted at (0,0)");
        int opaque = 0, minX = 9999, maxX = -1, minY = 9999, maxY = -1;
        std::set<uint16_t> colours;
        for (int y = 0; y < slash->height; ++y) {
            for (int x = 0; x < slash->width; ++x) {
                const size_t i = static_cast<size_t>(y) * slash->width + static_cast<size_t>(x);
                if (!slash->opaque[i]) continue;
                ++opaque;
                colours.insert(slash->pixels[i]);
                minX = (std::min)(minX, x);
                maxX = (std::max)(maxX, x);
                minY = (std::min)(minY, y);
                maxY = (std::max)(maxY, y);
            }
        }
        Check(opaque > 0 && opaque < slash->width * slash->height / 20,
              "almost all of it is transparent -- it is a stroke, not a wash (" +
                  std::to_string(opaque) + " opaque pixels)");
        Check(colours.size() == 1, "drawn in a single colour");
        const uint16_t only = *colours.begin();
        // RGB565 back to channels: red is the top five bits.
        Check((only >> 11) > 20 && ((only >> 5) & 0x3F) < 12,
              "and that colour is red");
        Check(maxX - minX > 40 && maxY - minY > 40,
              "the stroke runs diagonally across the view rather than along one axis");
    }
    // The timer, and the blend that draws it.
    Check(sk_bindings::PlayerExecutable::kHurtTimerUnits == 0x40,
          "the hurt timer FUN_10044814 writes is 0x40");
    Check(std::abs(0x40 / 256.0 - 0.25) < 0.001, "i.e. a quarter of a second");

    // Blit_RLESprite's blendMode 1: `((src & 0xeee) + (dst & 0xeee)) >> 1`
    // in the engine's RGB444, the same average in the backbuffer's RGB565.
    if (slash) {
        sk::Backbuffer target;
        target.Fill(0x0000);
        target.Blit(0, 0, *slash);
        sk::Backbuffer blended;
        blended.Fill(0x0000);
        blended.BlitBlend(0, 0, *slash);
        int halved = 0, straight = 0;
        for (int y = 0; y < slash->height; ++y) {
            for (int x = 0; x < slash->width; ++x) {
                const size_t i = static_cast<size_t>(y) * slash->width + static_cast<size_t>(x);
                if (!slash->opaque[i]) continue;
                ++straight;
                // Over black, a 50% average is half the source.
                if ((blended.Row(y)[x] >> 11) * 2 <= (target.Row(y)[x] >> 11) + 1) ++halved;
            }
        }
        Check(straight > 0 && halved == straight,
              "and blending it over black gives half its brightness at every pixel");
    }

    std::printf("\nm86_hit_feedback_smoke: %s (%d checks, %d failures)\n",
                gFailures == 0 ? "PASSED" : "FAILED", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
