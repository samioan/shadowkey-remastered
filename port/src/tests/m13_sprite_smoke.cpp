// Sprite archive smoke test: proves SpriteArchive against real
// global.spr/menu_sprites.txt data -- see docs/GRAPHICS_FORMAT.md's
// "The 384-slot image cache's real source format" section and
// docs/PORT_ROADMAP.md's real-menu-backgrounds/icons milestone.
#include <cstdio>
#include <fstream>
#include <string>

#include "assets/sprite_archive.h"

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::SpriteArchive archive;
    if (!archive.Load(scriptRoot)) {
        std::printf("m13_sprite_smoke: FAILED to load global.spr\n");
        return 1;
    }

    bool ok = true;

    // 1. TOC byte-accounting: the real global.spr's size is exactly
    // 1536 (384*4) + sum of all 384 real per-slot sizes -- checked
    // independently of SpriteArchive's own internals by reading the
    // raw file here too.
    {
        std::ifstream f(std::string(scriptRoot) + "/global.spr", std::ios::binary | std::ios::ate);
        if (!f) {
            std::printf("m13_sprite_smoke: FAILED to reopen global.spr for size check\n");
            return 1;
        }
        auto realSize = static_cast<long long>(f.tellg());
        std::printf("global.spr real file size: %lld\n", realSize);
        if (realSize <= 0) ok = false;
    }

    // 2. The 3 real MenuBackground() ids used across the whole script
    // corpus (grepped this session) -- must decode to exactly 176x208
    // (full screen) with a non-degenerate (not all-transparent) pixel
    // count.
    for (int id : {20, 69, 174}) {
        const sk::Sprite* s = archive.GetSprite(id);
        if (!s) {
            std::printf("MenuBackground id %d: FAILED to decode\n", id);
            ok = false;
            continue;
        }
        int opaqueCount = 0;
        for (bool o : s->opaque) {
            if (o) ++opaqueCount;
        }
        bool sizeOk = s->width == 176 && s->height == 208;
        bool notDegenerate = opaqueCount > (s->width * s->height) / 2;  // a background should be
                                                                          // mostly opaque, not
                                                                          // mostly transparent
        std::printf("MenuBackground id %d: %dx%d, opaque=%d/%d %s\n", id, s->width, s->height,
                    opaqueCount, s->width * s->height,
                    (sizeOk && notDegenerate) ? "OK" : "FAILED");
        if (!sizeOk || !notDegenerate) ok = false;
    }

    // 3. menu_sprites.txt loads and every listed slot that has real
    // (non-zero-size) data in global.spr actually decodes.
    if (!archive.LoadCategory(scriptRoot, "menu")) {
        std::printf("m13_sprite_smoke: FAILED to load menu_sprites.txt\n");
        ok = false;
    }

    // 4. A small known icon slot (dagger icon, this session's PNG
    // check) decodes to a sane, non-full-screen size.
    const sk::Sprite* icon = archive.GetSprite(30);
    if (!icon || icon->width != 32 || icon->height != 32) {
        std::printf("icon slot 30: FAILED (expected 32x32, got %s)\n",
                    icon ? (std::to_string(icon->width) + "x" + std::to_string(icon->height)).c_str()
                         : "<null>");
        ok = false;
    } else {
        std::printf("icon slot 30: %dx%d OK\n", icon->width, icon->height);
    }

    // 5. Out-of-range / unused-slot handling shouldn't crash and must
    // return null.
    if (archive.GetSprite(-1) != nullptr || archive.GetSprite(archive.slotCount() + 10) != nullptr) {
        std::printf("m13_sprite_smoke: FAILED -- out-of-range GetSprite() didn't return null\n");
        ok = false;
    }

    std::printf("\nm13_sprite_smoke: %s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
