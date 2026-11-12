#include "net/mp_transport.h"

#include <chrono>
#include <cstring>

#include <winsock2.h>
#include <ws2tcpip.h>

namespace sk_net {

namespace {

// ---- in-memory ----------------------------------------------------------

struct MemoryChannel {
    std::deque<MpBytes> toA, toB;
    bool open = true;
};

class MemoryLink : public MpLink {
public:
    MemoryLink(std::shared_ptr<MemoryChannel> channel, bool isA)
        : channel_(std::move(channel)), isA_(isA) {}
    ~MemoryLink() override { Close(); }
    bool Send(const MpBytes& packet) override {
        if (!channel_->open) return false;
        (isA_ ? channel_->toB : channel_->toA).push_back(packet);
        return true;
    }
    bool Receive(MpBytes& packet) override {
        std::deque<MpBytes>& q = isA_ ? channel_->toA : channel_->toB;
        if (q.empty()) return false;
        packet = std::move(q.front());
        q.pop_front();
        return true;
    }
    bool connected() const override { return channel_->open; }
    void Close() override { channel_->open = false; }

private:
    std::shared_ptr<MemoryChannel> channel_;
    bool isA_;
};

// ---- sockets ------------------------------------------------------------

bool EnsureWinsock() {
    static const bool ok = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ok;
}

void SetNonBlocking(SOCKET s) {
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
}

// A packet on TCP is a 4-byte little-endian length, then that many bytes.
// Sane packets are a few hundred bytes; the cap only stops a corrupt length
// from allocating the address space.
constexpr uint32_t kMaxPacket = 4u << 20;

class TcpLink : public MpLink {
public:
    explicit TcpLink(SOCKET s) : socket_(s) {
        SetNonBlocking(socket_);
        int on = 1;
        setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof(on));
    }
    ~TcpLink() override { Close(); }

    bool Send(const MpBytes& packet) override {
        if (socket_ == INVALID_SOCKET) return false;
        const uint32_t n = static_cast<uint32_t>(packet.size());
        const uint8_t header[4] = {static_cast<uint8_t>(n), static_cast<uint8_t>(n >> 8),
                                   static_cast<uint8_t>(n >> 16), static_cast<uint8_t>(n >> 24)};
        out_.insert(out_.end(), header, header + 4);
        out_.insert(out_.end(), packet.begin(), packet.end());
        Flush();
        return socket_ != INVALID_SOCKET;
    }

    bool Receive(MpBytes& packet) override {
        Flush();
        Pump();
        if (in_.size() < 4) return false;
        const uint32_t n = static_cast<uint32_t>(in_[0]) | (static_cast<uint32_t>(in_[1]) << 8) |
                           (static_cast<uint32_t>(in_[2]) << 16) |
                           (static_cast<uint32_t>(in_[3]) << 24);
        if (n > kMaxPacket) {
            Close();
            return false;
        }
        if (in_.size() < 4 + static_cast<size_t>(n)) return false;
        packet.assign(in_.begin() + 4, in_.begin() + 4 + static_cast<std::ptrdiff_t>(n));
        in_.erase(in_.begin(), in_.begin() + 4 + static_cast<std::ptrdiff_t>(n));
        return true;
    }

    // Still "connected" while whole packets that arrived before the close
    // are waiting to be read.
    bool connected() const override { return socket_ != INVALID_SOCKET || in_.size() >= 4; }

    void Close() override {
        if (socket_ != INVALID_SOCKET) {
            shutdown(socket_, SD_BOTH);
            closesocket(socket_);
            socket_ = INVALID_SOCKET;
        }
    }

private:
    void Flush() {
        while (socket_ != INVALID_SOCKET && !out_.empty()) {
            const int sent = send(socket_, reinterpret_cast<const char*>(out_.data()),
                                  static_cast<int>(out_.size() > 65536 ? 65536 : out_.size()), 0);
            if (sent > 0) {
                out_.erase(out_.begin(), out_.begin() + sent);
            } else {
                if (WSAGetLastError() != WSAEWOULDBLOCK) Close();
                return;
            }
        }
    }
    void Pump() {
        char buf[8192];
        while (socket_ != INVALID_SOCKET) {
            const int got = recv(socket_, buf, sizeof(buf), 0);
            if (got > 0) {
                in_.insert(in_.end(), buf, buf + got);
            } else if (got == 0) {
                Close();  // the other side hung up
            } else {
                if (WSAGetLastError() != WSAEWOULDBLOCK) Close();
                return;
            }
        }
    }

    SOCKET socket_;
    std::vector<uint8_t> in_, out_;
};

constexpr char kProbe[] = "SKMP?1";
constexpr char kAnswer[] = "SKMP!1";

}  // namespace

MpMemoryPair MakeMemoryLinkPair() {
    auto channel = std::make_shared<MemoryChannel>();
    MpMemoryPair pair;
    pair.a = std::make_unique<MemoryLink>(channel, true);
    pair.b = std::make_unique<MemoryLink>(channel, false);
    return pair;
}

struct MpLobby::Impl {
    SOCKET listener = INVALID_SOCKET;
    SOCKET udp = INVALID_SOCKET;
    bool discovering = false;
    std::string hostName;
    uint16_t hostPort = kMpGamePort;
    std::vector<MpHostEntry> hosts;
    std::vector<MpHostEntry> direct;
    std::chrono::steady_clock::time_point lastProbe{};
    std::string error;

    void CloseUdp() {
        if (udp != INVALID_SOCKET) closesocket(udp);
        udp = INVALID_SOCKET;
    }
    bool OpenUdp(bool bindDiscoveryPort) {
        CloseUdp();
        udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (udp == INVALID_SOCKET) return false;
        int on = 1;
        setsockopt(udp, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&on), sizeof(on));
        setsockopt(udp, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on), sizeof(on));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(bindDiscoveryPort ? kMpDiscoveryPort : 0);
        if (bind(udp, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            CloseUdp();
            return false;
        }
        SetNonBlocking(udp);
        return true;
    }
    void AddHost(const MpHostEntry& entry) {
        for (MpHostEntry& h : hosts) {
            if (h.address == entry.address && h.port == entry.port) {
                if (!entry.name.empty()) h.name = entry.name;
                return;
            }
        }
        hosts.push_back(entry);
    }
    void SendProbe() {
        if (udp == INVALID_SOCKET) return;
        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_port = htons(kMpDiscoveryPort);
        // The LAN, and this machine (two copies on one PC: a broadcast is
        // not always looped back to the sender's own host).
        for (uint32_t ip : {static_cast<uint32_t>(INADDR_BROADCAST), static_cast<uint32_t>(INADDR_LOOPBACK)}) {
            to.sin_addr.s_addr = htonl(ip);
            sendto(udp, kProbe, sizeof(kProbe) - 1, 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
        }
        for (const MpHostEntry& d : direct) {
            to.sin_port = htons(kMpDiscoveryPort);
            if (inet_pton(AF_INET, d.address.c_str(), &to.sin_addr) == 1) {
                sendto(udp, kProbe, sizeof(kProbe) - 1, 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
            }
        }
    }
};

MpLobby::MpLobby() : impl_(new Impl()) {}

MpLobby::~MpLobby() {
    StopHosting();
    EndDiscovery();
    delete impl_;
}

const std::string& MpLobby::lastError() const { return impl_->error; }
bool MpLobby::hosting() const { return impl_->listener != INVALID_SOCKET; }
const std::vector<MpHostEntry>& MpLobby::hosts() const { return impl_->hosts; }

bool MpLobby::Host(const std::string& name, uint16_t port) {
    Impl& d = *impl_;
    if (!EnsureWinsock()) {
        d.error = "winsock unavailable";
        return false;
    }
    StopHosting();
    EndDiscovery();
    d.listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (d.listener == INVALID_SOCKET) {
        d.error = "socket";
        return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(d.listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        listen(d.listener, 1) != 0) {
        d.error = "port " + std::to_string(port) + " is in use (is another game already hosting?)";
        closesocket(d.listener);
        d.listener = INVALID_SOCKET;
        return false;
    }
    SetNonBlocking(d.listener);
    d.hostName = name;
    d.hostPort = port;
    // Discovery is a convenience: a host that cannot bind the UDP port is
    // still reachable by address.
    d.OpenUdp(/*bindDiscoveryPort=*/true);
    d.error.clear();
    return true;
}

std::unique_ptr<MpLink> MpLobby::Accept() {
    Impl& d = *impl_;
    if (d.listener == INVALID_SOCKET) return nullptr;
    const SOCKET s = accept(d.listener, nullptr, nullptr);
    if (s == INVALID_SOCKET) return nullptr;
    return std::make_unique<TcpLink>(s);
}

void MpLobby::StopHosting() {
    Impl& d = *impl_;
    if (d.listener != INVALID_SOCKET) {
        closesocket(d.listener);
        d.listener = INVALID_SOCKET;
        d.CloseUdp();
    }
}

bool MpLobby::BeginDiscovery() {
    Impl& d = *impl_;
    if (!EnsureWinsock()) {
        d.error = "winsock unavailable";
        return false;
    }
    StopHosting();
    d.hosts.clear();
    for (const MpHostEntry& entry : d.direct) d.hosts.push_back(entry);
    if (!d.OpenUdp(/*bindDiscoveryPort=*/false)) {
        d.error = "discovery socket";
        return false;
    }
    d.discovering = true;
    d.SendProbe();
    d.lastProbe = std::chrono::steady_clock::now();
    return true;
}

void MpLobby::EndDiscovery() {
    Impl& d = *impl_;
    if (d.discovering) {
        d.discovering = false;
        d.CloseUdp();
    }
}

void MpLobby::AddDirectHost(const std::string& address, uint16_t port) {
    MpHostEntry entry;
    entry.name = address;
    entry.address = address;
    entry.port = port;
    impl_->direct.push_back(entry);
    impl_->AddHost(entry);
}

void MpLobby::Poll() {
    Impl& d = *impl_;
    if (d.udp == INVALID_SOCKET) return;
    char buf[256];
    for (;;) {
        sockaddr_in from{};
        int fromLen = sizeof(from);
        const int got = recvfrom(d.udp, buf, sizeof(buf) - 1, 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (got <= 0) break;
        buf[got] = 0;
        if (hosting()) {
            if (got >= static_cast<int>(sizeof(kProbe) - 1) && std::memcmp(buf, kProbe, sizeof(kProbe) - 1) == 0) {
                // "SKMP!1" + two port bytes + the host's name.
                std::string answer = kAnswer;
                answer.push_back(static_cast<char>(d.hostPort & 0xff));
                answer.push_back(static_cast<char>(d.hostPort >> 8));
                answer += d.hostName;
                sendto(d.udp, answer.data(), static_cast<int>(answer.size()), 0,
                       reinterpret_cast<sockaddr*>(&from), fromLen);
            }
        } else if (d.discovering) {
            const size_t head = sizeof(kAnswer) - 1;
            if (static_cast<size_t>(got) >= head + 2 && std::memcmp(buf, kAnswer, head) == 0) {
                MpHostEntry entry;
                entry.port = static_cast<uint16_t>(static_cast<uint8_t>(buf[head]) |
                                                   (static_cast<uint8_t>(buf[head + 1]) << 8));
                entry.name.assign(buf + head + 2, static_cast<size_t>(got) - head - 2);
                char ip[32] = {};
                inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
                entry.address = ip;
                d.AddHost(entry);
            }
        }
    }
    if (d.discovering) {
        const auto now = std::chrono::steady_clock::now();
        if (now - d.lastProbe > std::chrono::seconds(1)) {
            d.SendProbe();
            d.lastProbe = now;
        }
    }
}

std::unique_ptr<MpLink> MpLobby::Connect(const MpHostEntry& host, int timeoutMs) {
    Impl& d = *impl_;
    if (!EnsureWinsock()) return nullptr;
    const SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return nullptr;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(host.port);
    if (inet_pton(AF_INET, host.address.c_str(), &addr.sin_addr) != 1) {
        d.error = "bad address " + host.address;
        closesocket(s);
        return nullptr;
    }
    SetNonBlocking(s);
    connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    fd_set writable, failed;
    FD_ZERO(&writable);
    FD_ZERO(&failed);
    FD_SET(s, &writable);
    FD_SET(s, &failed);
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    const int ready = select(0, nullptr, &writable, &failed, &tv);
    if (ready <= 0 || !FD_ISSET(s, &writable)) {
        d.error = "no answer from " + host.address;
        closesocket(s);
        return nullptr;
    }
    d.error.clear();
    return std::make_unique<TcpLink>(s);
}

std::string MpLocalMachineName() {
    EnsureWinsock();
    char name[256] = {};
    if (gethostname(name, sizeof(name) - 1) != 0 || name[0] == 0) return "Player";
    return name;
}

}  // namespace sk_net
