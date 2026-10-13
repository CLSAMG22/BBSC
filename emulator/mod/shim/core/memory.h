// BBSC mod: stand-in for the emulator's memory manager. The game's memory sits at its guest
// addresses inside the emulator's process, so Windows' VirtualQuery answers what the emulator's
// own map would.
#pragma once
#include "common/enum.h"
#include "common/singleton.h"
#include "common/types.h"

namespace Core {

enum class MemoryProt : u32 {
    NoAccess = 0,
    CpuRead = 1,
    CpuWrite = 2,
    CpuReadWrite = 3,
    CpuExec = 4,
};
DECLARE_ENUM_FLAG_OPERATORS(MemoryProt)

class MemoryManager {
public:
    bool IsValidMapping(u64 address, u64 size = 0);
    s32 QueryProtection(u64 address, void** start, void** end, u32* prot);
};
using Memory = Common::Singleton<MemoryManager>;

} // namespace Core
