#pragma once
#include <string>

namespace sk {

// M119: the PC-only display settings -- Resolution and Frame Rate, and
// (M120) Aspect Ratio -- that the Options screen offers alongside the
// game's own rows.
//
// Both default to **Original**, and Original is not an approximation of the
// device: it is the code path the port had before these settings existed.
// Resolution Original draws the 3D view straight into the 176x208
// Backbuffer; Frame Rate Original presents once per 40ms tick. Nothing
// about either changes when the other is picked, and neither touches the
// simulation -- the game always ticks at the engine's own 25Hz
// (engine/game_clock.h), so a player's choice here cannot alter how the
// game plays, only how often and how sharply it is drawn.
//
// Stored in their own file (kFileName) beside `dragonstar.set`, not in it:
// that file is the engine's own format (assets/game_config.h) and stays
// byte-for-byte what the original writes.

// Resolution: the 3D view's render scale. 1 is the device's own 176x208;
// kAuto follows the window, picking the smallest whole multiple that is at
// least as tall as the picture on screen.
constexpr int kResolutionAuto = 0;
constexpr int kResolutionScales[] = {1, kResolutionAuto, 2, 3, 4, 5, 6};
constexpr int kResolutionOptionCount =
    static_cast<int>(sizeof(kResolutionScales) / sizeof(kResolutionScales[0]));
// Auto never goes past this: a 6x scene is 1056x1248, already taller than
// a 1080p or 1200p screen, and the software rasterizer's cost grows with
// the square of the scale.
constexpr int kMaxRenderScale = 6;

// Frame Rate: how often the 3D view is drawn. kFrameRateOriginal presents
// once per tick, as the device did. The others draw between ticks too,
// with the camera and every moving thing interpolated between the last two
// ticks (engine/frame_interpolation.h). kFrameRateVSync paces to the
// monitor through the desktop compositor; kFrameRateUnlimited draws as
// often as the loop comes round.
constexpr int kFrameRateOriginal = 25;
constexpr int kFrameRateVSync = -1;
constexpr int kFrameRateUnlimited = 0;
constexpr int kFrameRates[] = {kFrameRateOriginal, kFrameRateVSync, 30,  60, 75, 120,
                               144,                165,             240, kFrameRateUnlimited};
constexpr int kFrameRateOptionCount =
    static_cast<int>(sizeof(kFrameRates) / sizeof(kFrameRates[0]));

// M120: Aspect Ratio -- the shape of the 3D view. Original is the device's
// own 176x208. The others widen the view to that shape at the same height:
// the vertical field of view, the focal length and the pixel shape are all
// kept, so nothing in the world is stretched and the player simply sees
// further to each side (graphics/scene_upscale.h has how the 2D layer sits
// in it). kAspectAuto takes the window's own shape -- the whole screen,
// fullscreen. Each value is `width * 100 + height` of the ratio, which is
// also what pc_settings.cfg stores.
constexpr int kAspectOriginal = 0;
constexpr int kAspectAuto = -1;
constexpr int kAspectRatios[] = {kAspectOriginal, kAspectAuto, 403, 1610, 1609, 2109};
constexpr int kAspectOptionCount =
    static_cast<int>(sizeof(kAspectRatios) / sizeof(kAspectRatios[0]));
// The widest view any setting draws, in native pixels: 21:9 at 208 tall,
// already 134 degrees across at the engine's 104px focal length. The
// engine's visibility raycast fans out 140 (world/zone.cpp's TierFor), and a
// flat projection wider still is mostly stretched edges, so a wider window
// is pillarboxed instead.
constexpr int kMaxViewWidth = 486;

// M123: fog density, as a percentage of the engine's own. The fog level is
// `depth * engine+0x5c8 >> 8` (zone_renderer.cpp's FogScalar), so a
// percentage scales that factor and nothing else: 100 is the device, and
// the original code path. Ordered so that cycling right from Original goes
// heavier first and wraps through Off back up to it.
constexpr int kFogOriginal = 100;
constexpr int kFogDensities[] = {kFogOriginal, 150, 200, 0, 25, 50, 75};
constexpr int kFogOptionCount =
    static_cast<int>(sizeof(kFogDensities) / sizeof(kFogDensities[0]));

// M125: mouse look -- the mouse turns the view and tilts it, as the right
// stick does on a controller. The device had no mouse, so there is no
// "Original" here: the values are sensitivities, as a percentage of
// kMouseLookRadiansPerCount, and 0 is Off (the pre-M125 behaviour, the
// cursor free and the mouse only a button). On at 100% by default.
constexpr int kMouseLookOff = 0;
constexpr int kMouseLookSensitivities[] = {100, 150, 200, 300, kMouseLookOff, 50, 75};
constexpr int kMouseLookOptionCount =
    static_cast<int>(sizeof(kMouseLookSensitivities) / sizeof(kMouseLookSensitivities[0]));
// At 100%: about 0.13 degrees per count, i.e. a full turn in ~2900 counts
// (a ~18cm sweep on an 800dpi mouse) -- the usual default of PC shooters.
constexpr float kMouseLookRadiansPerCount = 0.0022f;

struct PcSettings {
    int resolutionIndex = 0;  // into kResolutionScales; 0 = Original
    int frameRateIndex = 0;   // into kFrameRates; 0 = Original
    int aspectIndex = 0;      // into kAspectRatios; 0 = Original (M120)
    int fogIndex = 0;         // into kFogDensities; 0 = Original (M123)
    int mouseLookIndex = 0;   // into kMouseLookSensitivities; 0 = 100% (M125)

    int resolutionScale() const { return kResolutionScales[resolutionIndex]; }
    int frameRate() const { return kFrameRates[frameRateIndex]; }
    int aspect() const { return kAspectRatios[aspectIndex]; }
    int fogPercent() const { return kFogDensities[fogIndex]; }
    int mouseLookPercent() const { return kMouseLookSensitivities[mouseLookIndex]; }
    bool mouseLook() const { return mouseLookPercent() != kMouseLookOff; }
    float mouseLookRadiansPerCount() const {
        return kMouseLookRadiansPerCount * static_cast<float>(mouseLookPercent()) / 100.0f;
    }
    bool originalResolution() const { return resolutionScale() == 1; }
    bool originalFrameRate() const { return frameRate() == kFrameRateOriginal; }
    bool originalAspect() const { return aspect() == kAspectOriginal; }
};

// M120: the width, in native pixels at the native 208 height, of the view
// Aspect Ratio means in a window whose client area is `clientWidth` x
// `clientHeight` (either 0 when unknown): 176 for Original, never less than
// 176 or more than kMaxViewWidth, and always 176 plus an even number, so the
// original 176-pixel picture sits exactly in its middle.
int ViewWidthFor(const PcSettings& settings, int clientWidth, int clientHeight);

// The scale Resolution means for a picture `viewHeight` pixels tall on
// screen (0 when unknown): the setting's own scale, or for Auto the
// smallest whole multiple of 208 that reaches it, within 1..kMaxRenderScale.
int RenderScaleFor(const PcSettings& settings, int viewHeight);

// The Options screen's view of the settings: rows of "Name: value" text in
// the game's current language, and a way to step one. MenuExecutable only
// sees this interface (simkin_bindings/menu_executable.h), so a smoke test
// can drive the rows without a window.
class PcSettingsControl {
public:
    virtual ~PcSettingsControl() = default;
    virtual int RowCount() const = 0;
    // `language` is the engine's language index (simkin_bindings/language.h):
    // 0 English, 1 Spanish, 2 German, 3 French, 4 Italian, 5 UK English.
    // Latin-1, like the string table the rest of the menu is drawn from.
    virtual std::string RowText(int row, int language) const = 0;
    // Steps row `row` by `delta` (+1 = next option), wrapping at either end.
    virtual void Cycle(int row, int delta) = 0;
};

// The real one: owns the settings, and writes kFileName after every change.
class PcSettingsStore : public PcSettingsControl {
public:
    static constexpr const char* kFileName = "pc_settings.cfg";

    // `path` may be empty, in which case nothing is read or written.
    explicit PcSettingsStore(std::string path = std::string());

    const PcSettings& settings() const { return settings_; }
    void Set(const PcSettings& settings);

    // Unknown keys and out-of-range values are ignored, so a file from a
    // newer build never breaks an older one; a missing file is a first run.
    bool Load();
    bool Save() const;

    enum Row {
        kRowResolution = 0,
        kRowAspect = 1,
        kRowFrameRate = 2,
        kRowFog = 3,        // M123
        kRowMouseLook = 4,  // M125
        kRowCount = 5
    };
    int RowCount() const override { return kRowCount; }
    std::string RowText(int row, int language) const override;
    void Cycle(int row, int delta) override;

private:
    std::string path_;
    PcSettings settings_;
};

// The value half of a row, in `language` -- exposed for the smoke test.
std::string ResolutionValueText(int scale, int language);
std::string FrameRateValueText(int fps, int language);
std::string AspectValueText(int aspect, int language);
std::string FogValueText(int percent, int language);
std::string MouseLookValueText(int percent, int language);

}  // namespace sk
