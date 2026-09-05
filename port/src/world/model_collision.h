#pragma once

// M55: the per-model collision box, and the two save-record fields that
// turned out to be its front end.
//
// `<zone>_models.txt` is `"%d %d %d %d %s"` (`ZoneModelList_Load`,
// `FUN_10060ac0`). The port -- and docs/ZONE_FORMAT.md -- read columns 2-4
// as "3 flags" of unknown meaning. They are not flags. The loader writes
// them into a parallel 8-byte-stride table at `engine+0x6f38`, immediately
// after the 256-slot model-pointer cache at `engine+0x6b38`:
//
//     table[index].solid       = (u8)  column 2   // -> Entity+0x8c
//     table[index].halfExtentX = (u16) column 3   // -> Entity+0x8e
//     table[index].halfExtentY = (u16) column 4   // -> Entity+0x90
//
// and `Entity::Init` (`FUN_100610e4`) copies all three onto every placed
// entity of that model, through vtable slots `+0x4c`, `+0x50` and `+0x54`
// -- the last two being the `SetRadius`/`SetRadius2` script bindings.
//
// **That is what `Entity+0x8c` is**: the model's own "this thing is
// solid" flag. It is one of the two scalars M52 could not name, and the
// reason it looked dead is that it is only ever read through vtable slot
// `+0x40`, never by a direct field access -- so an offset-based search of
// the decompile finds nothing. All 31 Entity-derived vtables carry the
// identical accessor pair, so the slot is unambiguous.
//
// Five functions read it, and they are the whole of the engine's
// entity-vs-entity collision: `FUN_100017c8` (movement, transcribed
// below), `FUN_10001fd0` (does a stance change still fit),
// `FUN_10000ab8` (is this tile occupied), and the tile-stamp pair
// `FUN_10066204`/`FUN_1006640c`.
//
// The units are the engine's usual 8.8 world units, 256 per tile. Real
// values across all 21 shipped zones: `bottle 2 64 64`, `door 2 64 256`
// (thin and wide), `rail 2 64 512` (a fence run), `table 2 256 256`,
// `roof 0 1500 1500` (huge, and deliberately *not* solid),
// `dagger 0 0 0`, `spiderweb 0 0 0`. Column 2 is only ever 0 or 2 --
// never 1 -- and every reader tests it only for non-zero; a `2` rather
// than a `1` is why the save stream writes this one-byte field through
// its **i32** overload (`SAVE_FORMAT.md`'s write slot `0x18`), the only
// place in the image that happens, i.e. it is an enum, not a `TBool`.

#include <array>
#include <string>

namespace sk {

// An extent above this (half a tile) means the entity covers more than
// the tile it stands on, so the engine bakes it into the tile grid
// instead of testing it per-entity -- see `tileStamped()`.
inline constexpr int kTileStampThreshold = 128;

// The mover's own half-extent. `FUN_10001fd0` sets the player's `+0x8e`
// to `0x80` for every stance it accepts, and `FUN_100017c8` uses that one
// value on *both* axes for the moving entity's box (only the entity being
// tested against gets a separate X and Y).
inline constexpr float kActorHalfExtent = 128.0f;

struct ModelCollision {
    int solid = 0;         // column 2 -- 0 or 2 in every shipped manifest
    int halfExtentX = 0;   // column 3 -- Entity+0x8e, `SetRadius`
    int halfExtentY = 0;   // column 4 -- Entity+0x90, `SetRadius2`

    // The exact guard every reader applies: solid, and both extents
    // non-zero. `roof.bin`'s `0 1500 1500` fails on the first, and every
    // loot item's `0 0 0` fails on all three.
    bool blocks() const { return solid != 0 && halfExtentX != 0 && halfExtentY != 0; }

    // `GameEngine_InitLevel` sets `Entity+0x92` on exactly this test, and
    // the save loader (`FUN_100188dc`) repeats it when it re-creates a
    // saved entity. **That is the other unnamed scalar**: "this entity is
    // wider than the tile it stands on". The engine then calls
    // `FUN_10066204(entity, 4, 0)`, which walks the box rotated by the
    // entity's heading and ORs bit 2 into the flags byte of every tile
    // cell it covers -- and `FUN_10005d60` refuses to clear a stamped
    // entity's box when it is deactivated, because that bit would be
    // left behind.
    bool tileStamped() const {
        return halfExtentX > kTileStampThreshold || halfExtentY > kTileStampThreshold;
    }
};

class ModelCollisionTable {
public:
    // The real table is 256 entries, indexed by models.idx archive index
    // -- the same index `entities.txt`'s second column names and every
    // instance in this port already carries as `modelArchiveIndex`.
    static constexpr int kSlotCount = 256;

    // Reads `<scriptRoot>/<zoneName>_models.txt`. Returns false (and
    // leaves every slot zeroed, i.e. nothing solid) if it cannot be read,
    // which is what the real loader does too -- it bails on a missing
    // file without touching the table.
    bool Load(const std::string& scriptRoot, const std::string& zoneName);

    // Out-of-range indices (including the -1 an unresolved placement
    // carries) return an all-zero entry, so `blocks()` is false.
    const ModelCollision& At(int modelArchiveIndex) const;

    int solidCount() const;

private:
    std::array<ModelCollision, kSlotCount> m_Slots{};
    ModelCollision m_Empty{};
};

// The axis-aligned box the engine builds around an entity, in world
// units. `FUN_100017c8` keeps it as four ints and slides it with the
// move delta rather than rebuilding it, which is reproduced here only in
// spirit -- the test itself is the same.
struct CollisionBox {
    float minX = 0, minY = 0, maxX = 0, maxY = 0;
};

inline CollisionBox MakeBox(float centerX, float centerY, float halfX, float halfY) {
    return CollisionBox{centerX - halfX, centerY - halfY, centerX + halfX, centerY + halfY};
}

// `FUN_1001c48c`, the overlap predicate the two collision walks share.
// The real one compares `<=` on all four edges, so two boxes that merely
// touch count as overlapping; kept as it is.
inline bool BoxesOverlap(const CollisionBox& a, const CollisionBox& b) {
    return a.minX <= b.maxX && b.minX <= a.maxX && a.minY <= b.maxY && b.minY <= a.maxY;
}

}  // namespace sk
