// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

// BBSC mod: the P2P channel - BBSC's own packets over the game's own player-to-player connection,
// for servers that run no BBSC relay (the public ones: The Hunter's Dream + shadNet).
//
// The players of a session already exchange P2P datagrams (the game's socket, vport 40). The
// emulator's P2P transport (official shadPS4: core/net/p2p_transport.cpp; the shadp2p fork:
// network/p2p_sockets.cpp) carries a source and destination vport in every frame and hands each
// frame to the socket bound to that vport, so a second socket on a vport of our own sees only
// BBSC's frames and the game never sees them.
//
//   - Peers: the game's import slots for sceNetSendto/sceNetRecvfrom are wrapped; every address
//     the game sends to or receives from on a P2P vport is a player of our session (kept while
//     the game talks to it, dropped 20 s after). sceNpSignalingGetConnectionStatus is wrapped too
//     (ACTIVE connections give the peer's address).
//   - Socket: our own DGRAM_P2P socket on ChannelVport, made through the emulator's sceNet
//     functions (the originals from the same slots), non-blocking, read by our own thread.
//   - Frames: the relay's frames unchanged (the "SHAD" 0x40 envelope with sender + session,
//     bbsc_relay.cpp), one datagram each; the receive side is the relay's Dispatch.
//
// Import slots, CUSA03173 01.09 (tools\eboot-imports.py, BBSC offsets):
//   0x053E4B00 sceNetSocket   0x053E4B38 sceNetBind   0x053E4B68 sceNetSetsockopt
//   0x053E4F48 sceNetSendto   0x053E4F58 sceNetRecvfrom
//   0x053E52D0 sceNpSignalingGetConnectionStatus
// Switch SHADPS4_BBSC_P2P_CHANNEL (Mods tab, on). Datagrams can be lost: every BBSC packet was
// made for the emulator's UDP signaling first and is re-sent or repeated by its sender.

#include "bbsc_platform.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "common/logging/log.h"
#include "common/types.h"

namespace BbscMod {
u64 EbootAddress(); // bbsc_mod.cpp
void GuardSlot(u64 slot_offset, u64 wrapper, void** original, const char* name);
void RelayDispatch(const std::vector<u8>& wrapped); // bbsc_relay.cpp
} // namespace BbscMod

namespace {

constexpr u64 EbootSize = 0x056D4000ULL;
constexpr u64 SlotSocket = 0x053E4B00;
constexpr u64 SlotSocketClose = 0x053E4B08;
constexpr u64 SlotBind = 0x053E4B38;
constexpr u64 SlotSetsockopt = 0x053E4B68;
constexpr u64 SlotSendto = 0x053E4F48;
constexpr u64 SlotRecvfrom = 0x053E4F58;
constexpr u64 SlotConnectionStatus = 0x053E52D0;

constexpr u16 ChannelVport = 3173; // below 32768 (official shadPS4 reserves the rest); game uses 40
constexpr s32 OrbisAfInet = 2;
constexpr s32 OrbisSockDgramP2P = 6;
constexpr s32 OrbisSolSocket = 0xFFFF;
constexpr s32 OrbisSoNbio = 0x1200;
constexpr s32 OrbisMsgDontwait = 0x80;
constexpr u32 SignalingConnActive = 2;
constexpr s64 PeerKeepMs = 20'000;
constexpr size_t MaxDatagram = 2048;

#pragma pack(push, 1)
struct OrbisSockaddrIn {
    u8 sin_len;
    u8 sin_family;
    u16 sin_port;  // network order (P2P datagrams: the peer's UDP port)
    u32 sin_addr;  // network order
    u16 sin_vport; // network order
    char sin_zero[6];
};
#pragma pack(pop)
static_assert(sizeof(OrbisSockaddrIn) == 16);

using SocketFn = s32 PS4_SYSV_ABI (*)(const char*, s32, s32, s32);
using SocketCloseFn = s32 PS4_SYSV_ABI (*)(s32);
using BindFn = s32 PS4_SYSV_ABI (*)(s32, const void*, u32);
using SetsockoptFn = s32 PS4_SYSV_ABI (*)(s32, s32, s32, const void*, u32);
using SendtoFn = s32 PS4_SYSV_ABI (*)(s32, const void*, u64, s32, const void*, u32);
using RecvfromFn = s32 PS4_SYSV_ABI (*)(s32, void*, u64, s32, void*, u32*);
using ConnectionStatusFn = s32 PS4_SYSV_ABI (*)(s32, s32, u32*, u32*, u16*);

SendtoFn original_sendto = nullptr;
RecvfromFn original_recvfrom = nullptr;
ConnectionStatusFn original_connection_status = nullptr;

struct Peer {
    s64 seen_ms = 0;
    bool logged = false;
};

std::mutex peers_mutex;
std::map<u64, Peer> peers; // key: addr << 16 | port (both network order)
std::atomic<s32> channel_socket{-1};
std::atomic<bool> enabled{false};
std::atomic<u32> sent_count{0};
std::atomic<u32> received_count{0};

s64 NowMs() {
    return static_cast<s64>(BbscMod::Platform::NowMs());
}

std::string AddressText(u32 addr, u16 port) {
    const auto* b = reinterpret_cast<const u8*>(&addr);
    return fmt::format("{}.{}.{}.x:{}", b[0], b[1], b[2], static_cast<u16>((port >> 8) | (port << 8)));
}

void NotePeer(u32 addr, u16 port, const char* how) {
    if (addr == 0 || port == 0) {
        return;
    }
    const u64 key = (static_cast<u64>(addr) << 16) | port;
    std::scoped_lock lock{peers_mutex};
    Peer& peer = peers[key];
    const bool fresh = !peer.logged || NowMs() - peer.seen_ms > PeerKeepMs;
    peer.seen_ms = NowMs();
    if (fresh) {
        peer.logged = true;
        LOG_INFO(Mod, "P2P channel: player at {} ({})", AddressText(addr, port), how);
    }
}

void NoteGameAddress(const void* addr, u32 len, const char* how) {
    if (!enabled || addr == nullptr || len < sizeof(OrbisSockaddrIn)) {
        return;
    }
    OrbisSockaddrIn in{};
    std::memcpy(&in, addr, sizeof(in));
    // A P2P address has a vport; plain UDP/TCP sockets of the game leave it 0.
    if (in.sin_family != OrbisAfInet || in.sin_vport == 0 ||
        in.sin_vport == static_cast<u16>((ChannelVport >> 8) | (ChannelVport << 8))) {
        return;
    }
    NotePeer(in.sin_addr, in.sin_port, how);
}

s32 PS4_SYSV_ABI Sendto(s32 s, const void* buf, u64 len, s32 flags, const void* addr, u32 addrlen) {
    const s32 r = original_sendto(s, buf, len, flags, addr, addrlen);
    if (r > 0 && s != channel_socket) {
        NoteGameAddress(addr, addrlen, "game sends to it");
    }
    return r;
}

s32 PS4_SYSV_ABI Recvfrom(s32 s, void* buf, u64 len, s32 flags, void* addr, u32* addrlen) {
    const s32 r = original_recvfrom(s, buf, len, flags, addr, addrlen);
    if (r > 0 && addrlen != nullptr && s != channel_socket) {
        NoteGameAddress(addr, *addrlen, "game hears from it");
    }
    return r;
}

s32 PS4_SYSV_ABI ConnectionStatus(s32 ctx, s32 conn, u32* status, u32* addr, u16* port) {
    // The game may pass no address pointers: ask with our own, hand back what it asked for.
    u32 own_addr = 0;
    u16 own_port = 0;
    const s32 r = original_connection_status(ctx, conn, status, addr ? addr : &own_addr,
                                             port ? port : &own_port);
    if (r == 0 && status != nullptr && *status == SignalingConnActive) {
        NotePeer(addr ? *addr : own_addr, port ? *port : own_port, "signaling connection active");
    }
    return r;
}

template <typename Fn>
bool Swap(u64 slot_offset, Fn wrapper, Fn& original, const char* name) {
    const u64 slot = BbscMod::EbootAddress() + slot_offset;
    for (int i = 0; i < 60'000; ++i) {
        const u64 value = BbscMod::Platform::ReadSlot(slot);
        if (value != 0 &&
            (value < BbscMod::EbootAddress() || value >= BbscMod::EbootAddress() + EbootSize)) {
            original = reinterpret_cast<Fn>(value);
            BbscMod::Platform::WriteSlot(slot, reinterpret_cast<u64>(wrapper));
            BbscMod::GuardSlot(slot_offset, reinterpret_cast<u64>(wrapper),
                               reinterpret_cast<void**>(&original), name);
            return true;
        }
        BbscMod::Platform::SleepMs(1);
    }
    return false;
}

template <typename Fn>
Fn ReadSlot(u64 slot_offset) {
    const u64 value = BbscMod::Platform::ReadSlot(BbscMod::EbootAddress() + slot_offset);
    if (value == 0 || (value >= BbscMod::EbootAddress() && value < BbscMod::EbootAddress() + EbootSize)) {
        return nullptr;
    }
    return reinterpret_cast<Fn>(value);
}

// Made on the first peer: the emulator starts its P2P transport when the game needs it.
// Made on the first peer: the emulator starts its P2P transport when the game needs it. The slots
// are read here, not at start-up (review 2026-10-11): the official shadPS4 links the imports again
// after BBSC starts, and these slots are not wrapped, so they always hold the emulator's current
// functions.
bool OpenSocket() {
    const auto open = ReadSlot<SocketFn>(SlotSocket);
    const auto bind = ReadSlot<BindFn>(SlotBind);
    const auto setsockopt = ReadSlot<SetsockoptFn>(SlotSetsockopt);
    const auto close = ReadSlot<SocketCloseFn>(SlotSocketClose);
    if (open == nullptr || bind == nullptr || setsockopt == nullptr) {
        LOG_WARNING(Mod, "P2P channel: the game's socket imports are not filled - retrying");
        return false;
    }
    const s32 s = open("BBSC channel", OrbisAfInet, OrbisSockDgramP2P, 0);
    if (s < 0) {
        LOG_WARNING(Mod, "P2P channel: sceNetSocket failed ({:#x}) - retrying", static_cast<u32>(s));
        return false;
    }
    OrbisSockaddrIn bind_to{};
    bind_to.sin_len = sizeof(bind_to);
    bind_to.sin_family = OrbisAfInet;
    bind_to.sin_vport = static_cast<u16>((ChannelVport >> 8) | (ChannelVport << 8));
    if (const s32 r = bind(s, &bind_to, sizeof(bind_to)); r < 0) {
        LOG_WARNING(Mod, "P2P channel: bind to vport {} failed ({:#x}) - retrying", ChannelVport,
                    static_cast<u32>(r));
        if (close != nullptr) {
            close(s);
        }
        return false;
    }
    const s32 on = 1;
    setsockopt(s, OrbisSolSocket, OrbisSoNbio, &on, sizeof(on));
    channel_socket = s;
    LOG_INFO(Mod, "P2P channel: socket {} on vport {} - BBSC packets go to the players of the "
                  "session directly",
             s, ChannelVport);
    return true;
}

void ChannelThread() {
    std::vector<u8> buffer(MaxDatagram);
    s64 next_open_ms = 0;
    int failures = 0;
    s64 next_report_ms = NowMs() + 60'000;
    for (;;) {
        const s32 s = channel_socket;
        if (s < 0) {
            bool any = false;
            {
                std::scoped_lock lock{peers_mutex};
                any = !peers.empty();
            }
            if (any && NowMs() >= next_open_ms && failures < 10 && !OpenSocket()) {
                next_open_ms = NowMs() + 5'000;
                if (++failures == 10) {
                    LOG_ERROR(Mod, "P2P channel: no socket after 10 tries - channel off for this run");
                }
            }
            BbscMod::Platform::SleepMs(100);
            continue;
        }
        bool got_any = false;
        for (int i = 0; i < 64; ++i) {
            OrbisSockaddrIn from{};
            u32 from_len = sizeof(from);
            const s32 r = original_recvfrom(s, buffer.data(), buffer.size(), OrbisMsgDontwait,
                                            &from, &from_len);
            if (r <= 0) {
                break;
            }
            got_any = true;
            ++received_count;
            NotePeer(from.sin_addr, from.sin_port, "BBSC packet from it");
            BbscMod::RelayDispatch(std::vector<u8>(buffer.begin(), buffer.begin() + r));
        }
        if (NowMs() >= next_report_ms) {
            next_report_ms = NowMs() + 60'000;
            const u32 sent = sent_count.exchange(0);
            const u32 received = received_count.exchange(0);
            if (sent != 0 || received != 0) {
                LOG_INFO(Mod, "P2P channel: last minute {} packet(s) sent, {} received", sent,
                         received);
            }
        }
        BbscMod::Platform::SleepMs(got_any ? 1 : 5);
    }
}

} // namespace

namespace BbscMod {

bool P2PChannelEnabled() {
    return enabled;
}

// One relay frame (envelope included, no length prefix) to every live peer. Returns the peer count.
u32 P2PChannelSend(const void* data, u32 size) {
    const s32 s = channel_socket;
    if (!enabled || s < 0 || size > MaxDatagram) {
        return 0;
    }
    std::vector<u64> live;
    {
        std::scoped_lock lock{peers_mutex};
        const s64 now = NowMs();
        for (auto it = peers.begin(); it != peers.end();) {
            if (now - it->second.seen_ms > PeerKeepMs) {
                it = peers.erase(it);
            } else {
                live.push_back(it->first);
                ++it;
            }
        }
    }
    u32 count = 0;
    for (const u64 key : live) {
        OrbisSockaddrIn to{};
        to.sin_len = sizeof(to);
        to.sin_family = OrbisAfInet;
        to.sin_addr = static_cast<u32>(key >> 16);
        to.sin_port = static_cast<u16>(key);
        to.sin_vport = static_cast<u16>((ChannelVport >> 8) | (ChannelVport << 8));
        if (original_sendto(s, data, size, OrbisMsgDontwait, &to, sizeof(to)) > 0) {
            ++count;
        }
    }
    sent_count += count;
    return count;
}

u32 P2PChannelPeerCount() {
    std::scoped_lock lock{peers_mutex};
    const s64 now = NowMs();
    u32 count = 0;
    for (const auto& [key, peer] : peers) {
        count += now - peer.seen_ms <= PeerKeepMs ? 1 : 0;
    }
    return count;
}

void StartP2PChannel() {
    if (const char* env = std::getenv("SHADPS4_BBSC_P2P_CHANNEL");
        env != nullptr && (env[0] == '0' || env[0] == 'f' || env[0] == 'F')) {
        LOG_INFO(Mod, "P2P channel: off (switch)");
        return;
    }
    if (!Swap<SendtoFn>(SlotSendto, &Sendto, original_sendto, "Sendto") ||
        !Swap<RecvfromFn>(SlotRecvfrom, &Recvfrom, original_recvfrom, "Recvfrom")) {
        LOG_ERROR(Mod, "P2P channel: an import slot was never filled - channel off");
        return;
    }
    Swap<ConnectionStatusFn>(SlotConnectionStatus, &ConnectionStatus, original_connection_status,
                             "ConnectionStatus");
    enabled = true;
    BbscMod::Platform::StartThread(ChannelThread);
    LOG_INFO(Mod, "P2P channel: ready (vport {}), waiting for the players of a session",
             ChannelVport);
}

} // namespace BbscMod
