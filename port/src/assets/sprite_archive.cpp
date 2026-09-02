#include "assets/sprite_archive.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "graphics/backbuffer.h"

namespace sk {

namespace {

constexpr int kSlotCount = 384;
constexpr size_t kTocBytes = static_cast<size_t>(kSlotCount) * 4;
constexpr uint16_t kColorKey = 0x0f0f;

uint16_t ReadU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t ReadU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// v is the real engine's 0x0RGB (4-bit-per-channel) convention -- same
// unpack world/model_archive.h's Model texels already use, just also
// repacked to this port's RGB565 Backbuffer format via PackRGB565.
uint16_t Rgb444ToRgb565(uint16_t v) {
    uint8_t r = static_cast<uint8_t>(((v >> 8) & 0xF) * 17);
    uint8_t g = static_cast<uint8_t>(((v >> 4) & 0xF) * 17);
    uint8_t b = static_cast<uint8_t>((v & 0xF) * 17);
    return PackRGB565(r, g, b);
}

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

}  // namespace

bool SpriteArchive::Load(const std::string& scriptRoot) {
    std::vector<uint8_t> file;
    if (!ReadWholeFile(scriptRoot + "/global.spr", file) || file.size() < kTocBytes) {
        std::printf("SpriteArchive: could not open %s/global.spr\n", scriptRoot.c_str());
        return false;
    }

    sizes_.resize(kSlotCount);
    offsets_.resize(kSlotCount);
    uint32_t acc = static_cast<uint32_t>(kTocBytes);
    for (int i = 0; i < kSlotCount; ++i) {
        sizes_[static_cast<size_t>(i)] = ReadU32(&file[static_cast<size_t>(i) * 4]);
        offsets_[static_cast<size_t>(i)] = acc;
        acc += sizes_[static_cast<size_t>(i)];
    }

    data_ = std::move(file);
    cache_.clear();
    cache_.resize(kSlotCount);
    std::printf("SpriteArchive: loaded global.spr, %d slots, %zu bytes\n", kSlotCount,
                data_.size());
    return true;
}

const Sprite* SpriteArchive::GetSprite(int slotIndex) {
    if (slotIndex < 0 || static_cast<size_t>(slotIndex) >= sizes_.size()) return nullptr;
    size_t idx = static_cast<size_t>(slotIndex);
    if (cache_[idx]) return cache_[idx].get();

    uint32_t size = sizes_[idx];
    uint32_t offset = offsets_[idx];
    if (size < 4 + 512 || static_cast<size_t>(offset) + size > data_.size()) {
        return nullptr;  // unused slot (size 0) or a bad/truncated entry
    }
    const uint8_t* blob = &data_[offset];

    uint16_t w = ReadU16(blob + 0);
    uint16_t h = ReadU16(blob + 2);
    if (w == 0 || h == 0) return nullptr;

    uint16_t palette[256];
    for (int c = 0; c < 256; ++c) palette[c] = ReadU16(blob + 4 + c * 2);

    auto sprite = std::make_unique<Sprite>();
    sprite->width = w;
    sprite->height = h;
    sprite->pixels.assign(static_cast<size_t>(w) * h, 0);
    sprite->opaque.assign(static_cast<size_t>(w) * h, false);

    size_t pos = 4 + 512;
    for (int row = 0; row < h; ++row) {
        if (pos + 4 > size) return nullptr;  // malformed -- shouldn't happen against real data
        uint16_t startX = ReadU16(blob + pos);
        uint16_t endX = ReadU16(blob + pos + 2);
        pos += 4;
        int n = static_cast<int>(endX) - static_cast<int>(startX);
        if (n < 0 || pos + static_cast<size_t>(n) > size) return nullptr;
        for (int i = 0; i < n; ++i) {
            uint8_t paletteIndex = blob[pos + static_cast<size_t>(i)];
            uint16_t color = palette[paletteIndex];
            int x = startX + i;
            if (x < 0 || x >= w) continue;
            size_t di = static_cast<size_t>(row) * w + static_cast<size_t>(x);
            if (color != kColorKey) {
                sprite->pixels[di] = Rgb444ToRgb565(color);
                sprite->opaque[di] = true;
            }
        }
        pos += static_cast<size_t>(n);
    }

    cache_[idx] = std::move(sprite);
    return cache_[idx].get();
}

bool SpriteArchive::LoadCategory(const std::string& scriptRoot, const std::string& category) {
    std::ifstream f(scriptRoot + "/" + category + "_sprites.txt");
    if (!f) {
        std::printf("SpriteArchive: could not open %s/%s_sprites.txt\n", scriptRoot.c_str(),
                    category.c_str());
        return false;
    }
    std::string line;
    int requested = 0, decoded = 0;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        int slot = std::atoi(line.c_str());
        ++requested;
        if (GetSprite(slot)) ++decoded;
    }
    std::printf("SpriteArchive: category '%s' -- %d/%d slot(s) decoded\n", category.c_str(),
                decoded, requested);
    return true;
}

}  // namespace sk
