// M89 -- the three things that made every zone too dark, and the one that
// put coloured patches on it.
//
// Reported from play: "there are random color patches on the ground in
// random places, and the levels, even the sunnier ones like snowline, are
// too dark", on top of the two defects M71 left open (model lighting, and
// a bright green patch on the wall behind azra's candelabra).
//
// All four are one investigation. This test pins what came out of it:
//
//  1. **`<zone>.zfg`** -- a ninth per-zone file this port never loaded.
//     `GameEngine_InitLevel` reads it into `engine+0x5c4`
//     (`"%s\\%s.zfg"`, `"InitLevel Pre Fog"`, `"Failed to load fog."`)
//     and `CompositeSceneBufferToScreen` runs the whole finished frame
//     through it. 65536 `uint16` entries indexed `(level << 12) | rgb444`
//     -- 16 fog levels over the entire RGB444 space.
//  2. **The fog scale**, `engine+0x5c8 = 0x10000 / (tier.maxSteps / 2)`,
//     and the per-vertex scalar `min(depth * scale >> 8, 0xffff)` whose
//     top nibble is that level.
//  3. **`Bullseye_PropagateLight`, for real** -- M9's float march capped
//     every ray at 20 tiles, credited a cell once per ray, and bounced
//     off walls. The original has no cap, adds every half-tile step, and
//     stops dead at the first wall.
//  4. **The `.zlu` family stride** -- 0x200 bytes (one rung), not 0x8000
//     (a whole 64-rung block).
//
// Everything below runs against the real shipped zone data.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "assets/zone_file.h"
#include "world/zone.h"

namespace {

const char* kScriptRoot =
    "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
    "EnFrDeEsIt-26102004/system/apps/6r51";

int g_Checks = 0;
int g_Failures = 0;

void Check(bool ok, const std::string& what) {
    ++g_Checks;
    if (!ok) {
        ++g_Failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

void CheckEq(long long got, long long want, const std::string& what) {
    ++g_Checks;
    if (got != want) {
        ++g_Failures;
        std::printf("  FAIL: %s -- got %lld, want %lld\n", what.c_str(), got, want);
    }
}

// One row of the reference table produced by an independent transcription
// of Bullseye_BakeLighting/Bullseye_PropagateLight (0x1000f130/0x1000ef74)
// straight out of the decompile, run over the shipped data. `lightSum` is
// the sum of every cell's baked `lightLevel` after the `.zcp` delta pass;
// `minRungCells` counts cells that end at the rasterizer's floor rung 4.
// `fam[i]` is the per-zone census of `ZmpCell::flags` bits 4-5, i.e. how
// many cells ask for each `.zlu` family. `fogBlack` is what that zone's
// `.zfg` fades pure white to at level 15 -- 0x000 for a dungeon, a pale
// daylight haze for the six outdoor zones.
struct ZoneRef {
    const char* name;
    long long lightSum;
    int minRungCells;
    int fam[4];
    int fogWhite15;
};

const ZoneRef kZones[] = {
    {"azra", 134442624, 537, {16346, 1, 35, 2}, 0x000},
    {"broken1", 82412608, 509, {16383, 1, 0, 0}, 0x000},
    {"broken2", 64755648, 508, {0, 0, 0, 16384}, 0x000},
    {"crypt1", 99328128, 586, {16384, 0, 0, 0}, 0x000},
    {"crypt2", 82332288, 570, {16384, 0, 0, 0}, 0x000},
    {"crypt3", 75087872, 526, {16380, 4, 0, 0}, 0x000},
    {"delfhide", 69121024, 1027, {16384, 0, 0, 0}, 0x000},
    {"drgnfld", 86444288, 538, {16384, 0, 0, 0}, 0xaac},
    {"dstar_e", 124739392, 508, {16384, 0, 0, 0}, 0xaac},
    {"dstar_w", 96518720, 930, {16383, 1, 0, 0}, 0xaac},
    {"erthcave", 85493952, 534, {16359, 0, 24, 1}, 0x000},
    {"fearfrst", 105310912, 703, {16254, 0, 130, 0}, 0x000},
    {"ffarena", 28656320, 252, {4096, 0, 0, 0}, 0x000},
    {"ghstpass", 99851200, 779, {16368, 15, 0, 1}, 0xaab},
    {"glaciercrawl", 94759424, 884, {16384, 0, 0, 0}, 0xaab},
    {"lakvan", 105740032, 520, {16384, 0, 0, 0}, 0x000},
    {"lothcav", 70497984, 512, {16384, 0, 0, 0}, 0x000},
    {"raiders", 76096384, 530, {16383, 0, 1, 0}, 0x000},
    {"snowline", 113596416, 767, {16371, 13, 0, 0}, 0x99b},
    {"stouttp", 89969600, 1106, {16382, 0, 2, 0}, 0x889},
    {"twilite", 76982336, 577, {16384, 0, 0, 0}, 0x000},
};

std::string Path(const std::string& zone, const char* ext) {
    return std::string(kScriptRoot) + "/" + zone + ext;
}

uint16_t ReadU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

}  // namespace

int main() {
    std::printf("m89_lighting_smoke\n");

    // ---------------------------------------------------------------
    // Part 1 -- `<zone>.zfg` is a 16-level x 4096-colour fog LUT, and
    // every shipped zone has one.
    //
    // The shape is forced by CompositeSceneBufferToScreen itself: it
    // indexes `engine+0x5c4` with `(pixel & 0xffff) * 2` and reads two
    // bytes, so the table is 65536 `uint16` entries = 131072 bytes, which
    // is exactly what all 21 files decompress to. The low 12 bits of that
    // index are the RGB444 colour the rasterizers write; the top 4 are
    // the fog nibble they OR in. Level 0 being the identity map in every
    // file is the proof that reading is right -- an unfogged pixel has to
    // come out of the table unchanged, and it does, for all 4096 colours,
    // in all 21 zones.
    // ---------------------------------------------------------------
    std::printf("\n[1] .zfg: the fog table, all 21 zones\n");
    for (const ZoneRef& ref : kZones) {
        std::vector<uint8_t> zfg = sk::LoadCompressedZoneFile(Path(ref.name, ".zfg"));
        CheckEq(static_cast<long long>(zfg.size()), 131072,
                 std::string(ref.name) + ".zfg decompressed size");
        if (zfg.size() != 131072) continue;

        int identity = 0;
        for (int c = 0; c < 4096; ++c) {
            if (ReadU16(&zfg[static_cast<size_t>(c) * 2]) == c) ++identity;
        }
        CheckEq(identity, 4096, std::string(ref.name) + ".zfg level 0 is the identity map");

        // Level 15 is the terminal colour: the whole palette has collapsed
        // onto (or very near) one value by then. Checking pure white and
        // pure black land within one 4-bit step of each other is the
        // cheapest way to say "this level is a flat fill".
        const int white15 = ReadU16(&zfg[(((15u << 12) | 0xfffu)) * 2]);
        const int black15 = ReadU16(&zfg[(((15u << 12) | 0x000u)) * 2]);
        CheckEq(white15, ref.fogWhite15, std::string(ref.name) + ".zfg level 15 fades white to");
        auto chan = [](int rgb, int shift) { return (rgb >> shift) & 0xf; };
        int spread = 0;
        for (int s = 0; s <= 8; s += 4) {
            spread = (std::max)(spread, std::abs(chan(white15, s) - chan(black15, s)));
        }
        Check(spread <= 2, std::string(ref.name) + ".zfg level 15 is a flat fill (spread " +
                                std::to_string(spread) + ")");

        // Monotone: each successive level moves every channel toward the
        // terminal colour and never back. Sampled on pure white, whose
        // 0xf channels give the ramp its full range.
        int prev = 0xfff;
        bool monotone = true;
        for (int lvl = 1; lvl <= 15; ++lvl) {
            const int v = ReadU16(&zfg[(((static_cast<unsigned>(lvl) << 12) | 0xfffu)) * 2]);
            for (int s = 0; s <= 8; s += 4) {
                const int target = chan(ref.fogWhite15, s);
                const int a = chan(prev, s), b = chan(v, s);
                if (target <= a ? b > a : b < a) monotone = false;
            }
            prev = v;
        }
        Check(monotone, std::string(ref.name) + ".zfg white ramp is monotone toward level 15");
    }

    // Fifteen of the twenty-one zones fog to pure black -- that is the
    // dungeon darkness cue, and it is why an indoor zone can look right
    // without a fog table at all. The six that do not are the outdoor
    // ones, and they fog to a *bright* pale haze: that is the whole
    // reason snowline looked black in this port and washed-out on the
    // device.
    {
        int black = 0, pale = 0;
        for (const ZoneRef& ref : kZones) (ref.fogWhite15 == 0 ? black : pale) += 1;
        CheckEq(black, 14, "zones whose fog fades to black");
        CheckEq(pale, 7, "zones whose fog fades to a pale daylight haze");
    }

    // ---------------------------------------------------------------
    // Part 2 -- the fog scale, engine+0x5c8.
    //
    // TileGrid_RaycastVisibility's first act, beside the visibility tier
    // it shares a source with: `engine+0x5c8 = 0x10000 / (maxSteps / 2)`.
    // Both fade rasterizer families then compute, per vertex,
    // `fog = min(depth * scale >> 8, 0xffff)` and use `fog >> 12` as the
    // level -- so fog saturates at half the tier's ray range, which at
    // the shipped default zoom (engine+0x608 == 0x100, 25 tiles) is about
    // twelve tiles. That is a *lot* of fog, and it is what the device
    // screenshots show.
    // ---------------------------------------------------------------
    std::printf("\n[2] engine+0x5c8: the fog scale\n");
    CheckEq(sk::Zone::FogScaleFor(0x100), 0x10000 / (0x19 / 2), "fog scale, default zoom");
    CheckEq(sk::Zone::FogScaleFor(0x180), 0x10000 / (0x5d / 2), "fog scale, mid tier");
    CheckEq(sk::Zone::FogScaleFor(0x300), 0x10000 / (0xac / 2), "fog scale, deep tier");

    auto levelAt = [](double tiles, int zoomScale) {
        const double raw = tiles * 256.0 * sk::Zone::FogScaleFor(zoomScale) / 256.0;
        return static_cast<int>((std::min)(raw, 65535.0)) >> 12;
    };
    CheckEq(levelAt(0.0, 0x100), 0, "no fog at the eye");
    CheckEq(levelAt(6.0, 0x100), 7, "half fog at six tiles");
    CheckEq(levelAt(12.0, 0x100), 15, "full fog at twelve tiles");
    CheckEq(levelAt(12.0, 0x300), 2, "the deep tier fogs far more slowly");
    // Saturation lands at maxSteps/2 tiles for every tier -- the point of
    // deriving the scale from the tier in the first place -- and half that
    // distance is still only half fogged.
    for (int zoom : {0x100, 0x180, 0x300}) {
        const int half = sk::Zone::TierFor(zoom).maxSteps / 2;
        CheckEq(levelAt(half, zoom), 15,
                 "full fog at half the ray range, zoom " + std::to_string(zoom));
        CheckEq(levelAt(half / 2.0, zoom), 7,
                 "half fog at a quarter of the ray range, zoom " + std::to_string(zoom));
    }

    // ---------------------------------------------------------------
    // Part 3 -- Bullseye_PropagateLight, and how much light M9's
    // approximation was throwing away.
    //
    // The reference numbers in kZones come from a separate transcription
    // of the two decompiled functions, run over the same shipped files.
    // The port's bake has to match them exactly: both are deterministic
    // integer algorithms over identical input, so anything but equality
    // means one of them is not the original.
    // ---------------------------------------------------------------
    std::printf("\n[3] Bullseye_BakeLighting over all 21 zones\n");
    for (const ZoneRef& ref : kZones) {
        sk::Zone zone;
        if (!zone.Load(kScriptRoot, ref.name)) {
            Check(false, std::string("load ") + ref.name);
            continue;
        }

        long long sum = 0;
        int minRung = 0;
        for (int ty = 0; ty < zone.height(); ++ty) {
            for (int tx = 0; tx < zone.width(); ++tx) {
                const int light = zone.CellAt(tx, ty).lightLevel;
                sum += light;
                if (std::clamp(light >> 8, 4, 63) == 4) ++minRung;
            }
        }
        CheckEq(sum, ref.lightSum, std::string(ref.name) + " baked light sum");
        CheckEq(minRung, ref.minRungCells, std::string(ref.name) + " cells at the floor rung");

        // Bit 4-5 census: this is the list of zones the palette bug could
        // ever have touched, and broken2 is the one it wrecked wholesale.
        int fam[4] = {0, 0, 0, 0};
        for (int ty = 0; ty < zone.height(); ++ty) {
            for (int tx = 0; tx < zone.width(); ++tx) {
                ++fam[(zone.CellAt(tx, ty).flags >> 4) & 3];
            }
        }
        for (int i = 0; i < 4; ++i) {
            CheckEq(fam[i], ref.fam[i],
                     std::string(ref.name) + " cells asking for .zlu family " + std::to_string(i));
        }
    }

    // The headline: snowline, the zone the report named, and azra, the
    // one M71 measured against a device screenshot. M9's bake left them
    // at mean rung 5.7 and 12.8 of 63 -- black and near-black. The real
    // one is three to five times that.
    {
        sk::Zone snow, azra;
        Check(snow.Load(kScriptRoot, "snowline"), "load snowline");
        Check(azra.Load(kScriptRoot, "azra"), "load azra");
        auto meanRung = [](const sk::Zone& z) {
            long long acc = 0;
            for (int ty = 0; ty < z.height(); ++ty) {
                for (int tx = 0; tx < z.width(); ++tx) {
                    acc += std::clamp(z.CellAt(tx, ty).lightLevel >> 8, 4, 63);
                }
            }
            return static_cast<double>(acc) / (z.width() * z.height());
        };
        const double snowMean = meanRung(snow);
        const double azraMean = meanRung(azra);
        std::printf("  mean .zlu rung: snowline %.2f, azra %.2f (M9 left them at 5.7 / 12.8)\n",
                     snowMean, azraMean);
        Check(snowMean > 26.0 && snowMean < 27.5, "snowline mean rung");
        Check(azraMean > 31.0 && azraMean < 32.5, "azra mean rung");

        // No 20-tile cap any more: snowline's light sources are sparse
        // and clustered, so with M9's cap most of the map got nothing at
        // all. Now the far corners are lit.
        int lit = 0, total = 0;
        for (int ty = 1; ty < snow.height() - 1; ++ty) {
            for (int tx = 1; tx < snow.width() - 1; ++tx) {
                ++total;
                if (snow.CellAt(tx, ty).lightLevel > 0) ++lit;
            }
        }
        Check(lit * 100 / total >= 90, "snowline: at least 90% of the map receives light");
    }

    // ---------------------------------------------------------------
    // Part 4 -- the .zlu family is a +0..+3 rung bias, not a block index.
    //
    // The four pointers GameEngine_InitLevel builds are zlu+0, +0x200,
    // +0x400 and +0x600; Bullseye_Init stashes them at
    // engine+0x6b24..+0x6b30; SurfaceFace_BuildAndProject picks one with
    // `engine + 0x6b24 + ((*zmpCell & 0x30) >> 2)`; the rasterizer adds
    // `(lightA + lightB) & 0xfffffe00` on top. So the whole address is
    // `(family + (light >> 8)) * 512 + texel * 2`, and "family" buys a
    // shade, not a hue.
    // ---------------------------------------------------------------
    std::printf("\n[4] .zlu addressing\n");
    {
        sk::Zone zone;
        Check(zone.Load(kScriptRoot, "azra"), "load azra for palette checks");
        std::vector<uint8_t> zlu = sk::LoadCompressedZoneFile(Path("azra", ".zlu"));
        CheckEq(static_cast<long long>(zlu.size()), 131072, "azra.zlu decompressed size");

        int mismatches = 0;
        for (int fam = 0; fam < 4 && zlu.size() == 131072; ++fam) {
            for (int light = 0; light <= 0x3f00; light += 0x100) {
                const int rung = std::clamp(light >> 8, 4, 63) + fam;
                for (int texel : {0, 1, 37, 100, 200, 255}) {
                    const uint16_t want =
                        ReadU16(&zlu[static_cast<size_t>(rung) * 512 + static_cast<size_t>(texel) * 2]);
                    const uint16_t got = zone.PaletteColor(static_cast<uint8_t>(fam),
                                                            static_cast<uint16_t>(light),
                                                            static_cast<uint8_t>(texel));
                    if (got != want) ++mismatches;
                }
            }
        }
        CheckEq(mismatches, 0, "PaletteColor == familyPtr + rung*512 + texel*2, every family/rung");

        // The regression this pins. The old formula addressed the family
        // as a whole 64-rung block, and a `.zlu`'s four blocks really are
        // four different colour ramps -- block 2's is green. Take the
        // real light level of azra's own family-2 cell (124,42) and show
        // what the two formulas do with the same texel: the corrected one
        // stays a shade away from family 0, the old one lands somewhere
        // else entirely.
        if (zlu.size() == 131072) {
            const int light = zone.CellAt(124, 42).lightLevel;
            const int rung = std::clamp(light >> 8, 4, 63);
            const int texel = 100;
            auto rgbAt = [&](int r) {
                return ReadU16(&zlu[static_cast<size_t>(r) * 512 + static_cast<size_t>(texel) * 2]);
            };
            auto dist = [](int a, int b) {
                int d = 0;
                for (int s = 0; s <= 8; s += 4) d += std::abs(((a >> s) & 0xf) - ((b >> s) & 0xf));
                return d;
            };
            const int base = rgbAt(rung);                // family 0, what the neighbours draw
            const int fixed = zone.PaletteColor(2, static_cast<uint16_t>(light),
                                                 static_cast<uint8_t>(texel));
            const int old = rgbAt(2 * 64 + rung);        // what this port used to read
            std::printf("  azra (124,42) light 0x%04x rung %d: family0 %03x, fixed %03x, M71 %03x\n",
                         light, rung, base, fixed, old);
            Check(dist(fixed, base) <= 3, "the corrected family-2 colour is a shade off family 0");
            Check(dist(old, base) >= 6, "the old block-indexed colour was a different colour");
        }
    }

    // ---------------------------------------------------------------
    // Part 5 -- Zone::FogColor, the lookup the renderer actually calls.
    // ---------------------------------------------------------------
    std::printf("\n[5] Zone::FogColor\n");
    {
        sk::Zone snow;
        Check(snow.Load(kScriptRoot, "snowline"), "load snowline for fog checks");
        Check(snow.hasFog(), "snowline has a fog table");

        int identity = 0;
        for (int c = 0; c < 4096; ++c) {
            if (snow.FogColor(0, static_cast<uint16_t>(c)) == c) ++identity;
        }
        CheckEq(identity, 4096, "FogColor level 0 passes every colour through");
        CheckEq(snow.FogColor(15, 0x0fff), 0x99b, "FogColor level 15, white");
        CheckEq(snow.FogColor(15, 0x0000), 0x88a, "FogColor level 15, black");
        CheckEq(snow.FogColor(99, 0x0fff), snow.FogColor(15, 0x0fff), "FogColor clamps high levels");
        CheckEq(snow.FogColor(-1, 0x0abc), 0x0abc, "FogColor clamps low levels");

        // Outdoor fog *brightens* what it touches and indoor fog darkens
        // it: the same mechanism, opposite ends, and the reason "the
        // levels are too dark" and "azra looks fine" were both true.
        auto luma = [](int rgb) { return ((rgb >> 8) & 0xf) + ((rgb >> 4) & 0xf) + (rgb & 0xf); };
        Check(luma(snow.FogColor(15, 0x0000)) > luma(0x0000),
              "snowline fog lifts black toward daylight");

        sk::Zone azra;
        Check(azra.Load(kScriptRoot, "azra"), "load azra for fog checks");
        Check(azra.hasFog(), "azra has a fog table");
        CheckEq(azra.FogColor(15, 0x0fff), 0x000, "azra fog takes white to black");

        // A default-constructed Zone has no table, and FogColor is then
        // the engine's own `engine+0xbe0f == 0` path: a plain copy.
        sk::Zone empty;
        Check(!empty.hasFog(), "a zone with no .zfg reports no fog");
        CheckEq(empty.FogColor(15, 0x0abc), 0x0abc, "no fog table -- colours pass through");
    }

    std::printf("\n%d checks, %d failures -- %s\n", g_Checks, g_Failures,
                 g_Failures == 0 ? "OK" : "FAILED");
    return g_Failures == 0 ? 0 : 1;
}
