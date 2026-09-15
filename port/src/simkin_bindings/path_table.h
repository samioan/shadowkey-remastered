#pragma once

// M100: `<zone>.pth`, the named path table -- decoded here for the first
// time (docs/ZONE_FORMAT.md had only its outline).
//
// GameEngine_InitLevel reads it uncompressed, straight after the `.ent`
// placements:
//
//     u16 count
//     count times:
//         0x44-byte header:  +0x00  the path's name, NUL-terminated (strcpy'd)
//                            +0x40  u16 waypoint count
//         count x 8 bytes:   int32 x, int32 y   (world units)
//
// Each header becomes one slot of the engine's path array (`engine+0xdf98`,
// 0x178 bytes a slot, room for 48 -- FUN_1001ae8c), and each waypoint one of
// that slot's 32 entries (`+0x78`, FUN_1001ad2c). All 21 shipped files
// parse to the byte with this layout. Most are empty (a bare `00 00`) or
// hold unnamed "New Path" stubs with no waypoints; the only real path in the
// game is **`UmbraKeth`**, in crypt1 (32 waypoints), crypt2 (31) and crypt3
// (23) -- the nodes the final boss teleports between.
//
// The one script reader is the Monster binding `FindPathNode(name)` (see
// MonsterExecutable).

#include <cstdint>
#include <string>
#include <vector>

namespace sk_bindings {

struct PathWaypoint {
    int32_t x = 0;
    int32_t y = 0;
};

struct PathNode {
    std::string name;
    std::vector<PathWaypoint> waypoints;
};

// The engine's two capacities. A header past the 48th is dropped
// (FUN_1001ae8c returns 0), and so is a waypoint past a path's 32nd; no
// shipped file comes near either.
constexpr size_t kMaxPaths = 0x30;
constexpr size_t kMaxWaypointsPerPath = 0x20;

// Parses a whole `.pth` file. A truncated file keeps the paths read so far.
std::vector<PathNode> ParsePathTable(const std::vector<uint8_t>& bytes);
// Reads and parses `path`; empty for a missing file, as the engine (which
// logs and carries on) would have it.
std::vector<PathNode> LoadPathTable(const std::string& path);

// FUN_1001ada0: first path whose name `strcmp`s equal, or null.
const PathNode* FindPath(const std::vector<PathNode>& paths, const std::string& name);

// FUN_10086970 with FUN_100683a8 as its distance: the waypoint nearest
// (tx, ty) by `(dy*dy >> 8) + (dx*dx >> 8)` in 32-bit arithmetic, the first
// one winning a tie. Null for a path with no waypoints.
const PathWaypoint* NearestWaypoint(const PathNode& path, int tx, int ty);

}  // namespace sk_bindings
