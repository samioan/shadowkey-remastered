// M113 smoke test -- the launcher's non-visual half.
//
// The launcher is a GUI binary, so the window itself is verified by hand
// (see docs/PORT_ROADMAP.md). What can be pinned down here is everything
// underneath it: the artwork blob actually inflates to the pixels it claims
// to, `launcher.cfg` survives a round trip, and the game-data discovery
// accepts the shapes people really point a file dialog at while rejecting
// folders that merely look plausible.
//
// Headless and window-free, so it runs in the same suite as every other
// smoke test -- and, unlike most of them, most of it runs with no game data
// at all, which is what lets CI exercise it.

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "launcher/banner.h"
#include "launcher/install.h"
#include "launcher/launcher_config.h"

namespace {

namespace fs = std::filesystem;
using namespace sk::launcher;

int g_Checks = 0;
int g_Failed = 0;

void Check(bool ok, const std::string& what) {
    ++g_Checks;
    if (!ok) ++g_Failed;
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what.c_str());
}

std::vector<unsigned char> ReadFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(file)),
                                      std::istreambuf_iterator<char>());
}

// A scratch directory of our own under TEMP, same convention as
// m91_ingame_menu_smoke.cpp -- never the repo, never the install.
fs::path ScratchDirectory() {
    const char* temp = std::getenv("TEMP");
    fs::path base = temp ? fs::path(temp) : fs::path(".");
    fs::path directory = base / "sk_m113_launcher_smoke";
    std::error_code error;
    fs::remove_all(directory, error);
    fs::create_directories(directory, error);
    return directory;
}

}  // namespace

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";
    const fs::path scratch = ScratchDirectory();

    // -- 1. the banner artwork --
    //
    // The image is committed (port/src/launcher/assets/banner.jpg) and the
    // launcher embeds it as RCDATA; decoding it from the file here is the
    // same code path LoadEmbeddedBanner() takes, minus the resource lookup.
    // DecodeBanner goes through WIC, so it needs COM on this thread.
    std::printf("\n-- 1. the banner artwork (WIC -> DecodeBanner) --\n");
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Check(SUCCEEDED(comResult), "COM initialises for WIC");

    const std::vector<unsigned char> blob = ReadFile("port/src/launcher/assets/banner.jpg");
    Check(!blob.empty(), "port/src/launcher/assets/banner.jpg is present");
    if (!blob.empty()) {
        Check(blob.size() > 2 && blob[0] == 0xff && blob[1] == 0xd8,
              "starts with a JPEG SOI marker");

        Banner banner;
        Check(DecodeBanner(blob.data(), blob.size(), banner), "DecodeBanner succeeds");
        Check(banner.valid(), "the decoded banner is self-consistent");
        Check(banner.width == 1600 && banner.height == 1195,
              "dimensions are the committed artwork's 1600x1195");
        Check(banner.pixels.size() ==
                  static_cast<size_t>(banner.width) * static_cast<size_t>(banner.height),
              "one pixel per width*height, no row padding");

        // The near-black purple corner. A wrong channel order still decodes
        // "successfully", so checking a known colour component-by-component
        // is what would actually catch it -- and this corner is far enough
        // from anything bright that JPEG ringing cannot move it much.
        if (banner.valid()) {
            const unsigned char* pixel =
                reinterpret_cast<const unsigned char*>(&banner.pixels[5 * banner.width + 5]);
            const int blue = pixel[0], green = pixel[1], red = pixel[2];
            Check(blue > red && red > green && blue < 60 && green < 30,
                  "pixel (5,5) is the art's dark purple in B,G,R order");
        }

        // A corrupt image must leave the launcher drawing a flat background
        // rather than a screenful of uninitialised memory. WIC's JPEG
        // decoder grumbles to stderr on its own account while rejecting
        // these ("Unsupported marker type ..."); that output is the checks
        // below working, not failing.
        std::printf("  (WIC may print its own complaints below -- that is these checks "
                    "feeding it deliberately broken data)\n");
        Banner rejected;
        Check(!DecodeBanner(blob.data(), 4, rejected), "a 4-byte fragment is rejected");
        std::vector<unsigned char> corrupt = blob;
        corrupt[0] = 'X';  // no longer any format WIC recognises
        Check(!DecodeBanner(corrupt.data(), corrupt.size(), rejected),
              "a file with no recognisable header is rejected");
        Check(!DecodeBanner(nullptr, 1024, rejected), "a null pointer is rejected");
        Check(!DecodeBanner(blob.data(), 0, rejected), "a zero length is rejected");
    }

    // -- 2. launcher.cfg --
    std::printf("\n-- 2. launcher.cfg round trip --\n");
    {
        const std::string path = (scratch / "launcher.cfg").string();
        LauncherConfig written;
        written.gameData = "data";
        written.font = "data/Ceurope.gdr";
        written.scale = 4;
        Check(SaveLauncherConfig(path, written), "SaveLauncherConfig writes the file");

        LauncherConfig read;
        Check(LoadLauncherConfig(path, read), "LoadLauncherConfig reads it back");
        Check(read.gameData == written.gameData, "gameData survives the round trip");
        Check(read.font == written.font, "font survives the round trip");
        Check(read.scale == written.scale, "scale survives the round trip");

        LauncherConfig missing;
        Check(!LoadLauncherConfig((scratch / "nope.cfg").string(), missing),
              "a missing file reports failure");
        Check(missing.scale == 3 && missing.gameData.empty(),
              "...and still yields the first-run defaults");

        // Hand-edited files are a thing people do, so neither an unknown
        // key nor a nonsense value may take the launcher down with it.
        const std::string odd = (scratch / "odd.cfg").string();
        {
            std::ofstream file(odd);
            file << "# a comment\n\nsomeFutureKey=1\ngameData = spaced \nscale=99\n";
        }
        LauncherConfig loaded;
        Check(LoadLauncherConfig(odd, loaded), "a file of unknown keys still loads");
        Check(loaded.gameData == "spaced", "whitespace around a value is trimmed");
        Check(loaded.scale == 3, "an out-of-range scale keeps the default");
    }

    // -- 3. paths relative to the install root --
    std::printf("\n-- 3. install-relative paths --\n");
    {
        const std::string root = scratch.string();
        Check(ResolveAgainst(root, "data") == (scratch / "data").string(),
              "a relative path resolves against the install root");
        Check(ResolveAgainst(root, "").empty(), "an unset path stays unset");
        const std::string absolute = (scratch / "elsewhere" / "x.gdr").string();
        Check(ResolveAgainst(root, absolute) == absolute, "an absolute path is left alone");

        Check(RelativeToIfInside(root, (scratch / "data").string()) == "data",
              "a path inside the install root is stored relative");
        // An install must not record a path that climbs out of itself --
        // that is what makes the whole folder movable.
        const std::string outside = (scratch.parent_path() / "somewhere_else").string();
        Check(RelativeToIfInside(root, outside) == outside,
              "a path outside it is stored absolute");
    }

    // -- 4. finding the game --
    std::printf("\n-- 4. game-data discovery --\n");
    {
        Check(!IsGameDataRoot(scratch.string()), "an empty folder is not the game");
        Check(!IsGameDataRoot(""), "an empty path is not the game");
        Check(FindGameDataRoot(scratch.string()).empty(),
              "FindGameDataRoot gives up on a folder with nothing in it");

        // A folder holding one marker file but not the rest must not pass:
        // the whole point of checking four is that any one of them alone is
        // a coincidence waiting to happen.
        const fs::path decoy = scratch / "decoy";
        std::error_code error;
        fs::create_directories(decoy, error);
        std::ofstream(decoy / "global.spr") << "not really";
        Check(!IsGameDataRoot(decoy.string()), "one marker file alone is not enough");

        if (fs::is_directory(scriptRoot, error)) {
            Check(IsGameDataRoot(scriptRoot), "the real 6r51 folder is recognised");
            Check(FindGameDataRoot(scriptRoot) == std::string(scriptRoot),
                  "...and pointing straight at it returns it unchanged");

            // The case that actually matters: nobody selects
            // system/apps/6r51, they select the folder they unzipped.
            const fs::path dumpRoot =
                fs::path(scriptRoot).parent_path().parent_path().parent_path();
            const std::string found = FindGameDataRoot(dumpRoot.string());
            Check(!found.empty() && fs::equivalent(found, scriptRoot, error),
                  "the dump root resolves down to system/apps/6r51");
        } else {
            std::printf("  [skip] no game data at %s -- discovery checks against the real "
                        "dump skipped\n",
                        scriptRoot);
        }
    }

    // -- 5. the font --
    std::printf("\n-- 5. font validation --\n");
    {
        Check(!IsUsableFont(""), "an empty path is not a font");
        Check(!IsUsableFont(scratch.string()), "a directory is not a font");
        const fs::path notAFont = scratch / "notafont.gdr";
        std::ofstream(notAFont) << "this is not a Symbian font store";
        // Named .gdr on purpose: the check has to be a real parse, or a
        // renamed file sails through the launcher and fails at first draw.
        Check(!IsUsableFont(notAFont.string()),
              "a non-font named .gdr is rejected (the check parses, not guesses)");

        const std::string realFont = "port/assets/fonts/Ceurope.gdr";
        std::error_code error;
        if (fs::is_regular_file(realFont, error)) {
            Check(IsUsableFont(realFont), "the real Ceurope.gdr is accepted");
        } else {
            // Expected on any machine but a developer's: the font is Nokia
            // firmware and .gitignore keeps it out of the repo.
            std::printf("  [skip] %s not present (it is never committed) -- "
                        "positive font check skipped\n",
                        realFont.c_str());
        }
    }

    // -- 6. copying an install in --
    //
    // Against a synthetic tree rather than the real 22 MB dump: what is
    // being checked is that a nested folder comes across, that the result
    // validates, and that the guards hold -- none of which needs the real
    // thing, and all of which should run in milliseconds.
    std::printf("\n-- 6. copy-in --\n");
    {
        const fs::path source = scratch / "fake_dump";
        std::error_code error;
        fs::create_directories(source / "armor", error);
        for (const char* marker :
             {"stringtable.eng", "mainmenu.s", "global.spr", "models.idx"}) {
            std::ofstream(source / marker) << marker;
        }
        std::ofstream(source / "armor" / "leather.spr") << "nested";
        Check(IsGameDataRoot(source.string()), "the synthetic tree looks like the game");

        const fs::path destination = scratch / "install" / "data";
        std::string copyError;
        Check(CopyGameData(source.string(), destination.string(), copyError),
              "CopyGameData succeeds (" + copyError + ")");
        Check(IsGameDataRoot(destination.string()), "the copy validates as the game");
        Check(fs::is_regular_file(destination / "armor" / "leather.spr", error) && !error,
              "nested files come across, not just the top level");

        // Re-running setup on an install that is already set up is a thing
        // people do; it must overwrite cleanly rather than fail.
        Check(CopyGameData(source.string(), destination.string(), copyError),
              "copying again over an existing install is fine");
        // And pointing it at its own data folder must not shred the install.
        Check(CopyGameData(destination.string(), destination.string(), copyError),
              "copying a folder onto itself is a no-op, not a corruption");
        Check(IsGameDataRoot(destination.string()), "...and the install still validates");

        std::string fileError;
        const fs::path fontDestination = scratch / "install" / "data" / "Ceurope.gdr";
        Check(CopyOneFile((source / "models.idx").string(), fontDestination.string(), fileError),
              "CopyOneFile creates missing parent directories");
        Check(!CopyOneFile((scratch / "does_not_exist").string(), fontDestination.string(),
                           fileError) && !fileError.empty(),
              "a missing source reports an error rather than failing silently");
    }

    std::error_code cleanup;
    fs::remove_all(scratch, cleanup);
    if (SUCCEEDED(comResult)) CoUninitialize();

    std::printf("\nm113_launcher_smoke: %d/%d checks passed -- %s\n", g_Checks - g_Failed,
                g_Checks, g_Failed == 0 ? "OK" : "FAILED");
    return g_Failed == 0 ? 0 : 1;
}
