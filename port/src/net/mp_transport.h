#pragma once
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "net/mp_protocol.h"

// M129: what carries the packets. The original used the N-Gage's GAMECOMMS
// library over Bluetooth -- "send this buffer to the other handset" -- and
// that is all the session above needs, so that is all this is:
//
//   * MpLink     one reliable, ordered, packet-preserving connection to the
//                other player. TCP with a length prefix, or (for tests) two
//                queues in memory.
//   * MpLobby    how the two find each other. "Looking for hosts" was a
//                Bluetooth inquiry; here the client broadcasts a UDP probe on
//                the LAN and every hosting game answers with its name. For
//                play across the internet the host's address can be given
//                directly (AddDirectHost) -- no server, no account.
//
// Everything is non-blocking and polled from the game's own tick, on the
// game's own thread.

namespace sk_net {

constexpr uint16_t kMpGamePort = 28510;       // TCP: the game connection
constexpr uint16_t kMpDiscoveryPort = 28511;  // UDP: "who is hosting?"

class MpLink {
public:
    virtual ~MpLink() = default;
    // Queues one packet. False once the link is down.
    virtual bool Send(const MpBytes& packet) = 0;
    // Takes the next whole packet, if one has arrived.
    virtual bool Receive(MpBytes& packet) = 0;
    virtual bool connected() const = 0;
    virtual void Close() = 0;
};

// Two ends of an in-memory connection: what one sends the other receives,
// in order. Closing either end disconnects both.
struct MpMemoryPair {
    std::unique_ptr<MpLink> a, b;
};
MpMemoryPair MakeMemoryLinkPair();

struct MpHostEntry {
    std::string name;     // the host's player name
    std::string address;  // dotted IPv4
    uint16_t port = kMpGamePort;
};

class MpLobby {
public:
    MpLobby();
    ~MpLobby();
    MpLobby(const MpLobby&) = delete;
    MpLobby& operator=(const MpLobby&) = delete;

    // ---- host ----
    // Listens for a client and answers discovery probes with `name`.
    bool Host(const std::string& name, uint16_t port = kMpGamePort);
    bool hosting() const;
    // The client that connected since the last call, if any.
    std::unique_ptr<MpLink> Accept();
    void StopHosting();

    // ---- client ----
    // Starts looking: a probe goes out now and again every second Poll() is
    // called for. Found hosts accumulate in hosts(), in the order found.
    bool BeginDiscovery();
    void EndDiscovery();
    // A host that cannot be found by broadcast (another network).
    void AddDirectHost(const std::string& address, uint16_t port = kMpGamePort);
    const std::vector<MpHostEntry>& hosts() const;
    // Connects to one. Blocks for at most `timeoutMs`.
    std::unique_ptr<MpLink> Connect(const MpHostEntry& host, int timeoutMs = 3000);

    // Both roles: answers probes / collects answers. Call every tick.
    void Poll();

    const std::string& lastError() const;

    struct Impl;

private:
    Impl* impl_;
};

// This machine's name, for the player-name row the Bluetooth name used to
// fill ("GetBluetoothName").
std::string MpLocalMachineName();

}  // namespace sk_net
