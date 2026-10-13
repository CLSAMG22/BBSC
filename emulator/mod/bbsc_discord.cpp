// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

// BBSC Discord status: "Playing BBSC - In Central Yharnam - Hosting (2 of 5)", or the boss during a
// boss fight. Talks to the Discord app's local pipe (\\.\pipe\discord-ipc-N) itself - no Discord
// library: a frame is {u32 opcode, u32 length, JSON}; opcode 0 = handshake with the application ID,
// opcode 1 = a command (SET_ACTIVITY). The game state comes from bloodborne_re.cpp's game tick
// (GetBbscPresence). Off with the Mods tab switch "Discord status" (SHADPS4_BBSC_DISCORD=0).
// No Discord running = nothing happens; it tries again every 20 s.

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include <fmt/format.h>

#include "core/bloodborne_re.h"

namespace BbscMod {
void LogPlain(const char* text);
}

namespace {

constexpr const char* DiscordApplicationId = "1554935855674691645"; // "BBSC" (CLS_AMG)

bool DiscordStatusEnabled() {
    const char* value = std::getenv("SHADPS4_BBSC_DISCORD");
    return value == nullptr || (std::string{value} != "0" && std::string{value} != "false");
}

std::string Json(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            out += ' ';
        } else {
            out += c;
        }
    }
    return out + '"';
}

class DiscordPipe {
public:
    ~DiscordPipe() {
        Close();
    }

    bool Connect() {
        for (int i = 0; i < 10; ++i) {
            const std::wstring name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
            pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                               0, nullptr);
            if (pipe != INVALID_HANDLE_VALUE) {
                break;
            }
        }
        if (pipe == INVALID_HANDLE_VALUE) {
            return false;
        }
        if (!Send(0, fmt::format("{{\"v\":1,\"client_id\":\"{}\"}}", DiscordApplicationId))) {
            Close();
            return false;
        }
        return Drain(true);
    }

    bool Send(std::uint32_t opcode, const std::string& json) {
        if (pipe == INVALID_HANDLE_VALUE) {
            return false;
        }
        std::string frame(8, '\0');
        const auto length = static_cast<std::uint32_t>(json.size());
        std::memcpy(frame.data(), &opcode, 4);
        std::memcpy(frame.data() + 4, &length, 4);
        frame += json;
        DWORD written = 0;
        return WriteFile(pipe, frame.data(), static_cast<DWORD>(frame.size()), &written, nullptr) &&
               written == frame.size();
    }

    // Reads whatever Discord sent (answers, READY) so the pipe never fills. wait = block for one
    // frame (the handshake answer). False = the pipe is gone or Discord closed it (opcode 2).
    bool Drain(bool wait) {
        for (;;) {
            DWORD available = 0;
            if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) {
                Close();
                return false;
            }
            if (available < 8 && !wait) {
                return true;
            }
            std::uint32_t header[2]{};
            DWORD got = 0;
            if (!ReadFile(pipe, header, 8, &got, nullptr) || got != 8) {
                Close();
                return false;
            }
            std::string body(header[1], '\0');
            if (header[1] != 0 &&
                (!ReadFile(pipe, body.data(), header[1], &got, nullptr) || got != header[1])) {
                Close();
                return false;
            }
            if (header[0] == 2) { // CLOSE: wrong application ID or Discord shutting down
                BbscMod::LogPlain(("Discord status: Discord closed the connection: " + body).c_str());
                Close();
                return false;
            }
            wait = false;
        }
    }

    bool Connected() const {
        return pipe != INVALID_HANDLE_VALUE;
    }

    void Close() {
        if (pipe != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe);
            pipe = INVALID_HANDLE_VALUE;
        }
    }

private:
    HANDLE pipe = INVALID_HANDLE_VALUE;
};

std::string ActivityJson(const Core::Bloodborne::BbscPresence& p, std::int64_t game_start_s) {
    std::string details;
    std::string state;
    std::string large_text = "Bloodborne Seamless Co-op";
    std::int64_t start = game_start_s;
    if (!p.in_game) {
        details = "At the title screen";
    } else if (!p.boss.empty()) {
        details = "Boss fight: " + p.boss;
        large_text = "In " + p.area;
        start = p.boss_since_s;
    } else {
        details = "In " + p.area;
    }
    if (p.in_game) {
        state = p.role == 1 ? "Hosting" : p.role == 2 ? "Co-op guest" : "Solo hunter";
    }
    std::string activity = "{\"details\":" + Json(details);
    if (!state.empty()) {
        activity += ",\"state\":" + Json(state);
    }
    activity += fmt::format(",\"timestamps\":{{\"start\":{}}}", start);
    activity += ",\"assets\":{\"large_image\":\"bbsc\",\"large_text\":" + Json(large_text) + "}";
    if (p.in_game && p.role != 0) {
        activity += fmt::format(",\"party\":{{\"id\":\"bbsc-{}\",\"size\":[{},{}]}}",
                                GetCurrentProcessId(), p.hunters, p.max_hunters);
    }
    activity += "}";
    return activity;
}

DWORD WINAPI DiscordThread(LPVOID) {
    const std::int64_t game_start_s = std::chrono::duration_cast<std::chrono::seconds>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count();
    DiscordPipe discord;
    std::string sent;
    std::uint64_t nonce = 0;
    auto next_try = std::chrono::steady_clock::now();
    bool logged_connect = false;
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (!discord.Connected()) {
            if (std::chrono::steady_clock::now() < next_try) {
                continue;
            }
            if (!discord.Connect()) {
                next_try = std::chrono::steady_clock::now() + std::chrono::seconds(20);
                continue;
            }
            if (!logged_connect) {
                BbscMod::LogPlain("Discord status: connected to Discord");
                logged_connect = true;
            }
            sent.clear();
        }
        if (!discord.Drain(false)) {
            continue;
        }
        const std::string activity = ActivityJson(Core::Bloodborne::GetBbscPresence(), game_start_s);
        if (activity == sent) {
            continue;
        }
        const std::string command = fmt::format(
            "{{\"cmd\":\"SET_ACTIVITY\",\"args\":{{\"pid\":{},\"activity\":{}}},\"nonce\":\"{}\"}}",
            GetCurrentProcessId(), activity, ++nonce);
        if (discord.Send(1, command)) {
            sent = activity;
        } else {
            discord.Close();
        }
    }
}

} // namespace

namespace BbscMod {

void StartDiscordStatus() {
    if (!DiscordStatusEnabled()) {
        LogPlain("Discord status: off (Mods tab)");
        return;
    }
    if (HANDLE thread = CreateThread(nullptr, 0, DiscordThread, nullptr, 0, nullptr)) {
        CloseHandle(thread);
        LogPlain("Discord status: on - shows when the Discord app is running");
    }
}

} // namespace BbscMod
