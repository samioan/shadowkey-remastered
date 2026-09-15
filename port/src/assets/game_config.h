#pragma once

// M52: `dragonstar.set` -- the settings file, and the only configuration
// file the shipped game actually writes.
//
// M40 listed five paths out of the binary's format strings and left three
// of them unread: `levelinfo.txt`, `dragonstar.cfg` and `dragonstar.set`.
// Two of those three turn out not to exist:
//
//   * **`levelinfo.txt` is dead.** Its full path
//     (`c:\system\apps\6R51\levelinfo.txt`, 0x100ab238) and the `"%d\n%d\n"`
//     format string next to it (0x100ab264) are *unreferenced* -- no word
//     anywhere in the image points at either, so no code can open the file
//     or use that format. Only the bare filename is referenced, and only
//     from the delete list below.
//   * **`dragonstar.cfg` is dead too**, and more thoroughly: it exists
//     only as a bare filename (0x100b34e4). There is no full path for it
//     anywhere, so nothing can open it at all.
//
// Both survive in exactly one place: `DeleteAllGames` (GameEngine binding
// 0x2e, `FUN_10078de4` case 0x2e) unlinks seven named files after wiping
// the four save slots --
//
//     current.sav  dragonstar.cfg  dragonstar.set
//     ngen.log  levelinfo.txt  tmp.big  6r51.cfg
//
// -- which is a cleanup list written against a longer file set than this
// build produces. Of the seven, this build only ever creates `current.sav`,
// `game0..3.sav` and `dragonstar.set`.
//
// So the third file is the whole of it, and it is plain text:
//
//     ACTIONMAP
//     16
//     <slot for action 0>
//     ... 16 lines ...
//     LANGUAGE
//     <n>
//     SOUNDVOLUME
//     <0..100>
//     MUSICVOLUME
//     <0..100>
//     MUTEONCALL
//     <0 or 1>
//
// M99: the writer runs from the `SaveConfig` binding, from `Quit` and
// `QuitGame`, and from the application-exit event -- MenuStack::SaveConfig
// is all four in this port.
//
// Writer `FUN_10019c04` (`SaveConfig`, GameEngine binding 0x71): opens the
// path `"wt"` and `fprintf`s each key with its own trailing newline and
// each value through `"%d\n"`. Reader `FUN_100199e0`: opens `"rt"` and
// loops `fscanf("%s")` on the key, then `strcmp`s it against the five
// keys, reading values with `"%d"`.
//
// Three details worth keeping, because they are asymmetries rather than
// tidy round trips:
//
//   * **The writer hardcodes 16**; the reader honours whatever count the
//     file gives. The real `InputState` binding table has 17 entries
//     (INPUT_HANDLING.md), so the 17th is never written and never
//     restored.
//   * **The writer refuses below 5000 bytes free** (`FUN_1000b5cc`), shows
//     its "free space - %d" message and sets the engine's save-failed flag
//     instead of writing a truncated file. That is what `SaveConfigFailed`
//     is for.
//   * **Volumes go straight to the mixer**, not to a settings struct: the
//     writer reads `soundMgr+0xabc` / `soundMgr+0xab8` and the reader
//     calls the two volume setters. Those are exactly M51's `SoundFXSlider`
//     and `MusicSlider` fields, which is the second, independent
//     confirmation of which slider is which.

#include <string>

#include "engine/input_state.h"

namespace sk {

struct GameConfig {
    // One raw button slot per action, in `Action` order; -1 for unbound.
    // Only kWrittenActions of them are written, matching the real writer.
    std::array<int, static_cast<size_t>(Action::kCount)> actionMap{};
    int language = 0;      // engine+0x14a4c
    int soundVolume = 100; // soundMgr+0xabc, the SFX master
    int musicVolume = 100; // soundMgr+0xab8, the music master
    bool muteOnCall = false;  // engine+0x14a79

    // The writer's own literal loop bound. The table is 17 long.
    static constexpr int kWrittenActions = 16;
    // The writer bails out below this much free space rather than
    // truncating (FUN_10019c04's `if (free < 5000)` branch).
    static constexpr int kMinFreeBytes = 5000;
    // The five keys, verbatim and in wire order.
    static const char* const kKeyActionMap;   // "ACTIONMAP"
    static const char* const kKeyLanguage;    // "LANGUAGE"
    static const char* const kKeySoundVolume; // "SOUNDVOLUME"
    static const char* const kKeyMusicVolume; // "MUSICVOLUME"
    static const char* const kKeyMuteOnCall;  // "MUTEONCALL"
    // `c:\system\apps\6R51\dragonstar.set` on the device; here, just the
    // basename, resolved against whatever directory the port keeps saves in.
    static const char* const kFileName;       // "dragonstar.set"

    // Fills `actionMap` from an InputState, and applies it back.
    void CaptureBindings(const InputState& input);
    void ApplyBindings(InputState& input) const;

    // The exact bytes `SaveConfig` would write.
    std::string Serialize() const;
    // The reader's own behaviour: unknown keys are skipped, missing keys
    // leave the current value alone, and the ACTIONMAP count comes from
    // the file rather than being assumed. Returns false only if `text` is
    // empty of any recognised key.
    bool Parse(const std::string& text);

    bool Save(const std::string& path) const;
    bool Load(const std::string& path);
};

}  // namespace sk
