#pragma once

// M27: decoded PCM audio -- the common output shape both wav_file.h (real
// .wav sound effects) and vorbis_decoder.h (real .ogg ambient/battle
// music, docs/AUDIO_FORMAT.md) produce, and what audio_engine.h consumes
// for playback. Always signed 16-bit PCM, interleaved if channels==2 --
// every real .wav in the corpus is already 16-bit PCM (docs/
// AUDIO_FORMAT.md's own verification), and stb_vorbis decodes straight to
// 16-bit PCM too, so no other sample format is needed anywhere in this
// port's audio path.

#include <cstdint>
#include <vector>

namespace sk {

struct Sound {
    std::vector<int16_t> samples;  // interleaved if channels == 2
    int sampleRate = 0;
    int channels = 0;

    bool empty() const { return samples.empty() || sampleRate <= 0 || channels <= 0; }
};

}  // namespace sk
