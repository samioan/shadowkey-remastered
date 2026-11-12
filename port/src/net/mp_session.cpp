#include "net/mp_session.h"

namespace sk_net {

MpSession::MpSession(MpWorld& world) : world_(world) {}

MpSession::~MpSession() { Close(); }

bool MpSession::BeginHost(const std::string& myName) {
    Close();
    if (!lobby_.Host(myName)) {
        error_ = lobby_.lastError();
        return false;
    }
    host_ = true;
    myIndex_ = 0;
    names_[0] = myName;
    names_[1].clear();
    state_ = State::HostLobby;
    return true;
}

bool MpSession::BeginJoin(const std::string& myName) {
    Close();
    if (!lobby_.BeginDiscovery()) {
        error_ = lobby_.lastError();
        return false;
    }
    host_ = false;
    myIndex_ = 1;
    names_[1] = myName;
    names_[0].clear();
    state_ = State::JoinLobby;
    return true;
}

bool MpSession::JoinHost(int hostIndex) {
    if (state_ != State::JoinLobby || link_) return false;
    const std::vector<MpHostEntry>& hosts = lobby_.hosts();
    if (hostIndex < 0 || static_cast<size_t>(hostIndex) >= hosts.size()) return false;
    link_ = lobby_.Connect(hosts[static_cast<size_t>(hostIndex)]);
    if (!link_) {
        error_ = lobby_.lastError();
        return false;
    }
    names_[0] = hosts[static_cast<size_t>(hostIndex)].name;
    // The host's menu shows who joined: Bluetooth told it, here 0x22 does.
    MpPlayerInfo info = world_.LocalPlayerInfo();
    info.index = myIndex_;
    if (info.name.empty()) info.name = names_[1];
    link_->Send(info.Encode());
    ++counters_.sent;
    return true;
}

void MpSession::RejectClient() {
    if (!host_ || !link_) return;
    link_->Send(EncodeIndexed(kTagPlayerLeft, myIndex_));
    link_->Close();
    link_.reset();
    names_[1].clear();
}

void MpSession::AdoptLink(std::unique_ptr<MpLink> link, bool asHost, const std::string& myName) {
    Close();
    link_ = std::move(link);
    host_ = asHost;
    myIndex_ = asHost ? 0 : 1;
    names_[myIndex_] = myName;
    state_ = asHost ? State::HostLobby : State::JoinLobby;
    if (asHost) {
        link_->Send(EncodeIndexed(kTagYourIndex, 1));
    } else {
        MpPlayerInfo info = world_.LocalPlayerInfo();
        info.index = myIndex_;
        if (info.name.empty()) info.name = myName;
        link_->Send(info.Encode());
    }
    ++counters_.sent;
}

bool MpSession::HostPlay(const std::string& levelName) {
    if (!host_ || state_ != State::HostLobby) return false;
    lobby_.StopHosting();  // the lobby is closed once the game starts
    state_ = State::Starting;
    HostAnnounceLevel(levelName);
    return true;
}

void MpSession::HostAnnounceLevel(const std::string& levelName) {
    if (!host_ || !active()) return;
    levelName_ = levelName;
    localLoaded_ = false;
    peerReady_ = false;
    streaming_ = false;
    stream_.clear();
    streamNext_ = 0;
    haveLastState_ = false;
    if (peerConnected()) {
        MpGameStart start;
        start.levelIndex = world_.LevelIndex(levelName);
        start.playerCount = 2;
        start.names[0] = names_[0];
        start.names[1] = names_[1];
        Send(start.Encode());
        state_ = State::Starting;
    }
}

void MpSession::RequestTravel(const MpTravel& travel) {
    if (isClient()) Send(travel.Encode());
}

void MpSession::Close() {
    if (link_) {
        if (link_->connected() && active()) link_->Send(EncodeIndexed(kTagPlayerLeft, myIndex_));
        link_->Close();
        link_.reset();
    }
    lobby_.StopHosting();
    lobby_.EndDiscovery();
    state_ = State::Idle;
    host_ = false;
    myIndex_ = 0;
    localLoaded_ = peerReady_ = placed_ = streaming_ = false;
    stream_.clear();
    streamNext_ = 0;
    pauseHolder_ = -1;
    localPause_ = false;
    haveLastState_ = false;
    moveTimer_ = keepaliveTimer_ = 0;
}

bool MpSession::Send(const MpBytes& packet) {
    if (!link_ || !link_->connected()) return false;
    if (!link_->Send(packet)) return false;
    ++counters_.sent;
    return true;
}

void MpSession::SetPauseLock(bool held) {
    if (!inGame() || held == localPause_) return;
    // Only one player holds it; a second request while the other has it is
    // simply not made (that player's frame is not running anyway).
    if (held && pauseHolder_ == peerIndex()) return;
    localPause_ = held;
    pauseHolder_ = held ? myIndex_ : -1;
    Send(EncodePauseLock(myIndex_, held));
}

void MpSession::SendOwnState() {
    MpPlayerState state = world_.LocalPlayerState();
    state.index = myIndex_;
    Send(state.Encode());
    lastState_ = state;
    haveLastState_ = true;
    MpPlayerInfo info = world_.LocalPlayerInfo();
    info.index = myIndex_;
    if (info.name.empty()) info.name = names_[myIndex_];
    Send(info.Encode());
    Send(EncodeHeldWeapon(myIndex_, world_.LocalHeldWeapon()));
}

void MpSession::NotifyLevelLoaded() {
    if (!active()) return;
    localLoaded_ = true;
    if (host_) {
        if (!peerConnected()) {
            state_ = State::InGame;  // hosting alone
            return;
        }
        if (peerReady_ && !streaming_ && state_ == State::Starting) StartStream();
    } else if (placed_) {
        // Step 4's tail: the client answers 0x06 with its own state and 0x30.
        SendOwnState();
        Send(EncodeTagOnly(kTagLoaded));
        placed_ = false;
        state_ = State::InGame;
        world_.OnPeerInGame();
    }
}

void MpSession::StartStream() {
    // Step 3: the host's story, then every entity, up to 50 a frame.
    Send(world_.BuildStoryState().Encode());
    stream_ = world_.BuildEntityStream();
    streamNext_ = 0;
    streaming_ = true;
}

void MpSession::DropPeer() {
    const int who = peerIndex();
    const std::string name = names_[who];
    if (link_) link_->Close();
    link_.reset();
    if (pauseHolder_ == who) pauseHolder_ = -1;
    if (state_ == State::HostLobby) {
        names_[1].clear();
        return;
    }
    if (state_ == State::JoinLobby) return;
    if (active()) {
        world_.OnPlayerLeft(who, name);
        // The game goes on alone. A client keeps the world it has.
        state_ = State::Idle;
        host_ = false;
        localPause_ = false;
        pauseHolder_ = -1;
    }
}

void MpSession::Tick(int deltaUnits) {
    lobby_.Poll();
    if (state_ == State::HostLobby && !link_) {
        link_ = lobby_.Accept();
        if (link_) {
            link_->Send(EncodeIndexed(kTagYourIndex, 1));
            ++counters_.sent;
        }
    }
    if (!link_) return;

    MpBytes packet;
    while (link_ && link_->Receive(packet)) {
        ++counters_.received;
        Dispatch(packet);
    }
    if (!link_) return;
    if (!link_->connected()) {
        DropPeer();
        return;
    }

    if (host_ && streaming_) {
        for (int n = 0; n < kEntitiesPerFrame && streamNext_ < stream_.size(); ++n, ++streamNext_) {
            const MpEntityRecord& e = stream_[streamNext_];
            Send(EncodeEntityStream(kTagSpawnEntity, e.typeId, e.entityId, e.chain));
        }
        if (streamNext_ >= stream_.size()) {
            // Step 4: the stream ends with 0x07, then 0x06 (the host's own
            // position: the client arrives beside it) and the host's state.
            streaming_ = false;
            stream_.clear();
            Send(EncodeTagOnly(kTagReady));
            const MpPlayerState me = world_.LocalPlayerState();
            MpPlaceAndStart place;
            place.x = me.x;
            place.y = me.y;
            place.z = me.z;
            place.heading = me.heading;
            Send(place.Encode());
            SendOwnState();
            state_ = State::InGame;
        }
    }

    if (state_ != State::InGame) return;
    moveTimer_ += deltaUnits;
    if (moveTimer_ >= kMoveInterval) {
        moveTimer_ = 0;
        MpPlayerState state = world_.LocalPlayerState();
        state.index = myIndex_;
        if (!haveLastState_ || !(state == lastState_)) {  // only when something changed
            Send(state.Encode());
            lastState_ = state;
            haveLastState_ = true;
        }
    }
    keepaliveTimer_ += deltaUnits;
    if (keepaliveTimer_ >= kKeepaliveInterval) {
        keepaliveTimer_ = 0;
        Send(EncodeTagOnly(kTagKeepalive));
    }
}

void MpSession::Dispatch(const MpBytes& packet) {
    if (packet.empty()) {
        ++counters_.malformed;
        return;
    }
    const uint8_t tag = packet[0];
    const MpTagInfo* info = MpTagInfoFor(tag);
    if (!info || (info->size != 0 && packet.size() != info->size)) {
        ++counters_.malformed;
        return;
    }
    MpReader r(packet);
    switch (tag) {
        // ---- session ----
        case kTagYourIndex:
            if (!host_) myIndex_ = r.U8(4) & 1;
            break;
        case kTagGameStart: {
            if (host_) break;
            const MpGameStart start = MpGameStart::Decode(packet);
            names_[0] = start.names[0];
            if (!start.names[1].empty()) names_[1] = start.names[1];
            levelName_ = world_.LevelName(start.levelIndex);
            lobby_.EndDiscovery();
            state_ = State::Starting;
            localLoaded_ = false;
            placed_ = false;
            haveLastState_ = false;
            world_.OnGameStart(levelName_);
            // Step 3: ready, with our own 0x01 / 0x22 / 0x23. The level is
            // loaded once the host's world has arrived (0x06).
            Send(EncodeTagOnly(kTagReady));
            SendOwnState();
            break;
        }
        case kTagReady:
            if (host_) {
                peerReady_ = true;
                if (localLoaded_ && !streaming_ && state_ == State::Starting) StartStream();
            }
            // On the client it only marks the end of the stream; 0x06 follows.
            break;
        case kTagPlaceAndStart:
            if (!host_) {
                placed_ = true;
                world_.OnPlaceAndStart(MpPlaceAndStart::Decode(packet));
            }
            break;
        case kTagLoaded:
            if (host_) world_.OnPeerInGame();
            break;
        case kTagPlayerLeft:
            DropPeer();
            break;
        case kTagPauseLock: {
            const int who = r.U8(4);
            if (r.U8(5) != 0) {
                pauseHolder_ = who;
            } else if (pauseHolder_ == who) {
                pauseHolder_ = -1;
            }
            break;
        }
        case kTagKeepalive:
            break;
        case kTagTravel:
            if (host_) world_.OnTravelRequest(MpTravel::Decode(packet));
            break;

        // ---- players ----
        case kTagPlayerState: {
            const MpPlayerState state = MpPlayerState::Decode(packet);
            if (state.index >= kMpMaxPlayers) break;  // the engine's own check
            world_.OnPlayerState(state);
            break;
        }
        case kTagPlayerInfo: {
            const MpPlayerInfo pi = MpPlayerInfo::Decode(packet);
            if (pi.index < kMpMaxPlayers && !pi.name.empty()) names_[pi.index] = pi.name;
            world_.OnPlayerInfo(pi);
            break;
        }
        case kTagHeldWeapon:
            world_.OnHeldWeapon(r.U8(4), r.I16(6));
            break;
        case kTagAttackVisual:
            world_.OnAttackVisual(r.U8(4), r.U8(5), r.U8(6));
            break;
        case kTagAnimation:
            world_.OnAnimation(r.U8(4), r.U8(5));
            break;
        case kTagTextMessage:
            world_.OnTextMessage(r.U8(4), r.Wide(6, 0x100));
            break;
        case kTagDamageMe:
            world_.OnDamageMe(r.I32(4));
            break;

        // ---- creatures and objects ----
        case kTagKill:
            world_.OnKill(r.U16(8));
            break;
        case kTagHitForZero:
            world_.OnHitForZero(r.U16(8));
            break;
        case kTagTouched:
            world_.OnTouched(r.U16(8));
            break;
        case kTagCreatureTarget:
            world_.OnCreatureTarget(r.U16(4), r.I32(0xc));
            break;
        case kTagDoor:
            world_.OnDoor(r.U16(8), r.U8(5) != 0);
            break;
        case kTagDestroyObject:
            world_.OnDestroyObject(r.U16(4));
            break;
        case kTagNoticed:
            world_.OnNoticed(r.U16(4));
            break;
        case kTagTakenFromContainer:
            world_.OnTakenFromContainer(r.U16(4), r.I16(8));
            break;
        case kTagSpawnEntity:
        case kTagHandOver: {
            MpEntityRecord rec;
            if (!DecodeEntityStream(packet, &rec.typeId, &rec.entityId, &rec.chain)) {
                ++counters_.malformed;
                break;
            }
            if (tag == kTagSpawnEntity) {
                world_.OnSpawnEntity(rec);
            } else {
                world_.OnHandOver(rec);
            }
            break;
        }
        case kTagSetPosition:
            world_.OnSetPosition(MpSetPosition::Decode(packet));
            break;
        case kTagMirrorMethod: {
            const MpMirrorMethod m = MpMirrorMethod::Decode(packet);
            world_.OnMirrorMethod(m.entityId, m.method);
            break;
        }
        case kTagInvulnerable:
            world_.OnInvulnerable(r.U16(4), r.U8(8) != 0);
            break;
        case kTagSetOnFire:
            world_.OnSetOnFire(r.U16(4), r.I32(8));
            break;
        case kTagAlerted:
            world_.OnAlerted(r.U16(4));
            break;

        // ---- world and story ----
        case kTagQuestFlag:
            world_.OnQuestFlag(r.U8(4), r.U8(5), r.U8(6));
            break;
        case kTagShareXp:
            world_.OnShareXp(r.U8(4), r.I32(8));
            break;
        case kTagStoryState: {
            MpStoryState story;
            if (!MpStoryState::Decode(packet, &story)) {
                ++counters_.malformed;
                break;
            }
            world_.OnStoryState(story);
            break;
        }
        case kTagLevelVar:
        case kTagLevelVarMenu: {
            const MpLevelVar v = MpLevelVar::Decode(packet);
            world_.OnLevelVar(v.name, v.value);
            break;
        }
        case kTagRegistryFlag:
            world_.OnRegistryFlag(r.U16(4), r.U16(6), r.U8(8) != 0);
            break;

        default:
            // In the catalogue, decoded for size, not applied by this port.
            ++counters_.unapplied;
            world_.OnUnapplied(tag, packet);
            break;
    }
}

}  // namespace sk_net
