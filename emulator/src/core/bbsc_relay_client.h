// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// BBSC relay client for the emulator build (2026-10-19). The mod (mod\bbsc_relay.cpp) sends and
// takes BBSC's own packets through the game server's relay (TCP 20444); the emulator build only
// used its P2P signaling. A mod player and an emulator player in one session therefore never heard
// each other: a mod guest of an emulator host took a stranger's packets instead (see
// bbsc_relay_client.cpp). The emulator build now joins the relay too, with the same frames.
namespace Core::Bbsc {

// Connects to the game server's relay (host_overrides.json, or BBSC_RELAY=host[:port]).
// SHADPS4_BBSC_RELAY=0 keeps it off.
void StartRelayClient(std::string_view serial);

// One BBSC packet ("SHAD" + type ...) to the relay as well; returns the players it reached.
std::uint32_t RelaySendPacket(const void* packet, std::uint32_t size);

// A BBSC packet came over the P2P signaling: while those arrive, the relay's host packets are
// not needed (two emulator builds keep talking as before).
void NoteSignalingBbscPacket();

// The summon broker requests (http.cpp): our own sign's UserId (from its /create body) and a
// claim of another player's sign (the host rang it: its player learns over the relay which
// relay sender is its host, as with the mod).
void NoteOwnSignUserId(std::int64_t user_id);
void RelaySendAimedPlacement(std::int64_t target_user, const std::string& placement);

} // namespace Core::Bbsc
