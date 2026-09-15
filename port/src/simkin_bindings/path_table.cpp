#include "simkin_bindings/path_table.h"

#include <cstring>
#include <fstream>
#include <iterator>

namespace sk_bindings {

namespace {

constexpr size_t kHeaderSize = 0x44;
constexpr size_t kNameSize = 0x40;
constexpr size_t kWaypointSize = 8;

int32_t ReadI32(const uint8_t* p) {
    return static_cast<int32_t>(static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                                (static_cast<uint32_t>(p[2]) << 16) |
                                (static_cast<uint32_t>(p[3]) << 24));
}

// FUN_100683a8, wrapping as the 32-bit original does.
int32_t PointDistance(int32_t ax, int32_t ay, int32_t bx, int32_t by) {
    const uint32_t dy = static_cast<uint32_t>(by) - static_cast<uint32_t>(ay);
    const uint32_t dx = static_cast<uint32_t>(bx) - static_cast<uint32_t>(ax);
    const int32_t yy = static_cast<int32_t>(dy * dy) >> 8;
    const int32_t xx = static_cast<int32_t>(dx * dx) >> 8;
    return static_cast<int32_t>(static_cast<uint32_t>(yy) + static_cast<uint32_t>(xx));
}

}  // namespace

std::vector<PathNode> ParsePathTable(const std::vector<uint8_t>& bytes) {
    std::vector<PathNode> paths;
    if (bytes.size() < 2) return paths;
    const size_t count = static_cast<size_t>(bytes[0]) | (static_cast<size_t>(bytes[1]) << 8);
    size_t off = 2;
    for (size_t i = 0; i < count; ++i) {
        if (off + kHeaderSize > bytes.size()) break;
        const uint8_t* header = &bytes[off];
        size_t nameLen = 0;
        while (nameLen < kNameSize && header[nameLen] != 0) ++nameLen;
        const size_t waypointCount =
            static_cast<size_t>(header[kNameSize]) | (static_cast<size_t>(header[kNameSize + 1]) << 8);
        off += kHeaderSize;
        PathNode node;
        node.name.assign(reinterpret_cast<const char*>(header), nameLen);
        for (size_t k = 0; k < waypointCount; ++k) {
            if (off + kWaypointSize > bytes.size()) break;
            if (node.waypoints.size() < kMaxWaypointsPerPath) {
                PathWaypoint wp;
                wp.x = ReadI32(&bytes[off]);
                wp.y = ReadI32(&bytes[off + 4]);
                node.waypoints.push_back(wp);
            }
            off += kWaypointSize;
        }
        if (paths.size() < kMaxPaths) paths.push_back(std::move(node));
    }
    return paths;
}

std::vector<PathNode> LoadPathTable(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return ParsePathTable(bytes);
}

const PathNode* FindPath(const std::vector<PathNode>& paths, const std::string& name) {
    for (const PathNode& node : paths) {
        if (node.name == name) return &node;
    }
    return nullptr;
}

const PathWaypoint* NearestWaypoint(const PathNode& path, int tx, int ty) {
    const PathWaypoint* best = nullptr;
    int32_t bestDistance = 0;
    for (size_t i = 0; i < path.waypoints.size(); ++i) {
        const PathWaypoint& wp = path.waypoints[i];
        // `vtable[0x60](target, wp.x, wp.y)` -- the target's distance to it.
        const int32_t d = PointDistance(tx, ty, wp.x, wp.y);
        if (i == 0 || d < bestDistance) {
            best = &wp;
            bestDistance = d;
        }
    }
    return best;
}

}  // namespace sk_bindings
