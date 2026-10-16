// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

// BBSC mod: bbsc_platform.h for Linux - libbbsc.so, preloaded into BBport's bb-probe.
//
// Memory questions are answered from /proc/self/maps. Reading it on every check would be far too
// slow (bloodborne_re.cpp checks every read, and bbsc_mod.cpp already keeps a short per-thread
// cache in front of this, like the DLL does for VirtualQuery), so the map is read again at most
// every MapRefreshMs and right after the mod changed a protection itself.
// Import slots are read with process_vm_readv: bb-probe sets up and links the image while the mod
// is already waiting for it, and a page that changes under a plain read would fault the game.

#include "bbsc_platform.h"

#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <thread>

#include "core/bbsc_memscan.h"

namespace BbscMod::Platform {

namespace {

constexpr u64 MapRefreshMs = 100;
constexpr u64 AddressSpaceEnd = 1ULL << 47;

std::mutex map_mutex;
std::vector<Core::Bbsc::MemoryMapEntry> map_entries;
u64 map_read_ms = 0;
bool map_stale = true;

void ForgetMap() {
    std::scoped_lock lock{map_mutex};
    map_stale = true;
}

u64 PageSize() {
    static const u64 size = static_cast<u64>(sysconf(_SC_PAGESIZE));
    return size;
}

int PosixProt(u32 prot) {
    return ((prot & ProtRead) ? PROT_READ : 0) | ((prot & ProtWrite) ? PROT_WRITE : 0) |
           ((prot & ProtExec) ? PROT_EXEC : 0);
}

// Copies size bytes to address. A page that is not writable is made writable (keeping its other
// access) for the copy and put back after.
bool WriteProtected(u64 address, const void* bytes, size_t size) {
    Region region;
    if (!QueryRegion(address, region) || !region.committed) {
        return false;
    }
    if ((region.prot & ProtWrite) != 0 && address + size <= region.end) {
        if (size == 8 && address % 8 == 0) {
            u64 value = 0;
            std::memcpy(&value, bytes, 8);
            // One store: an import slot is never seen half-written.
            *reinterpret_cast<volatile u64*>(address) = value;
        } else {
            std::memcpy(reinterpret_cast<void*>(address), bytes, size);
        }
        return true;
    }
    const u64 first = address & ~(PageSize() - 1);
    const u64 length = ((address + size + PageSize() - 1) & ~(PageSize() - 1)) - first;
    if (mprotect(reinterpret_cast<void*>(first), length, PosixProt(region.prot | ProtWrite)) != 0) {
        return false;
    }
    std::memcpy(reinterpret_cast<void*>(address), bytes, size);
    mprotect(reinterpret_cast<void*>(first), length, PosixProt(region.prot));
    ForgetMap();
    return true;
}

} // namespace

u64 NowMs() {
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count());
}

void SleepMs(u32 ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void StartThread(void (*body)()) {
    std::thread(body).detach();
}

u32 ProcessId() {
    return static_cast<u32>(getpid());
}

LocalTime Now() {
    timespec now{};
    clock_gettime(CLOCK_REALTIME, &now);
    tm local{};
    localtime_r(&now.tv_sec, &local);
    return {local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
            local.tm_hour,        local.tm_min,     local.tm_sec,
            static_cast<int>(now.tv_nsec / 1'000'000)};
}

std::filesystem::path ModulePath() {
    Dl_info info{};
    if (dladdr(reinterpret_cast<const void*>(&ModulePath), &info) == 0 ||
        info.dli_fname == nullptr) {
        return {};
    }
    std::error_code ignored;
    return std::filesystem::absolute(info.dli_fname, ignored);
}

std::filesystem::path ProgramPath() {
    std::error_code ignored;
    return std::filesystem::read_symlink("/proc/self/exe", ignored);
}

std::FILE* OpenFile(const std::filesystem::path& path, const char* mode) {
    return std::fopen(path.c_str(), mode);
}

void SetEnv(const char* name, const char* value) {
    setenv(name, value, 1);
}

std::vector<std::filesystem::path> HostOverridesFiles() {
    std::vector<std::filesystem::path> files;
    if (const char* named = std::getenv("SHADPS4_HTTP_HOST_OVERRIDES_JSON"); named && named[0]) {
        files.emplace_back(named);
    }
    std::error_code ignored;
    files.push_back(std::filesystem::current_path(ignored) / "user" / "host_overrides.json");
    files.push_back(ProgramPath().parent_path() / "user" / "host_overrides.json");
    return files;
}

void ForgetPreload() {
    const char* preload = std::getenv("LD_PRELOAD");
    const std::string self = ModulePath().filename().string();
    if (preload == nullptr || self.empty()) {
        return;
    }
    std::string kept;
    const std::string list = preload;
    for (size_t at = 0; at <= list.size();) {
        const size_t end = std::min(list.find_first_of(": ", at), list.size());
        const std::string entry = list.substr(at, end - at);
        if (!entry.empty() && std::filesystem::path(entry).filename() != self) {
            kept += (kept.empty() ? "" : ":") + entry;
        }
        at = end + 1;
    }
    if (kept.empty()) {
        unsetenv("LD_PRELOAD");
    } else {
        setenv("LD_PRELOAD", kept.c_str(), 1);
    }
}

bool QueryRegion(u64 address, Region& out) {
    std::scoped_lock lock{map_mutex};
    const u64 now = NowMs();
    if (map_stale || now - map_read_ms > MapRefreshMs) {
        map_entries = Core::Bbsc::ReadMemoryMap();
        map_read_ms = now;
        map_stale = false;
    }
    if (map_entries.empty()) {
        return false;
    }
    // The first mapping that ends after the address: it holds it, or the gap before it does.
    const auto it = std::upper_bound(map_entries.begin(), map_entries.end(), address,
                                     [](u64 at, const auto& entry) { return at < entry.end; });
    if (it != map_entries.end() && it->start <= address) {
        out.start = it->start;
        out.end = it->end;
        out.committed = true;
        out.prot =
            (it->read ? ProtRead : 0) | (it->write ? ProtWrite : 0) | (it->exec ? ProtExec : 0);
        return true;
    }
    out.start = it == map_entries.begin() ? 0 : std::prev(it)->end;
    out.end = it == map_entries.end() ? AddressSpaceEnd : it->start;
    out.committed = false;
    out.prot = 0;
    return true;
}

bool MakeWritableCode(u64 address, u64 size) {
    const u64 first = address & ~(PageSize() - 1);
    const u64 length = ((address + size + PageSize() - 1) & ~(PageSize() - 1)) - first;
    const bool ok = mprotect(reinterpret_cast<void*>(first), length,
                             PROT_READ | PROT_WRITE | PROT_EXEC) == 0;
    ForgetMap();
    return ok;
}

bool WriteCode(u64 address, const void* bytes, size_t size) {
    if (!WriteProtected(address, bytes, size)) {
        return false;
    }
    FlushCode(reinterpret_cast<const void*>(address), size);
    return true;
}

u64 ReadSlot(u64 address) {
    u64 value = 0;
    iovec local{&value, sizeof(value)};
    iovec remote{reinterpret_cast<void*>(address), sizeof(value)};
    return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == sizeof(value) ? value : 0;
}

void WriteSlot(u64 address, u64 value) {
    WriteProtected(address, &value, sizeof(value));
}

void* MapCodeAt(u64 address, u64 size) {
    void* got = mmap(reinterpret_cast<void*>(address), size, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (got == MAP_FAILED) {
        return nullptr;
    }
    ForgetMap();
    if (got != reinterpret_cast<void*>(address)) { // a kernel without MAP_FIXED_NOREPLACE
        munmap(got, size);
        return nullptr;
    }
    return got;
}

void FlushCode(const void* address, size_t size) {
    auto* begin = static_cast<char*>(const_cast<void*>(address));
    __builtin___clear_cache(begin, begin + size);
}

} // namespace BbscMod::Platform
