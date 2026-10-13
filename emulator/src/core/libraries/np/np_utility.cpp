// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

// libSceNpUtility's NP lookup (online id -> NpId).
//
// 2026-10-06 (4-player test): every guest that joined a room which already held another guest
// called sceNpLookupNpId for that other player, got nothing back (the call was an unresolved stub),
// passed the empty NpId to sceNpSignalingActivateConnection ("peer NpId is not a valid online id
// (first bytes 00 ...)"), and left the room. With one guest there is nobody to look up, which is
// why two players always worked. On shadnet an NpId is just the online id in its handle, so the
// lookup answers at once; the async request reports "finished" on the first poll.

#include <atomic>
#include <cstring>
#include "common/logging/log.h"
#include "core/libraries/error_codes.h"
#include "core/libraries/libs.h"
#include "core/libraries/np/np_types.h"
#include "core/libraries/np/np_utility.h"

namespace Libraries::Np::NpUtility {

constexpr s32 ORBIS_NP_LOOKUP_ERROR_INVALID_ARGUMENT = static_cast<s32>(0x80550702);
constexpr s32 PollAsyncFinished = 0;

std::atomic<s32> g_next_id{1};

s32 PS4_SYSV_ABI sceNpLookupCreateTitleCtx(const void* /*title_id*/, const OrbisNpId* /*self*/) {
    const s32 id = g_next_id++;
    LOG_INFO(Lib_NpManager, "lookup title ctx {}", id);
    return id;
}

s32 PS4_SYSV_ABI sceNpLookupDeleteTitleCtx(s32 /*ctx_id*/) {
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpLookupCreateAsyncRequest(s32 /*ctx_id*/) {
    return g_next_id++;
}

s32 PS4_SYSV_ABI sceNpLookupNpId(s32 req_id, const OrbisNpOnlineId* online_id, OrbisNpId* np_id,
                                 void* /*option*/) {
    if (online_id == nullptr || np_id == nullptr) {
        return ORBIS_NP_LOOKUP_ERROR_INVALID_ARGUMENT;
    }
    std::memset(np_id, 0, sizeof(*np_id));
    std::memcpy(np_id->handle.data, online_id->data, sizeof(np_id->handle.data));
    np_id->handle.term = 0;
    LOG_INFO(Lib_NpManager, "lookup {}: NpId for '{}'", req_id,
             std::string(np_id->handle.data, strnlen(np_id->handle.data,
                                                     sizeof(np_id->handle.data))));
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpLookupPollAsync(s32 /*req_id*/, s32* result) {
    if (result != nullptr) {
        *result = ORBIS_OK;
    }
    return PollAsyncFinished;
}

s32 PS4_SYSV_ABI sceNpLookupAbortRequest(s32 /*req_id*/) {
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpLookupDeleteRequest(s32 /*req_id*/) {
    return ORBIS_OK;
}

void RegisterLib(Core::Loader::SymbolsResolver* sym) {
    LIB_FUNCTION("8533Q+LU7EQ", "libSceNpUtility", 1, "libSceNpUtility",
                 sceNpLookupCreateTitleCtx);
    LIB_FUNCTION("mtqDK9zkoIE", "libSceNpUtility", 1, "libSceNpUtility",
                 sceNpLookupDeleteTitleCtx);
    LIB_FUNCTION("JA4+sS39GMs", "libSceNpUtility", 1, "libSceNpUtility",
                 sceNpLookupCreateAsyncRequest);
    LIB_FUNCTION("T6tnM1Uti4g", "libSceNpUtility", 1, "libSceNpUtility", sceNpLookupNpId);
    LIB_FUNCTION("V4EVrruHuy8", "libSceNpUtility", 1, "libSceNpUtility", sceNpLookupPollAsync);
    LIB_FUNCTION("eYz4v5Uek9U", "libSceNpUtility", 1, "libSceNpUtility", sceNpLookupAbortRequest);
    LIB_FUNCTION("wLaxchvEEnk", "libSceNpUtility", 1, "libSceNpUtility",
                 sceNpLookupDeleteRequest);
};

} // namespace Libraries::Np::NpUtility
