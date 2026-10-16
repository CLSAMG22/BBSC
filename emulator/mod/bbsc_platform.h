// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

// BBSC mod: the few operating-system calls the mod makes. Windows (bbsc.dll in shadPS4 and BBport
// for Windows) is answered here with the calls the DLL always made; Linux (libbbsc.so in BBport
// for Linux, loaded into bb-probe by linux/bbsc-probe) in bbsc_platform_linux.cpp.

#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "common/types.h"

namespace BbscMod::Platform {

u64 NowMs(); // a steady millisecond clock (GetTickCount64)
void SleepMs(u32 ms);
void StartThread(void (*body)()); // a detached thread
u32 ProcessId();

struct LocalTime {
    int year, month, day, hour, minute, second, millisecond;
};
LocalTime Now();

std::filesystem::path ModulePath(); // bbsc.dll / libbbsc.so
std::filesystem::path ProgramPath(); // the host: shadPS4.exe, bb-probe...
std::FILE* OpenFile(const std::filesystem::path& path, const char* mode);
void SetEnv(const char* name, const char* value);

// Where host_overrides.json may be, most specific first: the file the host names in
// SHADPS4_HTTP_HOST_OVERRIDES_JSON (shadPS4's own rule; BBport's run.sh / run.py set it), then the
// host's user folders.
std::vector<std::filesystem::path> HostOverridesFiles();

#ifndef _WIN32
// Takes libbbsc.so out of LD_PRELOAD, so the programs bb-probe starts (its restart runs run.sh
// again, which preloads it again through bbsc-probe) do not load the mod themselves.
void ForgetPreload();
#endif

// ---- Memory -------------------------------------------------------------------------------------

// Access bits, as the emulator's MemoryProt: 1 read, 2 write, 4 execute. 0: no access (also a
// guard page).
constexpr u32 ProtRead = 1;
constexpr u32 ProtWrite = 2;
constexpr u32 ProtExec = 4;

// The mapping (or the unmapped gap) that holds an address.
struct Region {
    u64 start = 0;
    u64 end = 0;
    bool committed = false;
    u32 prot = 0;
};
bool QueryRegion(u64 address, Region& out);

// Makes [address, address + size) read/write/execute for good (the game image's code).
bool MakeWritableCode(u64 address, u64 size);
// Writes code bytes: made writable for the copy, then the old protection is put back.
bool WriteCode(u64 address, const void* bytes, size_t size);
// An 8-byte import slot: read so that a page that changes under us cannot fault, written as above.
u64 ReadSlot(u64 address);
void WriteSlot(u64 address, u64 value);
// size bytes of read/write/execute memory at exactly address, or nullptr.
void* MapCodeAt(u64 address, u64 size);
void FlushCode(const void* address, size_t size);

#ifdef _WIN32

inline u64 NowMs() {
    return GetTickCount64();
}

inline void SleepMs(u32 ms) {
    Sleep(ms);
}

inline void StartThread(void (*body)()) {
    const auto run = [](void* param) -> DWORD {
        reinterpret_cast<void (*)()>(param)();
        return 0;
    };
    if (HANDLE thread = CreateThread(nullptr, 0, run, reinterpret_cast<void*>(body), 0, nullptr)) {
        CloseHandle(thread);
    }
}

inline u32 ProcessId() {
    return GetCurrentProcessId();
}

inline LocalTime Now() {
    SYSTEMTIME now;
    GetLocalTime(&now);
    return {now.wYear, now.wMonth,  now.wDay,         now.wHour,
            now.wMinute, now.wSecond, now.wMilliseconds};
}

// The module this code is in (the DLL, not the emulator).
inline std::filesystem::path ModulePath() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&ModulePath), &self);
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(self, path, MAX_PATH);
    return path;
}

inline std::filesystem::path ProgramPath() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

inline std::FILE* OpenFile(const std::filesystem::path& path, const char* mode) {
    const std::string narrow{mode};
    return _wfopen(path.c_str(), std::wstring(narrow.begin(), narrow.end()).c_str());
}

inline void SetEnv(const char* name, const char* value) {
    _putenv_s(name, value);
    SetEnvironmentVariableA(name, value);
}

inline std::vector<std::filesystem::path> HostOverridesFiles() {
    std::vector<std::filesystem::path> files;
    if (const char* named = std::getenv("SHADPS4_HTTP_HOST_OVERRIDES_JSON"); named && named[0]) {
        files.emplace_back(named);
    }
    // The emulator's own rule: a "user" folder in the folder it was started in wins (portable).
    wchar_t cwd[MAX_PATH]{};
    GetCurrentDirectoryW(MAX_PATH, cwd);
    files.push_back(std::filesystem::path{cwd} / L"user" / L"host_overrides.json");
    files.push_back(ProgramPath().parent_path() / L"user" / L"host_overrides.json");
    if (const wchar_t* appdata = _wgetenv(L"APPDATA")) {
        files.push_back(std::filesystem::path{appdata} / L"shadPS4" / L"host_overrides.json");
    }
    return files;
}

inline u32 ProtFromPage(DWORD protect) {
    if ((protect & PAGE_GUARD) != 0) {
        return 0;
    }
    switch (protect & 0xFF) {
    case PAGE_READONLY:
        return ProtRead;
    case PAGE_READWRITE:
    case PAGE_WRITECOPY:
        return ProtRead | ProtWrite;
    case PAGE_EXECUTE_READ:
        return ProtRead | ProtExec;
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
        return ProtRead | ProtWrite | ProtExec;
    case PAGE_EXECUTE:
        return ProtExec;
    default:
        return 0;
    }
}

inline bool QueryRegion(u64 address, Region& out) {
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) == 0) {
        return false;
    }
    out.start = reinterpret_cast<u64>(info.BaseAddress);
    out.end = out.start + info.RegionSize;
    out.committed = info.State == MEM_COMMIT;
    out.prot = ProtFromPage(info.Protect);
    return true;
}

inline bool MakeWritableCode(u64 address, u64 size) {
    DWORD old = 0;
    return VirtualProtect(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE, &old);
}

inline bool WriteCode(u64 address, const void* bytes, size_t size) {
    void* at = reinterpret_cast<void*>(address);
    DWORD old = 0;
    if (!VirtualProtect(at, size, PAGE_EXECUTE_READWRITE, &old)) {
        return false;
    }
    std::memcpy(at, bytes, size);
    DWORD ignored = 0;
    VirtualProtect(at, size, old, &ignored);
    FlushInstructionCache(GetCurrentProcess(), at, size);
    return true;
}

inline u64 ReadSlot(u64 address) {
    return *reinterpret_cast<const volatile u64*>(address);
}

inline void WriteSlot(u64 address, u64 value) {
    auto* slot = reinterpret_cast<volatile u64*>(address);
    DWORD old = 0;
    VirtualProtect(const_cast<u64*>(slot), 8, PAGE_READWRITE, &old);
    *slot = value;
    DWORD ignored = 0;
    VirtualProtect(const_cast<u64*>(slot), 8, old, &ignored);
}

inline void* MapCodeAt(u64 address, u64 size) {
    return VirtualAlloc(reinterpret_cast<void*>(address), size, MEM_RESERVE | MEM_COMMIT,
                        PAGE_EXECUTE_READWRITE);
}

inline void FlushCode(const void* address, size_t size) {
    FlushInstructionCache(GetCurrentProcess(), address, size);
}

#endif // _WIN32

} // namespace BbscMod::Platform
