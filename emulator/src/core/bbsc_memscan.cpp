// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/bbsc_memscan.h"

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>
#include <cstdio>
#include <string>
#endif

#include <algorithm>
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
#elif defined(__linux__)
// The memory is read with process_vm_readv, not touched: a page that went away or lost its read
// access since /proc/self/maps was read fails the call (EFAULT) instead of faulting the game.
void SearchRegion(std::uint64_t start, std::uint64_t end, const std::uint8_t* needle,
                  std::size_t length, std::vector<std::uint64_t>& hits, std::size_t max_hits) {
    constexpr std::size_t Chunk = 1 << 20;
    std::vector<std::uint8_t> buffer(Chunk + length);
    std::size_t carried = 0; // the end of the previous chunk, for a needle across chunks
    for (std::uint64_t at = start; at < end && hits.size() < max_hits;) {
        const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(Chunk, end - at));
        iovec local{buffer.data() + carried, want};
        iovec remote{reinterpret_cast<void*>(at), want};
        const ssize_t got = process_vm_readv(getpid(), &local, 1, &remote, 1, 0);
        if (got <= 0) {
            at += want;
            carried = 0;
            continue;
        }
        const std::size_t have = carried + static_cast<std::size_t>(got);
        const std::uint64_t base = at - carried;
        for (std::size_t i = 0; i + length <= have && hits.size() < max_hits; ++i) {
            const void* found = std::memchr(buffer.data() + i, needle[0], have - length - i + 1);
            if (found == nullptr) {
                break;
            }
            i = static_cast<std::size_t>(static_cast<const std::uint8_t*>(found) - buffer.data());
            if (std::memcmp(buffer.data() + i, needle, length) == 0) {
                hits.push_back(base + i);
            }
        }
        carried = std::min(length - 1, have);
        std::memmove(buffer.data(), buffer.data() + have - carried, carried);
        at += static_cast<std::uint64_t>(got);
    }
}
#endif

} // namespace

#ifdef __linux__
std::vector<MemoryMapEntry> ReadMemoryMap() {
    std::vector<MemoryMapEntry> entries;
    const int fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return entries;
    }
    std::string text;
    char chunk[64 * 1024];
    for (ssize_t got; (got = read(fd, chunk, sizeof(chunk))) > 0;) {
        text.append(chunk, static_cast<std::size_t>(got));
    }
    close(fd);
    entries.reserve(text.size() / 64);
    for (std::size_t line = 0; line < text.size();) {
        const std::size_t next = text.find('\n', line);
        unsigned long long start = 0;
        unsigned long long end = 0;
        char perms[5]{};
        if (std::sscanf(text.c_str() + line, "%llx-%llx %4s", &start, &end, perms) == 3) {
            entries.push_back({start, end, perms[0] == 'r', perms[1] == 'w', perms[2] == 'x'});
        }
        line = next == std::string::npos ? text.size() : next + 1;
    }
    return entries;
}
#endif

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
#elif defined(__linux__)
    if (needle.empty()) {
        return hits;
    }
    for (const MemoryMapEntry& entry : ReadMemoryMap()) {
        if (!entry.read || entry.end <= lo || entry.start >= hi || hits.size() >= max_hits) {
            continue;
        }
        SearchRegion(std::max(entry.start, lo), std::min(entry.end, hi), needle.data(),
                     needle.size(), hits, max_hits);
    }
#endif
    return hits;
}

} // namespace Core::Bbsc
