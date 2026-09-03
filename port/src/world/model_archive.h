#pragma once

// Loads the 3D model resource archive (system/apps/6r51/models.idx +
// models.huge, docs/MODEL_FORMAT.md, verified byte-for-byte against all
// 226 non-empty real entries by tools/parse_model_resource.py) and
// parses individual model resources on demand.
//
// M28: **all** animation frames are now kept (M8 kept only frame 0, so
// every creature rendered as a static resting-pose statue), along with the
// per-model animation clip table -- see AnimationClip below.

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

// One named animation range, from the resource's trailer.
//
// **Decoded this session against real data**, resolving what
// docs/MODEL_FORMAT.md flagged as unresolved ("the trailer's purpose ...
// is unresolved -- flagged as an open follow-up"). The trailer is always a
// whole number of 6-byte records, and each record is
// `(startFrame u16, endFrame u16, rate u16)`:
//
//   entry 18  (57 frames, 4 clips): (0,11,3) (11,22,10) (22,42,10) (42,57,10)
//   entry 59 (157 frames, 9 clips): (0,1,10) (1,19,10) ... (135,157,1)
//   entry 20 (144 frames, 11 clips): (0,1,10) (1,22,10) ... (138,144,10)
//
// The records **exactly partition** `[0, frameCount)` -- each one's start
// is the previous one's end, and the last end equals the header's own
// frame count -- in every animated entry in the archive, which is what
// pins this down rather than leaving it a guess. The clip *count* also
// matches how real scripts index them: `arat.s` names clips 0-3 and its
// model carries 4; the monsters that name clip 8 resolve to models with 9
// or 11. That is exactly what `SetIdleAnimation`/`SetWalkAnimation`/
// `SetSwingAnimation`/`SetDeathAnimation`/`PlayAnimation` pass.
//
// `rate`'s units are **not** confirmed -- observed values are 1, 3, 5, 6,
// 10, 11, 15, 17, 29, and this port reads them as frames per second (see
// world/model_archive.cpp), which produces sane-looking playback but is an
// interpretation, not a decompiled fact.
struct AnimationClip {
    int startFrame = 0;
    int endFrame = 0;  // exclusive
    int rate = 10;     // frames per second, unconfirmed -- see above
    int frameCount() const { return endFrame - startFrame; }
};

struct Model {
    // All frames back to back: `frameCount * vertsPerFrame` entries, so
    // frame N's vertices start at `N * vertsPerFrame`. Use VertexAt().
    std::vector<ModelVertex> vertices;
    std::vector<ModelUv> uvs;
    std::vector<ModelFace> faces;
    int skinCount = 0;
    int width = 0, height = 0;
    int frameCount = 1;
    int vertsPerFrame = 0;
    std::vector<AnimationClip> clips;

    // Vertex `vertexIndex` of frame `frameIndex`, with the frame clamped
    // into range (a static prop is 1 frame, so every caller can pass a
    // frame index unconditionally).
    const ModelVertex& VertexAt(int frameIndex, int vertexIndex) const;

    // The clip a script's animation number names, or nullptr if this model
    // doesn't carry that many.
    const AnimationClip* clip(int index) const {
        if (index < 0 || static_cast<size_t>(index) >= clips.size()) return nullptr;
        return &clips[static_cast<size_t>(index)];
    }

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

// Parses one already-decompressed MODEL_FORMAT.md resource blob into
// `out` -- the same per-resource logic ModelArchive::GetModel() uses for
// a models.huge slot, factored out so world/zone.h's M11 room-mesh
// loader (a whole decompressed .zsk file, confirmed to be an ordinary
// resource of this same format, docs/ZONE_FORMAT.md) can reuse it
// without going through the archive's offset/size index. Returns false
// (state of `out` unspecified) if `blob`/`size` don't parse as a valid
// resource.
bool ParseModelResource(const uint8_t* blob, size_t size, Model& out);

}  // namespace sk
