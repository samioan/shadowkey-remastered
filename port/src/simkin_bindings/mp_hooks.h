#pragma once
#include <functional>
#include <string>

// M129: where the native bindings meet the multiplayer session.
//
// In the engine every one of these is a line inside a native: "if
// (engine->inMultiplayer) session->vtable[slot](...)". The bindings library
// knows nothing about sockets and must not, so the session side is a set of
// callbacks main.cpp fills in while a multiplayer game is running. With
// none set -- single player, every test -- each call site is the no-op it
// always was.
//
// `active` is `engine+0x5c0`: what IsMultiplayer() answers. `client` is
// what IsMultiplayerClient() answers, the gate the shipped scripts put
// around host-only work (crypt1.s's Umbra spawn, glcrcrwl's control
// scripts, dstar_e's monsters).
//
// `applyingRemote` is set while an event that arrived from the other
// player is being applied. The receivers in the engine write state
// directly and send nothing; here some of them go back through the same
// native a script would call, and this keeps that from echoing.

namespace sk_bindings {

struct MpHooks {
    bool active = false;
    bool client = false;
    bool applyingRemote = false;

    // ---- events a native sends (docs/MULTIPLAYER_PROTOCOL.md tags) ----
    std::function<void(int netId, const std::string& method)> mirrorMethod;        // 0x34
    std::function<void(int netId)> destroyObject;                                  // 0x2b
    std::function<void(bool everyone, int netId, int x, int y, int z)> setPosition;  // 0x33
    std::function<void(int kind, int quest, int value)> questFlag;                 // 0x21
    std::function<void(int kind, int value)> shareXp;                              // 0x28
    std::function<void(int netId, bool flag)> invulnerable;                        // 0x35
    std::function<void(int netId, bool open)> door;                                // 0x1f
    // M132.
    std::function<void(int mask, bool set)> registryFlag;                          // 0x38
    std::function<void(int netId, bool canTeleport)> umbraBeat;                    // 0x37
    std::function<void(int typeId, int netId)> handOver;                           // 0x32

    // `FUN_10067f84`: the flame a magic hit leaves on its target -- sprites
    // `first..last`, alive for `seconds`. Not a multiplayer thing in
    // itself (it is set in every game); the session's part is tag 0x27,
    // which the same callback sends.
    std::function<void(class SpellActor* target, int first, int last, int seconds)> attachedEffect;

    // `ClearNewGameHook(n)` / `NewGameHook()`: where character creation
    // leads -- 0 a new single-player game, 1 the host menu, 2 the join menu.
    int newGameHook = 0;

    // ---- the lobby natives (the menus' side) ----
    std::function<bool(int slot)> preHost;        // MPPreHost / MPPreJoin: use this save's character
    std::function<bool()> beginHost;              // MPBeginHost
    std::function<void()> hostConnect;            // MPHostConnect
    std::function<bool()> hostPlay;               // MPHostPlay
    std::function<bool()> beginJoin;              // MPBeginJoin
    std::function<bool(int host)> joinGame;       // JoinGame(n)
    std::function<void(int client)> rejectClient; // RejectClient(n)
    std::function<void()> closeLobby;             // CloseMPHostMenu / CloseMPJoinMenu
    std::function<std::string()> localName;       // GetBluetoothName

    bool sending() const { return active && !applyingRemote; }
};

MpHooks& Mp();

// Sets `applyingRemote` for a scope.
class MpRemoteScope {
public:
    MpRemoteScope() : previous_(Mp().applyingRemote) { Mp().applyingRemote = true; }
    ~MpRemoteScope() { Mp().applyingRemote = previous_; }
    MpRemoteScope(const MpRemoteScope&) = delete;
    MpRemoteScope& operator=(const MpRemoteScope&) = delete;

private:
    bool previous_;
};

}  // namespace sk_bindings
