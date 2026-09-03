#pragma once

// M27: real .ogg ambient/battle music decoding (system/apps/6r51/*.ogg,
// 6 real tracks, docs/AUDIO_FORMAT.md -- confirmed genuine Ogg Vorbis by
// header, "OggS"/"vorbis" magic). Thin wrapper around the vendored
// third_party/stb_vorbis (PROVENANCE.md) single-file decoder -- no custom
// RE needed, Vorbis is a standard, already-understood codec.

#include <string>

#include "audio/sound.h"

namespace sk {

// Returns false (logged) on any read/decode failure -- same non-fatal
// "optional asset" tolerance every other loader in this port already has.
bool DecodeOggFile(const std::string& path, Sound& out);

}  // namespace sk
