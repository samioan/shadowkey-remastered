#include "net/mp_protocol.h"

namespace sk_net {

namespace {

// docs/MULTIPLAYER_PROTOCOL.md's catalogue, sizes in bytes.
constexpr MpTagInfo kTags[] = {
    {0x00, 0x88, "GameStart"},      {0x01, 0x20, "PlayerState"},
    {0x06, 0x10, "PlaceAndStart"},  {0x07, 4, "Ready"},
    {0x14, 8, "PlayerLeft"},        {0x15, 8, "PauseLock"},
    {0x17, 8, "YourIndex"},         {0x18, 8, "AttackVisual"},
    {0x19, 0xc, "Kill"},            {0x1a, 0x10, "HitForZero"},
    {0x1b, 0xc, "Touched"},         {0x1c, 0xc, "RoomEvent"},
    {0x1d, 0, "SpawnEntity"},       {0x1e, 0x10, "CreatureTarget"},
    {0x1f, 0xc, "Door"},            {0x21, 8, "QuestFlag"},
    {0x22, 0x108, "PlayerInfo"},    {0x23, 8, "HeldWeapon"},
    {0x24, 0x208, "TextMessage"},   {0x25, 0x1c, "SpellProjectile"},
    {0x26, 0x28, "EffectEntity"},   {0x27, 0x18, "AttachedEffect"},
    {0x28, 0xc, "ShareXp"},         {0x29, 0, "StoryState"},
    {0x2a, 8, "Animation"},         {0x2b, 8, "DestroyObject"},
    {0x2c, 0x108, "LevelVar"},      {0x2d, 0x108, "LevelVarMenu"},
    {0x2e, 0x20, "Travel"},         {0x2f, 8, "Noticed"},
    {0x30, 4, "Loaded"},            {0x31, 0xc, "TakenFromContainer"},
    {0x32, 0, "HandOver"},          {0x33, 0x18, "SetPosition"},
    {0x34, 0x88, "MirrorMethod"},   {0x35, 0xc, "Invulnerable"},
    {0x37, 0xc, "UmbraBeat"},       {0x38, 0xc, "RegistryFlag"},
    {0x39, 0x14, "AreaSpell"},      {0x3a, 0xc, "SetOnFire"},
    {0x3b, 8, "Alerted"},           {0x3c, 4, "Keepalive"},
    {0x3d, 8, "DamageMe"},
};

size_t SizeOf(uint8_t tag) {
    const MpTagInfo* info = MpTagInfoFor(tag);
    return info ? info->size : 0;
}

void AppendI16(MpBytes& b, int32_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xff));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
}
void AppendI32(MpBytes& b, int32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>((static_cast<uint32_t>(v) >> (8 * i)) & 0xff));
}

}  // namespace

const MpTagInfo* MpTagInfoFor(uint8_t tag) {
    for (const MpTagInfo& info : kTags) {
        if (info.tag == tag) return &info;
    }
    return nullptr;
}
int MpTagCount() { return static_cast<int>(sizeof(kTags) / sizeof(kTags[0])); }
const MpTagInfo& MpTagAt(int index) { return kTags[index]; }

void MpWriter::Wide(size_t off, const std::string& text, size_t maxChars) {
    if (maxChars == 0) return;
    const size_t n = text.size() < maxChars - 1 ? text.size() : maxChars - 1;
    for (size_t i = 0; i < n; ++i) U16(off + i * 2, static_cast<uint8_t>(text[i]));
    U16(off + n * 2, 0);
}

std::string MpReader::Wide(size_t off, size_t maxChars) const {
    std::string out;
    for (size_t i = 0; i < maxChars; ++i) {
        const uint16_t c = U16(off + i * 2);
        if (c == 0) break;
        out.push_back(c < 0x100 ? static_cast<char>(c) : '?');
    }
    return out;
}

MpBytes MpGameStart::Encode() const {
    MpWriter w(kTagGameStart, SizeOf(kTagGameStart));
    w.U8(4, static_cast<uint32_t>(levelIndex));
    w.U8(5, static_cast<uint32_t>(playerCount - 1));
    for (int i = 0; i < kMpMaxPlayers; ++i) w.Wide(6 + static_cast<size_t>(i) * 0x40, names[i], kMpNameChars);
    return w.Take();
}
MpGameStart MpGameStart::Decode(const MpBytes& b) {
    MpReader r(b);
    MpGameStart m;
    m.levelIndex = r.U8(4);
    m.playerCount = r.U8(5) + 1;
    for (int i = 0; i < kMpMaxPlayers; ++i) m.names[i] = r.Wide(6 + static_cast<size_t>(i) * 0x40, kMpNameChars);
    return m;
}

MpBytes MpPlayerState::Encode() const {
    MpWriter w(kTagPlayerState, SizeOf(kTagPlayerState));
    w.U8(4, static_cast<uint32_t>(index));
    w.I32(8, x);
    w.I32(0xc, y);
    w.I32(0x10, vx);
    w.I32(0x14, vy);
    w.I16(0x18, z);
    w.I16(0x1a, vz);
    w.I16(0x1c, heading);
    w.I16(0x1e, pitch);
    return w.Take();
}
MpPlayerState MpPlayerState::Decode(const MpBytes& b) {
    MpReader r(b);
    MpPlayerState m;
    m.index = r.U8(4);
    m.x = r.I32(8);
    m.y = r.I32(0xc);
    m.vx = r.I32(0x10);
    m.vy = r.I32(0x14);
    m.z = r.I16(0x18);
    m.vz = r.I16(0x1a);
    m.heading = r.I16(0x1c);
    m.pitch = r.I16(0x1e);
    return m;
}

MpBytes MpPlaceAndStart::Encode() const {
    MpWriter w(kTagPlaceAndStart, SizeOf(kTagPlaceAndStart));
    w.I32(4, x);
    w.I32(8, y);
    w.I16(0xc, z);
    w.I16(0xe, heading);
    return w.Take();
}
MpPlaceAndStart MpPlaceAndStart::Decode(const MpBytes& b) {
    MpReader r(b);
    MpPlaceAndStart m;
    m.x = r.I32(4);
    m.y = r.I32(8);
    m.z = r.I16(0xc);
    m.heading = r.I16(0xe);
    return m;
}

MpBytes MpPlayerInfo::Encode() const {
    MpWriter w(kTagPlayerInfo, SizeOf(kTagPlayerInfo));
    w.U8(4, static_cast<uint32_t>(index));
    w.U8(5, static_cast<uint32_t>(classId));
    w.U8(6, static_cast<uint32_t>(race));
    w.U8(7, static_cast<uint32_t>(sex));
    w.Wide(8, name, kMpWideName);
    return w.Take();
}
MpPlayerInfo MpPlayerInfo::Decode(const MpBytes& b) {
    MpReader r(b);
    MpPlayerInfo m;
    m.index = r.U8(4);
    m.classId = r.U8(5);
    m.race = r.U8(6);
    m.sex = r.U8(7);
    m.name = r.Wide(8, kMpWideName);
    return m;
}

MpBytes MpLevelVar::Encode(uint8_t tag) const {
    MpWriter w(tag, SizeOf(tag));
    w.Wide(4, name, kMpWideName);
    w.I32(0x104, value);
    return w.Take();
}
MpLevelVar MpLevelVar::Decode(const MpBytes& b) {
    MpReader r(b);
    MpLevelVar m;
    m.name = r.Wide(4, kMpWideName);
    m.value = r.I32(0x104);
    return m;
}

MpBytes MpTravel::Encode() const {
    MpWriter w(kTagTravel, SizeOf(kTagTravel));
    w.U8(4, static_cast<uint32_t>(levelIndex));
    w.U8(5, overrideArmed ? 1u : 0u);
    for (int i = 0; i < 6; ++i) w.I32(8 + static_cast<size_t>(i) * 4, spawn[i]);
    return w.Take();
}
MpTravel MpTravel::Decode(const MpBytes& b) {
    MpReader r(b);
    MpTravel m;
    m.levelIndex = r.U8(4);
    m.overrideArmed = r.U8(5) != 0;
    for (int i = 0; i < 6; ++i) m.spawn[i] = r.I32(8 + static_cast<size_t>(i) * 4);
    return m;
}

MpBytes MpSetPosition::Encode() const {
    MpWriter w(kTagSetPosition, SizeOf(kTagSetPosition));
    w.U8(4, everyone ? 2u : 0u);
    w.I32(8, entityId);
    w.I32(0xc, x);
    w.I32(0x10, y);
    w.I32(0x14, z);
    return w.Take();
}
MpSetPosition MpSetPosition::Decode(const MpBytes& b) {
    MpReader r(b);
    MpSetPosition m;
    m.everyone = r.U8(4) == 2;
    m.entityId = r.I32(8);
    m.x = r.I32(0xc);
    m.y = r.I32(0x10);
    m.z = r.I32(0x14);
    return m;
}

MpBytes MpMirrorMethod::Encode() const {
    MpWriter w(kTagMirrorMethod, SizeOf(kTagMirrorMethod));
    w.U16(4, static_cast<uint32_t>(entityId));
    w.Wide(8, method, kMpNameChars * 2);
    return w.Take();
}
MpMirrorMethod MpMirrorMethod::Decode(const MpBytes& b) {
    MpReader r(b);
    MpMirrorMethod m;
    m.entityId = r.U16(4);
    m.method = r.Wide(8, kMpNameChars * 2);
    return m;
}

MpBytes EncodeTagOnly(uint8_t tag) { return MpWriter(tag, SizeOf(tag)).Take(); }

MpBytes EncodeIndexed(uint8_t tag, int value) {
    MpWriter w(tag, SizeOf(tag));
    w.U8(4, static_cast<uint32_t>(value));
    return w.Take();
}

MpBytes EncodePauseLock(int index, bool take) {
    MpWriter w(kTagPauseLock, SizeOf(kTagPauseLock));
    w.U8(4, static_cast<uint32_t>(index));
    w.U8(5, take ? 1u : 0u);
    return w.Take();
}

MpBytes EncodeAttackVisual(int index, int anim, int sound) {
    MpWriter w(kTagAttackVisual, SizeOf(kTagAttackVisual));
    w.U8(4, static_cast<uint32_t>(index));
    w.U8(5, static_cast<uint32_t>(anim));
    w.U8(6, static_cast<uint32_t>(sound));
    return w.Take();
}

size_t EntityIdOffset(uint8_t tag) {
    switch (tag) {
        case kTagKill:
        case kTagHitForZero:
        case kTagTouched:
        case kTagDoor:
            return 8;
        case kTagCreatureTarget:
        case kTagDestroyObject:
        case kTagNoticed:
        case kTagAlerted:
        case kTagInvulnerable:
        case kTagSetOnFire:
        case kTagRegistryFlag:
        case kTagMirrorMethod:
        case kTagTakenFromContainer:
            return 4;
        default:
            return 0;
    }
}

MpBytes EncodeEntityId(uint8_t tag, int entityId) {
    MpWriter w(tag, SizeOf(tag));
    w.U16(EntityIdOffset(tag), static_cast<uint32_t>(entityId));
    return w.Take();
}

MpBytes EncodeHitForZero(int entityId) { return EncodeEntityId(kTagHitForZero, entityId); }

MpBytes EncodeCreatureTarget(int entityId, int playerIndex) {
    MpWriter w(kTagCreatureTarget, SizeOf(kTagCreatureTarget));
    w.U16(4, static_cast<uint32_t>(entityId));
    w.I32(0xc, playerIndex);
    return w.Take();
}

MpBytes EncodeDoor(int entityId, bool close) {
    MpWriter w(kTagDoor, SizeOf(kTagDoor));
    w.U8(5, close ? 1u : 0u);
    w.U16(8, static_cast<uint32_t>(entityId));
    return w.Take();
}

MpBytes EncodeQuestFlag(int kind, int quest, int value) {
    MpWriter w(kTagQuestFlag, SizeOf(kTagQuestFlag));
    w.U8(4, static_cast<uint32_t>(kind));
    w.U8(5, static_cast<uint32_t>(quest));
    w.U8(6, static_cast<uint32_t>(value));
    return w.Take();
}

MpBytes EncodeHeldWeapon(int index, int weapon) {
    MpWriter w(kTagHeldWeapon, SizeOf(kTagHeldWeapon));
    w.U8(4, static_cast<uint32_t>(index));
    w.I16(6, weapon);
    return w.Take();
}

MpBytes EncodeText(int index, const std::string& text) {
    MpWriter w(kTagTextMessage, SizeOf(kTagTextMessage));
    w.U8(4, static_cast<uint32_t>(index));
    w.Wide(6, text, 0x100);
    return w.Take();
}

MpBytes EncodeShareXp(int kind, int value) {
    MpWriter w(kTagShareXp, SizeOf(kTagShareXp));
    w.U8(4, static_cast<uint32_t>(kind));
    w.I32(8, value);
    return w.Take();
}

MpBytes EncodeAnimation(int anim, int index) {
    MpWriter w(kTagAnimation, SizeOf(kTagAnimation));
    w.U8(4, static_cast<uint32_t>(anim));
    w.U8(5, static_cast<uint32_t>(index));
    return w.Take();
}

MpBytes EncodeTaken(int containerId, int itemTypeId) {
    MpWriter w(kTagTakenFromContainer, SizeOf(kTagTakenFromContainer));
    w.U16(4, static_cast<uint32_t>(containerId));
    w.I16(8, itemTypeId);
    return w.Take();
}

MpBytes EncodeInvulnerable(int entityId, bool flag) {
    MpWriter w(kTagInvulnerable, SizeOf(kTagInvulnerable));
    w.U16(4, static_cast<uint32_t>(entityId));
    w.U8(8, flag ? 1u : 0u);
    return w.Take();
}

MpBytes EncodeRegistryFlag(int entityId, int mask, bool set) {
    MpWriter w(kTagRegistryFlag, SizeOf(kTagRegistryFlag));
    w.U16(4, static_cast<uint32_t>(entityId));
    w.U16(6, static_cast<uint32_t>(mask));
    w.U8(8, set ? 1u : 0u);
    return w.Take();
}

MpBytes EncodeSetOnFire(int entityId, int level) {
    MpWriter w(kTagSetOnFire, SizeOf(kTagSetOnFire));
    w.U16(4, static_cast<uint32_t>(entityId));
    w.I32(8, level);
    return w.Take();
}

MpBytes EncodeSpellProjectile(bool avatar, int idOrIndex, int sprite, int speed, int spellTypeId,
                              int pitch) {
    MpWriter w(kTagSpellProjectile, SizeOf(kTagSpellProjectile));
    w.U8(4, avatar ? 1u : 0u);
    w.I32(8, idOrIndex);
    w.I16(0xc, sprite);
    w.I16(0xe, 0x100);
    w.I16(0x10, speed);
    w.I32(0x14, spellTypeId);
    w.I16(0x18, pitch);
    return w.Take();
}

MpBytes EncodeAttachedEffect(bool avatar, int idOrIndex, int first, int last, int seconds) {
    MpWriter w(kTagAttachedEffect, SizeOf(kTagAttachedEffect));
    w.U8(4, avatar ? 1u : 0u);
    w.I32(8, idOrIndex);
    w.I32(0xc, first);
    w.I32(0x10, last);
    w.I32(0x14, seconds);
    return w.Take();
}

MpBytes EncodeUmbraBeat(int entityId, bool canTeleport) {
    MpWriter w(kTagUmbraBeat, SizeOf(kTagUmbraBeat));
    w.I32(4, entityId);
    w.U8(8, canTeleport ? 1u : 0u);
    return w.Take();
}

MpBytes EncodeAreaSpell(int casterId, int targetId, int value, int flag) {
    MpWriter w(kTagAreaSpell, SizeOf(kTagAreaSpell));
    w.I32(4, casterId);
    w.I32(8, targetId);
    w.I32(0xc, value);
    w.U8(0x10, static_cast<uint32_t>(flag));
    return w.Take();
}

MpBytes EncodeDamageMe(int amount) {
    MpWriter w(kTagDamageMe, SizeOf(kTagDamageMe));
    w.I32(4, amount);
    return w.Take();
}

MpBytes EncodeEntityStream(uint8_t tag, int typeId, int entityId, const MpBytes& chain) {
    MpBytes b;
    b.reserve(5 + chain.size());
    b.push_back(tag);
    AppendI16(b, typeId);
    AppendI16(b, entityId);
    b.insert(b.end(), chain.begin(), chain.end());
    return b;
}

bool DecodeEntityStream(const MpBytes& b, int* typeId, int* entityId, MpBytes* chain) {
    if (b.size() < 5) return false;
    MpReader r(b);
    if (typeId) *typeId = r.I16(1);
    if (entityId) *entityId = r.U16(3);
    if (chain) chain->assign(b.begin() + 5, b.end());
    return true;
}

MpBytes MpStoryState::Encode() const {
    MpBytes b;
    b.push_back(kTagStoryState);
    AppendI32(b, static_cast<int32_t>(levelRecord.size()));
    b.insert(b.end(), levelRecord.begin(), levelRecord.end());
    for (int i = 0; i < 256; ++i) {
        b.push_back(assigned[i]);
        b.push_back(completed[i]);
        b.push_back(solved[i]);
    }
    for (int32_t v : savedInts) AppendI32(b, v);
    return b;
}

bool MpStoryState::Decode(const MpBytes& b, MpStoryState* out) {
    if (b.size() < 5 || b[0] != kTagStoryState) return false;
    MpReader r(b);
    const int32_t n = r.I32(1);
    const size_t need = 5 + static_cast<size_t>(n < 0 ? 0 : n) + 256 * 3 + 18 * 4;
    if (n < 0 || b.size() < need) return false;
    size_t off = 5;
    out->levelRecord.assign(b.begin() + static_cast<std::ptrdiff_t>(off),
                            b.begin() + static_cast<std::ptrdiff_t>(off + static_cast<size_t>(n)));
    off += static_cast<size_t>(n);
    for (int i = 0; i < 256; ++i) {
        out->assigned[i] = b[off++];
        out->completed[i] = b[off++];
        out->solved[i] = b[off++];
    }
    for (int i = 0; i < 18; ++i) {
        out->savedInts[i] = r.I32(off);
        off += 4;
    }
    return true;
}

}  // namespace sk_net
