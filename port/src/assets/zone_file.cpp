#include "assets/zone_file.h"

#include <cstdio>
#include <fstream>
#include <iterator>

extern "C" {
#include "puff.h"
}

namespace sk {

std::vector<uint8_t> LoadCompressedZoneFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::printf("LoadCompressedZoneFile: could not open %s\n", path.c_str());
        return {};
    }
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    // 4-byte size header + 2-byte zlib header + at least an empty deflate
    // stream + 4-byte Adler-32 trailer.
    if (raw.size() < 10) {
        std::printf("LoadCompressedZoneFile: %s too short (%zu bytes)\n", path.c_str(), raw.size());
        return {};
    }

    uint32_t decompSize = static_cast<uint32_t>(raw[0]) | (static_cast<uint32_t>(raw[1]) << 8) |
                           (static_cast<uint32_t>(raw[2]) << 16) |
                           (static_cast<uint32_t>(raw[3]) << 24);

    // puff() decompresses raw DEFLATE, not the zlib-wrapped stream --
    // skip the 2-byte zlib header (CMF/FLG) and the 4-byte Adler-32
    // trailer at the end; puff() doesn't need either.
    const uint8_t* deflateStart = raw.data() + 6;
    unsigned long sourceLen = static_cast<unsigned long>(raw.size() - 6 - 4);

    std::vector<uint8_t> out(decompSize);
    unsigned long destLen = decompSize;
    int ret = puff(out.data(), &destLen, deflateStart, &sourceLen);
    if (ret != 0) {
        std::printf("LoadCompressedZoneFile: %s failed to inflate (puff error %d)\n", path.c_str(),
                    ret);
        return {};
    }
    if (destLen != decompSize) {
        std::printf(
            "LoadCompressedZoneFile: %s size mismatch -- header says %u, inflated %lu\n",
            path.c_str(), decompSize, destLen);
        return {};
    }
    return out;
}

}  // namespace sk
