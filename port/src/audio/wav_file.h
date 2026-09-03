#pragma once

// M27: real .wav sound-effect decoding -- every real .wav in the corpus
// (system/apps/6r51/*.wav, 38 files, docs/AUDIO_FORMAT.md) is an
// ordinary uncompressed-PCM RIFF/WAVE file (confirmed by inspecting real
// file headers: `RIFF....WAVEfmt ` then a 16-byte PCM `fmt ` chunk,
// AudioFormat=1, then a `data` chunk of raw samples -- every file checked
// mono, 8000Hz, 16-bit, but this parser walks chunks generically rather
// than assuming that exact shape). No custom format, no RE needed for
// the container itself -- a plain RIFF chunk walk.

#include <string>

#include "audio/sound.h"

namespace sk {

// Returns false (logged) on any read/format failure (missing file, not a
// PCM WAVE, truncated data) -- same non-fatal "optional asset" tolerance
// every other loader in this port already has.
bool LoadWavFile(const std::string& path, Sound& out);

}  // namespace sk
