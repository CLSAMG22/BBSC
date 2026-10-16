// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

// BBSC as a mod: a DLL that bbsc-launch.exe injects into shadPS4.exe before it starts (on Linux:
// libbbsc.so, preloaded into BBport's bb-probe by linux/bbsc-probe; bbsc_platform_linux.cpp).
//
// The DLL runs in the emulator's process, where the game's memory sits at its guest addresses
// (eboot at 0x800000000 in the shadp2p fork; the official shadPS4 nightly of 2026-10-09 loads it
// at 0x9B00000 - FindEbootBase looks for it), so it needs nothing from the emulator:
//   1. wait until the eboot is in memory and every byte patch site reads back as the bytes that
//      must be there (all of them before writing any - another game or version is left alone);
//   2. write the byte patches (core/bbsc_patch_tables.h, the same list as the emulator build);
//   3. install every hook with the emulator build's own code, core/bloodborne_re.cpp, compiled
//      into this DLL with BBSC_MOD; its emulator calls are answered here and in bbsc_hooks.cpp
//      (hook trampolines) and bbsc_relay.cpp (BBSC's packets, through the game server);
//   4. keep watching: a patch site the emulator wrote back is written again.
// Session suspensions and the lamp role toggles come from bloodborne_re.cpp as in the emulator
// build (SetPersistentSessionPatches / SetHostLampPatches below).
//
// Log: logs\bbsc-mod-<date>-<time>-<pid>.log next to the DLL (one per game window, so two
// windows on one PC do not overwrite each other); capture files in captures\ next to it.

#include "bbsc_platform.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <vector>
#include <string>

#include "common/logging/log.h"
#include "common/memory_patcher.h"
#include "common/path_util.h"
#include "core/bbsc_cinematic.h"
#include "core/bbsc_patch_tables.h"
#include "core/bloodborne_re.h"
#include "core/debugger.h"
#include "core/libraries/np/np_matching2/np_matching2_mm.h"
#include "core/memory.h"

using namespace Core::Bbsc::PatchTables;

// Public version (2026-10-11): 0.MINOR.PATCH while testing - PATCH for bug fixes, MINOR for new
// features, 1.0.0 the finished release. Keep it equal to "version" in mod.json.
#define BBSC_VERSION "0.0.1"

namespace BbscMod {
void LogPlain(const char* text);
u64 EbootAddress();
void StartRelay();
void StartP2PChannel(); // bbsc_channel.cpp
bool GameServerIsPublic(std::string* host_out); // bbsc_relay.cpp
void InstallHttpWrappers();
void StartDiscordStatus();
} // namespace BbscMod
namespace Core::Bbsc {
void InstallPadWrapper();
} // namespace Core::Bbsc

namespace {

constexpr u64 ForkEbootBase = 0x800000000ULL; // where the shadp2p fork puts it
u64 EbootBase = 0; // found by FindEbootBase before anything else uses it
constexpr u64 EbootSize = 0x056D4000ULL; // CUSA03173 01.09 image size
constexpr u64 WaitForImageMs = 180'000;
constexpr u32 SettleMs = 20;            // let the emulator finish its own load-time patching
constexpr u32 WatchMs = 500;

std::filesystem::path mod_dir;
FILE* log_file = nullptr;
std::mutex log_mutex;

void OpenLog() {
    mod_dir = BbscMod::Platform::ModulePath().parent_path();
    const auto now = BbscMod::Platform::Now();
    const std::string name =
        fmt::format("bbsc-mod-{:04}{:02}{:02}-{:02}{:02}{:02}-{}.log", now.year, now.month, now.day,
                    now.hour, now.minute, now.second, BbscMod::Platform::ProcessId());
    std::error_code ignored;
    std::filesystem::create_directories(mod_dir / "logs", ignored);
    log_file = BbscMod::Platform::OpenFile(mod_dir / "logs" / name, "w");
}

// ---- Byte patches --------------------------------------------------------------------------------

// VirtualQuery is a kernel call that shares the address-space lock with the emulator's constant
// page-protection changes (GPU memory tracking). bloodborne_re.cpp checks every memory read, so
// thousands of these per frame took the game to ~1 fps (2026-09-30, two testers). The emulator
// build answers the same checks from its own memory map; here each game thread remembers the last
// few regions it asked about for a moment.
struct CachedRegion {
    u64 start = 0;
    u64 end = 0;
    bool committed = false;
    u32 prot = 0;
    u64 stamp_ms = 0;
};
constexpr u64 RegionCacheMs = 250;
constexpr size_t RegionCacheSize = 16;

bool QueryRegion(u64 address, CachedRegion& out) {
    thread_local std::array<CachedRegion, RegionCacheSize> cache{};
    thread_local size_t next = 0;
    const u64 now = BbscMod::Platform::NowMs();
    for (const CachedRegion& region : cache) {
        if (address >= region.start && address < region.end &&
            now - region.stamp_ms <= RegionCacheMs) {
            out = region;
            return true;
        }
    }
    BbscMod::Platform::Region info;
    if (!BbscMod::Platform::QueryRegion(address, info)) {
        return false;
    }
    CachedRegion& slot = cache[next];
    next = (next + 1) % RegionCacheSize;
    slot.start = info.start;
    slot.end = info.end;
    slot.committed = info.committed;
    slot.prot = info.prot;
    slot.stamp_ms = now;
    out = slot;
    return true;
}

bool Readable(u64 address, size_t size) {
    u64 at = address;
    const u64 end = address + size;
    while (at < end) {
        CachedRegion region;
        if (!QueryRegion(at, region) || !region.committed || region.prot == 0) {
            return false;
        }
        at = region.end;
    }
    return true;
}

bool MatchesAt(u64 base, u64 offset, const u8* bytes, size_t size) {
    const u64 address = base + offset;
    return Readable(address, size) &&
           std::memcmp(reinterpret_cast<const void*>(address), bytes, size) == 0;
}

bool Matches(u64 offset, const u8* bytes, size_t size) {
    const u64 address = EbootBase + offset;
    return Readable(address, size) &&
           std::memcmp(reinterpret_cast<const void*>(address), bytes, size) == 0;
}

void Write(u64 offset, const u8* bytes, size_t size) {
    BbscMod::Platform::WriteCode(EbootBase + offset, bytes, size);
}

struct Site {
    const char* name;
    u64 offset;
    std::array<u8, 6> original{};
    std::array<u8, 6> patched{};
    size_t size;
    Group group;
};

template <size_t N>
std::array<u8, 6> Pad(const std::array<u8, N>& bytes) {
    std::array<u8, 6> out{};
    std::memcpy(out.data(), bytes.data(), N);
    return out;
}

constexpr size_t SiteCount =
    Retargets.size() + EntryReturns.size() + ByteEdits.size() + WideEdits.size() + EntryJumps.size();
std::array<Site, SiteCount> sites{};
std::mutex patch_mutex;
bool patches_ready = false;
bool session_on = true; // session groups: off while a suspension runs
bool role_on = false;   // role groups (lamp): on while hosting

void BuildSites() {
    size_t i = 0;
    for (const auto& e : Retargets) {
        sites[i++] = {e.name, e.site, Pad(EncodeRel32(e.opcode, e.site, e.original)),
                      Pad(EncodeRel32(e.opcode, e.site, e.target)), 5, e.group};
    }
    for (const auto& e : EntryReturns) {
        std::array<u8, 6> ret{};
        ret[0] = 0xC3;
        sites[i++] = {e.name, e.site, e.prologue, ret, 1, e.group};
    }
    for (const auto& e : ByteEdits) {
        sites[i++] = {e.name, e.site, Pad(e.original), Pad(e.replacement), 2, e.group};
    }
    for (const auto& e : WideEdits) {
        sites[i++] = {e.name, e.site, Pad(e.original), Pad(e.replacement), 4, e.group};
    }
    for (const auto& e : EntryJumps) {
        sites[i++] = {e.name, e.site, Pad(e.prologue), Pad(EncodeRel32(0xE9, e.site, e.target)), 5,
                      e.group};
    }
}

// The game image: where the fork puts it, else the start of any executable region whose first few
// patch sites read as Bloodborne's (original or patched). AllSitesKnown then checks every site.
u64 FindEbootBase() {
    const auto fits = [](u64 base) {
        int checked = 0;
        for (const Site& s : sites) {
            if (s.offset + s.size > EbootSize) {
                continue;
            }
            if (!MatchesAt(base, s.offset, s.original.data(), s.size) &&
                !MatchesAt(base, s.offset, s.patched.data(), s.size)) {
                return false;
            }
            if (++checked == 4) {
                return true;
            }
        }
        return checked != 0;
    };
    if (fits(ForkEbootBase)) {
        return ForkEbootBase;
    }
    BbscMod::Platform::Region info;
    u64 at = 0x10000;
    while (at < 0x7FFF'FFFF'0000ULL && BbscMod::Platform::QueryRegion(at, info)) {
        if (info.committed && (info.prot & BbscMod::Platform::ProtExec) != 0 && fits(info.start)) {
            return info.start;
        }
        at = info.end;
    }
    return 0;
}

bool AllSitesKnown(const Site** first_bad) {
    for (const Site& s : sites) {
        if (!GroupEnabled(s.group)) {
            continue;
        }
        if (s.offset + s.size > EbootSize ||
            (!Matches(s.offset, s.original.data(), s.size) &&
             !Matches(s.offset, s.patched.data(), s.size))) {
            *first_bad = &s;
            return false;
        }
    }
    return true;
}

// Whether a site should be patched right now.
bool Wanted(const Site& s) {
    if (!GroupEnabled(s.group)) {
        return false;
    }
    if (IsRoleGroup(s.group)) {
        return role_on;
    }
    if (IsSessionGroup(s.group)) {
        return session_on;
    }
    return true;
}

// Brings every enabled site to its wanted form. Returns how many were written.
int SyncSites() {
    int count = 0;
    for (const Site& s : sites) {
        if (!GroupEnabled(s.group)) {
            continue;
        }
        const auto& want = Wanted(s) ? s.patched : s.original;
        if (!Matches(s.offset, want.data(), s.size)) {
            Write(s.offset, want.data(), s.size);
            ++count;
        }
    }
    return count;
}

// ---- Start-up ------------------------------------------------------------------------------------

// LICENSE.txt (the GPL) and NOTICE.txt (copyright + source link) ship in the mod folder so a copy
// passed on keeps them. Missing or emptied -> BBSC stays off and says why.
bool NoticesPresent() {
    auto holds = [](const char* name, const char* text) {
        FILE* file = BbscMod::Platform::OpenFile(mod_dir / name, "rb");
        if (file == nullptr) {
            return false;
        }
        std::string content(64 * 1024, '\0');
        content.resize(fread(content.data(), 1, content.size(), file));
        fclose(file);
        return content.find(text) != std::string::npos;
    };
    // SUPPORTERS.txt (2026-10-11, the user): the supporters list travels with BBSC like the
    // license; its names change freely, only the file and its header are required.
    return holds("LICENSE.txt", "GNU GENERAL PUBLIC LICENSE") &&
           holds("NOTICE.txt", "github.com/Classmg22/BBSC") &&
           holds("SUPPORTERS.txt", "BBSC SUPPORTERS");
}

void Worker() {
    OpenLog();
    BbscMod::LogPlain("BBSC mod loaded - version " BBSC_VERSION);
    if (!NoticesPresent()) {
        LOG_ERROR(Mod, "LICENSE.txt / NOTICE.txt / SUPPORTERS.txt missing or changed in {} - BBSC "
                       "is off",
                  Common::FS::PathToUTF8String(mod_dir));
#ifdef _WIN32
        MessageBoxW(nullptr,
                    L"LICENSE.txt, NOTICE.txt or SUPPORTERS.txt is missing from the BBSC mod "
                    L"folder (or was changed), so BBSC is off.\n\nThese files have to travel with "
                    L"BBSC.\nDownload the full package again "
                    L"from:\nhttps://github.com/Classmg22/BBSC",
                    L"BBSC", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
#else
        std::fprintf(stderr, "BBSC: LICENSE.txt, NOTICE.txt or SUPPORTERS.txt is missing from %s "
                             "(or was changed), so BBSC is off\n",
                     mod_dir.c_str());
#endif
        return;
    }
    LOG_INFO(Mod, "BBSC mod in pid {} ({})", BbscMod::Platform::ProcessId(),
             Common::FS::PathToUTF8String(BbscMod::Platform::ProgramPath()));
    // Public servers (2026-10-11, shadNet's author: The Hunter's Dream is exact to the official
    // server, custom summon behaviour does not work there). Wozzardman's seamless summon patches
    // (cross-map sign search, host-placement summon build) and the 5-hunter search patches change
    // what the game asks the summon server for, so they are off there whatever the switches say
    // (review 2026-10-11: the Mods tab writes its defaults, BBSC_MOD_SEAMLESS=1 among them, at
    // every start, so "a switch the player set wins" would have kept them on).
    {
        std::string server;
        if (BbscMod::GameServerIsPublic(&server)) {
            const auto force_off = [](const char* name) { BbscMod::Platform::SetEnv(name, "0"); };
            force_off("BBSC_MOD_SEAMLESS");
            force_off("SHADPS4_BBSC_FIVE_HUNTERS");
            LOG_INFO(Mod, "Public game server ({}): vanilla summons - seamless summon patches and the "
                          "5-hunter search off (the session itself stays seamless)",
                     server);
        }
    }
    BuildSites();

    const u64 started = BbscMod::Platform::NowMs();
    while ((EbootBase = FindEbootBase()) == 0) {
        if (BbscMod::Platform::NowMs() - started > WaitForImageMs) {
            LOG_ERROR(Mod, "No Bloodborne CUSA03173 01.09 image in memory after {} s - not this "
                           "game or not this version; nothing was changed",
                      WaitForImageMs / 1000);
            return;
        }
        BbscMod::Platform::SleepMs(50);
    }
    LOG_INFO(Mod, "Game image found at {:#x}{}", EbootBase,
             EbootBase == ForkEbootBase ? "" : " (not the shadp2p fork's address)");
    // shadPS4 maps the game's code writable; other hosts (bbhost) keep it read-only, and the
    // shared hook code writes some code bytes directly. Make the image's code writable like shadPS4.
    {
        int unlocked = 0;
        BbscMod::Platform::Region info;
        u64 at = EbootBase;
        while (at < EbootBase + EbootSize && BbscMod::Platform::QueryRegion(at, info)) {
            const u64 next = info.end;
            if (info.committed && (info.prot & BbscMod::Platform::ProtExec) != 0 &&
                (info.prot & BbscMod::Platform::ProtWrite) == 0) {
                const u64 end = std::min<u64>(next, EbootBase + EbootSize);
                if (BbscMod::Platform::MakeWritableCode(info.start, end - info.start)) {
                    ++unlocked;
                }
            }
            at = next;
        }
        if (unlocked != 0) {
            LOG_INFO(Mod, "Game code made writable ({} region(s)) - this host maps it read-only",
                     unlocked);
        }
    }
    const Site* bad = nullptr;
    while (!AllSitesKnown(&bad)) {
        if (BbscMod::Platform::NowMs() - started > WaitForImageMs) {
            LOG_ERROR(Mod,
                      "No Bloodborne CUSA03173 01.09 image after {} s (first unknown site: {} at "
                      "{:#x}) - not this game or not this version; nothing was changed",
                      WaitForImageMs / 1000, bad ? bad->name : "?", bad ? bad->offset : 0);
            return;
        }
        BbscMod::Platform::SleepMs(1);
    }
    BbscMod::Platform::SleepMs(SettleMs);
    const u64 verified = BbscMod::Platform::NowMs() - started;

    MemoryPatcher::g_eboot_address = EbootBase;
    MemoryPatcher::g_eboot_image_size = EbootSize;
    MemoryPatcher::g_game_serial = "CUSA03173";

    // Wozzardman's seamless patches first: one of them (0x019471B1) is also in BBSC's list, and it
    // checks for the original bytes.
    Core::Bloodborne::InstallSeamlessCoopPatches();

    int written = 0;
    {
        std::scoped_lock lock{patch_mutex};
        patches_ready = true;
        written = SyncSites();
    }
    LOG_INFO(Mod, "Image verified after {} ms: {} byte patch sites written", verified, written);

    Core::Bloodborne::InstallReverseEngineeringTrace();
    BbscMod::StartRelay();
    BbscMod::StartP2PChannel();
    BbscMod::InstallHttpWrappers();
    Core::Bbsc::InstallPadWrapper();
    BbscMod::StartDiscordStatus();
    LOG_INFO(Mod, "BBSC mod ready after {} ms", BbscMod::Platform::NowMs() - started);

    for (;;) {
        BbscMod::Platform::SleepMs(WatchMs);
        std::scoped_lock lock{patch_mutex};
        if (const int redone = SyncSites(); redone != 0) {
            LOG_WARNING(Mod, "{} patch site(s) had changed back - written again", redone);
        }
    }
}

} // namespace

// ---- Logging -------------------------------------------------------------------------------------

namespace BbscMod {

void LogLine(char level, const char* category, const std::string& text) {
    std::scoped_lock lock{log_mutex};
    if (log_file == nullptr) {
        return;
    }
    const auto now = BbscMod::Platform::Now();
    std::fprintf(log_file, "[%02d:%02d:%02d.%03d] %c %s: %s\n", now.hour, now.minute, now.second,
                 now.millisecond, level, category, text.c_str());
    std::fflush(log_file);
}

void LogPlain(const char* text) {
    LogLine('I', "Mod", text);
}

u64 EbootAddress() {
    return EbootBase;
}

// Import slots the DLL swapped for its wrappers (bbsc_http.cpp, bbsc_channel.cpp). The official
// shadPS4 links the eboot's imports again when the game loads a library later (2026-10-10: no
// wrapper ever ran on the nightly - the slots were written back to the emulator's functions), so a
// thread puts the wrapper back whenever a slot no longer points at it, taking the new value as the
// original.
namespace {
struct GuardedSlot {
    u64 slot_offset;
    u64 wrapper;
    void** original; // the wrapper's function pointer (an aligned 8-byte store)
    const char* name;
    int rewritten = 0;
};
std::mutex guarded_mutex;
std::vector<GuardedSlot> guarded_slots;

void SlotGuardThread() {
    for (;;) {
        BbscMod::Platform::SleepMs(5);
        std::scoped_lock lock{guarded_mutex};
        for (auto& guarded : guarded_slots) {
            const u64 slot = EbootBase + guarded.slot_offset;
            const u64 value = BbscMod::Platform::ReadSlot(slot);
            if (value == guarded.wrapper || value == 0 ||
                (value >= EbootBase && value < EbootBase + EbootSize)) {
                continue;
            }
            *reinterpret_cast<void* volatile*>(guarded.original) = reinterpret_cast<void*>(value);
            BbscMod::Platform::WriteSlot(slot, guarded.wrapper);
            if (++guarded.rewritten <= 3) {
                LOG_INFO(Mod, "Import {} was linked again by the emulator - wrapper put back ({}x)",
                         guarded.name, guarded.rewritten);
            }
        }
    }
}
} // namespace

void GuardSlot(u64 slot_offset, u64 wrapper, void** original, const char* name) {
    std::scoped_lock lock{guarded_mutex};
    if (guarded_slots.empty()) {
        BbscMod::Platform::StartThread(SlotGuardThread);
    }
    guarded_slots.push_back({slot_offset, wrapper, original, name});
}

} // namespace BbscMod

// ---- What bloodborne_re.cpp asks the emulator for ----------------------------------------------

namespace Core {

bool MemoryManager::IsValidMapping(u64 address, u64 size) {
    return Readable(address, size == 0 ? 1 : size);
}

s32 MemoryManager::QueryProtection(u64 address, void** start, void** end, u32* prot) {
    CachedRegion info;
    if (!QueryRegion(address, info) || !info.committed) {
        return -1;
    }
    *start = reinterpret_cast<void*>(info.start);
    *end = reinterpret_cast<void*>(info.end);
    *prot = info.prot; // the same bits as MemoryProt
    return 0;
}

namespace Debugger {
int GetCurrentPid() {
    return static_cast<int>(BbscMod::Platform::ProcessId());
}
} // namespace Debugger

namespace Bbsc {

void InstallPersistentSessionPatches() {} // the DLL applies them itself (Worker)

void SetPersistentSessionPatches(bool enabled) {
    std::scoped_lock lock{patch_mutex};
    if (!patches_ready || session_on == enabled) {
        return;
    }
    session_on = enabled;
    const int count = SyncSites();
    LOG_INFO(Debug, "BBSC persistence {} ({} session sites; echoes, lamp, fog walls and deaths "
                    "untouched)",
             enabled ? "re-applied" : "suspended", count);
}

void SetHostLampPatches(bool hosting) {
    std::scoped_lock lock{patch_mutex};
    if (!patches_ready || role_on == hosting) {
        return;
    }
    role_on = hosting;
    const int count = SyncSites();
    LOG_INFO(Debug, "BBSC lamp: {} ({} sites)",
             hosting ? "prompt allowed (hosting a session)"
                     : "vanilla gates (solo or guest - a guest never gets the lamp)",
             count);
}

// The F1 camera. The emulator build feeds it from its SDL window events (bbsc_cinematic.cpp); a mod
// sees no window events, so the keys are polled here instead, only while this process's window is
// in front (Nucleus runs two). While it is on, CineWindowProc keeps the keys and the mouse from the
// game (2026-10-12; before, they reached it too). bloodborne_re.cpp turns it on by itself when you
// die in co-op and off when you respawn.
// 2026-10-05 (the user: better mouse controls): the mouse looks around without a button held - the
// cursor is hidden and kept in the middle of the game window - and the wheel sets the speed (the
// game window is subclassed for WM_MOUSEWHEEL / WM_SETCURSOR while the camera is on; PageUp /
// PageDown still work).
// Linux (BBport): no keyboard or mouse camera (they would reach the game too); the controller
// works.
std::mutex cine_mutex;
CinematicInput cine_state;
#ifdef _WIN32
std::array<bool, 256> cine_was_down{};
bool cine_looking = false;
POINT cine_anchor{};
HWND cine_window = nullptr;          // the subclassed game window (SDL's "SDL_app")
WNDPROC cine_window_proc = nullptr;  // its own window procedure
std::atomic<bool> cine_mouse_owned{false}; // the camera is on and has the mouse
std::atomic<int> cine_wheel{0};      // wheel notches since the last TakeCinematicInput

LRESULT CALLBACK CineWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (cine_mouse_owned) {
        if (message == WM_MOUSEWHEEL) {
            const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
            cine_wheel += delta > 0 ? 1 : (delta < 0 ? -1 : 0);
            return 0; // not for the game
        }
        if (message == WM_SETCURSOR && LOWORD(lparam) == HTCLIENT) {
            SetCursor(nullptr);
            return TRUE;
        }
        // 2026-10-12 (the user: "each button does a million things"): the camera's keys walked,
        // dodged and used items at the same time, and the mouse turned the game's camera. While
        // the camera is on, presses and movement stop here; the emulator (SDL) never sees them.
        // Releases still go through, so a key held when the camera came on is not left stuck.
        // The camera reads the keys itself (GetAsyncKeyState), so it is not affected. Alt+F4 passes.
        switch (message) {
        case WM_KEYDOWN:
        case WM_CHAR:
        case WM_DEADCHAR:
        case WM_SYSCHAR:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONDBLCLK:
        case WM_MOUSEMOVE:
            return 0;
        case WM_SYSKEYDOWN:
            if (wparam != VK_F4) {
                return 0;
            }
            break;
        case WM_INPUT: {
            // Raw input (SDL's relative mouse): drop movement and presses, keep releases.
            RAWINPUT raw{};
            UINT size = sizeof(raw);
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, &raw, &size,
                                sizeof(RAWINPUTHEADER)) != static_cast<UINT>(-1)) {
                bool release = false;
                if (raw.header.dwType == RIM_TYPEKEYBOARD) {
                    release = (raw.data.keyboard.Flags & RI_KEY_BREAK) != 0;
                } else if (raw.header.dwType == RIM_TYPEMOUSE) {
                    constexpr USHORT Ups = RI_MOUSE_BUTTON_1_UP | RI_MOUSE_BUTTON_2_UP |
                                           RI_MOUSE_BUTTON_3_UP | RI_MOUSE_BUTTON_4_UP |
                                           RI_MOUSE_BUTTON_5_UP;
                    release = (raw.data.mouse.usButtonFlags & Ups) != 0;
                }
                if (!release) {
                    return DefWindowProcW(window, message, wparam, lparam);
                }
            }
            break;
        }
        default:
            break;
        }
    }
    return CallWindowProcW(cine_window_proc, window, message, wparam, lparam);
}

// The game window when it is in front, else nullptr. Subclassed the first time it is seen.
HWND CineGameWindowInFront() {
    HWND window = GetForegroundWindow();
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (window == nullptr || pid != GetCurrentProcessId()) {
        return nullptr;
    }
    char cls[32]{};
    GetClassNameA(window, cls, sizeof(cls));
    if (std::strcmp(cls, "SDL_app") != 0) {
        return nullptr; // e.g. the console
    }
    if (cine_window != window) {
        if (cine_window != nullptr && cine_window_proc != nullptr) {
            SetWindowLongPtrW(cine_window, GWLP_WNDPROC,
                              reinterpret_cast<LONG_PTR>(cine_window_proc));
        }
        cine_window = window;
        cine_window_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
            window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&CineWindowProc)));
        LOG_INFO(Debug, "BBSC cinematic camera: mouse wheel and cursor hooked on the game window");
    }
    return window;
}
#endif // _WIN32

// The controller: the game's scePadReadState import slot (0x053E4EA0, tools/gamedata/
// eboot_imports.py) is swapped for PadReadState below, which sees the pad data the emulator hands
// the game - any controller the emulator supports. L3 + R3 together toggles the camera; while it is
// on the sticks and buttons fly it and the game gets a pad at rest (the hunter does not move).
constexpr u64 PadReadStateSlot = 0x053E4EA0;
using PadReadStateFn = int PS4_SYSV_ABI (*)(s32 handle, u8* data);
PadReadStateFn original_pad_read = nullptr;
constexpr u32 PadL3 = 0x2, PadR3 = 0x4, PadUp = 0x10, PadRight = 0x20, PadDown = 0x40,
              PadLeft = 0x80, PadL1 = 0x400, PadR1 = 0x800, PadTriangle = 0x1000,
              PadCross = 0x4000, PadSquare = 0x8000;
struct CinePad {
    u32 buttons = 0;
    u32 last_buttons = 0;
    float lx = 0.0f, ly = 0.0f, rx = 0.0f, ry = 0.0f, l2 = 0.0f, r2 = 0.0f;
    int speed_steps = 0, smooth_steps = 0;
    bool zoom_reset = false;
};
CinePad cine_pad;

float PadAxis(u8 value) {
    const float v = (static_cast<float>(value) - 128.0f) / 127.0f;
    constexpr float Dead = 0.15f;
    return std::abs(v) < Dead ? 0.0f : std::clamp((v - std::copysign(Dead, v)) / (1.0f - Dead), -1.0f, 1.0f);
}

// OrbisPadData: +0 buttons (u32), +4 left stick x/y, +6 right stick x/y, +8 L2, +9 R2 (u8).
int PS4_SYSV_ABI PadReadState(s32 handle, u8* data) {
    const int result = original_pad_read(handle, data);
    if (result != 0 || data == nullptr) {
        return result;
    }
    u32 buttons = 0;
    std::memcpy(&buttons, data, 4);
    std::scoped_lock lock{cine_mutex};
    const u32 pressed = buttons & ~cine_pad.last_buttons;
    cine_pad.last_buttons = buttons;
    if ((buttons & (PadL3 | PadR3)) == (PadL3 | PadR3) && (pressed & (PadL3 | PadR3)) != 0) {
        const bool enabled = !cine_state.enabled;
        cine_state = CinematicInput{};
        cine_state.enabled = enabled;
        LOG_INFO(Debug, "BBSC cinematic camera: {} (L3 + R3)", enabled ? "on" : "off");
    }
    if (!cine_state.enabled) {
        cine_pad = CinePad{.last_buttons = buttons};
        return result;
    }
    cine_pad.buttons = buttons;
    cine_pad.lx = PadAxis(data[4]);
    cine_pad.ly = PadAxis(data[5]);
    cine_pad.rx = PadAxis(data[6]);
    cine_pad.ry = PadAxis(data[7]);
    cine_pad.l2 = static_cast<float>(data[8]) / 255.0f;
    cine_pad.r2 = static_cast<float>(data[9]) / 255.0f;
    cine_pad.speed_steps += ((pressed & PadUp) ? 1 : 0) - ((pressed & PadDown) ? 1 : 0);
    cine_pad.smooth_steps += ((pressed & PadRight) ? 1 : 0) - ((pressed & PadLeft) ? 1 : 0);
    cine_pad.zoom_reset = cine_pad.zoom_reset || (pressed & PadTriangle) != 0;
    // The game sees a controller at rest (connected, nothing pressed).
    const u32 rest_buttons = buttons & 0x80000000u;
    std::memcpy(data, &rest_buttons, 4);
    data[4] = data[5] = data[6] = data[7] = 128;
    data[8] = data[9] = 0;
    return result;
}

void InstallPadWrapper() {
    const u64 slot = EbootBase + PadReadStateSlot;
    for (int i = 0; i < 60'000; ++i) {
        const u64 value = BbscMod::Platform::ReadSlot(slot);
        if (value != 0 && (value < EbootBase || value >= EbootBase + EbootSize)) {
            original_pad_read = reinterpret_cast<PadReadStateFn>(value);
            BbscMod::Platform::WriteSlot(slot, reinterpret_cast<u64>(&PadReadState));
            LOG_INFO(Mod, "Pad: controller works the F1 camera too (L3 + R3)");
            return;
        }
        BbscMod::Platform::SleepMs(1);
    }
    LOG_WARNING(Mod, "Pad: scePadReadState slot never filled - F1 camera keyboard only");
}

#ifdef _WIN32
bool CineKeyPressed(int key, bool down) {
    const bool pressed = down && !cine_was_down[key];
    cine_was_down[key] = down;
    return pressed;
}

#endif

CinematicInput TakeCinematicInput() {
    std::scoped_lock lock{cine_mutex};
#ifdef _WIN32
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    const bool front = pid == GetCurrentProcessId();
    auto down = [front](int key) { return front && (GetAsyncKeyState(key) & 0x8000) != 0; };
    if (CineKeyPressed(VK_F1, down(VK_F1))) {
        const bool enabled = !cine_state.enabled;
        cine_state = CinematicInput{};
        cine_state.enabled = enabled;
        LOG_INFO(Debug, "BBSC cinematic camera: {} (F1)", enabled ? "on" : "off");
    }
#endif
    if (!cine_state.enabled) {
#ifdef _WIN32
        cine_looking = false;
        cine_mouse_owned = false;
        cine_wheel = 0;
#endif
        return {};
    }
    CinematicInput& c = cine_state;
    const CinePad& p = cine_pad;
    bool key_fast = false;
    bool key_slow = false;
    bool key_zoom_reset = false;
    int key_wheel = 0;
    int key_smooth_steps = 0;
#ifdef _WIN32
    c.forward = down('W');
    c.back = down('S');
    c.left = down('A');
    c.right = down('D');
    c.up = down('E') || down(VK_SPACE);
    c.down = down('Q') || down('C');
    key_fast = down(VK_SHIFT);
    key_slow = down(VK_MENU);
    c.zoom_in = down('Z');
    c.zoom_out = down('X');
    key_zoom_reset = CineKeyPressed('V', down('V'));
    c.roll_left = down(VK_OEM_COMMA);
    c.roll_right = down(VK_OEM_PERIOD);
    c.roll_reset = CineKeyPressed(VK_OEM_2, down(VK_OEM_2));
    c.look_up = down(VK_UP);
    c.look_down = down(VK_DOWN);
    c.look_left = down(VK_LEFT);
    c.look_right = down(VK_RIGHT);
    key_wheel = (CineKeyPressed(VK_PRIOR, down(VK_PRIOR)) ? 1 : 0) -
                (CineKeyPressed(VK_NEXT, down(VK_NEXT)) ? 1 : 0) + cine_wheel.exchange(0);
    key_smooth_steps = (CineKeyPressed(VK_OEM_6, down(VK_OEM_6)) ? 1 : 0) -
                       (CineKeyPressed(VK_OEM_4, down(VK_OEM_4)) ? 1 : 0);
#endif
    c.fast = key_fast || (p.buttons & PadCross) != 0;
    c.slow = key_slow || (p.buttons & PadSquare) != 0;
    c.zoom_reset = key_zoom_reset || p.zoom_reset;
    cine_pad.zoom_reset = false;
    c.wheel = key_wheel + p.speed_steps;
    c.smooth_steps = key_smooth_steps + p.smooth_steps;
    cine_pad.speed_steps = 0;
    cine_pad.smooth_steps = 0;
    c.pad_move_x = p.lx;
    c.pad_move_y = -p.ly; // stick up = smaller y = forward
    c.pad_rise = ((p.buttons & PadR1) ? 1.0f : 0.0f) - ((p.buttons & PadL1) ? 1.0f : 0.0f);
    c.pad_look_x = p.rx;
    c.pad_look_y = -p.ry;
    c.pad_zoom = p.r2 - p.l2;
#ifdef _WIN32
    // Mouse look, no button needed: the cursor's movement, put back in the middle of the game
    // window each frame (hidden there by CineWindowProc).
    c.mouse_dx = 0.0f;
    c.mouse_dy = 0.0f;
    POINT now{};
    RECT client{};
    HWND window = front ? CineGameWindowInFront() : nullptr;
    if (window != nullptr && GetClientRect(window, &client) && GetCursorPos(&now)) {
        POINT center{(client.left + client.right) / 2, (client.top + client.bottom) / 2};
        ClientToScreen(window, &center);
        if (cine_looking) {
            c.mouse_dx = static_cast<float>(now.x - cine_anchor.x);
            c.mouse_dy = static_cast<float>(now.y - cine_anchor.y);
        }
        cine_anchor = center;
        cine_looking = true;
        SetCursorPos(center.x, center.y);
        cine_mouse_owned = true;
    } else {
        cine_looking = false; // another window is in front: the mouse is the user's
        cine_mouse_owned = false;
    }
#endif
    return c;
}

void CinematicForceOff() {
    std::scoped_lock lock{cine_mutex};
    cine_state = CinematicInput{};
    cine_pad = CinePad{.last_buttons = cine_pad.last_buttons};
#ifdef _WIN32
    cine_looking = false;
    cine_mouse_owned = false;
#endif
}

void CinematicForceOn() {
    std::scoped_lock lock{cine_mutex};
    if (cine_state.enabled) {
        return;
    }
    cine_state = CinematicInput{};
    cine_state.enabled = true;
    cine_pad = CinePad{.last_buttons = cine_pad.last_buttons};
#ifdef _WIN32
    cine_looking = false;
#endif
}

} // namespace Bbsc
} // namespace Core

namespace Common::FS {
std::filesystem::path GetUserPath(PathType type) {
    return type == PathType::CapturesDir ? mod_dir / "captures" : mod_dir;
}
} // namespace Common::FS

namespace Libraries::Np::NpMatching2 {
int BbscReplayRoomMembers(bool) {
    return 0; // the emulator's room cache is out of reach for a mod
}
} // namespace Libraries::Np::NpMatching2

#ifdef _WIN32
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        BbscMod::Platform::StartThread(Worker);
    }
    return TRUE;
}
#else
// LD_PRELOAD: runs before bb-probe's main, which then loads the game while Worker waits for it.
__attribute__((constructor)) static void BbscModLoad() {
    BbscMod::Platform::ForgetPreload();
    BbscMod::Platform::StartThread(Worker);
}
#endif
