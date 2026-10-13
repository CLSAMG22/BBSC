// BBSC mod: paths. The "user" folder is the DLL's own folder.
#pragma once
#include <filesystem>
#include <string>

namespace Common::FS {
enum class PathType { UserDir, LogDir, CapturesDir };
std::filesystem::path GetUserPath(PathType type);
inline std::string PathToUTF8String(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}
} // namespace Common::FS
