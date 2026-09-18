#pragma once

// M113/M114: the launcher's background artwork.
//
// The image is committed as a JPEG (assets/banner.jpg, downscaled from the
// full-resolution master by tools/make_banner.py), embedded as an RCDATA
// resource, and decoded at startup through **WIC** -- the imaging component
// that ships with Windows. Nothing is vendored and nothing is
// redistributed; `windowscodecs.dll` is as much a part of the OS as
// `gdi32.dll`, which this launcher already links.
//
// M113 did this differently and it is worth recording why it changed. The
// artwork then was flat, vector-style colour, so the image could be stored
// as raw pixels under a plain DEFLATE stream and expanded with the `puff`
// inflater already vendored for the zone-file reader -- an image with no
// image decoder, 3.2 MB of pixels in 155 KB. M114's artwork is a rendered
// piece, all gradients and soft light, and DEFLATE has nothing to work
// with: measured at 1200x896 it came to 2.44 MB against 0.19 MB for the
// same image as a quality-88 JPEG. Thirteen times the size of every
// download and every auto-update is too much to pay for the tidiness of
// having no decoder, so the decoding moved to the one already installed on
// every machine this runs on.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sk {
namespace launcher {

// Decoded pixels in the layout `StretchDIBits` wants for a top-down 32bpp
// DIB: one `uint32_t` per pixel, bytes in B,G,R,X order, rows top to
// bottom, no row padding.
struct Banner {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels;

    bool valid() const {
        return width > 0 && height > 0 &&
               pixels.size() == static_cast<size_t>(width) * static_cast<size_t>(height);
    }
};

// A sanity bound on the decoded dimensions, so a corrupt or hostile image
// header cannot ask for a multi-gigabyte allocation. The committed artwork
// is 1600x1195.
constexpr int kMaxBannerDimension = 8192;

// Decodes an encoded image (JPEG, PNG, BMP -- whatever WIC is willing to
// open) from memory. Returns false and leaves `out` empty on anything that
// goes wrong: a truncated buffer, an unrecognised format, implausible
// dimensions, a failed allocation. The launcher then draws a flat
// background rather than refusing to start, the same "optional asset"
// tolerance the game itself applies to every real asset it loads.
//
// Requires COM to have been initialised on the calling thread.
bool DecodeBanner(const void* data, size_t size, Banner& out);

// Same, reading the image from the executable's own RCDATA resource.
Banner LoadEmbeddedBanner();

}  // namespace launcher
}  // namespace sk
