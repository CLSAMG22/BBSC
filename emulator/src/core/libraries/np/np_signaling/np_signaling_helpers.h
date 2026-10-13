// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"

namespace Libraries::Np::NpSignaling::Helpers {

struct SignalingRuntimeHooks {
    void (*start_dispatch)(s32 priority, u64 affinity_mask, u64 stack_size);
    void (*start_receive)(s32 priority, u64 affinity_mask, u64 stack_size);
    void (*start_ping)(s32 priority, u64 affinity_mask, u64 stack_size);
    void (*stop_ping)();
    void (*stop_receive)();
    void (*stop_dispatch)();
};

void SetRuntimeHooks(const SignalingRuntimeHooks& hooks);
s32 CheckInitializeAppType(u32* is_app_type_4);
s32 InitSignalingHeap(s64 pool_size);
void ShutdownSignalingHeap();
s32 CheckAppType();
s32 StartMainRuntime(s32 thread_priority, s32 cpu_affinity_mask, s64 thread_stack_size);
s32 StartEchoRuntime(s32 thread_priority, s32 cpu_affinity_mask);
void ShutdownRuntime();

// BBSC: send one "the host is in this map" packet to every established signaling peer. Called
// from the game thread; takes the signaling mutex only long enough to copy the peer addresses.
// Returns how many peers it went to.
u32 SendBbscFollowToPeers(u32 packed_map, float x, float y, float z, float heading, u32 sequence,
                          s32 lamp_point);
// BBSC: send one world-flag chunk (SignalingBbscWorldFlags, filled in by the caller) to every
// established signaling peer. Same threading as SendBbscFollowToPeers.
u32 SendBbscWorldFlagsToPeers(const void* packet, u32 size);

} // namespace Libraries::Np::NpSignaling::Helpers

namespace Libraries::Np::NpSignaling {

void RegisterRuntimeHooks();

}
