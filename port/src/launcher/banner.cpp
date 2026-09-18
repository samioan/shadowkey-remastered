#include "launcher/banner.h"

#include <windows.h>

#include <wincodec.h>

#include "launcher/resource.h"

namespace sk {
namespace launcher {

namespace {

// A minimal COM pointer. The alternative here is either <wrl.h>/<atlbase.h>
// (a much larger include for one job) or a `goto cleanup` ladder, and this
// function has five separate failure points.
template <typename T>
struct ComPtr {
    T* raw = nullptr;
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ~ComPtr() {
        if (raw) raw->Release();
    }
    T** operator&() { return &raw; }
    T* operator->() const { return raw; }
    explicit operator bool() const { return raw != nullptr; }
};

}  // namespace

bool DecodeBanner(const void* data, size_t size, Banner& out) {
    out = Banner{};
    if (!data || size == 0) return false;

    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory)))) {
        return false;
    }

    // The resource bytes are read-only and outlive this call, so the stream
    // can point straight at them -- no copy of the encoded image.
    ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(&stream))) return false;
    if (FAILED(stream->InitializeFromMemory(
            static_cast<BYTE*>(const_cast<void*>(data)), static_cast<DWORD>(size)))) {
        return false;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromStream(stream.raw, nullptr,
                                                WICDecodeMetadataCacheOnDemand, &decoder))) {
        return false;
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return false;

    // 32bppBGRA regardless of what the file actually is, because that is
    // the one layout StretchDIBits takes without further work. The
    // converter also handles a source that is greyscale, paletted or CMYK,
    // none of which this artwork is but any of which a replacement could be.
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter))) return false;
    if (FAILED(converter->Initialize(frame.raw, GUID_WICPixelFormat32bppBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom))) {
        return false;
    }

    UINT width = 0, height = 0;
    if (FAILED(converter->GetSize(&width, &height))) return false;
    if (width == 0 || height == 0 || width > static_cast<UINT>(kMaxBannerDimension) ||
        height > static_cast<UINT>(kMaxBannerDimension)) {
        return false;
    }

    std::vector<uint32_t> pixels;
    try {
        pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
    } catch (const std::bad_alloc&) {
        return false;
    }

    const UINT stride = width * 4;
    const UINT bufferSize = stride * height;
    if (FAILED(converter->CopyPixels(nullptr, stride, bufferSize,
                                     reinterpret_cast<BYTE*>(pixels.data())))) {
        return false;
    }

    out.width = static_cast<int>(width);
    out.height = static_cast<int>(height);
    out.pixels = std::move(pixels);
    return true;
}

Banner LoadEmbeddedBanner() {
    Banner banner;
    // MAKEINTRESOURCEW(10) rather than RT_RCDATA: that macro follows the
    // UNICODE define, and this file is also compiled into the smoke test,
    // which does not set it -- so the named constant would be an LPSTR
    // there and an LPWSTR here.
    HRSRC found = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_BANNER), MAKEINTRESOURCEW(10));
    if (!found) return banner;
    const DWORD size = SizeofResource(nullptr, found);
    HGLOBAL loaded = LoadResource(nullptr, found);
    if (!loaded || size == 0) return banner;
    const void* blob = LockResource(loaded);
    if (!blob) return banner;
    // No FreeResource/UnlockResource: since Win32 those are no-ops, and the
    // blob stays mapped for the life of the module either way -- which is
    // what lets DecodeBanner read it without copying it first.
    DecodeBanner(blob, size, banner);
    return banner;
}

}  // namespace launcher
}  // namespace sk
