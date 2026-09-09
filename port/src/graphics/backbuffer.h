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
    // sprite's own bounds. `Blit_RLESprite`'s straight-copy mode
    // (blendMode == 0).
    void Blit(int x, int y, const Sprite& sprite) { BlitRegion(x, y, sprite, 0, sprite.width); }

    // M86: `Blit_RLESprite`'s **blendMode == 1**, the 50% average blend
    // (docs/GRAPHICS_FORMAT.md), which had no caller in this port until
    // the player's hit overlay needed one. The engine averages in its own
    // RGB444 -- `((src & 0xeee) + (dst & 0xeee)) >> 1`, masking each
    // channel's low bit off so the sum cannot carry between channels --
    // and this is the same operation in the backbuffer's RGB565: mask
    // 0xf7de, the low bit of all three channels at once.
    void BlitBlend(int x, int y, const Sprite& sprite) {
        BlitRegion(x, y, sprite, 0, sprite.width, /*blend=*/true);
    }

    // Like Blit(), but only copies `srcW` source columns starting at
    // source column `srcX` -- the real HUD's own srcXOffset/clipRight
    // Blit_RLESprite parameters (docs/GRAPHICS_FORMAT.md's HUD section),
    // used for the percentage-scaled vitals bar fill (srcX=0, srcW =
    // fraction*sprite.width) and the heading-scrolled compass tape
    // (srcX = heading-derived offset into a wider-than-screen strip).
    // M86: `FUN_1006a894` -- Blit_RLESprite with the hit-flash remap
    // spliced into its inner loop. Every opaque texel is replaced by
    // `ramp565[red nibble]` instead of itself, which is the same
    // substitution Poly3D_RasterizeTextured_v8 does for a flashing model.
    // `ramp565` is 16 entries already in this buffer's own format (see
    // render3d/zone_renderer.h's FlashRamp565), so nothing here needs to
    // know about RGB444. The red nibble survives the RGB444->RGB565
    // conversion exactly: a 4-bit r becomes the 5-bit `r<<1 | r>>3`, and
    // shifting that back down by one recovers r for every value.
    void BlitRamped(int x, int y, const Sprite& sprite, const uint16_t* ramp565) {
        for (int sy = 0; sy < sprite.height; ++sy) {
            const int dy = y + sy;
            if (dy < 0 || dy >= kHeight) continue;
            for (int sx = 0; sx < sprite.width; ++sx) {
                const int dx = x + sx;
                if (dx < 0 || dx >= kWidth) continue;
                const size_t si = static_cast<size_t>(sy) * sprite.width + static_cast<size_t>(sx);
                if (!sprite.opaque[si]) continue;
                pixels_[static_cast<size_t>(dy) * kWidth + static_cast<size_t>(dx)] =
                    ramp565[(sprite.pixels[si] >> 12) & 0xF];
            }
        }
    }

    void BlitRegion(int x, int y, const Sprite& sprite, int srcX, int srcW, bool blend = false) {
        for (int sy = 0; sy < sprite.height; ++sy) {
            int dy = y + sy;
            if (dy < 0 || dy >= kHeight) continue;
            for (int i = 0; i < srcW; ++i) {
                int sx = srcX + i;
                if (sx < 0 || sx >= sprite.width) continue;
                int dx = x + i;
                if (dx < 0 || dx >= kWidth) continue;
                size_t si = static_cast<size_t>(sy) * sprite.width + static_cast<size_t>(sx);
                if (!sprite.opaque[si]) continue;
                uint16_t& dst = pixels_[static_cast<size_t>(dy) * kWidth +
                                        static_cast<size_t>(dx)];
                if (blend) {
                    dst = static_cast<uint16_t>(
                        (((sprite.pixels[si] & 0xf7de) + (dst & 0xf7de)) >> 1));
                } else {
                    dst = sprite.pixels[si];
                }
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
