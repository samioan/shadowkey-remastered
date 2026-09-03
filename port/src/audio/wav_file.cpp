#include "audio/wav_file.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace sk {

namespace {

uint32_t ReadU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint16_t ReadU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

}  // namespace

bool LoadWavFile(const std::string& path, Sound& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::printf("LoadWavFile: could not open %s\n", path.c_str());
        return false;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.size() < 12 || std::memcmp(data.data(), "RIFF", 4) != 0 ||
        std::memcmp(data.data() + 8, "WAVE", 4) != 0) {
        std::printf("LoadWavFile: %s is not a RIFF/WAVE file\n", path.c_str());
        return false;
    }

    uint16_t audioFormat = 0, numChannels = 0, bitsPerSample = 0;
    uint32_t sampleRate = 0;
    const uint8_t* pcmData = nullptr;
    uint32_t pcmSize = 0;

    size_t pos = 12;
    while (pos + 8 <= data.size()) {
        char chunkId[5] = {0};
        std::memcpy(chunkId, &data[pos], 4);
        uint32_t chunkSize = ReadU32LE(&data[pos + 4]);
        size_t chunkStart = pos + 8;
        if (chunkStart + chunkSize > data.size()) break;  // truncated -- stop, use what we have

        if (std::memcmp(chunkId, "fmt ", 4) == 0 && chunkSize >= 16) {
            audioFormat = ReadU16LE(&data[chunkStart]);
            numChannels = ReadU16LE(&data[chunkStart + 2]);
            sampleRate = ReadU32LE(&data[chunkStart + 4]);
            bitsPerSample = ReadU16LE(&data[chunkStart + 14]);
        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            pcmData = &data[chunkStart];
            pcmSize = chunkSize;
        }
        pos = chunkStart + chunkSize + (chunkSize & 1);  // chunks are word-aligned
    }

    if (audioFormat != 1 || !pcmData || numChannels == 0 || sampleRate == 0) {
        std::printf("LoadWavFile: %s -- not recognized PCM WAVE (format=%u channels=%u rate=%u)\n",
                    path.c_str(), audioFormat, numChannels, sampleRate);
        return false;
    }

    out.sampleRate = static_cast<int>(sampleRate);
    out.channels = numChannels;
    out.samples.clear();
    if (bitsPerSample == 16) {
        size_t count = pcmSize / 2;
        out.samples.resize(count);
        for (size_t i = 0; i < count; ++i) {
            out.samples[i] = static_cast<int16_t>(ReadU16LE(&pcmData[i * 2]));
        }
    } else if (bitsPerSample == 8) {
        // Unsigned 8-bit PCM (the standard WAV convention) -- widen to
        // signed 16-bit so audio_engine.h only ever deals with one format.
        out.samples.resize(pcmSize);
        for (uint32_t i = 0; i < pcmSize; ++i) {
            out.samples[i] = static_cast<int16_t>((static_cast<int>(pcmData[i]) - 128) * 256);
        }
    } else {
        std::printf("LoadWavFile: %s -- unsupported bits-per-sample %u\n", path.c_str(),
                    bitsPerSample);
        return false;
    }
    return !out.samples.empty();
}

}  // namespace sk
