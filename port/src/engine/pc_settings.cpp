#include "engine/pc_settings.h"

#include <cstdio>
#include <cstring>
#include <filesystem>

#include "graphics/backbuffer.h"

namespace sk {

namespace {

// Per-language labels, indexed like the engine's language (0 eng, 1 spa,
// 2 ger, 3 fre, 4 ita, 5 euk). The game's own string table has no text for
// settings it never had, so these are the port's -- written in Latin-1 so
// they draw in the same LatinBold12 face as every row around them.
struct Labels {
    const char* resolution;
    const char* frameRate;
    const char* original;
    const char* unlimited;
    const char* aspect;  // M120
};
constexpr Labels kLabels[] = {
    {"Resolution", "Frame Rate", "Original", "Unlimited", "Aspect Ratio"},
    {"Resoluci\xf3n", "Fotogramas", "Original", "Ilimitado", "Formato"},
    {"Aufl\xf6sung", "Bildrate", "Original", "Unbegrenzt", "Format"},
    {"R\xe9solution", "Images/s", "Originale", "Illimit\xe9", "Format"},
    {"Risoluzione", "Fotogrammi", "Originale", "Illimitati", "Formato"},
    {"Resolution", "Frame Rate", "Original", "Unlimited", "Aspect Ratio"},
};

const Labels& LabelsFor(int language) {
    const int n = static_cast<int>(sizeof(kLabels) / sizeof(kLabels[0]));
    return kLabels[(language >= 0 && language < n) ? language : 0];
}

int Wrap(int value, int count) { return ((value % count) + count) % count; }

}  // namespace

int RenderScaleFor(const PcSettings& settings, int viewHeight) {
    const int scale = settings.resolutionScale();
    if (scale != kResolutionAuto) return scale;
    if (viewHeight <= 0) return 1;
    const int fit = (viewHeight + Backbuffer::kHeight - 1) / Backbuffer::kHeight;
    return fit < 1 ? 1 : (fit > kMaxRenderScale ? kMaxRenderScale : fit);
}

int ViewWidthFor(const PcSettings& settings, int clientWidth, int clientHeight) {
    const int aspect = settings.aspect();
    if (aspect == kAspectOriginal) return Backbuffer::kWidth;
    double ratio = 0.0;
    if (aspect == kAspectAuto) {
        if (clientWidth <= 0 || clientHeight <= 0) return Backbuffer::kWidth;
        ratio = static_cast<double>(clientWidth) / clientHeight;
    } else {
        ratio = static_cast<double>(aspect / 100) / (aspect % 100);
    }
    const double wanted = Backbuffer::kHeight * ratio;
    if (wanted <= Backbuffer::kWidth) return Backbuffer::kWidth;
    // The extra width, split evenly between the two sides.
    int side = static_cast<int>((wanted - Backbuffer::kWidth) / 2.0 + 0.5);
    const int maxSide = (kMaxViewWidth - Backbuffer::kWidth) / 2;
    if (side > maxSide) side = maxSide;
    return Backbuffer::kWidth + 2 * side;
}

std::string AspectValueText(int aspect, int language) {
    if (aspect == kAspectOriginal) return LabelsFor(language).original;
    if (aspect == kAspectAuto) return "Auto";
    return std::to_string(aspect / 100) + ":" + std::to_string(aspect % 100);
}

std::string ResolutionValueText(int scale, int language) {
    if (scale == 1) return LabelsFor(language).original;
    if (scale == kResolutionAuto) return "Auto";
    return std::to_string(Backbuffer::kWidth * scale) + "x" +
           std::to_string(Backbuffer::kHeight * scale);
}

std::string FrameRateValueText(int fps, int language) {
    if (fps == kFrameRateOriginal) return LabelsFor(language).original;
    if (fps == kFrameRateVSync) return "V-Sync";
    if (fps == kFrameRateUnlimited) return LabelsFor(language).unlimited;
    return std::to_string(fps);
}

PcSettingsStore::PcSettingsStore(std::string path) : path_(std::move(path)) {}

void PcSettingsStore::Set(const PcSettings& settings) {
    settings_ = settings;
    settings_.resolutionIndex = Wrap(settings_.resolutionIndex, kResolutionOptionCount);
    settings_.frameRateIndex = Wrap(settings_.frameRateIndex, kFrameRateOptionCount);
    settings_.aspectIndex = Wrap(settings_.aspectIndex, kAspectOptionCount);
}

bool PcSettingsStore::Load() {
    if (path_.empty()) return false;
    FILE* f = nullptr;
    if (fopen_s(&f, path_.c_str(), "r") != 0 || !f) return false;
    char line[128];
    while (std::fgets(line, sizeof(line), f)) {
        char key[64] = {};
        int value = 0;
        if (sscanf_s(line, " %63[^= ] = %d", key, static_cast<unsigned>(sizeof(key)), &value) != 2) {
            continue;
        }
        // Stored as the option's *value* (a scale, a rate), not its index,
        // so reordering or extending the option lists never reinterprets an
        // existing file.
        if (std::strcmp(key, "resolution") == 0) {
            for (int i = 0; i < kResolutionOptionCount; ++i) {
                if (kResolutionScales[i] == value) settings_.resolutionIndex = i;
            }
        } else if (std::strcmp(key, "framerate") == 0) {
            for (int i = 0; i < kFrameRateOptionCount; ++i) {
                if (kFrameRates[i] == value) settings_.frameRateIndex = i;
            }
        } else if (std::strcmp(key, "aspect") == 0) {  // M120
            for (int i = 0; i < kAspectOptionCount; ++i) {
                if (kAspectRatios[i] == value) settings_.aspectIndex = i;
            }
        }
    }
    std::fclose(f);
    return true;
}

bool PcSettingsStore::Save() const {
    if (path_.empty()) return false;
    std::error_code ec;
    const std::filesystem::path parent = std::filesystem::path(path_).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    FILE* f = nullptr;
    if (fopen_s(&f, path_.c_str(), "w") != 0 || !f) return false;
    std::fprintf(f,
                 "# Shadowkey PC port display settings (the Options screen's PC rows).\n"
                 "# resolution: 1 = original 176x208, 0 = auto (follow the window), 2..6 = "
                 "that multiple\n"
                 "# framerate: 25 = original, -1 = v-sync, 0 = unlimited, else frames a second\n"
                 "# aspect: 0 = original, -1 = auto (fill the window), else w*100+h "
                 "(1609 = 16:9)\n"
                 "resolution=%d\nframerate=%d\naspect=%d\n",
                 settings_.resolutionScale(), settings_.frameRate(), settings_.aspect());
    std::fclose(f);
    return true;
}

std::string PcSettingsStore::RowText(int row, int language) const {
    const Labels& labels = LabelsFor(language);
    if (row == kRowResolution) {
        return std::string(labels.resolution) + ": " +
               ResolutionValueText(settings_.resolutionScale(), language);
    }
    if (row == kRowFrameRate) {
        return std::string(labels.frameRate) + ": " +
               FrameRateValueText(settings_.frameRate(), language);
    }
    if (row == kRowAspect) {
        return std::string(labels.aspect) + ": " + AspectValueText(settings_.aspect(), language);
    }
    return std::string();
}

void PcSettingsStore::Cycle(int row, int delta) {
    if (row == kRowResolution) {
        settings_.resolutionIndex = Wrap(settings_.resolutionIndex + delta, kResolutionOptionCount);
    } else if (row == kRowFrameRate) {
        settings_.frameRateIndex = Wrap(settings_.frameRateIndex + delta, kFrameRateOptionCount);
    } else if (row == kRowAspect) {
        settings_.aspectIndex = Wrap(settings_.aspectIndex + delta, kAspectOptionCount);
    } else {
        return;
    }
    Save();
}

}  // namespace sk
