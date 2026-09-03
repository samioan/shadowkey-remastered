#pragma once

// M27: real per-zone/menu sound manifest (system/apps/6r51/<category>_
// sounds.txt, docs/AUDIO_FORMAT.md) -- same "numbered slot -> filename,
// NULL.<ext> sentinel for an unused slot" convention already established
// for <zone>_models.txt (world/model_archive.h) and <category>_
// sprites.txt (sprite_archive.h), just for real .wav sound effects and
// .ogg ambient/battle music tracks instead. Confirmed by real corpus
// cross-reference: a real script's PlaySound(id)/PlayAmbient(id, volume)
// `id` argument is exactly this manifest's own slot index (e.g. door.s's
// `GetPlayer().PlaySound(63)` on open / `PlaySound(62)` on close matches
// azra_sounds.txt's own `63 door_open.wav` / `62 door_close.wav`
// one-for-one; azra.s's `Level.PlayAmbient(73,100)` matches azra_sounds.
// txt's own `73 explore3.ogg`).
//
// Unlike sprite_archive.h's `global.spr` (one shared 384-slot archive
// file, LoadCategory() just prewarms a subset), there is no single sound
// archive file -- each real .wav/.ogg is its own file directly under
// scriptRoot, and each zone's own manifest assigns slot numbers
// independently (the same slot index can name a different real file in
// a different zone). LoadCategory() therefore *replaces* the previous
// manifest wholesale (not additive) -- called once per zone load,
// mirroring sprite_archive.h/model_archive.h's own per-zone reload
// pattern (main.cpp's zone-load block). Decoded PCM is cached by
// resolved file path (case-insensitive), not by slot index, so a real
// sound shared across zones/menus (e.g. "door_open.wav", present in
// nearly every zone's own manifest) is only ever decoded once.

#include <map>
#include <string>

#include "audio/sound.h"

namespace sk {

class SoundArchive {
public:
    // Reads <scriptRoot>/<category>_sounds.txt (e.g. "azra", "menu") --
    // replaces whatever manifest was previously loaded. Returns false
    // (logged) if the manifest file itself can't be opened; a missing
    // manifest is not fatal to the rest of this port (same "optional
    // asset" tolerance as LoadCategory() everywhere else) -- callers just
    // won't get any real GetSound() hits until a real one loads.
    bool LoadCategory(const std::string& scriptRoot, const std::string& category);

    // Returns nullptr for an out-of-range/unmapped slot, a real "NULL.wav"
    // sentinel entry, or a file that fails to decode (missing, corrupt,
    // unrecognized format) -- lazily decodes (via wav_file.h/
    // vorbis_decoder.h, dispatched by real file extension) and caches on
    // first call.
    const Sound* GetSound(int slotIndex);

    // True if the slot's real file is a .ogg (ambient/battle music,
    // meant to loop) rather than a .wav (a one-shot sound effect) --
    // callers (LevelExecutable's real PlayAmbient handler) use this to
    // decide AudioEngine::PlayMusic() vs PlaySfx(), though in practice
    // PlayAmbient's own real corpus usage always targets a real .ogg slot
    // anyway.
    bool IsMusic(int slotIndex) const;

private:
    std::string m_ScriptRoot;
    std::map<int, std::string> m_SlotToFile;  // current category only -- see class comment
    std::map<std::string, Sound> m_Cache;      // by lowercased full path -- persists across reloads
};

}  // namespace sk
