#pragma once
#include <functional>

#include "net/mp_session.h"

// M129: MpWorld as a set of callbacks. The game's state lives in main()'s
// locals, which a class cannot reach but a lambda can, so main.cpp fills
// these in and hands the object to the session. An unset callback is the
// base class's empty default.

namespace sk_net {

class MpWorldFns : public MpWorld {
public:
    std::function<int(const std::string&)> levelIndex;
    std::function<std::string(int)> levelName;
    std::function<MpPlayerState()> localPlayerState;
    std::function<MpPlayerInfo()> localPlayerInfo;
    std::function<int()> localHeldWeapon;
    std::function<MpStoryState()> buildStoryState;
    std::function<std::vector<MpEntityRecord>()> buildEntityStream;

    std::function<void(const std::string&)> onGameStart;
    std::function<void(const MpStoryState&)> onStoryState;
    std::function<void(const MpEntityRecord&)> onSpawnEntity;
    std::function<void(const MpPlaceAndStart&)> onPlaceAndStart;
    std::function<void()> onPeerInGame;
    std::function<void(int, const std::string&)> onPlayerLeft;
    std::function<void(const MpTravel&)> onTravelRequest;

    std::function<void(const MpPlayerState&)> onPlayerState;
    std::function<void(const MpPlayerInfo&)> onPlayerInfo;
    std::function<void(int, int)> onHeldWeapon;
    std::function<void(int, int, int)> onAttackVisual;
    std::function<void(int, int)> onAnimation;
    std::function<void(int, const std::string&)> onTextMessage;
    std::function<void(int)> onDamageMe;

    std::function<void(int)> onKill, onHitForZero, onTouched, onDestroyObject, onNoticed, onAlerted;
    std::function<void(int, int)> onCreatureTarget, onTakenFromContainer, onSetOnFire;
    std::function<void(int, bool)> onDoor, onInvulnerable;
    std::function<void(const MpEntityRecord&)> onHandOver;
    std::function<void(const MpSetPosition&)> onSetPosition;
    std::function<void(int, const std::string&)> onMirrorMethod;
    std::function<void(int, int, int)> onQuestFlag;
    std::function<void(int, int)> onShareXp;
    std::function<void(const std::string&, int)> onLevelVar;
    std::function<void(int, int, bool)> onRegistryFlag;
    std::function<void(uint8_t, const MpBytes&)> onUnapplied;

    int LevelIndex(const std::string& n) override { return levelIndex ? levelIndex(n) : 0; }
    std::string LevelName(int i) override { return levelName ? levelName(i) : std::string(); }
    MpPlayerState LocalPlayerState() override {
        return localPlayerState ? localPlayerState() : MpPlayerState{};
    }
    MpPlayerInfo LocalPlayerInfo() override {
        return localPlayerInfo ? localPlayerInfo() : MpPlayerInfo{};
    }
    int LocalHeldWeapon() override { return localHeldWeapon ? localHeldWeapon() : 0; }
    MpStoryState BuildStoryState() override {
        return buildStoryState ? buildStoryState() : MpStoryState{};
    }
    std::vector<MpEntityRecord> BuildEntityStream() override {
        return buildEntityStream ? buildEntityStream() : std::vector<MpEntityRecord>{};
    }

    void OnGameStart(const std::string& l) override { if (onGameStart) onGameStart(l); }
    void OnStoryState(const MpStoryState& s) override { if (onStoryState) onStoryState(s); }
    void OnSpawnEntity(const MpEntityRecord& e) override { if (onSpawnEntity) onSpawnEntity(e); }
    void OnPlaceAndStart(const MpPlaceAndStart& p) override { if (onPlaceAndStart) onPlaceAndStart(p); }
    void OnPeerInGame() override { if (onPeerInGame) onPeerInGame(); }
    void OnPlayerLeft(int i, const std::string& n) override { if (onPlayerLeft) onPlayerLeft(i, n); }
    void OnTravelRequest(const MpTravel& t) override { if (onTravelRequest) onTravelRequest(t); }

    void OnPlayerState(const MpPlayerState& s) override { if (onPlayerState) onPlayerState(s); }
    void OnPlayerInfo(const MpPlayerInfo& i) override { if (onPlayerInfo) onPlayerInfo(i); }
    void OnHeldWeapon(int i, int w) override { if (onHeldWeapon) onHeldWeapon(i, w); }
    void OnAttackVisual(int i, int a, int s) override { if (onAttackVisual) onAttackVisual(i, a, s); }
    void OnAnimation(int a, int i) override { if (onAnimation) onAnimation(a, i); }
    void OnTextMessage(int i, const std::string& t) override { if (onTextMessage) onTextMessage(i, t); }
    void OnDamageMe(int a) override { if (onDamageMe) onDamageMe(a); }

    void OnKill(int id) override { if (onKill) onKill(id); }
    void OnHitForZero(int id) override { if (onHitForZero) onHitForZero(id); }
    void OnTouched(int id) override { if (onTouched) onTouched(id); }
    void OnCreatureTarget(int id, int p) override { if (onCreatureTarget) onCreatureTarget(id, p); }
    void OnDoor(int id, bool close) override { if (onDoor) onDoor(id, close); }
    void OnDestroyObject(int id) override { if (onDestroyObject) onDestroyObject(id); }
    void OnNoticed(int id) override { if (onNoticed) onNoticed(id); }
    void OnTakenFromContainer(int c, int t) override { if (onTakenFromContainer) onTakenFromContainer(c, t); }
    void OnHandOver(const MpEntityRecord& e) override { if (onHandOver) onHandOver(e); }
    void OnSetPosition(const MpSetPosition& p) override { if (onSetPosition) onSetPosition(p); }
    void OnMirrorMethod(int id, const std::string& m) override { if (onMirrorMethod) onMirrorMethod(id, m); }
    void OnInvulnerable(int id, bool f) override { if (onInvulnerable) onInvulnerable(id, f); }
    void OnSetOnFire(int id, int l) override { if (onSetOnFire) onSetOnFire(id, l); }
    void OnAlerted(int id) override { if (onAlerted) onAlerted(id); }
    void OnQuestFlag(int k, int q, int v) override { if (onQuestFlag) onQuestFlag(k, q, v); }
    void OnShareXp(int k, int v) override { if (onShareXp) onShareXp(k, v); }
    void OnLevelVar(const std::string& n, int v) override { if (onLevelVar) onLevelVar(n, v); }
    void OnRegistryFlag(int id, int m, bool s) override { if (onRegistryFlag) onRegistryFlag(id, m, s); }
    void OnUnapplied(uint8_t t, const MpBytes& p) override { if (onUnapplied) onUnapplied(t, p); }
};

}  // namespace sk_net
