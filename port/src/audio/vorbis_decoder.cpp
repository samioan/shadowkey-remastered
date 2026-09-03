#include "audio/vorbis_decoder.h"

#include <cstdio>
#include <cstdlib>

#define STB_VORBIS_IMPLEMENTATION
#include "stb_vorbis.c"

namespace sk {

bool DecodeOggFile(const std::string& path, Sound& out) {
    int channels = 0, sampleRate = 0;
    short* samples = nullptr;
    int frames = stb_vorbis_decode_filename(path.c_str(), &channels, &sampleRate, &samples);
    if (frames <= 0 || !samples) {
        std::printf("DecodeOggFile: could not decode %s\n", path.c_str());
        if (samples) free(samples);
        return false;
    }
    out.sampleRate = sampleRate;
    out.channels = channels;
    out.samples.assign(samples, samples + static_cast<size_t>(frames) * channels);
    free(samples);
    return !out.samples.empty();
}

}  // namespace sk
