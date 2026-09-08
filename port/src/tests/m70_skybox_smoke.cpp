// M70 smoke test: what a `.zsk` actually is.
//
// M11 read `<zone>.zsk` as the zone's own baked room mesh and verified that
// reading against `azra` alone. M66 found that nine zones ship a
// byte-identical one, which no per-zone room geometry could be. This test
// carries the answer -- **it is the zone's skybox** -- and every claim
// behind it, measured against the shipped files and the real renderer
// rather than argued:
//
//   * **Part 1 -- the census.** All 21 `.zsk` files, parsed. Three distinct
//     meshes between them (11 zones share one, 9 share another, raiders has
//     a variant) but six distinct 256x256 skins -- so what varies per zone
//     is the picture, not the geometry, which is the shape of a shared sky
//     asset and not of per-zone room geometry. Eight of those skins are an
//     actual sky and they belong to exactly the eight open-air zones; the
//     other thirteen are black. The 11-zone mesh is a closed dome whose
//     apex vertex maps to the exact centre of the skin: the apex is the
//     zenith and the skin is a fisheye sky.
//
//   * **Part 2 -- the texture the engine actually reads.** Those nine
//     zones' texture headers say `skinCount=256, width=256, height=0`, i.e.
//     zero pixels, which is what killed a Debug build before M66. The
//     engine never reads that header for a `.zsk`: its skybox rasterizer
//     addresses a fixed 256x256 skin. Both readings are taken here from the
//     same bytes and compared.
//
//   * **Part 3 -- it behaves like a skybox in the real renderer.** Drawn
//     underneath everything (a frame with the sky on differs from one with
//     it off only where the sky-off frame was bare background), fixed to the
//     camera under translation (walk several tiles, the sky does not move),
//     and turning with the view (rotate, and it does).
//
// The engine's own name for the thing is not inferred: the zone loader's
// debug markers around this file's load read "InitLevel Pre skybox load" /
// "InitLevel Post skybox load", and the object it fills is built at
// "Bullseye constructer Post newing Skybox".

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "assets/zone_file.h"
#include "graphics/backbuffer.h"
#include "render3d/camera.h"
#include "render3d/zone_renderer.h"
#include "world/model_archive.h"
#include "world/zone.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%-92s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    if (!ok) ++g_failures;
}

const char* kScriptRoot =
    "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
    "EnFrDeEsIt-26102004/system/apps/6r51";

const char* kZones[] = {"azra",     "broken1",  "broken2",  "crypt1",       "crypt2",
                        "crypt3",   "delfhide", "drgnfld",  "dstar_e",      "dstar_w",
                        "erthcave", "fearfrst", "ffarena",  "ghstpass",     "glaciercrawl",
                        "lakvan",   "lothcav",  "raiders",  "snowline",     "stouttp",
                        "twilite"};

// The zones that carry an actual sky image. Every other zone's skin is
// black, and the split is not subtle once the shipped display names
// (assets/zone_display_names.cpp -> stringtable.eng) are read next to it:
// these eight are Azra's Crossing, Dragonfields, Ghast's Pass, Snowline,
// Stout's Trading Post, Dragonstar East and West, and Glacier Crawl -- the
// open-air zones. The thirteen with a black sky are Crypt of Hearts I-III,
// Earthtear Caverns, Loth'Na Caverns, Fearfrost Caverns, Broken Wing I and
// II, Twilight Temple, Delfran's Hideout, Lakvan's Stronghold, Raider's
// Nest and the arena. A sky asset that is present in every outdoor zone and
// black in every interior is not ambiguous about what it is for.
const std::set<std::string> kZonesWithSkyImage = {
    "azra", "drgnfld", "dstar_e", "dstar_w", "ghstpass", "glaciercrawl", "snowline", "stouttp"};

// Cheap content hash -- this test only ever compares blobs for equality,
// never reverses one.
uint64_t Hash(const uint8_t* p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

uint64_t HashModelGeometry(const sk::Model& m) {
    std::vector<uint8_t> buf;
    auto push = [&buf](int16_t v) {
        buf.push_back(static_cast<uint8_t>(v & 0xff));
        buf.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    };
    for (const auto& v : m.vertices) { push(v.x); push(v.y); push(v.z); }
    for (const auto& uv : m.uvs) {
        push(static_cast<int16_t>(uv.u));
        push(static_cast<int16_t>(uv.v));
    }
    for (const auto& f : m.faces) { push(f.vA); push(f.vB); push(f.vC); push(f.uA); push(f.uB); push(f.uC); }
    return Hash(buf.data(), buf.size());
}

uint64_t HashModelSkin(const sk::Model& m) {
    if (m.pixels.empty()) return 0;
    return Hash(reinterpret_cast<const uint8_t*>(m.pixels.data()), m.pixels.size() * 2);
}

void WriteBackbufferPpm(const sk::Backbuffer& bb, const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    f << "P6\n" << sk::Backbuffer::kWidth << " " << sk::Backbuffer::kHeight << "\n255\n";
    for (int y = 0; y < sk::Backbuffer::kHeight; ++y) {
        const uint16_t* row = bb.Row(y);
        for (int x = 0; x < sk::Backbuffer::kWidth; ++x) {
            uint16_t c = row[x];
            f.put(static_cast<char>(((c >> 11) & 0x1f) << 3));
            f.put(static_cast<char>(((c >> 5) & 0x3f) << 2));
            f.put(static_cast<char>((c & 0x1f) << 3));
        }
    }
    std::printf("  wrote %s\n", path.c_str());
}

struct Frame {
    std::vector<uint16_t> px;
    uint16_t at(int i) const { return px[static_cast<size_t>(i)]; }
};

Frame RenderFrame(const sk::Zone& zone, const sk::Camera& camera, bool withSky) {
    sk::Backbuffer bb;
    sk::ZoneRenderer renderer;
    renderer.drawSkybox = withSky;
    renderer.Render(bb, zone, camera);
    Frame f;
    f.px.resize(static_cast<size_t>(sk::Backbuffer::kWidth) * sk::Backbuffer::kHeight);
    for (int y = 0; y < sk::Backbuffer::kHeight; ++y) {
        const uint16_t* row = bb.Row(y);
        for (int x = 0; x < sk::Backbuffer::kWidth; ++x) {
            f.px[static_cast<size_t>(y) * sk::Backbuffer::kWidth + x] = row[x];
        }
    }
    return f;
}

}  // namespace

int main(int argc, char** argv) {
    const char* scriptRoot = argc > 1 ? argv[1] : kScriptRoot;
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    // ---------------------------------------------------------------
    // Part 1 -- the census over all 21 shipped `.zsk` files.
    // ---------------------------------------------------------------
    std::printf("=== Part 1: what the 21 shipped .zsk files contain ===\n");

    std::map<uint64_t, std::vector<std::string>> byGeometry, bySkin;
    std::map<std::string, sk::Model> skyByZone;
    int parsedCount = 0, carriesFullSkinBlock = 0, sixByteTrailer = 0, fourByteTrailer = 0;

    for (const char* name : kZones) {
        std::vector<uint8_t> raw =
            sk::LoadCompressedZoneFile(std::string(scriptRoot) + "/" + name + ".zsk");
        sk::Model sky;
        if (raw.empty() || !sk::ParseSkyboxResource(raw.data(), raw.size(), sky)) {
            std::printf("  %-13s FAILED to load/parse\n", name);
            continue;
        }
        ++parsedCount;
        byGeometry[HashModelGeometry(sky)].push_back(name);
        bySkin[HashModelSkin(sky)].push_back(name);

        // The engine reads the skin as a fixed 131072-byte block starting
        // right after the 8-byte texture header, and every one of these
        // files physically carries exactly that -- which is the layout claim
        // ParseSkyboxResource rests on. Recomputed here from the raw bytes
        // rather than trusted.
        sk::Model byHeader;
        (void)sk::ParseModelResource(raw.data(), raw.size(), byHeader);
        size_t geometryBytes = 14 + static_cast<size_t>(sky.vertices.size()) * 6 +
                               sky.uvs.size() * 4 + sky.faces.size() * 12;
        size_t pixStart = geometryBytes + 8;
        size_t skinBytes = static_cast<size_t>(sk::kSkyboxSkinWidth) * sk::kSkyboxSkinHeight * 2;
        if (pixStart + skinBytes <= raw.size()) ++carriesFullSkinBlock;
        size_t trailer = raw.size() - pixStart - skinBytes;
        if (trailer == 6) ++sixByteTrailer;
        if (trailer == 4) ++fourByteTrailer;

        int litTexels = 0;
        for (uint16_t t : sky.pixels) {
            if (t > 1) ++litTexels;
        }
        std::printf("  %-13s %3zu verts %4zu faces  header says %dx%dx%d  trailer %zu  "
                    "skin %5d lit/%zu\n",
                    name, sky.vertices.size(), sky.faces.size(), byHeader.skinCount, byHeader.width,
                    byHeader.height, trailer, litTexels, sky.pixels.size());
        skyByZone[name] = std::move(sky);
    }

    Check(parsedCount == 21, "all 21 shipped .zsk files decompress and parse as model resources");
    Check(carriesFullSkinBlock == 21,
          "every one carries the full 256x256 skin block the engine's rasterizer reads");
    // Twelve files end with a well-formed 6-byte clip record after that
    // block, like any other static model resource. The nine blank ones end
    // two bytes short of one -- a detail worth keeping rather than
    // smoothing over: together with the impossible `256x256x0` header, it
    // says those nine were exported by something that did not fill in a
    // sky, not authored as geometry. The engine reads neither field for a
    // skybox, so it never notices.
    Check(sixByteTrailer == 12 && fourByteTrailer == 9,
          "12 end in a well-formed 6-byte clip record; the 9 blank ones end 2 bytes short");

    std::printf("\n  distinct meshes:\n");
    for (const auto& g : byGeometry) {
        std::printf("    %2zu zones:", g.second.size());
        for (const std::string& z : g.second) std::printf(" %s", z.c_str());
        std::printf("\n");
    }
    std::printf("  distinct skins:\n");
    for (const auto& s : bySkin) {
        std::printf("    %2zu zones:", s.second.size());
        for (const std::string& z : s.second) std::printf(" %s", z.c_str());
        std::printf("\n");
    }

    Check(byGeometry.size() == 3, "the 21 zones share only three distinct meshes between them");
    Check(bySkin.size() == 6, "but six distinct skins -- the sky, not the geometry, is per-zone");

    // Which zones carry a picture of a sky, and which carry black.
    std::set<std::string> withImage;
    for (const auto& kv : skyByZone) {
        int lit = 0;
        for (uint16_t t : kv.second.pixels) {
            if (t > 1 && ++lit > 64) break;
        }
        if (lit > 64) withImage.insert(kv.first);
    }
    Check(withImage == kZonesWithSkyImage,
          "the 8 zones whose skin is a picture rather than black are exactly the outdoor ones");

    // The 11-zone mesh is a dome, and its apex is the zenith: rings of
    // vertices whose radius shrinks as the up axis (local +Y) grows, closed
    // by a single apex vertex, and that apex is the one whose UV is the
    // centre of the skin.
    const sk::Model& azra = skyByZone["azra"];
    int apex = -1;
    for (size_t v = 0; v < azra.vertices.size(); ++v) {
        if (apex < 0 || azra.vertices[v].y > azra.vertices[static_cast<size_t>(apex)].y) {
            apex = static_cast<int>(v);
        }
    }
    std::set<std::pair<int, int>> apexUvs;
    bool ringsShrink = true;
    {
        // radius by height band -- one band per distinct local Y.
        std::map<int, double> maxRadiusByY;
        for (const auto& v : azra.vertices) {
            double r = std::sqrt(static_cast<double>(v.x) * v.x + static_cast<double>(v.z) * v.z);
            double& slot = maxRadiusByY[v.y];
            slot = std::max(slot, r);
        }
        double prev = -1.0;
        // walk bands from the widest (lowest y) upward; skip the single
        // bottom vertex, which closes the dome underneath.
        std::vector<std::pair<int, double>> bands(maxRadiusByY.begin(), maxRadiusByY.end());
        for (size_t i = 1; i < bands.size(); ++i) {
            if (prev >= 0.0 && bands[i].second > prev) ringsShrink = false;
            prev = bands[i].second;
        }
        std::printf("\n  azra dome bands (local Y -> max radius):");
        for (const auto& b : bands) std::printf(" %d:%.0f", b.first, b.second);
        std::printf("\n");
    }
    for (const auto& f : azra.faces) {
        const int16_t vs[3] = {f.vA, f.vB, f.vC};
        const int16_t us[3] = {f.uA, f.uB, f.uC};
        for (int i = 0; i < 3; ++i) {
            if (vs[i] != apex) continue;
            const sk::ModelUv& uv = azra.uvs[static_cast<size_t>(us[i])];
            apexUvs.insert({uv.u / 256, uv.v / 256});
        }
    }
    std::printf("  apex vertex %d at local (%d,%d,%d), texel", apex,
                azra.vertices[static_cast<size_t>(apex)].x,
                azra.vertices[static_cast<size_t>(apex)].y,
                azra.vertices[static_cast<size_t>(apex)].z);
    for (const auto& uv : apexUvs) std::printf(" (%d,%d)", uv.first, uv.second);
    std::printf(" of %dx%d\n", sk::kSkyboxSkinWidth, sk::kSkyboxSkinHeight);

    Check(ringsShrink, "the mesh's rings shrink as local +Y grows -- it is a dome, closed at the top");
    Check(apexUvs.size() == 1 && std::abs(apexUvs.begin()->first - 128) <= 2 &&
              std::abs(apexUvs.begin()->second - 128) <= 2,
          "and its apex maps to the centre of the skin -- the apex is the zenith of a fisheye sky");

    // ---------------------------------------------------------------
    // Part 2 -- the texture header the engine ignores.
    // ---------------------------------------------------------------
    std::printf("\n=== Part 2: the skin the engine reads vs the one the header declares ===\n");

    int headerSaysEmpty = 0, skyboxReadNonEmpty = 0;
    for (const char* name : kZones) {
        std::vector<uint8_t> raw =
            sk::LoadCompressedZoneFile(std::string(scriptRoot) + "/" + name + ".zsk");
        sk::Model byHeader, bySkybox;
        if (!sk::ParseModelResource(raw.data(), raw.size(), byHeader)) continue;
        if (!sk::ParseSkyboxResource(raw.data(), raw.size(), bySkybox)) continue;
        if (byHeader.pixels.empty()) ++headerSaysEmpty;
        if (!bySkybox.pixels.empty()) ++skyboxReadNonEmpty;
        // Where the header is sane it must agree with the fixed rule, or
        // ParseSkyboxResource would be changing what the other 12 zones draw.
        if (!byHeader.pixels.empty()) {
            Check(byHeader.pixels.size() == bySkybox.pixels.size() &&
                      byHeader.pixels == bySkybox.pixels,
                  std::string("skin identical under both readings for ") + name);
        }
    }
    Check(headerSaysEmpty == 9, "nine .zsk texture headers declare zero pixels (`256x256x0`)");
    Check(skyboxReadNonEmpty == 21,
          "all 21 yield a full 256x256 skin under the engine's fixed skybox addressing");

    // ---------------------------------------------------------------
    // Part 3 -- how it behaves in the real renderer.
    // ---------------------------------------------------------------
    std::printf("\n=== Part 3: ZoneRenderer::Render, sky on vs sky off ===\n");

    const char* kOutdoor = "drgnfld";
    sk::Zone zone;
    if (!zone.Load(scriptRoot, kOutdoor)) {
        std::printf("m70_skybox_smoke: FAILED to load %s\n", kOutdoor);
        return 1;
    }
    Check(zone.SkyMesh() != nullptr, "the zone loads a skybox mesh");

    const uint16_t kFill = sk::kBackgroundFill;
    const size_t kFrameSize =
        static_cast<size_t>(sk::Backbuffer::kWidth) * sk::Backbuffer::kHeight;

    sk::Camera camera;
    camera.x = static_cast<float>(zone.playerStartX);
    camera.y = static_cast<float>(zone.playerStartY);
    camera.z = static_cast<float>(zone.playerStartZ) + sk::kEyeHeightOffset;
    camera.fovY = 1.2f;

    // Find a pose with a real mix of world and open sky in frame. The
    // "sky changes only background pixels" check below is worth nothing
    // aimed at empty air, and drgnfld's player start happens to look at
    // exactly that: the first pose tried fills the frame with sky.
    {
        bool found = false;
        for (int yawStep = 0; yawStep < 8 && !found; ++yawStep) {
            for (float pitch : {0.0f, -0.25f, -0.5f}) {
                sk::Camera probe = camera;
                probe.yaw = static_cast<float>(yawStep) * 0.7853981634f;  // eighths of a turn
                probe.pitch = pitch;
                Frame bare = RenderFrame(zone, probe, false);
                size_t background = 0;
                for (uint16_t p : bare.px) {
                    if (p == kFill) ++background;
                }
                if (background > kFrameSize / 5 && background < kFrameSize * 4 / 5) {
                    camera = probe;
                    found = true;
                    std::printf("  pose: yaw %.2f pitch %.2f -- %zu%% of the frame is open sky\n",
                                static_cast<double>(camera.yaw), static_cast<double>(camera.pitch),
                                background * 100 / kFrameSize);
                    break;
                }
            }
        }
        Check(found, "found a camera pose showing both world geometry and open sky");
    }

    Frame off = RenderFrame(zone, camera, false);
    Frame on = RenderFrame(zone, camera, true);
    int differing = 0, differingOverBackground = 0, backgroundPixels = 0;
    for (size_t i = 0; i < off.px.size(); ++i) {
        bool bare = off.px[i] == kFill;
        if (bare) ++backgroundPixels;
        if (off.px[i] != on.px[i]) {
            ++differing;
            if (bare) ++differingOverBackground;
        }
    }
    std::printf("  %d/%zu pixels differ with the sky on; %d of those were bare background,"
                " out of %d bare in all\n",
                differing, off.px.size(), differingOverBackground, backgroundPixels);
    Check(differing > 1000, "turning the sky on changes a substantial part of the frame");
    Check(backgroundPixels < static_cast<int>(kFrameSize),
          "and the frame it changes has real world geometry in it, so that means something");
    Check(differing == differingOverBackground,
          "and changes *only* pixels that were bare background -- the sky writes no depth,"
          " so nothing occludes the world");

    // Translation invariance: the property that makes it a skybox. Walk
    // three tiles along the view direction and compare the two frames on
    // the pixels that are sky in both.
    sk::Camera moved = camera;
    moved.x += 3.0f * sk::kTileScale * std::cos(camera.yaw);
    moved.y += 3.0f * sk::kTileScale * std::sin(camera.yaw);
    Frame movedOff = RenderFrame(zone, moved, false);
    Frame movedOn = RenderFrame(zone, moved, true);

    int commonSky = 0, movedDisagreements = 0;
    for (size_t i = 0; i < off.px.size(); ++i) {
        if (off.px[i] != kFill || movedOff.px[i] != kFill) continue;  // world in the way
        ++commonSky;
        if (on.px[i] != movedOn.px[i]) ++movedDisagreements;
    }
    std::printf("  walked 3 tiles: %d pixels are open sky in both frames, %d of them changed\n",
                commonSky, movedDisagreements);
    Check(commonSky > 500, "the two positions share enough open sky for the comparison to mean something");
    Check(movedDisagreements == 0,
          "walking does not move the sky by one pixel -- it is anchored to the camera");

    // ...and turning does move it, or it would just be wallpaper.
    sk::Camera turned = camera;
    turned.yaw = camera.yaw + 0.6f;
    Frame turnedOff = RenderFrame(zone, turned, false);
    Frame turnedOn = RenderFrame(zone, turned, true);
    int commonSkyTurned = 0, turnedDisagreements = 0;
    for (size_t i = 0; i < off.px.size(); ++i) {
        if (off.px[i] != kFill || turnedOff.px[i] != kFill) continue;
        ++commonSkyTurned;
        if (on.px[i] != turnedOn.px[i]) ++turnedDisagreements;
    }
    std::printf("  turned 34 degrees: %d pixels open sky in both, %d of them changed\n",
                commonSkyTurned, turnedDisagreements);
    Check(turnedDisagreements > 0, "turning the camera does move the sky -- it rotates with the view");

    // An interior zone's sky is black, not the port's own fill colour:
    // the blank skin really is drawn, exactly as the device would draw it.
    sk::Zone interior;
    if (interior.Load(scriptRoot, "crypt1")) {
        sk::Camera ic;
        ic.x = static_cast<float>(interior.playerStartX);
        ic.y = static_cast<float>(interior.playerStartY);
        ic.z = static_cast<float>(interior.playerStartZ) + sk::kEyeHeightOffset;
        ic.pitch = -0.5f;
        ic.fovY = 1.2f;
        Frame iOff = RenderFrame(interior, ic, false);
        Frame iOn = RenderFrame(interior, ic, true);
        int bare = 0, bareBlack = 0;
        for (size_t i = 0; i < iOff.px.size(); ++i) {
            if (iOff.px[i] != kFill) continue;
            ++bare;
            if (iOn.px[i] == 0) ++bareBlack;
        }
        std::printf("  crypt1: %d bare-background pixels, %d of them black once the sky draws\n",
                    bare, bareBlack);
        Check(bare > 0 && bare == bareBlack,
              "an interior zone's blank sky paints black, not the port's placeholder fill");
    }

    // Dumps for eyeballing -- not compared against anything, just the
    // fastest way to see whether the sky looks like a sky.
    {
        sk::Backbuffer bb;
        sk::ZoneRenderer renderer;
        renderer.Render(bb, zone, camera);
        WriteBackbufferPpm(bb, "skybox_drgnfld.ppm");
        sk::Zone night;
        if (night.Load(scriptRoot, "azra")) {
            sk::Camera nc;
            nc.x = static_cast<float>(night.playerStartX);
            nc.y = static_cast<float>(night.playerStartY);
            nc.z = static_cast<float>(night.playerStartZ) + sk::kEyeHeightOffset;
            nc.pitch = -0.85f;
            nc.yaw = 2.2f;
            nc.fovY = 1.2f;
            sk::Backbuffer nb;
            renderer.Render(nb, night, nc);
            WriteBackbufferPpm(nb, "skybox_azra.ppm");
        }
    }

    std::printf("\nm70_skybox_smoke: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
