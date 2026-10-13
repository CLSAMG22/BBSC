// BBSC mod: the three values the emulator's MemoryPatcher knows about the game, set by the DLL
// once it has verified the image (bbsc_mod.cpp).
#pragma once
#include <cstdint>
#include <string>

namespace MemoryPatcher {
inline std::uintptr_t g_eboot_address = 0;
inline std::uint64_t g_eboot_image_size = 0;
inline std::string g_game_serial;
} // namespace MemoryPatcher
