#pragma once

// M113: turning "the folder the user pointed at" into a working install.
//
// The launcher asks for two things this project is not allowed to ship: the
// game's own files (an N-Gage dump) and `Ceurope.gdr` (Nokia device
// firmware). Both are the same two inputs `shadowkey_port.exe` has always
// taken as argv[1] and argv[2] -- the launcher's whole job on the data side
// is to find them, check them, and copy them somewhere stable.
//
// "Somewhere stable" matters: people point a file dialog at a download
// folder, then tidy up. Copying the ~22 MB game folder into the install's
// own `data/` once means the install keeps working after the source dump is
// moved, renamed or deleted.

#include <string>

namespace sk {
namespace launcher {

// The four files every real 6r51 folder has, checked together so that a
// folder which merely contains one of them by coincidence is not mistaken
// for the game. `stringtable.eng` is the one the engine treats as fatal to
// miss (main.cpp), so it is the one worth being certain about.
bool IsGameDataRoot(const std::string& directory);

// Accepts either the game folder itself or any parent within three levels
// -- people unzip these dumps at several different depths, and the folder
// that actually holds the files is `system/apps/6r51`, which nobody thinks
// to select. Returns the directory that passes IsGameDataRoot, or an empty
// string if there isn't one.
std::string FindGameDataRoot(const std::string& picked);

// A file is a usable font iff the port's own parser can load LatinBold12
// out of it -- the same call main.cpp makes, so "the launcher accepted it"
// and "the game can use it" cannot disagree.
bool IsUsableFont(const std::string& path);

// Best-effort guess at where a `.gdr` might already be on this machine
// (an EKA2L1 install's ROM drive, or a previous install of this launcher).
// Returns an empty string when nothing plausible is found -- it is a
// convenience for the first-run dialog, never a requirement.
std::string FindExistingFont(const std::string& installRoot);

// Recursive copy of `from` onto `to`, creating `to` and overwriting what is
// already there. On failure returns false and puts something a user can act
// on in `error` (a permission problem, a full disk, a source that vanished
// mid-copy).
bool CopyGameData(const std::string& from, const std::string& to, std::string& error);

// Copies a single file, same error convention. Used for the font.
bool CopyOneFile(const std::string& from, const std::string& to, std::string& error);

}  // namespace launcher
}  // namespace sk
