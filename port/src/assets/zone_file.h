#pragma once

// Loader for the "compressed per-zone file" format docs/ZONE_FORMAT.md
// documents (.ztx/.zmp/.zlu/.zfg/.zcp/.zsk all go through this): a 4-byte
// LE decompressed-size header followed by an ordinary zlib stream
// (EZLIB__uncompress -- standard 2-byte zlib header + deflate + Adler-32
// trailer). Verified directly against real azra.zmp/.zcp this session.

#include <cstdint>
#include <string>
#include <vector>

namespace sk {

// Returns the decompressed bytes, or empty (logged) on any failure --
// missing file, corrupt header, or a size mismatch against the 4-byte
// header's claimed length.
std::vector<uint8_t> LoadCompressedZoneFile(const std::string& path);

}  // namespace sk
