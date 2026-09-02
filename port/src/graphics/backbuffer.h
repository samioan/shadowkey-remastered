#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

#include "assets/sprite_archive.h"

namespace sk {

// Mirrors the original engine's screen buffer: 176x208, 16 bits per pixel,
// 352-byte (0x160) row stride -- see docs/GRAPHICS_FORMAT.md.
//
// NOTE: docs/GRAPHICS_FORMAT.md describes the color format as "4-bit-per-
// channel (RGB444-ish)" from observed palette constants (0xfff, 0xf00, the
// 0x0f0f colorkey) but the exact bit layout within the 16 bits was never
// pinned down byte-for-byte. Stored/presented as RGB565 for now (the
// natural 16bpp GDI format) -- close enough for the M0/M1 scaffold, revisit
// when real paletted assets (Blit_RLESprite, M2+) need exact color fidelity.
class Backbuffer {
public:
    static constexpr int kWidth = 176;
    static constexpr int kHeight = 208;

    Backbuffer() : pixels_(static_cast<size_t>(kWidth) * kHeight, 0) {}

    void Fill(uint16_t rgb565) {
        std::fill(pixels_.begin(), pixels_.end(), rgb565);
    }

    uint16_t* Row(int y) { return pixels_.data() + static_cast<size_t>(y) * kWidth; }
    const uint16_t* Row(int y) const { return pixels_.data() + static_cast<size_t>(y) * kWidth; }
    const uint16_t* Data() const { return pixels_.data(); }

    void SetPixel(int x, int y, uint16_t rgb565) {
        if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) return;
        pixels_[static_cast<size_t>(y) * kWidth + x] = rgb565;
    }

    // Straight opaque-pixel copy of a decoded assets/sprite_archive.h
    // Sprite at (x,y) -- clips against both the backbuffer and the
    // sprite's own bounds. Only the real engine's Blit_RLESprite
    // straight-copy mode (blendMode==0); its 50%-average-blend mode
    // (blendMode==1, docs/GRAPHICS_FORMAT.md) has no current caller in
    // this port and isn't implemented here.
    void Blit(int x, int y, const Sprite& sprite) {
        for (int sy = 0; sy < sprite.height; ++sy) {
            int dy = y + sy;
            if (dy < 0 || dy >= kHeight) continue;
            for (int sx = 0; sx < sprite.width; ++sx) {
                int dx = x + sx;
                if (dx < 0 || dx >= kWidth) continue;
                size_t si = static_cast<size_t>(sy) * sprite.width + static_cast<size_t>(sx);
                if (!sprite.opaque[si]) continue;
                pixels_[static_cast<size_t>(dy) * kWidth + static_cast<size_t>(dx)] =
                    sprite.pixels[si];
            }
        }
    }

private:
    std::vector<uint16_t> pixels_;
};

// RGB565 packer from 8-bit-per-channel input, for placeholder/debug drawing.
constexpr uint16_t PackRGB565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

}  // namespace sk
