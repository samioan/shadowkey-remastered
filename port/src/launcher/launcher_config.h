#pragma once

// M113: `launcher.cfg` -- where the launcher remembers what the user
// plugged in.
//
// Deliberately *not* `dragonstar.set`. That file is the engine's own
// settings file and its format is reproduced exactly as the real game
// writes it, down to the writer's hardcoded 16 (assets/game_config.h);
// putting launcher state in it would break a faithful format for the sake
// of convenience. This is a separate file with a plain `key=value` layout,
// because nothing about it needs to match anything the N-Gage did.
//
//     gameData=data
//     font=data/Ceurope.gdr
//     scale=3
//
// Paths are stored relative to the install root when they live inside it
// (which, after the setup flow copies the game data in, they always do), so
// the whole folder can be moved or renamed without breaking the install.
// An absolute path is still read back correctly if one ever gets written.
// Unknown keys are ignored rather than dropped-on-rewrite being a surprise;
// a missing file yields the defaults, which is the first-run state.

#include <string>

namespace sk {
namespace launcher {

struct LauncherConfig {
    // Directory holding the game's own files (stringtable.eng, mainmenu.s,
    // global.spr, ...) -- passed to shadowkey_port.exe as argv[1]. Empty
    // until the user has chosen one.
    std::string gameData;
    // The Nokia ROM font, passed as argv[2]. Optional: the game falls back
    // to placeholder glyphs without it (see main.cpp's LoadRealFont note).
    std::string font;
    // Integer window scale over the native 176x208. The game hardcoded 3
    // before this milestone, which stays the default.
    int scale = 3;

    static constexpr int kMinScale = 1;
    static constexpr int kMaxScale = 8;

    static constexpr const char* kFileName = "launcher.cfg";
};

// Both return false if the file could not be read/written at all. A file
// that exists but is empty or entirely unrecognised is not an error -- it
// reads back as defaults, same as no file.
bool LoadLauncherConfig(const std::string& path, LauncherConfig& out);
bool SaveLauncherConfig(const std::string& path, const LauncherConfig& config);

// Joins `root` and `relative` unless `relative` is already absolute, in
// which case it is returned unchanged. The one place the "relative to the
// install root" convention above is actually applied.
std::string ResolveAgainst(const std::string& root, const std::string& relative);

// The inverse, used before saving: makes `path` relative to `root` when it
// is inside it, and returns it unchanged when it is not.
std::string RelativeToIfInside(const std::string& root, const std::string& path);

}  // namespace launcher
}  // namespace sk
