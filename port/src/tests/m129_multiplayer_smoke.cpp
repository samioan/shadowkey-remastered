// M129 milestone smoke test: multiplayer.
//
//   1. The wire format against docs/MULTIPLAYER_PROTOCOL.md -- all 43 tags,
//      each at the catalogue's size, fields at the catalogue's offsets.
//   2. A whole session between two scripted worlds over an in-memory link:
//      the join sequence in the catalogue's order, the entity stream at 50 a
//      frame, the two timers, the pause lock, every event reaching the
//      other world, a travel request, and a dropped link.
//   3. The socket transport on loopback: hosting, LAN discovery, connecting,
//      packets arriving whole and in order.
//   4. The script side: IsMultiplayer / IsMultiplayerClient answer from the
//      session's state, and stay false in single player.
//
// Needs no game data.

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "net/mp_protocol.h"
#include "net/mp_session.h"
#include "net/mp_transport.h"
#include "simkin_bindings/mp_hooks.h"
#include "simkin_bindings/native_binding_common.h"
#include "skRValue.h"
#include "skRValueArray.h"

namespace {

int g_Failures = 0;
int g_Checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_Checks;
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what.c_str());
    if (!ok) ++g_Failures;
}

using namespace sk_net;

// A world that records what the session tells it.
struct ScriptedWorld : MpWorld {
    std::string label;
    std::vector<std::string> log;
    MpPlayerState me;
    std::vector<MpEntityRecord> entities;  // what a host streams
    std::vector<MpEntityRecord> received;
    MpStoryState receivedStory;
    bool haveStory = false;
    MpPlaceAndStart place;
    bool placed = false;
    MpPlayerState peer;
    int peerStates = 0;
    MpTravel travel;
    int unapplied = 0;

    int LevelIndex(const std::string& name) override { return name == "crypt1" ? 3 : 0; }
    std::string LevelName(int index) override { return index == 3 ? "crypt1" : "azra"; }
    MpPlayerState LocalPlayerState() override { return me; }
    MpPlayerInfo LocalPlayerInfo() override {
        MpPlayerInfo info;
        info.name = label;
        info.race = label == "host" ? 5 : 2;
        info.sex = label == "host" ? 0 : 1;
        info.classId = 7;
        return info;
    }
    int LocalHeldWeapon() override { return 4711; }
    MpStoryState BuildStoryState() override {
        MpStoryState story;
        story.levelRecord = {1, 2, 3, 4, 5};
        story.assigned[26] = 1;
        story.solved[33] = 1;
        story.completed[240] = 1;
        story.savedInts[17] = 99;
        return story;
    }
    std::vector<MpEntityRecord> BuildEntityStream() override { return entities; }

    void OnGameStart(const std::string& level) override { log.push_back("start:" + level); }
    void OnStoryState(const MpStoryState& story) override {
        receivedStory = story;
        haveStory = true;
        log.push_back("story");
    }
    void OnSpawnEntity(const MpEntityRecord& e) override { received.push_back(e); }
    void OnPlaceAndStart(const MpPlaceAndStart& p) override {
        place = p;
        placed = true;
        log.push_back("place");
    }
    void OnPeerInGame() override { log.push_back("peer-in-game"); }
    void OnPlayerLeft(int index, const std::string& name) override {
        log.push_back("left:" + std::to_string(index) + ":" + name);
    }
    void OnTravelRequest(const MpTravel& t) override {
        travel = t;
        log.push_back("travel");
    }
    void OnPlayerState(const MpPlayerState& s) override {
        peer = s;
        ++peerStates;
    }
    void OnPlayerInfo(const MpPlayerInfo& i) override { log.push_back("info:" + i.name); }
    void OnHeldWeapon(int, int w) override { log.push_back("weapon:" + std::to_string(w)); }
    void OnAttackVisual(int, int a, int s) override {
        log.push_back("attack:" + std::to_string(a) + ":" + std::to_string(s));
    }
    void OnAnimation(int a, int) override { log.push_back("anim:" + std::to_string(a)); }
    void OnTextMessage(int, const std::string& t) override { log.push_back("text:" + t); }
    void OnDamageMe(int a) override { log.push_back("damage:" + std::to_string(a)); }
    void OnKill(int id) override { log.push_back("kill:" + std::to_string(id)); }
    void OnHitForZero(int id) override { log.push_back("hit:" + std::to_string(id)); }
    void OnTouched(int id) override { log.push_back("touched:" + std::to_string(id)); }
    void OnCreatureTarget(int id, int p) override {
        log.push_back("target:" + std::to_string(id) + ":" + std::to_string(p));
    }
    void OnDoor(int id, bool close) override {
        log.push_back("door:" + std::to_string(id) + (close ? ":close" : ":open"));
    }
    void OnDestroyObject(int id) override { log.push_back("destroy:" + std::to_string(id)); }
    void OnSetPosition(const MpSetPosition& p) override {
        log.push_back(std::string("setpos:") + (p.everyone ? "all" : std::to_string(p.entityId)) + ":" +
                      std::to_string(p.x) + "," + std::to_string(p.y) + "," + std::to_string(p.z));
    }
    void OnMirrorMethod(int id, const std::string& m) override {
        log.push_back("mirror:" + std::to_string(id) + ":" + m);
    }
    void OnInvulnerable(int id, bool f) override {
        log.push_back("invulnerable:" + std::to_string(id) + (f ? ":1" : ":0"));
    }
    void OnQuestFlag(int k, int q, int v) override {
        log.push_back("quest:" + std::to_string(k) + ":" + std::to_string(q) + ":" + std::to_string(v));
    }
    void OnShareXp(int k, int v) override {
        log.push_back("xp:" + std::to_string(k) + ":" + std::to_string(v));
    }
    void OnLevelVar(const std::string& n, int v) override {
        log.push_back("var:" + n + "=" + std::to_string(v));
    }
    void OnUnapplied(uint8_t, const MpBytes&) override { ++unapplied; }

    bool Saw(const std::string& entry) const {
        for (const std::string& e : log) {
            if (e == entry) return true;
        }
        return false;
    }
    int IndexOf(const std::string& entry) const {
        for (size_t i = 0; i < log.size(); ++i) {
            if (log[i] == entry) return static_cast<int>(i);
        }
        return -1;
    }
};

}  // namespace

int main() {
    // ---------------------------------------------------------------
    std::printf("== Part 1: the wire format ==\n");
    // ---------------------------------------------------------------
    {
        Check(MpTagCount() == 43, "the catalogue has 43 tags (" + std::to_string(MpTagCount()) + ")");
        Check(MpTagInfoFor(0x20) == nullptr && MpTagInfoFor(0x36) == nullptr &&
                  MpTagInfoFor(0x3e) == nullptr,
              "0x20 is unused, and nothing lies past 0x3d");
        int variable = 0;
        bool padded = true, ordered = true;
        for (int i = 0; i < MpTagCount(); ++i) {
            const MpTagInfo& info = MpTagAt(i);
            if (info.size == 0) ++variable;
            if (info.size % 4 != 0) padded = false;
            if (i > 0 && MpTagAt(i - 1).tag >= info.tag) ordered = false;
        }
        Check(variable == 3 && MpTagInfoFor(0x1d)->size == 0 && MpTagInfoFor(0x29)->size == 0 &&
                  MpTagInfoFor(0x32)->size == 0,
              "three are streams: 0x1d, 0x29, 0x32");
        Check(padded && ordered, "every fixed message is padded to 4 bytes");

        MpGameStart start;
        start.levelIndex = 3;
        start.playerCount = 2;
        start.names[0] = "Azra";
        start.names[1] = "Tanyin";
        const MpBytes startBytes = start.Encode();
        const MpGameStart startBack = MpGameStart::Decode(startBytes);
        Check(startBytes.size() == 0x88 && startBytes[0] == 0x00 && startBytes[4] == 3 &&
                  startBytes[5] == 1 && startBytes[6] == 'A' && startBytes[7] == 0 &&
                  startBytes[6 + 0x40] == 'T',
              "0x00 is 0x88 bytes: level at +4, count - 1 at +5, two 0x40-byte wide names at +6");
        Check(startBack.levelIndex == 3 && startBack.playerCount == 2 &&
                  startBack.names[0] == "Azra" && startBack.names[1] == "Tanyin",
              "and reads back");

        MpPlayerState st;
        st.index = 1;
        st.x = 30346;
        st.y = -11904;
        st.vx = 1408;
        st.vy = -77;
        st.z = -512;
        st.vz = 0x500;
        st.heading = static_cast<int16_t>(0xc000);
        st.pitch = -300;
        const MpBytes stBytes = st.Encode();
        MpReader r(stBytes);
        Check(stBytes.size() == 0x20 && stBytes[0] == 0x01 && r.U8(4) == 1 && r.I32(8) == 30346 &&
                  r.I32(0xc) == -11904 && r.I32(0x10) == 1408 && r.I32(0x14) == -77 &&
                  r.I16(0x18) == -512 && r.I16(0x1a) == 0x500 && r.U16(0x1c) == 0xc000 &&
                  r.I16(0x1e) == -300,
              "0x01 is 0x20 bytes: index, x, y, vx, vy, z, vz, heading, pitch at their offsets");
        Check(MpPlayerState::Decode(stBytes) == st, "and reads back");

        MpPlayerInfo info;
        info.index = 1;
        info.classId = 7;
        info.race = 5;
        info.sex = 1;
        info.name = "Caf\xe9";
        const MpBytes infoBytes = info.Encode();
        const MpPlayerInfo infoBack = MpPlayerInfo::Decode(infoBytes);
        Check(infoBytes.size() == 0x108 && infoBytes[5] == 7 && infoBytes[6] == 5 && infoBytes[7] == 1 &&
                  infoBack.name == "Caf\xe9",
              "0x22 is 0x108 bytes: class, race, sex at +5..+7, a Latin-1 name widened at +8");

        struct {
            const char* what;
            MpBytes bytes;
            uint8_t tag;
        } sized[] = {
            {"0x06", MpPlaceAndStart{}.Encode(), 0x06},
            {"0x07", EncodeTagOnly(kTagReady), 0x07},
            {"0x14", EncodeIndexed(kTagPlayerLeft, 1), 0x14},
            {"0x15", EncodePauseLock(1, true), 0x15},
            {"0x17", EncodeIndexed(kTagYourIndex, 1), 0x17},
            {"0x18", EncodeAttackVisual(1, 2, 2), 0x18},
            {"0x19", EncodeEntityId(kTagKill, 77), 0x19},
            {"0x1a", EncodeHitForZero(77), 0x1a},
            {"0x1b", EncodeEntityId(kTagTouched, 77), 0x1b},
            {"0x1e", EncodeCreatureTarget(77, 1), 0x1e},
            {"0x1f", EncodeDoor(77, true), 0x1f},
            {"0x21", EncodeQuestFlag(2, 33, 1), 0x21},
            {"0x23", EncodeHeldWeapon(1, 4711), 0x23},
            {"0x24", EncodeText(1, "hello"), 0x24},
            {"0x28", EncodeShareXp(0, 250), 0x28},
            {"0x2a", EncodeAnimation(6, 1), 0x2a},
            {"0x2b", EncodeEntityId(kTagDestroyObject, 77), 0x2b},
            {"0x2c", MpLevelVar{}.Encode(), 0x2c},
            {"0x2d", MpLevelVar{}.Encode(kTagLevelVarMenu), 0x2d},
            {"0x2e", MpTravel{}.Encode(), 0x2e},
            {"0x2f", EncodeEntityId(kTagNoticed, 77), 0x2f},
            {"0x30", EncodeTagOnly(kTagLoaded), 0x30},
            {"0x31", EncodeTaken(77, 4702), 0x31},
            {"0x33", MpSetPosition{}.Encode(), 0x33},
            {"0x34", MpMirrorMethod{}.Encode(), 0x34},
            {"0x35", EncodeInvulnerable(77, true), 0x35},
            {"0x25", EncodeSpellProjectile(true, 1, 5, 100, 4001, -300), 0x25},  // M132
            {"0x27", EncodeAttachedEffect(false, 77, 0x3e, 0x44, 6), 0x27},
            {"0x37", EncodeUmbraBeat(77, true), 0x37},
            {"0x38", EncodeRegistryFlag(77, 0x10, true), 0x38},
            {"0x39", EncodeAreaSpell(77, 0, 0, 1), 0x39},
            {"0x3a", EncodeSetOnFire(77, 3), 0x3a},
            {"0x3b", EncodeEntityId(kTagAlerted, 77), 0x3b},
            {"0x3c", EncodeTagOnly(kTagKeepalive), 0x3c},
            {"0x3d", EncodeDamageMe(40), 0x3d},
        };
        bool sizes = true;
        for (const auto& s : sized) {
            const MpTagInfo* tagInfo = MpTagInfoFor(s.tag);
            if (!tagInfo || s.bytes.size() != tagInfo->size || s.bytes[0] != s.tag) {
                sizes = false;
                std::printf("    %s: %zu bytes\n", s.what, s.bytes.size());
            }
        }
        Check(sizes, "every encoder writes its tag at byte 0 and exactly the catalogue's size");
        {
            // M132: the four that were decoded but never built.
            const MpBytes shot = EncodeSpellProjectile(true, 1, 5, 100, 4001, -300);
            const MpReader sr(shot);
            Check(sr.U8(4) == 1 && sr.I32(8) == 1 && sr.I16(0xc) == 5 && sr.I16(0xe) == 0x100 &&
                      sr.I16(0x10) == 100 && sr.I32(0x14) == 4001 && sr.I16(0x18) == -300,
                  "0x25 carries owner kind, id, sprite, 0x100, speed, spell and pitch");
            const MpBytes flame = EncodeAttachedEffect(false, 77, 0x3e, 0x44, 6);
            const MpReader fr(flame);
            Check(fr.U8(4) == 0 && fr.I32(8) == 77 && fr.I32(0xc) == 0x3e && fr.I32(0x10) == 0x44 &&
                      fr.I32(0x14) == 6,
                  "0x27 carries the actor and FUN_10067f84's three arguments");
            const MpBytes beat = EncodeUmbraBeat(77, true);
            Check(MpReader(beat).I32(4) == 77 && MpReader(beat).U8(8) == 1,
                  "0x37 carries the entity and its SetCanTeleport flag");
        }

        const MpBytes kill = EncodeEntityId(kTagKill, 0x1234);
        const MpBytes door = EncodeDoor(0x55, true);
        const MpBytes quest = EncodeQuestFlag(2, 33, 1);
        Check(MpReader(kill).U16(8) == 0x1234 && MpReader(door).U16(8) == 0x55 && door[5] == 1 &&
                  quest[4] == 2 && quest[5] == 33 && quest[6] == 1,
              "0x19 names its entity at +8; 0x1f has the id at +8 and close at +5; 0x21 is kind, quest, value");

        MpStoryState story;
        story.levelRecord = {9, 8, 7};
        story.assigned[1] = 1;
        story.completed[255] = 1;
        story.solved[100] = 1;
        story.savedInts[0] = -5;
        story.savedInts[17] = 123456;
        MpStoryState storyBack;
        const MpBytes storyBytes = story.Encode();
        Check(MpStoryState::Decode(storyBytes, &storyBack) && storyBack.levelRecord == story.levelRecord &&
                  storyBack.assigned[1] == 1 && storyBack.completed[255] == 1 &&
                  storyBack.solved[100] == 1 && storyBack.savedInts[0] == -5 &&
                  storyBack.savedInts[17] == 123456,
              "0x29 carries the level record, 256 x 3 quest bytes and the 18 saved ints");
        MpBytes truncated(storyBytes.begin(), storyBytes.begin() + 40);
        Check(!MpStoryState::Decode(truncated, &storyBack), "a truncated 0x29 is refused");

        int typeId = 0, entityId = 0;
        MpBytes chain;
        const MpBytes stream = EncodeEntityStream(kTagSpawnEntity, 202, 0x8001, {0xaa, 0xbb});
        Check(DecodeEntityStream(stream, &typeId, &entityId, &chain) && typeId == 202 &&
                  entityId == 0x8001 && chain == MpBytes({0xaa, 0xbb}) && stream[0] == 0x1d,
              "0x1d is tag, typeId, entity id, then the class's save chain");
    }

    // ---------------------------------------------------------------
    std::printf("\n== Part 2: a session between two worlds ==\n");
    // ---------------------------------------------------------------
    {
        ScriptedWorld hostWorld, clientWorld;
        hostWorld.label = "host";
        clientWorld.label = "client";
        hostWorld.me.x = 1000;
        hostWorld.me.y = 2000;
        hostWorld.me.z = 64;
        hostWorld.me.heading = 0x4000;
        for (int i = 0; i < 120; ++i) {
            MpEntityRecord rec;
            rec.typeId = 200 + (i % 5);
            rec.entityId = i + 1;
            rec.chain = {static_cast<uint8_t>(i), 1, 2, 3};
            hostWorld.entities.push_back(rec);
        }
        MpSession host(hostWorld), client(clientWorld);
        MpMemoryPair pair = MakeMemoryLinkPair();
        host.AdoptLink(std::move(pair.a), /*asHost=*/true, "host");
        client.AdoptLink(std::move(pair.b), /*asHost=*/false, "client");
        auto pump = [&](int ticks = 1) {
            for (int i = 0; i < ticks; ++i) {
                host.Tick(10);
                client.Tick(10);
            }
        };
        pump(2);
        Check(client.myIndex() == 1 && host.myIndex() == 0 && host.playerName(1) == "client",
              "in the lobby: the client learns its index (0x17), the host the client's name (0x22)");
        Check(!host.active() && !client.active(), "nobody is in a multiplayer game yet");

        Check(host.HostPlay("crypt1") && host.state() == MpSession::State::Starting,
              "MPHostPlay: the host announces the level (0x00)");
        pump(1);
        Check(clientWorld.Saw("start:crypt1") && client.state() == MpSession::State::Starting &&
                  client.isClient() && client.playerName(0) == "host",
              "the client is told the level by index and is now a multiplayer client");
        // The client's 0x07 has arrived, but the host's level is not up.
        pump(2);
        Check(clientWorld.received.empty() && !clientWorld.haveStory,
              "nothing is streamed before the host's own level is loaded");

        host.NotifyLevelLoaded();
        pump(1);
        Check(clientWorld.haveStory && clientWorld.receivedStory.assigned[26] == 1 &&
                  clientWorld.receivedStory.solved[33] == 1 &&
                  clientWorld.receivedStory.completed[240] == 1 &&
                  clientWorld.receivedStory.savedInts[17] == 99 &&
                  clientWorld.receivedStory.levelRecord == MpBytes({1, 2, 3, 4, 5}),
              "then the host's story arrives (0x29): level record, quests, saved ints");
        Check(clientWorld.received.size() == 50,
              "and the entity stream starts at 50 a frame (" +
                  std::to_string(clientWorld.received.size()) + " after one)");
        pump(1);
        Check(clientWorld.received.size() == 100 && !clientWorld.placed,
              "100 after two, and the client is not placed yet");
        pump(1);
        bool streamOk = clientWorld.received.size() == 120;
        for (size_t i = 0; streamOk && i < clientWorld.received.size(); ++i) {
            const MpEntityRecord& got = clientWorld.received[i];
            const MpEntityRecord& want = hostWorld.entities[i];
            if (got.typeId != want.typeId || got.entityId != want.entityId || got.chain != want.chain) {
                streamOk = false;
            }
        }
        Check(streamOk, "all 120 entities arrive in order, type, id and save chain intact");
        Check(clientWorld.placed && clientWorld.place.x == 1000 && clientWorld.place.y == 2000 &&
                  clientWorld.place.z == 64 && clientWorld.place.heading == 0x4000,
              "the stream ends with 0x06: the client is placed where the host stands");
        Check(clientWorld.IndexOf("story") < clientWorld.IndexOf("place") &&
                  clientWorld.Saw("info:host") && clientWorld.Saw("weapon:4711") &&
                  clientWorld.peerStates >= 1,
              "followed by the host's own 0x01 / 0x22 / 0x23");
        Check(host.inGame() && !client.inGame(), "the host is in the game; the client is still loading");

        client.NotifyLevelLoaded();
        pump(1);
        Check(client.inGame() && hostWorld.Saw("peer-in-game") && hostWorld.Saw("info:client") &&
                  hostWorld.peerStates >= 1,
              "the client loads, answers with its own state and 0x30, and both are in the game");

        // ---- the two timers ----
        const int sentBefore = host.counters().sent;
        pump(40);  // 400 units, nothing moved
        Check(host.counters().sent == sentBefore,
              "a player who has not moved sends no 0x01 (" +
                  std::to_string(host.counters().sent - sentBefore) + " packets in 400 units)");
        const int statesBefore = clientWorld.peerStates;
        hostWorld.me.x = 1500;
        hostWorld.me.vx = 1408;
        pump(19);  // one 0xc0 window (192 units) at most
        Check(clientWorld.peerStates == statesBefore + 1 && clientWorld.peer.x == 1500 &&
                  clientWorld.peer.vx == 1408 && clientWorld.peer.index == 0,
              "a move goes out once, within 0xc0 units (0.75 s), with the velocity to coast on");
        const int receivedBefore = client.counters().received;
        pump(260);  // 2600 units > 0xa00
        Check(client.counters().received - receivedBefore == 1,
              "the keepalive (0x3c) is one packet every 0xa00 units (10 s)");

        // ---- the pause lock ----
        host.SetPauseLock(true);
        pump(1);
        Check(client.peerHoldsPause() && !host.peerHoldsPause(),
              "a modal screen on the host stops the client's frame (0x15)");
        client.SetPauseLock(true);
        pump(1);
        Check(!host.peerHoldsPause(), "the lock has one holder: the client cannot take it too");
        host.SetPauseLock(false);
        pump(1);
        Check(!client.peerHoldsPause(), "and closing the screen releases it");

        // ---- events ----
        host.Send(EncodeEntityId(kTagKill, 17));
        host.Send(EncodeHitForZero(18));
        host.Send(EncodeDoor(40, false));
        host.Send(EncodeDoor(41, true));
        host.Send(EncodeEntityId(kTagDestroyObject, 55));
        host.Send(EncodeEntityId(kTagTouched, 56));
        host.Send(EncodeQuestFlag(0, 26, 1));
        host.Send(EncodeShareXp(0, 250));
        host.Send(EncodeAttackVisual(0, 2, 2));
        host.Send(EncodeAnimation(6, 0));
        host.Send(EncodeText(0, "host has fallen."));
        host.Send(EncodeCreatureTarget(17, 1));
        host.Send(EncodeInvulnerable(17, true));
        host.Send(EncodeDamageMe(40));
        MpMirrorMethod mirror;
        mirror.entityId = 9;
        mirror.method = "ClientShowCrystal1";
        host.Send(mirror.Encode());
        MpSetPosition pos;
        pos.everyone = true;
        pos.x = 4096;
        pos.y = 8192;
        pos.z = -64;
        host.Send(pos.Encode());
        MpLevelVar var;
        var.name = "saved_Dawn";
        var.value = 2;
        host.Send(var.Encode());
        pump(1);
        const char* expected[] = {"kill:17", "hit:18", "door:40:open", "door:41:close", "destroy:55",
                                  "touched:56", "quest:0:26:1", "xp:0:250", "attack:2:2", "anim:6",
                                  "text:host has fallen.", "target:17:1", "invulnerable:17:1",
                                  "damage:40", "mirror:9:ClientShowCrystal1",
                                  "setpos:all:4096,8192,-64", "var:saved_Dawn=2"};
        bool allEvents = true;
        for (const char* e : expected) {
            if (!clientWorld.Saw(e)) {
                allEvents = false;
                std::printf("    missing: %s\n", e);
            }
        }
        Check(allEvents, "seventeen events each reach the other world decoded");

        // The six tags this port decodes but does not apply are counted.
        host.Send(MpWriter(kTagRoomEvent, 0xc).Take());
        host.Send(MpWriter(kTagAreaSpell, 0x14).Take());
        pump(1);
        Check(clientWorld.unapplied == 2 && client.counters().unapplied == 2,
              "a tag the port does not apply yet is reported, not dropped silently");
        // A packet of the wrong size never reaches the world.
        const size_t logBefore = clientWorld.log.size();
        host.Send(MpBytes{kTagKill, 0, 0});
        host.Send(MpBytes{0x20, 0, 0, 0});
        pump(1);
        Check(client.counters().malformed == 2 && clientWorld.log.size() == logBefore,
              "a wrong-sized packet and an unknown tag are refused");

        // ---- travel ----
        MpTravel travel;
        travel.levelIndex = 3;
        travel.overrideArmed = true;
        travel.spawn[0] = 111;
        travel.spawn[3] = 0x8000;
        client.RequestTravel(travel);
        pump(1);
        Check(hostWorld.Saw("travel") && hostWorld.travel.levelIndex == 3 &&
                  hostWorld.travel.overrideArmed && hostWorld.travel.spawn[0] == 111 &&
                  hostWorld.travel.spawn[3] == 0x8000,
              "a client's door is a request to the host (0x2e), spawn override included");
        clientWorld.log.clear();
        clientWorld.received.clear();
        clientWorld.placed = false;
        host.HostAnnounceLevel("azra");
        pump(1);
        Check(clientWorld.Saw("start:azra") && !client.inGame() && host.state() == MpSession::State::Starting,
              "the host announces the new level and both go back to loading");
        host.NotifyLevelLoaded();
        pump(4);
        client.NotifyLevelLoaded();
        pump(1);
        Check(clientWorld.placed && clientWorld.received.size() == 120 && client.inGame() && host.inGame(),
              "and the whole sequence runs again for the new level");

        // ---- the other player leaves ----
        client.Close();
        pump(1);
        Check(hostWorld.Saw("left:1:client") && !host.active() && host.state() == MpSession::State::Idle,
              "when the client leaves (0x14) the host is told who, and plays on alone");
    }
    {
        // A link that simply dies, with no goodbye.
        ScriptedWorld hostWorld, clientWorld;
        hostWorld.label = "host";
        clientWorld.label = "client";
        MpSession host(hostWorld), client(clientWorld);
        MpMemoryPair pair = MakeMemoryLinkPair();
        MpLink* clientEnd = pair.b.get();
        host.AdoptLink(std::move(pair.a), true, "host");
        client.AdoptLink(std::move(pair.b), false, "client");
        host.Tick(10);
        client.Tick(10);
        host.HostPlay("azra");
        host.NotifyLevelLoaded();
        for (int i = 0; i < 4; ++i) {
            host.Tick(10);
            client.Tick(10);
        }
        client.NotifyLevelLoaded();
        host.Tick(10);
        clientEnd->Close();
        host.Tick(10);
        Check(hostWorld.Saw("left:1:client") && !host.active(),
              "a link that dies without a goodbye is the same: the game goes on alone");
        // Hosting alone starts at once.
        ScriptedWorld soloWorld;
        MpSession solo(soloWorld);
        Check(!solo.HostPlay("azra"), "MPHostPlay outside the host lobby does nothing");
    }

    // ---------------------------------------------------------------
    std::printf("\n== Part 3: the socket transport, on loopback ==\n");
    // ---------------------------------------------------------------
    {
        MpLobby hostLobby, clientLobby;
        const bool hosting = hostLobby.Host("Azra's game");
        if (!hosting) {
            // Another copy of the game (or of this test) holds the port.
            std::printf("  (port %d unavailable: %s -- socket checks skipped)\n", kMpGamePort,
                        hostLobby.lastError().c_str());
        } else {
            MpLobby second;
            Check(!second.Host("another") && !second.lastError().empty(),
                  "a second host on the same port is refused with a reason");
            Check(clientLobby.BeginDiscovery(), "the client starts looking for hosts");
            bool found = false;
            for (int i = 0; i < 100 && !found; ++i) {
                hostLobby.Poll();
                clientLobby.Poll();
                found = !clientLobby.hosts().empty();
                if (!found) std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            Check(found && clientLobby.hosts()[0].name == "Azra's game" &&
                      clientLobby.hosts()[0].port == kMpGamePort,
                  "the host answers the probe with its name and port" +
                      (found ? " (" + clientLobby.hosts()[0].address + ")" : std::string()));
            std::unique_ptr<MpLink> clientLink, hostLink;
            if (found) {
                MpHostEntry local = clientLobby.hosts()[0];
                local.address = "127.0.0.1";
                clientLink = clientLobby.Connect(local);
            }
            for (int i = 0; i < 100 && clientLink && !hostLink; ++i) {
                hostLink = hostLobby.Accept();
                if (!hostLink) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            Check(clientLink && hostLink && clientLink->connected() && hostLink->connected(),
                  "the client connects and the host accepts");
            if (clientLink && hostLink) {
                // Small packets, one large one, in order.
                MpBytes big(200000);
                for (size_t i = 0; i < big.size(); ++i) big[i] = static_cast<uint8_t>(i * 31);
                clientLink->Send(EncodeEntityId(kTagKill, 7));
                clientLink->Send(big);
                clientLink->Send(EncodeTagOnly(kTagKeepalive));
                std::vector<MpBytes> got;
                for (int i = 0; i < 300 && got.size() < 3; ++i) {
                    MpBytes packet;
                    while (hostLink->Receive(packet)) got.push_back(packet);
                    clientLink->Receive(packet);  // lets the sender flush
                    if (got.size() < 3) std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                Check(got.size() == 3 && got[0] == EncodeEntityId(kTagKill, 7) && got[1] == big &&
                          got[2] == EncodeTagOnly(kTagKeepalive),
                      "three packets arrive whole and in order, a 200 KB one among them");
                hostLink->Send(EncodeTagOnly(kTagReady));
                MpBytes back;
                bool answered = false;
                for (int i = 0; i < 200 && !answered; ++i) {
                    answered = clientLink->Receive(back);
                    if (!answered) std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                Check(answered && back == EncodeTagOnly(kTagReady), "and the other way");
                hostLink->Close();
                bool noticed = false;
                for (int i = 0; i < 200 && !noticed; ++i) {
                    clientLink->Receive(back);
                    noticed = !clientLink->connected();
                    if (!noticed) std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                Check(noticed, "closing one end is seen at the other");
            }
            MpLobby nowhere;
            MpHostEntry dead;
            dead.address = "127.0.0.1";
            dead.port = 1;  // nothing listens here
            Check(nowhere.Connect(dead, 300) == nullptr, "connecting to nobody fails, in bounded time");
        }
        Check(!MpLocalMachineName().empty(), "this machine has a name for the player-name row");
    }

    // ---------------------------------------------------------------
    std::printf("\n== Part 4: what the scripts are told ==\n");
    // ---------------------------------------------------------------
    {
        sk_bindings::MpHooks& hooks = sk_bindings::Mp();
        auto ask = [](const char* name) {
            skRValueArray args;
            skRValue ret;
            const bool handled = sk_bindings::TryHandleMultiplayerQuery(skString(name), args, ret);
            return handled && ret.boolValue();
        };
        Check(!ask("IsMultiplayer") && !ask("IsMultiplayerClient"),
              "single player: IsMultiplayer() and IsMultiplayerClient() are both false");
        hooks.active = true;
        hooks.client = false;
        Check(ask("IsMultiplayer") && !ask("IsMultiplayerClient"),
              "hosting: IsMultiplayer() is true, IsMultiplayerClient() false");
        hooks.client = true;
        Check(ask("IsMultiplayer") && ask("IsMultiplayerClient"),
              "joined: both true -- the gate the scripts put around host-only spawns");
        Check(hooks.sending(), "an event is sent while a game is on");
        {
            sk_bindings::MpRemoteScope remote;
            Check(!hooks.sending(), "but not while an event from the other player is being applied");
        }
        Check(hooks.sending(), "and again afterwards");
        hooks.active = false;
        hooks.client = false;
    }

    std::printf("\nm129_multiplayer_smoke: %d/%d checks passed\n", g_Checks - g_Failures, g_Checks);
    return g_Failures == 0 ? 0 : 1;
}
