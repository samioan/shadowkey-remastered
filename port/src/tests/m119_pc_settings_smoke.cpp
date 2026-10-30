// M119 smoke test: the PC-only display settings (Resolution, Frame Rate,
// and M120's Aspect Ratio) and controller support.
//
//   1. The settings model: defaults are Original, the option lists wrap,
//      the file round-trips by value, Auto follows the window.
//   2. The Options screen, through the real options.s: the three rows take
//      the places of the three blank spacers (M120: the top one moved down
//      under the sliders), every row from Select Language down keeps its
//      index, Enter/Left/Right step them, `ClearMenu(); Init();` (a language
//      change) brings them back relabelled, and a stack with no settings
//      control leaves the screen exactly the script's own.
//   3. Resolution: the renderer at the native size is the pre-M119 frame,
//      byte for byte; a 2x scene is the same view at twice the size; the
//      melee probe reads the same creature at either size; the compositor
//      keeps untouched scene pixels at full resolution and the 2D layer's
//      pixels as blocks.
//   4. Frame Rate: the interpolator's in-between frames -- the midpoint,
//      heading wrap, teleports snapped, new things drawn where they are.
//   5. The controller: context mapping, bindings followed, a held button
//      keeping the key it pressed across a screen change, keyboard and pad
//      on one slot, and a pad that vanishes letting go.
//
// Run from the repo root (reads the real game data and, if present, the
// ROM font).

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "engine/frame_interpolation.h"
#include "engine/gamepad_map.h"
#include "engine/input_state.h"
#include "engine/pc_settings.h"
#include "graphics/backbuffer.h"
#include "graphics/bitmap_font.h"
#include "graphics/scene_upscale.h"
#include "render3d/camera.h"
#include "render3d/zone_renderer.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"
#include "world/model_archive.h"
#include "world/zone.h"

namespace sk_b = sk_bindings;
using Menu = sk_b::MenuExecutable;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("  %-92s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    if (!ok) ++g_failures;
}

bool FileExists(const std::string& path) { return std::ifstream(path).good(); }

std::string RowLabel(const Menu::MenuRow& row, const sk::StringTable& strings) {
    if (!row.literalText.empty()) return row.literalText;
    return row.textId >= 0 ? strings.Get(row.textId) : std::string();
}

bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string root =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";

    // ---------------------------------------------------------------
    std::printf("\n== Part 1: the settings ==\n");
    // ---------------------------------------------------------------
    {
        sk::PcSettingsStore store;
        Check(store.settings().originalResolution() && store.settings().originalFrameRate(),
              "a first run is Original / Original");
        Check(store.RowText(0, 0) == "Resolution: Original" &&
                  store.RowText(1, 0) == "Aspect Ratio: Original" &&
                  store.RowText(2, 0) == "Frame Rate: Original",
              "and reads \"Resolution: Original\", \"Aspect Ratio: Original\", "
              "\"Frame Rate: Original\"");
        store.Cycle(0, 1);
        Check(store.settings().resolutionScale() == sk::kResolutionAuto &&
                  store.RowText(0, 0) == "Resolution: Auto",
              "Resolution steps Original -> Auto");
        store.Cycle(0, 1);
        Check(store.RowText(0, 0) == "Resolution: 352x416", "-> 352x416 (2x)");
        store.Cycle(0, -3);
        Check(store.settings().resolutionScale() == 6 && store.RowText(0, 0) == "Resolution: 1056x1248",
              "and wraps backwards from Original to 1056x1248 (6x)");
        store.Cycle(2, 1);
        Check(store.RowText(2, 0) == "Frame Rate: V-Sync", "Frame Rate steps Original -> V-Sync");
        store.Cycle(2, 2);
        Check(store.settings().frameRate() == 60 && store.RowText(2, 0) == "Frame Rate: 60",
              "-> 30 -> 60");
        store.Cycle(2, -4);
        Check(store.RowText(2, 0) == "Frame Rate: Unlimited",
              "and wraps backwards past Original to Unlimited");
        store.Cycle(1, 1);
        Check(store.settings().aspect() == sk::kAspectAuto && store.RowText(1, 0) == "Aspect Ratio: Auto",
              "Aspect Ratio steps Original -> Auto");
        store.Cycle(1, 3);
        Check(store.settings().aspect() == 1609 && store.RowText(1, 0) == "Aspect Ratio: 16:9",
              "-> 4:3 -> 16:10 -> 16:9");
        store.Cycle(1, 2);
        Check(store.settings().originalAspect(), "-> 21:9 -> and wraps to Original");

        Check(store.RowText(0, 2) == "Aufl\xF6sung: 1056x1248" &&
                  store.RowText(2, 3) == "Images/s: Illimit\xE9" &&
                  store.RowText(1, 2) == "Format: Original" &&
                  store.RowText(2, 9) == "Frame Rate: Unlimited",
              "labels follow the language, in Latin-1; an unknown language reads English");

        // Round trip, by value.
        const std::string path = "port/build/m119_pc_settings_smoke.cfg";
        std::remove(path.c_str());
        sk::PcSettingsStore writer(path);
        writer.Cycle(0, 3);  // 3x
        writer.Cycle(2, 7);  // 165
        writer.Cycle(1, 5);  // 21:9
        sk::PcSettingsStore reader(path);
        Check(reader.Load() && reader.settings().resolutionScale() == 3 &&
                  reader.settings().frameRate() == 165 && reader.settings().aspect() == 2109,
              "a change is written at once and reads back (3x, 165, 21:9)");
        {
            std::ofstream f(path);
            f << "framerate=144\nsomething_new=5\nresolution=13\naspect=1234\n";
        }
        sk::PcSettingsStore tolerant(path);
        Check(tolerant.Load() && tolerant.settings().frameRate() == 144 &&
                  tolerant.settings().originalResolution() && tolerant.settings().originalAspect(),
              "unknown keys and values that are not an option are ignored");
        sk::PcSettingsStore missing("port/build/m119_no_such_file.cfg");
        Check(!missing.Load() && missing.settings().originalFrameRate(),
              "a missing file is a first run");
        std::remove(path.c_str());

        sk::PcSettings autoRes;
        autoRes.resolutionIndex = 1;
        Check(sk::RenderScaleFor(autoRes, 624) == 3 && sk::RenderScaleFor(autoRes, 625) == 4 &&
                  sk::RenderScaleFor(autoRes, 1080) == 6 && sk::RenderScaleFor(autoRes, 5000) == 6 &&
                  sk::RenderScaleFor(autoRes, 100) == 1 && sk::RenderScaleFor(autoRes, 0) == 1,
              "Auto: the smallest multiple at least as tall as the picture, 1..6");
        sk::PcSettings fixed;
        fixed.resolutionIndex = 3;
        Check(sk::RenderScaleFor(fixed, 208) == 3 && sk::RenderScaleFor(sk::PcSettings{}, 2000) == 1,
              "a fixed scale ignores the window; Original is always 1");

        // M120: the view's width for each Aspect Ratio.
        auto widthAt = [](int aspect, int w, int h) {
            sk::PcSettings s;
            for (int i = 0; i < sk::kAspectOptionCount; ++i) {
                if (sk::kAspectRatios[i] == aspect) s.aspectIndex = i;
            }
            return sk::ViewWidthFor(s, w, h);
        };
        Check(widthAt(sk::kAspectOriginal, 1920, 1080) == 176 && widthAt(sk::kAspectOriginal, 0, 0) == 176,
              "Aspect Original is always the device's 176 columns");
        Check(widthAt(403, 0, 0) == 278 && widthAt(1610, 0, 0) == 332 && widthAt(1609, 0, 0) == 370 &&
                  widthAt(2109, 0, 0) == 486,
              "4:3 / 16:10 / 16:9 / 21:9 are 278 / 332 / 370 / 486 across at 208 tall");
        Check(widthAt(sk::kAspectAuto, 1920, 1080) == 370 && widthAt(sk::kAspectAuto, 2560, 1600) == 332 &&
                  widthAt(sk::kAspectAuto, 5120, 1440) == sk::kMaxViewWidth &&
                  widthAt(sk::kAspectAuto, 528, 624) == 176 && widthAt(sk::kAspectAuto, 0, 0) == 176,
              "Auto follows the window: 16:9, 16:10, capped at 21:9, never narrower than 176");
        bool evenSides = true;
        for (int w = 100; w < 4000; w += 7) {
            const int v = widthAt(sk::kAspectAuto, w, 1000);
            evenSides = evenSides && v >= 176 && v <= sk::kMaxViewWidth && (v - 176) % 2 == 0;
        }
        Check(evenSides, "and always leaves the same number of columns either side of the original 176");
    }

    // ---------------------------------------------------------------
    std::printf("\n== Part 2: the Options screen ==\n");
    // ---------------------------------------------------------------
    sk::StringTable strings;
    if (!strings.Load(root + "/stringtable.eng")) {
        std::printf("m119_pc_settings_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }
    {
        skInterpreter interpreter;
        sk::InputState input;
        auto call = [&](skiExecutable* obj, const char* name) {
            skRValue ret;
            skRValueArray args;
            skExecutableContext ctxt(&interpreter);
            try {
                obj->method(skString(name), args, ret, ctxt);
            } catch (skRuntimeException& e) {
                std::printf("   (RUNTIME ERROR in %s: %s)\n", name, e.toString().ptr());
            }
            return ret;
        };

        // The script's own screen, with no settings control: the baseline.
        std::vector<std::string> plain;
        std::vector<Menu::RowKind> plainKinds;
        {
            sk_b::MenuStack stack(root, interpreter, &strings);
            stack.SetInput(&input);
            stack.OpenMenu("Options");
            Menu* options = stack.currentMenu();
            Check(options != nullptr, "options.s opens");
            if (!options) return 1;
            options->EnsureValidSelection();
            for (const Menu::MenuRow& row : options->rows()) {
                plain.push_back(RowLabel(row, strings));
                plainKinds.push_back(row.kind);
            }
            bool anyPc = false;
            for (const Menu::MenuRow& row : options->rows()) anyPc = anyPc || row.pcSetting != 0;
            Check(!anyPc, "with no settings control the screen is exactly options.s's own");
        }

        sk::PcSettingsStore store;
        sk_b::MenuStack stack(root, interpreter, &strings);
        stack.SetInput(&input);
        stack.SetPcSettings(&store);
        stack.OpenMenu("Options");
        Menu* options = stack.currentMenu();
        if (!options) return 1;
        options->EnsureValidSelection();
        const auto& rows = options->rows();

        int resRow = -1, aspectRow = -1, rateRow = -1;
        for (size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].pcSetting == 1) resRow = static_cast<int>(i);
            if (rows[i].pcSetting == 2) aspectRow = static_cast<int>(i);
            if (rows[i].pcSetting == 3) rateRow = static_cast<int>(i);
        }
        // M123: the fourth row (Fog) is the only one added, and it is the
        // last row -- every row the script built keeps its index.
        int fogRow = -1;
        for (size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].pcSetting == 4) fogRow = static_cast<int>(i);
        }
        Check(rows.size() == plain.size() + 1 && fogRow == static_cast<int>(rows.size()) - 1,
              "one row is added, Fog, after the script's last: " + std::to_string(rows.size()) +
                  " rows");
        Check(resRow >= 1 && aspectRow == resRow + 1 && rateRow == resRow + 2 &&
                  rows[resRow - 1].kind == Menu::RowKind::Slider,
              "the three rows sit straight after the Music slider");
        // options.s: [spacer][Sound][Music][spacer][spacer][Language]... --
        // the leading spacer moves down, the sliders go up one.
        Check(resRow == 2 && plain[0] == " " && plainKinds[1] == Menu::RowKind::Slider &&
                  plainKinds[2] == Menu::RowKind::Slider && plain[3] == " " && plain[4] == " " &&
                  rows[0].kind == Menu::RowKind::Slider && rows[1].kind == Menu::RowKind::Slider &&
                  RowLabel(rows[0], strings) == plain[1] && RowLabel(rows[1], strings) == plain[2],
              "in the places of options.s's three spacers: the sliders up one, the gap under them");
        bool othersUnchanged = true;
        for (size_t i = static_cast<size_t>(rateRow) + 1; i < plain.size(); ++i) {  // M123: not Fog
            if (RowLabel(rows[i], strings) != plain[i] || rows[i].kind != plainKinds[i]) {
                othersUnchanged = false;
            }
        }
        Check(othersUnchanged, "every row from Select Language down keeps its index, kind and text");
        Check(rows[resRow].selectable && rows[resRow].kind == Menu::RowKind::MenuItem &&
                  RowLabel(rows[resRow], strings) == "Resolution: Original" &&
                  RowLabel(rows[aspectRow], strings) == "Aspect Ratio: Original" &&
                  RowLabel(rows[rateRow], strings) == "Frame Rate: Original",
              "all three are ordinary selectable menu items reading their values");

        // Navigation reaches them and the keys step them.
        call(options, "ClearMenu");  // start from a known selection below
        call(options, "Init");
        options->EnsurePcSettingRows();
        options->EnsureValidSelection();
        int guard = 0;
        while (options->selectedItem() != resRow + 1 && guard++ < 20) options->MoveSelection(1);
        Check(options->selectedItem() == resRow + 1, "Down reaches Resolution");
        options->ActivateSelected();
        Check(store.settings().resolutionScale() == sk::kResolutionAuto &&
                  RowLabel(options->rows()[resRow], strings) == "Resolution: Auto",
              "Enter steps it forward, and the row shows the new value");
        options->CycleSelectedCombo(-1);
        Check(store.settings().originalResolution(), "Left steps it back");
        options->MoveSelection(1);
        options->CycleSelectedCombo(1);
        options->CycleSelectedCombo(3);
        Check(store.settings().aspect() == 1609 &&
                  RowLabel(options->rows()[aspectRow], strings) == "Aspect Ratio: 16:9",
              "Down, Right x4: Aspect Ratio 16:9");
        options->MoveSelection(1);
        options->CycleSelectedCombo(1);
        options->CycleSelectedCombo(1);
        options->CycleSelectedCombo(1);
        Check(store.settings().frameRate() == 60 &&
                  RowLabel(options->rows()[rateRow], strings) == "Frame Rate: 60",
              "Down then Right x3: Frame Rate 60");

        // options.s's own handlers rebuild the screen with ClearMenu(); Init();.
        call(options, "SetGerman");
        options->EnsurePcSettingRows();
        Check(options->rows().size() == plain.size() + 1 &&  // M123: + Fog
                  options->rows()[resRow].pcSetting == 1 &&
                  RowLabel(options->rows().back(), strings) == "Nebel: Original" &&
                  RowLabel(options->rows()[resRow], strings) == "Aufl\xF6sung: Original" &&
                  RowLabel(options->rows()[aspectRow], strings) == "Format: 16:9" &&
                  RowLabel(options->rows()[rateRow], strings) == "Bildrate: 60",
              "SetGerman (ClearMenu; Init) brings them back, relabelled in German");
        call(options, "SetEnglish");
        options->EnsurePcSettingRows();
        Check(RowLabel(options->rows()[rateRow], strings) == "Frame Rate: 60",
              "and SetEnglish puts them back in English, value kept");

        // The other screens are untouched.
        stack.OpenMenu("MainMenu");
        Menu* mainMenu = stack.currentMenu();
        bool mainPc = false;
        if (mainMenu) {
            mainMenu->EnsureValidSelection();
            for (const Menu::MenuRow& row : mainMenu->rows()) mainPc = mainPc || row.pcSetting != 0;
        }
        Check(mainMenu && !mainPc, "no other screen gets the rows (MainMenu)");

        // Every label fits the 176px screen in the real menu face.
        const std::string fontPath = "port/assets/fonts/Ceurope.gdr";
        if (FileExists(fontPath) && sk::BitmapFont::LoadRealFont(fontPath, "LatinBold12")) {
            int widest = 0;
            std::string widestText;
            for (int lang = 0; lang < 6; ++lang) {
                for (int scale : sk::kResolutionScales) {
                    sk::PcSettings s;
                    for (int i = 0; i < sk::kResolutionOptionCount; ++i) {
                        if (sk::kResolutionScales[i] == scale) s.resolutionIndex = i;
                    }
                    sk::PcSettingsStore probe;
                    probe.Set(s);
                    const std::string t = probe.RowText(0, lang);
                    if (sk::BitmapFont::TextWidth(t) > widest) {
                        widest = sk::BitmapFont::TextWidth(t);
                        widestText = t;
                    }
                }
                for (int i = 0; i < sk::kAspectOptionCount; ++i) {
                    sk::PcSettings s;
                    s.aspectIndex = i;
                    sk::PcSettingsStore probe;
                    probe.Set(s);
                    const std::string t = probe.RowText(1, lang);
                    if (sk::BitmapFont::TextWidth(t) > widest) {
                        widest = sk::BitmapFont::TextWidth(t);
                        widestText = t;
                    }
                }
                for (int i = 0; i < sk::kFrameRateOptionCount; ++i) {
                    sk::PcSettings s;
                    s.frameRateIndex = i;
                    sk::PcSettingsStore probe;
                    probe.Set(s);
                    const std::string t = probe.RowText(2, lang);
                    if (sk::BitmapFont::TextWidth(t) > widest) {
                        widest = sk::BitmapFont::TextWidth(t);
                        widestText = t;
                    }
                }
            }
            Check(widest > 0 && widest <= sk::Backbuffer::kWidth - 8,
                  "every label in every language fits the screen (widest " +
                      std::to_string(widest) + "px)");
        } else {
            std::printf("  (Ceurope.gdr not present -- label width check skipped)\n");
        }
    }

    // ---------------------------------------------------------------
    std::printf("\n== Part 3: Resolution ==\n");
    // ---------------------------------------------------------------
    {
        sk::EntityTypeTable entityTypes;
        sk::ModelArchive models;
        sk::Zone zone;
        if (!entityTypes.Load(root) || !models.Load(root) || !zone.Load(root, "azra")) {
            std::printf("m119_pc_settings_smoke: FAILED to load azra\n");
            return 1;
        }
        sk::Camera camera;
        camera.x = static_cast<float>(zone.playerStartX);
        camera.y = static_cast<float>(zone.playerStartY);
        camera.z = zone.playerStartZ + sk::kEyeHeightOffset;
        camera.yaw = 0.0f;
        sk::PlacedEntity subject;
        subject.x = camera.x + 400.0f;
        subject.y = camera.y;
        subject.z = static_cast<float>(zone.playerStartZ);
        subject.modelArchiveIndex = 23;  // Bandit_Thug, as m84 uses
        subject.objectId = 7;
        const std::vector<sk::PlacedEntity> list{subject};

        sk::ZoneRenderer renderer;
        sk::Backbuffer native;
        renderer.Render(native, zone, camera, list, &models);
        sk::Backbuffer explicitNative(sk::Backbuffer::kWidth, sk::Backbuffer::kHeight);
        sk::ZoneRenderer again;
        again.Render(explicitNative, zone, camera, list, &models);
        bool same = true;
        for (int y = 0; y < sk::Backbuffer::kHeight && same; ++y) {
            for (int x = 0; x < sk::Backbuffer::kWidth; ++x) {
                if (native.Row(y)[x] != explicitNative.Row(y)[x]) {
                    same = false;
                    break;
                }
            }
        }
        Check(same, "a 176x208 target renders the same frame however it was made");
        // Twice, into one renderer: the kept depth plane must not leak.
        renderer.Render(explicitNative, zone, camera, list, &models);
        bool stable = true;
        for (int y = 0; y < sk::Backbuffer::kHeight && stable; ++y) {
            for (int x = 0; x < sk::Backbuffer::kWidth; ++x) {
                if (native.Row(y)[x] != explicitNative.Row(y)[x]) {
                    stable = false;
                    break;
                }
            }
        }
        Check(stable, "and the same again from a renderer that has drawn before (no carried state)");

        sk::HiResScene hiRes;
        sk::Backbuffer& scene = hiRes.BeginScene(2);
        Check(scene.width() == 352 && scene.height() == 416, "a 2x scene target is 352x416");
        sk::ZoneRenderer hiRenderer;
        hiRenderer.Render(scene, zone, camera, list, &models);
        // Compare the 2x scene against the native one: sampled at block
        // centres, nearly every pixel should agree (edges may differ by a
        // pixel of coverage, which is the point of a higher resolution).
        sk::Backbuffer sampled;
        hiRes.EndScene(sampled);
        if (std::getenv("SK_M119_DUMP")) {
            auto dump = [](const sk::Backbuffer& bb, const char* path) {
                std::FILE* f = std::fopen(path, "wb");
                if (!f) return;
                std::fprintf(f, "P6\n%d %d\n255\n", bb.width(), bb.height());
                for (int y = 0; y < bb.height(); ++y) {
                    for (int x = 0; x < bb.width(); ++x) {
                        const uint16_t c = bb.Row(y)[x];
                        const unsigned char rgb[3] = {
                            static_cast<unsigned char>(((c >> 11) & 0x1f) << 3),
                            static_cast<unsigned char>(((c >> 5) & 0x3f) << 2),
                            static_cast<unsigned char>((c & 0x1f) << 3)};
                        std::fwrite(rgb, 1, 3, f);
                    }
                }
                std::fclose(f);
            };
            dump(native, "port/build/m119_native.ppm");
            dump(scene, "port/build/m119_scene2x.ppm");
        }
        // "The same view" is a statement about structure, not texels: the
        // walls are dithered noise, and a 2x pixel centre sits a quarter of
        // a native pixel from the native one, so exact per-pixel agreement
        // is only ~70% on a correct render. Compare 8x8-pixel block
        // averages instead -- the same framing puts the same wall, floor,
        // sky and creature in each block.
        auto blockMean = [](const sk::Backbuffer& bb, int bx, int by, int size, int ch) {
            long sum = 0;
            for (int y = by * size; y < (by + 1) * size; ++y) {
                for (int x = bx * size; x < (bx + 1) * size; ++x) {
                    const uint16_t c = bb.Row(y)[x];
                    sum += ch == 0 ? ((c >> 11) & 0x1f) << 3
                                   : ch == 1 ? ((c >> 5) & 0x3f) << 2 : (c & 0x1f) << 3;
                }
            }
            return sum / (size * size);
        };
        int blocks = 0, close = 0;
        for (int by = 0; by < sk::Backbuffer::kHeight / 8; ++by) {
            for (int bx = 0; bx < sk::Backbuffer::kWidth / 8; ++bx) {
                bool ok = true;
                for (int ch = 0; ch < 3; ++ch) {
                    const long d = blockMean(native, bx, by, 8, ch) - blockMean(scene, bx, by, 16, ch);
                    if (d > 24 || d < -24) ok = false;
                }
                ++blocks;
                if (ok) ++close;
            }
        }
        Check(close * 100 >= blocks * 95,
              "the 2x scene is the same view: " + std::to_string(close) + "/" +
                  std::to_string(blocks) + " 8x8 blocks match the native frame's");
        int stamped = 0;
        for (int y = 0; y < sk::Backbuffer::kHeight; ++y) {
            for (int x = 0; x < sk::Backbuffer::kWidth; ++x) {
                if (hiRenderer.ObjectIdAt(x, y) == 7) ++stamped;
            }
        }
        uint8_t probed = 0;
        for (int y : {0x68, 0x7c, 0x90, 0xa4, 0xb8}) {
            probed = hiRenderer.ObjectIdAt(0x58, y);
            if (probed != 0) break;
        }
        Check(stamped > 200 && probed == 7,
              "the melee probe, in native coordinates, still finds the creature at 2x");

        // The bands are a schedule, not a different renderer: a 3x frame
        // drawn on the worker threads and the same frame in one pass match
        // pixel for pixel, object ids included.
        {
            sk::Backbuffer banded(sk::Backbuffer::kWidth * 3, sk::Backbuffer::kHeight * 3);
            sk::Backbuffer single(sk::Backbuffer::kWidth * 3, sk::Backbuffer::kHeight * 3);
            sk::ZoneRenderer threaded;
            sk::ZoneRenderer onePass;
            onePass.bandLimit = 1;
            threaded.Render(banded, zone, camera, list, &models);
            onePass.Render(single, zone, camera, list, &models);
            bool identical = true;
            for (int y = 0; y < banded.height() && identical; ++y) {
                for (int x = 0; x < banded.width(); ++x) {
                    if (banded.Row(y)[x] != single.Row(y)[x]) {
                        identical = false;
                        break;
                    }
                }
            }
            for (int y = 0; y < sk::Backbuffer::kHeight && identical; ++y) {
                for (int x = 0; x < sk::Backbuffer::kWidth; ++x) {
                    if (threaded.ObjectIdAt(x, y) != onePass.ObjectIdAt(x, y)) identical = false;
                }
            }
            Check(identical, "a 3x frame drawn in bands on the worker threads is the one-pass frame");
        }

        // M123: the rasterizers' row-span narrowing skips only pixels the
        // per-pixel test would reject -- so a frame drawn with it is the
        // frame drawn testing every pixel of every bounding box, at the
        // native size and at 3x, from eight headings and three pitches.
        {
            bool identical = true;
            int frames = 0;
            for (int scale : {1, 3}) {
                for (int turn = 0; turn < 8; ++turn) {
                    for (float pitch : {-0.5f, 0.0f, 0.4f}) {
                        sk::Camera view = camera;
                        view.yaw = camera.yaw + static_cast<float>(turn) * 0.785398f;
                        view.pitch = pitch;
                        sk::Backbuffer fast(sk::Backbuffer::kWidth * scale,
                                            sk::Backbuffer::kHeight * scale);
                        sk::Backbuffer full(sk::Backbuffer::kWidth * scale,
                                            sk::Backbuffer::kHeight * scale);
                        sk::ZoneRenderer r;
                        r.bandLimit = 1;
                        sk::SetRasterSpanNarrowingForTest(true);
                        r.Render(fast, zone, view, list, &models);
                        sk::SetRasterSpanNarrowingForTest(false);
                        r.Render(full, zone, view, list, &models);
                        sk::SetRasterSpanNarrowingForTest(true);
                        ++frames;
                        for (int y = 0; y < fast.height() && identical; ++y) {
                            for (int x = 0; x < fast.width(); ++x) {
                                if (fast.Row(y)[x] != full.Row(y)[x]) {
                                    identical = false;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            Check(identical && frames == 48,
                  "row-span narrowing draws every pixel exactly as testing the whole box does "
                  "(48 frames, 1x and 3x)");
        }

        // The composite: a 2D pixel over the scene replaces its block; an
        // untouched pixel shows the full-resolution scene.
        sk::Backbuffer ui = sampled;
        const uint16_t marker = 0xF81F;  // magenta, nowhere in the scene
        for (int x = 10; x < 20; ++x) ui.SetPixel(x, 5, marker);
        const sk::Backbuffer& out = hiRes.Compose(ui);
        bool blockOk = true;
        for (int x = 10; x < 20; ++x) {
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    if (out.Row(5 * 2 + dy)[x * 2 + dx] != marker) blockOk = false;
                }
            }
        }
        Check(out.width() == 352 && blockOk, "a 2D pixel becomes a 2x2 block of itself");
        bool sceneOk = true;
        for (int y = 100; y < 110; ++y) {
            for (int x = 0; x < 352; ++x) {
                if (out.Row(y)[x] != scene.Row(y)[x]) sceneOk = false;
            }
        }
        Check(sceneOk, "an untouched region is the 2x scene pixel for pixel");

        // ---- M120: Aspect Ratio ----
        //
        // A wider target is the same projection with more of it: the focal
        // length comes from the height and the view centres on the target,
        // so the middle 176 columns of a 370-wide frame are the native frame.
        std::printf("\n  -- M120: Aspect Ratio --\n");
        const int wideW = 370, side = (wideW - sk::Backbuffer::kWidth) / 2;
        sk::Backbuffer wide(wideW, sk::Backbuffer::kHeight);
        sk::ZoneRenderer wideRenderer;
        wideRenderer.Render(wide, zone, camera, list, &models);
        long samePx = 0, total = 0;
        for (int y = 0; y < sk::Backbuffer::kHeight; ++y) {
            for (int x = 0; x < sk::Backbuffer::kWidth; ++x) {
                ++total;
                if (wide.Row(y)[side + x] == native.Row(y)[x]) ++samePx;
            }
        }
        Check(samePx * 1000 >= total * 995,
              "16:9's middle 176 columns are the original frame (" + std::to_string(samePx) + "/" +
                  std::to_string(total) + " pixels) -- nothing is stretched");
        long sideDrawn = 0;
        const uint16_t cleared = wide.Row(0)[0];
        for (int y = 40; y < 160; ++y) {
            for (int x = 0; x < side; ++x) {
                if (wide.Row(y)[x] != cleared || wide.Row(y)[wideW - 1 - x] != cleared) ++sideDrawn;
            }
        }
        Check(sideDrawn > side * 100, "and the columns either side are world, not bars");
        uint8_t wideProbe = 0;
        for (int y : {0x68, 0x7c, 0x90, 0xa4, 0xb8}) {
            wideProbe = wideRenderer.ObjectIdAt(0x58, y);
            if (wideProbe != 0) break;
        }
        // At a wide 2x too, through the compositor's own target.
        sk::HiResScene wideHi;
        sk::Backbuffer& wideScene = wideHi.BeginScene(2, wideW);
        sk::ZoneRenderer wideHiRenderer;
        wideHiRenderer.Render(wideScene, zone, camera, list, &models);
        uint8_t wideHiProbe = 0;
        for (int y : {0x68, 0x7c, 0x90, 0xa4, 0xb8}) {
            wideHiProbe = wideHiRenderer.ObjectIdAt(0x58, y);
            if (wideHiProbe != 0) break;
        }
        Check(wideScene.width() == wideW * 2 && wideScene.height() == 416 && wideProbe == 7 &&
                  wideHiProbe == 7,
              "the melee probe finds the creature in a wide view, at 1x and 2x");
        sk::Backbuffer layer;
        wideHi.EndScene(layer);
        Check(layer.width() == wideW && layer.height() == sk::Backbuffer::kHeight,
              "the wide 2D layer is the view's width at the native height");
        for (int x = 0; x < 8; ++x) layer.SetPixel(x, 200, marker);  // the left edge
        const sk::Backbuffer& wideOut = wideHi.Compose(layer);
        bool edgeOk = wideOut.width() == wideW * 2;
        for (int x = 0; x < 16 && edgeOk; ++x) edgeOk = wideOut.Row(401)[x] == marker;
        edgeOk = edgeOk && wideOut.Row(100)[wideW * 2 - 1] == wideScene.Row(100)[wideW * 2 - 1];
        Check(edgeOk, "and composites across the whole width, edges included");
        sk::HiResScene wide1x;
        sk::Backbuffer& flat = wide1x.BeginScene(1, wideW);
        flat.Fill(0x1234);
        sk::Backbuffer flatLayer;
        wide1x.EndScene(flatLayer);
        Check(&wide1x.Compose(flatLayer) == &flatLayer && flatLayer.Row(10)[300] == 0x1234,
              "at the original Resolution a wide frame is its 2D layer, with no composite");

        sk::ViewLayout layout;
        layout.width = wideW;
        Check(!sk::ViewLayout{}.wide() && sk::ViewLayout{}.centreX() == 0 &&
                  sk::ViewLayout{}.rightX() == 0 && layout.wide() && layout.centreX() == 97 &&
                  layout.leftX() == 0 && layout.rightX() == 194,
              "ViewLayout: the device's frame is all zeroes; 16:9 centres at 97, right edge 194");
        sk::Backbuffer column;
        sk::CopyColumnOut(wide, layout.centreX(), column);
        bool columnOk = true;
        for (int y = 0; y < sk::Backbuffer::kHeight; ++y) {
            for (int x = 0; x < sk::Backbuffer::kWidth; ++x) {
                columnOk = columnOk && column.Row(y)[x] == wide.Row(y)[layout.centreX() + x];
            }
        }
        column.SetPixel(0, 0, marker);
        sk::CopyColumnIn(column, wide, layout.centreX());
        Check(columnOk && wide.Row(0)[97] == marker && wide.Row(0)[96] != marker,
              "the middle column copies out to a native buffer and back into place");
    }

    // ---------------------------------------------------------------
    std::printf("\n== Part 4: Frame Rate ==\n");
    // ---------------------------------------------------------------
    {
        sk::FrameInterpolator frames;
        sk::Camera a;
        a.x = 1000;
        a.y = 2000;
        a.z = 300;
        a.yaw = 3.0f;
        a.pitch = 0.1f;
        sk::PlacedEntity walker;
        walker.x = 100;
        walker.y = 100;
        walker.interpKey = 42;
        sk::PlacedEntity statue;
        statue.x = 500;
        std::vector<sk::PlacedEntity> ents{walker, statue};
        sk::SpriteBillboard bolt;
        bolt.x = 0;
        bolt.interpKey = 99;
        std::vector<sk::SpriteBillboard> bbs{bolt};

        sk::Camera cam;
        std::vector<sk::PlacedEntity> outE;
        std::vector<sk::SpriteBillboard> outB;
        Check(!frames.ready(), "nothing to draw before the first tick");
        frames.Push(a, ents, bbs);
        frames.Build(0.5f, cam, outE, outB);
        Check(Near(cam.x, 1000) && outE.size() == 2, "one tick in: the frame is that tick");

        sk::Camera b = a;
        b.x = 1100;
        b.yaw = -3.0f;  // across the +-pi seam: 0.28 rad the short way
        b.pitch = 0.3f;
        ents[0].x = 140;
        bbs[0].x = 60;
        sk::PlacedEntity newcomer;
        newcomer.x = 700;
        newcomer.interpKey = 77;
        ents.push_back(newcomer);
        frames.Push(b, ents, bbs);
        frames.Build(0.5f, cam, outE, outB);
        Check(Near(cam.x, 1050) && Near(cam.pitch, 0.2f), "halfway: the camera is halfway");
        const float expectYaw = 3.0f + 0.5f * (6.2831853f - 6.0f);
        Check(Near(std::remainder(cam.yaw - expectYaw, 6.2831853f), 0.0f, 1e-3f),
              "the heading turns the short way across +-pi");
        Check(Near(outE[0].x, 120) && Near(outE[1].x, 500) && Near(outE[2].x, 700),
              "a walker is halfway, a static prop and a newcomer are where they are");
        Check(Near(outB[0].x, 30), "a billboard is halfway too");
        frames.Build(0.0f, cam, outE, outB);
        Check(Near(cam.x, 1000) && Near(outE[0].x, 100), "alpha 0 is the previous tick");
        frames.Build(7.0f, cam, outE, outB);
        Check(Near(cam.x, 1100) && Near(outE[0].x, 140), "alpha is clamped to the latest tick");

        sk::Camera c = b;
        c.x = b.x + 5000;  // a teleport
        ents[0].x = 140 + 3000;
        frames.Push(c, ents, bbs);
        frames.Build(0.5f, cam, outE, outB);
        Check(Near(cam.x, c.x) && Near(outE[0].x, 3140), "a teleport is drawn where it landed");

        frames.Reset();
        Check(!frames.ready(), "Reset forgets both ticks");
        frames.Push(a, ents, bbs);
        frames.Build(0.5f, cam, outE, outB);
        Check(Near(cam.x, 1000), "and the next tick starts fresh");
    }

    // ---------------------------------------------------------------
    std::printf("\n== Part 5: the controller ==\n");
    // ---------------------------------------------------------------
    {
        using sk::ButtonSlot;
        using sk::PadControl;
        sk::InputState input;
        std::array<bool, static_cast<size_t>(ButtonSlot::kCount)> keyHeld{};
        sk::GamepadMapper mapper;
        auto apply = [&](const sk::PadFrame& f, bool gameplay) {
            mapper.Update(f, gameplay, input, [&](ButtonSlot slot, bool held) {
                input.SetButton(slot, held || keyHeld[static_cast<size_t>(slot)]);
            });
        };
        auto frameWith = [](std::initializer_list<PadControl> held) {
            sk::PadFrame f;
            f.connected = true;
            for (PadControl c : held) f.held[static_cast<size_t>(c)] = true;
            return f;
        };
        auto down = [&](ButtonSlot s) { return input.GetButton(s); };

        apply(frameWith({PadControl::LStickUp, PadControl::RStickLeft, PadControl::LStickRight}),
              true);
        Check(down(ButtonSlot::Up) && down(ButtonSlot::Left) && down(ButtonSlot::Key6),
              "in the 3D view: left stick walks and sidesteps, right stick turns");
        apply(frameWith({}), true);
        Check(!down(ButtonSlot::Up) && !down(ButtonSlot::Left) && !down(ButtonSlot::Key6),
              "and letting go lets go");

        apply(frameWith({PadControl::A, PadControl::Y, PadControl::RT, PadControl::LT,
                         PadControl::X, PadControl::Back, PadControl::RB, PadControl::LB}),
              true);
        Check(down(ButtonSlot::Key3) && down(ButtonSlot::Key1) && down(ButtonSlot::Key5) &&
                  down(ButtonSlot::Key7) && down(ButtonSlot::KeyHash) && down(ButtonSlot::Key9) &&
                  down(ButtonSlot::Key0) && down(ButtonSlot::KeyStar),
              "A use, Y jump, RT/LT the hands, X character, Back map, RB/LB the queues");
        apply(frameWith({}), true);

        apply(frameWith({PadControl::A}), false);
        Check(down(ButtonSlot::LeftSelectionKey) && !down(ButtonSlot::Key3),
              "in a menu A is confirm, not Use");
        apply(frameWith({}), false);
        apply(frameWith({PadControl::LStickDown, PadControl::B}), false);
        Check(down(ButtonSlot::Down) && down(ButtonSlot::RightSelectionKey),
              "the stick moves the selection, B is back");
        apply(frameWith({}), false);

        // A held button keeps the key it pressed when the screen changes.
        apply(frameWith({PadControl::A}), true);
        Check(down(ButtonSlot::Key3), "A held in the 3D view presses Use...");
        (void)input.ConsumeJustPressed(ButtonSlot::LeftSelectionKey);
        apply(frameWith({PadControl::A}), false);  // a conversation opened
        Check(down(ButtonSlot::Key3) && !input.ConsumeJustPressed(ButtonSlot::LeftSelectionKey),
              "...and still Use after a screen opens -- no stray confirm");
        apply(frameWith({}), false);
        Check(!down(ButtonSlot::Key3), "released, it releases Use");

        // Bindings: rebind Use to Key 6, and A follows.
        input.RedefineBinding(sk::Action::Use, ButtonSlot::Key6);
        apply(frameWith({PadControl::A}), true);
        Check(down(ButtonSlot::Key6), "A follows Use to wherever Customize Controls put it");
        apply(frameWith({}), true);
        input.RestoreDefaultBindings();

        // Keyboard and pad on one slot.
        keyHeld[static_cast<size_t>(ButtonSlot::Up)] = true;
        input.SetButton(ButtonSlot::Up, true);
        apply(frameWith({PadControl::DpadUp}), true);
        apply(frameWith({}), true);
        Check(down(ButtonSlot::Up), "the pad letting go does not release a key still held");
        apply(frameWith({PadControl::DpadUp}), true);
        keyHeld[static_cast<size_t>(ButtonSlot::Up)] = false;
        input.SetButton(ButtonSlot::Up, mapper.SlotHeld(ButtonSlot::Up));
        Check(down(ButtonSlot::Up), "nor the key a pad still held");

        sk::PadFrame gone;  // unplugged mid-press
        apply(gone, true);
        Check(!down(ButtonSlot::Up) && !mapper.SlotHeld(ButtonSlot::Up),
              "a pad that disappears lets go of everything");
    }

    std::printf("\nm119_pc_settings_smoke: %d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
