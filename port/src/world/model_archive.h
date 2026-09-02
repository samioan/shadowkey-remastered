#pragma once

// Loads the 3D model resource archive (system/apps/6r51/models.idx +
// models.huge, docs/MODEL_FORMAT.md, verified byte-for-byte against all
// 226 non-empty real entries by tools/parse_model_resource.py) and
// parses individual model resources on demand.
//
// SIMPLIFICATION: only frame 0 (the MD2-style vertex-animation base
// pose) is kept -- animated creature models render in their resting
// pose. Per-instance keyframe selection (actor+0x70, MODEL_FORMAT.md) is
// a separate follow-up, not attempted in this first entity-rendering
// pass (port/docs/PORT_ROADMAP.md's M8).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sk {

struct ModelVertex {
    int16_t x = 0, y = 0, z = 0;
};

struct ModelUv {
    uint16_t u = 0, v = 0;
};

struct ModelFace {
    int16_t vA = 0, vB = 0, vC = 0;
    int16_t uA = 0, uB = 0, uC = 0;
};

struct Model {
    std::vector<ModelVertex> vertices;  // frame 0 only, vertsPerFrame entries
    std::vector<ModelUv> uvs;
    std::vector<ModelFace> faces;
    int skinCount = 0;
    int width = 0, height = 0;

    // Raw 16bpp texel at (x,y) within skin `skinIndex` -- same 4-bit-per-
    // channel 0x0RGB convention as everything else in the engine's
    // framebuffer (docs/GRAPHICS_FORMAT.md), NOT palettized like the
    // zone wall/floor textures (world/zone.h's TexelAt/PaletteColor) --
    // models carry their own raw pixel data directly. Returns the
    // chroma-key value (0x0f0f) if out of range.
    uint16_t TexelAt(int skinIndex, int x, int y) const;

    std::vector<uint16_t> pixels;  // skinCount * width * height, row-major
};

class ModelArchive {
public:
    // scriptRoot e.g. ".../system/apps/6r51" -- reads models.idx/.huge
    // from it directly. Loads the whole (small, ~5MB) archive into
    // memory up front; individual resources are parsed lazily on first
    // GetModel() and cached (most zones reference only a fraction of the
    // 237 real entries).
    bool Load(const std::string& scriptRoot);

    // Returns nullptr for an out-of-range index, the zero-size "NULL.bin"
    // placeholder entries (see docs/ZONE_FORMAT.md's <zone>_models.txt
    // writeup -- typeId 0/"!NO_ENTITY" resolves to one of these), or a
    // resource too small to plausibly hold the 14-byte header.
    const Model* GetModel(int archiveIndex);

    int entryCount() const { return static_cast<int>(index_.size()); }

private:
    struct IndexEntry {
        uint32_t offset = 0, size = 0;
    };

    std::vector<IndexEntry> index_;
    std::vector<uint8_t> huge_;
    std::vector<std::unique_ptr<Model>> cache_;  // parallel to index_; null until parsed/found-empty
};

}  // namespace sk
