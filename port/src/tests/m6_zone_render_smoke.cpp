// M6 smoke test: renders one real frame of a zone's tile-grid geometry
// (render3d/zone_renderer.h) from the zone's actual player-start position
// and dumps it as a PPM image -- much faster to iterate on than the
// interactive window, and directly viewable to sanity-check the
// projection/rasterization/texturing pipeline end to end.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

#include "graphics/backbuffer.h"
#include "render3d/camera.h"
#include "render3d/zone_renderer.h"
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
    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";
    const char* zoneName = argc > 2 ? argv[2] : "azra";
    float yawDegrees = argc > 3 ? static_cast<float>(std::atof(argv[3])) : 0.0f;

    sk::Zone zone;
    if (!zone.Load(scriptRoot, zoneName)) {
        std::printf("zone_render_smoke: FAILED to load zone %s\n", zoneName);
        return 1;
    }

    sk::Camera camera;
    camera.x = static_cast<float>(zone.playerStartX);
    camera.y = static_cast<float>(zone.playerStartY);
    camera.z = static_cast<float>(zone.playerStartZ) + sk::kEyeHeightOffset;
    camera.yaw = yawDegrees * 3.14159265f / 180.0f;
    camera.fovY = sk::kEngineFovY;

    sk::Backbuffer backbuffer;
    sk::ZoneRenderer renderer;
    renderer.Render(backbuffer, zone, camera);

    WriteBackbufferPpm(backbuffer, "zone_render.ppm");
    std::printf("zone_render_smoke: OK (camera at tile %d,%d yaw=%.0fdeg)\n",
                zone.playerStartX >> 8, zone.playerStartY >> 8, yawDegrees);
    return 0;
}
