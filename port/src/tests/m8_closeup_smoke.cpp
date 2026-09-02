// M8 debug smoke test: places the camera at a fixed offset from one
// specific real entity and renders just that entity (no tile geometry
// clutter) to directly inspect its shape/orientation/scale in isolation
// -- used to check the local-axis (Y-up) fix and the position/scale
// assumptions documented in render3d/zone_renderer.cpp.
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

int main() {
    const std::string scriptRoot =
        "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
        "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::EntityTypeTable types;
    sk::ModelArchive models;
    sk::Zone zone;
    if (!types.Load(scriptRoot) || !models.Load(scriptRoot) || !zone.Load(scriptRoot, "azra")) {
        std::printf("m8_closeup_smoke: FAILED to load\n");
        return 1;
    }

    // The nearest barrel to player start, per m8_render_entities_smoke's
    // debug dump: typeId=6, pos=(29824,12160,-3680).
    sk::PlacedEntity target{29824.0f, 12160.0f, -3680.0f, 5};

    sk::Backbuffer backbuffer;
    sk::ZoneRenderer renderer;
    std::vector<sk::PlacedEntity> one = {target};

    for (int i = 0; i < 4; ++i) {
        float angle = i * 3.14159265f / 2.0f;
        // Stand kTileScale (one tile) back from the entity, at roughly
        // its own height, looking at it.
        sk::Camera camera;
        camera.x = target.x - std::cos(angle) * 256.0f;
        camera.y = target.y - std::sin(angle) * 256.0f;
        camera.z = target.z + 100.0f;
        camera.yaw = angle;
        camera.fovY = 1.2f;
        renderer.Render(backbuffer, zone, camera, one, &models);
        WriteBackbufferPpm(backbuffer, "closeup_barrel_" + std::to_string(i) + ".ppm");
    }

    std::printf("m8_closeup_smoke: OK\n");
    return 0;
}
