// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

// BBSC mod: send this player's BBSC logs to the game server when it asks, for debugging.
//
// Bugs in co-op need both players' logs, and getting a guest's logs meant asking them to find the
// folder and send it by hand. The game server can now ask every player connected to its relay for
// theirs (relay frame 0x7E + u32 request id, see bbsc_relay.cpp). Each player answers with an HTTP
// POST of each of its newest logs to the same game server:
//     POST http://<server>:<http port>/bbsc/client_log?request=<id>&file=<name>
// with the file as the body. The server keeps them under its logs\clients folder.
//
// No setting (the user, 2026-10-20: the logs are for stability work, so a request is always
// answered). Only on request, only to the game server the player is connected to, and only BBSC's
// own logs\bbsc-mod-*.log files, the newest BbscShareLogCount of them (the current run's
// included), nothing else. The server takes them only for a request it made itself.

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "common/types.h"

namespace BbscMod {
void LogPlain(const char* text);
std::filesystem::path ModDir(); // bbsc_mod.cpp
} // namespace BbscMod
namespace Core::Bloodborne {
void RequestBbscStateSnapshot(); // bloodborne_re.cpp
} // namespace Core::Bloodborne

namespace {

constexpr size_t BbscShareLogCount = 3;
constexpr u64 BbscShareLogMaxBytes = 48ull * 1024 * 1024; // a longer log sends its last 48 MB
constexpr DWORD BbscShareTimeoutMs = 60'000;

std::atomic<bool> busy{false};

struct Job {
    u32 request;
    std::string server;
    u16 port;
};

void Log(const std::string& text) {
    BbscMod::LogPlain(("Logs: " + text).c_str());
}

// The newest bbsc-mod-*.log files, newest first.
std::vector<std::filesystem::path> NewestLogs() {
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> found;
    std::error_code error;
    for (const auto& entry :
         std::filesystem::directory_iterator(BbscMod::ModDir() / L"logs", error)) {
        const std::wstring name = entry.path().filename().wstring();
        if (entry.is_regular_file(error) && name.rfind(L"bbsc-mod-", 0) == 0 &&
            entry.path().extension() == L".log") {
            found.emplace_back(entry.last_write_time(error), entry.path());
        }
    }
    std::ranges::sort(found, [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<std::filesystem::path> newest;
    for (size_t i = 0; i < found.size() && i < BbscShareLogCount; ++i) {
        newest.push_back(found[i].second);
    }
    return newest;
}

// The file's bytes; the current run's log is still open for writing, so it is read shared.
bool ReadShared(const std::filesystem::path& path, std::string& out) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size{};
    GetFileSizeEx(file, &size);
    u64 length = static_cast<u64>(size.QuadPart);
    if (length > BbscShareLogMaxBytes) {
        LARGE_INTEGER skip{};
        skip.QuadPart = static_cast<LONGLONG>(length - BbscShareLogMaxBytes);
        SetFilePointerEx(file, skip, nullptr, FILE_BEGIN);
        length = BbscShareLogMaxBytes;
    }
    out.resize(static_cast<size_t>(length));
    DWORD got = 0;
    size_t done = 0;
    while (done < out.size() &&
           ReadFile(file, out.data() + done, static_cast<DWORD>(std::min<size_t>(out.size() - done, 1u << 20)),
                    &got, nullptr) &&
           got != 0) {
        done += got;
    }
    out.resize(done);
    CloseHandle(file);
    return true;
}

// POSTs one file; the HTTP status, or 0 when the server could not be reached.
DWORD Upload(HINTERNET connection, u32 request, const std::wstring& name, const std::string& body) {
    const std::wstring path = L"/bbsc/client_log?request=" + std::to_wstring(request) +
                              L"&file=" + name;
    HINTERNET post = WinHttpOpenRequest(connection, L"POST", path.c_str(), nullptr,
                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (post == nullptr) {
        return 0;
    }
    DWORD status = 0;
    const wchar_t* headers = L"Content-Type: text/plain; charset=utf-8\r\n";
    if (WinHttpSendRequest(post, headers, static_cast<DWORD>(-1),
                           const_cast<char*>(body.data()), static_cast<DWORD>(body.size()),
                           static_cast<DWORD>(body.size()), 0) &&
        WinHttpReceiveResponse(post, nullptr)) {
        DWORD size = sizeof(status);
        WinHttpQueryHeaders(post, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    }
    WinHttpCloseHandle(post);
    return status;
}

DWORD WINAPI ShareThread(void* raw) {
    const Job job = *static_cast<Job*>(raw);
    delete static_cast<Job*>(raw);
    // Give the game thread a moment to log its state snapshot, so the current log has it.
    Core::Bloodborne::RequestBbscStateSnapshot();
    Sleep(1'500);
    const auto logs = NewestLogs();
    Log("the game server asked for our logs (request " + std::to_string(job.request) +
        ") - sending " + std::to_string(logs.size()) + " file(s) to " + job.server + ":" +
        std::to_string(job.port));
    HINTERNET session = WinHttpOpen(L"BBSC-logs/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    const std::wstring server(job.server.begin(), job.server.end());
    HINTERNET connection =
        session != nullptr ? WinHttpConnect(session, server.c_str(), job.port, 0) : nullptr;
    if (session != nullptr) {
        WinHttpSetTimeouts(session, 5'000, 5'000, BbscShareTimeoutMs, BbscShareTimeoutMs);
    }
    size_t sent = 0;
    for (const auto& path : logs) {
        std::string body;
        if (connection == nullptr || !ReadShared(path, body)) {
            break;
        }
        const DWORD status = Upload(connection, job.request, path.filename().wstring(), body);
        const std::string name = path.filename().string();
        if (status == 200) {
            ++sent;
            Log(name + " sent (" + std::to_string(body.size() / 1024) + " KB)");
        } else {
            Log(name + " not sent (" +
                (status == 0 ? std::string("server not reachable") : "status " + std::to_string(status)) +
                ")");
            if (status == 0 || status == 403) {
                break;
            }
        }
    }
    if (connection != nullptr) {
        WinHttpCloseHandle(connection);
    }
    if (session != nullptr) {
        WinHttpCloseHandle(session);
    }
    Log(std::to_string(sent) + " of " + std::to_string(logs.size()) + " log(s) sent");
    busy = false;
    return 0;
}

} // namespace

namespace BbscMod {

// From the relay thread (bbsc_relay.cpp): the game server asks for our logs.
void OnLogRequest(u32 request, const std::string& server, u16 http_port) {
    if (server.empty() || busy.exchange(true)) {
        return;
    }
    auto* job = new Job{request, server, http_port};
    if (HANDLE thread = CreateThread(nullptr, 0, ShareThread, job, 0, nullptr)) {
        CloseHandle(thread);
    } else {
        delete job;
        busy = false;
    }
}

} // namespace BbscMod
