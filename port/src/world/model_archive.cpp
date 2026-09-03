#include "world/model_archive.h"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace sk {

namespace {

uint16_t ReadU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
int16_t ReadI16(const uint8_t* p) { return static_cast<int16_t>(ReadU16(p)); }
uint32_t ReadU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

}  // namespace

bool ModelArchive::Load(const std::string& scriptRoot) {
    std::vector<uint8_t> idx;
    if (!ReadWholeFile(scriptRoot + "/models.idx", idx) || idx.size() < 4) {
        std::printf("ModelArchive: could not open %s/models.idx\n", scriptRoot.c_str());
        return false;
    }
    uint32_t count = ReadU32(&idx[0]);
    if (idx.size() < 4 + static_cast<size_t>(count) * 8) {
        std::printf("ModelArchive: models.idx too short for %u entries\n", count);
        return false;
    }
    index_.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* p = &idx[4 + static_cast<size_t>(i) * 8];
        index_[i].offset = ReadU32(p);
        index_[i].size = ReadU32(p + 4);
    }

    if (!ReadWholeFile(scriptRoot + "/models.huge", huge_)) {
        std::printf("ModelArchive: could not open %s/models.huge\n", scriptRoot.c_str());
        return false;
    }

    cache_.clear();
    cache_.resize(count);
    std::printf("ModelArchive: loaded %u entries, models.huge is %zu bytes\n", count, huge_.size());
    return true;
}

const Model* ModelArchive::GetModel(int archiveIndex) {
    if (archiveIndex < 0 || static_cast<size_t>(archiveIndex) >= index_.size()) return nullptr;
    if (cache_[static_cast<size_t>(archiveIndex)]) return cache_[static_cast<size_t>(archiveIndex)].get();

    const IndexEntry& e = index_[static_cast<size_t>(archiveIndex)];
    if (e.size < 14 || static_cast<size_t>(e.offset) + e.size > huge_.size()) {
        return nullptr;  // "NULL.bin" placeholder or a bad index -- no model
    }

    auto model = std::make_unique<Model>();
    if (!ParseModelResource(&huge_[e.offset], e.size, *model)) return nullptr;

    cache_[static_cast<size_t>(archiveIndex)] = std::move(model);
    return cache_[static_cast<size_t>(archiveIndex)].get();
}

bool ParseModelResource(const uint8_t* blob, size_t size, Model& out) {
    if (size < 14) return false;

    int16_t h0 = ReadI16(blob + 0x00);
    int16_t h1 = ReadI16(blob + 0x02);  // animation frame count
    int16_t h2 = ReadI16(blob + 0x04);  // vertices per frame
    int16_t h3 = ReadI16(blob + 0x06);
    int16_t h4 = ReadI16(blob + 0x08);
    int16_t h5 = ReadI16(blob + 0x0a);  // halfwords per frame's vertex block

    if (h2 < 0 || h3 < 0 || h4 < 0 || h1 < 1) return false;

    // uv_base = h0 + h1*h5 (halfwords); face_base = uv_base + h3*2;
    // tex_hdr_base = face_base + h4*6 -- see docs/MODEL_FORMAT.md and
    // tools/parse_model_resource.py (the reference this mirrors exactly).
    int uvBase = h0 + h1 * h5;
    int faceBase = uvBase + h3 * 2;
    int texHdrBase = faceBase + h4 * 6;
    size_t texHdrByte = static_cast<size_t>(texHdrBase) * 2;
    if (texHdrByte + 8 > size) return false;

    uint16_t skinCount = ReadU16(blob + texHdrByte);
    uint16_t width = ReadU16(blob + texHdrByte + 2);
    uint16_t height = ReadU16(blob + texHdrByte + 4);
    size_t pixStart = texHdrByte + 8;
    size_t pixTotal = static_cast<size_t>(skinCount) * width * height * 2;
    if (pixStart + pixTotal > size) return false;

    out.skinCount = skinCount;
    out.width = width;
    out.height = height;

    out.frameCount = h1;
    out.vertsPerFrame = h2;
    // M28: every frame, not just frame 0. The real per-frame byte offset
    // (docs/MODEL_FORMAT.md, and both real consumer functions compute it
    // identically) is `(frameIndex * H5 + H0) * 2`.
    out.vertices.resize(static_cast<size_t>(h1) * static_cast<size_t>(h2));
    for (int f = 0; f < h1; ++f) {
        size_t frameBase = (static_cast<size_t>(f) * static_cast<size_t>(h5) +
                            static_cast<size_t>(h0)) * 2;
        for (int v = 0; v < h2; ++v) {
            size_t off = frameBase + static_cast<size_t>(v) * 6;
            if (off + 6 > size) return false;
            out.vertices[static_cast<size_t>(f) * static_cast<size_t>(h2) +
                         static_cast<size_t>(v)] = {ReadI16(blob + off), ReadI16(blob + off + 2),
                                                     ReadI16(blob + off + 4)};
        }
    }

    out.uvs.resize(static_cast<size_t>(h3));
    for (int u = 0; u < h3; ++u) {
        size_t off = static_cast<size_t>(uvBase) * 2 + static_cast<size_t>(u) * 4;
        if (off + 4 > size) return false;
        out.uvs[static_cast<size_t>(u)] = {ReadU16(blob + off), ReadU16(blob + off + 2)};
    }

    out.faces.resize(static_cast<size_t>(h4));
    for (int f = 0; f < h4; ++f) {
        size_t off = static_cast<size_t>(faceBase) * 2 + static_cast<size_t>(f) * 12;
        if (off + 12 > size) return false;
        ModelFace& face = out.faces[static_cast<size_t>(f)];
        face.vA = ReadI16(blob + off);
        face.vB = ReadI16(blob + off + 2);
        face.vC = ReadI16(blob + off + 4);
        face.uA = ReadI16(blob + off + 6);
        face.uB = ReadI16(blob + off + 8);
        face.uC = ReadI16(blob + off + 10);
    }

    out.pixels.resize(pixTotal / 2);
    for (size_t i = 0; i < out.pixels.size(); ++i) {
        out.pixels[i] = ReadU16(blob + pixStart + i * 2);
    }

    // M28: the trailer -- a whole number of 6-byte animation clip records,
    // `(startFrame, endFrame, rate)`. See AnimationClip's comment in
    // world/model_archive.h for how this was pinned down. Only accepted
    // when the records actually partition [0, frameCount): anything else
    // means this interpretation doesn't hold for that entry, and it's
    // safer to fall back to "one implicit whole-model clip" than to seek
    // to a bogus frame.
    out.clips.clear();
    size_t trailerStart = pixStart + pixTotal;
    size_t trailerBytes = size - trailerStart;
    if (trailerBytes >= 6 && trailerBytes % 6 == 0) {
        size_t recordCount = trailerBytes / 6;
        std::vector<AnimationClip> parsed;
        parsed.reserve(recordCount);
        bool contiguous = true;
        int expectedStart = 0;
        for (size_t r = 0; r < recordCount; ++r) {
            const uint8_t* p = blob + trailerStart + r * 6;
            AnimationClip c;
            c.startFrame = ReadU16(p);
            c.endFrame = ReadU16(p + 2);
            c.rate = ReadU16(p + 4);
            if (c.startFrame != expectedStart || c.endFrame <= c.startFrame ||
                c.endFrame > out.frameCount) {
                contiguous = false;
                break;
            }
            expectedStart = c.endFrame;
            parsed.push_back(c);
        }
        if (contiguous && expectedStart == out.frameCount) {
            out.clips = std::move(parsed);
        }
    }
    if (out.clips.empty()) {
        // Static props (the 92.5% of entries with one frame and a 6-byte
        // trailer that doesn't parse as a clip) get one implicit clip so
        // callers never have to special-case them.
        out.clips.push_back({0, out.frameCount, 10});
    }

    return true;
}

const ModelVertex& Model::VertexAt(int frameIndex, int vertexIndex) const {
    static const ModelVertex kZero{};
    if (vertsPerFrame <= 0 || vertexIndex < 0 || vertexIndex >= vertsPerFrame) return kZero;
    int frame = frameIndex;
    if (frame < 0) frame = 0;
    if (frame >= frameCount) frame = frameCount > 0 ? frameCount - 1 : 0;
    size_t offset = static_cast<size_t>(frame) * static_cast<size_t>(vertsPerFrame) +
                    static_cast<size_t>(vertexIndex);
    if (offset >= vertices.size()) return kZero;
    return vertices[offset];
}

uint16_t Model::TexelAt(int skinIndex, int x, int y) const {
    if (skinIndex < 0 || skinIndex >= skinCount || x < 0 || y < 0 || x >= width || y >= height) {
        return 0x0f0f;  // chroma-key -- out-of-range samples draw as transparent
    }
    size_t offset = static_cast<size_t>(skinIndex) * width * height +
                    static_cast<size_t>(y) * width + static_cast<size_t>(x);
    if (offset >= pixels.size()) return 0x0f0f;
    return pixels[offset];
}

}  // namespace sk
