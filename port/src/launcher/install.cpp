#include "launcher/install.h"

#include <cstdlib>
#include <filesystem>
#include <vector>

#include "assets/gdr_font.h"

namespace sk {
namespace launcher {

namespace {

namespace fs = std::filesystem;

// All four have to be present. Any one of them alone is a weak signal --
// `global.spr` in particular is a generic enough name that some unrelated
// folder could carry it -- but a directory holding the string table, the
// main menu script, the sprite archive and the model index together is the
// game and nothing else.
const char* const kMarkerFiles[] = {
    "stringtable.eng",
    "mainmenu.s",
    "global.spr",
    "models.idx",
};

// How far below the picked folder to look. A dump unzipped at its natural
// depth puts the game at `<picked>/system/apps/6r51`, which is exactly
// three; going deeper would start scanning the game's own subfolders
// (armor/, etc.) for no reason.
constexpr int kMaxSearchDepth = 3;

std::string EnvOrEmpty(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

}  // namespace

bool IsGameDataRoot(const std::string& directory) {
    if (directory.empty()) return false;
    std::error_code error;
    if (!fs::is_directory(fs::path(directory), error) || error) return false;
    for (const char* marker : kMarkerFiles) {
        if (!fs::is_regular_file(fs::path(directory) / marker, error) || error) return false;
    }
    return true;
}

std::string FindGameDataRoot(const std::string& picked) {
    if (picked.empty()) return std::string();
    if (IsGameDataRoot(picked)) return picked;

    // Breadth-first, so the shallowest match wins and a dump that somehow
    // contains two copies resolves to the outer one predictably.
    std::vector<fs::path> current{fs::path(picked)};
    for (int depth = 0; depth < kMaxSearchDepth && !current.empty(); ++depth) {
        std::vector<fs::path> next;
        for (const fs::path& directory : current) {
            std::error_code error;
            fs::directory_iterator it(directory, error);
            if (error) continue;
            for (const fs::directory_entry& entry : it) {
                if (!entry.is_directory(error) || error) continue;
                if (IsGameDataRoot(entry.path().string())) return entry.path().string();
                next.push_back(entry.path());
            }
        }
        current = std::move(next);
    }
    return std::string();
}

bool IsUsableFont(const std::string& path) {
    if (path.empty()) return false;
    std::error_code error;
    if (!fs::is_regular_file(fs::path(path), error) || error) return false;
    // The exact call main.cpp makes for the legend font. Deliberately not a
    // filename or extension check: a `.gdr` from the wrong device, or a
    // truncated download, fails here instead of at the first frame of text.
    GdrFont font;
    return font.Load(path, "LatinBold12");
}

std::string FindExistingFont(const std::string& installRoot) {
    std::vector<fs::path> candidates;
    // A previous run of this launcher, or a hand-assembled install.
    if (!installRoot.empty()) {
        candidates.push_back(fs::path(installRoot) / "data" / "Ceurope.gdr");
        candidates.push_back(fs::path(installRoot) / "Ceurope.gdr");
    }
    // EKA2L1 keeps an extracted ROM under its own data directory; the
    // device folder name varies by dump, so each device directory is
    // probed rather than guessed at. This is a convenience, so it stays
    // shallow and cheap -- no sweep of the user's profile.
    for (const char* variable : {"APPDATA", "LOCALAPPDATA", "USERPROFILE"}) {
        const std::string base = EnvOrEmpty(variable);
        if (base.empty()) continue;
        const fs::path drives = fs::path(base) / "EKA2L1" / "data" / "drives" / "z";
        std::error_code error;
        fs::directory_iterator it(drives, error);
        if (error) continue;
        for (const fs::directory_entry& device : it) {
            if (!device.is_directory(error) || error) continue;
            candidates.push_back(device.path() / "System" / "Fonts" / "Ceurope.gdr");
            candidates.push_back(device.path() / "Resource" / "Fonts" / "Ceurope.gdr");
        }
    }
    for (const fs::path& candidate : candidates) {
        if (IsUsableFont(candidate.string())) return candidate.string();
    }
    return std::string();
}

bool CopyGameData(const std::string& from, const std::string& to, std::string& error) {
    error.clear();
    std::error_code code;
    // Copying a folder onto itself would be a slow way to corrupt an
    // install; equivalent() is the reliable check (it sees through a
    // different spelling of the same path, and through junctions).
    if (fs::exists(fs::path(to), code) && fs::equivalent(fs::path(from), fs::path(to), code) &&
        !code) {
        return true;  // already installed from exactly here -- nothing to do
    }
    fs::create_directories(fs::path(to), code);
    if (code) {
        error = "could not create " + to + ": " + code.message();
        return false;
    }
    fs::copy(fs::path(from), fs::path(to),
             fs::copy_options::recursive | fs::copy_options::overwrite_existing, code);
    if (code) {
        error = "could not copy the game files: " + code.message();
        return false;
    }
    if (!IsGameDataRoot(to)) {
        error = "the copy finished but " + to + " does not look like the game -- "
                "the source may have changed while it was being copied";
        return false;
    }
    return true;
}

bool CopyOneFile(const std::string& from, const std::string& to, std::string& error) {
    error.clear();
    std::error_code code;
    if (fs::exists(fs::path(to), code) && fs::equivalent(fs::path(from), fs::path(to), code) &&
        !code) {
        return true;
    }
    fs::create_directories(fs::path(to).parent_path(), code);
    if (code) {
        error = "could not create " + fs::path(to).parent_path().string() + ": " + code.message();
        return false;
    }
    fs::copy_file(fs::path(from), fs::path(to), fs::copy_options::overwrite_existing, code);
    if (code) {
        error = "could not copy " + from + ": " + code.message();
        return false;
    }
    return true;
}

}  // namespace launcher
}  // namespace sk
