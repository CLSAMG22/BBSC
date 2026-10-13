// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

// BBSC mod: Core::InstallGuestCodeHook for the DLL. The same trampoline as the emulator's
// (core/cpu_patches.cpp): save every register and YMM into a GuestRegisterSnapshot, call the C++
// hook with it, restore the (possibly changed) snapshot, run the copied instructions, jump back.
//
// Where the trampolines live: the emulator maps an 8 MB read/write/execute area right after every
// module's image for its own patches (core/module.cpp, TrampolineSize) and fills it from the
// start. The DLL takes the last 1 MB of the eboot's area - inside the 2 GB reach of a 5-byte jmp
// from any game instruction, and checked to be mapped, executable and still all zero before use.
// The image is wherever bbsc_mod.cpp found it (EbootAddress). If that area is not there or not
// free (another emulator build), the DLL maps its own 1 MB within reach after (or before) the image.
//
// Not done here (the emulator checks them with its instruction decoder): that the copied bytes are
// whole instructions without RIP-relative operands. Every site BBSC hooks is a fixed, byte-checked
// instruction sequence that the emulator build has already installed the same way.

#include <windows.h>

#include <cstddef>
#include <cstring>
#include <mutex>

#include <xbyak/xbyak.h>

#include "core/cpu_patches.h"

namespace BbscMod {
void LogPlain(const char* text);
u64 EbootAddress();
}

namespace Core {
namespace {

using namespace Xbyak::util;

constexpr u64 EbootAlignedSize = 0x056D4000ULL;
constexpr u64 EmulatorTrampolineSize = 8ULL << 20;
constexpr u64 OurSize = 1ULL << 20;
constexpr u64 JumpReach = 0x7000'0000ULL; // inside a rel32 jump's 2 GB from anywhere in the image

std::mutex hook_mutex;
Xbyak::CodeGenerator* generator = nullptr;
bool area_checked = false;
bool area_ok = false;

// The last 1 MB of the emulator's trampoline area after the image, when it is mapped RWX and unused.
bool EmulatorAreaFree(u64 start) {
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(reinterpret_cast<void*>(start), &info, sizeof(info)) == 0 ||
        info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0 ||
        reinterpret_cast<u64>(info.BaseAddress) + info.RegionSize < start + OurSize) {
        return false;
    }
    const auto* bytes = reinterpret_cast<const u8*>(start);
    for (u64 i = 0; i < OurSize; ++i) {
        if (bytes[i] != 0) {
            return false;
        }
    }
    return true;
}

// Our own 1 MB, 64 KB-aligned, within jump reach of the whole image.
u64 MapOwnArea(u64 image) {
    const u64 image_end = image + EbootAlignedSize + EmulatorTrampolineSize;
    for (u64 at = (image_end + 0xFFFF) & ~0xFFFFULL; at < image + JumpReach; at += 0x10000) {
        if (void* got = VirtualAlloc(reinterpret_cast<void*>(at), OurSize, MEM_RESERVE | MEM_COMMIT,
                                     PAGE_EXECUTE_READWRITE)) {
            return reinterpret_cast<u64>(got);
        }
    }
    const u64 low = image > JumpReach - EbootAlignedSize ? image + EbootAlignedSize - JumpReach
                                                         : 0x10000;
    for (u64 at = (image - OurSize) & ~0xFFFFULL; at >= low && at < image; at -= 0x10000) {
        if (void* got = VirtualAlloc(reinterpret_cast<void*>(at), OurSize, MEM_RESERVE | MEM_COMMIT,
                                     PAGE_EXECUTE_READWRITE)) {
            return reinterpret_cast<u64>(got);
        }
    }
    return 0;
}

bool CheckArea() {
    if (area_checked) {
        return area_ok;
    }
    area_checked = true;
    const u64 image = BbscMod::EbootAddress();
    if (image == 0) {
        BbscMod::LogPlain("Hook area: the game image was not found - no hooks");
        return false;
    }
    u64 start = image + EbootAlignedSize + EmulatorTrampolineSize - OurSize;
    if (!EmulatorAreaFree(start)) {
        start = MapOwnArea(image);
        if (start == 0) {
            BbscMod::LogPlain("Hook area: no free memory within reach of the game image - no hooks");
            return false;
        }
        BbscMod::LogPlain("Hook area: the emulator's area after the image is not usable - BBSC "
                          "mapped its own");
    }
    generator = new Xbyak::CodeGenerator(OurSize, reinterpret_cast<void*>(start));
    area_ok = true;
    return true;
}

bool WriteJump(u8* site, const void* target, size_t length) {
    DWORD old = 0;
    if (!VirtualProtect(site, length, PAGE_EXECUTE_READWRITE, &old)) {
        return false;
    }
    std::array<u8, 32> bytes{};
    const s64 rel = reinterpret_cast<s64>(target) - reinterpret_cast<s64>(site + 5);
    bytes[0] = 0xE9;
    const s32 rel32 = static_cast<s32>(rel);
    std::memcpy(bytes.data() + 1, &rel32, 4);
    for (size_t i = 5; i < length; ++i) {
        bytes[i] = 0x90;
    }
    std::memcpy(site, bytes.data(), length);
    DWORD ignored = 0;
    VirtualProtect(site, length, old, &ignored);
    FlushInstructionCache(GetCurrentProcess(), site, length);
    return true;
}

} // namespace

bool InstallGuestCodeHook(void* address, std::span<const u8> expected_instructions, u64 tag,
                          GuestCodeHook hook, const u8* skip_flag) {
    constexpr size_t NearJumpBytes = 5;
    constexpr size_t GuestRedZoneBytes = 128;
    constexpr size_t SnapshotOffset = 0;
    constexpr size_t ScratchR11Offset = sizeof(GuestRegisterSnapshot);
    constexpr size_t ScratchRflagsOffset = ScratchR11Offset + sizeof(u64);
    constexpr size_t FrameReserve =
        ((ScratchRflagsOffset + sizeof(u64) + GuestRedZoneBytes) + 15) & ~size_t{15};

    auto* code = static_cast<u8*>(address);
    if (hook == nullptr || expected_instructions.size() < NearJumpBytes ||
        expected_instructions.size() > 32) {
        return false;
    }
    std::scoped_lock lock{hook_mutex};
    if (!CheckArea()) {
        return false;
    }
    if (std::memcmp(expected_instructions.data(), code, expected_instructions.size()) != 0) {
        return false;
    }

    auto& trampoline = *generator;
    const size_t trampoline_offset = trampoline.getSize();
    const auto* trampoline_start = trampoline.getCurr();
    const auto snapshot = [](size_t field_offset) {
        return qword[rsp + SnapshotOffset + field_offset];
    };

    try {
        trampoline.lea(rsp, ptr[rsp - FrameReserve]);
        trampoline.mov(qword[rsp + ScratchR11Offset], r11);
        trampoline.pushfq();
        trampoline.pop(r11);
        trampoline.mov(qword[rsp + ScratchRflagsOffset], r11);
        trampoline.mov(r11, rsp);
        trampoline.and_(rsp, -16);

        trampoline.mov(snapshot(offsetof(GuestRegisterSnapshot, rax)), rax);
        trampoline.mov(snapshot(offsetof(GuestRegisterSnapshot, rbx)), rbx);
        trampoline.mov(snapshot(offsetof(GuestRegisterSnapshot, rcx)), rcx);
        trampoline.mov(snapshot(offsetof(GuestRegisterSnapshot, rdx)), rdx);
        trampoline.mov(snapshot(offsetof(GuestRegisterSnapshot, rsi)), rsi);
        trampoline.mov(snapshot(offsetof(GuestRegisterSnapshot, rdi)), rdi);
        trampoline.mov(snapshot(offsetof(GuestRegisterSnapshot, rbp)), rbp);
        trampoline.mov(snapshot(offsetof(GuestRegisterSnapshot, r8)), r8);
        trampoline.mov(snapshot(offsetof(GuestRegisterSnapshot, r9)), r9);
        trampoline.mov(snapshot(offsetof(GuestRegisterSnapshot, r10)), r10);
        trampoline.mov(snapshot(offsetof(GuestRegisterSnapshot, r12)), r12);
        trampoline.mov(snapshot(offsetof(GuestRegisterSnapshot, r13)), r13);
        trampoline.mov(snapshot(offsetof(GuestRegisterSnapshot, r14)), r14);
        trampoline.mov(snapshot(offsetof(GuestRegisterSnapshot, r15)), r15);
        trampoline.mov(rax, qword[r11 + ScratchR11Offset]);
        trampoline.mov(snapshot(offsetof(GuestRegisterSnapshot, r11)), rax);
        trampoline.mov(rax, qword[r11 + ScratchRflagsOffset]);
        trampoline.mov(snapshot(offsetof(GuestRegisterSnapshot, rflags)), rax);
        trampoline.lea(rax, ptr[r11 + FrameReserve]);
        trampoline.mov(snapshot(offsetof(GuestRegisterSnapshot, rsp)), rax);
        trampoline.stmxcsr(dword[rsp + SnapshotOffset + offsetof(GuestRegisterSnapshot, mxcsr)]);
        for (size_t index = 0; index < 16; ++index) {
            const size_t offset = SnapshotOffset + offsetof(GuestRegisterSnapshot, ymm) +
                                  index * sizeof(std::array<u8, 32>);
            trampoline.vmovdqu(yword[rsp + offset], Xbyak::Ymm(static_cast<int>(index)));
        }

        // The hook is Windows code: 32 bytes of shadow space and the MS x64 arguments are not
        // what a PS4_SYSV_ABI function expects, so it is called the SysV way (rdi, rsi), exactly
        // as the emulator does. A 32-byte gap below the snapshot keeps either convention safe.
        trampoline.mov(r12, rsp);
        trampoline.sub(rsp, 32);
        trampoline.mov(rdi, tag);
        trampoline.lea(rsi, ptr[r12 + SnapshotOffset]);
        trampoline.mov(rax, reinterpret_cast<u64>(hook));
        trampoline.call(rax);
        trampoline.mov(rsp, r12);

        for (size_t index = 0; index < 16; ++index) {
            const size_t offset = SnapshotOffset + offsetof(GuestRegisterSnapshot, ymm) +
                                  index * sizeof(std::array<u8, 32>);
            trampoline.vmovdqu(Xbyak::Ymm(static_cast<int>(index)), yword[rsp + offset]);
        }
        trampoline.ldmxcsr(dword[rsp + SnapshotOffset + offsetof(GuestRegisterSnapshot, mxcsr)]);
        trampoline.mov(rbx, snapshot(offsetof(GuestRegisterSnapshot, rbx)));
        trampoline.mov(rcx, snapshot(offsetof(GuestRegisterSnapshot, rcx)));
        trampoline.mov(rdx, snapshot(offsetof(GuestRegisterSnapshot, rdx)));
        trampoline.mov(rsi, snapshot(offsetof(GuestRegisterSnapshot, rsi)));
        trampoline.mov(rdi, snapshot(offsetof(GuestRegisterSnapshot, rdi)));
        trampoline.mov(rbp, snapshot(offsetof(GuestRegisterSnapshot, rbp)));
        trampoline.mov(r8, snapshot(offsetof(GuestRegisterSnapshot, r8)));
        trampoline.mov(r9, snapshot(offsetof(GuestRegisterSnapshot, r9)));
        trampoline.mov(r10, snapshot(offsetof(GuestRegisterSnapshot, r10)));
        trampoline.mov(r11, snapshot(offsetof(GuestRegisterSnapshot, r11)));
        trampoline.mov(r12, snapshot(offsetof(GuestRegisterSnapshot, r12)));
        trampoline.mov(r13, snapshot(offsetof(GuestRegisterSnapshot, r13)));
        trampoline.mov(r14, snapshot(offsetof(GuestRegisterSnapshot, r14)));
        trampoline.mov(r15, snapshot(offsetof(GuestRegisterSnapshot, r15)));
        trampoline.mov(rax, snapshot(offsetof(GuestRegisterSnapshot, rflags)));
        trampoline.push(rax);
        trampoline.popfq();
        trampoline.mov(rax, snapshot(offsetof(GuestRegisterSnapshot, rax)));
        trampoline.mov(rsp, snapshot(offsetof(GuestRegisterSnapshot, rsp)));
        Xbyak::Label run_original;
        if (skip_flag != nullptr) {
            // Function entry only: a plain ret skips the whole function (see cpu_patches.h).
            trampoline.push(rax);
            trampoline.mov(rax, reinterpret_cast<u64>(skip_flag));
            trampoline.cmp(byte[rax], 0);
            trampoline.mov(byte[rax], 0);
            trampoline.pop(rax);
            trampoline.je(run_original, Xbyak::CodeGenerator::LabelType::T_NEAR);
            trampoline.ret();
        }
        trampoline.L(run_original);
        trampoline.db(expected_instructions.data(), expected_instructions.size());
        trampoline.jmp(code + expected_instructions.size(), Xbyak::CodeGenerator::LabelType::T_NEAR);
    } catch (const Xbyak::Error& error) {
        trampoline.setSize(trampoline_offset);
        BbscMod::LogPlain(error.what());
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), trampoline_start,
                          trampoline.getSize() - trampoline_offset);
    return WriteJump(code, trampoline_start, expected_instructions.size());
}

} // namespace Core
