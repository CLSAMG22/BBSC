// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <QFutureWatcher>
#include <QWidget>

#include "qt_gui/bbsc_mod_scan.h"

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QVBoxLayout;
class gui_settings;
class IpcClient;

// BBSC additions to the settings dialog. Both tabs read and write the same files as the BBSC
// emulator build (src-shadp2p): <user>/mods/ (layout in bbsc_mod_scan.h),
// <user>/mods/_settings/<serial>.json, <user>/servers/*.json, users.json, shadnet_server.
namespace Bbsc {

// Launcher start: a BBSC package ships emulator\shadPS4.exe and mods\ next to the launcher. The
// emulator is added to the Version Manager as "BBSC" and selected once (the user's later choice
// stays); the bundled mods are copied into <user>/mods (newer files replace older ones).
void SetUpBundle(const std::shared_ptr<gui_settings>& settings);

// Launcher start: LICENSE.txt (the GPL) and NOTICE.txt (copyright + source link) must be next to
// the launcher, so a copy passed on keeps them. Missing or emptied -> message box, false (main
// quits).
bool NoticesPresent();

// Mods tab (game-specific settings): every mod for the game with its switch, load order and
// settings; installs mods from a .zip / folder / single file.
class ModsTab : public QWidget {
    Q_OBJECT
public:
    ModsTab(const std::string& serial, QWidget* parent = nullptr);
    void Save();

private:
    void Rescan(const std::string& select_id = {});
    void FillList();
    void ShowMod(int row);
    void Move(int step);
    void FindConflicts();
    void Install(const QString& source, bool is_archive);

    std::string m_serial;
    std::vector<BbscMods::ModInfo> m_mods;
    BbscMods::Choices m_choices;
    std::map<std::string, QStringList> m_conflicts; // mod id -> lines
    int m_shown = -1;

    QListWidget* m_list = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_where = nullptr;
    QLabel* m_details = nullptr;
    QWidget* m_settingsHost = nullptr;
    QFormLayout* m_settingsForm = nullptr;
};

// Patches tab (game-specific settings): the downloaded shadPS4 / GoldHEN patches for this game
// and version, switched on/off in their XML files (like the Cheats / Patches window).
class PatchesTab : public QWidget {
    Q_OBJECT
public:
    PatchesTab(const std::string& serial, std::shared_ptr<gui_settings> gui,
               std::shared_ptr<IpcClient> ipc, QWidget* parent = nullptr);
    void Save();

private:
    struct PatchEntry {
        QString file, name, appVer;
        QCheckBox* box = nullptr;
    };
    void Load();
    void Download();

    std::string m_serial;
    QString m_version;
    std::shared_ptr<gui_settings> m_gui;
    std::shared_ptr<IpcClient> m_ipc;
    std::vector<PatchEntry> m_entries;
    QPushButton* m_download = nullptr;
    QLabel* m_status = nullptr;
    QWidget* m_listHost = nullptr;
    QVBoxLayout* m_listLayout = nullptr;
};

// Accounts tab (global settings): shadNet server, server cards, account creation on the server.
class AccountsTab : public QWidget {
    Q_OBJECT
public:
    explicit AccountsTab(QWidget* parent = nullptr);
    void Save(); // before EmulatorSettings.Save(): writes the server address into it

private:
    struct ServerCard {
        QString name, address, key;
    };
    void LoadCards();
    void FillUsers();
    void CreateAccount();

    std::vector<ServerCard> m_cards;
    QComboBox* m_cardBox = nullptr;
    QLineEdit* m_server = nullptr;
    QComboBox* m_userBox = nullptr;
    QLineEdit* m_npid = nullptr;
    QLineEdit* m_password = nullptr;
    QLineEdit* m_email = nullptr;
    QLineEdit* m_key = nullptr;
    QCheckBox* m_useForUser = nullptr;
    QPushButton* m_create = nullptr;
    QLabel* m_result = nullptr;
    QFutureWatcher<std::pair<bool, QString>> m_job;
};

} // namespace Bbsc
