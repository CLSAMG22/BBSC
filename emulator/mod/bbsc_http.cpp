// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

// BBSC mod: the game's HTTP calls. In the emulator build, core/libraries/network/http.cpp
// changes Bloodborne's summon traffic on its way out (Wozzardman's seamless route plus BBSC's
// connect-first and stale-record cleanup). A mod cannot reach that code, so the DLL does it one
// step earlier: it swaps the eboot's import slots (GOT entries the emulator filled at load) of
// the libSceHttp functions the game calls for its own wrappers, which change the request and pass
// it on to the emulator's function.
//
// Import slots, CUSA03173 01.09 (tools/gamedata/eboot_imports.py, BBSC offsets):
//   0x053E5618 sceHttpCreateConnectionWithURL   0x053E5620 sceHttpCreateRequestWithURL
//   0x053E55E0 sceHttpAddRequestHeader          0x053E5628 sceHttpSetRequestContentLength
//   0x053E5670 sceHttpSendRequest               0x053E5648 sceHttpDeleteRequest
//   0x053E5668 sceHttpReadData
//
// What the wrappers do:
//   - URLs: the entries of host_overrides.json (same file and format as the emulator's) are
//     applied here too, so a stock emulator reaches the BBSC game server and summon broker.
//   - /summon_messenger/*: header X-ShadPS4-Bloodborne-Seamless: 1 (the broker's seamless mode).
//   - summon search/request (a host looking for signs): the host's placement header, as the
//     emulator sent it, AND the same text to every other player through the BBSC relay - the game
//     does not import sceHttpGetAllResponseHeaders, so the guest cannot read it from the broker's
//     reply; the relay brings it instead (bbsc_relay.cpp -> BbscMod::OnHostPlacement).
//   - summon create (a guest's sign): the AreaId becomes the host's map once a host's placement
//     is known (connect-first), and the first seamless create of this run sends a forced delete
//     for the same SessionId/UserId first (stale broker record after a crash).
//   - a claim the broker delivered with a sign that never became a join, and a session that
//     ended, re-arm that delete: the next sign starts clean (see "Stuck claims" below).
// Not done: the emulator's 3 s "fast sign" re-posts. They replaced the game's own reply; extra
// posts from here would take the broker's claim themselves and the game would never join.

#include "bbsc_platform.h"
#ifdef _WIN32
#include <winhttp.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include <array>
#include <atomic>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <thread>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/logging/log.h"
#include "common/path_util.h"
#include "common/types.h"
#include "core/bloodborne_re.h"

namespace BbscMod {
void RelaySendPlacement(const std::string& placement);
u64 EbootAddress(); // bbsc_mod.cpp: where the game image is (0 until found)
void GuardSlot(u64 slot_offset, u64 wrapper, void** original, const char* name); // bbsc_mod.cpp
}

namespace {

constexpr u64 EbootSize = 0x056D4000ULL;
constexpr u64 SlotCreateConnection = 0x053E5618;
constexpr u64 SlotCreateRequest = 0x053E5620;
constexpr u64 SlotAddHeader = 0x053E55E0;
constexpr u64 SlotSetContentLength = 0x053E5628;
constexpr u64 SlotSend = 0x053E5670;
constexpr u64 SlotDelete = 0x053E5648;
constexpr u64 SlotReadData = 0x053E5668;
// A claim delivered with our sign is followed by a join (session state 4) within seconds when it
// works; one that has not after this long is a dead claim.
constexpr s64 ClaimJoinWaitMs = 20'000;
constexpr s64 PlacementStaleMs = 20'000;

using CreateConnectionFn = int PS4_SYSV_ABI (*)(int, const char*, bool);
using CreateRequestFn = int PS4_SYSV_ABI (*)(int, s32, const char*, u64);
using AddHeaderFn = int PS4_SYSV_ABI (*)(int, const char*, const char*, s32);
using SetContentLengthFn = int PS4_SYSV_ABI (*)(int, u64);
using SendFn = int PS4_SYSV_ABI (*)(int, const void*, u64);
using DeleteFn = int PS4_SYSV_ABI (*)(int);
using ReadDataFn = int PS4_SYSV_ABI (*)(int, void*, u64);

CreateConnectionFn original_create_connection = nullptr;
CreateRequestFn original_create_request = nullptr;
AddHeaderFn original_add_header = nullptr;
SetContentLengthFn original_set_content_length = nullptr;
SendFn original_send = nullptr;
DeleteFn original_delete = nullptr;
ReadDataFn original_read_data = nullptr;

std::mutex state_mutex;
std::map<int, std::string> request_urls;            // reqId -> URL the request really goes to
std::vector<std::pair<std::string, std::string>> overrides; // URL prefix -> server
std::atomic<bool> stale_cleared{false};
std::atomic<s64> placement_received_ms{0};
// 5-player test 2026-10-12: every player polls /summon_messenger/get, waiting guests too, so every
// guest sent its own placement as "a host looking for signs". The other waiting guests switched
// their sign's map between the real host (Dream) and a guest in Central Yharnam, and the broker
// hid every sign outside the searcher's map (Preparing) - nobody past the 3rd was summoned.
// Only a player without a sign of its own (or a host, state 3) sends the placement now, and a
// waiting guest follows one host until that host goes quiet.
std::atomic<s64> own_sign_created_ms{0};
std::atomic<s64> own_sign_deleted_ms{0};
constexpr s64 OwnSignMs = 60'000; // the game posts its sign again about every 30 s
std::atomic<u32> placement_sender{0};
std::atomic<s64> claim_delivered_ms{0}; // our sign's reply carried a host's claim (0 = none open)
std::atomic<bool> was_in_session{false};

s64 NowMs() {
    return static_cast<s64>(BbscMod::Platform::NowMs());
}

// ---- host_overrides.json ------------------------------------------------------------------------

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

// {"prefix": "replacement", ...} - flat string pairs, as JOIN-HOST writes them.
void LoadOverrides() {
    std::string exe_name =
        Common::FS::PathToUTF8String(BbscMod::Platform::ProgramPath().filename());
    for (char& c : exe_name) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    // bbhost sends the game's URLs to its own server ([online] host) itself. shadPS4's
    // host_overrides.json on the same PC pointed them at another server (2026-10-12: game API on
    // the shadPS4 server, NP sessions on bbhost's - nothing matched). BBSC_URL_OVERRIDES=0 does the
    // same for any host.
    const char* switch_env = std::getenv("BBSC_URL_OVERRIDES");
    if (exe_name.rfind("bbhost", 0) == 0 || (switch_env && switch_env[0] == '0')) {
        LOG_INFO(Mod, "HTTP: URL overrides off ({}) - the host sends the game's URLs itself",
                 switch_env && switch_env[0] == '0' ? "BBSC_URL_OVERRIDES=0" : "bbhost");
        return;
    }
    for (const auto& path : BbscMod::Platform::HostOverridesFiles()) {
        const std::string json = ReadFile(path);
        std::vector<std::string> strings;
        for (size_t at = json.find('"'); at != std::string::npos; at = json.find('"', at)) {
            const size_t end = json.find('"', at + 1);
            if (end == std::string::npos) {
                break;
            }
            strings.push_back(json.substr(at + 1, end - at - 1));
            at = end + 1;
        }
        for (size_t i = 0; i + 1 < strings.size(); i += 2) {
            overrides.emplace_back(strings[i], strings[i + 1]);
        }
        if (!overrides.empty()) {
            LOG_INFO(Mod, "HTTP: {} URL override(s) from host_overrides.json", overrides.size());
            return;
        }
    }
    LOG_INFO(Mod, "HTTP: no host_overrides.json - URLs unchanged");
}

// 0.0.0.0 is a server's "listen on everything", never an address to connect to. A game server
// started with it as its shadnet address (seen 2026-10-01 under Nucleus Co-op with
// NoDreamForHunter's server) puts it into ss.info, and every summon sign call then fails with
// "Could not establish connection". It can only have meant this PC.
std::string FixAnyAddress(std::string text) {
    for (const char* scheme : {"http://0.0.0.0", "https://0.0.0.0"}) {
        const std::string prefix = scheme;
        if (text.rfind(prefix, 0) == 0 &&
            (text.size() == prefix.size() || text[prefix.size()] == ':' ||
             text[prefix.size()] == '/')) {
            static bool logged = false;
            if (!logged) {
                logged = true;
                LOG_WARNING(Mod, "HTTP: the server list sends the game to 0.0.0.0 ({}) - using "
                                 "127.0.0.1 instead. Start the game server with a real shadnet "
                                 "address.",
                            text);
            }
            return text.replace(prefix.size() - 7, 7, "127.0.0.1");
        }
    }
    return text;
}

// An entry moves the URL to another server the way the emulator's http.cpp (ApplyHostOverride)
// reads it: the value gives the scheme, host and port, the URL keeps its path, and a value without
// a port keeps the URL's port (https 443 <-> http 80 aside). 2026-10-16, BBport for Linux: its
// {"https://ss4.scej-network.jp:20443": "http://play.seamlesshunt.com"} went to port 80 as plain
// text replacement, and "<server>:18671/summon_messenger" lost "/summon_messenger" - the game could
// not reach its server.
std::string Override(const char* url) {
    std::string text = FixAnyAddress(url != nullptr ? url : "");
    for (const auto& [prefix, replacement] : overrides) {
        if (text.rfind(prefix, 0) != 0) {
            continue;
        }
        const size_t scheme_end = text.find("://");
        if (scheme_end == std::string::npos) {
            return replacement + text.substr(prefix.size());
        }
        const std::string scheme = text.substr(0, scheme_end);
        const size_t path_at = std::min(text.find_first_of("/?#", scheme_end + 3), text.size());
        const std::string authority = text.substr(scheme_end + 3, path_at - scheme_end - 3);
        const size_t colon = authority.rfind(':');
        std::string port = colon != std::string::npos ? authority.substr(colon + 1)
                           : scheme == "https"         ? "443"
                                                       : "80";
        std::string new_scheme = scheme;
        std::string target = replacement;
        if (const size_t at = target.find("://"); at != std::string::npos) {
            new_scheme = target.substr(0, at);
            target = target.substr(at + 3);
        }
        target = target.substr(0, target.find('/'));
        if (target.find(':') == std::string::npos) {
            if (new_scheme != scheme &&
                ((scheme == "https" && port == "443") || (scheme == "http" && port == "80"))) {
                port = new_scheme == "https" ? "443" : "80";
            }
            target += ":" + port;
        }
        return new_scheme + "://" + target + text.substr(path_at);
    }
    return text;
}

// ---- Summon paths -------------------------------------------------------------------------------

bool Contains(const std::string& url, const char* part) {
    return url.find(part) != std::string::npos;
}
bool IsSummon(const std::string& url) {
    return Contains(url, "/summon_messenger/");
}
bool IsSummonCreate(const std::string& url) {
    return Contains(url, "/summon_messenger/create");
}
bool IsSummonDelete(const std::string& url) {
    return Contains(url, "/summon_messenger/delete");
}

bool IsSummonSearch(const std::string& url) {
    return Contains(url, "/summon_messenger/request") || Contains(url, "/summon_messenger/get");
}

// Public servers (2026-10-09): The Hunter's Dream (and the other community servers) let mods in as
// long as the game's API calls are not changed. On them BBSC adds no headers, rewrites no sign and
// sends no deletes - the seamless broker extras exist only on a BBSC host's shadnet-p2p server.
bool IsPublicServer(const std::string& url) {
    static constexpr std::array<const char*, 4> Hosts{"thehuntersdream.com", "nodreamforthehunter.com",
                                                      "shadps4.net", "scej-network.jp"};
    const size_t scheme = url.find("://");
    const size_t start = scheme == std::string::npos ? 0 : scheme + 3;
    const std::string host = url.substr(start, url.find_first_of(":/", start) - start);
    for (const char* known : Hosts) {
        const size_t len = std::strlen(known);
        if (host.size() >= len && host.compare(host.size() - len, len, known) == 0) {
            return true;
        }
    }
    return false;
}

// "AreaId":<digits> -> the host's map, when connect-first has one. Returns true if changed.
bool RewriteAreaId(std::string& body) {
    const auto host_map = Core::Bloodborne::GetBbscConnectFirstAreaId();
    if (!host_map) {
        return false;
    }
    const std::string key = "\"AreaId\":";
    const size_t at = body.find(key);
    if (at == std::string::npos) {
        return false;
    }
    const size_t begin = at + key.size();
    size_t end = begin;
    while (end < body.size() && body[end] >= '0' && body[end] <= '9') {
        ++end;
    }
    const std::string original = body.substr(begin, end - begin);
    const std::string replacement = std::to_string(*host_map);
    if (end == begin || original == replacement) {
        return false;
    }
    body.replace(begin, end - begin, replacement);
    LOG_INFO(Mod, "HTTP: connect-first - sign AreaId {} -> {}", original, replacement);
    return true;
}

std::string JsonString(const std::string& body, const char* key) {
    const std::string needle = std::string("\"") + key + "\":\"";
    const size_t at = body.find(needle);
    if (at == std::string::npos) {
        return {};
    }
    const size_t begin = at + needle.size();
    const size_t end = body.find('"', begin);
    return end == std::string::npos ? std::string{} : body.substr(begin, end - begin);
}

std::string JsonNumber(const std::string& body, const char* key) {
    const std::string needle = std::string("\"") + key + "\":";
    const size_t at = body.find(needle);
    if (at == std::string::npos) {
        return {};
    }
    size_t end = at + needle.size();
    while (end < body.size() && (std::isdigit(static_cast<unsigned char>(body[end])) || body[end] == '-')) {
        ++end;
    }
    return body.substr(at + needle.size(), end - at - needle.size());
}

// One plain POST with WinHTTP (the stale-record delete). Returns the status code, 0 on failure.
#ifdef _WIN32
u32 Post(const std::string& url, const std::string& body) {
    std::wstring wide(url.begin(), url.end());
    URL_COMPONENTS parts{sizeof(parts)};
    wchar_t host[256]{};
    wchar_t path[2048]{};
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts)) {
        return 0;
    }
    DWORD status = 0;
    HINTERNET session = WinHttpOpen(L"BBSC", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
    HINTERNET connect = session ? WinHttpConnect(session, host, parts.nPort, 0) : nullptr;
    HINTERNET request =
        connect ? WinHttpOpenRequest(connect, L"POST", path, nullptr, nullptr, nullptr,
                                     parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                : nullptr;
    if (request != nullptr) {
        WinHttpSetTimeouts(request, 1000, 1000, 1000, 1500);
        const wchar_t* headers =
            L"Content-Type: application/json\r\nX-ShadPS4-Bloodborne-Seamless: 1\r\n";
        if (WinHttpSendRequest(request, headers, static_cast<DWORD>(-1),
                               const_cast<char*>(body.data()), static_cast<DWORD>(body.size()),
                               static_cast<DWORD>(body.size()), 0) &&
            WinHttpReceiveResponse(request, nullptr)) {
            DWORD size = sizeof(status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                nullptr, &status, &size, nullptr);
        }
    }
    for (HINTERNET h : {request, connect, session}) {
        if (h != nullptr) {
            WinHttpCloseHandle(h);
        }
    }
    return status;
}
#else
// Linux: the same POST over a plain socket. The delete only goes to a BBSC host's summon broker
// (shadnet-p2p's WebAPI, plain http); public servers (https) never get it (IsPublicServer).
u32 Post(const std::string& url, const std::string& body) {
    constexpr std::string_view Scheme = "http://";
    if (url.rfind(Scheme, 0) != 0) {
        return 0;
    }
    const size_t path_at = url.find('/', Scheme.size());
    const std::string authority = url.substr(Scheme.size(), path_at - Scheme.size());
    const std::string path = path_at == std::string::npos ? "/" : url.substr(path_at);
    const size_t colon = authority.rfind(':');
    const std::string host = authority.substr(0, colon);
    const std::string port = colon == std::string::npos ? "80" : authority.substr(colon + 1);
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &found) != 0) {
        return 0;
    }
    int s = -1;
    for (const addrinfo* at = found; at != nullptr && s < 0; at = at->ai_next) {
        s = socket(at->ai_family, at->ai_socktype | SOCK_CLOEXEC, at->ai_protocol);
        if (s < 0) {
            continue;
        }
        // WinHTTP's timeouts above: about a second each way, on the game's thread.
        const timeval send_timeout{1, 0};
        const timeval receive_timeout{1, 500'000};
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout));
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receive_timeout, sizeof(receive_timeout));
        if (connect(s, at->ai_addr, at->ai_addrlen) != 0) {
            close(s);
            s = -1;
        }
    }
    freeaddrinfo(found);
    if (s < 0) {
        return 0;
    }
    const std::string request = "POST " + path + " HTTP/1.1\r\nHost: " + authority +
                                "\r\nContent-Type: application/json\r\n"
                                "X-ShadPS4-Bloodborne-Seamless: 1\r\nContent-Length: " +
                                std::to_string(body.size()) +
                                "\r\nConnection: close\r\n\r\n" + body;
    size_t sent = 0;
    while (sent < request.size()) {
        const ssize_t n = send(s, request.data() + sent, request.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            break;
        }
        sent += static_cast<size_t>(n);
    }
    u32 status = 0;
    if (sent == request.size()) {
        std::string reply;
        char chunk[256];
        while (reply.find("\r\n") == std::string::npos && reply.size() < 1024) {
            const ssize_t n = recv(s, chunk, sizeof(chunk), 0);
            if (n <= 0) {
                break;
            }
            reply.append(chunk, static_cast<size_t>(n));
        }
        // "HTTP/1.1 200 OK"
        const size_t space = reply.find(' ');
        if (reply.rfind("HTTP/", 0) == 0 && space != std::string::npos) {
            status = static_cast<u32>(std::strtoul(reply.c_str() + space + 1, nullptr, 10));
        }
    }
    close(s);
    return status;
}
#endif

// The broker keeps Preparing/Claimed/Delivered records 15 minutes and Bloodborne reuses its
// SessionId/UserId after a restart, so the first seamless sign of this run removes any old one.
void ClearStaleRecord(const std::string& url, const std::string& body) {
    if (stale_cleared.exchange(true)) {
        return;
    }
    const std::string session = JsonString(body, "SessionId");
    const std::string user = JsonNumber(body, "UserId");
    if (session.empty() || user.empty()) {
        return;
    }
    std::string removal_url = url;
    removal_url.replace(removal_url.find("/summon_messenger/create"),
                        std::strlen("/summon_messenger/create"), "/summon_messenger/delete");
    const std::string removal = "{\"MessageId\":\"SummonDataRemoveRequest\",\"SessionId\":\"" +
                                session + "\",\"UserId\":" + user + ",\"Force\":true}";
    // It must finish before the new sign is created (the delete names the same session), so it
    // stays on this thread - the game's - with short timeouts (2026-10-08: up to 14 s before).
    const u32 status = Post(removal_url, removal);
    LOG_INFO(Mod, "HTTP: stale summon cleanup for session {} user {}: status {}", session, user,
             status);
}

// ---- Stuck claims (2026-10-06) -------------------------------------------------------------------
// 4-player Nucleus test: one guest never joined and another waited minutes. In seamless mode the
// broker keeps a claimed sign 15 minutes (Preparing/Claimed/Delivered) and, every time that guest
// posts a sign again, hands back the OLD claim - from a host room that is gone or a join the game
// already gave up on. The host's searches list the same record, and its new claims are refused as
// conflicts. Nothing frees it but the 15-minute expiry or a forced delete, and the forced delete
// only ran once per game start. Now it runs again (on the next sign) when:
//   - the broker delivered a claim and no join followed within ClaimJoinWaitMs, or
//   - we were in a session (joining, guest or host) and are back to posting signs.
s32 SessionState() {
    const u64 manager = *reinterpret_cast<const u64*>(BbscMod::EbootAddress() + 0x05540290);
    BbscMod::Platform::Region info;
    if (manager >= 0x10000 && BbscMod::Platform::QueryRegion(manager + 0x124, info) &&
        info.committed && (info.prot & BbscMod::Platform::ProtRead) != 0) {
        return *reinterpret_cast<const s32*>(manager + 0x124);
    }
    return -1;
}

void WatchSessionForStaleClaims() {
    const s32 state = SessionState();
    if (state == 3 || state == 4 || state == 6) {
        was_in_session = true;
        if (claim_delivered_ms.exchange(0) != 0) {
            LOG_INFO(Mod, "HTTP: the delivered claim became a join (session state {})", state);
        }
        return;
    }
    if (state != 0) {
        return;
    }
    if (was_in_session.exchange(false)) {
        stale_cleared = false;
        LOG_INFO(Mod, "HTTP: session over - the next sign first clears our old broker record");
    }
    const s64 delivered = claim_delivered_ms.load();
    if (delivered != 0 && NowMs() - delivered > ClaimJoinWaitMs) {
        claim_delivered_ms = 0;
        stale_cleared = false;
        LOG_WARNING(Mod, "HTTP: a host's claim was delivered {} s ago and never became a join - "
                         "the next sign first clears it",
                    (NowMs() - delivered) / 1000);
    }
}

// ---- The wrappers -------------------------------------------------------------------------------

int PS4_SYSV_ABI CreateConnectionWithURL(int template_id, const char* url, bool keepalive) {
    const std::string target = Override(url);
    return original_create_connection(template_id, target.c_str(), keepalive);
}

int PS4_SYSV_ABI CreateRequestWithURL(int connection, s32 method, const char* url, u64 length) {
    const std::string target = Override(url);
    const int request = original_create_request(connection, method, target.c_str(), length);
    if (request > 0) {
        std::scoped_lock lock{state_mutex};
        request_urls[request] = target;
    }
    return request;
}

// A slot's value when it holds an emulator function (outside the eboot), else 0.
u64 CurrentSlot(u64 slot_offset) {
    const u64 base = BbscMod::EbootAddress();
    const u64 value = BbscMod::Platform::ReadSlot(base + slot_offset);
    return value != 0 && (value < base || value >= base + EbootSize) ? value : 0;
}

int PS4_SYSV_ABI SendRequest(int request, const void* data, u64 size) {
    std::string url;
    {
        std::scoped_lock lock{state_mutex};
        if (const auto it = request_urls.find(request); it != request_urls.end()) {
            url = it->second;
        }
    }
    WatchSessionForStaleClaims();
    if (!IsSummon(url)) {
        return original_send(request, data, size);
    }
    if (IsPublicServer(url)) {
        static std::atomic<bool> logged{false};
        if (!logged.exchange(true)) {
            LOG_INFO(Mod, "HTTP: summon requests go to a public server - sent unchanged (no "
                          "seamless headers, connect-first or cleanup)");
        }
        return original_send(request, data, size);
    }
    // Not wrapped, so the slots always hold the emulator's current functions: read them again at
    // use (review 2026-10-11 - the official shadPS4 links the imports again after start-up).
    if (const u64 add = CurrentSlot(SlotAddHeader); add != 0) {
        original_add_header = reinterpret_cast<AddHeaderFn>(add);
    }
    if (const u64 length = CurrentSlot(SlotSetContentLength); length != 0) {
        original_set_content_length = reinterpret_cast<SetContentLengthFn>(length);
    }
    if (original_add_header == nullptr || original_set_content_length == nullptr) {
        return original_send(request, data, size); // (review 2026-10-08) the slots never filled
    }
    original_add_header(request, "X-ShadPS4-Bloodborne-Seamless", "1", 0);
    if (IsSummonDelete(url)) {
        own_sign_deleted_ms = NowMs();
    }
    if (IsSummonSearch(url)) {
        const s64 created = own_sign_created_ms.load();
        const bool own_sign_out = SessionState() != 3 && created != 0 &&
                                  created > own_sign_deleted_ms.load() &&
                                  NowMs() - created < OwnSignMs;
        if (own_sign_out) {
            static std::atomic<s64> last_logged{0};
            if (NowMs() - last_logged.load() > 60'000) {
                last_logged = NowMs();
                LOG_INFO(Mod, "HTTP: sign search while our own sign is out - no host placement "
                              "sent (we are waiting, not hosting)");
            }
        } else if (const auto placement = Core::Bloodborne::GetSeamlessHostPlacementHeader()) {
            original_add_header(request, "X-ShadPS4-Bloodborne-Host-Placement", placement->c_str(),
                                0);
            BbscMod::RelaySendPlacement(*placement);
        }
        return original_send(request, data, size);
    }
    if (IsSummonCreate(url)) {
        own_sign_created_ms = NowMs();
    }
    if (IsSummonCreate(url) && data != nullptr && size != 0) {
        std::string body(static_cast<const char*>(data), static_cast<size_t>(size));
        ClearStaleRecord(url, body);
        // A host's placement that has not been refreshed for a while is a host that stopped
        // looking: forget it, as the emulator did when a create reply carried none.
        const s64 received = placement_received_ms.load();
        if (received != 0 && NowMs() - received > PlacementStaleMs) {
            placement_received_ms = 0;
            placement_sender = 0;
            Core::Bloodborne::ClearSeamlessHostPlacementHeader();
        }
        if (RewriteAreaId(body)) {
            original_set_content_length(request, body.size());
            // The emulator copies the body during the call, so a local buffer is fine.
            return original_send(request, body.data(), body.size());
        }
    }
    return original_send(request, data, size);
}

// A sign's reply is a bare {"ResKind":0,"MessageId":"SummonDataCreateResponse"} unless the broker
// delivers a host's claim with it, which adds the claim's members (the host's UserId among them).
int PS4_SYSV_ABI ReadData(int request, void* data, u64 size) {
    const int read = original_read_data(request, data, size);
    if (read > 0 && data != nullptr) {
        std::string url;
        {
            std::scoped_lock lock{state_mutex};
            if (const auto it = request_urls.find(request); it != request_urls.end()) {
                url = it->second;
            }
        }
        // Public servers (2026-10-10: a host's summon never reached the guest on The Hunter's
        // Dream): every summon reply that is not a bare create answer goes into the log.
        if (IsSummon(url) && IsPublicServer(url)) {
            const std::string_view reply(static_cast<const char*>(data), static_cast<size_t>(read));
            const size_t q = url.find('?');
            const size_t slash = url.rfind('/', q);
            const std::string what = url.substr(slash + 1, q == std::string::npos ? q : q - slash - 1);
            const bool bare = reply.find("\"SummonDataCreateResponse\"") != std::string_view::npos &&
                              reply.size() <= 60;
            static std::atomic<int> bare_logged{0};
            if (what == "get") {
                // Sign lists hold other players' signs: only their size.
                LOG_INFO(Mod, "HTTP: summon reply (get, {} bytes)", read);
            } else if (!bare || bare_logged.fetch_add(1) < 2) {
                std::string text(reply.substr(0, 400));
                for (char& c : text) {
                    if (static_cast<unsigned char>(c) < 0x20) {
                        c = '.';
                    }
                }
                LOG_INFO(Mod, "HTTP: summon reply ({}, {} bytes): {}", what, read, text);
            }
        }
        if (IsSummonCreate(url)) {
            const std::string_view reply(static_cast<const char*>(data), static_cast<size_t>(read));
            if (reply.find("\"UserId\"") != std::string_view::npos &&
                claim_delivered_ms.exchange(NowMs()) == 0) {
                LOG_INFO(Mod, "HTTP: a host's claim came with our sign ({} bytes) - waiting for the "
                              "join",
                         read);
            }
        }
    }
    return read;
}

int PS4_SYSV_ABI DeleteRequest(int request) {
    {
        std::scoped_lock lock{state_mutex};
        request_urls.erase(request);
    }
    return original_delete(request);
}

// Waits until the emulator has filled the slot (a host address, outside the eboot), then swaps
// it. The original goes into `original` BEFORE the slot points at the wrapper (review 2026-10-08:
// the game could call the wrapper before the original was stored). False if the slot never filled.
template <typename Fn>
bool Swap(u64 slot_offset, Fn wrapper, Fn& original, const char* name) {
    const u64 slot = BbscMod::EbootAddress() + slot_offset;
    for (int i = 0; i < 60'000; ++i) {
        const u64 value = BbscMod::Platform::ReadSlot(slot);
        if (value != 0 && (value < BbscMod::EbootAddress() || value >= BbscMod::EbootAddress() + EbootSize)) {
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

// The two slots read, not swapped: waits for them as Swap does (they are filled at the same time).
u64 WaitForSlot(u64 slot_offset) {
    const u64 slot = BbscMod::EbootAddress() + slot_offset;
    for (int i = 0; i < 60'000; ++i) {
        const u64 value = BbscMod::Platform::ReadSlot(slot);
        if (value != 0 && (value < BbscMod::EbootAddress() || value >= BbscMod::EbootAddress() + EbootSize)) {
            return value;
        }
        BbscMod::Platform::SleepMs(1);
    }
    return 0;
}

} // namespace

namespace BbscMod {

// The relay asks before connecting (bbsc_relay.cpp): public servers run no BBSC relay.
bool IsPublicServerAddress(const std::string& url_or_host) {
    return IsPublicServer(url_or_host);
}

// A host's placement from the relay: what the broker's reply header carried in the emulator
// build. Used only by a player who is not in a session yet (a guest waiting to be summoned).
void OnHostPlacement(const std::string& placement, u32 sender) {
    const s32 state = SessionState();
    if (state == 3 || state == 4 || state == 6) {
        return; // hosting, joining or already a guest
    }
    // One host at a time: another sender's placement is taken only once ours has gone quiet.
    const u32 current = placement_sender.load();
    const s64 received = placement_received_ms.load();
    if (current != 0 && current != sender && received != 0 &&
        NowMs() - received <= PlacementStaleMs) {
        static std::atomic<s64> last_logged{0};
        if (NowMs() - last_logged.load() > 30'000) {
            last_logged = NowMs();
            LOG_INFO(Mod, "HTTP: placement from another player {:08x} ignored - following host "
                          "{:08x}",
                     sender, current);
        }
        return;
    }
    if (Core::Bloodborne::SetSeamlessHostPlacementHeader(placement)) {
        if (placement_sender.exchange(sender) != sender) {
            LOG_INFO(Mod, "HTTP: following host {:08x} for the sign's map", sender);
        }
        // "1,<map>,x,y,z,heading,area": the map is logged whenever it changes (2026-10-07: the
        // third player's sign stayed on a map the host had left; only the first one was logged).
        static std::string last_map;
        const size_t a = placement.find(',');
        const size_t b = a == std::string::npos ? a : placement.find(',', a + 1);
        const std::string map = b == std::string::npos ? std::string{} : placement.substr(a + 1, b - a - 1);
        if (placement_received_ms.exchange(NowMs()) == 0 || map != last_map) {
            LOG_INFO(Mod, "HTTP: a host is looking for signs - placement {}", placement);
        }
        last_map = map;
    }
}

void InstallHttpWrappers() {
    LoadOverrides();
    original_add_header = reinterpret_cast<AddHeaderFn>(WaitForSlot(SlotAddHeader));
    original_set_content_length =
        reinterpret_cast<SetContentLengthFn>(WaitForSlot(SlotSetContentLength));
    Swap<CreateConnectionFn>(SlotCreateConnection, &CreateConnectionWithURL,
                             original_create_connection, "CreateConnectionWithURL");
    Swap<CreateRequestFn>(SlotCreateRequest, &CreateRequestWithURL, original_create_request,
                          "CreateRequestWithURL");
    Swap<SendFn>(SlotSend, &SendRequest, original_send, "SendRequest");
    Swap<DeleteFn>(SlotDelete, &DeleteRequest, original_delete, "DeleteRequest");
    Swap<ReadDataFn>(SlotReadData, &ReadData, original_read_data, "ReadData");
    const bool ok = original_create_connection && original_create_request && original_add_header &&
                    original_set_content_length && original_send && original_delete &&
                    original_read_data;
    LOG_INFO(Mod, "HTTP: game's HTTP calls {} (URL overrides, seamless summon, connect-first, stale "
                  "cleanup)",
             ok ? "wrapped" : "NOT wrapped - an import slot was never filled");
}

} // namespace BbscMod
