// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/bbsc_memscan.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <cstring>

namespace Core::Bbsc {

namespace {

#ifdef _WIN32
// One region. The emulator changes page protection all the time (GPU tracking), so a page that was
// readable a moment ago may fault now: the search runs under SEH and a fault just ends the region.
void SearchRegion(const std::uint8_t* start, std::size_t size, const std::uint8_t* needle,
                  std::size_t length, std::vector<std::uint64_t>& hits, std::size_t max_hits) {
    __try {
        const std::uint8_t* end = start + size;
        const std::uint8_t* at = start;
        while (hits.size() < max_hits && end - at >= static_cast<std::ptrdiff_t>(length)) {
            at = static_cast<const std::uint8_t*>(
                std::memchr(at, needle[0], static_cast<std::size_t>(end - at) - length + 1));
            if (at == nullptr) {
                break;
            }
            if (std::memcmp(at, needle, length) == 0) {
                hits.push_back(reinterpret_cast<std::uint64_t>(at));
            }
            ++at;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}
#endif

} // namespace

std::vector<std::uint64_t> FindInMemory(std::span<const std::uint8_t> needle, std::uint64_t lo,
                                        std::uint64_t hi, std::size_t max_hits) {
    std::vector<std::uint64_t> hits;
#ifdef _WIN32
    if (needle.empty()) {
        return hits;
    }
    constexpr DWORD Readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                               PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    std::uint64_t at = lo;
    while (at < hi && hits.size() < max_hits) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<void*>(at), &info, sizeof(info)) == 0) {
            break;
        }
        const auto start = reinterpret_cast<std::uint64_t>(info.BaseAddress);
        const std::uint64_t end = start + info.RegionSize;
        if (info.State == MEM_COMMIT && (info.Protect & Readable) != 0 &&
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0) {
            SearchRegion(reinterpret_cast<const std::uint8_t*>(start), info.RegionSize,
                         needle.data(), needle.size(), hits, max_hits);
        }
        if (end <= at) {
            break;
        }
        at = end;
    }
#endif
    return hits;
}

} // namespace Core::Bbsc
