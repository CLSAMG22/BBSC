//  SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
//  SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <ranges>
#include <nlohmann/json.hpp>
#include <ImGuiFileDialog.h>
#include <cmrc/cmrc.hpp>
#include <stb_image.h>

#include "common/elf_info.h"
#include "common/logging/log.h"
#include "common/path_util.h"
#include "core/devtools/layer.h"
#include "core/user_settings.h"
#include "imgui/imgui_std.h"
#include "settings_dialog_imgui.h"

CMRC_DECLARE(res);

constexpr float gameImageSize = 200.f;
constexpr float settingsIconSize = 125.f;

namespace ImGuiEmuSettings {

int SettingsWindow::GetComboIndex(std::string selection, std::vector<std::string> options) {
    for (int i = 0; i < options.size(); i++) {
        if (selection == options[i])
            return i;
    }

    return 0;
}

void SettingsWindow::LoadSettings(std::string profile) {
    const bool isSpecific = currentProfile != "Global";
    isSpecific ? EmulatorSettings.Load(profile) : EmulatorSettings.Load();

    /////////// General Tab
    int languageIndex = EmulatorSettings.GetConsoleLanguage();
    std::string language;
    for (const auto& [key, value] : languageMap) {
        if (value == languageIndex) {
            language = key;
        }
    }

    consoleLanguageSetting = GetComboIndex(language, languageOptions);
    volumeSetting = EmulatorSettings.GetVolumeSlider();
    showSplashSetting = EmulatorSettings.IsShowSplash();
    audioBackendSetting = EmulatorSettings.GetAudioBackend();

    /////////// Graphics Tab
    fullscreenModeSetting =
        GetComboIndex(EmulatorSettings.GetFullScreenMode(), fullscreenModeOptions);
    presentModeSetting = GetComboIndex(EmulatorSettings.GetPresentMode(), presentModeOptions);
    windowHeightSetting = EmulatorSettings.GetWindowHeight();
    windowWidthSetting = EmulatorSettings.GetWindowWidth();
    hdrAllowedSetting = EmulatorSettings.IsHdrAllowed();
    fsrEnabledSetting = EmulatorSettings.IsFsrEnabled();
    rcasEnabledSetting = EmulatorSettings.IsRcasEnabled();
    rcasAttenuationSetting = static_cast<float>(EmulatorSettings.GetRcasAttenuation() * 0.001f);

    /////////// Input Tab
    motionControlsSetting = EmulatorSettings.IsMotionControlsEnabled();
    backgroundControllerSetting = EmulatorSettings.IsBackgroundControllerInput();
    cursorStateSetting = EmulatorSettings.GetCursorState();
    cursorTimeoutSetting = EmulatorSettings.GetCursorHideTimeout();

    /////////// Trophy Tab
    trophyPopupDisabledSetting = EmulatorSettings.IsTrophyPopupDisabled();
    trophySideSetting =
        GetComboIndex(EmulatorSettings.GetTrophyNotificationSide(), trophySideOptions);
    trophyDurationSetting = static_cast<float>(EmulatorSettings.GetTrophyNotificationDuration());

    /////////// Log Tab
    logEnableSetting = EmulatorSettings.IsLogEnable();
    logSeparateSetting = EmulatorSettings.IsLogSeparate();
    logSyncSetting = EmulatorSettings.IsLogSync();

    /////////// Experimental Tab
    if (isSpecific) {
        readbacksModeSetting = EmulatorSettings.GetReadbacksMode();
        readbackLinearImagesSetting = EmulatorSettings.IsReadbackLinearImagesEnabled();
        directMemoryAccessSetting = EmulatorSettings.IsDirectMemoryAccessEnabled();
        // Windows static guest red-zone protection
        windowsGuestRedZoneProtectionModeSetting =
            static_cast<int>(EmulatorSettings.GetWindowsGuestRedZoneProtectionMode());
        if (windowsGuestRedZoneProtectionModeSetting < 0 ||
            windowsGuestRedZoneProtectionModeSetting >=
                static_cast<int>(windowsGuestRedZoneProtectionModeOptions.size())) {
            windowsGuestRedZoneProtectionModeSetting =
                static_cast<int>(WindowsGuestRedZoneProtectionMode::Disabled);
        }
        devkitConsoleSetting = EmulatorSettings.IsDevKit();
        neoModeSetting = EmulatorSettings.IsNeo();
        shadnetEnabledSetting = EmulatorSettings.IsShadNetEnabledSetting();
        connectedNetworkSetting = EmulatorSettings.IsConnectedToNetwork();
        pipelineCacheEnabledSetting = EmulatorSettings.IsPipelineCacheEnabled();
        pipelineCacheArchiveSetting = EmulatorSettings.IsPipelineCacheArchived();
        extraDmemSetting = EmulatorSettings.GetExtraDmemInMBytes();
        vblankFrequencySetting = EmulatorSettings.GetVblankFrequency();
    }

    /////////// Mods Tab (a game profile, or the game that is running)
    modSerial = isSpecific ? profile : runningGameSerial;
    mods.clear();
    modChoices.clear();
    selectedMod = 0;
    if (!modSerial.empty()) {
        mods = Core::Mods::ModsForGame(modSerial, modChoices);
    }

    /////////// Users Tab
    if (!isSpecific && !isGameRunning) {
        LoadUsersTab();
    }
}

void SettingsWindow::SaveSettings(std::string profile) {
    const bool isSpecific = currentProfile != "Global";

    /////////// General Tab
    EmulatorSettings.SetConsoleLanguage(languageMap.at(languageOptions.at(consoleLanguageSetting)),
                                        isSpecific);
    EmulatorSettings.SetVolumeSlider(volumeSetting, isSpecific);
    EmulatorSettings.SetShowSplash(showSplashSetting, isSpecific);
    EmulatorSettings.SetAudioBackend(audioBackendSetting, isSpecific);

    /////////// Graphics Tab
    bool isFullscreen = fullscreenModeSetting != 0;
    EmulatorSettings.SetFullScreen(isFullscreen);
    EmulatorSettings.SetFullScreenMode(fullscreenModeOptions.at(fullscreenModeSetting), isSpecific);
    EmulatorSettings.SetPresentMode(presentModeOptions.at(presentModeSetting), isSpecific);
    EmulatorSettings.SetWindowHeight(windowHeightSetting, isSpecific);
    EmulatorSettings.SetWindowWidth(windowWidthSetting, isSpecific);
    EmulatorSettings.SetHdrAllowed(hdrAllowedSetting, isSpecific);
    EmulatorSettings.SetFsrEnabled(fsrEnabledSetting, isSpecific);
    EmulatorSettings.SetRcasEnabled(rcasEnabledSetting, isSpecific);
    EmulatorSettings.SetRcasAttenuation(static_cast<int>(rcasAttenuationSetting * 1000),
                                        isSpecific);

    /////////// Input Tab
    EmulatorSettings.SetMotionControlsEnabled(motionControlsSetting, isSpecific);
    EmulatorSettings.SetBackgroundControllerInput(backgroundControllerSetting, isSpecific);
    EmulatorSettings.SetCursorState(cursorStateSetting, isSpecific);
    EmulatorSettings.SetCursorHideTimeout(cursorTimeoutSetting, isSpecific);

    /////////// Trophy Tab
    EmulatorSettings.SetTrophyPopupDisabled(trophyPopupDisabledSetting, isSpecific);
    EmulatorSettings.SetTrophyNotificationSide(trophySideOptions.at(trophySideSetting), isSpecific);
    EmulatorSettings.SetTrophyNotificationDuration(static_cast<double>(trophyDurationSetting));

    /////////// Log Tab
    EmulatorSettings.SetLogEnable(logEnableSetting, isSpecific);
    EmulatorSettings.SetLogSeparate(logSeparateSetting, isSpecific);
    EmulatorSettings.SetLogSync(logSyncSetting, isSpecific);

    /////////// Experimental Tab
    if (isSpecific) {
        EmulatorSettings.SetReadbacksMode(readbacksModeSetting, true);
        EmulatorSettings.SetReadbackLinearImagesEnabled(readbackLinearImagesSetting, true);
        EmulatorSettings.SetDirectMemoryAccessEnabled(directMemoryAccessSetting, true);
        // Windows static guest red-zone protection
        EmulatorSettings.SetWindowsGuestRedZoneProtectionMode(
            static_cast<WindowsGuestRedZoneProtectionMode>(
                windowsGuestRedZoneProtectionModeSetting),
            true);
        EmulatorSettings.SetDevKit(devkitConsoleSetting, true);
        EmulatorSettings.SetNeo(neoModeSetting, true);
        EmulatorSettings.SetShadNetEnabled(shadnetEnabledSetting, true);
        EmulatorSettings.SetConnectedToNetwork(connectedNetworkSetting, true);
        EmulatorSettings.SetPipelineCacheEnabled(pipelineCacheEnabledSetting, true);
        EmulatorSettings.SetPipelineCacheArchived(pipelineCacheArchiveSetting, true);
        EmulatorSettings.SetExtraDmemInMBytes(extraDmemSetting, true);
        EmulatorSettings.SetVblankFrequency(vblankFrequencySetting, true);
    }

    /////////// Users Tab (the server address is a global setting)
    if (!isSpecific && !isGameRunning) {
        EmulatorSettings.SetShadNetServer(shadnetServerSetting);
    }

    isSpecific ? EmulatorSettings.Save(profile) : EmulatorSettings.Save();

    /////////// Mods Tab
    if (!modSerial.empty() && !mods.empty()) {
        Core::Mods::SaveChoices(modSerial, modChoices);
    }

    /////////// Users Tab
    if (!isSpecific && !isGameRunning) {
        SaveUsersTab();
    }
}

void SettingsWindow::SaveInstallDirs() {
    std::string profile;
    const bool isGlobal = currentProfile == "Global";
    if (!isGlobal) {
        profile = currentProfile.substr(0, 9);
        EmulatorSettings.Load();
    }

    EmulatorSettings.SetAllGameInstallDirs(m_GameInstallDirs);
    EmulatorSettings.Save();

    if (!isGlobal) {
        EmulatorSettings.Load(profile);
    }

    if (!isGameRunning) {
        GetProfileInfo();
    }
}

void SettingsWindow::GetProfileInfo() {
    GetGameIconInfo(profileIcons);

    BigPictureMode::IconInfo global;
    global.title = "Global";
    profileIcons.emplace(profileIcons.begin(), global);
}

SettingsWindow::SettingsWindow(bool gameRunning) : isGameRunning(gameRunning) {
    auto resource = cmrc::res::get_filesystem();
    auto loadTexture = [&](const std::string& resourcePath,
                           std::variant<SDL_Texture*, ImGui::RefCountedTexture>& texture) {
        auto file = resource.open(resourcePath);
        std::vector<u8> texData = std::vector<u8>(file.begin(), file.end());
        gameRunning ? texture = ImGui::RefCountedTexture::DecodePngTexture(texData)
                    : texture = BigPictureMode::LoadSdlTextureData(texData);
    };

    loadTexture("src/resources/big_picture/settings.png", generalTexture);
    loadTexture("src/resources/big_picture/experimental.png", experimentalTexture);
    loadTexture("src/resources/big_picture/graphics.png", graphicsTexture);
    loadTexture("src/resources/big_picture/controller.png", inputTexture);
    loadTexture("src/resources/big_picture/trophy.png", trophyTexture);
    loadTexture("src/resources/big_picture/log.png", logTexture);
    loadTexture("src/resources/big_picture/folder.png", foldersTexture);
    loadTexture("src/resources/big_picture/profiles.png", profilesTexture);
    loadTexture("src/resources/big_picture/mods.png", modsTexture);
    loadTexture("src/resources/big_picture/users.png", usersTexture);

    auto languageKeys = std::views::keys(languageMap);
    languageOptions.assign(languageKeys.begin(), languageKeys.end());

    currentProfile = "Global";
    m_GameInstallDirs = EmulatorSettings.GetAllGameInstallDirs();
    currentCategory = isGameRunning ? SettingsCategory::General : SettingsCategory::Profiles;

    bool customConfigFound = false;
    if (isGameRunning) {
        runningGameSerial = std::string(Common::ElfInfo::Instance().GameSerial());
        std::filesystem::path customConfigFile =
            Common::FS::GetUserPath(Common::FS::PathType::CustomConfigs) /
            (runningGameSerial + ".json");

        if (std::filesystem::exists(customConfigFile)) {
            customConfigFound = true;
            currentProfile =
                runningGameSerial + " - " + std::string(Common::ElfInfo::Instance().Title());
        }
    } else {
        GetProfileInfo();
    }

    customConfigFound ? LoadSettings(runningGameSerial) : LoadSettings("Global");
}

void SettingsWindow::Prepare() {
    uiScale = EmulatorSettings.GetBigPictureScale() / 1000.f;
}

void SettingsWindow::DeInit() {
    EmulatorSettings.Load();
    EmulatorSettings.SetBigPictureScale(static_cast<int>(uiScale * 1000));
    EmulatorSettings.Save();

    if (isGameRunning && !runningGameSerial.empty()) {
        EmulatorSettings.Load(runningGameSerial);
    }
}
void SettingsWindow::DrawSettings(bool* open, const std::function<void()>& applySettings) {
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.06f, 0.06f, 1.00f)); // black
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.40f, 0.70f, 1.00f));   // blue
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                          ImVec4(0.25f, 0.50f, 0.85f, 1.00f)); // lighter blue
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,
                          ImVec4(0.26f, 0.59f, 0.98f, 0.80f)); // another light blue
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,
                          ImVec4(0.26f, 0.59f, 0.98f, 0.80f)); // another light blue

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f * uiScale);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f * uiScale, 10.0f * uiScale));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f * uiScale, 10.0f * uiScale));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.5f * uiScale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f * uiScale, 20.0f * uiScale));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 20.0f * uiScale);

    SetupWindow();
    DrawCategoryTabs();
    DrawMainContent(open, applySettings);

    ImGui::PopStyleVar(8);
    ImGui::PopStyleColor(5);

    ImGui::End();
}

void SettingsWindow::SetupWindow() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGui::Begin("Settings", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::DrawPrettyBackground();
    ImGui::SetWindowFontScale(uiScale);

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }

    // Navigate the visible categories with Tab / R1 / L1 (text boxes keep Tab for themselves)
    const auto visible = VisibleCategories();
    const auto found = std::ranges::find(visible, currentCategory);
    const int count = static_cast<int>(visible.size());
    const int index = found == visible.end() ? 0 : static_cast<int>(found - visible.begin());
    if (found == visible.end()) {
        currentCategory = visible.front();
    }
    const bool typing = ImGui::GetIO().WantTextInput;
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1) ||
        (!typing && ImGui::IsKeyPressed(ImGuiKey_Tab))) {
        currentCategory = visible[(index + 1) % count];
    }

    if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1)) {
        currentCategory = visible[(index + count - 1) % count];
    }
}

std::vector<SettingsWindow::SettingsCategory> SettingsWindow::VisibleCategories() const {
    std::vector<SettingsCategory> visible;
    if (!isGameRunning) {
        visible.push_back(SettingsCategory::Profiles);
    }
    visible.insert(visible.end(), {SettingsCategory::General, SettingsCategory::Graphics,
                                   SettingsCategory::Input, SettingsCategory::Trophy,
                                   SettingsCategory::Folders, SettingsCategory::Log});
    const bool isGlobal = currentProfile == "Global";
    if (isGlobal && !isGameRunning) {
        visible.push_back(SettingsCategory::Users);
    }
    if (!isGlobal) {
        visible.push_back(SettingsCategory::Experimental);
    }
    if (!modSerial.empty()) {
        visible.push_back(SettingsCategory::Mods);
    }
    return visible;
}

void SettingsWindow::DrawCategoryTabs() {
    ImVec4 settingsColor = ImVec4(0.1f, 0.1f, 0.12f, 0.8f); // Darker gray
    ImGui::PushStyleColor(ImGuiCol_ChildBg, settingsColor);

    float vertSize = (settingsIconSize * uiScale + ImGui::CalcTextSize("Profiles").y) +
                     ImGui::GetStyle().FramePadding.y * 4.f + 20.0 * uiScale;
    ImGuiWindowFlags child_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NavFlattened;
    ImGui::BeginChild("Categories", ImVec2(0, vertSize), true,
                      child_flags | ImGuiWindowFlags_HorizontalScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(30.0f * uiScale, 0.0f));

    for (const SettingsCategory category : VisibleCategories()) {
        switch (category) {
        case SettingsCategory::Profiles:
            AddCategory("Profiles", profilesTexture, category);
            break;
        case SettingsCategory::General:
            AddCategory("General", generalTexture, category);
            break;
        case SettingsCategory::Graphics:
            AddCategory("Graphics", graphicsTexture, category);
            break;
        case SettingsCategory::Input:
            AddCategory("Input", inputTexture, category);
            break;
        case SettingsCategory::Trophy:
            AddCategory("Trophy", trophyTexture, category);
            break;
        case SettingsCategory::Folders:
            AddCategory("Game Folders", foldersTexture, category);
            break;
        case SettingsCategory::Log:
            AddCategory("Log", logTexture, category);
            break;
        case SettingsCategory::Users:
            AddCategory("Users", usersTexture, category);
            break;
        case SettingsCategory::Experimental:
            AddCategory("Experimental", experimentalTexture, category);
            break;
        case SettingsCategory::Mods:
            AddCategory("Mods", modsTexture, category);
            break;
        }
    }

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::EndChild();
}

void SettingsWindow::AddCategory(std::string name,
                                 std::variant<SDL_Texture*, ImGui::RefCountedTexture> texture,
                                 SettingsCategory category) {
    ImGui::SameLine();
    ImGui::BeginGroup();

    // make button appear hovered as long as category is selected, otherwise dull its hovered color
    currentCategory == category
        ? ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered])
        : ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.235f, 0.392f, 0.624f, 1.00f));

    ImTextureID id = nullptr;
    if (std::holds_alternative<SDL_Texture*>(texture)) {
        id = ImTextureID(std::get<SDL_Texture*>(texture));
    } else if (std::holds_alternative<ImGui::RefCountedTexture>(texture)) {
        id = std::get<ImGui::RefCountedTexture>(texture).GetTexture().im_id;
    }

    if (id != nullptr) {
        if (ImGui::ImageButton(name.c_str(), id,
                               ImVec2(settingsIconSize * uiScale, settingsIconSize * uiScale))) {
            currentCategory = category;
        }
    }

    ImGui::PopStyleColor();

    ImGui::SetCursorPosX(
        (ImGui::GetCursorPosX() +
         (settingsIconSize * uiScale - ImGui::CalcTextSize(name.c_str()).x) * 0.5f) +
        ImGui::GetStyle().FramePadding.x);
    ImGui::Text("%s", name.c_str());
    ImGui::EndGroup();
}

void SettingsWindow::DrawMainContent(bool* open, const std::function<void()>& applySettings) {
    ImVec4 settingsColor = ImVec4(0.1f, 0.1f, 0.12f, 0.8f); // Darker gray
    ImGui::PushStyleColor(ImGuiCol_ChildBg, settingsColor);
    ImGuiWindowFlags child_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NavFlattened;

    std::string centeredText = "Selected Profile: " + currentProfile;
    if (currentCategory == SettingsCategory::Folders) {
        centeredText = "Manage shadPS4 Game Folders";
    } else if (currentCategory == SettingsCategory::Users) {
        centeredText = "Users and shadNet accounts";
    } else if (currentCategory == SettingsCategory::Mods) {
        centeredText = "Mods for " + modSerial;
    }

    ImGui::Separator();
    Overlay::TextCentered(centeredText.c_str());
    ImGui::Separator();

    if (currentCategory == SettingsCategory::Profiles) {
        DrawProfileSelector();
    } else if (currentCategory == SettingsCategory::Folders) {
        DrawGameFolderManager();
    } else if (currentCategory == SettingsCategory::Mods) {
        DrawModsTab();
    } else if (currentCategory == SettingsCategory::Users) {
        DrawUsersTab();
    } else {
        DrawSettingsTable(currentCategory);
    }

    ImGui::PopStyleColor();

    ImGui::Separator();
    ImGui::SetNextItemWidth(300.0f * uiScale);
    static float sliderScale2 = 1.0f;
    if (ImGui::IsWindowAppearing()) {
        sliderScale2 = uiScale;
    }

    ImGui::SliderFloat("UI Scale", &sliderScale2, 0.25f, 3.0f);
    // Only update when user is not interacting with slider
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        uiScale = sliderScale2;
    }
    ImGui::SameLine();

    // Align buttons right
    float buttonsWidth = ImGui::CalcTextSize("Save").x + ImGui::CalcTextSize("Cancel").x +
                         ImGui::CalcTextSize("Apply").x + ImGui::GetStyle().FramePadding.x * 6.0f +
                         ImGui::GetStyle().ItemSpacing.x * 2;
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - buttonsWidth);

    if (ImGui::Button("Save")) {
        closeOnSave = true;
        ImGui::OpenPopup("Save Confirmation");
    }

    ImGui::SameLine();
    if (ImGui::Button("Apply")) {
        ImGui::OpenPopup("Save Confirmation");
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Save Confirmation", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", ("Profile Saved:\n" + currentProfile).c_str());
        ImGui::Separator();

        if (ImGui::Button("OK", ImVec2(250 * uiScale, 0))) {
            std::string profile = currentProfile;
            if (currentProfile != "Global") {
                profile = currentProfile.substr(0, 9);
            }

            SaveSettings(profile);
            if (closeOnSave) {
                DeInit();
                *open = false;
                closeOnSave = false;
                ImGui::CloseCurrentPopup();
            } else {
                ImGui::CloseCurrentPopup();
            }
            if (applySettings) {
                applySettings();
            }
        }

        ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        DeInit();
        *open = false;
        if (applySettings) {
            applySettings();
        }
    }
}

void SettingsWindow::DrawProfileSelector() {
    ImGuiWindowFlags child_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NavFlattened;
    ImVec4 settingsColor = ImVec4(0.1f, 0.1f, 0.12f, 0.8f); // Darker gray
    ImGui::PushStyleColor(ImGuiCol_ChildBg, settingsColor);
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);

    ImGui::BeginChild("Profile Selection", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), true,
                      child_flags);
    ImGui::PopStyleColor();

    if (ImGui::BeginTable("ProfilesTable", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("CreateDeleteButton", ImGuiTableColumnFlags_WidthFixed,
                                400.0f * uiScale);
        ImGui::TableSetupColumn("Profile");

        for (int i = 0; i < profileIcons.size(); i++) {
            const std::filesystem::path customConfigFile =
                Common::FS::GetUserPath(Common::FS::PathType::CustomConfigs) /
                (profileIcons[i].serial + ".json");
            const bool gameConfigExists = std::filesystem::exists(customConfigFile);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            std::string deleteButtonLabel = "Delete game-specific config##" + std::to_string(i);

            if (gameConfigExists) {
                // Different shades of red depending on button state
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.0f, 0.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.0f, 0.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.0f, 0.0f, 1.0f));

                if (ImGui::Button(deleteButtonLabel.c_str(),
                                  ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
                    deleteProfileIndex = i;
                }

                ImGui::PopStyleColor(3);
            }

            ImGui::TableNextColumn();
            std::string profileLabel =
                i == 0 ? "Global" : profileIcons[i].serial + " - " + profileIcons[i].title;
            if (ImGui::Button(profileLabel.c_str(),
                              ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

                currentProfile = profileLabel;
                if (currentProfile == "Global") {
                    LoadSettings("Global");
                } else {
                    LoadSettings(profileIcons[i].serial);
                    if (!gameConfigExists) {
                        SaveSettings(profileIcons[i].serial);
                    }
                }
            }
        }

        ImGui::EndTable();
    }

    if (deleteProfileIndex != -1) {
        ImGui::OpenPopup("Confirm Delete");
    }

    ImGui::PopStyleVar(3);
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Confirm Delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::string title = profileIcons[deleteProfileIndex].title;
        const std::string message = "Delete game-specific config file for " + title + "?";
        const std::filesystem::path path =
            Common::FS::GetUserPath(Common::FS::PathType::CustomConfigs) /
            (profileIcons[deleteProfileIndex].serial + ".json");

        ImGui::Text("%s", message.c_str());
        ImGui::Separator();

        if (ImGui::Button("OK", ImVec2(120 * uiScale, 0))) {
            try {
                std::filesystem::remove(path);
            } catch (const std::exception& e) {
                LOG_ERROR(ImGui, "Could not delete config file {}: {}", path.string(), e.what());
            }

            deleteProfileIndex = -1;
            currentProfile = "Global";
            LoadSettings("Global");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(120 * uiScale, 0))) {
            deleteProfileIndex = -1;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::EndChild();
}

void SettingsWindow::DrawGameFolderManager() {
    ImGuiWindowFlags child_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NavFlattened;
    ImGui::BeginChild("ContentRegion", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), true,
                      child_flags);

    if (ImGui::Button("Add Folder", ImVec2(400.f * uiScale, 0))) {
        ImGuiFileDialog::Instance()->OpenDialog("OpenFolder", "Add shadPS4 game folder", nullptr,
                                                ".", 1, nullptr,
                                                ImGuiFileDialogFlags_DisableCreateDirectoryButton |
                                                    ImGuiFileDialogFlags_DontShowHiddenFiles);

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
    }

    if (ImGuiFileDialog::Instance()->Display("OpenFolder", child_flags | ImGuiWindowFlags_NoMove)) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            GameInstallDir dir;
            dir.path = ImGuiFileDialog::Instance()->GetCurrentPath();
            dir.enabled = true;

#ifdef WIN32
            // replace \ with / on windows
            std::string pathString = dir.path.string();
            size_t pos = 0;
            while ((pos = pathString.find("\\", pos)) != std::string::npos) {
                pathString.replace(pos, 1, "/");
                pos += 2;
            }

            dir.path = pathString;
#endif

            m_GameInstallDirs.push_back(dir);
            SaveInstallDirs();
        }

        ImGuiFileDialog::Instance()->Close();
    }

    ImGui::BeginChild("Game Folder List", ImVec2(0, 0), true, child_flags);

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 5.0f * uiScale));
    if (ImGui::BeginTable("FoldersTable", 3)) {
        ImGui::TableSetupColumn("FolderButton", ImGuiTableColumnFlags_WidthFixed, 300.0f * uiScale);
        ImGui::TableSetupColumn("FolderEnabled", ImGuiTableColumnFlags_WidthFixed, 0.0f);
        ImGui::TableSetupColumn("FolderPath");

        for (int i = 0; i < m_GameInstallDirs.size(); i++) {
            std::string buttonLabel = "Remove Folder##" + std::to_string(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Button(buttonLabel.c_str(), ImVec2(280 * uiScale, 0))) {
                m_GameInstallDirs.erase(m_GameInstallDirs.begin() + i);
                SaveInstallDirs();
            }

            ImGui::TableNextColumn();
            std::string checkboxLabel = "##EnableFolder" + std::to_string(i);
            bool previousState = m_GameInstallDirs[i].enabled;
            ImGui::Checkbox(checkboxLabel.c_str(), &m_GameInstallDirs[i].enabled);
            ImGui::SameLine();
            ImGui::Dummy(ImVec2(5.0 * uiScale, 0));

            if (m_GameInstallDirs[i].enabled != previousState) {
                SaveInstallDirs();
            }

            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", m_GameInstallDirs[i].path.string().c_str());
        }

        ImGui::EndTable();
    }

    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::EndChild();
}

void SettingsWindow::DrawSettingsTable(SettingsCategory category) {
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f * uiScale, 10.0f * uiScale));
    ImGuiWindowFlags child_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NavFlattened;

    ImGui::BeginChild("ContentRegion", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), true,
                      child_flags);

    if (category == SettingsCategory::General) {
        if (ImGui::BeginTable("SettingsTable", 2)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 500.0f * uiScale);
            ImGui::TableSetupColumn("Value");

            AddSettingCombo("Console Language", consoleLanguageSetting, languageOptions);
            AddSettingSliderInt("Volume", volumeSetting, 0, 500);
            AddSettingCheckbox("Show Splash Screen When Launching Game", showSplashSetting);
            AddSettingCombo("Audio Backend", audioBackendSetting, audioBackendOptions);

            ImGui::EndTable();
        }
    } else if (category == SettingsCategory::Graphics) {
        if (ImGui::BeginTable("SettingsTable", 2)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 500.0f * uiScale);
            ImGui::TableSetupColumn("Value");

            AddSettingCombo("Display Mode", fullscreenModeSetting, fullscreenModeOptions);
            AddSettingCombo("Present Mode", presentModeSetting, presentModeOptions);
            AddSettingSliderInt("Window Width", windowWidthSetting, 0, 8000);
            AddSettingSliderInt("Window Height", windowHeightSetting, 0, 7000);
            AddSettingCheckbox("Enable HDR", hdrAllowedSetting);
            AddSettingCheckbox("Enable FSR", fsrEnabledSetting);

            if (fsrEnabledSetting) {
                AddSettingCheckbox("Enable RCAS", rcasEnabledSetting);
            }

            if (rcasEnabledSetting && fsrEnabledSetting) {
                AddSettingSliderFloat("RCAS Attenuation", rcasAttenuationSetting, 0.0f, 3.0f, 3);
            }

            ImGui::EndTable();
        }
    } else if (category == SettingsCategory::Input) {
        if (ImGui::BeginTable("SettingsTable", 2)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 500.0f * uiScale);
            ImGui::TableSetupColumn("Value");

            AddSettingCheckbox("Enable Motion Controls", motionControlsSetting);
            AddSettingCheckbox("Enable Background Controller Input", backgroundControllerSetting);
            AddSettingCombo("Hide Cursor", cursorStateSetting, hideCursorOptions);

            if (cursorStateSetting == 1) {
                AddSettingSliderInt("Hide Cursor Idle Timeout", cursorTimeoutSetting, 1, 10);
            }

            ImGui::EndTable();
        }
    } else if (category == SettingsCategory::Trophy) {
        if (ImGui::BeginTable("SettingsTable", 2)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 500.0f * uiScale);
            ImGui::TableSetupColumn("Value");

            AddSettingCheckbox("Disable Trophy Notification", trophyPopupDisabledSetting);
            if (!trophyPopupDisabledSetting) {
                AddSettingCombo("Trophy Notification Position", trophySideSetting,
                                trophySideOptions);
                AddSettingSliderFloat("Trophy Notification Duration", trophyDurationSetting, 0.f,
                                      10.f, 1);
            }

            ImGui::EndTable();
        }
    } else if (category == SettingsCategory::Log) {
        if (ImGui::BeginTable("SettingsTable", 2)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 500.0f * uiScale);
            ImGui::TableSetupColumn("Value");

            AddSettingCheckbox("Enable Logging", logEnableSetting);
            if (logEnableSetting) {
                AddSettingCheckbox("Separate Log Files", logSeparateSetting);
                AddSettingCheckbox("Log Sync", logSyncSetting);
            }

            ImGui::EndTable();
        }
    } else if (category == SettingsCategory::Experimental) {
        if (ImGui::BeginTable("SettingsTable", 2)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 500.0f * uiScale);
            ImGui::TableSetupColumn("Value");

            AddSettingSliderInt("Additional DMem Allocation", extraDmemSetting, 0, 20000);
            AddSettingSliderInt("Vblank Frequency", vblankFrequencySetting, 30, 360);
            AddSettingCombo("Readbacks Mode", readbacksModeSetting, readbacksModeOptions);
            AddSettingCheckbox("Enable Readback Linear Images", readbackLinearImagesSetting);
            AddSettingCheckbox("Enable Direct Memory Access", directMemoryAccessSetting);
#ifdef _WIN32
            // Windows static guest red-zone protection
            AddSettingCombo("Windows Guest Red Zone Protection (Requires Restart)",
                            windowsGuestRedZoneProtectionModeSetting,
                            windowsGuestRedZoneProtectionModeOptions);
#endif
            AddSettingCheckbox("Enable Devkit Console Mode", devkitConsoleSetting);
            AddSettingCheckbox("Enable PS4 Neo Mode", neoModeSetting);
            AddSettingCheckbox("Enable ShadNet", shadnetEnabledSetting);
            AddSettingCheckbox("Set Network Connected to True", connectedNetworkSetting);
            AddSettingCheckbox("Enable Shader Cache", pipelineCacheEnabledSetting);

            if (pipelineCacheEnabledSetting) {
                AddSettingCheckbox("Compress Shader Cache to Zip File",
                                   pipelineCacheArchiveSetting);
            }

            ImGui::EndTable();
        }
    }

    ImGui::PopStyleVar();
    ImGui::EndChild();
}

void SettingsWindow::AddSettingCheckbox(std::string name, bool& value) {
    std::string label = "##" + name;
    ImGui::TableNextRow();
    ImGui::TableNextColumn();

    ImGui::TextWrapped("%s", name.c_str());
    ImGui::TableNextColumn();
    ImGui::Checkbox(label.c_str(), &value);
}

void SettingsWindow::AddSettingSliderInt(std::string name, int& value, int min, int max) {
    std::string label = "##" + name;
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextWrapped("%s", name.c_str());

    ImGui::TableNextColumn();
    ImGui::SliderInt(label.c_str(), &value, min, max);
}

void SettingsWindow::AddSettingSliderFloat(std::string name, float& value, int min, int max,
                                           int precision) {
    std::string label = "##" + name;
    std::string precisionString = "%." + std::to_string(precision) + "f";

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextWrapped("%s", name.c_str());

    ImGui::TableNextColumn();
    ImGui::SliderFloat(label.c_str(), &value, min, max, precisionString.c_str());
}

void SettingsWindow::AddSettingCombo(std::string name, int& value,
                                     std::vector<std::string> options) {
    std::string label = "##" + name;
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextWrapped("%s", name.c_str());

    ImGui::TableNextColumn();
    const char* combo_value = options[value].c_str();
    if (ImGui::BeginCombo(label.c_str(), combo_value)) {
        for (int i = 0; i < options.size(); i++) {
            const bool selected = (i == value);
            if (ImGui::Selectable(options[i].c_str(), selected))
                value = i;

            // Set the initial focus when opening the combo
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

// ---------------------------------------------------------------- Mods / Users tabs (BBSC fork)

static bool InputString(const char* label, std::string& value, ImGuiInputTextFlags flags = 0) {
    char buffer[512];
    std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
    if (ImGui::InputText(label, buffer, sizeof(buffer), flags)) {
        value = buffer;
        return true;
    }
    return false;
}

static void OpenFolder(const std::filesystem::path& folder) {
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    SDL_OpenURL(("file:///" + folder.generic_string()).c_str());
}

void SettingsWindow::DrawModSetting(const Core::Mods::SettingDef& def, std::string& value) {
    const std::string label = "##mod_" + def.id;
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextWrapped("%s", def.name.c_str());
    if (!def.description.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::TextWrapped("%s", def.description.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::TableNextColumn();
    switch (def.type) {
    case Core::Mods::SettingType::Bool: {
        bool on = value == "1";
        if (ImGui::Checkbox(label.c_str(), &on)) {
            value = on ? "1" : "0";
        }
        break;
    }
    case Core::Mods::SettingType::Int: {
        int number = std::atoi(value.c_str());
        if (ImGui::SliderInt(label.c_str(), &number, def.min, def.max)) {
            value = std::to_string(number);
        }
        break;
    }
    case Core::Mods::SettingType::Choice: {
        if (def.options.empty()) {
            break;
        }
        int index = GetComboIndex(value, def.options);
        if (ImGui::BeginCombo(label.c_str(), def.options[index].c_str())) {
            for (int i = 0; i < static_cast<int>(def.options.size()); i++) {
                if (ImGui::Selectable(def.options[i].c_str(), i == index)) {
                    value = def.options[i];
                }
            }
            ImGui::EndCombo();
        }
        break;
    }
    case Core::Mods::SettingType::Text:
        ImGui::SetNextItemWidth(-1.0f);
        InputString(label.c_str(), value);
        break;
    }
}

void SettingsWindow::DrawModsTab() {
    ImGuiWindowFlags child_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NavFlattened;
    ImGui::BeginChild("ContentRegion", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), true,
                      child_flags);

    const auto modsFolder = Common::FS::GetUserPath(Common::FS::PathType::ModsDir);
    if (ImGui::Button("Open Mods Folder", ImVec2(400.f * uiScale, 0))) {
        OpenFolder(modsFolder);
    }
    ImGui::SameLine();
    if (ImGui::Button("Rescan", ImVec2(250.f * uiScale, 0))) {
        // Pick up new mod folders, keeping what was already changed here.
        mods = Core::Mods::ModsForGame(modSerial, modChoices);
        selectedMod = 0;
    }
    if (isGameRunning) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
                           "Changes take effect the next time the game starts.");
    }

    if (mods.empty()) {
        ImGui::TextWrapped("No mods for this game. Put a mod folder (DLL, PRX, patch XML or game "
                           "files) in %s\\%s", modsFolder.string().c_str(), modSerial.c_str());
        ImGui::EndChild();
        return;
    }
    selectedMod = std::clamp(selectedMod, 0, static_cast<int>(mods.size()) - 1);
    int moveMod = 0;

    if (ImGui::BeginTable("ModsLayout", 2, ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("ModList", ImGuiTableColumnFlags_WidthFixed, 520.0f * uiScale);
        ImGui::TableSetupColumn("ModDetails");
        ImGui::TableNextRow();

        // Left: every mod with its on/off switch
        ImGui::TableNextColumn();
        for (int i = 0; i < static_cast<int>(mods.size()); i++) {
            const auto& mod = mods[i];
            auto& choice = modChoices[mod.id];
            ImGui::PushID(i);
            ImGui::Checkbox("##enabled", &choice.enabled);
            ImGui::SameLine();
            const std::string label = std::to_string(i + 1) + ". " + mod.name + "  [" +
                                      mod.Kinds() + "]";
            if (ImGui::Selectable(label.c_str(), selectedMod == i)) {
                selectedMod = i;
            }
            if (!mod.problem.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "  %s", mod.problem.c_str());
            }
            ImGui::PopID();
        }

        // Right: the selected mod and its settings
        ImGui::TableNextColumn();
        const auto& mod = mods[selectedMod];
        auto& choice = modChoices[mod.id];
        ImGui::Text("%s", mod.name.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::TextWrapped("Version: %s%s%s", mod.version.empty() ? "-" : mod.version.c_str(),
                           mod.author.empty() ? "" : "   By: ", mod.author.c_str());
        ImGui::TextWrapped("Type: %s", mod.Kinds().c_str());
        ImGui::PopStyleColor();
        if (!mod.description.empty()) {
            ImGui::TextWrapped("%s", mod.description.c_str());
        }
        // Load order: higher in the list wins when two mods change the same game file.
        if (ImGui::Button("Move Up")) {
            moveMod = -1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Move Down")) {
            moveMod = 1;
        }
        if (!mod.problem.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Cannot run: %s",
                               mod.problem.c_str());
        }
        ImGui::Checkbox("Enabled##selected", &choice.enabled);

        if (!mod.settings.empty()) {
            ImGui::Separator();
            if (!choice.enabled) {
                ImGui::BeginDisabled();
            }
            if (ImGui::BeginTable("ModSettings", 2)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.4f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                for (const auto& def : mod.settings) {
                    DrawModSetting(def, choice.values[def.id]);
                }
                ImGui::EndTable();
            }
            if (!choice.enabled) {
                ImGui::EndDisabled();
            }
        }
        ImGui::EndTable();
    }

    // Load order change, applied after drawing so nothing above points at a moved entry.
    if (const int target = selectedMod + moveMod;
        moveMod != 0 && target >= 0 && target < static_cast<int>(mods.size())) {
        std::swap(modChoices[mods[selectedMod].id].order, modChoices[mods[target].id].order);
        std::swap(mods[selectedMod], mods[target]);
        selectedMod = target;
    }

    ImGui::EndChild();
}

void SettingsWindow::LoadUsersTab() {
    editUsers = UserManagement.GetUsers().user;
    selectedUser = 0;
    const User* defaultUser = UserManagement.GetUserByPlayerIndex(1);
    defaultUserId = defaultUser != nullptr ? defaultUser->user_id : -1;
    shadnetServerSetting = EmulatorSettings.GetShadNetServer();

    // Server cards: <user>/servers/*.json = {"name", "shadnet": "host:port", "registration_key"}
    serverCards.clear();
    selectedCard = -1;
    const auto folder = Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "servers";
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (entry.path().extension() != ".json") {
            continue;
        }
        try {
            std::ifstream file(entry.path());
            const auto j = nlohmann::json::parse(file, nullptr, true, true);
            ServerCard card;
            card.name = j.value("name", entry.path().stem().string());
            card.address = j.value("shadnet", "");
            card.key = j.value("registration_key", "");
            if (!card.address.empty()) {
                serverCards.push_back(std::move(card));
            }
        } catch (const std::exception& e) {
            LOG_ERROR(ImGui, "Could not read server card {}: {}", entry.path().string(), e.what());
        }
    }
    for (int i = 0; i < static_cast<int>(serverCards.size()); i++) {
        if (serverCards[i].address == shadnetServerSetting) {
            selectedCard = i;
            registrationKey = serverCards[i].key;
        }
    }
}

void SettingsWindow::SaveUsersTab() {
    for (const User& edited : editUsers) {
        if (User* user = UserManagement.GetUserByID(edited.user_id)) {
            user->user_name = edited.user_name;
            user->user_color = edited.user_color;
            user->shadnet_enabled = edited.shadnet_enabled;
            user->shadnet_npid = edited.shadnet_npid;
            user->shadnet_password = edited.shadnet_password;
            user->shadnet_email = edited.shadnet_email;
        } else {
            UserManagement.AddUser(edited); // also makes its home / savedata folders
        }
    }
    const User* current = UserManagement.GetUserByPlayerIndex(1);
    if (defaultUserId >= 0 && (current == nullptr || current->user_id != defaultUserId)) {
        UserManagement.SetControllerPort(static_cast<u32>(defaultUserId), 1);
    }
    UserManagement.Save();
}

void SettingsWindow::DrawUsersTab() {
    ImGuiWindowFlags child_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NavFlattened;
    ImGui::BeginChild("ContentRegion", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), true,
                      child_flags);

    // A finished "create account" request
    if (createAccountJob.valid() &&
        createAccountJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const auto result = createAccountJob.get();
        createAccountOk = result.ok;
        createAccountMessage = result.message;
        if (result.ok && selectedUser < static_cast<int>(editUsers.size())) {
            editUsers[selectedUser].shadnet_enabled = true;
        }
    }
    const bool creating = createAccountJob.valid();

    // ---- Server
    if (ImGui::BeginTable("ServerTable", 2)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 500.0f * uiScale);
        ImGui::TableSetupColumn("Value");

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextWrapped("Server card");
        ImGui::TableNextColumn();
        const char* cardName =
            selectedCard >= 0 ? serverCards[selectedCard].name.c_str() : "(type the address)";
        if (ImGui::BeginCombo("##ServerCard", cardName)) {
            if (ImGui::Selectable("(type the address)", selectedCard < 0)) {
                selectedCard = -1;
            }
            for (int i = 0; i < static_cast<int>(serverCards.size()); i++) {
                if (ImGui::Selectable(serverCards[i].name.c_str(), selectedCard == i)) {
                    selectedCard = i;
                    shadnetServerSetting = serverCards[i].address;
                    registrationKey = serverCards[i].key;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Open Cards Folder")) {
            OpenFolder(Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "servers");
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextWrapped("shadNet server (host:port)");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0f);
        if (InputString("##ShadNetServer", shadnetServerSetting)) {
            selectedCard = -1;
        }
        ImGui::EndTable();
    }
    ImGui::Separator();

    if (editUsers.empty()) {
        ImGui::TextWrapped("No users found.");
        ImGui::EndChild();
        return;
    }
    selectedUser = std::clamp(selectedUser, 0, static_cast<int>(editUsers.size()) - 1);

    if (ImGui::BeginTable("UsersLayout", 2, ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("UserList", ImGuiTableColumnFlags_WidthFixed, 520.0f * uiScale);
        ImGui::TableSetupColumn("UserDetails");
        ImGui::TableNextRow();

        // Left: users
        ImGui::TableNextColumn();
        for (int i = 0; i < static_cast<int>(editUsers.size()); i++) {
            const User& user = editUsers[i];
            std::string label = std::to_string(user.user_id) + "  " + user.user_name;
            if (user.user_id == defaultUserId) {
                label += "  (default)";
            }
            if (user.shadnet_enabled && !user.shadnet_npid.empty()) {
                label += "\n      shadNet: " + user.shadnet_npid;
            }
            label += "##user" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), selectedUser == i)) {
                selectedUser = i;
                createAccountMessage.clear();
            }
        }
        if (ImGui::Button("Add User", ImVec2(-1.0f, 0))) {
            s32 nextId = 1000;
            for (const User& user : editUsers) {
                nextId = std::max(nextId, user.user_id + 1);
            }
            User user;
            user.user_id = nextId;
            user.user_name = "User " + std::to_string(nextId);
            user.player_index = -1;
            editUsers.push_back(user);
            selectedUser = static_cast<int>(editUsers.size()) - 1;
            createAccountMessage.clear();
        }

        // Right: the selected user
        ImGui::TableNextColumn();
        User& user = editUsers[selectedUser];
        if (ImGui::BeginTable("UserFields", 2)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.4f);
            const auto row = [](const char* name) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", name);
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1.0f);
            };

            row("Name");
            InputString("##UserName", user.user_name);
            row("Default user (player 1)");
            bool isDefault = user.user_id == defaultUserId;
            if (ImGui::Checkbox("##UserDefault", &isDefault) && isDefault) {
                defaultUserId = user.user_id;
            }
            row("Use shadNet");
            ImGui::Checkbox("##UserShadNet", &user.shadnet_enabled);
            row("shadNet ID");
            InputString("##UserNpid", user.shadnet_npid);
            row("Password");
            InputString("##UserPassword", user.shadnet_password, ImGuiInputTextFlags_Password);
            row("Email (optional)");
            InputString("##UserEmail", user.shadnet_email);
            row("Registration key (from the host)");
            InputString("##RegistrationKey", registrationKey, ImGuiInputTextFlags_Password);
            ImGui::EndTable();
        }

        const bool canCreate = !creating && !user.shadnet_npid.empty() &&
                               !user.shadnet_password.empty() && !shadnetServerSetting.empty();
        if (!canCreate) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Create Account on Server", ImVec2(-1.0f, 0))) {
            std::string host;
            u16 port = 0;
            if (!ShadNet::SplitServerAddress(shadnetServerSetting, host, port)) {
                createAccountOk = false;
                createAccountMessage = "The server address should look like host:31313.";
            } else {
                if (user.shadnet_email.empty()) {
                    user.shadnet_email = user.shadnet_npid + "@shadnet.local";
                }
                createAccountMessage.clear();
                createAccountJob = std::async(
                    std::launch::async,
                    [host, port, npid = user.shadnet_npid, password = user.shadnet_password,
                     email = user.shadnet_email, key = registrationKey] {
                        return ShadNet::CreateAccount(host, port, npid, password, email, key);
                    });
            }
        }
        if (!canCreate) {
            ImGui::EndDisabled();
        }

        if (creating) {
            ImGui::TextWrapped("Contacting the server...");
        } else if (!createAccountMessage.empty()) {
            ImGui::TextColored(createAccountOk ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                                               : ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               "%s", createAccountMessage.c_str());
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::TextWrapped("The account is made on the server above, so it works from any PC. "
                           "Press Save to keep the changes on this page.");
        ImGui::PopStyleColor();
        ImGui::EndTable();
    }

    ImGui::EndChild();
}

} // namespace ImGuiEmuSettings
