// M8 smoke test: renders one real frame of a zone including placed
// entities (render3d/zone_renderer.h's PlacedEntity path) from the
// player-start position at a few yaw angles, dumping each as a PPM --
// the same technique m6_zone_render_smoke.cpp uses for tile geometry,
// extended to also resolve and submit real .ent placements.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

#include "graphics/backbuffer.h"
#include "render3d/camera.h"
#include "render3d/zone_renderer.h"
#include "world/entity_types.h"
#include "world/model_archive.h"
#include "world/zone.h"

namespace {

void WriteBackbufferPpm(const sk::Backbuffer& bb, const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    f << "P6\n" << sk::Backbuffer::kWidth << " " << sk::Backbuffer::kHeight << "\n255\n";
    for (int y = 0; y < sk::Backbuffer::kHeight; ++y) {
        const uint16_t* row = bb.Row(y);
        for (int x = 0; x < sk::Backbuffer::kWidth; ++x) {
            uint16_t c = row[x];
            uint8_t r = static_cast<uint8_t>(((c >> 11) & 0x1f) << 3);
            uint8_t g = static_cast<uint8_t>(((c >> 5) & 0x3f) << 2);
            uint8_t b = static_cast<uint8_t>((c & 0x1f) << 3);
            f.put(static_cast<char>(r));
            f.put(static_cast<char>(g));
            f.put(static_cast<char>(b));
        }
    }
    std::printf("wrote %s\n", path.c_str());
}

}  // namespace

int main(int argc, char** argv) {
    const std::string scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";
    const char* zoneName = argc > 2 ? argv[2] : "azra";

    sk::EntityTypeTable types;
    sk::ModelArchive models;
    sk::Zone zone;
    if (!types.Load(scriptRoot) || !models.Load(scriptRoot) || !zone.Load(scriptRoot, zoneName)) {
        std::printf("m8_render_entities_smoke: FAILED to load\n");
        return 1;
    }

    std::vector<sk::PlacedEntity> entities;
    for (const auto& e : zone.entities()) {
        const sk::EntityTypeDescriptor* desc = types.Lookup(e.typeId);
        if (!desc) continue;
        entities.push_back({static_cast<float>(e.x), static_cast<float>(e.y),
                             static_cast<float>(e.z), desc->modelArchiveIndex});
    }
    std::printf("resolved %zu / %zu placed entities\n", entities.size(), zone.entities().size());

    // DEBUG: dump the nearest few entities to player start with their
    // resolved model's vertex bounding box, to sanity-check the
    // position/scale assumptions in render3d/zone_renderer.cpp.
    {
        std::vector<std::pair<float, size_t>> byDist;
        for (size_t i = 0; i < entities.size(); ++i) {
            float dx = entities[i].x - static_cast<float>(zone.playerStartX);
            float dy = entities[i].y - static_cast<float>(zone.playerStartY);
            byDist.push_back({dx * dx + dy * dy, i});
        }
        std::sort(byDist.begin(), byDist.end());
        for (int i = 0; i < 6 && i < static_cast<int>(byDist.size()); ++i) {
            const sk::PlacedEntity& pe = entities[byDist[static_cast<size_t>(i)].second];
            const sk::Zone::EntPlacement& raw = zone.entities()[byDist[static_cast<size_t>(i)].second];
            const sk::EntityTypeDescriptor* desc = types.Lookup(raw.typeId);
            const sk::Model* model = models.GetModel(pe.modelArchiveIndex);
            float dist = std::sqrt(byDist[static_cast<size_t>(i)].first);
            std::printf("dist=%.0f typeId=%d name=%s archiveIdx=%d pos=(%.0f,%.0f,%.0f)", dist,
                        raw.typeId, desc ? desc->name.c_str() : "?", pe.modelArchiveIndex, pe.x,
                        pe.y, pe.z);
            if (model) {
                int16_t minX = 32767, maxX = -32768, minY = 32767, maxY = -32768, minZ = 32767,
                        maxZ = -32768;
                for (const auto& v : model->vertices) {
                    minX = std::min(minX, v.x);
                    maxX = std::max(maxX, v.x);
                    minY = std::min(minY, v.y);
                    maxY = std::max(maxY, v.y);
                    minZ = std::min(minZ, v.z);
                    maxZ = std::max(maxZ, v.z);
                }
                std::printf(" verts=%zu bbox=(%d..%d, %d..%d, %d..%d)", model->vertices.size(),
                            minX, maxX, minY, maxY, minZ, maxZ);
            } else {
                std::printf(" (no model)");
            }
            std::printf("\n");
        }
    }

    sk::Camera camera;
    camera.x = static_cast<float>(zone.playerStartX);
    camera.y = static_cast<float>(zone.playerStartY);
    camera.z = static_cast<float>(zone.playerStartZ) + sk::kEyeHeightOffset;
    camera.fovY = sk::kEngineFovY;

    sk::Backbuffer backbuffer;
    sk::ZoneRenderer renderer;
    for (int yawDeg : {0, 90, 180, 270}) {
        camera.yaw = yawDeg * 3.14159265f / 180.0f;
        renderer.Render(backbuffer, zone, camera, entities, &models);
        WriteBackbufferPpm(backbuffer, "entity_render_yaw" + std::to_string(yawDeg) + ".ppm");
    }

    std::printf("m8_render_entities_smoke: OK\n");
    return 0;
}
