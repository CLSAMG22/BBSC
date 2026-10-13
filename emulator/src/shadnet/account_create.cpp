// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <string>
#include <vector>
#include "common/logging/log.h"
#include "shadnet.pb.h"
#include "shadnet/account_create.h"
#include "shadnet/client.h" // socket platform typedefs + protocol constants

namespace ShadNet {

namespace {

void PlatformInit() {
#ifdef _WIN32
    static bool done = false;
    if (!done) {
        WSADATA wd;
        WSAStartup(MAKEWORD(2, 2), &wd);
        done = true;
    }
#endif
}

u32 GetLE32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
           (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
}

void PutLE(std::vector<u8>& b, size_t off, u64 v, int bytes) {
    for (int i = 0; i < bytes; ++i) {
        b[off + i] = static_cast<u8>(v >> (8 * i));
    }
}

bool RecvN(ShadSocketHandle sock, u8* buf, u32 n) {
    u32 got = 0;
    while (got < n) {
        const int r =
            ::recv(sock, reinterpret_cast<char*>(buf) + got, static_cast<int>(n - got), 0);
        if (r <= 0) {
            return false;
        }
        got += static_cast<u32>(r);
    }
    return true;
}

bool SendAll(ShadSocketHandle sock, const std::vector<u8>& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        const int r = ::send(sock, reinterpret_cast<const char*>(data.data()) + sent,
                             static_cast<int>(data.size() - sent), 0);
        if (r <= 0) {
            return false;
        }
        sent += static_cast<size_t>(r);
    }
    return true;
}

// One whole packet: header fields + payload.
bool RecvPacket(ShadSocketHandle sock, PacketType& type, u16& cmd, std::vector<u8>& payload) {
    u8 hdr[SHAD_HEADER_SIZE];
    if (!RecvN(sock, hdr, SHAD_HEADER_SIZE)) {
        return false;
    }
    type = static_cast<PacketType>(hdr[0]);
    cmd = static_cast<u16>(hdr[1] | (hdr[2] << 8));
    const u32 total = GetLE32(hdr + 3);
    if (total < SHAD_HEADER_SIZE || total > SHAD_MAX_PACKET_SIZE) {
        return false;
    }
    payload.resize(total - SHAD_HEADER_SIZE);
    return payload.empty() || RecvN(sock, payload.data(), static_cast<u32>(payload.size()));
}

std::string ErrorText(ErrorType error) {
    switch (error) {
    case ErrorType::NoError:
        return "Account created. Put the same name and password in this user's shadNet fields.";
    case ErrorType::CreationExistingUsername:
        return "That name is taken on this server.";
    case ErrorType::CreationExistingEmail:
        return "That email is already used on this server.";
    case ErrorType::CreationBannedEmailProvider:
        return "The server does not accept that email provider.";
    case ErrorType::Unauthorized:
        return "Wrong or missing registration key - ask the host for it.";
    case ErrorType::InvalidInput:
    case ErrorType::Malformed:
        return "The server rejected the name, password or email (letters, numbers, - and _ only).";
    case ErrorType::TooSoon:
        return "Too many attempts - wait a minute and try again.";
    case ErrorType::DbFail:
        return "The server could not save the account (database error).";
    default:
        return "The server refused (error " + std::to_string(static_cast<int>(error)) + ").";
    }
}

} // namespace

bool SplitServerAddress(const std::string& address, std::string& host, u16& port) {
    host = address;
    port = 31313;
    const auto colon = address.rfind(':');
    if (colon != std::string::npos) {
        host = address.substr(0, colon);
        try {
            const int value = std::stoi(address.substr(colon + 1));
            if (value <= 0 || value > 65535) {
                return false;
            }
            port = static_cast<u16>(value);
        } catch (...) {
            return false;
        }
    }
    return !host.empty();
}

CreateAccountResult CreateAccount(const std::string& host, u16 port, const std::string& npid,
                                  const std::string& password, const std::string& email,
                                  const std::string& secret_key, u32 timeout_ms) {
    CreateAccountResult result;
    PlatformInit();

    addrinfo hints{};
    addrinfo* list = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &list) != 0 || !list) {
        result.message = "Could not find the server address '" + host + "'.";
        return result;
    }
    const ShadSocketHandle sock = ::socket(list->ai_family, SOCK_STREAM, IPPROTO_TCP);
    if (sock == SHAD_INVALID_SOCK) {
        ::freeaddrinfo(list);
        result.message = "Could not open a network socket.";
        return result;
    }
#ifdef _WIN32
    const DWORD timeout = timeout_ms;
#else
    timeval timeout{};
    timeout.tv_sec = static_cast<decltype(timeout.tv_sec)>(timeout_ms / 1000);
    timeout.tv_usec = static_cast<decltype(timeout.tv_usec)>((timeout_ms % 1000) * 1000);
#endif
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
                 sizeof(timeout));
    ::setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout),
                 sizeof(timeout));

    const int connected =
        ::connect(sock, list->ai_addr, static_cast<int>(list->ai_addrlen));
    ::freeaddrinfo(list);
    if (connected != 0) {
        SHAD_CLOSE(sock);
        result.message = "Could not reach " + host + ":" + std::to_string(port) +
                         " - is the server on, and are you on the same VPN / address?";
        return result;
    }

    // The server greets every connection with ServerInfo first.
    PacketType type{};
    u16 cmd = 0;
    std::vector<u8> payload;
    if (!RecvPacket(sock, type, cmd, payload) || type != PacketType::ServerInfo) {
        SHAD_CLOSE(sock);
        result.message = "That address answered, but it is not a shadNet server.";
        return result;
    }

    shadnet::RegistrationRequest req;
    req.set_npid(npid);
    req.set_password(password);
    req.set_email(email);
    if (!secret_key.empty()) {
        req.set_secret_key(secret_key);
    }
    const std::string body = req.SerializeAsString();
    std::vector<u8> packet(SHAD_HEADER_SIZE + 4);
    packet[0] = static_cast<u8>(PacketType::Request);
    PutLE(packet, 1, static_cast<u16>(CommandType::Create), 2);
    PutLE(packet, 3, SHAD_HEADER_SIZE + 4 + body.size(), 4);
    PutLE(packet, 7, 1, 8);
    PutLE(packet, SHAD_HEADER_SIZE, body.size(), 4);
    packet.insert(packet.end(), body.begin(), body.end());
    if (!SendAll(sock, packet)) {
        SHAD_CLOSE(sock);
        result.message = "The connection dropped while sending.";
        return result;
    }

    // Skip anything else (notifications) until the Create reply; its first byte is the error.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!RecvPacket(sock, type, cmd, payload)) {
            break;
        }
        if (type == PacketType::Reply && cmd == static_cast<u16>(CommandType::Create)) {
            const ErrorType error =
                payload.empty() ? ErrorType::Malformed : static_cast<ErrorType>(payload[0]);
            SHAD_CLOSE(sock);
            result.ok = error == ErrorType::NoError;
            result.message = ErrorText(error);
            LOG_INFO(ShadNet, "Create account '{}' on {}:{}: error {}", npid, host, port,
                     static_cast<int>(error));
            return result;
        }
    }
    SHAD_CLOSE(sock);
    result.message = "No answer from the server.";
    return result;
}

} // namespace ShadNet
