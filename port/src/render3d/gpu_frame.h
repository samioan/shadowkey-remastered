#pragma once
#include <cstdint>
#include <vector>

// M129: one 3D frame as a list of triangles for a GPU to draw.
//
// The software renderer (zone_renderer.cpp) does everything up to the
// rasterizer in floating point already: the visibility raycast, the face
// build, the transform, the near clip and the projection. Since M119 it can
// *record* that work instead of rasterizing it on the spot. A GpuFrame is the
// same record handed to a graphics card instead of to the band threads -- so
// the GPU path draws exactly the triangles the software path would, lit and
// fogged by exactly the per-vertex scalars the software path interpolates,
// and the only thing that moves to the card is the per-pixel work that made
// a high Resolution expensive (M126: 10-14 ms of a 6x frame).
//
// It is backend-neutral on purpose: nothing here names Direct3D, so the
// record can be checked by a test and drawn by whatever backend exists
// (platform/win32/d3d11_renderer.h is the one that does).
//
// What the pixel stage has to do with it is the engine's own per-pixel
// arithmetic, unchanged:
//
//   surface:  texel = ztx[tex][v & mask][u & mask]
//             rgb444 = zlu[(clamp(light >> 8, 4, 63) + hue) * 256 + texel]
//             if (rgb444 == 0xf0f) discard
//             rgb444 = zfg[(fog >> 12) << 12 | rgb444]
//   model:    rgb444 = skin[clamp(v)][clamp(u)];  0xf0f discards
//             flash >= 0 ? ramp[flash][rgb444 >> 8 & 0xf] : zfg[...]
//   sky:      rgb444 = skin[...], no depth, no key, no fog
//   sprite:   the 0xf0f mask, the depth test, and the 1/4-1/2-3/4 blend
//
// `light` and `fog` are interpolated in screen space (the engine steps them
// per scanline); `u`/`v` are perspective-correct.

namespace sk {

class Zone;
struct Model;
struct Sprite;

struct GpuVertex {
    float x = 0, y = 0;    // target pixels, origin top-left, pixel centres at +0.5
    float invW = 0;        // 1 / view depth in tile units; larger is nearer
    float uOverW = 0, vOverW = 0;  // texel coordinates already divided by depth
    float light = 0;       // ZmpCell::lightLevel fixed point, [0x400, 0x3f00]
    float fog = 0;         // 0..0xffff; the top nibble is the .zfg level
    // Surface triangles: textureIndex | hueGroup << 8 | detailMask << 16.
    // Unused (0) for the other kinds.
    uint32_t params = 0;
};

struct GpuBatch {
    enum class Kind : uint8_t { Sky, Surface, Model, Billboard };
    Kind kind = Kind::Surface;
    uint32_t firstVertex = 0;
    uint32_t vertexCount = 0;  // a triangle list: always a multiple of 3
    // Model / Sky.
    const Model* model = nullptr;
    int skinIndex = 0;
    int flashLevel = -1;  // >= 0: the hit-flash ramp row (HitFlashRamps)
    // Billboard.
    const Sprite* sprite = nullptr;
    int blendMode = 0;  // 1 = blended at `blendLevel` quarters (1..3)
    int blendLevel = 0;
};

struct GpuFrame {
    int width = 0, height = 0;  // the target the vertices were projected for
    const Zone* zone = nullptr;
    uint16_t background565 = 0;  // the flat fill under a zone with no sky
    std::vector<GpuVertex> vertices;
    // In draw order: the sky first (it is the frame clear), then surfaces,
    // models and billboards as the engine submits them.
    std::vector<GpuBatch> batches;

    void Clear() {
        vertices.clear();
        batches.clear();
        zone = nullptr;
    }
    size_t triangleCount() const { return vertices.size() / 3; }
};

}  // namespace sk
