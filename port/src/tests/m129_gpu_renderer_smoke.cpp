// M129 milestone smoke test: the Direct3D 11 renderer.
//
// The software rasterizer is the reference, so that is what this measures
// against:
//
//   1. The display list (render3d/gpu_frame.h) -- that RenderGpu records
//      the frame Render draws: the same triangles at any target size, the
//      sky first, every surface carrying its texture / hue / detail mask,
//      and the object-ID buffer left alone.
//   2. The settings: the Renderer row, its file key, and the size the card
//      is asked to draw at for each Resolution.
//   3. The card itself, on WARP -- Windows' own software Direct3D 11
//      device, present on every machine and on CI, so this needs no GPU.
//      The same scene is drawn by both renderers and compared pixel for
//      pixel: at the device's 176x208, at 4x, down a corridor, outdoors
//      under a sky, with a flashing creature and a blended billboard.
//      They do not match to the last pixel and are not meant to (the card
//      fills triangle edges by the top-left rule, the software path tests
//      pixel centres inclusively), so the check is a high fraction of
//      identical pixels plus a small mean difference over the rest.
//
// Run from the repo root (reads the real game data).

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "assets/sprite_archive.h"
#include "engine/pc_settings.h"
#include "graphics/backbuffer.h"
#include "platform/win32/d3d11_renderer.h"
#include "render3d/camera.h"
#include "render3d/gpu_frame.h"
#include "render3d/zone_renderer.h"
#include "world/entity_types.h"
#include "world/model_archive.h"
#include "world/zone.h"

namespace {

int g_Failures = 0;
int g_Checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_Checks;
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what.c_str());
    if (!ok) ++g_Failures;
}

struct Comparison {
    double identical = 0.0;  // fraction of pixels equal once both are RGB565
    double meanDiff = 0.0;   // mean per-channel difference, 0..255, both as RGB565
};

uint32_t Widen565(uint16_t c) {
    const uint32_t r = (c >> 11) & 0x1f, g = (c >> 5) & 0x3f, b = c & 0x1f;
    return ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
}

Comparison Compare(const sk::Backbuffer& software, const std::vector<uint32_t>& gpu, int w, int h) {
    Comparison out;
    if (software.width() != w || software.height() != h || gpu.size() != static_cast<size_t>(w) * h) {
        return out;
    }
    size_t same = 0;
    double diff = 0.0;
    for (int y = 0; y < h; ++y) {
        const uint16_t* row = software.Row(y);
        for (int x = 0; x < w; ++x) {
            const uint32_t g = gpu[static_cast<size_t>(y) * w + x];
            const uint16_t g565 = sk::PackRGB565(static_cast<uint8_t>(g >> 16),
                                                 static_cast<uint8_t>(g >> 8),
                                                 static_cast<uint8_t>(g));
            if (g565 == row[x]) ++same;
            // Both through RGB565, the software frame's own precision: the
            // card keeps the full RGB444 expansion, which is a constant
            // rounding difference and not a disagreement.
            const uint32_t s = Widen565(row[x]);
            const uint32_t q = Widen565(g565);
            for (int shift = 0; shift <= 16; shift += 8) {
                diff += std::abs(static_cast<int>((s >> shift) & 0xff) -
                                 static_cast<int>((q >> shift) & 0xff));
            }
        }
    }
    const double n = static_cast<double>(w) * h;
    out.identical = static_cast<double>(same) / n;
    out.meanDiff = diff / (n * 3.0);
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string root =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::EntityTypeTable types;
    sk::ModelArchive models;
    sk::Zone azra;
    if (!types.Load(root) || !models.Load(root) || !azra.Load(root, "azra")) {
        std::printf("m129_gpu_renderer_smoke: FAILED to load azra\n");
        return 1;
    }
    auto placed = [&](const sk::Zone& zone) {
        std::vector<sk::PlacedEntity> out;
        int id = 1;
        for (const auto& e : zone.entities()) {
            const sk::EntityTypeDescriptor* desc = types.Lookup(e.typeId);
            if (!desc) continue;
            sk::PlacedEntity pe;
            pe.x = static_cast<float>(e.x);
            pe.y = static_cast<float>(e.y);
            pe.z = static_cast<float>(e.z);
            pe.modelArchiveIndex = desc->modelArchiveIndex;
            pe.objectId = (id++ % 250) + 1;
            out.push_back(pe);
        }
        return out;
    };
    auto startCamera = [](const sk::Zone& zone, float yaw) {
        sk::Camera camera;
        camera.x = static_cast<float>(zone.playerStartX);
        camera.y = static_cast<float>(zone.playerStartY);
        camera.z = zone.playerStartZ + sk::kEyeHeightOffset;
        camera.yaw = yaw;
        return camera;
    };
    const std::vector<sk::PlacedEntity> azraEntities = placed(azra);

    // ---------------------------------------------------------------
    std::printf("== Part 1: the display list ==\n");
    // ---------------------------------------------------------------
    {
        sk::ZoneRenderer renderer;
        const sk::Camera camera = startCamera(azra, 0.0f);
        sk::Backbuffer native;
        renderer.Render(native, azra, camera, azraEntities, &models);
        std::vector<uint8_t> idsBefore;
        for (int y = 0; y < sk::Backbuffer::kHeight; y += 4) {
            for (int x = 0; x < sk::Backbuffer::kWidth; x += 4) {
                idsBefore.push_back(renderer.ObjectIdAt(x, y));
            }
        }

        sk::GpuFrame frame;
        renderer.RenderGpu(frame, sk::Backbuffer::kWidth, sk::Backbuffer::kHeight, azra, camera,
                           azraEntities, &models);
        Check(frame.width == 176 && frame.height == 208 && frame.zone == &azra,
              "RenderGpu records the frame for the target it was given");
        Check(!frame.vertices.empty() && frame.vertices.size() % 3 == 0,
              "it is a triangle list (" + std::to_string(frame.triangleCount()) + " triangles in " +
                  std::to_string(frame.batches.size()) + " batches)");

        size_t covered = 0;
        bool contiguous = true, wholeTriangles = true, skyFirst = true, seenOther = false;
        int surfaces = 0, modelBatches = 0, skies = 0;
        for (const sk::GpuBatch& batch : frame.batches) {
            if (batch.firstVertex != covered) contiguous = false;
            if (batch.vertexCount % 3 != 0) wholeTriangles = false;
            covered += batch.vertexCount;
            if (batch.kind == sk::GpuBatch::Kind::Sky) {
                ++skies;
                if (seenOther) skyFirst = false;
            } else {
                seenOther = true;
            }
            if (batch.kind == sk::GpuBatch::Kind::Surface) ++surfaces;
            if (batch.kind == sk::GpuBatch::Kind::Model) ++modelBatches;
        }
        Check(contiguous && wholeTriangles && covered == frame.vertices.size(),
              "the batches cover every vertex once, in order, in whole triangles");
        Check(surfaces >= 1 && modelBatches >= 1,
              "walls and models are both in it (" + std::to_string(surfaces) + " surface, " +
                  std::to_string(modelBatches) + " model batches)");
        Check((azra.SkyMesh() == nullptr) == (skies == 0) && skyFirst,
              "the sky is drawn first, under everything, when the zone has one");

        bool masksOk = true, texturesOk = true, depthOk = true;
        for (const sk::GpuBatch& batch : frame.batches) {
            for (uint32_t i = 0; i < batch.vertexCount; ++i) {
                const sk::GpuVertex& v = frame.vertices[batch.firstVertex + i];
                if (!(v.invW > 0.0f) || v.invW > 51.5f) depthOk = false;
                if (batch.kind != sk::GpuBatch::Kind::Surface) continue;
                const uint32_t mask = (v.params >> 16) & 0xff;
                if (mask != 0x7f && mask != 0x7e && mask != 0x7c && mask != 0x78) masksOk = false;
                if (static_cast<int>(v.params & 0xff) >= azra.textureSlotCount()) texturesOk = false;
            }
        }
        Check(masksOk, "every surface vertex carries one of the engine's four detail masks");
        Check(texturesOk, "and a texture slot the zone has (" +
                              std::to_string(azra.textureSlotCount()) + " slots)");
        Check(depthOk, "every vertex is in front of the near plane");

        bool idsKept = true;
        size_t k = 0;
        for (int y = 0; y < sk::Backbuffer::kHeight; y += 4) {
            for (int x = 0; x < sk::Backbuffer::kWidth; x += 4) {
                if (renderer.ObjectIdAt(x, y) != idsBefore[k++]) idsKept = false;
            }
        }
        Check(idsKept, "recording a GPU frame leaves the melee probes' object IDs alone");

        // Twice the size is the same frame, scaled: same triangles, every
        // vertex at twice the pixel coordinates.
        sk::GpuFrame doubled;
        renderer.RenderGpu(doubled, 352, 416, azra, camera, azraEntities, &models);
        bool scaled = doubled.vertices.size() == frame.vertices.size();
        for (size_t i = 0; scaled && i < frame.vertices.size(); ++i) {
            const sk::GpuVertex& a = frame.vertices[i];
            const sk::GpuVertex& b = doubled.vertices[i];
            if (std::fabs(b.x - 2.0f * a.x) > 0.01f + 1e-4f * std::fabs(b.x) ||
                std::fabs(b.y - 2.0f * a.y) > 0.01f + 1e-4f * std::fabs(b.y) ||
                b.invW != a.invW || b.params != a.params) {
                scaled = false;
            }
        }
        Check(scaled, "a 2x target is the same triangle list at twice the coordinates");

        // Any size at all, not only whole multiples: a 1080p window.
        sk::GpuFrame odd;
        renderer.RenderGpu(odd, 1920, 1080, azra, camera, azraEntities, &models);
        Check(odd.width == 1920 && odd.height == 1080 && odd.triangleCount() > frame.triangleCount() / 2,
              "an arbitrary target (1920x1080) records too");
        sk::GpuFrame none;
        renderer.RenderGpu(none, 0, 0, azra, camera, azraEntities, &models);
        Check(none.vertices.empty() && none.batches.empty(), "a zero-sized target records nothing");
    }

    // ---------------------------------------------------------------
    std::printf("\n== Part 2: the Renderer setting ==\n");
    // ---------------------------------------------------------------
    {
        sk::PcSettings defaults;
        Check(!defaults.gpuRenderer() && !defaults.smoothTextures(),
              "the default renderer is the original software rasterizer");

        const std::string path =
            (std::filesystem::temp_directory_path() / "sk_m129_pc_settings.cfg").string();
        std::remove(path.c_str());
        {
            sk::PcSettingsStore store(path);
            Check(store.RowCount() == sk::PcSettingsStore::kRowRenderer + 1,
                  "the Renderer row is the last PC row, after Mouse Look");
            Check(store.RowText(sk::PcSettingsStore::kRowRenderer, 0) == "Renderer: Original",
                  "it reads \"Renderer: Original\" by default");
            store.Cycle(sk::PcSettingsStore::kRowRenderer, +1);
            Check(store.settings().gpuRenderer() && !store.settings().smoothTextures() &&
                      store.RowText(sk::PcSettingsStore::kRowRenderer, 0) == "Renderer: Direct3D",
                  "one step is Direct3D");
            store.Cycle(sk::PcSettingsStore::kRowRenderer, +1);
            Check(store.settings().smoothTextures() &&
                      store.RowText(sk::PcSettingsStore::kRowRenderer, 0) == "Renderer: D3D Smooth",
                  "the next is Direct3D with smooth textures");
            store.SetGpuUnavailable(true);
            Check(store.RowText(sk::PcSettingsStore::kRowRenderer, 0) == "Renderer: Original" &&
                      store.settings().smoothTextures(),
                  "without Direct3D 11 the row says what is drawing, and keeps the choice");
            store.SetGpuUnavailable(false);
        }
        {
            sk::PcSettingsStore reloaded(path);
            Check(reloaded.Load() && reloaded.settings().renderer() == sk::kRendererGpuSmooth,
                  "the choice round-trips through pc_settings.cfg");
            reloaded.Cycle(sk::PcSettingsStore::kRowRenderer, +1);
            Check(!reloaded.settings().gpuRenderer(), "and wraps back to Original");
        }
        std::remove(path.c_str());

        int w = 0, h = 0;
        sk::PcSettings s;
        sk::GpuSceneSizeFor(s, 176, 914, 1080, &w, &h);
        Check(w == 176 && h == 208, "Resolution Original draws the card's scene at 176x208");
        s.resolutionIndex = 1;  // Auto
        sk::GpuSceneSizeFor(s, 176, 914, 1080, &w, &h);
        Check(w == 914 && h == 1080, "Auto is the window's own rectangle, not a whole multiple");
        sk::GpuSceneSizeFor(s, 370, 3840, 2160, &w, &h);
        Check(w == 3840 && h == 2160, "...with no 6x ceiling: a 4K window is drawn at 4K");
        s.resolutionIndex = 4;  // 4x
        sk::GpuSceneSizeFor(s, 370, 1920, 1080, &w, &h);
        Check(w == 1480 && h == 832, "a fixed multiple is that multiple of the view");
    }

    // ---------------------------------------------------------------
    std::printf("\n== Part 3: the card, against the software rasterizer (WARP) ==\n");
    // ---------------------------------------------------------------
    {
        sk::D3D11Renderer gpu;
        if (!gpu.Init(/*warp=*/true)) {
            // No Direct3D 11 at all (a stripped container, say). The game
            // falls back to software in the same situation; so does this.
            std::printf("  (Direct3D 11 unavailable: %s -- GPU comparison skipped)\n",
                        gpu.lastError().c_str());
        } else {
            std::printf("  device: %s\n", gpu.adapterName().c_str());
            sk::ZoneRenderer renderer;
            std::vector<uint32_t> pixels;
            int w = 0, h = 0;

            auto compareAt = [&](const sk::Zone& zone, const sk::Camera& camera,
                                 const std::vector<sk::PlacedEntity>& entities,
                                 const std::vector<sk::SpriteBillboard>& billboards, int width,
                                 int height) {
                sk::Backbuffer software(width, height);
                renderer.Render(software, zone, camera, entities, &models, billboards);
                sk::GpuFrame frame;
                renderer.RenderGpu(frame, width, height, zone, camera, entities, &models,
                                   billboards);
                Comparison c;
                if (gpu.DrawScene(frame, sk::D3D11Renderer::Filter::Original) &&
                    gpu.ReadScene(pixels, &w, &h)) {
                    c = Compare(software, pixels, w, h);
                }
                return c;
            };
            auto report = [](const char* what, const Comparison& c) {
                char buf[200];
                std::snprintf(buf, sizeof(buf), "%s: %.1f%% identical, mean difference %.2f / 255",
                              what, c.identical * 100.0, c.meanDiff);
                return std::string(buf);
            };

            // Four ways round from the start, at the device's own size.
            double worst = 1.0, worstDiff = 0.0;
            for (int turn = 0; turn < 4; ++turn) {
                const Comparison c = compareAt(azra, startCamera(azra, static_cast<float>(turn) * 1.5708f),
                                               azraEntities, {}, 176, 208);
                worst = std::min(worst, c.identical);
                worstDiff = std::max(worstDiff, c.meanDiff);
            }
            Check(worst >= 0.97 && worstDiff <= 1.0,
                  report("azra at 176x208, four headings (worst)", Comparison{worst, worstDiff}));

            const Comparison big = compareAt(azra, startCamera(azra, 0.0f), azraEntities, {}, 704, 832);
            Check(big.identical >= 0.99 && big.meanDiff <= 0.5, report("azra at 4x (704x832)", big));

            sk::Camera pitched = startCamera(azra, 0.7f);
            pitched.pitch = 0.4f;
            const Comparison tilt = compareAt(azra, pitched, azraEntities, {}, 370, 208);
            Check(tilt.identical >= 0.97 && tilt.meanDiff <= 1.0,
                  report("a wide 16:9 view, looking down", tilt));

            // Outdoors: a sky, and fog that fades to a pale haze.
            sk::Zone snowline;
            if (snowline.Load(root, "snowline")) {
                const std::vector<sk::PlacedEntity> ents = placed(snowline);
                const Comparison c = compareAt(snowline, startCamera(snowline, 0.0f), ents, {}, 352, 416);
                Check(snowline.SkyMesh() != nullptr && c.identical >= 0.99 && c.meanDiff <= 0.5,
                      report("snowline under its sky at 2x", c));
            } else {
                Check(false, "snowline loads");
            }

            // A flashing creature and a blended billboard, both a few tiles
            // in front of the camera.
            {
                const sk::Camera camera = startCamera(azra, 0.0f);
                sk::PlacedEntity flashing;
                flashing.x = camera.x + 420.0f;
                flashing.y = camera.y;
                flashing.z = static_cast<float>(azra.playerStartZ);
                flashing.modelArchiveIndex = 23;  // Bandit_Thug
                flashing.flashLevel = 3;
                sk::Sprite sprite;
                sprite.width = 16;
                sprite.height = 16;
                sprite.pixels.assign(256, sk::PackRGB565(255, 160, 32));
                sprite.opaque.assign(256, true);
                for (int i = 0; i < 16; ++i) {
                    sprite.opaque[static_cast<size_t>(i) * 16 + i] = false;  // a keyed diagonal
                }
                sk::SpriteBillboard solid, blended;
                solid.x = camera.x + 300.0f;
                solid.y = camera.y + 90.0f;
                solid.z = camera.z - 100.0f;
                solid.sprite = &sprite;
                solid.halfWidth = 40;
                solid.halfHeight = 40;
                blended = solid;
                blended.y = camera.y - 90.0f;
                blended.blendMode = 1;
                blended.blendLevel = 0x80;  // FUN_1004f218's rounding -> level 2, half
                const Comparison c =
                    compareAt(azra, camera, {flashing}, {solid, blended}, 352, 416);
                Check(c.identical >= 0.97 && c.meanDiff <= 1.0,
                      report("a hit-flashed creature and two billboards", c));
                // And they are really there: the flash is a red ramp.
                bool red = false;
                for (uint32_t p : pixels) {
                    const int r = (p >> 16) & 0xff, g = (p >> 8) & 0xff, b = p & 0xff;
                    if (r > 150 && g < 60 && b < 60) red = true;
                }
                Check(red, "the flashed creature is drawn out of the red ramp");
            }

            // The caches: a second frame of the same zone uploads nothing.
            {
                const sk::Camera camera = startCamera(azra, 0.3f);
                sk::GpuFrame frame;
                renderer.RenderGpu(frame, 352, 416, azra, camera, azraEntities, &models);
                gpu.DrawScene(frame, sk::D3D11Renderer::Filter::Original);
                const int before = gpu.lastStats().texturesCreated;
                gpu.DrawScene(frame, sk::D3D11Renderer::Filter::Original);
                const sk::D3D11Renderer::Stats stats = gpu.lastStats();
                Check(stats.texturesCreated == before && stats.drawCalls > 0 &&
                          stats.triangles == static_cast<int>(frame.triangleCount()),
                      "a second frame reuses every texture (" + std::to_string(stats.drawCalls) +
                          " draw calls, " + std::to_string(stats.triangles) + " triangles)");

                // Smooth: draws, and is a different picture from Original
                // without being a different scene.
                std::vector<uint32_t> original, smooth;
                gpu.ReadScene(original, &w, &h);
                const bool drew = gpu.DrawScene(frame, sk::D3D11Renderer::Filter::Smooth) &&
                                  gpu.ReadScene(smooth, &w, &h);
                size_t changed = 0;
                double diff = 0.0;
                for (size_t i = 0; drew && i < original.size(); ++i) {
                    if (original[i] != smooth[i]) ++changed;
                    for (int shift = 0; shift <= 16; shift += 8) {
                        diff += std::abs(static_cast<int>((original[i] >> shift) & 0xff) -
                                         static_cast<int>((smooth[i] >> shift) & 0xff));
                    }
                }
                const double mean = original.empty() ? 0.0 : diff / (original.size() * 3.0);
                char buf[160];
                std::snprintf(buf, sizeof(buf),
                              "Smooth filtering changes %.0f%% of the pixels by a mean of %.1f / 255",
                              original.empty() ? 0.0 : 100.0 * changed / original.size(), mean);
                Check(drew && changed > original.size() / 10 && mean < 12.0, buf);
            }
        }
    }

    std::printf("\nm129_gpu_renderer_smoke: %d/%d checks passed\n", g_Checks - g_Failures, g_Checks);
    return g_Failures == 0 ? 0 : 1;
}
