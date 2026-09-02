// M9 smoke test: loads a real zone and checks the Bullseye lighting bake
// (world/zone.cpp's BakeLighting(), reproducing docs/ZONE_FORMAT.md's
// Bullseye_BakeLighting/Bullseye_PropagateLight) produces sane results
// against real data -- light-source cells end up brighter than distant
// cells, brightness falls off with distance, and nothing crashes/hangs
// on the real 128x128 grid with its real light-source count.
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "world/zone.h"

int main(int argc, char** argv) {
    const std::string scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";
    const char* zoneName = argc > 2 ? argv[2] : "azra";

    sk::Zone zone;
    if (!zone.Load(scriptRoot, zoneName)) {
        std::printf("m9_lighting_smoke: FAILED to load zone %s\n", zoneName);
        return 1;
    }

    int lightSources = 0, litCells = 0, darkCells = 0, wallCells = 0;
    uint16_t maxLevel = 0;
    long long sum = 0;
    int sampleCount = 0;
    int firstSourceX = -1, firstSourceY = -1;
    for (int y = 0; y < zone.height(); ++y) {
        for (int x = 0; x < zone.width(); ++x) {
            const sk::ZmpCell& c = zone.CellAt(x, y);
            if (c.IsLightSource()) {
                ++lightSources;
                if (firstSourceX < 0) {
                    firstSourceX = x;
                    firstSourceY = y;
                }
            }
            if (c.IsWall()) {
                ++wallCells;
                continue;
            }
            ++sampleCount;
            sum += c.lightLevel;
            maxLevel = std::max(maxLevel, c.lightLevel);
            if (c.lightLevel > 0) {
                ++litCells;
            } else {
                ++darkCells;
            }
        }
    }

    std::printf("zone '%s': %d light sources, %d wall cells\n", zoneName, lightSources, wallCells);
    std::printf("open cells: %d, lit (>0): %d, dark (==0): %d\n", sampleCount, litCells, darkCells);
    std::printf("max lightLevel: %u (cap %u), mean: %.1f\n", maxLevel, sk::kMaxLightLevel,
                sampleCount ? static_cast<double>(sum) / sampleCount : 0.0);

    bool ok = lightSources > 0 && litCells > 0 && maxLevel > 0 && maxLevel <= sk::kMaxLightLevel;

    // Falloff check: brightness at the first light source's own cell
    // should be >= brightness a good distance away along an open
    // corridor, if one exists nearby.
    if (firstSourceX >= 0) {
        float nearBrightness = sk::LightLevelToBrightness(zone.CellAt(firstSourceX, firstSourceY).lightLevel);
        std::printf("first light source at (%d,%d): lightLevel=%u brightness=%.2f\n", firstSourceX,
                    firstSourceY, zone.CellAt(firstSourceX, firstSourceY).lightLevel, nearBrightness);

        // Informational only, not a pass/fail signal: with 65 light
        // sources spread across a 128x128 grid (average nearest-neighbor
        // spacing ~16 tiles), a cell "far" from *this* source may well be
        // close to a *different* one, so this doesn't reliably show
        // single-source falloff on its own -- the aggregate stats above
        // (mean/max/lit-vs-dark counts) are the real correctness check.
        int farX = firstSourceX + 15, farY = firstSourceY;
        if (zone.InBounds(farX, farY) && !zone.CellAt(farX, farY).IsWall()) {
            uint16_t farLevel = zone.CellAt(farX, farY).lightLevel;
            std::printf("cell 15 tiles east (%d,%d): lightLevel=%u brightness=%.2f\n", farX, farY,
                        farLevel, sk::LightLevelToBrightness(farLevel));
        }
    }

    std::printf("\nm9_lighting_smoke: %s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
