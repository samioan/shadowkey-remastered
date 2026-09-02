#pragma once

// Loads the real menu/HUD/item icon archive (system/apps/6r51/
// global.spr, docs/GRAPHICS_FORMAT.md's "The 384-slot image cache's
// real source format" section) and decodes individual sprites on
// demand. Mirrors world/model_archive.h's shape exactly: Load() reads
// the whole (small, ~1.6MB) archive once, individual slots are lazily
// RLE-decoded and cached on first GetSprite() (most contexts reference
// only a fraction of the 384 real slots -- 253 are populated at all).
//
// global.spr's own format: a 384-entry uint32 (LE) size table (1536
// bytes), then the sprite blobs themselves concatenated back-to-back in
// slot order with no padding -- slot i's byte offset is
// 1536 + sum(sizeTable[0..i-1]). Verified against the real file: the
// prefix-summed sizes plus the 1536-byte header land on the real file's
// exact byte size. Each blob is the same in-memory RLE sprite layout
// docs/GRAPHICS_FORMAT.md's "Sprite/image format" section already
// decoded from Blit_RLESprite: ushort width, ushort height, a 256-entry
// 16-bit-per-entry palette, then per-row (startX,endX) + palette-index-
// byte runs. Decoded here into a flat RGB565 pixel buffer (this port's
// Backbuffer format) with a parallel opacity mask, rather than kept as
// raw RLE + reproducing Blit_RLESprite's exact scanline walk -- the
// Phase 2 "behavioral RE, not byte-exact" choice (docs/ROADMAP.md),
// same spirit as every other renderer in this port.
//
// Which of the 384 slots a given context needs comes from a real
// <category>_sprites.txt manifest (menu_sprites.txt, azra_sprites.txt,
// ...) -- a plain newline-separated list of decimal slot indices, same
// convention as the already-known <zone>_models.txt/_sounds.txt.
// LoadCategory() reads one of these and prewarms exactly the slots it
// lists (matching the real engine's own per-category loading), though
// GetSprite() works standalone too since decode is lazy either way.
//
// NOT implemented: Blit_RLESprite's blendMode==1 50%-average-blend path
// -- no current UI use for it (see graphics/backbuffer.h's Blit()).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sk {

struct Sprite {
    int width = 0, height = 0;
    std::vector<uint16_t> pixels;  // RGB565, width*height, row-major
    std::vector<bool> opaque;      // parallel to pixels -- false where the
                                    // real 0x0f0f colorkey applies
};

class SpriteArchive {
public:
    // scriptRoot e.g. ".../system/apps/6r51" -- reads global.spr from it.
    bool Load(const std::string& scriptRoot);

    // Returns nullptr for an out-of-range index, an empty (size-0, i.e.
    // unused) slot, or a blob too small to plausibly hold the header.
    const Sprite* GetSprite(int slotIndex);

    // Reads <scriptRoot>/<category>_sprites.txt and calls GetSprite() on
    // every listed index, so a whole category's worth of slots decode
    // up front. Not fatal if the manifest is missing -- returns false,
    // but GetSprite() still works for any index a caller already knows.
    bool LoadCategory(const std::string& scriptRoot, const std::string& category);

    int slotCount() const { return static_cast<int>(sizes_.size()); }

private:
    std::vector<uint32_t> sizes_;   // 384 entries, byte length per slot (0 = unused)
    std::vector<uint32_t> offsets_; // parallel to sizes_, byte offset into data_
    std::vector<uint8_t> data_;     // the whole file, sizes_/offsets_ index into it
    std::vector<std::unique_ptr<Sprite>> cache_;  // parallel to sizes_
};

}  // namespace sk
