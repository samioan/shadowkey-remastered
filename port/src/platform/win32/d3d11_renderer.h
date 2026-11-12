#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "graphics/backbuffer.h"
#include "render3d/gpu_frame.h"

namespace sk {

// M129: the Direct3D 11 renderer -- the 3D view drawn by the graphics card.
//
// The software rasterizer stays what it always was: the Original path, the
// one the tracked .ppm dumps pin, and the one the melee probes read their
// object IDs from. This draws the *same triangles* (render3d/gpu_frame.h is
// the software renderer's own record of them) with the per-pixel work in a
// pixel shader, so the frame costs the same at 176x208 and at 4K.
//
// What the shaders do is the engine's arithmetic, not an approximation of
// it: the `.ztx` texel is an index, `.zlu` turns it into RGB444 on the rung
// the interpolated light selects, 0xf0f is the cut-out key, and `.zfg` fogs
// the finished colour by the top nibble of the interpolated fog scalar. All
// three files go onto the card as integer lookup textures, unfiltered.
//
// `Filter::Smooth` is the one place it goes past the device: texels are
// blended bilinearly (after the palette, so the blend is between colours and
// never between palette indices), and the light rungs and fog levels are
// interpolated instead of stepped. The far-surface detail mask is dropped
// with it. Original leaves every one of those exactly as the engine has it.
//
// One object owns the device, the scene target and (optionally) a window's
// swap chain. Without a window it still draws and reads back, which is how
// the test compares it against the software frame on WARP, Windows' own
// software Direct3D device.
class D3D11Renderer {
public:
    enum class Filter { Original, Smooth };

    // Called with a GDI device context for the finished back buffer, just
    // before it is presented -- the debug overlay draws through it.
    using GdiOverlay = std::function<void(void* hdc, int width, int height)>;

    D3D11Renderer();
    ~D3D11Renderer();
    D3D11Renderer(const D3D11Renderer&) = delete;
    D3D11Renderer& operator=(const D3D11Renderer&) = delete;

    // Creates the device and compiles the shaders. `warp` asks for the
    // software device outright; otherwise it is the fallback when no
    // hardware device can be made. False (with lastError()) when Direct3D 11
    // or its shader compiler is not available at all.
    bool Init(bool warp = false);
    bool ready() const;
    const std::string& lastError() const;
    // "NVIDIA GeForce ..." / "Microsoft Basic Render Driver".
    const std::string& adapterName() const;

    // Gives the renderer a window to present to (an HWND). Detach before
    // anything else draws to the window with GDI again.
    bool AttachWindow(void* hwnd);
    void DetachWindow();
    bool hasWindow() const;

    // Draws `frame` into the scene target, which is made frame.width x
    // frame.height. Nothing reaches a window until a Present call.
    bool DrawScene(const GpuFrame& frame, Filter filter);

    // The scene target's pixels as 0x00RRGGBB, row-major. For tests.
    bool ReadScene(std::vector<uint32_t>& pixels, int* width, int* height);

    // Presents a finished 2D frame (a menu, the loading screen, a software
    // 3D frame): letterboxed at its own shape, unfiltered, as the GDI path
    // does it.
    bool PresentBackbuffer(const Backbuffer& frame, const GdiOverlay& overlay = {});

    // Presents the scene target under the game's 2D layer. `layer` is the
    // native-height frame the 2D code drew into (176 or wider x 208) and
    // `under` is what was in it before the 2D code ran: a pixel the 2D code
    // left alone shows the GPU scene, a pixel it changed is drawn as itself.
    // The same rule as graphics/scene_upscale.h's Compose.
    bool PresentScene(const Backbuffer& layer, const Backbuffer& under,
                      const GdiOverlay& overlay = {});

    // Drops every cached texture (zone, model skins, sprites). Called by
    // itself when the zone changes; public for a device-lost style reset.
    void FlushCaches();

    struct Stats {
        int drawCalls = 0;
        int triangles = 0;
        int texturesCreated = 0;  // since Init
    };
    Stats lastStats() const;

    struct Impl;

private:
    Impl* impl_;
};

}  // namespace sk
