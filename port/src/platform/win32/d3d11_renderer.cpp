#include "platform/win32/d3d11_renderer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <unordered_map>

#include <windows.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>

#include "assets/sprite_archive.h"
#include "graphics/viewport.h"
#include "render3d/zone_renderer.h"
#include "world/model_archive.h"
#include "world/zone.h"

using Microsoft::WRL::ComPtr;

namespace sk {

namespace {

// ---------------------------------------------------------------------
// The shaders. One vertex shader for the scene, one for a screen rectangle,
// and a pixel shader per kind of triangle (render3d/gpu_frame.h).
// ---------------------------------------------------------------------
const char kShaderSource[] = R"hlsl(
cbuffer Frame : register(b0) {
    float2 targetSize;   // the scene target, in pixels
    float  depthScale;   // z/w = depthScale * invW, kept inside (0, 1]
    uint   smooth;       // Filter::Smooth
    uint   modelW;       // the bound skin's size (Model / Sky)
    uint   modelH;
    int    flashRow;     // >= 0: HitFlashRamps row
    uint   skin;         // slice of the bound skin array
    float4 rect;         // Blit: x0, y0, x1, y1 in clip space
    float  blendAlpha;   // Billboard: 1, or the blend weight
    float3 pad;
};

Texture2DArray<uint> ztx      : register(t0);  // 128x128 x slots, palette indices
Texture2D<uint>      zlu      : register(t1);  // [rung][texel] -> RGB444
Texture2D<uint>      zfg      : register(t2);  // [level][rgb444] -> RGB444
Texture2DArray<uint> skinTex  : register(t3);  // a model's skins, RGB444
Texture2D<uint>      ramp     : register(t4);  // [row][step] hit-flash ramps
Texture2D            colorTex : register(t5);  // a sprite, or a frame to blit
SamplerState         samp     : register(s0);

struct VSIn {
    float2 pos    : POSITION;
    float3 wuv    : TEXCOORD0;   // invW, u/w, v/w
    float2 lf     : TEXCOORD1;   // light, fog
    uint   params : TEXCOORD2;
};
struct VSOut {
    float4 pos                   : SV_Position;
    float2 uv                    : TEXCOORD0;   // perspective-correct
    noperspective float2 lf      : TEXCOORD1;   // screen-space, as the engine steps them
    nointerpolation uint params  : TEXCOORD2;
};

VSOut VSScene(VSIn i) {
    VSOut o;
    float w = 1.0 / i.wuv.x;
    float2 ndc = float2(i.pos.x / targetSize.x * 2.0 - 1.0, 1.0 - i.pos.y / targetSize.y * 2.0);
    o.pos = float4(ndc * w, depthScale, w);
    o.uv = i.wuv.yz * w;
    o.lf = i.lf;
    o.params = i.params;
    return o;
}

float3 Rgb444(uint c) {
    return float3((c >> 8) & 15, (c >> 4) & 15, c & 15) / 15.0;
}

// The fog remap on a finished colour word. Stepped: the engine's own
// `fog >> 12`. Smooth: the two neighbouring levels, blended.
float3 Fogged(uint c, float fog) {
    fog = clamp(fog, 0.0, 65535.0);
    uint level = min((uint)fog >> 12, 15u);
    float3 a = Rgb444(zfg.Load(int3(c & 0xfff, level, 0)));
    if (smooth == 0) return a;
    float3 b = Rgb444(zfg.Load(int3(c & 0xfff, min(level + 1, 15u), 0)));
    return lerp(a, b, frac(fog / 4096.0));
}

uint PaletteAt(uint texel, uint hue, uint rung) {
    return zlu.Load(int3(texel, rung + hue, 0));
}

// One surface texel, shaded. `key` is set when it is the cut-out colour.
float3 SurfaceTap(uint tex, int2 t, uint mask, uint hue, float light, float fog, out bool key) {
    uint texel = ztx.Load(int4(t.x & (int)mask, t.y & (int)mask, tex, 0));
    light = clamp(light, 0.0, 16128.0);
    uint rung = clamp((uint)light >> 8, 4u, 63u);
    uint c = PaletteAt(texel, hue, rung);
    key = (c == 0x0f0f);
    float3 rgb = Fogged(c, fog);
    if (smooth != 0) {
        uint c1 = PaletteAt(texel, hue, min(rung + 1, 63u));
        float t01 = (light < 1024.0 || light >= 16128.0) ? 0.0 : frac(light / 256.0);
        if (c1 != 0x0f0f) rgb = lerp(rgb, Fogged(c1, fog), t01);
    }
    return rgb;
}

float4 PSSurface(VSOut i) : SV_Target {
    uint tex = i.params & 0xff;
    uint hue = (i.params >> 8) & 3;
    uint mask = (i.params >> 16) & 0xff;
    bool key;
    int2 tn = int2(floor(i.uv));
    if (smooth == 0) {
        float3 c = SurfaceTap(tex, tn, mask, hue, i.lf.x, i.lf.y, key);
        if (key) discard;
        return float4(c, 1.0);
    }
    // Smooth: the nearest texel decides the cut-out, the four around the
    // sample point blend. A keyed neighbour takes the nearest one's colour,
    // so a cut-out edge stays an edge instead of fading to the key.
    float3 cn = SurfaceTap(tex, tn, 0x7f, hue, i.lf.x, i.lf.y, key);
    if (key) discard;
    float2 st = i.uv - 0.5;
    int2 t0 = int2(floor(st));
    float2 f = st - floor(st);
    bool k00, k10, k01, k11;
    float3 c00 = SurfaceTap(tex, t0, 0x7f, hue, i.lf.x, i.lf.y, k00);
    float3 c10 = SurfaceTap(tex, t0 + int2(1, 0), 0x7f, hue, i.lf.x, i.lf.y, k10);
    float3 c01 = SurfaceTap(tex, t0 + int2(0, 1), 0x7f, hue, i.lf.x, i.lf.y, k01);
    float3 c11 = SurfaceTap(tex, t0 + int2(1, 1), 0x7f, hue, i.lf.x, i.lf.y, k11);
    if (k00) c00 = cn;
    if (k10) c10 = cn;
    if (k01) c01 = cn;
    if (k11) c11 = cn;
    return float4(lerp(lerp(c00, c10, f.x), lerp(c01, c11, f.x), f.y), 1.0);
}

uint SkinAt(int2 t) {
    // Wrapped, as both engine rasterizers mask (see zone_renderer.cpp).
    t = t & int2((int)modelW - 1, (int)modelH - 1);
    return skinTex.Load(int4(t, skin, 0));
}

float3 ModelShade(uint c, float fog) {
    if (flashRow >= 0) return Rgb444(ramp.Load(int3((c >> 8) & 15, flashRow, 0)));
    return Fogged(c, fog);
}

float4 PSModel(VSOut i) : SV_Target {
    uint cn = SkinAt(int2(i.uv));   // the engine truncates, then wraps
    if (cn == 0x0f0f) discard;
    float3 nearest = ModelShade(cn, i.lf.y);
    if (smooth == 0) return float4(nearest, 1.0);
    float2 st = i.uv - 0.5;
    int2 t0 = int2(floor(st));
    float2 f = st - floor(st);
    uint s00 = SkinAt(t0), s10 = SkinAt(t0 + int2(1, 0));
    uint s01 = SkinAt(t0 + int2(0, 1)), s11 = SkinAt(t0 + int2(1, 1));
    float3 c00 = s00 == 0x0f0f ? nearest : ModelShade(s00, i.lf.y);
    float3 c10 = s10 == 0x0f0f ? nearest : ModelShade(s10, i.lf.y);
    float3 c01 = s01 == 0x0f0f ? nearest : ModelShade(s01, i.lf.y);
    float3 c11 = s11 == 0x0f0f ? nearest : ModelShade(s11, i.lf.y);
    return float4(lerp(lerp(c00, c10, f.x), lerp(c01, c11, f.x), f.y), 1.0);
}

// The skybox rasterizer: the texel and nothing else -- no key, no fog.
float4 PSSky(VSOut i) : SV_Target {
    if (smooth == 0) return float4(Rgb444(SkinAt(int2(i.uv))), 1.0);
    float2 st = i.uv - 0.5;
    int2 t0 = int2(floor(st));
    float2 f = st - floor(st);
    float3 c00 = Rgb444(SkinAt(t0)), c10 = Rgb444(SkinAt(t0 + int2(1, 0)));
    float3 c01 = Rgb444(SkinAt(t0 + int2(0, 1))), c11 = Rgb444(SkinAt(t0 + int2(1, 1)));
    return float4(lerp(lerp(c00, c10, f.x), lerp(c01, c11, f.x), f.y), 1.0);
}

// A global.spr quad: alpha is the 0xf0f mask, blendAlpha the blend weight.
float4 PSBillboard(VSOut i) : SV_Target {
    float4 c = colorTex.Sample(samp, i.uv);
    if (c.a < 0.5) discard;
    return float4(c.rgb, blendAlpha);
}

struct BlitOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

BlitOut VSBlit(uint id : SV_VertexID) {
    BlitOut o;
    float2 c = float2(id & 1, (id >> 1) & 1);
    o.pos = float4(lerp(rect.x, rect.z, c.x), lerp(rect.y, rect.w, c.y), 0.5, 1.0);
    o.uv = c;
    return o;
}

float4 PSBlit(BlitOut i) : SV_Target {
    return colorTex.Sample(samp, i.uv);
}
)hlsl";

struct FrameConstants {
    float targetSize[2];
    float depthScale;
    uint32_t smooth;
    uint32_t modelW;
    uint32_t modelH;
    int32_t flashRow;
    uint32_t skin;
    float rect[4];
    float blendAlpha;
    float pad[3];
};
static_assert(sizeof(FrameConstants) % 16 == 0, "constant buffers are 16-byte aligned");

// The nearest anything is drawn: a billboard is culled under 5 world units
// (kBillboardNearDepth), i.e. 5/256 of a tile, and everything else clips at
// 0.05. z/w = depthScale * invW therefore never exceeds 1.
constexpr float kDepthScale = kBillboardNearDepth / 256.0f * 0.98f;

uint32_t Widen565(uint16_t c) {
    const uint32_t r = (c >> 11) & 0x1f, g = (c >> 5) & 0x3f, b = c & 0x1f;
    return ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
}

using CompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*,
                                   LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

// The shader compiler ships with Windows (8.1 and later) as a system DLL.
// Loaded by name so a machine without it falls back to the software
// renderer instead of failing to start.
CompileFn LoadCompiler() {
    static CompileFn fn = []() -> CompileFn {
        HMODULE dll = LoadLibraryW(L"d3dcompiler_47.dll");
        return dll ? reinterpret_cast<CompileFn>(GetProcAddress(dll, "D3DCompile")) : nullptr;
    }();
    return fn;
}

}  // namespace

struct D3D11Renderer::Impl {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> ctx;
    std::string error;
    std::string adapter;
    bool ok = false;

    ComPtr<ID3D11VertexShader> vsScene, vsBlit;
    ComPtr<ID3D11PixelShader> psSurface, psModel, psSky, psBillboard, psBlit;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11Buffer> vertexBuffer;
    size_t vertexCapacity = 0;

    ComPtr<ID3D11SamplerState> pointSampler, linearSampler;
    ComPtr<ID3D11BlendState> opaqueBlend, alphaBlend;
    ComPtr<ID3D11DepthStencilState> depthOff, depthTestWrite;
    ComPtr<ID3D11RasterizerState> raster;

    // The scene target.
    ComPtr<ID3D11Texture2D> sceneTex, sceneDepth;
    ComPtr<ID3D11RenderTargetView> sceneRtv;
    ComPtr<ID3D11ShaderResourceView> sceneSrv;
    ComPtr<ID3D11DepthStencilView> sceneDsv;
    int sceneW = 0, sceneH = 0;
    bool sceneValid = false;

    // The window.
    HWND hwnd = nullptr;
    ComPtr<IDXGISwapChain> swap;
    ComPtr<ID3D11RenderTargetView> backRtv;
    int backW = 0, backH = 0;

    // A CPU frame on its way to the card (2D frames, and the 2D layer).
    ComPtr<ID3D11Texture2D> uploadTex;
    ComPtr<ID3D11ShaderResourceView> uploadSrv;
    int uploadW = 0, uploadH = 0;
    std::vector<uint32_t> scratch;

    // Caches.
    unsigned long long zoneSerial = 0;
    ComPtr<ID3D11ShaderResourceView> ztxSrv, zluSrv, zfgSrv, rampSrv;
    struct SkinEntry {
        ComPtr<ID3D11ShaderResourceView> srv;
    };
    std::unordered_map<const Model*, SkinEntry> skins;
    std::unordered_map<const Sprite*, ComPtr<ID3D11ShaderResourceView>> sprites;

    Stats stats;
    int texturesCreated = 0;

    bool Fail(const char* what, HRESULT hr = S_OK) {
        char buf[160];
        if (hr != S_OK) {
            std::snprintf(buf, sizeof(buf), "%s (0x%08lx)", what, static_cast<unsigned long>(hr));
        } else {
            std::snprintf(buf, sizeof(buf), "%s", what);
        }
        error = buf;
        return false;
    }

    bool Compile(CompileFn compile, const char* entry, const char* target, ComPtr<ID3DBlob>& out) {
        ComPtr<ID3DBlob> errors;
        const HRESULT hr = compile(kShaderSource, sizeof(kShaderSource) - 1, "shadowkey", nullptr,
                                   nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                                   out.GetAddressOf(), errors.GetAddressOf());
        if (FAILED(hr)) {
            std::string msg = std::string("shader ") + entry + ": ";
            if (errors) {
                msg.append(static_cast<const char*>(errors->GetBufferPointer()),
                           errors->GetBufferSize());
            }
            error = msg;
            return false;
        }
        return true;
    }

    // An immutable integer (or colour) texture from CPU data.
    ComPtr<ID3D11ShaderResourceView> MakeTexture(DXGI_FORMAT format, int w, int h, int slices,
                                                 const void* data, int bytesPerPixel,
                                                 bool asArray) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(w);
        desc.Height = static_cast<UINT>(h);
        desc.MipLevels = 1;
        desc.ArraySize = static_cast<UINT>(slices);
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        std::vector<D3D11_SUBRESOURCE_DATA> init(static_cast<size_t>(slices));
        const size_t sliceBytes = static_cast<size_t>(w) * h * bytesPerPixel;
        for (int i = 0; i < slices; ++i) {
            init[static_cast<size_t>(i)].pSysMem = static_cast<const uint8_t*>(data) + sliceBytes * i;
            init[static_cast<size_t>(i)].SysMemPitch = static_cast<UINT>(w * bytesPerPixel);
        }
        ComPtr<ID3D11Texture2D> tex;
        if (FAILED(device->CreateTexture2D(&desc, init.data(), tex.GetAddressOf()))) return nullptr;
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = format;
        if (asArray) {
            view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
            view.Texture2DArray.MipLevels = 1;
            view.Texture2DArray.ArraySize = static_cast<UINT>(slices);
        } else {
            view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            view.Texture2D.MipLevels = 1;
        }
        ComPtr<ID3D11ShaderResourceView> srv;
        if (FAILED(device->CreateShaderResourceView(tex.Get(), &view, srv.GetAddressOf()))) {
            return nullptr;
        }
        ++texturesCreated;
        return srv;
    }

    // The three per-zone lookups, built through the zone's own accessors so
    // they hold exactly what the software rasterizer would have read.
    bool EnsureZone(const Zone& zone) {
        if (zoneSerial == zone.resourceSerial() && ztxSrv && zluSrv && zfgSrv) return true;
        // A new zone: its sky mesh may sit where the last one's did, and the
        // model cache would otherwise only ever grow.
        skins.clear();
        ztxSrv.Reset();
        zluSrv.Reset();
        zfgSrv.Reset();

        const int slots = std::max(1, zone.textureSlotCount());
        std::vector<uint8_t> ztx(static_cast<size_t>(slots) * 128 * 128);
        for (int s = 0; s < slots; ++s) {
            for (int y = 0; y < 128; ++y) {
                for (int x = 0; x < 128; ++x) {
                    ztx[(static_cast<size_t>(s) * 128 + y) * 128 + x] = zone.TexelAt(s, x, y);
                }
            }
        }
        ztxSrv = MakeTexture(DXGI_FORMAT_R8_UINT, 128, 128, slots, ztx.data(), 1, true);

        // Rows 0..66: rung (4..63, the clamp is the shader's) plus the 0..3
        // hue bias. PaletteColor clamps the rung itself, so each row is read
        // with the hue at 0 and the rung where the row is.
        constexpr int kRows = 67;
        std::vector<uint16_t> zlu(static_cast<size_t>(kRows) * 256);
        for (int row = 0; row < kRows; ++row) {
            // row = rung + hue with rung in 4..63: use the largest hue that
            // keeps the rung in range, so every row comes back unclamped.
            const int hue = std::clamp(row - 63, 0, 3);
            const int rung = row - hue;
            for (int texel = 0; texel < 256; ++texel) {
                zlu[static_cast<size_t>(row) * 256 + texel] =
                    rung < 4 ? 0
                             : zone.PaletteColor(static_cast<uint8_t>(hue),
                                                 static_cast<uint16_t>(rung << 8),
                                                 static_cast<uint8_t>(texel));
            }
        }
        zluSrv = MakeTexture(DXGI_FORMAT_R16_UINT, 256, kRows, 1, zlu.data(), 2, false);

        std::vector<uint16_t> zfg(static_cast<size_t>(16) * 4096);
        for (int level = 0; level < 16; ++level) {
            for (int c = 0; c < 4096; ++c) {
                zfg[static_cast<size_t>(level) * 4096 + c] =
                    zone.FogColor(level, static_cast<uint16_t>(c)) & 0x0fff;
            }
        }
        zfgSrv = MakeTexture(DXGI_FORMAT_R16_UINT, 4096, 16, 1, zfg.data(), 2, false);

        if (!ztxSrv || !zluSrv || !zfgSrv) return Fail("zone textures");
        zoneSerial = zone.resourceSerial();
        return true;
    }

    bool EnsureRamp() {
        if (rampSrv) return true;
        const HitFlashRamps& ramps = FlashRamps();
        rampSrv = MakeTexture(DXGI_FORMAT_R16_UINT, 16, 14, 1, ramps.entry, 2, false);
        return rampSrv != nullptr;
    }

    ID3D11ShaderResourceView* SkinFor(const Model& model) {
        auto it = skins.find(&model);
        if (it != skins.end()) return it->second.srv.Get();
        SkinEntry entry;
        const size_t need =
            static_cast<size_t>(model.skinCount) * model.width * model.height;
        if (model.skinCount > 0 && model.width > 0 && model.height > 0 &&
            model.pixels.size() >= need) {
            entry.srv = MakeTexture(DXGI_FORMAT_R16_UINT, model.width, model.height,
                                    model.skinCount, model.pixels.data(), 2, true);
        }
        ID3D11ShaderResourceView* srv = entry.srv.Get();
        skins.emplace(&model, std::move(entry));
        return srv;
    }

    ID3D11ShaderResourceView* SpriteFor(const Sprite& sprite) {
        auto it = sprites.find(&sprite);
        if (it != sprites.end()) return it->second.Get();
        ComPtr<ID3D11ShaderResourceView> srv;
        const size_t n = static_cast<size_t>(sprite.width) * sprite.height;
        if (sprite.width > 0 && sprite.height > 0 && sprite.pixels.size() >= n) {
            std::vector<uint32_t> rgba(n);
            for (size_t i = 0; i < n; ++i) {
                const uint32_t c = Widen565(sprite.pixels[i]);  // 0x00RRGGBB
                rgba[i] = (sprite.opaque[i] ? 0xff000000u : 0u) | ((c & 0xff) << 16) |
                          (c & 0xff00) | ((c >> 16) & 0xff);
            }
            srv = MakeTexture(DXGI_FORMAT_R8G8B8A8_UNORM, sprite.width, sprite.height, 1,
                              rgba.data(), 4, false);
        }
        ID3D11ShaderResourceView* raw = srv.Get();
        sprites.emplace(&sprite, std::move(srv));
        return raw;
    }

    bool EnsureScene(int w, int h) {
        if (sceneTex && sceneW == w && sceneH == h) return true;
        sceneTex.Reset();
        sceneDepth.Reset();
        sceneRtv.Reset();
        sceneSrv.Reset();
        sceneDsv.Reset();
        sceneValid = false;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(w);
        desc.Height = static_cast<UINT>(h);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, sceneTex.GetAddressOf()))) {
            return Fail("scene target");
        }
        D3D11_TEXTURE2D_DESC depth = desc;
        depth.Format = DXGI_FORMAT_D32_FLOAT;
        depth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        if (FAILED(device->CreateTexture2D(&depth, nullptr, sceneDepth.GetAddressOf())) ||
            FAILED(device->CreateRenderTargetView(sceneTex.Get(), nullptr,
                                                  sceneRtv.GetAddressOf())) ||
            FAILED(device->CreateShaderResourceView(sceneTex.Get(), nullptr,
                                                    sceneSrv.GetAddressOf())) ||
            FAILED(device->CreateDepthStencilView(sceneDepth.Get(), nullptr,
                                                  sceneDsv.GetAddressOf()))) {
            sceneTex.Reset();
            return Fail("scene views");
        }
        sceneW = w;
        sceneH = h;
        return true;
    }

    bool EnsureVertexBuffer(size_t count) {
        if (vertexBuffer && vertexCapacity >= count) return true;
        vertexBuffer.Reset();
        size_t capacity = 4096;
        while (capacity < count) capacity *= 2;
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(capacity * sizeof(GpuVertex));
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device->CreateBuffer(&desc, nullptr, vertexBuffer.GetAddressOf()))) {
            return Fail("vertex buffer");
        }
        vertexCapacity = capacity;
        return true;
    }

    void SetConstants(const FrameConstants& c) {
        D3D11_MAPPED_SUBRESOURCE map{};
        if (SUCCEEDED(ctx->Map(constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
            std::memcpy(map.pData, &c, sizeof(c));
            ctx->Unmap(constants.Get(), 0);
        }
    }

    // ---- the window ----

    bool EnsureBackBuffer() {
        if (!swap) return false;
        RECT client{};
        GetClientRect(hwnd, &client);
        const int w = client.right - client.left, h = client.bottom - client.top;
        if (w <= 0 || h <= 0) return false;  // minimized
        if (backRtv && w == backW && h == backH) return true;
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
        backRtv.Reset();
        if (backW != 0 &&
            FAILED(swap->ResizeBuffers(1, static_cast<UINT>(w), static_cast<UINT>(h),
                                       DXGI_FORMAT_B8G8R8A8_UNORM,
                                       DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE))) {
            return Fail("resize");
        }
        ComPtr<ID3D11Texture2D> back;
        if (FAILED(swap->GetBuffer(0, IID_PPV_ARGS(back.GetAddressOf()))) ||
            FAILED(device->CreateRenderTargetView(back.Get(), nullptr, backRtv.GetAddressOf()))) {
            return Fail("back buffer");
        }
        backW = w;
        backH = h;
        return true;
    }

    // Copies a 0x00RRGGBB / 0xAARRGGBB image into the upload texture.
    bool Upload(const uint32_t* pixels, int w, int h) {
        if (!uploadTex || uploadW != w || uploadH != h) {
            uploadTex.Reset();
            uploadSrv.Reset();
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = static_cast<UINT>(w);
            desc.Height = static_cast<UINT>(h);
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(device->CreateTexture2D(&desc, nullptr, uploadTex.GetAddressOf())) ||
                FAILED(device->CreateShaderResourceView(uploadTex.Get(), nullptr,
                                                        uploadSrv.GetAddressOf()))) {
                uploadTex.Reset();
                return Fail("upload texture");
            }
            uploadW = w;
            uploadH = h;
        }
        D3D11_MAPPED_SUBRESOURCE map{};
        if (FAILED(ctx->Map(uploadTex.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
            return Fail("upload map");
        }
        for (int y = 0; y < h; ++y) {
            std::memcpy(static_cast<uint8_t*>(map.pData) + static_cast<size_t>(y) * map.RowPitch,
                        pixels + static_cast<size_t>(y) * w, static_cast<size_t>(w) * 4);
        }
        ctx->Unmap(uploadTex.Get(), 0);
        return true;
    }

    // Draws `srv` over the viewport rectangle `view` of the back buffer.
    void Blit(ID3D11ShaderResourceView* srv, const Viewport& view, bool linear, bool blend) {
        FrameConstants c{};
        c.rect[0] = static_cast<float>(view.x) / backW * 2.0f - 1.0f;
        c.rect[1] = 1.0f - static_cast<float>(view.y) / backH * 2.0f;
        c.rect[2] = static_cast<float>(view.x + view.width) / backW * 2.0f - 1.0f;
        c.rect[3] = 1.0f - static_cast<float>(view.y + view.height) / backH * 2.0f;
        SetConstants(c);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        ctx->VSSetShader(vsBlit.Get(), nullptr, 0);
        ctx->PSSetShader(psBlit.Get(), nullptr, 0);
        ctx->PSSetShaderResources(5, 1, &srv);
        ID3D11SamplerState* s = linear ? linearSampler.Get() : pointSampler.Get();
        ctx->PSSetSamplers(0, 1, &s);
        ctx->OMSetBlendState(blend ? alphaBlend.Get() : opaqueBlend.Get(), nullptr, 0xffffffff);
        ctx->OMSetDepthStencilState(depthOff.Get(), 0);
        ctx->Draw(4, 0);
        ID3D11ShaderResourceView* none = nullptr;
        ctx->PSSetShaderResources(5, 1, &none);
    }

    bool BeginBackBuffer() {
        if (!EnsureBackBuffer()) return false;
        ID3D11RenderTargetView* rtv = backRtv.Get();
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        D3D11_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(backW), static_cast<float>(backH), 0.0f,
                          1.0f};
        ctx->RSSetViewports(1, &vp);
        ctx->RSSetState(raster.Get());
        ID3D11Buffer* cb = constants.Get();
        ctx->VSSetConstantBuffers(0, 1, &cb);
        ctx->PSSetConstantBuffers(0, 1, &cb);
        const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        ctx->ClearRenderTargetView(rtv, black);  // the letterbox bars
        return true;
    }

    bool EndBackBuffer(const GdiOverlay& overlay) {
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
        if (overlay) {
            // The debug overlay is GDI. A GDI-compatible swap chain hands
            // out a device context for its back buffer, so the overlay code
            // draws exactly as it does on the software path.
            ComPtr<IDXGISurface1> surface;
            if (SUCCEEDED(swap->GetBuffer(0, IID_PPV_ARGS(surface.GetAddressOf())))) {
                HDC hdc = nullptr;
                if (SUCCEEDED(surface->GetDC(FALSE, &hdc)) && hdc) {
                    overlay(hdc, backW, backH);
                    surface->ReleaseDC(nullptr);
                }
            }
        }
        const HRESULT hr = swap->Present(0, 0);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            ok = false;
            return Fail("device lost", hr);
        }
        return SUCCEEDED(hr);
    }
};

D3D11Renderer::D3D11Renderer() : impl_(new Impl()) {}

D3D11Renderer::~D3D11Renderer() {
    if (impl_->ctx) impl_->ctx->ClearState();
    delete impl_;
}

bool D3D11Renderer::ready() const { return impl_->ok; }
const std::string& D3D11Renderer::lastError() const { return impl_->error; }
const std::string& D3D11Renderer::adapterName() const { return impl_->adapter; }
bool D3D11Renderer::hasWindow() const { return impl_->swap != nullptr; }
D3D11Renderer::Stats D3D11Renderer::lastStats() const {
    Stats s = impl_->stats;
    s.texturesCreated = impl_->texturesCreated;
    return s;
}

bool D3D11Renderer::Init(bool warp) {
    Impl& d = *impl_;
    if (d.ok) return true;
    const CompileFn compile = LoadCompiler();
    if (!compile) return d.Fail("d3dcompiler_47.dll is not available");

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                        D3D_FEATURE_LEVEL_10_0};
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = E_FAIL;
    if (!warp) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                               static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
                               d.device.GetAddressOf(), nullptr, d.ctx.GetAddressOf());
    }
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels,
                               static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
                               d.device.GetAddressOf(), nullptr, d.ctx.GetAddressOf());
    }
    if (FAILED(hr)) return d.Fail("no Direct3D 11 device", hr);

    {
        ComPtr<IDXGIDevice> dxgi;
        ComPtr<IDXGIAdapter> adapter;
        DXGI_ADAPTER_DESC desc{};
        if (SUCCEEDED(d.device.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(adapter.GetAddressOf())) &&
            SUCCEEDED(adapter->GetDesc(&desc))) {
            char name[160] = {};
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name) - 1, nullptr,
                                nullptr);
            d.adapter = name;
        }
    }

    ComPtr<ID3DBlob> vsScene, vsBlit, ps;
    if (!d.Compile(compile, "VSScene", "vs_4_0", vsScene) ||
        !d.Compile(compile, "VSBlit", "vs_4_0", vsBlit)) {
        return false;
    }
    d.device->CreateVertexShader(vsScene->GetBufferPointer(), vsScene->GetBufferSize(), nullptr,
                                 d.vsScene.GetAddressOf());
    d.device->CreateVertexShader(vsBlit->GetBufferPointer(), vsBlit->GetBufferSize(), nullptr,
                                 d.vsBlit.GetAddressOf());
    struct {
        const char* entry;
        ComPtr<ID3D11PixelShader>* out;
    } pixelShaders[] = {{"PSSurface", &d.psSurface}, {"PSModel", &d.psModel},
                        {"PSSky", &d.psSky},         {"PSBillboard", &d.psBillboard},
                        {"PSBlit", &d.psBlit}};
    for (const auto& entry : pixelShaders) {
        ps.Reset();
        if (!d.Compile(compile, entry.entry, "ps_4_0", ps)) return false;
        d.device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr,
                                    entry.out->GetAddressOf());
    }

    const D3D11_INPUT_ELEMENT_DESC elements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(GpuVertex, x),
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(GpuVertex, invW),
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(GpuVertex, light),
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 2, DXGI_FORMAT_R32_UINT, 0, offsetof(GpuVertex, params),
         D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(d.device->CreateInputLayout(elements, static_cast<UINT>(std::size(elements)),
                                           vsScene->GetBufferPointer(), vsScene->GetBufferSize(),
                                           d.layout.GetAddressOf()))) {
        return d.Fail("input layout");
    }

    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(FrameConstants);
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(d.device->CreateBuffer(&cb, nullptr, d.constants.GetAddressOf()))) {
        return d.Fail("constant buffer");
    }

    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    d.device->CreateSamplerState(&sampler, d.pointSampler.GetAddressOf());
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    d.device->CreateSamplerState(&sampler, d.linearSampler.GetAddressOf());

    D3D11_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    d.device->CreateBlendState(&blend, d.opaqueBlend.GetAddressOf());
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    d.device->CreateBlendState(&blend, d.alphaBlend.GetAddressOf());

    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
    d.device->CreateDepthStencilState(&depth, d.depthOff.GetAddressOf());
    // The software test draws a pixel only when it is strictly nearer than
    // what is there (`invW <= stored` skips), and stores what it drew.
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depth.DepthFunc = D3D11_COMPARISON_GREATER;
    d.device->CreateDepthStencilState(&depth, d.depthTestWrite.GetAddressOf());

    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;  // the software rasterizer draws both windings
    raster.DepthClipEnable = FALSE;
    d.device->CreateRasterizerState(&raster, d.raster.GetAddressOf());

    if (!d.vsScene || !d.vsBlit || !d.psSurface || !d.psModel || !d.psSky || !d.psBillboard ||
        !d.psBlit || !d.pointSampler || !d.linearSampler || !d.opaqueBlend || !d.alphaBlend ||
        !d.depthOff || !d.depthTestWrite || !d.raster) {
        return d.Fail("pipeline state");
    }
    d.ok = true;
    return true;
}

bool D3D11Renderer::AttachWindow(void* hwnd) {
    Impl& d = *impl_;
    if (!d.ok) return false;
    if (d.swap && d.hwnd == hwnd) return true;
    DetachWindow();
    ComPtr<IDXGIDevice> dxgi;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory> factory;
    if (FAILED(d.device.As(&dxgi)) || FAILED(dxgi->GetAdapter(adapter.GetAddressOf())) ||
        FAILED(adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf())))) {
        return d.Fail("dxgi factory");
    }
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 1;
    desc.OutputWindow = static_cast<HWND>(hwnd);
    desc.Windowed = TRUE;
    // A copy-model chain, not a flip one: it is the kind that can hand GDI
    // a device context (the debug overlay), and the kind GDI can draw to the
    // same window after -- which is what switching back to the software
    // renderer needs.
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE;
    const HRESULT hr = factory->CreateSwapChain(d.device.Get(), &desc, d.swap.GetAddressOf());
    if (FAILED(hr)) return d.Fail("swap chain", hr);
    // The window handles Alt+Enter itself (borderless fullscreen, M120).
    factory->MakeWindowAssociation(static_cast<HWND>(hwnd),
                                   DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    d.hwnd = static_cast<HWND>(hwnd);
    d.backW = d.backH = 0;
    return true;
}

void D3D11Renderer::DetachWindow() {
    Impl& d = *impl_;
    if (d.ctx) d.ctx->OMSetRenderTargets(0, nullptr, nullptr);
    d.backRtv.Reset();
    d.swap.Reset();
    d.hwnd = nullptr;
    d.backW = d.backH = 0;
}

void D3D11Renderer::FlushCaches() {
    Impl& d = *impl_;
    d.skins.clear();
    d.sprites.clear();
    d.ztxSrv.Reset();
    d.zluSrv.Reset();
    d.zfgSrv.Reset();
    d.zoneSerial = 0;
}

bool D3D11Renderer::DrawScene(const GpuFrame& frame, Filter filter) {
    Impl& d = *impl_;
    if (!d.ok || frame.width <= 0 || frame.height <= 0 || !frame.zone) return false;
    if (!d.EnsureScene(frame.width, frame.height) || !d.EnsureZone(*frame.zone) ||
        !d.EnsureRamp() || !d.EnsureVertexBuffer(std::max<size_t>(frame.vertices.size(), 3))) {
        return false;
    }
    d.stats = Stats{};

    ID3D11DeviceContext* ctx = d.ctx.Get();
    if (!frame.vertices.empty()) {
        D3D11_MAPPED_SUBRESOURCE map{};
        if (FAILED(ctx->Map(d.vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
            return d.Fail("vertex map");
        }
        std::memcpy(map.pData, frame.vertices.data(), frame.vertices.size() * sizeof(GpuVertex));
        ctx->Unmap(d.vertexBuffer.Get(), 0);
    }

    ID3D11RenderTargetView* rtv = d.sceneRtv.Get();
    ctx->OMSetRenderTargets(1, &rtv, d.sceneDsv.Get());
    const uint32_t bg = Widen565(frame.background565);
    const float clear[4] = {((bg >> 16) & 0xff) / 255.0f, ((bg >> 8) & 0xff) / 255.0f,
                            (bg & 0xff) / 255.0f, 1.0f};
    ctx->ClearRenderTargetView(rtv, clear);
    ctx->ClearDepthStencilView(d.sceneDsv.Get(), D3D11_CLEAR_DEPTH, 0.0f, 0);  // 0 == infinitely far
    D3D11_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(frame.width),
                      static_cast<float>(frame.height), 0.0f, 1.0f};
    ctx->RSSetViewports(1, &vp);
    ctx->RSSetState(d.raster.Get());

    ctx->IASetInputLayout(d.layout.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11Buffer* vb = d.vertexBuffer.Get();
    const UINT stride = sizeof(GpuVertex), offset = 0;
    ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    ctx->VSSetShader(d.vsScene.Get(), nullptr, 0);
    ID3D11Buffer* cb = d.constants.Get();
    ctx->VSSetConstantBuffers(0, 1, &cb);
    ctx->PSSetConstantBuffers(0, 1, &cb);
    ID3D11ShaderResourceView* zoneViews[3] = {d.ztxSrv.Get(), d.zluSrv.Get(), d.zfgSrv.Get()};
    ctx->PSSetShaderResources(0, 3, zoneViews);
    ID3D11ShaderResourceView* rampView = d.rampSrv.Get();
    ctx->PSSetShaderResources(4, 1, &rampView);
    ID3D11SamplerState* pointSampler = d.pointSampler.Get();
    ctx->PSSetSamplers(0, 1, &pointSampler);

    FrameConstants base{};
    base.targetSize[0] = static_cast<float>(frame.width);
    base.targetSize[1] = static_cast<float>(frame.height);
    base.depthScale = kDepthScale;
    base.smooth = filter == Filter::Smooth ? 1u : 0u;
    base.flashRow = -1;
    base.blendAlpha = 1.0f;

    for (const GpuBatch& batch : frame.batches) {
        if (batch.vertexCount < 3) continue;
        FrameConstants c = base;
        switch (batch.kind) {
            case GpuBatch::Kind::Surface:
                ctx->PSSetShader(d.psSurface.Get(), nullptr, 0);
                ctx->OMSetBlendState(d.opaqueBlend.Get(), nullptr, 0xffffffff);
                ctx->OMSetDepthStencilState(d.depthTestWrite.Get(), 0);
                break;
            case GpuBatch::Kind::Sky:
            case GpuBatch::Kind::Model: {
                if (!batch.model) continue;
                ID3D11ShaderResourceView* skin = d.SkinFor(*batch.model);
                if (!skin) continue;  // no pixels: every texel would be the key
                ctx->PSSetShaderResources(3, 1, &skin);
                c.modelW = static_cast<uint32_t>(batch.model->width);
                c.modelH = static_cast<uint32_t>(batch.model->height);
                c.skin = static_cast<uint32_t>(
                    std::clamp(batch.skinIndex, 0, batch.model->skinCount - 1));
                const bool sky = batch.kind == GpuBatch::Kind::Sky;
                c.flashRow = sky ? -1 : std::min(batch.flashLevel, 13);
                ctx->PSSetShader(sky ? d.psSky.Get() : d.psModel.Get(), nullptr, 0);
                ctx->OMSetBlendState(d.opaqueBlend.Get(), nullptr, 0xffffffff);
                ctx->OMSetDepthStencilState(sky ? d.depthOff.Get() : d.depthTestWrite.Get(), 0);
                break;
            }
            case GpuBatch::Kind::Billboard: {
                if (!batch.sprite) continue;
                ID3D11ShaderResourceView* sprite = d.SpriteFor(*batch.sprite);
                if (!sprite) continue;
                ctx->PSSetShaderResources(5, 1, &sprite);
                // `FillBillboard`'s mix: level 1 is a quarter source, 2 a
                // half, anything else three quarters.
                const bool blended = batch.blendMode == 1;
                c.blendAlpha = !blended ? 1.0f
                                        : batch.blendLevel == 1 ? 0.25f
                                                                : batch.blendLevel == 2 ? 0.5f : 0.75f;
                ctx->PSSetShader(d.psBillboard.Get(), nullptr, 0);
                ctx->OMSetBlendState(blended ? d.alphaBlend.Get() : d.opaqueBlend.Get(), nullptr,
                                     0xffffffff);
                ctx->OMSetDepthStencilState(d.depthTestWrite.Get(), 0);
                break;
            }
        }
        d.SetConstants(c);
        ctx->Draw(batch.vertexCount, batch.firstVertex);
        ++d.stats.drawCalls;
        d.stats.triangles += static_cast<int>(batch.vertexCount / 3);
    }

    ID3D11ShaderResourceView* none[6] = {};
    ctx->PSSetShaderResources(0, 6, none);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    d.sceneValid = true;
    return true;
}

bool D3D11Renderer::ReadScene(std::vector<uint32_t>& pixels, int* width, int* height) {
    Impl& d = *impl_;
    if (!d.ok || !d.sceneTex) return false;
    D3D11_TEXTURE2D_DESC desc{};
    d.sceneTex->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(d.device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()))) return false;
    d.ctx->CopyResource(staging.Get(), d.sceneTex.Get());
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(d.ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map))) return false;
    pixels.resize(static_cast<size_t>(d.sceneW) * d.sceneH);
    for (int y = 0; y < d.sceneH; ++y) {
        const uint32_t* src = reinterpret_cast<const uint32_t*>(
            static_cast<const uint8_t*>(map.pData) + static_cast<size_t>(y) * map.RowPitch);
        for (int x = 0; x < d.sceneW; ++x) {
            pixels[static_cast<size_t>(y) * d.sceneW + x] = src[x] & 0x00ffffff;
        }
    }
    d.ctx->Unmap(staging.Get(), 0);
    if (width) *width = d.sceneW;
    if (height) *height = d.sceneH;
    return true;
}

bool D3D11Renderer::PresentBackbuffer(const Backbuffer& frame, const GdiOverlay& overlay) {
    Impl& d = *impl_;
    if (!d.ok || !d.swap) return false;
    if (!d.BeginBackBuffer()) return false;
    const int w = frame.width(), h = frame.height();
    d.scratch.resize(static_cast<size_t>(w) * h);
    const uint16_t* in = frame.Data();
    for (size_t i = 0, n = d.scratch.size(); i < n; ++i) d.scratch[i] = 0xff000000u | Widen565(in[i]);
    if (!d.Upload(d.scratch.data(), w, h)) return false;
    // The frame's own shape at the native height -- the GDI path's rule.
    const int logicalWidth = h > 0 ? w * Backbuffer::kHeight / h : Backbuffer::kWidth;
    const Viewport view = FitPreservingAspect(d.backW, d.backH, logicalWidth, Backbuffer::kHeight);
    d.Blit(d.uploadSrv.Get(), view, /*linear=*/false, /*blend=*/false);
    return d.EndBackBuffer(overlay);
}

bool D3D11Renderer::PresentScene(const Backbuffer& layer, const Backbuffer& under,
                                 const GdiOverlay& overlay) {
    Impl& d = *impl_;
    if (!d.ok || !d.swap || !d.sceneValid) return false;
    if (!d.BeginBackBuffer()) return false;
    const int w = layer.width(), h = layer.height();
    const Viewport view = FitPreservingAspect(d.backW, d.backH, w, h);

    // The scene: unfiltered when it lands on whole pixels, smoothed when the
    // target and the window disagree (a fixed multiple in an odd window).
    const bool exact = view.width % d.sceneW == 0 && view.height % d.sceneH == 0;
    d.Blit(d.sceneSrv.Get(), view, /*linear=*/!exact, /*blend=*/false);

    // The 2D layer: transparent wherever the 2D code changed nothing.
    const bool comparable = under.width() == w && under.height() == h;
    d.scratch.resize(static_cast<size_t>(w) * h);
    const uint16_t* ui = layer.Data();
    const uint16_t* keep = comparable ? under.Data() : nullptr;
    bool any = false;
    for (size_t i = 0, n = d.scratch.size(); i < n; ++i) {
        if (keep && ui[i] == keep[i]) {
            d.scratch[i] = 0;
        } else {
            d.scratch[i] = 0xff000000u | Widen565(ui[i]);
            any = true;
        }
    }
    if (any) {
        if (!d.Upload(d.scratch.data(), w, h)) return false;
        d.Blit(d.uploadSrv.Get(), view, /*linear=*/false, /*blend=*/true);
    }
    return d.EndBackBuffer(overlay);
}

}  // namespace sk
