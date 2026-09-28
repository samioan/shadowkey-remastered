#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "graphics/backbuffer.h"

namespace sk {

// M119: the Resolution setting -- the 3D view drawn at a whole multiple of
// the device's 176x208, with everything else left exactly as the game draws
// it.
//
// The difficulty is that "everything else" is a great deal of code: the
// viewmodel, the HUD, the automap, the prompt line, the coordinate readout,
// the hit overlay -- all of it draws straight into the one native
// Backbuffer, over the scene, at fixed device coordinates, and some of it
// *blends* with the scene underneath. Rewriting all of that to draw into a
// separate layer would touch most of main.cpp for a feature that should
// leave the original picture alone.
//
// So the 2D code is not touched at all. The frame is built like this:
//
//   1. BeginScene: the renderer draws into a (176*s) x (208*s) buffer.
//   2. EndScene: that buffer is point-sampled down into the ordinary native
//      Backbuffer, and a copy of the result is kept.
//   3. The game draws its 2D layer into the native Backbuffer as it always
//      has -- blends included, against the sampled scene.
//   4. Compose: every native pixel the 2D layer left exactly as the scene
//      put it is replaced by its s x s block of the high-resolution scene;
//      every pixel it changed is scaled up as a block of itself.
//
// A 2D pixel that happens to equal the sampled scene pixel under it is
// shown as the scene -- which is the same colour at that spot, so there is
// nothing to see. A full-screen blend (none exists in the shipped game, but
// the rule does not care) would simply show the 2D layer's native pixels
// for as long as it lasted.
//
// At scale 1 and the original shape none of this runs: main.cpp renders
// straight into the native Backbuffer and presents it, exactly as before
// M119. M120's wider views go through here at any scale, with a 2D layer
// as wide as the view (ViewLayout below).
class HiResScene {
public:
    // The buffer the renderer should draw into this frame: `viewWidth`
    // native pixels across (M120 -- Backbuffer::kWidth unless Aspect Ratio
    // widens the view; see ViewLayout below) and 208 down, times `scale`.
    Backbuffer& BeginScene(int scale, int viewWidth = Backbuffer::kWidth) {
        scale_ = scale < 1 ? 1 : scale;
        viewWidth_ = viewWidth < Backbuffer::kWidth ? Backbuffer::kWidth : viewWidth;
        scene_.Resize(viewWidth_ * scale_, Backbuffer::kHeight * scale_);
        return scene_;
    }

    // Samples the finished scene down into `native` -- the centre of each
    // s x s block, the same point ZoneRenderer::ObjectIdAt reads -- and
    // remembers the result for Compose. `native` is made viewWidth x 208:
    // for the original shape that is the ordinary 176x208 Backbuffer, and
    // for a wider view it is the 2D layer's own wide buffer.
    void EndScene(Backbuffer& native) {
        const int s = scale_;
        const int off = s / 2;
        native.Resize(viewWidth_, Backbuffer::kHeight);
        sampled_.Resize(viewWidth_, Backbuffer::kHeight);
        for (int y = 0; y < Backbuffer::kHeight; ++y) {
            const uint16_t* src = scene_.Row(y * s + off);
            uint16_t* dst = native.Row(y);
            uint16_t* keep = sampled_.Row(y);
            for (int x = 0; x < viewWidth_; ++x) {
                dst[x] = keep[x] = src[x * s + off];
            }
        }
    }

    // The frame to present: `native` (the scene plus the 2D layer) with the
    // untouched scene pixels restored to full resolution. At scale 1 -- a
    // wider view at the original Resolution -- that is `native` itself.
    const Backbuffer& Compose(const Backbuffer& native) {
        const int s = scale_;
        if (s == 1) return native;
        out_.Resize(scene_.width(), scene_.height());
        for (int y = 0; y < Backbuffer::kHeight; ++y) {
            const uint16_t* ui = native.Row(y);
            const uint16_t* keep = sampled_.Row(y);
            for (int sub = 0; sub < s; ++sub) {
                const int hy = y * s + sub;
                const uint16_t* hi = scene_.Row(hy);
                uint16_t* dst = out_.Row(hy);
                for (int x = 0; x < viewWidth_; ++x) {
                    const int hx = x * s;
                    if (ui[x] == keep[x]) {
                        for (int k = 0; k < s; ++k) dst[hx + k] = hi[hx + k];
                    } else {
                        for (int k = 0; k < s; ++k) dst[hx + k] = ui[x];
                    }
                }
            }
        }
        return out_;
    }

    int scale() const { return scale_; }
    int viewWidth() const { return viewWidth_; }
    const Backbuffer& scene() const { return scene_; }

private:
    int scale_ = 1;
    int viewWidth_ = Backbuffer::kWidth;
    Backbuffer scene_;
    Backbuffer sampled_;  // viewWidth x 208
    Backbuffer out_;
};

// M120: where the 2D layer goes in a view wider than the device's.
//
// The 3D view widens (the renderer keeps its focal length and centres the
// wider target, so the world is not stretched: the player sees further to
// each side). The 2D art cannot widen -- it is 176-pixel-wide pixel art,
// and stretching it is exactly what this setting must not do -- so each
// piece is placed at native size where it belongs on the wider screen:
//
//  - centred, in the original 176-pixel column: the compass banner (one
//    piece of art across the whole top, hand icons and all), the hit
//    slash, the automap, the prompt and coordinate text, a spell's hands
//    (the casting frames are one composition around the view's centre);
//  - on the left edge: the dragon-head vitals frame, which the device
//    drew in its bottom-left corner;
//  - on the right edge: a weapon. Every weapon frame is a 176x208 canvas
//    whose arm enters from its right edge, so centred it would end in a
//    hard vertical cut in the middle of the screen's right half.
//
// Menus and every other full-screen 2D screen are drawn as they always were
// and presented at their own 176x208 shape, pillarboxed.
struct ViewLayout {
    int width = Backbuffer::kWidth;  // the view, in native pixels

    bool wide() const { return width > Backbuffer::kWidth; }
    int centreX() const { return (width - Backbuffer::kWidth) / 2; }
    int leftX() const { return 0; }
    int rightX() const { return width - Backbuffer::kWidth; }
};

// M120: the original 176x208 column of a wide 2D layer, out and back --
// for the 2D code that only ever draws into a native Backbuffer (text,
// the automap), which draws there and is copied into place.
inline void CopyColumnOut(const Backbuffer& wide, int x0, Backbuffer& native) {
    for (int y = 0; y < Backbuffer::kHeight; ++y) {
        std::copy_n(wide.Row(y) + x0, Backbuffer::kWidth, native.Row(y));
    }
}
inline void CopyColumnIn(const Backbuffer& native, Backbuffer& wide, int x0) {
    for (int y = 0; y < Backbuffer::kHeight; ++y) {
        std::copy_n(native.Row(y), Backbuffer::kWidth, wide.Row(y) + x0);
    }
}

}  // namespace sk
