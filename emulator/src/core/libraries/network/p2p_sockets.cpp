// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef __linux__
#include <sys/eventfd.h>
#endif

// clang-format off
#include "sockets.h"
// clang-format on
#include "common/assert.h"
#include "common/logging/log.h"
#include "core/bloodborne_re.h"
#include "common/singleton.h"
#include "core/libraries/kernel/kernel.h"
#include "core/libraries/network/net_util.h"
#include "net.h"
#include "net_error.h"

namespace Libraries::Net {

namespace {

constexpr u16 kSignalingVPortNbo = 0xffff;
constexpr size_t kVPortHeaderSize = 4;
constexpr size_t kMaxUdpPayload = 65507;
constexpr size_t kMaxP2PPayload = kMaxUdpPayload - kVPortHeaderSize;

#ifdef _WIN32
constexpr net_socket kInvalidSocket = INVALID_SOCKET;
#else
constexpr net_socket kInvalidSocket = -1;
#endif

enum class InternalChannel {
    Signaling,
    Control,
    Matching2,
};

struct QueuedPacket {
    u32 source_addr{};
    u16 source_port{};
    std::vector<u8> payload;
};

int FailWith(int error) {
    *Libraries::Kernel::__Error() = error;
    return -1;
}

bool IsValidSocket(net_socket socket) {
    return socket != kInvalidSocket;
}

int LastSocketError() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

bool IsRetryableSocketError(int error) {
#ifdef _WIN32
    return error == WSAEWOULDBLOCK || error == WSAEINTR;
#else
    return error == EAGAIN || error == EWOULDBLOCK || error == EINTR;
#endif
}

void CloseSocket(net_socket& socket) {
    if (!IsValidSocket(socket)) {
        return;
    }
#ifdef _WIN32
    ::closesocket(socket);
#else
    ::close(socket);
#endif
    socket = kInvalidSocket;
}

bool SetSocketNonBlocking(net_socket socket) {
#ifdef _WIN32
    u_long enabled = 1;
    return ::ioctlsocket(socket, FIONBIO, &enabled) == 0;
#else
    int enabled = 1;
    return ::ioctl(socket, FIONBIO, &enabled) == 0;
#endif
}

#ifdef _WIN32
bool CreateReadinessPair(net_socket* read_socket, net_socket* signal_socket) {
    *read_socket = kInvalidSocket;
    *signal_socket = kInvalidSocket;

    net_socket reader = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (!IsValidSocket(reader)) {
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(reader, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
        CloseSocket(reader);
        return false;
    }

    socklen_t address_len = sizeof(address);
    if (::getsockname(reader, reinterpret_cast<sockaddr*>(&address), &address_len) < 0) {
        CloseSocket(reader);
        return false;
    }

    net_socket signaler = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (!IsValidSocket(signaler) ||
        ::connect(signaler, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0 ||
        !SetSocketNonBlocking(reader) || !SetSocketNonBlocking(signaler)) {
        CloseSocket(signaler);
        CloseSocket(reader);
        return false;
    }

    *read_socket = reader;
    *signal_socket = signaler;
    return true;
}
#endif

class P2PTransport {
public:
    static P2PTransport& Instance() {
        static P2PTransport transport;
        return transport;
    }

    ~P2PTransport() {
        Stop();
    }

    bool Start() {
#if !defined(__linux__) && !defined(_WIN32)
        LOG_ERROR(Lib_Net, "P2P transport is currently supported on Linux and Windows");
        return false;
#else
        std::lock_guard lock(start_mutex);
        if (running.load(std::memory_order_acquire)) {
            return true;
        }

        host_socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (!IsValidSocket(host_socket)) {
            LOG_ERROR(Lib_Net, "P2P transport socket() failed error={}", LastSocketError());
            return false;
        }

        int one = 1;
        (void)::setsockopt(host_socket, SOL_SOCKET, SO_REUSEADDR,
                           reinterpret_cast<const char*>(&one), sizeof(one));
        if (!SetSocketNonBlocking(host_socket)) {
            LOG_ERROR(Lib_Net, "P2P transport failed to make socket nonblocking error={}",
                      LastSocketError());
            CloseSocket(host_socket);
            return false;
        }

        u16 requested_port = 0;
        if (const char* env = std::getenv("SHADPS4_P2P_PORT"); env != nullptr && *env != '\0') {
            char* end = nullptr;
            const long parsed = std::strtol(env, &end, 10);
            if (end == env || *end != '\0' || parsed < 1 || parsed > 65535) {
                LOG_ERROR(Lib_Net, "Invalid SHADPS4_P2P_PORT='{}'", env);
                CloseSocket(host_socket);
                return false;
            }
            requested_port = static_cast<u16>(parsed);
        }

        sockaddr_in bind_addr{};
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        bind_addr.sin_port = htons(requested_port);

        if (::bind(host_socket, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) < 0) {
            LOG_ERROR(Lib_Net, "P2P transport bind(port={}) failed error={}", requested_port,
                      LastSocketError());
            CloseSocket(host_socket);
            return false;
        }

        sockaddr_in actual{};
        socklen_t actual_len = sizeof(actual);
        if (::getsockname(host_socket, reinterpret_cast<sockaddr*>(&actual), &actual_len) < 0) {
            LOG_ERROR(Lib_Net, "P2P transport getsockname() failed error={}", LastSocketError());
            CloseSocket(host_socket);
            return false;
        }

        physical_port = ntohs(actual.sin_port);
        running.store(true, std::memory_order_release);
        receive_thread = std::thread(&P2PTransport::ReceiveMain, this);

        LOG_INFO(Lib_Net,
                 "P2P transport ready: UDP physical_port={} (set SHADPS4_P2P_PORT to pin it)",
                 physical_port);
        return true;
#endif
    }

    void Stop() {
#if defined(__linux__) || defined(_WIN32)
        std::lock_guard lock(start_mutex);
        if (!running.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        if (receive_thread.joinable()) {
            receive_thread.join();
        }

        CloseSocket(host_socket);
        physical_port = 0;
#endif
    }

    bool IsReady() const {
        return running.load(std::memory_order_acquire);
    }

    u16 PhysicalPort() const {
        return physical_port;
    }

    bool RegisterSocket(P2PSocket* socket, u16 requested_vport_nbo, u16* actual_vport_nbo) {
        if (socket == nullptr || actual_vport_nbo == nullptr) {
            return false;
        }

        std::lock_guard lock(registry_mutex);
        u16 vport = requested_vport_nbo;

        // vport 0 acts as an ephemeral request in this PoC.
        if (vport == 0) {
            for (u32 attempt = 0; attempt < 35000; ++attempt) {
                if (next_ephemeral_vport >= 65535) {
                    next_ephemeral_vport = 30000;
                }
                const u16 candidate_host = next_ephemeral_vport++;
                const u16 candidate_nbo = htons(candidate_host);
                if (candidate_nbo == kSignalingVPortNbo || game_sockets.contains(candidate_nbo)) {
                    continue;
                }
                vport = candidate_nbo;
                break;
            }
        }

        if (vport == 0 || vport == kSignalingVPortNbo || game_sockets.contains(vport)) {
            return false;
        }

        game_sockets.emplace(vport, socket);
        *actual_vport_nbo = vport;
        return true;
    }

    void UnregisterSocket(P2PSocket* socket, u16 vport_nbo) {
        std::lock_guard lock(registry_mutex);
        const auto it = game_sockets.find(vport_nbo);
        if (it != game_sockets.end() && it->second == socket) {
            game_sockets.erase(it);
        }
    }

    int SendFrame(u16 source_vport_nbo, u16 dest_vport_nbo, const void* data, u32 len,
                  u32 dest_addr_nbo, u16 dest_port_nbo) {
#if !defined(__linux__) && !defined(_WIN32)
        return -1;
#else
        if (!Start() || !IsValidSocket(host_socket) || (data == nullptr && len != 0) ||
            len > kMaxP2PPayload || dest_addr_nbo == 0 || dest_port_nbo == 0) {
            return -1;
        }

        std::vector<u8> framed(kVPortHeaderSize + len);
        std::memcpy(framed.data(), &source_vport_nbo, sizeof(source_vport_nbo));
        std::memcpy(framed.data() + sizeof(source_vport_nbo), &dest_vport_nbo,
                    sizeof(dest_vport_nbo));
        if (len != 0) {
            std::memcpy(framed.data() + kVPortHeaderSize, data, len);
        }

        const RelayPath path = RelayRoute(dest_addr_nbo, dest_port_nbo, framed);
        if (path == RelayPath::Relay) {
            return static_cast<int>(len);
        }

        sockaddr_in dest{};
        dest.sin_family = AF_INET;
        dest.sin_addr.s_addr = dest_addr_nbo;
        dest.sin_port = dest_port_nbo;

        const auto rc = ::sendto(host_socket, reinterpret_cast<const char*>(framed.data()),
                                 static_cast<int>(framed.size()), 0,
                                 reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
        if (rc < 0) {
            LOG_DEBUG(Lib_Net, "P2P sendto failed error={} dst={:#x}:{}", LastSocketError(),
                      dest_addr_nbo, ntohs(dest_port_nbo));
            return path == RelayPath::Both ? static_cast<int>(len) : -1;
        }
        if (static_cast<size_t>(rc) != framed.size()) {
            LOG_WARNING(Lib_Net, "P2P short UDP send: {} of {} bytes", rc, framed.size());
            return -1;
        }
        return static_cast<int>(len);
#endif
    }

    int SendInternal(const void* data, u32 len, u32 dest_addr_nbo, u16 dest_port_nbo) {
        return SendFrame(kSignalingVPortNbo, kSignalingVPortNbo, data, len, dest_addr_nbo,
                         dest_port_nbo);
    }

    int ReceiveInternal(InternalChannel channel, void* buf, u32 len, u32* from_addr,
                        u16* from_port) {
        std::lock_guard lock(internal_mutex);
        auto& queue = QueueFor(channel);
        if (queue.empty()) {
            return -1;
        }

        QueuedPacket packet = std::move(queue.front());
        queue.pop_front();

        const u32 copy_len = static_cast<u32>(std::min<size_t>(len, packet.payload.size()));
        if (copy_len != 0 && buf != nullptr) {
            std::memcpy(buf, packet.payload.data(), copy_len);
        }
        if (from_addr != nullptr) {
            *from_addr = packet.source_addr;
        }
        if (from_port != nullptr) {
            *from_port = packet.source_port;
        }
        return static_cast<int>(copy_len);
    }

private:
    P2PTransport() = default;

    std::deque<QueuedPacket>& QueueFor(InternalChannel channel) {
        switch (channel) {
        case InternalChannel::Signaling:
            return signaling_queue;
        case InternalChannel::Control:
            return control_queue;
        case InternalChannel::Matching2:
            return matching2_queue;
        }
        return signaling_queue;
    }

    static InternalChannel ClassifyInternal(const u8* payload, size_t size) {
        if (size >= 5 && std::memcmp(payload, "SHAD", 4) == 0) {
            if (payload[4] == 0x21) {
                return InternalChannel::Matching2;
            }
            if (payload[4] == 0x10) {
                return InternalChannel::Control;
            }
        }
        // STUN echo (6 bytes) and generic signaling 0x05/0x06/0x07 land here.
        return InternalChannel::Signaling;
    }

    void PushInternal(InternalChannel channel, u32 from_addr, u16 from_port, const u8* payload,
                      size_t size) {
        QueuedPacket packet{};
        packet.source_addr = from_addr;
        packet.source_port = from_port;
        packet.payload.assign(payload, payload + size);

        std::lock_guard lock(internal_mutex);
        QueueFor(channel).push_back(std::move(packet));
    }

    void ReceiveMain() {
#if defined(__linux__) || defined(_WIN32)
        std::vector<u8> buffer(65536);
        while (running.load(std::memory_order_acquire)) {
            // Before recvfrom: its send would overwrite recvfrom's error code.
            RelayKeepAlive();
            sockaddr_in from{};
            socklen_t from_len = sizeof(from);
#ifdef _WIN32
            constexpr int receive_flags = 0;
#else
            constexpr int receive_flags = MSG_DONTWAIT;
#endif
            const auto rc = ::recvfrom(host_socket, reinterpret_cast<char*>(buffer.data()),
                                       static_cast<int>(buffer.size()), receive_flags,
                                       reinterpret_cast<sockaddr*>(&from), &from_len);

            if (rc < 0) {
                const int error = LastSocketError();
                if (IsRetryableSocketError(error)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    continue;
                }
                LOG_WARNING(Lib_Net, "P2P recvfrom failed error={}", error);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }

            if (IsRelayServer(from.sin_addr.s_addr, from.sin_port)) {
                RelayReceive(buffer.data(), static_cast<size_t>(rc));
                continue;
            }
            NoteDirectReceive(from.sin_addr.s_addr, from.sin_port);
            DispatchFrame(from.sin_addr.s_addr, from.sin_port, buffer.data(),
                          static_cast<size_t>(rc));
        }
#endif
    }

    void DispatchFrame(u32 from_addr, u16 from_port, const u8* frame, size_t size) {
        if (size < kVPortHeaderSize) {
            LOG_DEBUG(Lib_Net, "P2P dropping too-short UDP frame size={}", size);
            return;
        }

        u16 source_vport_nbo = 0;
        u16 dest_vport_nbo = 0;
        std::memcpy(&source_vport_nbo, frame, sizeof(source_vport_nbo));
        std::memcpy(&dest_vport_nbo, frame + sizeof(source_vport_nbo), sizeof(dest_vport_nbo));

        const u8* payload = frame + kVPortHeaderSize;
        const size_t payload_size = size - kVPortHeaderSize;

        if (dest_vport_nbo == kSignalingVPortNbo) {
            PushInternal(ClassifyInternal(payload, payload_size), from_addr, from_port, payload,
                         payload_size);
            return;
        }

        std::lock_guard lock(registry_mutex);
        const auto it = game_sockets.find(dest_vport_nbo);
        if (it == game_sockets.end() || it->second == nullptr) {
            LOG_TRACE(Lib_Net, "P2P RX no socket for dst_vport={}", ntohs(dest_vport_nbo));
            return;
        }

        P2PDatagram packet{};
        packet.source_addr = from_addr;
        packet.source_port = from_port;
        packet.source_vport = source_vport_nbo;
        packet.payload.assign(payload, payload + payload_size);
        it->second->EnqueuePacket(std::move(packet));
    }

public:
    // ---- BBSC relay fallback (2026-10-12) ------------------------------------------------------
    // Direct P2P needs one side reachable: CGNAT, strict routers and several windows on one PC
    // (no hairpin) leave both sides unreachable and the connection goes DEAD after 30 s. Every
    // peer whose online id we know (shadNet lookups) starts direct; if nothing came back from it
    // directly 2 s after our first frame, its frames go through the game server's relay instead
    // (tools/gameserver/bbhost_service.py Relay._shad, UDP 3478):
    //   "BBR1" 0x01 id[16]                 register, every 5 s (keeps our NAT mapping open)
    //   "BBR1" 0x03 dst[16] src[16] ep[6] frame  frame for dst
    //   "BBR1" 0x04 src[16] ep[6] frame          frame from src
    //   "BBR1" 0x05 dst[16]                      dst is not on the relay (direct only for 10 s)
    // ep = the sender's own public address and port as shadNet sees them (its STUN echo), so the
    // receiver files the frame under the same endpoint its signaling code uses for that peer (an
    // ActivationRequest answer goes to the packet's mapped_addr + the frame's source port).
    // A peer that reaches us through the relay is answered through it at once.
    // Public players can be behind CGNAT or strict routers, so use the relay from the first frame.
    // Direct copies still go out until the peer answers. SHADPS4_P2P_RELAY=auto (or direct)
    // opts into direct-first with relay fallback after 2 s; =0/off disables the relay.
    void RelaySetServer(u32 addr_nbo) {
        std::lock_guard lock(relay_mutex);
        const char* env = std::getenv("SHADPS4_P2P_RELAY");
        const std::string mode = env != nullptr ? env : "";
        relay_force = mode != "auto" && mode != "direct";
        u16 port = 3478;
        if (const char* p = std::getenv("SHADPS4_P2P_RELAY_PORT"); p != nullptr && *p != 0) {
            const long parsed = std::strtol(p, nullptr, 10);
            if (parsed > 0 && parsed < 65536) {
                port = static_cast<u16>(parsed);
            }
        }
        if (mode == "0" || mode == "off") {
            addr_nbo = 0;
        } else if (const size_t colon = mode.find(':'); colon != std::string::npos) {
            // Test override "a.b.c.d:port" (a relay other than the shadNet server's).
            in_addr parsed{};
            const long parsed_port = std::strtol(mode.c_str() + colon + 1, nullptr, 10);
            if (addr_nbo != 0 &&
                ::inet_pton(AF_INET, mode.substr(0, colon).c_str(), &parsed) == 1 &&
                parsed_port > 0 && parsed_port < 65536) {
                addr_nbo = parsed.s_addr;
                port = static_cast<u16>(parsed_port);
            }
        }
        const u16 port_nbo = addr_nbo != 0 ? htons(port) : 0;
        if (relay_server_addr == addr_nbo && relay_server_port == port_nbo) {
            return;
        }
        relay_server_addr = addr_nbo;
        relay_server_port = port_nbo;
        relay_acked = false;
        relay_last_register = {};
        if (addr_nbo != 0) {
            LOG_INFO(Lib_Net, "P2P relay fallback: server {:#x}:{}{}", addr_nbo, port,
                     relay_force ? " (forced for every peer)" : "");
        } else {
            LOG_INFO(Lib_Net, "P2P relay fallback: off");
        }
    }

    void RelaySetSelfEndpoint(u32 addr_nbo, u16 port_nbo) {
        std::lock_guard lock(relay_mutex);
        if (relay_self_addr != addr_nbo || relay_self_port != port_nbo) {
            LOG_INFO(Lib_Net, "P2P relay fallback: our public endpoint {:#x}:{}", addr_nbo,
                     ntohs(port_nbo));
        }
        relay_self_addr = addr_nbo;
        relay_self_port = port_nbo;
    }

    void RelaySetLocalId(std::string_view online_id) {
        if (online_id.empty()) {
            return;
        }
        std::lock_guard lock(relay_mutex);
        const std::string id = PadId(online_id);
        if (std::find(relay_local_ids.begin(), relay_local_ids.end(), id) ==
            relay_local_ids.end()) {
            relay_local_ids.push_back(id);
            relay_last_register = {};
        }
    }

    void RelayRememberPeer(std::string_view online_id, u32 addr_nbo, u16 port_nbo) {
        if (online_id.empty() || addr_nbo == 0 || port_nbo == 0) {
            return;
        }
        std::lock_guard lock(relay_mutex);
        const std::string id = PadId(online_id);
        // Peers are kept by online id; every endpoint signaling or the relay ever gave for it
        // stays an alias. (Before 2026-10-13 the peer was filed under ONE endpoint and moved when
        // the relay brought another one - signaling's sends to the endpoint it knows then found
        // no relay entry and went direct only: one-way relay, DEAD after 30 s behind CGNAT.)
        RelayPeer& peer = relay_peers[id];
        peer.id = id;
        peer.addr = addr_nbo; // the endpoint signaling uses: relayed frames arrive "from" it
        peer.port = port_nbo;
        AddRelayAliasLocked(id, addr_nbo, port_nbo, "signaling");
    }

private:
    struct RelayPeer {
        std::string id;
        u32 addr{};
        u16 port{};
        bool relayed{false};
        bool direct_rx{false};
        bool tx_started{false};
        bool relay_seen{false}; // it sent to us through the relay (it has the relay too)
        std::chrono::steady_clock::time_point first_tx{};
        std::chrono::steady_clock::time_point relayed_since{};
        // The relay said it is not registered (no relay in its emulator, or not running): direct
        // only until then (asked again after it).
        std::chrono::steady_clock::time_point missing_until{};
        bool missing_logged{false};
        std::vector<std::vector<u8>> early; // sent directly before the switch (sent again then)
    };
    static constexpr size_t kRelayEarlyFrames = 32;

    // Direct = send directly only; Relay = through the relay only; Both = relay + a direct copy.
    // After the switch, frames still go directly too until the peer answers either way: an
    // emulator without the relay can only be reached directly (and NAT holes can take longer
    // than 2 s to open). A direct answer from a peer that never used the relay -> back to direct.
    // A peer that uses the relay gets direct copies for 10 s more, then the relay alone.
    enum class RelayPath { Direct, Relay, Both };

    static constexpr size_t kRelayIdSize = 16;
    static constexpr size_t kRelayEpSize = 6;
    static constexpr u8 kRelayMagic[4] = {'B', 'B', 'R', '1'};

    static std::string PadId(std::string_view online_id) {
        std::string id(online_id.substr(0, kRelayIdSize));
        id.resize(kRelayIdSize, static_cast<char>(0));
        return id;
    }

    static std::string IdText(const std::string& id) {
        return std::string(id.c_str());
    }

    static u64 EndpointKey(u32 addr_nbo, u16 port_nbo) {
        return (static_cast<u64>(addr_nbo) << 16) | port_nbo;
    }

    // relay_mutex held.
    void AddRelayAliasLocked(const std::string& id, u32 addr_nbo, u16 port_nbo, const char* from) {
        if (addr_nbo == 0 || port_nbo == 0) {
            return;
        }
        const u64 key = EndpointKey(addr_nbo, port_nbo);
        const auto it = relay_alias.find(key);
        if (it != relay_alias.end() && it->second == id) {
            return;
        }
        relay_alias[key] = id;
        LOG_INFO(Lib_Net, "P2P relay fallback: '{}' known at {:#x}:{} (from {})", IdText(id),
                 addr_nbo, ntohs(port_nbo), from);
    }

    // relay_mutex held.
    RelayPeer* FindRelayPeerLocked(u32 addr_nbo, u16 port_nbo) {
        const auto alias = relay_alias.find(EndpointKey(addr_nbo, port_nbo));
        if (alias == relay_alias.end()) {
            return nullptr;
        }
        const auto it = relay_peers.find(alias->second);
        return it == relay_peers.end() ? nullptr : &it->second;
    }

    bool IsRelayServer(u32 addr_nbo, u16 port_nbo) {
        std::lock_guard lock(relay_mutex);
        return relay_server_addr != 0 && addr_nbo == relay_server_addr &&
               port_nbo == relay_server_port;
    }

    // relay_mutex held.
    void SendToRelayLocked(const std::vector<u8>& packet) {
        sockaddr_in dest{};
        dest.sin_family = AF_INET;
        dest.sin_addr.s_addr = relay_server_addr;
        dest.sin_port = relay_server_port;
        (void)::sendto(host_socket, reinterpret_cast<const char*>(packet.data()),
                       static_cast<int>(packet.size()), 0,
                       reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
    }

    // Called from the receive loop (every few ms): register every 5 s (2 s until answered).
    void RelayKeepAlive() {
        std::lock_guard lock(relay_mutex);
        if (relay_server_addr == 0 || relay_local_ids.empty()) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto interval = relay_acked ? std::chrono::seconds(5) : std::chrono::seconds(2);
        if (relay_last_register.time_since_epoch().count() != 0 &&
            now - relay_last_register < interval) {
            return;
        }
        relay_last_register = now;
        if (now - relay_last_status >= std::chrono::seconds(30) &&
            (relay_sent != relay_status_sent || relay_received != relay_status_received)) {
            relay_last_status = now;
            relay_status_sent = relay_sent;
            relay_status_received = relay_received;
            std::string peers;
            for (const auto& [id, peer] : relay_peers) {
                if (!peers.empty()) {
                    peers += ", ";
                }
                peers += IdText(peer.id);
                peers += !peer.relayed                     ? " direct"
                         : now < peer.missing_until        ? " not on the relay"
                         : peer.relay_seen                 ? " relay"
                                                           : " relay (no answer yet)";
            }
            LOG_INFO(Lib_Net, "P2P relay fallback: {} frame(s) sent / {} received through the "
                              "relay so far; peers: {}",
                     relay_sent, relay_received, peers.empty() ? "none" : peers);
        }
        for (const std::string& id : relay_local_ids) {
            std::vector<u8> packet(std::begin(kRelayMagic), std::end(kRelayMagic));
            packet.push_back(0x01);
            packet.insert(packet.end(), id.begin(), id.end());
            SendToRelayLocked(packet);
        }
    }

    // Relay = sent through the relay, the caller sends nothing directly; Both = sent through the
    // relay and the caller sends it directly too; Direct = the caller sends it directly only.
    RelayPath RelayRoute(u32 dest_addr_nbo, u16 dest_port_nbo, const std::vector<u8>& framed) {
        std::lock_guard lock(relay_mutex);
        if (relay_server_addr == 0 || relay_local_ids.empty() ||
            dest_addr_nbo == relay_server_addr) {
            return RelayPath::Direct;
        }
        RelayPeer* const found = FindRelayPeerLocked(dest_addr_nbo, dest_port_nbo);
        if (found == nullptr) {
            return RelayPath::Direct;
        }
        RelayPeer& peer = *found;
        const auto now = std::chrono::steady_clock::now();
        if (peer.relayed && now < peer.missing_until) {
            return RelayPath::Direct;
        }
        if (!peer.relayed) {
            if (relay_force) {
                peer.relayed = true;
            } else if (!peer.tx_started) {
                peer.tx_started = true;
                peer.first_tx = now;
            } else if (!peer.direct_rx && now - peer.first_tx >= std::chrono::seconds(2)) {
                peer.relayed = true;
            }
            if (!peer.relayed) {
                // Kept for the switch: signaling sends some packets only once (the
                // ActivationRequest); lost directly, the peer ignores everything after it and
                // the connection dies after 30 s.
                if (!peer.direct_rx && peer.early.size() < kRelayEarlyFrames) {
                    peer.early.push_back(framed);
                }
                return RelayPath::Direct;
            }
            peer.relayed_since = now;
            LOG_INFO(Lib_Net,
                     "P2P relay fallback: '{}' at {:#x}:{} {} - through the relay ({} earlier "
                     "frame(s) sent again)",
                     IdText(peer.id), peer.addr, ntohs(peer.port),
                     relay_force ? "(forced)" : "sent nothing back directly in 2 s",
                     peer.early.size());
            for (const std::vector<u8>& early : peer.early) {
                SendRelayedLocked(peer, early);
            }
            peer.early.clear();
            peer.early.shrink_to_fit();
        }
        SendRelayedLocked(peer, framed);
        const bool direct_copy =
            !peer.direct_rx &&
            (!peer.relay_seen || now - peer.relayed_since < std::chrono::seconds(10));
        return direct_copy ? RelayPath::Both : RelayPath::Relay;
    }

    // relay_mutex held.
    void SendRelayedLocked(const RelayPeer& peer, const std::vector<u8>& framed) {
        const std::string& self = relay_local_ids.front();
        std::vector<u8> packet;
        packet.reserve(5 + 2 * kRelayIdSize + kRelayEpSize + framed.size());
        packet.insert(packet.end(), std::begin(kRelayMagic), std::end(kRelayMagic));
        packet.push_back(0x03);
        packet.insert(packet.end(), peer.id.begin(), peer.id.end());
        packet.insert(packet.end(), self.begin(), self.end());
        u8 ep[kRelayEpSize];
        std::memcpy(ep, &relay_self_addr, 4);
        std::memcpy(ep + 4, &relay_self_port, 2);
        packet.insert(packet.end(), ep, ep + kRelayEpSize);
        packet.insert(packet.end(), framed.begin(), framed.end());
        SendToRelayLocked(packet);
        ++relay_sent;
    }

    void NoteDirectReceive(u32 addr_nbo, u16 port_nbo) {
        std::lock_guard lock(relay_mutex);
        if (relay_peers.empty()) {
            return;
        }
        RelayPeer* const found = FindRelayPeerLocked(addr_nbo, port_nbo);
        if (found != nullptr && !found->direct_rx) {
            RelayPeer& peer = *found;
            peer.direct_rx = true;
            peer.early.clear();
            if (peer.relayed && !peer.relay_seen) {
                // It answers directly and never used the relay (an emulator without it, or a
                // NAT hole that took longer than 2 s): direct from now on.
                peer.relayed = false;
                LOG_INFO(Lib_Net, "P2P relay fallback: '{}' reached us directly - back to direct",
                         IdText(peer.id));
            } else {
                LOG_INFO(Lib_Net, "P2P relay fallback: '{}' reached us directly{}",
                         IdText(peer.id), peer.relayed ? " (staying on the relay)" : "");
            }
        }
    }

    void RelayReceive(const u8* data, size_t size) {
        if (size < 5 || std::memcmp(data, kRelayMagic, 4) != 0) {
            return;
        }
        if (data[4] == 0x02) {
            std::lock_guard lock(relay_mutex);
            if (!relay_acked) {
                relay_acked = true;
                LOG_INFO(Lib_Net, "P2P relay fallback: registered with the relay");
            }
            return;
        }
        if (data[4] == 0x05 && size >= 5 + kRelayIdSize) {
            const std::string id(reinterpret_cast<const char*>(data + 5), kRelayIdSize);
            std::lock_guard lock(relay_mutex);
            const auto known = relay_peers.find(id);
            if (known != relay_peers.end()) {
                RelayPeer& peer = known->second;
                peer.missing_until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                if (!peer.missing_logged) {
                    peer.missing_logged = true;
                    LOG_WARNING(Lib_Net,
                                "P2P relay fallback: '{}' is not on the relay (its emulator is "
                                "older than 2026-10-12, or it is not running) - direct only; if "
                                "direct does not work either, it cannot connect until it updates",
                                IdText(peer.id));
                    Core::Bloodborne::BbscTimelineEvent(
                        "NET", fmt::format("'{}' is not on the relay (old emulator or not running)",
                                           IdText(peer.id)));
                }
            }
            return;
        }
        if (data[4] != 0x04 || size < 5 + kRelayIdSize + kRelayEpSize) {
            return;
        }
        const std::string id(reinterpret_cast<const char*>(data + 5), kRelayIdSize);
        u32 ep_addr = 0;
        u16 ep_port = 0;
        std::memcpy(&ep_addr, data + 5 + kRelayIdSize, 4);
        std::memcpy(&ep_port, data + 5 + kRelayIdSize + 4, 2);
        u32 from_addr = 0;
        u16 from_port = 0;
        {
            std::lock_guard lock(relay_mutex);
            if (!relay_peers.contains(id)) {
                // A peer signaling never looked up: the endpoint it gave, else a made-up one
                // (169.254.x.x, never sent to directly) so the answers go back through the relay.
                RelayPeer& made = relay_peers[id];
                made.id = id;
                if (ep_addr != 0 && ep_port != 0) {
                    made.addr = ep_addr;
                    made.port = ep_port;
                } else {
                    made.addr = htonl(0xA9FE0000u | (relay_next_pseudo++ & 0xFFFFu));
                    made.port = htons(3173);
                }
                AddRelayAliasLocked(id, made.addr, made.port, "relay, new peer");
            }
            // Its own endpoint is one more alias; the peer keeps the endpoint signaling knows,
            // so the frame below arrives from where signaling expects it.
            AddRelayAliasLocked(id, ep_addr, ep_port, "relay");
            RelayPeer& peer = relay_peers[id];
            peer.relay_seen = true;
            peer.missing_until = {};
            if (!peer.relayed) {
                peer.relayed = true;
                peer.relayed_since = std::chrono::steady_clock::now();
                LOG_INFO(Lib_Net,
                         "P2P relay fallback: '{}' reached us through the relay - answering the "
                         "same way",
                         IdText(peer.id));
            }
            ++relay_received;
            from_addr = peer.addr;
            from_port = peer.port;
        }
        const size_t head = 5 + kRelayIdSize + kRelayEpSize;
        DispatchFrame(from_addr, from_port, data + head, size - head);
    }

    std::mutex relay_mutex;
    u32 relay_server_addr{0};
    u16 relay_server_port{0};
    bool relay_force{false};
    bool relay_acked{false};
    u32 relay_self_addr{0};
    u16 relay_self_port{0};
    std::chrono::steady_clock::time_point relay_last_register{};
    std::vector<std::string> relay_local_ids;
    std::unordered_map<std::string, RelayPeer> relay_peers; // by padded online id
    std::unordered_map<u64, std::string> relay_alias;       // every known endpoint -> online id
    u32 relay_next_pseudo{1};
    u64 relay_sent{0};
    u64 relay_received{0};
    std::chrono::steady_clock::time_point relay_last_status{};
    u64 relay_status_sent{0};
    u64 relay_status_received{0};

private:

    mutable std::mutex start_mutex;
    std::atomic<bool> running{false};
    std::thread receive_thread;

    net_socket host_socket{kInvalidSocket};
    u16 physical_port{0};

    std::mutex registry_mutex;
    std::unordered_map<u16, P2PSocket*> game_sockets;
    u16 next_ephemeral_vport{30000};

    std::mutex internal_mutex;
    std::deque<QueuedPacket> signaling_queue;
    std::deque<QueuedPacket> control_queue;
    std::deque<QueuedPacket> matching2_queue;
};

int ReadIntegerOption(const void* optval, u32 optlen, int* out) {
    if (optval == nullptr || out == nullptr) {
        return FailWith(ORBIS_NET_EFAULT);
    }
    if (optlen < sizeof(int)) {
        return FailWith(ORBIS_NET_EINVAL);
    }
    std::memcpy(out, optval, sizeof(int));
    return 0;
}

int WriteIntegerOption(void* optval, u32* optlen, int value) {
    if (optval == nullptr || optlen == nullptr) {
        return FailWith(ORBIS_NET_EFAULT);
    }
    if (*optlen < sizeof(int)) {
        return FailWith(ORBIS_NET_EINVAL);
    }
    std::memcpy(optval, &value, sizeof(int));
    *optlen = sizeof(int);
    return 0;
}

} // namespace

P2PSocket::P2PSocket(int domain, int type, int protocol) : Socket(domain, type, protocol) {
#ifdef _WIN32
    if (!CreateReadinessPair(&readiness_fd, &readiness_signal_fd)) {
        LOG_ERROR(Lib_Net, "P2P readiness socket pair failed error={}", LastSocketError());
        valid = false;
    }
#elif defined(__linux__)
    readiness_fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (readiness_fd < 0) {
        LOG_ERROR(Lib_Net, "P2P eventfd() failed errno={}", errno);
        valid = false;
    }
#endif
}

P2PSocket::~P2PSocket() {
    Close();
}

int P2PSocket::Close() {
    std::lock_guard lock(m_mutex);
    if (!valid) {
        return 0;
    }

    if (bound) {
        P2PTransport::Instance().UnregisterSocket(this, local_addr.sin_vport);
        bound = false;
    }

    {
        std::lock_guard receive_lock(receive_mutex);
        valid = false;
    }
    connected = false;
    receive_cv.notify_all();

#ifdef _WIN32
    CloseSocket(readiness_signal_fd);
    CloseSocket(readiness_fd);
#elif defined(__linux__)
    if (readiness_fd >= 0) {
        ::close(readiness_fd);
        readiness_fd = -1;
    }
#endif
    return 0;
}

int P2PSocket::SetSocketOptions(int level, int optname, const void* optval, u32 optlen) {
    if (level != ORBIS_NET_SOL_SOCKET) {
        LOG_WARNING(Lib_Net, "P2P setsockopt ignoring non-SOL_SOCKET level={} opt={}", level,
                    optname);
        return 0;
    }

    int value = 0;
    switch (optname) {
    case ORBIS_NET_SO_NBIO:
    case ORBIS_NET_SO_USECRYPTO:
    case ORBIS_NET_SO_USESIGNATURE:
    case ORBIS_NET_SO_REUSEADDR:
    case ORBIS_NET_SO_REUSEPORT:
    case ORBIS_NET_SO_KEEPALIVE:
    case ORBIS_NET_SO_BROADCAST:
    case ORBIS_NET_SO_ONESBCAST:
    case ORBIS_NET_SO_SNDBUF:
    case ORBIS_NET_SO_RCVBUF:
    case ORBIS_NET_SO_PRIORITY:
        if (ReadIntegerOption(optval, optlen, &value) < 0) {
            return -1;
        }
        integer_socket_options[optname] = value;
        if (optname == ORBIS_NET_SO_NBIO) {
            std::lock_guard receive_lock(receive_mutex);
            sockopt_so_nbio = value;
        } else if (optname == ORBIS_NET_SO_USECRYPTO) {
            sockopt_so_usecrypto = value;
        } else if (optname == ORBIS_NET_SO_USESIGNATURE) {
            sockopt_so_usesignature = value;
        }
        return 0;
    default:
        // For the first emulator-to-emulator PoC, remember that an option was requested
        // but do not fail the title merely because the Sony-specific behavior is absent.
        LOG_WARNING(Lib_Net, "P2P setsockopt opt={:#x} accepted as no-op", optname);
        return 0;
    }
}

int P2PSocket::GetSocketOptions(int level, int optname, void* optval, u32* optlen) {
    if (level != ORBIS_NET_SOL_SOCKET) {
        return FailWith(ORBIS_NET_ENOPROTOOPT);
    }

    switch (optname) {
    case ORBIS_NET_SO_NBIO:
        return WriteIntegerOption(optval, optlen, sockopt_so_nbio);
    case ORBIS_NET_SO_USECRYPTO:
        return WriteIntegerOption(optval, optlen, sockopt_so_usecrypto);
    case ORBIS_NET_SO_USESIGNATURE:
        return WriteIntegerOption(optval, optlen, sockopt_so_usesignature);
    case ORBIS_NET_SO_TYPE:
        return WriteIntegerOption(optval, optlen, socket_type);
    case ORBIS_NET_SO_ERROR:
    case ORBIS_NET_SO_ERROR_EX:
        return WriteIntegerOption(optval, optlen, 0);
    default:
        if (const auto it = integer_socket_options.find(optname);
            it != integer_socket_options.end()) {
            return WriteIntegerOption(optval, optlen, it->second);
        }
        return FailWith(ORBIS_NET_ENOPROTOOPT);
    }
}

int P2PSocket::Bind(const OrbisNetSockaddr* addr, u32 addrlen) {
    if (socket_type != ORBIS_NET_SOCK_DGRAM_P2P) {
        return FailWith(ORBIS_NET_EOPNOTSUPP);
    }
    if (addr == nullptr) {
        return FailWith(ORBIS_NET_EFAULT);
    }
    if (addrlen < sizeof(OrbisNetSockaddrIn)) {
        return FailWith(ORBIS_NET_EINVAL);
    }

    const auto* in = reinterpret_cast<const OrbisNetSockaddrIn*>(addr);
    if (in->sin_family != ORBIS_NET_AF_INET) {
        return FailWith(ORBIS_NET_EAFNOSUPPORT);
    }
    if (!EnsureP2PTransport()) {
        return FailWith(ORBIS_NET_ENETDOWN);
    }

    std::lock_guard lock(m_mutex);
    if (!valid) {
        return FailWith(ORBIS_NET_EBADF);
    }
    if (bound) {
        return FailWith(ORBIS_NET_EINVAL);
    }

    u16 actual_vport = 0;
    if (!P2PTransport::Instance().RegisterSocket(this, in->sin_vport, &actual_vport)) {
        return FailWith(ORBIS_NET_EADDRINUSE);
    }

    local_addr = *in;
    local_addr.sin_len = sizeof(OrbisNetSockaddrIn);
    local_addr.sin_family = ORBIS_NET_AF_INET;
    local_addr.sin_port = htons(P2PTransport::Instance().PhysicalPort());
    local_addr.sin_vport = actual_vport;
    bound = true;

    LOG_INFO(Lib_Net, "P2P bind: physical_port={} virtual_port={}",
             P2PTransport::Instance().PhysicalPort(), ntohs(local_addr.sin_vport));
    return 0;
}

int P2PSocket::Listen(int backlog) {
    LOG_WARNING(Lib_Net, "P2P STREAM listen not implemented (backlog={})", backlog);
    return FailWith(ORBIS_NET_EOPNOTSUPP);
}

int P2PSocket::SendMessage(const OrbisNetMsghdr* msg, int flags) {
    if (msg == nullptr || msg->msg_iovlen < 0) {
        return FailWith(ORBIS_NET_EINVAL);
    }

    size_t total = 0;
    for (int i = 0; i < msg->msg_iovlen; ++i) {
        total += static_cast<size_t>(msg->msg_iov[i].iov_len);
        if (total > kMaxP2PPayload || total > std::numeric_limits<u32>::max()) {
            return FailWith(ORBIS_NET_EMSGSIZE);
        }
    }

    std::vector<u8> payload(total);
    size_t offset = 0;
    for (int i = 0; i < msg->msg_iovlen; ++i) {
        const auto len = static_cast<size_t>(msg->msg_iov[i].iov_len);
        if (len != 0) {
            std::memcpy(payload.data() + offset, msg->msg_iov[i].iov_base, len);
        }
        offset += len;
    }

    return SendPacket(payload.data(), static_cast<u32>(payload.size()), flags,
                      reinterpret_cast<const OrbisNetSockaddr*>(msg->msg_name), msg->msg_namelen);
}

int P2PSocket::SendPacket(const void* msg, u32 len, int flags, const OrbisNetSockaddr* to,
                          u32 tolen) {
    if (socket_type != ORBIS_NET_SOCK_DGRAM_P2P) {
        return FailWith(ORBIS_NET_EOPNOTSUPP);
    }
    if (msg == nullptr && len != 0) {
        return FailWith(ORBIS_NET_EFAULT);
    }
    if (len > kMaxP2PPayload) {
        return FailWith(ORBIS_NET_EMSGSIZE);
    }
    if (!EnsureP2PTransport()) {
        return FailWith(ORBIS_NET_ENETDOWN);
    }

    std::lock_guard lock(m_mutex);
    if (!valid) {
        return FailWith(ORBIS_NET_EBADF);
    }

    // UDP permits implicit local binding. Give an unbound guest socket an ephemeral vport.
    if (!bound) {
        u16 actual_vport = 0;
        if (!P2PTransport::Instance().RegisterSocket(this, 0, &actual_vport)) {
            return FailWith(ORBIS_NET_EADDRINUSE);
        }
        local_addr = {};
        local_addr.sin_len = sizeof(local_addr);
        local_addr.sin_family = ORBIS_NET_AF_INET;
        local_addr.sin_port = htons(P2PTransport::Instance().PhysicalPort());
        local_addr.sin_vport = actual_vport;
        bound = true;
    }

    OrbisNetSockaddrIn target{};
    if (to != nullptr) {
        if (tolen < sizeof(OrbisNetSockaddrIn)) {
            return FailWith(ORBIS_NET_EINVAL);
        }
        target = *reinterpret_cast<const OrbisNetSockaddrIn*>(to);
        if (target.sin_family != ORBIS_NET_AF_INET) {
            return FailWith(ORBIS_NET_EAFNOSUPPORT);
        }
    } else {
        if (!connected) {
            return FailWith(ORBIS_NET_ENOTCONN);
        }
        target = peer_addr;
    }

    const int rc = P2PTransport::Instance().SendFrame(local_addr.sin_vport, target.sin_vport, msg,
                                                      len, target.sin_addr, target.sin_port);
    if (rc < 0) {
        return FailWith(ORBIS_NET_EHOSTUNREACH);
    }

    LOG_TRACE(Lib_Net, "P2P TX {} bytes vport {} -> {} dst={:#x}:{}", len,
              ntohs(local_addr.sin_vport), ntohs(target.sin_vport), target.sin_addr,
              ntohs(target.sin_port));
    return rc;
}

int P2PSocket::ReceiveMessage(OrbisNetMsghdr* msg, int flags) {
    if (msg == nullptr || msg->msg_iovlen < 0) {
        return FailWith(ORBIS_NET_EINVAL);
    }

    size_t capacity = 0;
    for (int i = 0; i < msg->msg_iovlen; ++i) {
        capacity += static_cast<size_t>(msg->msg_iov[i].iov_len);
        if (capacity > std::numeric_limits<u32>::max()) {
            return FailWith(ORBIS_NET_EMSGSIZE);
        }
    }

    std::vector<u8> payload(capacity);
    u32 name_len = msg->msg_namelen;
    const int rc = ReceivePacket(payload.data(), static_cast<u32>(payload.size()), flags,
                                 reinterpret_cast<OrbisNetSockaddr*>(msg->msg_name),
                                 msg->msg_name != nullptr ? &name_len : nullptr);
    if (rc < 0) {
        return rc;
    }
    msg->msg_namelen = name_len;
    msg->msg_flags = 0;

    size_t remaining = static_cast<size_t>(rc);
    size_t offset = 0;
    for (int i = 0; i < msg->msg_iovlen && remaining != 0; ++i) {
        const size_t amount =
            std::min<size_t>(remaining, static_cast<size_t>(msg->msg_iov[i].iov_len));
        if (amount != 0) {
            std::memcpy(msg->msg_iov[i].iov_base, payload.data() + offset, amount);
        }
        offset += amount;
        remaining -= amount;
    }
    return rc;
}

int P2PSocket::ReceivePacket(void* buf, u32 len, int flags, OrbisNetSockaddr* from, u32* fromlen) {
    if (buf == nullptr && len != 0) {
        return FailWith(ORBIS_NET_EFAULT);
    }

    std::unique_lock lock(receive_mutex);
    while (receive_queue.empty() && valid) {
        if (sockopt_so_nbio != 0 || (flags & ORBIS_NET_MSG_DONTWAIT) != 0) {
            return FailWith(ORBIS_NET_EWOULDBLOCK);
        }
        receive_cv.wait(lock);
    }

    if (!valid) {
        return FailWith(ORBIS_NET_EBADF);
    }

    const P2PDatagram& packet = receive_queue.front();
    const u32 amount = static_cast<u32>(std::min<size_t>(len, packet.payload.size()));
    if (amount != 0) {
        std::memcpy(buf, packet.payload.data(), amount);
    }

    if (from != nullptr && fromlen != nullptr) {
        if (*fromlen < sizeof(OrbisNetSockaddrIn)) {
            return FailWith(ORBIS_NET_EINVAL);
        }
        auto* out = reinterpret_cast<OrbisNetSockaddrIn*>(from);
        *out = {};
        out->sin_len = sizeof(OrbisNetSockaddrIn);
        out->sin_family = ORBIS_NET_AF_INET;
        out->sin_addr = packet.source_addr;
        out->sin_port = packet.source_port;
        out->sin_vport = packet.source_vport;
        *fromlen = sizeof(OrbisNetSockaddrIn);
    }

    const bool peek = (flags & ORBIS_NET_MSG_PEEK) != 0;
    const bool peek_len =
        (flags & ORBIS_NET_MSG_PEEKLEN) == static_cast<int>(ORBIS_NET_MSG_PEEKLEN);
    const int result =
        peek_len ? static_cast<int>(packet.payload.size()) : static_cast<int>(amount);

    if (!peek) {
        receive_queue.pop_front();
#ifdef _WIN32
        if (receive_queue.empty() && IsValidSocket(readiness_fd)) {
            char signal = 0;
            (void)::recv(readiness_fd, &signal, sizeof(signal), 0);
        }
#elif defined(__linux__)
        if (receive_queue.empty() && readiness_fd >= 0) {
            u64 event_count = 0;
            (void)::read(readiness_fd, &event_count, sizeof(event_count));
        }
#endif
    }

    return result;
}

SocketPtr P2PSocket::Accept(OrbisNetSockaddr* addr, u32* addrlen) {
    LOG_WARNING(Lib_Net, "P2P STREAM accept not implemented");
    *Libraries::Kernel::__Error() = ORBIS_NET_EOPNOTSUPP;
    return nullptr;
}

int P2PSocket::Connect(const OrbisNetSockaddr* addr, u32 namelen) {
    if (socket_type != ORBIS_NET_SOCK_DGRAM_P2P) {
        return FailWith(ORBIS_NET_EOPNOTSUPP);
    }
    if (addr == nullptr) {
        return FailWith(ORBIS_NET_EFAULT);
    }
    if (namelen < sizeof(OrbisNetSockaddrIn)) {
        return FailWith(ORBIS_NET_EINVAL);
    }

    const auto* in = reinterpret_cast<const OrbisNetSockaddrIn*>(addr);
    if (in->sin_family != ORBIS_NET_AF_INET) {
        return FailWith(ORBIS_NET_EAFNOSUPPORT);
    }
    if (!EnsureP2PTransport()) {
        return FailWith(ORBIS_NET_ENETDOWN);
    }

    std::lock_guard lock(m_mutex);
    peer_addr = *in;
    connected = true;
    return 0;
}

int P2PSocket::GetSocketAddress(OrbisNetSockaddr* name, u32* namelen) {
    if (name == nullptr || namelen == nullptr) {
        return FailWith(ORBIS_NET_EFAULT);
    }
    if (*namelen < sizeof(OrbisNetSockaddrIn)) {
        return FailWith(ORBIS_NET_EINVAL);
    }

    auto out = local_addr;
    out.sin_len = sizeof(out);
    out.sin_family = ORBIS_NET_AF_INET;
    if (!bound && EnsureP2PTransport()) {
        out.sin_port = htons(GetP2PConfiguredPort());
    }
    std::memcpy(name, &out, sizeof(out));
    *namelen = sizeof(out);
    return 0;
}

int P2PSocket::GetPeerName(OrbisNetSockaddr* addr, u32* namelen) {
    if (!connected) {
        return FailWith(ORBIS_NET_ENOTCONN);
    }
    if (addr == nullptr || namelen == nullptr) {
        return FailWith(ORBIS_NET_EFAULT);
    }
    if (*namelen < sizeof(OrbisNetSockaddrIn)) {
        return FailWith(ORBIS_NET_EINVAL);
    }
    std::memcpy(addr, &peer_addr, sizeof(peer_addr));
    *namelen = sizeof(peer_addr);
    return 0;
}

int P2PSocket::fstat(Libraries::Kernel::OrbisKernelStat* stat) {
    LOG_DEBUG(Lib_Net, "P2P fstat called");
    return 0;
}

void P2PSocket::EnqueuePacket(P2PDatagram packet) {
    std::lock_guard lock(receive_mutex);
    if (!valid) {
        return;
    }

    const bool was_empty = receive_queue.empty();
    LOG_TRACE(Lib_Net, "P2P RX {} bytes vport {} <- {} src={:#x}:{}", packet.payload.size(),
              ntohs(local_addr.sin_vport), ntohs(packet.source_vport), packet.source_addr,
              ntohs(packet.source_port));
    receive_queue.push_back(std::move(packet));

#ifdef _WIN32
    if (was_empty && IsValidSocket(readiness_signal_fd)) {
        constexpr char signal = 1;
        if (::send(readiness_signal_fd, &signal, sizeof(signal), 0) < 0) {
            LOG_WARNING(Lib_Net, "P2P readiness signal failed error={}", LastSocketError());
        }
    }
#elif defined(__linux__)
    if (was_empty && readiness_fd >= 0) {
        const u64 one = 1;
        (void)::write(readiness_fd, &one, sizeof(one));
    }
#endif
    receive_cv.notify_all();
}

u16 GetP2PConfiguredPort() {
    return P2PTransport::Instance().PhysicalPort();
}

u32 GetP2PAdvertisedAddr() {
    auto* netinfo = Common::Singleton<NetUtil::NetUtilInternal>::Instance();
    return netinfo->GetExternalIp();
}

bool EnsureP2PTransport() {
    return P2PTransport::Instance().Start();
}

bool P2PTransportIsReady() {
    return P2PTransport::Instance().IsReady();
}

int P2PSignalingSendTo(const void* data, u32 len, u32 dest_addr, u16 dest_port) {
    return P2PTransport::Instance().SendInternal(data, len, dest_addr, dest_port);
}

int P2PSignalingRecvFrom(void* buf, u32 len, u32* from_addr, u16* from_port) {
    return P2PTransport::Instance().ReceiveInternal(InternalChannel::Signaling, buf, len, from_addr,
                                                    from_port);
}

int P2PControlSendTo(const void* data, u32 len, u32 dest_addr, u16 dest_port) {
    return P2PTransport::Instance().SendInternal(data, len, dest_addr, dest_port);
}

int P2PControlRecvFrom(void* buf, u32 len, u32* from_addr, u16* from_port) {
    return P2PTransport::Instance().ReceiveInternal(InternalChannel::Control, buf, len, from_addr,
                                                    from_port);
}

int P2PMatching2SendTo(const void* data, u32 len, u32 dest_addr, u16 dest_port) {
    return P2PTransport::Instance().SendInternal(data, len, dest_addr, dest_port);
}

int P2PMatching2RecvFrom(void* buf, u32 len, u32* from_addr, u16* from_port) {
    return P2PTransport::Instance().ReceiveInternal(InternalChannel::Matching2, buf, len, from_addr,
                                                    from_port);
}

void P2PRelaySetServer(u32 addr_nbo) {
    P2PTransport::Instance().RelaySetServer(addr_nbo);
}

void P2PRelaySetSelfEndpoint(u32 addr_nbo, u16 port_nbo) {
    P2PTransport::Instance().RelaySetSelfEndpoint(addr_nbo, port_nbo);
}

void P2PRelaySetLocalId(std::string_view online_id) {
    P2PTransport::Instance().RelaySetLocalId(online_id);
}

void P2PRelayRememberPeer(std::string_view online_id, u32 addr_nbo, u16 port_nbo) {
    P2PTransport::Instance().RelayRememberPeer(online_id, addr_nbo, port_nbo);
}

} // namespace Libraries::Net
