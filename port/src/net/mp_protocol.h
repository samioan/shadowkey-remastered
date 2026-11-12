#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// M129: the multiplayer wire format -- docs/MULTIPLAYER_PROTOCOL.md, as code.
//
// One message is one packet. Byte 0 is the tag; fixed-size messages are a
// little-endian struct padded to 4 bytes with most fields at +4, +8, +0xc;
// the three variable ones (0x1d, 0x29, 0x32) are a tag byte followed by a
// typed stream. Every layout here is the one the shipped sender writes and
// the shipped receiver reads, at the offsets the catalogue lists, so a
// capture of this port's traffic reads against that document directly.
//
// Two things are this port's, because Bluetooth gave the original what a
// socket does not:
//
//   * names on the wire are UTF-16LE in the shipped packets; they are kept
//     that way (Latin-1 widened), so the sizes stay the catalogue's;
//   * the entity stream's per-class save chain is the SavedEntity record the
//     port already writes for current.sav (M121), carried as bytes.

namespace sk_net {

enum MpTag : uint8_t {
    kTagGameStart = 0x00,
    kTagPlayerState = 0x01,
    kTagPlaceAndStart = 0x06,
    kTagReady = 0x07,
    kTagPlayerLeft = 0x14,
    kTagPauseLock = 0x15,
    kTagYourIndex = 0x17,
    kTagAttackVisual = 0x18,
    kTagKill = 0x19,
    kTagHitForZero = 0x1a,
    kTagTouched = 0x1b,
    kTagRoomEvent = 0x1c,
    kTagSpawnEntity = 0x1d,
    kTagCreatureTarget = 0x1e,
    kTagDoor = 0x1f,
    kTagQuestFlag = 0x21,
    kTagPlayerInfo = 0x22,
    kTagHeldWeapon = 0x23,
    kTagTextMessage = 0x24,
    kTagSpellProjectile = 0x25,
    kTagEffectEntity = 0x26,
    kTagAttachedEffect = 0x27,
    kTagShareXp = 0x28,
    kTagStoryState = 0x29,
    kTagAnimation = 0x2a,
    kTagDestroyObject = 0x2b,
    kTagLevelVar = 0x2c,
    kTagLevelVarMenu = 0x2d,
    kTagTravel = 0x2e,
    kTagNoticed = 0x2f,
    kTagLoaded = 0x30,
    kTagTakenFromContainer = 0x31,
    kTagHandOver = 0x32,
    kTagSetPosition = 0x33,
    kTagMirrorMethod = 0x34,
    kTagInvulnerable = 0x35,
    kTagUmbraBeat = 0x37,
    kTagRegistryFlag = 0x38,
    kTagAreaSpell = 0x39,
    kTagSetOnFire = 0x3a,
    kTagAlerted = 0x3b,
    kTagKeepalive = 0x3c,
    kTagDamageMe = 0x3d,
};

struct MpTagInfo {
    uint8_t tag;
    uint16_t size;  // bytes on the wire; 0 = variable (a stream)
    const char* name;
};

// All 43 tags, in tag order. 0x20 is unused in the shipped game and has no
// row. Null for a tag the protocol does not have.
const MpTagInfo* MpTagInfoFor(uint8_t tag);
int MpTagCount();
const MpTagInfo& MpTagAt(int index);

constexpr int kMpMaxPlayers = 2;      // host and client: the comms layer's two peer slots
constexpr int kMpNameChars = 0x20;    // a 0x40-byte UTF-16 name in tag 0x00
constexpr int kMpWideName = 0x80;     // tag 0x22's name / 0x2c's variable name, in chars

using MpBytes = std::vector<uint8_t>;

// A fixed-size message under construction: zero-filled, tag in byte 0.
class MpWriter {
public:
    MpWriter(uint8_t tag, size_t size) : bytes_(size, 0) {
        if (!bytes_.empty()) bytes_[0] = tag;
    }
    void U8(size_t off, uint32_t v) { Put(off, &v, 1); }
    void U16(size_t off, uint32_t v) { Put(off, &v, 2); }
    void I16(size_t off, int32_t v) { Put(off, &v, 2); }
    void I32(size_t off, int32_t v) { Put(off, &v, 4); }
    // Latin-1 widened to UTF-16LE, NUL terminated, at most `maxChars`.
    void Wide(size_t off, const std::string& text, size_t maxChars);
    MpBytes Take() { return std::move(bytes_); }

private:
    void Put(size_t off, const void* v, size_t n) {
        if (off + n <= bytes_.size()) std::memcpy(bytes_.data() + off, v, n);  // little-endian host
    }
    MpBytes bytes_;
};

class MpReader {
public:
    MpReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}
    explicit MpReader(const MpBytes& b) : data_(b.data()), size_(b.size()) {}
    uint8_t U8(size_t off) const { return off < size_ ? data_[off] : 0; }
    uint16_t U16(size_t off) const {
        uint16_t v = 0;
        if (off + 2 <= size_) std::memcpy(&v, data_ + off, 2);
        return v;
    }
    int16_t I16(size_t off) const { return static_cast<int16_t>(U16(off)); }
    int32_t I32(size_t off) const {
        int32_t v = 0;
        if (off + 4 <= size_) std::memcpy(&v, data_ + off, 4);
        return v;
    }
    std::string Wide(size_t off, size_t maxChars) const;
    size_t size() const { return size_; }

private:
    const uint8_t* data_;
    size_t size_;
};

// ---- the messages this port sends and applies --------------------------
// Each Encode returns exactly the catalogue's packet; each struct's Decode
// reads one back. Field comments give the wire offsets.

struct MpGameStart {  // 0x00, 0x88 bytes, host -> client
    int levelIndex = 0;            // +4
    int playerCount = 1;           // +5 holds playerCount - 1
    std::string names[kMpMaxPlayers];  // +6, two 0x40-byte names
    MpBytes Encode() const;
    static MpGameStart Decode(const MpBytes& b);
};

struct MpPlayerState {  // 0x01, 0x20 bytes
    int index = 0;                 // +4
    int32_t x = 0, y = 0;          // +8, +0xc
    int32_t vx = 0, vy = 0;        // +0x10, +0x14
    int16_t z = 0, vz = 0;         // +0x18, +0x1a
    int16_t heading = 0, pitch = 0;  // +0x1c, +0x1e
    MpBytes Encode() const;
    static MpPlayerState Decode(const MpBytes& b);
    bool operator==(const MpPlayerState& o) const {
        return index == o.index && x == o.x && y == o.y && vx == o.vx && vy == o.vy && z == o.z &&
               vz == o.vz && heading == o.heading && pitch == o.pitch;
    }
};

struct MpPlaceAndStart {  // 0x06, 0x10 bytes: +4 x, +8 y, +0xc z (s16), +0xe heading
    int32_t x = 0, y = 0;
    int16_t z = 0, heading = 0;
    MpBytes Encode() const;
    static MpPlaceAndStart Decode(const MpBytes& b);
};

struct MpPlayerInfo {  // 0x22, 0x108 bytes
    int index = 0;                 // +4
    int classId = 0, race = 0, sex = 0;  // +5, +6, +7
    std::string name;              // +8
    MpBytes Encode() const;
    static MpPlayerInfo Decode(const MpBytes& b);
};

struct MpLevelVar {  // 0x2c / 0x2d, 0x108 bytes: +4 name, +0x104 value
    std::string name;
    int32_t value = 0;
    MpBytes Encode(uint8_t tag = kTagLevelVar) const;
    static MpLevelVar Decode(const MpBytes& b);
};

struct MpTravel {  // 0x2e, 0x20 bytes: +4 level index, +5 armed, +8.. six spawn fields
    int levelIndex = 0;
    bool overrideArmed = false;
    int32_t spawn[6] = {};  // x, y, z, yaw, pitch, roll (engine+0x14a20..)
    MpBytes Encode() const;
    static MpTravel Decode(const MpBytes& b);
};

struct MpSetPosition {  // 0x33, 0x18 bytes: +4 2 = everyone, +8 id, +0xc x, +0x10 y, +0x14 z
    bool everyone = false;
    int entityId = 0;
    int32_t x = 0, y = 0, z = 0;
    MpBytes Encode() const;
    static MpSetPosition Decode(const MpBytes& b);
};

struct MpMirrorMethod {  // 0x34, 0x88 bytes: +4 entity id, +8 wide method name
    int entityId = 0;
    std::string method;
    MpBytes Encode() const;
    static MpMirrorMethod Decode(const MpBytes& b);
};

// The small ones: one or two fields at fixed offsets.
MpBytes EncodeTagOnly(uint8_t tag);                       // 0x07, 0x30, 0x3c: 4 bytes
MpBytes EncodeIndexed(uint8_t tag, int value);            // 0x14, 0x17: +4
MpBytes EncodePauseLock(int index, bool take);            // 0x15: +4 index, +5 flag
MpBytes EncodeAttackVisual(int index, int anim, int sound);  // 0x18: +4, +5, +6
MpBytes EncodeEntityId(uint8_t tag, int entityId);        // 0x19, 0x1b, 0x2b(+4), 0x2f(+4), 0x3b(+4)
MpBytes EncodeHitForZero(int entityId);                   // 0x1a: +8
MpBytes EncodeCreatureTarget(int entityId, int playerIndex);  // 0x1e: +4 id, +0xc index
MpBytes EncodeDoor(int entityId, bool close);             // 0x1f: +8 id, +5 0 open / 1 close
MpBytes EncodeQuestFlag(int kind, int quest, int value);  // 0x21: +4, +5, +6
MpBytes EncodeHeldWeapon(int index, int weapon);          // 0x23: +4, +6 (s16)
MpBytes EncodeText(int index, const std::string& text);   // 0x24: +4, +6 wide
MpBytes EncodeShareXp(int kind, int value);               // 0x28: +4 kind, +8 value
MpBytes EncodeAnimation(int anim, int index);             // 0x2a: +4, +5
MpBytes EncodeTaken(int containerId, int itemTypeId);     // 0x31: +4, +8 (s16)
MpBytes EncodeInvulnerable(int entityId, bool flag);      // 0x35: +4, +8
MpBytes EncodeRegistryFlag(int entityId, int mask, bool set);  // 0x38: +4, +6, +8
MpBytes EncodeSetOnFire(int entityId, int level);         // 0x3a: +4, +8
MpBytes EncodeDamageMe(int amount);                       // 0x3d: +4
// M132. 0x25: +4 1 = an avatar's (index at +8) / 0 = an entity's (id), +0xc
// sprite, +0xe 0x100, +0x10 speed, +0x14 the spell's typeId, +0x18 pitch.
MpBytes EncodeSpellProjectile(bool avatar, int idOrIndex, int sprite, int speed, int spellTypeId,
                              int pitch);
// 0x27: +4 avatar flag, +8 id or index, +0xc first sprite, +0x10 last, +0x14 seconds.
MpBytes EncodeAttachedEffect(bool avatar, int idOrIndex, int first, int last, int seconds);
MpBytes EncodeUmbraBeat(int entityId, bool canTeleport);  // 0x37: +4, +8
// 0x39: +4 caster id, +8 target id or 0, +0xc, +0x10 (u8).
MpBytes EncodeAreaSpell(int casterId, int targetId, int value, int flag);
// Where the entity id of a single-id message sits (0 when the tag has none).
size_t EntityIdOffset(uint8_t tag);

// ---- the two streams ----------------------------------------------------

// 0x1d / 0x32: tag, typeId (s16), entity id (u16), then the class's save
// chain -- here the bytes of one SavedEntity record.
MpBytes EncodeEntityStream(uint8_t tag, int typeId, int entityId, const MpBytes& chain);
bool DecodeEntityStream(const MpBytes& b, int* typeId, int* entityId, MpBytes* chain);

// 0x29: the level record, then 256 x (assigned, completed, solved), then
// the 18 `saved_*` ints.
struct MpStoryState {
    MpBytes levelRecord;
    uint8_t assigned[256] = {}, completed[256] = {}, solved[256] = {};
    int32_t savedInts[18] = {};
    MpBytes Encode() const;
    static bool Decode(const MpBytes& b, MpStoryState* out);
};

}  // namespace sk_net
