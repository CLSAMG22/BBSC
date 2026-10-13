// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/mod_scan.h"

// Mod manager: mods in <user>/mods/ (formats and folder layout: core/mod_scan.h), switched on/off,
// ordered and configured per game from the settings window's Mods tab (emulator or BBSC launcher).
//
// Start-up, in this order:
//  1. ApplyEnvironment   - every enabled mod's "env" block and settings go into the environment
//                          (a variable already set by a launcher or command line wins)
//  2. FileOverlays       - enabled file mods, layered over /app0 in load order
//  3. LoadEnabledDlls    - after the eboot is mapped, before the game runs
//  4. EnabledPrx         - loaded and started with the game's modules (Linker::Execute)
//  5. EnabledPatches     - applied with the other shadPS4 patches (MemoryPatcher::OnGameLoaded)
//
// mod.json settings (optional):
//   {"id": "fast", "name": "Go fast", "type": "bool", "default": true, "env": "MYMOD_FAST"},
//   {"id": "count", "type": "int", "default": 3, "min": 1, "max": 9, "env": ["MYMOD_COUNT"]},
//   {"id": "mode", "type": "choice", "options": ["a", "b"], "default": "a", "env": "MYMOD_MODE"},
//   {"id": "addr", "type": "text", "default": "", "env": "MYMOD_ADDR"}
// Env values: bool "1"/"0", int as digits, choice = the option text, text as typed.

namespace Core::Mods {

using BbscMods::Choices;
using BbscMods::ModChoice;
using BbscMods::ModInfo;
using BbscMods::SettingDef;
using BbscMods::SettingType;

// Every mod: the built-in ones first, then the mods folder.
std::vector<ModInfo> Scan();

bool AppliesTo(const ModInfo& mod, std::string_view serial);

// The mods of one game in load order, with the saved choices (defaults for new mods).
std::vector<ModInfo> ModsForGame(std::string_view serial, Choices& choices);
bool SaveChoices(std::string_view serial, const Choices& choices);

// Emulator start-up (see above).
void ApplyEnvironment(std::string_view serial);
// {folder or .zar, prefix}: prefix "dvdroot_ps4" = the folder holds what the game keeps there.
std::vector<std::pair<std::filesystem::path, std::string>> FileOverlays(std::string_view serial);
void LoadEnabledDlls(std::string_view serial);
std::vector<std::filesystem::path> EnabledPrx(std::string_view serial);
std::vector<std::pair<std::filesystem::path, std::set<std::string>>> EnabledPatches(
    std::string_view serial);

} // namespace Core::Mods
