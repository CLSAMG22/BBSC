// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

// Shared by the BBSC emulator build (src-shadp2p/src/core/mod_scan.h) and the BBSC Qt launcher
// (src-qtlauncher/src/qt_gui/bbsc_mod_scan.h). Keep the two copies identical: only the standard
// library and nlohmann::json are used.
//
// A mod is a folder in <user>/mods/ (every game, unless mod.json lists "games") or in
// <user>/mods/<SERIAL>/ (that game only). mod.json is optional; without it the folder is detected:
//   *.dll                       -> DLL mod, loaded into the emulator after the eboot
//   *.prx / *.sprx (or prx/)    -> PS4 modules, loaded and started with the game's modules
//   *.xml (or patches/)         -> shadPS4 patch files, every <Metadata> patch is a switch
//   files/, a .zar, or other    -> game files layered over the game (e.g. files/dvdroot_ps4/...
//   folders (dvdroot_ps4, ...)     or dvdroot_ps4/... at the top), in load order
// mod.json can name them itself: "dll" / "prx" / "patches" (string or list), "files" (folder or
// .zar, "." = the mod folder), plus id, name, version, author, description, games,
// enabled_by_default, env, env_disabled and settings (see mod_manager.h in the emulator).
// Per-game choices: <user>/mods/_settings/<SERIAL>.json
//   {"mods": {"<id>": {"enabled": true, "order": 0, "settings": {"<setting id>": "<value>"}}}}

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <utility>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>

namespace BbscMods {

namespace fs = std::filesystem;

enum class SettingType { Bool, Int, Choice, Text };

struct SettingDef {
    std::string id;
    std::string name;
    std::string description;
    SettingType type = SettingType::Bool;
    std::vector<std::string> env;
    std::string default_value; // "1"/"0", digits, option text, or text
    int min = 0;
    int max = 100;
    std::vector<std::string> options;
};

struct PatchFile {
    fs::path path;
    std::vector<std::string> names; // <Metadata Name="..."> entries; each is a setting "patch:<name>"
};

struct ModInfo {
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    fs::path folder; // empty for a built-in mod
    std::vector<fs::path> dlls;
    std::vector<fs::path> prx;
    std::vector<PatchFile> patches;
    fs::path files; // folder or .zar layered over the game; empty = none
    // What `files` stands for in the game: "" = the game's root (it holds dvdroot_ps4\...),
    // "dvdroot_ps4" = it holds menu\, map\, parts\... directly (mods made to be copied there).
    std::string files_prefix;
    std::vector<std::string> games;
    bool enabled_by_default = false;
    bool built_in = false;
    std::map<std::string, std::string> env_enabled;
    std::map<std::string, std::string> env_disabled;
    std::vector<SettingDef> settings;
    std::string problem; // why it cannot run; empty when fine

    std::string Kinds() const {
        std::string out;
        const auto add = [&out](std::string_view kind) {
            out += out.empty() ? "" : " + ";
            out += kind;
        };
        if (built_in) {
            add("Built in");
        }
        if (!dlls.empty()) {
            add("DLL");
        }
        if (!prx.empty()) {
            add("PRX");
        }
        if (!patches.empty()) {
            add("Patch");
        }
        if (!files.empty()) {
            add("Files");
        }
        return out.empty() ? "Settings" : out;
    }
};

struct ModChoice {
    bool enabled = false;
    int order = 0; // load order: lower wins when two file mods have the same file
    std::map<std::string, std::string> values;
};

using Choices = std::map<std::string, ModChoice>;

// ---- BBSC, compiled into the BBSC emulator build ----

struct BuiltInSwitch {
    const char* id;
    const char* name;
    const char* description;
    const char* env;
};

inline constexpr BuiltInSwitch BbscSwitches[] = {
    {"seamless", "Seamless summon", "Summon without the vanilla signs and waits (Wozzardman)",
     "SHADPS4_BLOODBORNE_SEAMLESS_COOP"},
    {"persist", "Keep the session", "The session survives loads, deaths and boss kills",
     "SHADPS4_BBSC_PERSIST"},
    {"connect_first", "Cross-map summon", "Join first, then one load into the host's map",
     "SHADPS4_BBSC_CONNECT_FIRST"},
    {"warp_together", "Warp together", "The guest follows the host through lamp warps",
     "SHADPS4_BBSC_WARP_TOGETHER"},
    {"world_sync", "World sync", "The guest sees the host's world state (doors, bosses, lifts)",
     "SHADPS4_BBSC_WORLD_SYNC"},
    {"dream", "Hunter's Dream together", "The guest uses the Doll, workshop and messengers",
     "SHADPS4_BBSC_DREAM"},
    {"no_fog_walls", "No co-op fog walls", "Leaving the area is allowed", "SHADPS4_BBSC_NO_FOG_WALLS"},
    {"lamp", "Lamps in co-op", "The host can use lamps during a session", "SHADPS4_BBSC_LAMP"},
    {"host_dead", "Stay when the host dies", "The guest keeps the session if the host dies",
     "SHADPS4_BBSC_PERSIST_HOSTDEAD"},
    {"spectate", "Respawn in place", "A dead player respawns / spectates instead of leaving",
     "SHADPS4_BBSC_SPECTATE"},
    {"full_echoes", "Full echoes", "A guest is paid the host's rate, not half",
     "SHADPS4_BBSC_FULL_ECHOES"},
    {"no_hp_penalty", "No guest HP cut", "Removes the summoned guest's 30% max HP cut",
     "SHADPS4_BBSC_NO_HP_PENALTY"},
    {"guest_items", "Guest items", "A guest picks up and keeps items", "SHADPS4_BBSC_GUEST_ITEMS"},
    {"solid_npcs", "Solid NPCs", "A guest sees NPCs solid, not as grey ghosts",
     "SHADPS4_BBSC_SOLID_NPCS"},
    {"death_penalty", "Bloodstain on death", "A kept death still drops a bloodstain and echoes",
     "SHADPS4_BBSC_DEATH_PENALTY"},
    {"free_bell", "Free Beckoning Bell", "The bell costs no Insight", "SHADPS4_BBSC_FREE_BELL"},
    {"five_hunters", "Five hunters", "The host may summon up to 4 guests", "SHADPS4_BBSC_FIVE_HUNTERS"},
    {"messages", "Co-op texts", "Texts that fit seamless play (no \"Returning to your world\")",
     "SHADPS4_BBSC_MESSAGES"},
    {"load_cover", "Black fade on joins", "The guest's loads happen behind a black fade",
     "SHADPS4_BBSC_LOAD_COVER"},
    {"floor_guard", "Floor guard", "No crash when a character's floor was unloaded",
     "SHADPS4_BBSC_FLOOR_GUARD"},
    {"cinematic", "Free camera (F1)", "Keyboard + mouse camera for filming",
     "SHADPS4_BBSC_CINEMATIC"},
};

inline ModInfo BuiltInBbsc() {
    ModInfo mod;
    mod.id = "bbsc";
    mod.name = "BBSC - Bloodborne Seamless Co-op";
    mod.version = "built in";
    mod.author = "CLS_AMG; seamless summon by Wozzardman; server format by fhody125";
    mod.description = "One co-op session across areas, deaths, lamps, bosses and the Hunter's "
                      "Dream. Built into the BBSC emulator build (other builds ignore it). "
                      "Off = vanilla online.";
    mod.games = {"CUSA03173"};
    mod.enabled_by_default = true;
    mod.built_in = true;
    for (const auto& sw : BbscSwitches) {
        SettingDef def;
        def.id = sw.id;
        def.name = sw.name;
        def.description = sw.description;
        def.env = {sw.env};
        def.default_value = "1";
        mod.settings.push_back(std::move(def));
        mod.env_disabled[sw.env] = "0";
    }
    // Off also removes the hooks and the summon capture, i.e. a vanilla game.
    mod.env_disabled["SHADPS4_BLOODBORNE_RE_TRACE"] = "0";
    mod.env_disabled["SHADPS4_CAPTURE_BLOODBORNE_SUMMON"] = "0";
    return mod;
}

// ---- helpers ----

inline bool IsSerial(std::string_view name) {
    if (name.size() != 9) {
        return false;
    }
    for (size_t i = 0; i < 9; ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (i < 4 ? !std::isupper(c) : !std::isdigit(c)) {
            return false;
        }
    }
    return true;
}

inline std::string Lower(std::string text) {
    std::ranges::transform(text, text.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

inline std::vector<std::string> StringList(const nlohmann::json& value) {
    std::vector<std::string> out;
    if (value.is_string()) {
        out.push_back(value.get<std::string>());
    } else if (value.is_array()) {
        for (const auto& item : value) {
            if (item.is_string()) {
                out.push_back(item.get<std::string>());
            }
        }
    }
    return out;
}

inline std::map<std::string, std::string> StringMap(const nlohmann::json& value) {
    std::map<std::string, std::string> out;
    if (value.is_object()) {
        for (const auto& [key, item] : value.items()) {
            if (item.is_string()) {
                out[key] = item.get<std::string>();
            } else if (item.is_boolean()) {
                out[key] = item.get<bool>() ? "1" : "0";
            } else if (item.is_number_integer()) {
                out[key] = std::to_string(item.get<long long>());
            }
        }
    }
    return out;
}

inline std::string DefaultValue(const nlohmann::json& value, SettingType type) {
    switch (type) {
    case SettingType::Bool:
        return value.is_boolean() && value.get<bool>() ? "1" : "0";
    case SettingType::Int:
        return value.is_number_integer() ? std::to_string(value.get<long long>()) : "0";
    default:
        return value.is_string() ? value.get<std::string>() : "";
    }
}

// The patches in a shadPS4 patch XML: every <Metadata ... Name="x" ... isEnabled="true|false">.
inline void ReadPatchNames(const fs::path& xml, PatchFile& out, std::vector<SettingDef>& settings) {
    std::ifstream file(xml, std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    const std::string content = text.str();
    static const std::regex metadata(R"(<Metadata\b([^>]*)>)");
    static const std::regex name_attr(R"(\bName\s*=\s*"([^"]*)\")");
    static const std::regex enabled_attr(R"(\bisEnabled\s*=\s*"([^"]*)\")");
    static const std::regex note_attr(R"(\bNote\s*=\s*"([^"]*)\")");
    for (auto it = std::sregex_iterator(content.begin(), content.end(), metadata);
         it != std::sregex_iterator(); ++it) {
        const std::string attributes = (*it)[1];
        std::smatch m;
        if (!std::regex_search(attributes, m, name_attr)) {
            continue;
        }
        const std::string name = m[1];
        if (std::ranges::find(out.names, name) != out.names.end()) {
            continue;
        }
        out.names.push_back(name);
        SettingDef def;
        def.id = "patch:" + name;
        def.name = name;
        if (std::regex_search(attributes, m, note_attr)) {
            def.description = m[1];
        }
        def.default_value =
            std::regex_search(attributes, m, enabled_attr) && m[1] == "true" ? "1" : "0";
        settings.push_back(std::move(def));
    }
}

inline void AddFiles(const fs::path& folder, std::string_view subfolder,
                     std::initializer_list<std::string_view> extensions,
                     std::vector<fs::path>& out) {
    std::error_code ec;
    const fs::path dir = subfolder.empty() ? folder : folder / subfolder;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string ext = Lower(entry.path().extension().string());
        if (std::ranges::find(extensions, ext) != extensions.end()) {
            out.push_back(entry.path());
        }
    }
    std::ranges::sort(out);
}

// ---- Where a file mod's game files start ----
// Mods come laid out three ways: <mod>\dvdroot_ps4\menu\... (the game's root), <mod>\menu\...
// (made to be copied INTO dvdroot_ps4), or either of those inside one or two wrapper folders
// (<mod>\<mod name>\dvdroot_ps4\...). The emulator needs to know which, or the files land in the
// wrong place and the game never sees them.
inline constexpr std::array GameDataFolders{
    "action", "chr", "event", "facegen", "font", "map", "material", "menu", "movie", "msg",
    "mtd", "obj", "other", "param", "parts", "remo", "script", "sfx", "shader", "sound",
};
inline constexpr std::array NotGameFiles{"prx", "patches", "logs", "log", "docs",
                                         "doc", "config", "cache", "captures"};

inline bool IsModOwnFolder(const std::string& name) {
    return std::ranges::find(NotGameFiles, Lower(name)) != NotGameFiles.end() ||
           name.starts_with('_') || name.starts_with('.');
}

// {folder to layer, prefix}; {"", ""} when the folder holds no game files.
inline std::pair<fs::path, std::string> FindGameFiles(const fs::path& folder, int depth = 0) {
    std::error_code ec;
    bool game_data = false;
    bool other = false;
    std::vector<fs::path> subfolders;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        if (!entry.is_directory(ec)) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (IsModOwnFolder(name)) {
            continue;
        }
        const std::string lower = Lower(name);
        if (lower == "dvdroot_ps4") {
            return {folder, ""};
        }
        if (std::ranges::find(GameDataFolders, lower) != GameDataFolders.end()) {
            game_data = true;
        } else {
            other = true;
            subfolders.push_back(entry.path());
        }
    }
    if (game_data) {
        return {folder, "dvdroot_ps4"};
    }
    if (depth < 2) {
        // A wrapper: exactly the folders that are not game data, look inside them.
        for (const fs::path& sub : subfolders) {
            if (auto found = FindGameFiles(sub, depth + 1); !found.first.empty()) {
                return found;
            }
        }
    }
    // sce_sys\ and the like at the top: the game's root, as before.
    return other && depth == 0 ? std::pair<fs::path, std::string>{folder, ""}
                               : std::pair<fs::path, std::string>{};
}

// The mods folder: <user>\mods, or the folder named in <user>\mods-folder.txt (the launcher's
// "Mods Folder..." button) when that folder exists.
inline fs::path ModsRoot(const fs::path& user_dir) {
    std::ifstream file(user_dir / "mods-folder.txt");
    std::string line;
    if (file && std::getline(file, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        std::error_code ec;
        const fs::path custom = fs::u8path(line);
        if (!line.empty() && fs::is_directory(custom, ec)) {
            return custom;
        }
    }
    return user_dir / "mods";
}


inline ModInfo ReadMod(const fs::path& folder, std::string_view serial_folder) {
    using nlohmann::json;
    ModInfo mod;
    mod.folder = folder;
    mod.id = folder.filename().string();
    mod.name = mod.id;
    if (!serial_folder.empty()) {
        mod.games = {std::string(serial_folder)};
    }

    json j = json::object();
    if (fs::exists(folder / "mod.json")) {
        try {
            std::ifstream file(folder / "mod.json");
            j = json::parse(file, nullptr, true, true);
        } catch (const std::exception& e) {
            mod.problem = std::string("mod.json could not be read: ") + e.what();
            return mod;
        }
    }
    mod.id = j.value("id", mod.id);
    mod.name = j.value("name", mod.name);
    mod.version = j.value("version", "");
    mod.author = j.value("author", "");
    mod.description = j.value("description", "");
    mod.enabled_by_default = j.value("enabled_by_default", false);
    if (j.contains("games")) {
        mod.games = StringList(j["games"]);
    }
    if (j.contains("env")) {
        mod.env_enabled = StringMap(j["env"]);
    }
    if (j.contains("env_disabled")) {
        mod.env_disabled = StringMap(j["env_disabled"]);
    }
    if (j.contains("settings") && j["settings"].is_array()) {
        for (const auto& s : j["settings"]) {
            SettingDef def;
            def.id = s.value("id", "");
            if (def.id.empty()) {
                continue;
            }
            def.name = s.value("name", def.id);
            def.description = s.value("description", "");
            const std::string type = s.value("type", "bool");
            def.type = type == "int"      ? SettingType::Int
                       : type == "choice" ? SettingType::Choice
                       : type == "text"   ? SettingType::Text
                                          : SettingType::Bool;
            if (s.contains("env")) {
                def.env = StringList(s["env"]);
            }
            def.min = s.value("min", 0);
            def.max = s.value("max", 100);
            if (s.contains("options")) {
                def.options = StringList(s["options"]);
            }
            def.default_value = DefaultValue(s.contains("default") ? s["default"] : json{},
                                             def.type);
            if (def.type == SettingType::Choice && !def.options.empty() &&
                std::ranges::find(def.options, def.default_value) == def.options.end()) {
                def.default_value = def.options.front();
            }
            mod.settings.push_back(std::move(def));
        }
    }

    // Parts: named in mod.json, otherwise detected.
    const auto named = [&](const char* key, std::vector<fs::path>& out) {
        for (const auto& item : StringList(j[key])) {
            out.push_back(folder / item);
        }
    };
    if (j.contains("dll")) {
        named("dll", mod.dlls);
    } else {
        AddFiles(folder, "", {".dll"}, mod.dlls);
    }
    if (j.contains("prx")) {
        named("prx", mod.prx);
    } else {
        AddFiles(folder, "", {".prx", ".sprx"}, mod.prx);
        AddFiles(folder, "prx", {".prx", ".sprx"}, mod.prx);
    }
    std::vector<fs::path> xmls;
    if (j.contains("patches")) {
        named("patches", xmls);
    } else {
        AddFiles(folder, "", {".xml"}, xmls);
        AddFiles(folder, "patches", {".xml"}, xmls);
    }
    for (const auto& xml : xmls) {
        PatchFile patch{xml, {}};
        ReadPatchNames(xml, patch, mod.settings);
        mod.patches.push_back(std::move(patch));
    }
    std::error_code ec;
    if (j.contains("files") && j["files"].is_string()) {
        const std::string files = j["files"].get<std::string>();
        mod.files = files.empty() || files == "." ? folder : folder / files;
    } else if (fs::is_directory(folder / "files", ec)) {
        mod.files = folder / "files";
    } else {
        std::vector<fs::path> zars;
        AddFiles(folder, "", {".zar"}, zars);
        if (!zars.empty()) {
            mod.files = zars.front();
        } else {
            const auto [root, prefix] = FindGameFiles(folder);
            mod.files = root;
            mod.files_prefix = prefix;
        }
    }
    if (!mod.files.empty() && mod.files != folder && fs::is_directory(mod.files, ec) &&
        mod.files_prefix.empty() && Lower(mod.files.filename().string()) == "files") {
        // files\ itself may be laid out either way
        const auto [root, prefix] = FindGameFiles(mod.files);
        if (!root.empty()) {
            mod.files = root;
            mod.files_prefix = prefix;
        }
    }

    // Anything named but missing
    const auto check = [&mod](const fs::path& path) {
        std::error_code missing;
        if (mod.problem.empty() && !fs::exists(path, missing)) {
            mod.problem = "missing " + path.filename().string();
        }
    };
    std::ranges::for_each(mod.dlls, check);
    std::ranges::for_each(mod.prx, check);
    for (const auto& patch : mod.patches) {
        check(patch.path);
    }
    if (!mod.files.empty()) {
        check(mod.files);
    }
    return mod;
}

// Every mod: the built-in ones, then <root>/<folder> and <root>/<SERIAL>/<folder>.
// bbsc_built_in: the emulator has BBSC compiled in - it is listed as a built-in mod and a bbsc DLL
// mod is renamed "bbsc-dll" (must not run too). Otherwise the DLL mod is simply "bbsc".
inline std::vector<ModInfo> Scan(const fs::path& root, bool bbsc_built_in = true) {
    std::vector<ModInfo> mods;
    if (bbsc_built_in) {
        mods.push_back(BuiltInBbsc());
    }
    std::vector<ModInfo> found;
    std::error_code ec;
    const auto add = [&](const fs::path& folder, std::string_view serial) {
        ModInfo mod = ReadMod(folder, serial);
        const auto taken = [&](const std::string& id) {
            const auto same = [&](const ModInfo& other) { return other.id == id; };
            return std::ranges::any_of(mods, same) || std::ranges::any_of(found, same);
        };
        if (bbsc_built_in && mod.id == "bbsc" && !mod.dlls.empty()) {
            // The DLL build of BBSC: an emulator with BBSC built in must not load it too.
            mod.id = "bbsc-dll";
            mod.name += " (DLL)";
        }
        const std::string base = mod.id;
        for (int n = 2; taken(mod.id); ++n) {
            mod.id = base + "-" + std::to_string(n);
        }
        found.push_back(std::move(mod));
    };
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        const std::string name = entry.path().filename().string();
        if (!entry.is_directory() || name.starts_with('_') || name.starts_with('.')) {
            continue;
        }
        if (IsSerial(name)) {
            std::error_code inner;
            for (const auto& sub : fs::directory_iterator(entry.path(), inner)) {
                const std::string subname = sub.path().filename().string();
                if (sub.is_directory() && !subname.starts_with('_') && !subname.starts_with('.')) {
                    add(sub.path(), name);
                }
            }
        } else {
            add(entry.path(), "");
        }
    }
    std::ranges::sort(found, {}, [](const ModInfo& m) { return Lower(m.name); });
    mods.insert(mods.end(), std::make_move_iterator(found.begin()),
                std::make_move_iterator(found.end()));
    return mods;
}

inline bool AppliesTo(const ModInfo& mod, std::string_view serial) {
    return mod.games.empty() || std::ranges::find(mod.games, serial) != mod.games.end();
}

inline fs::path ChoicesPath(const fs::path& root, std::string_view serial) {
    return root / "_settings" / (std::string(serial) + ".json");
}

// Saved choices for a game with defaults for every mod that applies to it.
inline Choices LoadChoices(const fs::path& root, std::string_view serial,
                           const std::vector<ModInfo>& mods) {
    using nlohmann::json;
    json saved = json::object();
    try {
        std::ifstream file(ChoicesPath(root, serial));
        if (file.is_open()) {
            saved = json::parse(file, nullptr, true, true);
        }
    } catch (...) {
        saved = json::object();
    }
    const json empty = json::object();
    const json& saved_mods = saved.contains("mods") && saved["mods"].is_object() ? saved["mods"]
                                                                                 : empty;
    Choices choices;
    int index = 0;
    for (const ModInfo& mod : mods) {
        if (!AppliesTo(mod, serial)) {
            continue;
        }
        ModChoice choice;
        const json& entry = saved_mods.contains(mod.id) ? saved_mods[mod.id] : empty;
        choice.enabled = entry.value("enabled", mod.enabled_by_default);
        choice.order = entry.value("order", 1000 + index++);
        const json& values = entry.contains("settings") ? entry["settings"] : empty;
        for (const SettingDef& def : mod.settings) {
            choice.values[def.id] = values.contains(def.id) && values[def.id].is_string()
                                        ? values[def.id].get<std::string>()
                                        : def.default_value;
        }
        choices[mod.id] = std::move(choice);
    }
    return choices;
}

inline bool SaveChoices(const fs::path& root, std::string_view serial, const Choices& choices) {
    nlohmann::json out;
    out["mods"] = nlohmann::json::object();
    for (const auto& [id, choice] : choices) {
        out["mods"][id]["enabled"] = choice.enabled;
        out["mods"][id]["order"] = choice.order;
        out["mods"][id]["settings"] = choice.values;
    }
    try {
        const fs::path path = ChoicesPath(root, serial);
        fs::create_directories(path.parent_path());
        std::ofstream file(path);
        file << out.dump(2);
        return file.good();
    } catch (...) {
        return false;
    }
}

// The mods of one game in load order (first = wins file conflicts), orders renumbered 0..n.
inline std::vector<ModInfo> ModsForGame(const fs::path& root, std::string_view serial,
                                        Choices& choices, bool bbsc_built_in = true) {
    std::vector<ModInfo> mods;
    for (auto& mod : Scan(root, bbsc_built_in)) {
        if (AppliesTo(mod, serial)) {
            mods.push_back(std::move(mod));
        }
    }
    const Choices loaded = LoadChoices(root, serial, mods);
    for (const auto& [id, choice] : loaded) {
        if (!choices.contains(id)) {
            choices[id] = choice;
        } else {
            for (const auto& [key, value] : choice.values) {
                choices[id].values.try_emplace(key, value);
            }
        }
    }
    std::ranges::stable_sort(mods, {}, [&](const ModInfo& m) { return choices[m.id].order; });
    for (int i = 0; i < static_cast<int>(mods.size()); ++i) {
        choices[mods[i].id].order = i;
    }
    return mods;
}

// Relative file paths of a file mod (folders only; a .zar is not listed).
inline std::vector<std::string> ListFiles(const ModInfo& mod) {
    std::vector<std::string> out;
    std::error_code ec;
    if (mod.files.empty() || !fs::is_directory(mod.files, ec)) {
        return out;
    }
    for (auto it = fs::recursive_directory_iterator(mod.files, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) {
            break;
        }
        if (!it->is_regular_file()) {
            continue;
        }
        std::string rel = Lower(fs::relative(it->path(), mod.files, ec).generic_string());
        if (mod.files == mod.folder &&
            (rel == "mod.json" || rel.find('/') == std::string::npos)) {
            continue; // the mod's own top files are not game files
        }
        if (IsModOwnFolder(rel.substr(0, rel.find('/')))) {
            continue;
        }
        out.push_back(mod.files_prefix.empty() ? std::move(rel) : mod.files_prefix + "/" + rel);
    }
    std::ranges::sort(out);
    return out;
}

} // namespace BbscMods
