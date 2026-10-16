// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

// BBSC mod: the new home of BBSC's own packets (host position / follow, world flags, Dream state).
// In the emulator build they rode on the emulator's P2P signaling. A mod cannot reach that, so
// they go through the BBSC game server instead, which every host already runs and every player's
// game already talks to: one TCP connection to its relay port (20444), frames of
// [u32 little-endian length][payload], and the server hands every frame to every other player
// connected to it. The payloads are the signaling packets unchanged ("SHAD" + type 0x20 follow,
// 0x22 world flags), so the receiving code is the same.
//
// No relay (public servers, or ours not reachable): the same frames go over the game's own P2P
// connection instead (bbsc_channel.cpp), and come back into Dispatch from there.
//
// Server address: BBSC_RELAY=host[:port], else the game server entry of host_overrides.json
// (portable install: <emulator>\user\, else %APPDATA%\shadPS4\; BBport: the file its run.sh /
// run.py names) - the address JOIN-HOST wrote.

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif
#include "bbsc_platform.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "common/types.h"
#include "core/bloodborne_re.h"
#include "core/libraries/np/np_signaling/np_signaling_bbsc.h"
#include "core/libraries/np/np_signaling/np_signaling_helpers.h"

namespace BbscMod {
void LogPlain(const char* text);
void OnHostPlacement(const std::string& placement, u32 sender);
bool IsPublicServerAddress(const std::string& url_or_host); // bbsc_http.cpp
u32 P2PChannelSend(const void* data, u32 size);             // bbsc_channel.cpp
u32 P2PChannelPeerCount();
}

namespace {

#ifdef _WIN32
constexpr int SendFlags = 0;
#else
using SOCKET = int;
constexpr SOCKET INVALID_SOCKET = -1;
int closesocket(SOCKET s) {
    return close(s);
}
// A closed link is an error here, not a SIGPIPE that ends the game.
constexpr int SendFlags = MSG_NOSIGNAL;
#endif

constexpr u16 DefaultRelayPort = 20444;
constexpr u8 FollowType = 0x20;
constexpr u8 PlacementType = 0x30; // "SHAD" 0x30 + a host's placement text (bbsc_http.cpp)
constexpr u8 LifeType = 0x23;   // "SHAD" 0x23 + u32 player, u8 dead, u32 sequence
constexpr u8 ScaleType = 0x24;  // "SHAD" 0x24 + 5 x u16 the host's scaling percentages
constexpr u8 PvpType = 0x25;    // "SHAD" 0x25 + u8 kind (0 host state, 1 guest asks) + u8 on + u32
constexpr u8 ProgressType = 0x26; // "SHAD" 0x26 + u8 count + count x u32 the host's boss flags
constexpr u8 HostBossType = 0x27; // "SHAD" 0x27 + s32 the boss on the host's boss bar (-1 none)
constexpr u8 PeersType = 0x7F; // server -> client: "N other players are connected", 1 byte count
// Every frame since 2026-10-06: "SHAD" 0x40 + u32 sender + u32 session (the host's sender id, 0 = in
// no session) + the packet above. The server sends each frame to everyone connected to it, so the
// receiver keeps only its own session's packets (see "Relay sessions" in bloodborne_re.cpp).
// Frames without it come from an older BBSC and are dropped.
constexpr u8 EnvelopeType = 0x40;
constexpr size_t EnvelopeSize = 13;

#pragma pack(push, 1)
struct FollowPacket {
    u8 magic[4] = {'S', 'H', 'A', 'D'};
    u8 type = FollowType;
    u8 kind = 0;
    u32 packed_map = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float heading = 0.0f;
    u32 sequence = 0;
    s32 lamp_point = -1;
};
#pragma pack(pop)
static_assert(sizeof(FollowPacket) == 0x22);

std::mutex send_mutex;
std::mutex dispatch_mutex; // the relay thread and the P2P channel's thread both deliver frames
SOCKET relay = INVALID_SOCKET;
std::atomic<int> peers{0};
std::atomic<bool> started{false};
u32 own_id = 0; // set once in StartRelay, never 0
std::atomic<bool> logged_old_sender{false};

void Log(const std::string& text) {
    BbscMod::LogPlain(("Relay: " + text).c_str());
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

// "http://26.1.2.3:20443" as the value of the ss4.scej-network.jp key -> "26.1.2.3".
std::string HostFromOverrides(const std::string& json) {
    const size_t key = json.find("ss4.scej-network.jp");
    if (key == std::string::npos) {
        return {};
    }
    const size_t colon = json.find(':', json.find('"', key + 1) + 1);
    const size_t open = json.find('"', colon);
    const size_t close = json.find('"', open + 1);
    if (colon == std::string::npos || open == std::string::npos || close == std::string::npos) {
        return {};
    }
    std::string url = json.substr(open + 1, close - open - 1);
    if (const size_t scheme = url.find("://"); scheme != std::string::npos) {
        url = url.substr(scheme + 3);
    }
    return url.substr(0, url.find_first_of(":/"));
}

bool FindServer(std::string& host, u16& port) {
    port = DefaultRelayPort;
    if (const char* env = std::getenv("BBSC_RELAY"); env != nullptr && env[0] != '\0') {
        std::string value = env;
        if (const size_t colon = value.find(':'); colon != std::string::npos) {
            port = static_cast<u16>(std::atoi(value.c_str() + colon + 1));
            value.resize(colon);
        }
        host = value;
        return true;
    }
    for (const auto& path : BbscMod::Platform::HostOverridesFiles()) {
        host = HostFromOverrides(ReadFile(path));
        if (!host.empty()) {
            return true;
        }
    }
    return false;
}

bool ReadAll(SOCKET s, void* data, int size) {
    auto* at = static_cast<char*>(data);
    while (size > 0) {
        const int got = static_cast<int>(recv(s, at, size, 0));
        if (got <= 0) {
            return false;
        }
        at += got;
        size -= got;
    }
    return true;
}

// Our session as the relay sees it: hosting -> ourselves, a guest (or joining) -> our host.
u32 SessionKey() {
    const s32 state = Core::Bloodborne::GetBbscRelaySessionState();
    if (state == 3) {
        return own_id;
    }
    if (state == 4 || state == 6) {
        return Core::Bloodborne::GetBbscRelayHost();
    }
    return 0;
}

// Returns how many players it went to: the relay's count, or (no relay connected - public servers)
// the players the P2P channel sent it to (bbsc_channel.cpp).
u32 SendFrame(const void* data, u32 size) {
    std::scoped_lock lock{send_mutex};
    const u32 key = SessionKey();
    const u32 wrapped = static_cast<u32>(EnvelopeSize) + size;
    std::vector<char> frame(4 + wrapped);
    std::memcpy(frame.data(), &wrapped, 4);
    std::memcpy(frame.data() + 4, "SHAD", 4);
    frame[8] = static_cast<char>(EnvelopeType);
    std::memcpy(frame.data() + 9, &own_id, 4);
    std::memcpy(frame.data() + 13, &key, 4);
    std::memcpy(frame.data() + 4 + EnvelopeSize, data, size);
    if (relay == INVALID_SOCKET) {
        return BbscMod::P2PChannelSend(frame.data() + 4, wrapped);
    }
    int left = static_cast<int>(frame.size());
    const char* at = frame.data();
    while (left > 0) {
        const int sent = static_cast<int>(send(relay, at, left, SendFlags));
        if (sent <= 0) {
            closesocket(relay);
            relay = INVALID_SOCKET;
            return BbscMod::P2PChannelSend(frame.data() + 4, wrapped);
        }
        at += sent;
        left -= sent;
    }
    return static_cast<u32>(peers.load());
}

void Dispatch(const std::vector<u8>& wrapped) {
    if (wrapped.size() == 2 && wrapped[0] == PeersType) {
        peers = wrapped[1];
        return;
    }
    if (wrapped.size() < 5 || std::memcmp(wrapped.data(), "SHAD", 4) != 0) {
        return;
    }
    if (wrapped[4] != EnvelopeType || wrapped.size() < EnvelopeSize + 5) {
        if (!logged_old_sender.exchange(true)) {
            Log("a player on this server runs an older BBSC (packets without a sender) - their "
                "packets are ignored until they update");
        }
        return;
    }
    u32 sender = 0;
    u32 session = 0;
    std::memcpy(&sender, wrapped.data() + 5, 4);
    std::memcpy(&session, wrapped.data() + 9, 4);
    const std::vector<u8> frame(wrapped.begin() + EnvelopeSize, wrapped.end());
    if (sender == own_id || std::memcmp(frame.data(), "SHAD", 4) != 0) {
        return;
    }
    const s32 state = Core::Bloodborne::GetBbscRelaySessionState();
    const bool guest = state == 4 || state == 6;
    const bool sender_hosting = session != 0 && session == sender;
    // Host-only packets are taken from our host alone.
    const auto from_our_host = [&] {
        return guest && sender_hosting && sender == Core::Bloodborne::GetBbscRelayHost();
    };
    if (frame[4] == FollowType && frame.size() >= 0x1E) {
        FollowPacket follow{};
        std::memcpy(&follow, frame.data(), std::min(frame.size(), sizeof(follow)));
        if (sender_hosting) {
            Core::Bloodborne::NoteBbscRelayHost(sender, follow.packed_map, follow.x, follow.y,
                                                follow.z);
        }
        if (from_our_host()) {
            Core::Bloodborne::OnBbscHostFollowPacket(
                follow.packed_map, follow.x, follow.y, follow.z, follow.heading, follow.sequence,
                frame.size() >= sizeof(follow) ? follow.lamp_point : -1);
        }
    } else if (frame[4] == LifeType && frame.size() >= 14) {
        // Only the players of our own session (same host).
        if (session == 0 || session != SessionKey()) {
            return;
        }
        u32 player = 0;
        u32 sequence = 0;
        std::memcpy(&player, frame.data() + 5, 4);
        std::memcpy(&sequence, frame.data() + 10, 4);
        Core::Bloodborne::OnBbscLifePacket(player, frame[9], sequence);
    } else if (frame[4] == ScaleType && frame.size() >= 15) {
        if (from_our_host()) {
            std::uint16_t percent[5]{};
            std::memcpy(percent, frame.data() + 5, sizeof(percent));
            Core::Bloodborne::OnBbscScalePacket(percent);
        }
    } else if (frame[4] == PvpType && frame.size() >= 11) {
        // Kind 0 is the host's state (for its guests), kind 1 a guest asking its host.
        const bool ours = frame[5] == 0 ? from_our_host() : state == 3 && session == own_id;
        if (ours) {
            Core::Bloodborne::OnBbscPvpPacket(frame[5], frame[6]);
        }
    } else if (frame[4] == ProgressType && frame.size() >= 6) {
        // The host's boss flags for shared progress: u8 count + count x u32.
        const size_t count = std::min<size_t>(frame[5], (frame.size() - 6) / 4);
        if (from_our_host() && count != 0) {
            std::vector<std::uint32_t> flags(count);
            std::memcpy(flags.data(), frame.data() + 6, count * 4);
            Core::Bloodborne::OnBbscProgressPacket(flags.data(), flags.size());
        }
    } else if (frame[4] == HostBossType && frame.size() >= 9) {
        if (from_our_host()) {
            std::int32_t boss = -1;
            std::memcpy(&boss, frame.data() + 5, 4);
            Core::Bloodborne::OnBbscHostBossPacket(boss);
        }
    } else if (frame[4] == PlacementType) {
        // A host looking for signs: for players not in a session yet, any host on the server.
        BbscMod::OnHostPlacement(std::string(frame.begin() + 5, frame.end()), sender);
    } else if (frame[4] == Libraries::Np::NpSignaling::kSignalingBbscWorldFlagsType) {
        if (from_our_host()) {
            Core::Bloodborne::OnBbscWorldFlagsPacket(frame.data(), static_cast<u32>(frame.size()));
        }
    }
}

void RelayThread() {
#ifdef _WIN32
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    std::string last_problem;
    for (;;) {
        std::string host;
        u16 port = 0;
        if (!FindServer(host, port)) {
            if (last_problem != "no server") {
                last_problem = "no server";
                Log("no game server address (host_overrides.json / BBSC_RELAY) - retrying");
            }
            BbscMod::Platform::SleepMs(10'000);
            continue;
        }
        if (BbscMod::IsPublicServerAddress(host)) {
            // The Hunter's Dream / NoDream run no BBSC relay: BBSC packets take the P2P channel.
            if (last_problem != "public") {
                last_problem = "public";
                Log("public game server (" + host + ") - no relay there; BBSC packets go over "
                    "the P2P channel");
            }
            BbscMod::Platform::SleepMs(30'000);
            continue;
        }
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* found = nullptr;
        SOCKET s = INVALID_SOCKET;
        if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found) == 0) {
            s = socket(found->ai_family, found->ai_socktype, found->ai_protocol);
            if (s != INVALID_SOCKET && connect(s, found->ai_addr, static_cast<int>(found->ai_addrlen)) != 0) {
                closesocket(s);
                s = INVALID_SOCKET;
            }
            freeaddrinfo(found);
        }
        if (s == INVALID_SOCKET) {
            const std::string problem = "cannot reach " + host + ":" + std::to_string(port);
            if (problem != last_problem) {
                last_problem = problem;
                Log(problem + " (game server not started, or an older one without the relay) - "
                              "retrying every 5 s");
            }
            BbscMod::Platform::SleepMs(5'000);
            continue;
        }
        const int on = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof(on));
        // Frames are sent from the game thread (review 2026-10-08): a stalled server must not
        // freeze the game in send() - after 300 ms the frame fails and the link is rebuilt.
#ifdef _WIN32
        const DWORD send_timeout = 300;
#else
        const timeval send_timeout{0, 300'000};
#endif
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&send_timeout),
                   sizeof(send_timeout));
        {
            std::scoped_lock lock{send_mutex};
            relay = s;
        }
        last_problem.clear();
        Log("connected to " + host + ":" + std::to_string(port));
        for (;;) {
            u32 size = 0;
            if (!ReadAll(s, &size, 4) || size > 64 * 1024) {
                break;
            }
            std::vector<u8> frame(size);
            if (size != 0 && !ReadAll(s, frame.data(), static_cast<int>(size))) {
                break;
            }
            std::scoped_lock dispatch_lock{dispatch_mutex};
            Dispatch(frame);
        }
        {
            std::scoped_lock lock{send_mutex};
            if (relay == s) {
                closesocket(relay);
                relay = INVALID_SOCKET;
            }
        }
        peers = 0;
        Log("connection lost - reconnecting");
        BbscMod::Platform::SleepMs(2'000);
    }
}

} // namespace

namespace BbscMod {
// Public game server (The Hunter's Dream...): it works like Sony's, so BBSC must summon the vanilla
// way (bbsc_mod.cpp Worker). No host_overrides.json found = not public (review 2026-10-11): a
// self-host install whose file BBSC does not look for must keep the seamless summons, and without
// the file the game reaches no server at all anyway.
bool GameServerIsPublic(std::string* host_out) {
    std::string host;
    u16 port = 0;
    if (!FindServer(host, port)) {
        if (host_out != nullptr) {
            *host_out = "none found";
        }
        return false;
    }
    if (host_out != nullptr) {
        *host_out = host;
    }
    return BbscMod::IsPublicServerAddress(host);
}

// Frames the P2P channel received (same format as the relay's, without the length).
void RelayDispatch(const std::vector<u8>& wrapped) {
    std::scoped_lock lock{dispatch_mutex};
    Dispatch(wrapped);
}

void RelaySendPlacement(const std::string& placement) {
    std::string frame = "SHAD";
    frame.push_back(static_cast<char>(PlacementType));
    frame += placement;
    SendFrame(frame.data(), static_cast<u32>(frame.size()));
}

void StartRelay() {
    if (!started.exchange(true)) {
        const auto counter = std::chrono::steady_clock::now().time_since_epoch().count();
        own_id = static_cast<u32>((static_cast<u64>(counter) ^ BbscMod::Platform::ProcessId()) *
                                  2654435761ull) |
                 1u;
        BbscMod::Platform::StartThread(RelayThread);
    }
}
} // namespace BbscMod

namespace Libraries::Np::NpSignaling::Helpers {

u32 SendBbscFollowToPeers(u32 packed_map, float x, float y, float z, float heading, u32 sequence,
                          s32 lamp_point) {
    FollowPacket packet{};
    packet.packed_map = packed_map;
    packet.x = x;
    packet.y = y;
    packet.z = z;
    packet.heading = heading;
    packet.sequence = sequence;
    packet.lamp_point = lamp_point;
    return SendFrame(&packet, sizeof(packet));
}

u32 SendBbscWorldFlagsToPeers(const void* packet, u32 size) {
    return SendFrame(packet, size);
}

} // namespace Libraries::Np::NpSignaling::Helpers
