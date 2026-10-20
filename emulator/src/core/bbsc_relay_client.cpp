// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

// BBSC relay client for the emulator build - the emulator side of mod\bbsc_relay.cpp (same server,
// same frames: [u32 length]["SHAD" 0x40 + u32 sender + u32 session + the packet]).
//
// Why (2026-10-19, logs test-logs\2026-10-19-boss-session): the host (emulator build) rang the Duel
// Bell; the first guest (emulator build) got it over the P2P signaling, the third player (BBport +
// the mod) never did - his log has no "received the host's Duel Bell state: on" - so on his screen
// the others stayed friends (his hits went through them) while they could hit him. CONFIRMED: the
// emulator build never connected to the relay (no "Relay" line in either emulator log) and the mod
// cannot hear the emulator's signaling packets, so his mod took a stranger as its host by position
// ("our host is player 1136bcaf (10 hosting on the server, it stands 14.5 m from the host...)") and
// used that stranger's scaling, world flags, boss progress and PvP state.
//
// Sending: every BBSC packet the emulator sends to its signaling peers goes to the relay as well
// (np_signaling_helpers.cpp). Receiving: frames from the relay go to the same handlers the mod
// uses, with two limits that keep two emulator builds exactly as before - a guest ignores the
// relay while BBSC packets arrive over the signaling (its host is an emulator build), and life
// packets ("SHAD" 0x23) stay unhandled as the emulator's signaling receive always did.

#include "core/bbsc_relay_client.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include "common/logging/log.h"
#include "common/path_util.h"
#include "core/bbsc_persistence.h"
#include "core/bloodborne_re.h"
#include "core/libraries/np/np_signaling/np_signaling_bbsc.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace Core::Bbsc {

#ifdef _WIN32
namespace {

constexpr std::uint16_t DefaultRelayPort = 20444;
constexpr std::uint8_t FollowType = 0x20;
constexpr std::uint8_t ScaleType = 0x24;
constexpr std::uint8_t PvpType = 0x25;
constexpr std::uint8_t ProgressType = 0x26;
constexpr std::uint8_t HostBossType = 0x27;
constexpr std::uint8_t AimedPlacementType = 0x31;
constexpr std::uint8_t PeersType = 0x7F;
constexpr std::uint8_t EnvelopeType = 0x40;
constexpr size_t EnvelopeSize = 13;
constexpr std::int64_t SignalingFreshMs = 5'000;
constexpr size_t SendQueueLimit = 256 * 1024;

#pragma pack(push, 1)
struct FollowPacket {
    std::uint8_t magic[4];
    std::uint8_t type;
    std::uint8_t kind;
    std::uint32_t packed_map;
    float x, y, z, heading;
    std::uint32_t sequence;
    std::int32_t lamp_point;
};
#pragma pack(pop)
static_assert(sizeof(FollowPacket) == 0x22);

std::atomic<bool> started{false};
std::uint32_t own_id = 0;
std::mutex send_mutex;
std::condition_variable send_cv;
std::vector<std::vector<char>> send_queue;
size_t send_queue_bytes = 0;
SOCKET relay = INVALID_SOCKET;
std::atomic<int> peers{0};
std::atomic<std::int64_t> signaling_ms{0};
std::atomic<std::int64_t> own_sign_user{0};
std::mutex dispatch_mutex;

std::int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void Log(const std::string& text) {
    LOG_INFO(Debug, "BBSC relay client: {}", text);
    Core::Bloodborne::BbscTimelineEvent("RELAY", text);
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

// The same list as the mod's bbsc_http.cpp IsPublicServer: those servers run no BBSC relay.
bool IsPublicServer(const std::string& host) {
    for (const char* known : {"thehuntersdream.com", "nodreamforthehunter.com", "shadps4.net",
                              "scej-network.jp"}) {
        const size_t len = std::strlen(known);
        if (host.size() >= len && host.compare(host.size() - len, len, known) == 0) {
            return true;
        }
    }
    return false;
}

// The game server the emulator's HTTP uses (http.cpp LoadHostOverrideState reads the same file).
bool FindServer(std::string& host, std::uint16_t& port) {
    port = DefaultRelayPort;
    if (const char* env = std::getenv("BBSC_RELAY"); env != nullptr && env[0] != '\0') {
        std::string value = env;
        if (const size_t colon = value.find(':'); colon != std::string::npos) {
            port = static_cast<std::uint16_t>(std::atoi(value.c_str() + colon + 1));
            value.resize(colon);
        }
        host = value;
        return true;
    }
    std::filesystem::path path;
    if (const char* env = std::getenv("SHADPS4_HTTP_HOST_OVERRIDES_JSON"); env && env[0]) {
        path = env;
    } else {
        path = Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "host_overrides.json";
    }
    host = HostFromOverrides(ReadFile(path));
    return !host.empty();
}

std::uint32_t SessionKey() {
    const std::int32_t state = Core::Bloodborne::GetBbscRelaySessionState();
    if (state == 3) {
        return own_id;
    }
    if (state == 4 || state == 6) {
        return Core::Bloodborne::GetBbscRelayHost();
    }
    return 0;
}

std::uint32_t SendFrame(const void* data, std::uint32_t size) {
    std::scoped_lock lock{send_mutex};
    if (relay == INVALID_SOCKET) {
        return 0;
    }
    const std::uint32_t key = SessionKey();
    const std::uint32_t wrapped = static_cast<std::uint32_t>(EnvelopeSize) + size;
    std::vector<char> frame(4 + wrapped);
    std::memcpy(frame.data(), &wrapped, 4);
    std::memcpy(frame.data() + 4, "SHAD", 4);
    frame[8] = static_cast<char>(EnvelopeType);
    std::memcpy(frame.data() + 9, &own_id, 4);
    std::memcpy(frame.data() + 13, &key, 4);
    std::memcpy(frame.data() + 4 + EnvelopeSize, data, size);
    if (send_queue_bytes + frame.size() > SendQueueLimit) {
        return static_cast<std::uint32_t>(peers.load()); // the server is stuck: drop, keep the link
    }
    send_queue_bytes += frame.size();
    send_queue.push_back(std::move(frame));
    send_cv.notify_all();
    return static_cast<std::uint32_t>(peers.load());
}

void WriterThread() {
    for (;;) {
        std::vector<char> frame;
        SOCKET s = INVALID_SOCKET;
        {
            std::unique_lock lock{send_mutex};
            send_cv.wait(lock, [] { return !send_queue.empty(); });
            frame = std::move(send_queue.front());
            send_queue.erase(send_queue.begin());
            send_queue_bytes -= frame.size();
            s = relay;
        }
        if (s == INVALID_SOCKET) {
            continue;
        }
        int left = static_cast<int>(frame.size());
        const char* at = frame.data();
        while (left > 0) {
            const int sent = send(s, at, left, 0);
            if (sent <= 0) {
                shutdown(s, SD_BOTH); // the relay thread sees it in recv and reconnects
                break;
            }
            at += sent;
            left -= sent;
        }
    }
}

bool ReadAll(SOCKET s, void* data, int size) {
    auto* at = static_cast<char*>(data);
    while (size > 0) {
        const int got = recv(s, at, size, 0);
        if (got <= 0) {
            return false;
        }
        at += got;
        size -= got;
    }
    return true;
}

void Dispatch(const std::vector<std::uint8_t>& wrapped) {
    if (wrapped.size() == 2 && wrapped[0] == PeersType) {
        peers = wrapped[1];
        return;
    }
    if (wrapped.size() < EnvelopeSize + 5 || std::memcmp(wrapped.data(), "SHAD", 4) != 0 ||
        wrapped[4] != EnvelopeType) {
        return; // an older BBSC without the envelope
    }
    std::uint32_t sender = 0;
    std::uint32_t session = 0;
    std::memcpy(&sender, wrapped.data() + 5, 4);
    std::memcpy(&session, wrapped.data() + 9, 4);
    const std::vector<std::uint8_t> frame(wrapped.begin() + EnvelopeSize, wrapped.end());
    if (sender == own_id || std::memcmp(frame.data(), "SHAD", 4) != 0) {
        return;
    }
    const std::int32_t state = Core::Bloodborne::GetBbscRelaySessionState();
    const bool guest = state == 4 || state == 6;
    const bool sender_hosting = session != 0 && session == sender;
    const std::uint8_t type = frame[4];
    if (type == FollowType && frame.size() >= 0x1E && sender_hosting) {
        FollowPacket follow{};
        std::memcpy(&follow, frame.data(), std::min(frame.size(), sizeof(follow)));
        // Every host's position: the mod's host lock (bloodborne_re.cpp) picks ours from them.
        Core::Bloodborne::NoteBbscRelayHost(sender, follow.packed_map, follow.x, follow.y,
                                            follow.z);
    }
    if (type == AimedPlacementType) {
        // A host rang a sign: ours -> that sender is the host we are joining.
        const std::string text(frame.begin() + 5, frame.end());
        const size_t bar = text.find('|');
        const std::int64_t own = own_sign_user.load();
        if (bar != std::string::npos && own != 0 &&
            std::atoll(text.substr(0, bar).c_str()) == own && state != 3 && !guest) {
            Core::Bloodborne::NoteBbscHostRangOurSign(sender);
            Log(fmt::format("host {:08x} rang our sign (user {})", sender, own));
        }
        return;
    }
    if (type == PvpType && frame.size() >= 11 && frame[5] == 1) {
        // A guest asks its host (us) for PvP on/off.
        if (state == 3 && session == own_id) {
            Core::Bloodborne::OnBbscPvpPacket(frame[5], frame[6]);
        }
        return;
    }
    // Everything below comes from a host for its guests.
    if (!guest || !sender_hosting || sender != Core::Bloodborne::GetBbscRelayHost()) {
        return;
    }
    if (NowMs() - signaling_ms.load() < SignalingFreshMs) {
        return; // our host talks over the signaling (an emulator build): as before
    }
    if (type == FollowType && frame.size() >= 0x1E) {
        FollowPacket follow{};
        std::memcpy(&follow, frame.data(), std::min(frame.size(), sizeof(follow)));
        Core::Bloodborne::OnBbscHostFollowPacket(follow.packed_map, follow.x, follow.y, follow.z,
                                                 follow.heading, follow.sequence,
                                                 frame.size() >= sizeof(follow) ? follow.lamp_point
                                                                                : -1);
    } else if (type == ScaleType && frame.size() >= 15) {
        std::uint16_t percent[5]{};
        std::memcpy(percent, frame.data() + 5, sizeof(percent));
        Core::Bloodborne::OnBbscScalePacket(percent);
    } else if (type == PvpType && frame.size() >= 11) {
        Core::Bloodborne::OnBbscPvpPacket(frame[5], frame[6]);
    } else if (type == ProgressType && frame.size() >= 6) {
        const size_t count = std::min<size_t>(frame[5], (frame.size() - 6) / 4);
        if (count != 0) {
            std::vector<std::uint32_t> flags(count);
            std::memcpy(flags.data(), frame.data() + 6, count * 4);
            Core::Bloodborne::OnBbscProgressPacket(flags.data(), flags.size());
        }
    } else if (type == HostBossType && frame.size() >= 9) {
        std::int32_t boss = -1;
        std::memcpy(&boss, frame.data() + 5, 4);
        Core::Bloodborne::OnBbscHostBossPacket(boss);
    } else if (type == Libraries::Np::NpSignaling::kSignalingBbscWorldFlagsType) {
        Core::Bloodborne::OnBbscWorldFlagsPacket(frame.data(),
                                                 static_cast<std::uint32_t>(frame.size()));
    }
}

void RelayThread() {
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
    std::string last_problem;
    for (;;) {
        std::string host;
        std::uint16_t port = 0;
        if (!FindServer(host, port)) {
            if (last_problem != "no server") {
                last_problem = "no server";
                Log("no game server address (host_overrides.json / BBSC_RELAY) - retrying");
            }
            std::this_thread::sleep_for(std::chrono::seconds(10));
            continue;
        }
        if (IsPublicServer(host)) {
            if (last_problem != "public") {
                last_problem = "public";
                Log("public game server (" + host + ") - no BBSC relay there");
            }
            std::this_thread::sleep_for(std::chrono::seconds(30));
            continue;
        }
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* found = nullptr;
        SOCKET s = INVALID_SOCKET;
        if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found) == 0) {
            s = socket(found->ai_family, found->ai_socktype, found->ai_protocol);
            if (s != INVALID_SOCKET &&
                connect(s, found->ai_addr, static_cast<int>(found->ai_addrlen)) != 0) {
                closesocket(s);
                s = INVALID_SOCKET;
            }
            freeaddrinfo(found);
        }
        if (s == INVALID_SOCKET) {
            const std::string problem = "cannot reach " + host + ":" + std::to_string(port);
            if (problem != last_problem) {
                last_problem = problem;
                Log(problem + " (game server not started, or one without the relay) - retrying "
                              "every 5 s");
            }
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }
        BOOL on = TRUE;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof(on));
        setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&on), sizeof(on));
        const DWORD send_timeout_ms = 10'000;
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&send_timeout_ms),
                   sizeof(send_timeout_ms));
        {
            std::scoped_lock lock{send_mutex};
            relay = s;
        }
        last_problem.clear();
        Log("connected to " + host + ":" + std::to_string(port));
        const std::int64_t connected_at = NowMs();
        std::uint64_t frames_in = 0;
        for (;;) {
            std::uint32_t size = 0;
            if (!ReadAll(s, &size, 4) || size > 64 * 1024) {
                break;
            }
            std::vector<std::uint8_t> frame(size);
            if (size != 0 && !ReadAll(s, frame.data(), static_cast<int>(size))) {
                break;
            }
            ++frames_in;
            std::scoped_lock dispatch_lock{dispatch_mutex};
            Dispatch(frame);
        }
        {
            std::scoped_lock lock{send_mutex};
            relay = INVALID_SOCKET;
            send_queue.clear();
            send_queue_bytes = 0;
        }
        shutdown(s, SD_BOTH);
        closesocket(s);
        peers = 0;
        Log("connection lost after " + std::to_string((NowMs() - connected_at) / 1000) + " s, " +
            std::to_string(frames_in) + " frames in - reconnecting");
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

} // namespace

void StartRelayClient(std::string_view serial) {
    if (serial != "CUSA03173") {
        return; // BBSC only knows the EU 1.09 game
    }
    // The emulator's own BBSC switched off (the mod runs in it instead: bbsc-launch sets these to
    // 0) - the mod has its own relay link.
    if (!Core::Bbsc::SwitchEnabled("SHADPS4_BLOODBORNE_RE_TRACE") ||
        !Core::Bbsc::SwitchEnabled("SHADPS4_BBSC_PERSIST")) {
        return;
    }
    if (const char* env = std::getenv("SHADPS4_BBSC_RELAY"); env != nullptr && env[0] == '0') {
        Log("off (SHADPS4_BBSC_RELAY=0)");
        return;
    }
    if (started.exchange(true)) {
        return;
    }
    const auto seed = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    own_id = static_cast<std::uint32_t>((seed ^ static_cast<std::uint64_t>(GetCurrentProcessId())) *
                                        2654435761ull) |
             1u;
    std::thread(RelayThread).detach();
    std::thread(WriterThread).detach();
}

std::uint32_t RelaySendPacket(const void* packet, std::uint32_t size) {
    if (!started.load() || packet == nullptr || size < 5) {
        return 0;
    }
    return SendFrame(packet, size);
}

void NoteSignalingBbscPacket() {
    signaling_ms = NowMs();
}

void NoteOwnSignUserId(std::int64_t user_id) {
    if (user_id > 0 && own_sign_user.exchange(user_id) != user_id) {
        LOG_INFO(Debug, "BBSC relay client: our sign's user id is {}", user_id);
    }
}

void RelaySendAimedPlacement(std::int64_t target_user, const std::string& placement) {
    std::string frame = "SHAD";
    frame.push_back(static_cast<char>(AimedPlacementType));
    frame += std::to_string(target_user) + "|" + placement;
    if (!started.load()) {
        return;
    }
    const std::uint32_t reached = RelaySendPacket(frame.data(), static_cast<std::uint32_t>(frame.size()));
    Log(fmt::format("ringing the sign of user {} - told it over the relay that we are its host "
                    "({} other player(s) on the relay)",
                    target_user, reached));
}

#else // not Windows: the relay client is Windows-only for now

void StartRelayClient(std::string_view) {}
std::uint32_t RelaySendPacket(const void*, std::uint32_t) {
    return 0;
}
void NoteSignalingBbscPacket() {}
void NoteOwnSignUserId(std::int64_t) {}
void RelaySendAimedPlacement(std::int64_t, const std::string&) {}

#endif

} // namespace Core::Bbsc
