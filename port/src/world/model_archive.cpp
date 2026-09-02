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
    const uint8_t* blob = &huge_[e.offset];

    int16_t h0 = ReadI16(blob + 0x00);
    int16_t h1 = ReadI16(blob + 0x02);  // frame count, unused beyond frame 0 here
    int16_t h2 = ReadI16(blob + 0x04);
    int16_t h3 = ReadI16(blob + 0x06);
    int16_t h4 = ReadI16(blob + 0x08);
    int16_t h5 = ReadI16(blob + 0x0a);
    (void)h1;
    (void)h5;

    if (h2 < 0 || h3 < 0 || h4 < 0) return nullptr;

    // uv_base = h0 + h1*h5 (halfwords); face_base = uv_base + h3*2;
    // tex_hdr_base = face_base + h4*6 -- see docs/MODEL_FORMAT.md and
    // tools/parse_model_resource.py (the reference this mirrors exactly).
    int uvBase = h0 + h1 * h5;
    int faceBase = uvBase + h3 * 2;
    int texHdrBase = faceBase + h4 * 6;
    size_t texHdrByte = static_cast<size_t>(texHdrBase) * 2;
    if (texHdrByte + 8 > e.size) return nullptr;

    uint16_t skinCount = ReadU16(blob + texHdrByte);
    uint16_t width = ReadU16(blob + texHdrByte + 2);
    uint16_t height = ReadU16(blob + texHdrByte + 4);
    size_t pixStart = texHdrByte + 8;
    size_t pixTotal = static_cast<size_t>(skinCount) * width * height * 2;
    if (pixStart + pixTotal > e.size) return nullptr;

    auto model = std::make_unique<Model>();
    model->skinCount = skinCount;
    model->width = width;
    model->height = height;

    model->vertices.resize(static_cast<size_t>(h2));
    for (int v = 0; v < h2; ++v) {
        size_t off = static_cast<size_t>(h0) * 2 + static_cast<size_t>(v) * 6;  // frame 0
        if (off + 6 > e.size) return nullptr;
        model->vertices[static_cast<size_t>(v)] = {ReadI16(blob + off), ReadI16(blob + off + 2),
                                                     ReadI16(blob + off + 4)};
    }

    model->uvs.resize(static_cast<size_t>(h3));
    for (int u = 0; u < h3; ++u) {
        size_t off = static_cast<size_t>(uvBase) * 2 + static_cast<size_t>(u) * 4;
        if (off + 4 > e.size) return nullptr;
        model->uvs[static_cast<size_t>(u)] = {ReadU16(blob + off), ReadU16(blob + off + 2)};
    }

    model->faces.resize(static_cast<size_t>(h4));
    for (int f = 0; f < h4; ++f) {
        size_t off = static_cast<size_t>(faceBase) * 2 + static_cast<size_t>(f) * 12;
        if (off + 12 > e.size) return nullptr;
        ModelFace& face = model->faces[static_cast<size_t>(f)];
        face.vA = ReadI16(blob + off);
        face.vB = ReadI16(blob + off + 2);
        face.vC = ReadI16(blob + off + 4);
        face.uA = ReadI16(blob + off + 6);
        face.uB = ReadI16(blob + off + 8);
        face.uC = ReadI16(blob + off + 10);
    }

    model->pixels.resize(pixTotal / 2);
    for (size_t i = 0; i < model->pixels.size(); ++i) {
        model->pixels[i] = ReadU16(blob + pixStart + i * 2);
    }

    cache_[static_cast<size_t>(archiveIndex)] = std::move(model);
    return cache_[static_cast<size_t>(archiveIndex)].get();
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
