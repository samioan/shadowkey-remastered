#pragma once
#include <memory>
#include <string>
#include <vector>

#include "net/mp_protocol.h"
#include "net/mp_transport.h"

// M129: the multiplayer session -- the port's counterpart of the object at
// `engine+0x5d0` (vtable 0x100fde1c) and the comms wrapper at `engine+0x5cc`.
//
// The model is the original's, unchanged (docs/MULTIPLAYER_PROTOCOL.md):
// two players, host and client, each running the whole game; nothing is
// streamed but each player's own movement, and everything else is an event
// the other copy applies with its own engine. This class owns the part of
// that which is not game logic:
//
//   * the lobby (hosting, finding a host, connecting);
//   * the join sequence -- 0x17, 0x00, 0x07, 0x29, the 0x1d stream, 0x07,
//     0x06, 0x30 -- in the order the catalogue gives it, including the
//     50-entities-a-frame pace of the stream;
//   * the two timers: 0x01 at most every 0xc0 units (0.75 s) and only when
//     the player's state changed, 0x3c every 0xa00 units (10 s);
//   * the pause lock (0x15);
//   * receiving: every tag is decoded here and handed to MpWorld, which is
//     the game (main.cpp).
//
// It never touches game state itself, so it runs against a scripted MpWorld
// in the test exactly as it runs against the game.

namespace sk_net {

// One entity of the host's world, as the 0x1d stream carries it.
struct MpEntityRecord {
    int typeId = 0;
    int entityId = 0;
    MpBytes chain;  // the class's save chain (a SavedEntity record)
};

// What the session needs from the game, and what it tells it. Every
// receiver has an empty default so a test implements only what it checks.
class MpWorld {
public:
    virtual ~MpWorld() = default;

    // ---- asked of the game ----
    virtual int LevelIndex(const std::string& /*levelName*/) { return 0; }
    virtual std::string LevelName(int /*levelIndex*/) { return std::string(); }
    // The local player, for 0x01 / 0x22 / 0x23. `index` is filled in by the
    // session.
    virtual MpPlayerState LocalPlayerState() { return MpPlayerState{}; }
    virtual MpPlayerInfo LocalPlayerInfo() { return MpPlayerInfo{}; }
    virtual int LocalHeldWeapon() { return 0; }
    // Host, when the client is ready: its story and every entity it has.
    virtual MpStoryState BuildStoryState() { return MpStoryState{}; }
    virtual std::vector<MpEntityRecord> BuildEntityStream() { return {}; }

    // ---- session events ----
    // Client: the host started (or travelled to) `levelName`. The level is
    // not loaded yet -- the host's world arrives first.
    virtual void OnGameStart(const std::string& /*levelName*/) {}
    virtual void OnStoryState(const MpStoryState& /*story*/) {}
    virtual void OnSpawnEntity(const MpEntityRecord& /*entity*/) {}
    // Client: the stream is complete; load the level and stand here.
    virtual void OnPlaceAndStart(const MpPlaceAndStart& /*place*/) {}
    // The other player is in the level with us from now on.
    virtual void OnPeerInGame() {}
    virtual void OnPlayerLeft(int /*index*/, const std::string& /*name*/) {}
    // Host: the client asked to travel (0x2e).
    virtual void OnTravelRequest(const MpTravel& /*travel*/) {}

    // ---- the other player ----
    virtual void OnPlayerState(const MpPlayerState& /*state*/) {}
    virtual void OnPlayerInfo(const MpPlayerInfo& /*info*/) {}
    virtual void OnHeldWeapon(int /*index*/, int /*weapon*/) {}
    virtual void OnAttackVisual(int /*index*/, int /*anim*/, int /*sound*/) {}
    virtual void OnAnimation(int /*anim*/, int /*index*/) {}
    virtual void OnTextMessage(int /*index*/, const std::string& /*text*/) {}
    virtual void OnDamageMe(int /*amount*/) {}

    // ---- creatures, objects, the world ----
    virtual void OnKill(int /*entityId*/) {}
    virtual void OnHitForZero(int /*entityId*/) {}
    virtual void OnTouched(int /*entityId*/) {}
    virtual void OnCreatureTarget(int /*entityId*/, int /*playerIndex*/) {}
    virtual void OnDoor(int /*entityId*/, bool /*close*/) {}
    virtual void OnDestroyObject(int /*entityId*/) {}
    virtual void OnNoticed(int /*entityId*/) {}
    virtual void OnTakenFromContainer(int /*containerId*/, int /*itemTypeId*/) {}
    virtual void OnHandOver(const MpEntityRecord& /*entity*/) {}
    virtual void OnSetPosition(const MpSetPosition& /*position*/) {}
    virtual void OnMirrorMethod(int /*entityId*/, const std::string& /*method*/) {}
    virtual void OnInvulnerable(int /*entityId*/, bool /*flag*/) {}
    virtual void OnSetOnFire(int /*entityId*/, int /*level*/) {}
    virtual void OnAlerted(int /*entityId*/) {}
    virtual void OnQuestFlag(int /*kind*/, int /*quest*/, int /*value*/) {}
    virtual void OnShareXp(int /*kind*/, int /*value*/) {}
    virtual void OnLevelVar(const std::string& /*name*/, int /*value*/) {}
    virtual void OnRegistryFlag(int /*entityId*/, int /*mask*/, bool /*set*/) {}
    // A tag the catalogue has and this port decodes but does not apply yet
    // (0x1c, 0x25, 0x26, 0x27, 0x37, 0x39). Counted, never silently lost.
    virtual void OnUnapplied(uint8_t /*tag*/, const MpBytes& /*packet*/) {}
};

class MpSession {
public:
    enum class State {
        Idle,       // single player
        HostLobby,  // hosting, waiting in the host menu
        JoinLobby,  // looking for hosts / connected, waiting for the host
        Starting,   // 0x00 is out; levels are loading, the world is in flight
        InGame,
    };

    // The engine's two timers, in its 1/256 s units.
    static constexpr int kMoveInterval = 0xc0;
    static constexpr int kKeepaliveInterval = 0xa00;
    static constexpr int kEntitiesPerFrame = 50;

    explicit MpSession(MpWorld& world);
    ~MpSession();

    State state() const { return state_; }
    bool active() const { return state_ == State::Starting || state_ == State::InGame; }
    bool inGame() const { return state_ == State::InGame; }
    bool isHost() const { return host_; }
    bool isClient() const { return active() && !host_; }
    int myIndex() const { return myIndex_; }
    int peerIndex() const { return myIndex_ == 0 ? 1 : 0; }
    bool peerConnected() const { return link_ && link_->connected(); }
    const std::string& playerName(int index) const { return names_[index & 1]; }
    const std::string& lastError() const { return error_; }
    MpLobby& lobby() { return lobby_; }

    // ---- the lobby (the menu natives) ----
    bool BeginHost(const std::string& myName);            // MPBeginHost
    bool BeginJoin(const std::string& myName);            // MPBeginJoin
    bool JoinHost(int hostIndex);                         // JoinGame(n)
    void RejectClient();                                  // RejectClient
    // MPHostPlay: starts the game in `levelName`. The game then loads it and
    // calls NotifyLevelLoaded().
    bool HostPlay(const std::string& levelName);
    // CloseMPHostMenu / CloseMPJoinMenu, a quit to the menu, a lost link.
    void Close();
    // For tests: an already-connected link instead of the lobby's socket.
    void AdoptLink(std::unique_ptr<MpLink> link, bool asHost, const std::string& myName);

    // ---- in the game ----
    // The level named in the last 0x00 is loaded and the local player is in
    // it. Host: starts the story + entity stream once the client is ready.
    // Client: answers 0x06 with its own state and 0x30.
    void NotifyLevelLoaded();
    // Host: the game is about to load another level (a door, a script).
    void HostAnnounceLevel(const std::string& levelName);
    // Client: asks the host to travel instead of loading by itself.
    void RequestTravel(const MpTravel& travel);
    // The pause lock: this player opened (true) or closed a modal screen.
    void SetPauseLock(bool held);
    // True while the *other* player holds it: the local game frame waits.
    bool peerHoldsPause() const { return pauseHolder_ == peerIndex(); }
    // Sends one packet to the other player, if there is one in the game.
    bool Send(const MpBytes& packet);
    // Once per engine tick with the frame delta (10 units at 25 Hz).
    void Tick(int deltaUnits);

    struct Counters {
        int sent = 0, received = 0, unapplied = 0, malformed = 0;
    };
    const Counters& counters() const { return counters_; }

private:
    void Dispatch(const MpBytes& packet);
    void SendOwnState();
    void StartStream();
    void DropPeer();

    MpWorld& world_;
    MpLobby lobby_;
    std::unique_ptr<MpLink> link_;
    State state_ = State::Idle;
    bool host_ = false;
    int myIndex_ = 0;
    std::string names_[kMpMaxPlayers];
    std::string error_;
    std::string levelName_;

    bool localLoaded_ = false;   // this side's level is up
    bool peerReady_ = false;     // host: the client sent 0x07
    bool placed_ = false;        // client: 0x06 arrived
    bool streaming_ = false;     // host: the 0x1d stream is going out
    std::vector<MpEntityRecord> stream_;
    size_t streamNext_ = 0;

    int moveTimer_ = 0;
    int keepaliveTimer_ = 0;
    bool haveLastState_ = false;
    MpPlayerState lastState_;
    int pauseHolder_ = -1;
    bool localPause_ = false;
    Counters counters_;
};

}  // namespace sk_net
