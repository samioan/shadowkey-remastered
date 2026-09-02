// M9 debug tool: renders one frame with the camera placed at an
// arbitrary tile/yaw (not just the zone's player start) -- used to
// visually spot-check lighting variation between known bright (near a
// light-source cell) and dark (far from any) spots.
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
            f.put(static_cast<char>(((c >> 11) & 0x1f) << 3));
            f.put(static_cast<char>(((c >> 5) & 0x3f) << 2));
            f.put(static_cast<char>((c & 0x1f) << 3));
        }
    }
    std::printf("wrote %s\n", path.c_str());
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::printf("usage: render_at_smoke <tx> <ty> <yawDeg> <outPath> [zoneName]\n");
        return 1;
    }
    int tx = std::atoi(argv[1]);
    int ty = std::atoi(argv[2]);
    float yawDeg = static_cast<float>(std::atof(argv[3]));
    std::string outPath = argv[4];
    const char* zoneName = argc > 5 ? argv[5] : "azra";
    const char* scriptRoot =
        "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
        "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::Zone zone;
    if (!zone.Load(scriptRoot, zoneName)) {
        std::printf("FAILED to load zone\n");
        return 1;
    }

    const sk::ZmpCell& cell = zone.CellAt(tx, ty);
    std::printf("tile (%d,%d): lightLevel=%u brightness=%.2f isWall=%d isSource=%d\n", tx, ty,
                cell.lightLevel, sk::LightLevelToBrightness(cell.lightLevel), cell.IsWall(),
                cell.IsLightSource());

    sk::Camera camera;
    camera.x = (tx + 0.5f) * sk::kTileScale;
    camera.y = (ty + 0.5f) * sk::kTileScale;
    camera.z = static_cast<float>(zone.playerStartZ) + sk::kEyeHeightOffset;
    camera.yaw = yawDeg * 3.14159265f / 180.0f;
    camera.fovY = 1.2f;

    sk::Backbuffer backbuffer;
    sk::ZoneRenderer renderer;
    renderer.Render(backbuffer, zone, camera);
    WriteBackbufferPpm(backbuffer, outPath);
    return 0;
}
