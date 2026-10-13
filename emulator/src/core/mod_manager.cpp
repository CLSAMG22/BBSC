// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdlib>

#include "common/logging/log.h"
#include "common/path_util.h"
#include "core/bbsc_persistence.h"
#include "core/mod_manager.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Core::Mods {

namespace {

namespace fs = std::filesystem;

// A folder with at least one mod (or <SERIAL> folder) in it; _settings alone does not count.
bool HasMods(const fs::path& dir) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.is_directory(ec) && !entry.path().filename().string().starts_with('_')) {
            return true;
        }
    }
    return false;
}

// <user>\mods normally. Started without the launcher and with an empty user folder - Nucleus
// Co-op split screen gives every window its own fresh %APPDATA%\shadPS4 - the mods next to the
// emulator are used instead: <exe folder>\mods, or <exe folder>\..\mods (the BBSC package keeps
// the emulator in emulator\ and mods\ beside it).
fs::path Root() {
    const fs::path user =
        BbscMods::ModsRoot(Common::FS::GetUserPath(Common::FS::PathType::UserDir));
    if (HasMods(user)) {
        return user;
    }
#ifdef _WIN32
    wchar_t exe[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) != 0) {
        const fs::path exe_dir = fs::path{exe}.parent_path();
        for (const fs::path& dir : {exe_dir / "mods", exe_dir.parent_path() / "mods"}) {
            if (HasMods(dir)) {
                static bool logged = false;
                if (!logged) {
                    logged = true;
                    LOG_INFO(Loader, "Mods: none in {}, using the mods next to the emulator: {}",
                             user.string(), dir.string());
                }
                return dir;
            }
        }
    }
#endif
    return user;
}

// BBSC is compiled into this emulator: its DLL build must not run on top of it.
void MarkBuiltInDuplicates(std::vector<ModInfo>& mods) {
    if constexpr (!Core::Bbsc::BuiltIn) {
        return;
    }
    for (ModInfo& mod : mods) {
        if (mod.id.starts_with("bbsc-dll") && mod.problem.empty()) {
            mod.problem = "BBSC is built into this emulator - the DLL is not needed here";
        }
    }
}

bool Runs(const ModInfo& mod, const Choices& choices) {
    const auto it = choices.find(mod.id);
    return it != choices.end() && it->second.enabled && mod.problem.empty();
}

void SetEnv(const std::string& name, const std::string& value) {
    if (name.empty()) {
        return;
    }
#ifdef _WIN32
    _putenv_s(name.c_str(), value.c_str());
#else
    setenv(name.c_str(), value.c_str(), 1);
#endif
}

std::string PathText(const fs::path& path) {
    return path.string();
}

} // namespace

std::vector<ModInfo> Scan() {
    auto mods = BbscMods::Scan(Root(), Core::Bbsc::BuiltIn);
    MarkBuiltInDuplicates(mods);
    return mods;
}

bool AppliesTo(const ModInfo& mod, std::string_view serial) {
    return BbscMods::AppliesTo(mod, serial);
}

std::vector<ModInfo> ModsForGame(std::string_view serial, Choices& choices) {
    auto mods = BbscMods::ModsForGame(Root(), serial, choices, Core::Bbsc::BuiltIn);
    MarkBuiltInDuplicates(mods);
    return mods;
}

bool SaveChoices(std::string_view serial, const Choices& choices) {
    const bool ok = BbscMods::SaveChoices(Root(), serial, choices);
    if (!ok) {
        LOG_ERROR(Loader, "Mods: could not save {}",
                  PathText(BbscMods::ChoicesPath(Root(), serial)));
    }
    return ok;
}

void ApplyEnvironment(std::string_view serial) {
    if (serial.empty()) {
        return;
    }
    Choices choices;
    const std::vector<ModInfo> mods = ModsForGame(serial, choices);

    // Everything to set, collected first so a launcher's own variables can be kept.
    std::map<std::string, std::string> wanted;
    for (const ModInfo& mod : mods) {
        const bool on = Runs(mod, choices);
        LOG_INFO(Loader, "Mods: {} #{} {} [{}] ({}){}", on ? "ON " : "off", choices[mod.id].order,
                 mod.name, mod.Kinds(), mod.built_in ? "built in" : PathText(mod.folder),
                 mod.problem.empty() ? std::string{} : " - " + mod.problem);
        if (!on) {
            for (const auto& [name, value] : mod.env_disabled) {
                wanted[name] = value;
            }
            continue;
        }
        for (const auto& [name, value] : mod.env_enabled) {
            wanted[name] = value;
        }
        const ModChoice& choice = choices[mod.id];
        for (const SettingDef& def : mod.settings) {
            const auto value = choice.values.find(def.id);
            if (value == choice.values.end()) {
                continue;
            }
            for (const std::string& name : def.env) {
                if (def.type == SettingType::Text && value->second.empty()) {
                    continue; // an empty text box leaves the mod's own default
                }
                wanted[name] = value->second;
            }
        }
    }

    for (const auto& [name, value] : wanted) {
        if (const char* existing = std::getenv(name.c_str()); existing != nullptr) {
            LOG_INFO(Loader, "Mods: {} kept at {} (set before start)", name, existing);
            continue;
        }
        SetEnv(name, value);
        LOG_INFO(Loader, "Mods: {}={}", name, value);
    }
}

std::vector<std::pair<fs::path, std::string>> FileOverlays(std::string_view serial) {
    std::vector<std::pair<fs::path, std::string>> out;
    if (serial.empty()) {
        return out;
    }
    Choices choices;
    for (const ModInfo& mod : ModsForGame(serial, choices)) {
        if (Runs(mod, choices) && !mod.files.empty()) {
            LOG_INFO(Loader, "Mods: game files from {} ({}{})", mod.name, PathText(mod.files),
                     mod.files_prefix.empty() ? "" : " = /app0/" + mod.files_prefix);
            out.emplace_back(mod.files, mod.files_prefix);
        }
    }
    return out;
}

void LoadEnabledDlls(std::string_view serial) {
    if (serial.empty()) {
        return;
    }
    Choices choices;
    for (const ModInfo& mod : ModsForGame(serial, choices)) {
        if (!Runs(mod, choices)) {
            continue;
        }
        for (const fs::path& dll : mod.dlls) {
#ifdef _WIN32
            // The mod's folder is searched for the DLL's own dependencies too.
            const HMODULE handle =
                LoadLibraryExW(dll.c_str(), nullptr,
                               LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (handle == nullptr) {
                LOG_ERROR(Loader, "Mods: could not load {} (error {})", PathText(dll),
                          GetLastError());
                continue;
            }
#else
            void* handle = dlopen(dll.c_str(), RTLD_NOW);
            if (handle == nullptr) {
                LOG_ERROR(Loader, "Mods: could not load {}: {}", PathText(dll), dlerror());
                continue;
            }
#endif
            LOG_INFO(Loader, "Mods: loaded DLL {} ({})", PathText(dll), mod.name);
        }
    }
}

std::vector<fs::path> EnabledPrx(std::string_view serial) {
    std::vector<fs::path> out;
    if (serial.empty()) {
        return out;
    }
    Choices choices;
    for (const ModInfo& mod : ModsForGame(serial, choices)) {
        if (Runs(mod, choices)) {
            out.insert(out.end(), mod.prx.begin(), mod.prx.end());
        }
    }
    return out;
}

std::vector<std::pair<fs::path, std::set<std::string>>> EnabledPatches(std::string_view serial) {
    std::vector<std::pair<fs::path, std::set<std::string>>> out;
    if (serial.empty()) {
        return out;
    }
    Choices choices;
    for (const ModInfo& mod : ModsForGame(serial, choices)) {
        if (!Runs(mod, choices)) {
            continue;
        }
        const ModChoice& choice = choices[mod.id];
        for (const auto& patch : mod.patches) {
            std::set<std::string> names;
            for (const std::string& name : patch.names) {
                const auto value = choice.values.find("patch:" + name);
                if (value != choice.values.end() && value->second == "1") {
                    names.insert(name);
                }
            }
            if (!names.empty()) {
                out.emplace_back(patch.path, std::move(names));
            }
        }
    }
    return out;
}

} // namespace Core::Mods
