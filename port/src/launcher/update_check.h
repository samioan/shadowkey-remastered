#pragma once

// M115: finding out whether a newer build exists.
//
// Two halves, deliberately separated: the parsing and comparing are pure
// functions over strings, so the smoke test can exercise every branch
// without a network; only FetchLatestRelease() talks to GitHub.
//
// **Not `/releases/latest`.** That endpoint excludes pre-releases, and this
// project's releases are pre-releases while the major version is 0 -- asked
// for the current build it returns **404**, so an updater written against
// it would report "up to date" forever and nobody would notice until the
// first 1.0. The list endpoint with `per_page=1` returns the newest
// published release whatever its flags, which is what we actually mean.
// (Drafts never appear unauthenticated, so they need no filtering.)

#include <string>

namespace sk {
namespace launcher {

struct ReleaseInfo {
    std::string tag;          // "v0.114.0"
    std::string version;      // "0.114.0" -- the tag with any leading v removed
    std::string assetName;    // "ShadowkeyRemastered-v0.114.0-win64.zip"
    std::string downloadUrl;  // the asset's browser_download_url
    bool prerelease = false;

    bool valid() const { return !version.empty() && !downloadUrl.empty(); }
};

// Compares two version strings the way a person would: numerically
// component by component, so 0.115.0 beats 0.99.0 (which a string compare
// gets backwards). A `-suffix` marks a pre-release and sorts *before* the
// same numbers without one, so 1.0.0 beats 1.0.0-beta1.
//
// Returns <0 if `left` is older, 0 if equal, >0 if newer.
int CompareVersions(const std::string& left, const std::string& right);

// Pulls the fields we need out of the JSON array the releases endpoint
// returns. Hand-rolled rather than vendoring a JSON library for four
// fields; it reads only the first release object in the array, which is
// why the request asks for exactly one. Returns false if the payload has
// no usable release (an empty array, or one with no .zip asset).
bool ParseReleaseList(const std::string& json, ReleaseInfo& out);

// The GET. Returns false and fills `error` with something a user can read
// on any failure -- no network, DNS, a rate-limit, a non-200. Blocking, so
// call it off the UI thread.
bool FetchLatestRelease(const std::string& owner, const std::string& repo, ReleaseInfo& out,
                        std::string& error);

// Downloads `url` to `destPath`. `onProgress` may be null; when the server
// sends a content-length it is called with (bytesSoFar, totalBytes),
// otherwise totalBytes is 0. Returns false and fills `error` on failure,
// leaving no partial file behind.
bool DownloadFile(const std::string& url, const std::string& destPath,
                  void (*onProgress)(unsigned long long, unsigned long long, void*),
                  void* progressContext, std::string& error);

}  // namespace launcher
}  // namespace sk
