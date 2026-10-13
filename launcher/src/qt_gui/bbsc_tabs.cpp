// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <fstream>
#include <set>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDate>
#include <QDesktopServices>
#include <QFile>
#include <QScrollArea>
#include <QTextStream>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTcpSocket>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>
#include <nlohmann/json.hpp>

#include "common/logging/log.h"
#include "common/path_util.h"
#include "common/versions.h"
#include "core/emulator_settings.h"
#include "core/file_format/psf.h"
#include "core/user_settings.h"
#include "qt_gui/bbsc_tabs.h"
#include "qt_gui/cheats_patches.h"
#include "qt_gui/gui_settings.h"
#include "qt_gui/user_manager_dialog.h"

namespace Bbsc {

namespace {

using nlohmann::json;
namespace fs = std::filesystem;
using BbscMods::ModInfo;
using BbscMods::SettingType;

fs::path UserDir() {
    return Common::FS::GetUserPath(Common::FS::PathType::UserDir);
}
fs::path ModsDir() {
    return BbscMods::ModsRoot(UserDir()); // <user>\mods, or the folder chosen with Mods Folder...
}

QString Q(const std::string& s) {
    return QString::fromStdString(s);
}

QString QPath(const fs::path& path) {
    QString out;
    Common::FS::PathToQString(out, path);
    return out;
}

void OpenFolder(const fs::path& folder) {
    std::error_code ec;
    fs::create_directories(folder, ec);
    QDesktopServices::openUrl(QUrl::fromLocalFile(QPath(folder)));
}

// With the BBSC mod (bbsc.dll) installed, the emulator is the vanilla build and BBSC is that mod;
// otherwise BBSC is the one compiled into the emulator. The choice id is "bbsc" either way.
bool BbscBuiltIn() {
    for (const ModInfo& mod : BbscMods::Scan(ModsDir(), false)) {
        if (mod.id == "bbsc" && !mod.dlls.empty()) {
            return false;
        }
    }
    return true;
}

// A free folder name under parent: name, name-2, name-3...
fs::path FreeFolder(const fs::path& parent, const std::string& name) {
    fs::path out = parent / name;
    for (int n = 2; fs::exists(out); ++n) {
        out = parent / (name + "-" + std::to_string(n));
    }
    return out;
}

// A zip often wraps everything in one folder ("MyMod v2/", "CUSA03173-mods/"): lift its content
// up, unless that folder is game content itself (dvdroot_ps4/, sce_sys/, ...).
void Flatten(const fs::path& folder) {
    static const std::set<std::string> keep{"dvdroot_ps4", "dvdroot", "sce_sys", "files", "prx",
                                            "patches",     "sound",   "movie",   "data"};
    for (int round = 0; round < 3; ++round) {
        std::vector<fs::path> entries;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(folder, ec)) {
            entries.push_back(entry.path());
        }
        if (entries.size() != 1 || !fs::is_directory(entries[0]) ||
            keep.contains(BbscMods::Lower(entries[0].filename().string()))) {
            return;
        }
        const fs::path inner = entries[0];
        const fs::path temp = folder / "__bbsc_flatten";
        fs::rename(inner, temp, ec);
        if (ec) {
            return;
        }
        for (const auto& entry : fs::directory_iterator(temp, ec)) {
            fs::rename(entry.path(), folder / entry.path().filename(), ec);
        }
        fs::remove(temp, ec);
    }
}

} // namespace

// ======================================================================== Mods tab

ModsTab::ModsTab(const std::string& serial, QWidget* parent) : QWidget(parent), m_serial(serial) {
    auto* layout = new QVBoxLayout(this);

    auto* buttons = new QHBoxLayout();
    auto* install = new QPushButton(tr("Install Mod..."), this);
    auto* installMenu = new QMenu(install);
    QAction* fromArchive = installMenu->addAction(tr("From a .zip / .7z file..."));
    QAction* fromFolder = installMenu->addAction(tr("From a folder..."));
    QAction* fromFile = installMenu->addAction(tr("From a single file (.dll, .prx, .sprx, .xml, .zar)..."));
    install->setMenu(installMenu);
    auto* open = new QPushButton(tr("Open Mods Folder"), this);
    auto* where = new QPushButton(tr("Mods Folder..."), this);
    where->setToolTip(tr("Keep your mods somewhere else (another drive, a shared folder)"));
    auto* rescan = new QPushButton(tr("Rescan"), this);
    buttons->addWidget(install);
    buttons->addWidget(open);
    buttons->addWidget(where);
    buttons->addWidget(rescan);
    buttons->addStretch();
    layout->addLayout(buttons);
    m_where = new QLabel(this);
    m_where->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_where);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    auto* left = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    m_list = new QListWidget(left);
    auto* orderRow = new QHBoxLayout();
    auto* up = new QPushButton(tr("Move Up"), left);
    auto* down = new QPushButton(tr("Move Down"), left);
    orderRow->addWidget(up);
    orderRow->addWidget(down);
    orderRow->addStretch();
    leftLayout->addWidget(m_list, 1);
    leftLayout->addLayout(orderRow);

    auto* right = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(right);
    m_title = new QLabel(right);
    QFont bold = m_title->font();
    bold.setBold(true);
    bold.setPointSizeF(bold.pointSizeF() * 1.2);
    m_title->setFont(bold);
    m_details = new QLabel(right);
    m_details->setWordWrap(true);
    m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_settingsHost = new QWidget(right);
    m_settingsForm = new QFormLayout(m_settingsHost);
    rightLayout->addWidget(m_title);
    rightLayout->addWidget(m_details);
    rightLayout->addWidget(m_settingsHost);
    rightLayout->addStretch();
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    layout->addWidget(splitter, 1);

    auto* note = new QLabel(
        tr("A mod is a folder with any of: a DLL, PRX/SPRX modules, patch XMLs, or game files "
           "(files/, dvdroot_ps4/ or a .zar). Higher in the list wins when two mods change the "
           "same game file. Changes apply the next time the game starts."),
        this);
    note->setWordWrap(true);
    layout->addWidget(note);

    connect(open, &QPushButton::clicked, this, [this] { OpenFolder(ModsDir() / m_serial); });
    connect(rescan, &QPushButton::clicked, this, [this] { Rescan(); });
    connect(where, &QPushButton::clicked, this, [this] {
        // The folder holds <SERIAL>\<mod> (this game) and <mod> (every game), like the default.
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose the mods folder"),
                                                              QPath(ModsDir()));
        if (dir.isEmpty()) {
            return;
        }
        const fs::path chosen = Common::FS::PathFromQString(dir);
        std::error_code ec;
        const fs::path setting = UserDir() / "mods-folder.txt";
        if (fs::equivalent(chosen, UserDir() / "mods", ec)) {
            fs::remove(setting, ec); // back to the default
        } else {
            std::ofstream out(setting, std::ios::trunc);
            out << dir.toStdString(); // UTF-8
        }
        Rescan();
    });
    connect(up, &QPushButton::clicked, this, [this] { Move(-1); });
    connect(down, &QPushButton::clicked, this, [this] { Move(1); });
    connect(fromArchive, &QAction::triggered, this, [this] {
        const QString file = QFileDialog::getOpenFileName(
            this, tr("Install mod from archive"), QString(),
            tr("Mod archives (*.zip *.7z *.rar *.tar *.tar.gz);;All files (*.*)"));
        if (!file.isEmpty()) {
            Install(file, true);
        }
    });
    connect(fromFolder, &QAction::triggered, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Install mod from folder"));
        if (!dir.isEmpty()) {
            Install(dir, false);
        }
    });
    connect(fromFile, &QAction::triggered, this, [this] {
        const QString file = QFileDialog::getOpenFileName(
            this, tr("Install mod from file"), QString(),
            tr("Mod files (*.dll *.prx *.sprx *.xml *.zar);;All files (*.*)"));
        if (!file.isEmpty()) {
            Install(file, false);
        }
    });
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) { ShowMod(row); });
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        const int row = m_list->row(item);
        if (row >= 0 && row < static_cast<int>(m_mods.size())) {
            m_choices[m_mods[row].id].enabled = item->checkState() == Qt::Checked;
            FindConflicts();
            if (row == m_shown) {
                ShowMod(row);
            }
        }
    });

    Rescan();
}

void ModsTab::Rescan(const std::string& select_id) {
    m_where->setText(tr("Mods folder: %1").arg(QPath(ModsDir())));
    std::string keep = select_id;
    if (keep.empty() && m_shown >= 0 && m_shown < static_cast<int>(m_mods.size())) {
        keep = m_mods[m_shown].id;
    }
    m_mods = BbscMods::ModsForGame(ModsDir(), m_serial, m_choices, BbscBuiltIn());
    FindConflicts();
    FillList();
    int row = 0;
    for (int i = 0; i < static_cast<int>(m_mods.size()); ++i) {
        if (m_mods[i].id == keep) {
            row = i;
        }
    }
    m_shown = -1;
    m_list->setCurrentRow(m_mods.empty() ? -1 : row);
    ShowMod(m_list->currentRow());
}

void ModsTab::FillList() {
    const QSignalBlocker block(m_list);
    const int current = m_list->currentRow();
    m_list->clear();
    for (int i = 0; i < static_cast<int>(m_mods.size()); ++i) {
        const ModInfo& mod = m_mods[i];
        QString label = QString("%1. %2  [%3]").arg(i + 1).arg(Q(mod.name)).arg(Q(mod.Kinds()));
        if (!mod.problem.empty()) {
            label += "  - " + Q(mod.problem);
        }
        auto* item = new QListWidgetItem(label, m_list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(m_choices[mod.id].enabled ? Qt::Checked : Qt::Unchecked);
        if (m_conflicts.contains(mod.id)) {
            item->setToolTip(m_conflicts[mod.id].join("\n"));
        }
    }
    m_list->setCurrentRow(current);
}

void ModsTab::Move(int step) {
    const int row = m_list->currentRow();
    const int target = row + step;
    if (row < 0 || target < 0 || target >= static_cast<int>(m_mods.size())) {
        return;
    }
    std::swap(m_choices[m_mods[row].id].order, m_choices[m_mods[target].id].order);
    std::swap(m_mods[row], m_mods[target]);
    FindConflicts();
    FillList();
    m_shown = -1;
    m_list->setCurrentRow(target);
}

void ModsTab::FindConflicts() {
    // Enabled file mods in load order: the first one to have a file is the one the game sees.
    m_conflicts.clear();
    std::map<std::string, int> owner; // file -> index of the mod the game uses
    std::map<std::pair<int, int>, int> counts; // (winner, loser) -> files
    for (int i = 0; i < static_cast<int>(m_mods.size()); ++i) {
        const ModInfo& mod = m_mods[i];
        if (!m_choices[mod.id].enabled || mod.files.empty()) {
            continue;
        }
        for (const std::string& file : BbscMods::ListFiles(mod)) {
            const auto [it, added] = owner.emplace(file, i);
            if (!added) {
                ++counts[{it->second, i}];
            }
        }
    }
    for (const auto& [pair, files] : counts) {
        const ModInfo& winner = m_mods[pair.first];
        const ModInfo& loser = m_mods[pair.second];
        m_conflicts[winner.id].push_back(
            tr("Replaces %1 file(s) of %2").arg(files).arg(Q(loser.name)));
        m_conflicts[loser.id].push_back(
            tr("%1 file(s) replaced by %2 (higher in the list)").arg(files).arg(Q(winner.name)));
    }
}

void ModsTab::ShowMod(int row) {
    while (m_settingsForm->rowCount() > 0) {
        m_settingsForm->removeRow(0);
    }
    m_shown = row;
    if (row < 0 || row >= static_cast<int>(m_mods.size())) {
        m_title->setText(tr("No mods for this game"));
        m_details->setText(tr("Use Install Mod, or put a mod folder in the mods folder and press "
                              "Rescan."));
        return;
    }
    const ModInfo& mod = m_mods[row];
    BbscMods::ModChoice& choice = m_choices[mod.id];
    m_title->setText(Q(mod.name));
    QStringList lines;
    QString head = tr("Type: %1").arg(Q(mod.Kinds()));
    if (!mod.version.empty()) {
        head += "   " + tr("Version: %1").arg(Q(mod.version));
    }
    if (!mod.author.empty()) {
        head += "   " + tr("By: %1").arg(Q(mod.author));
    }
    lines << head;
    if (!mod.description.empty()) {
        lines << Q(mod.description);
    }
    if (!mod.folder.empty()) {
        lines << tr("Folder: %1").arg(QPath(mod.folder));
    }
    if (!mod.files.empty()) {
        // Where the game will see these files - wrong layouts are the usual "my mod does nothing".
        lines << (mod.files_prefix.empty()
                      ? tr("Game files from: %1  (laid over the game's root, so it should hold "
                           "dvdroot_ps4\...)")
                            .arg(QPath(mod.files))
                      : tr("Game files from: %1  (laid over %2\ - it holds menu\, map\... "
                           "directly)")
                            .arg(QPath(mod.files), Q(mod.files_prefix)));
    }
    if (!mod.prx.empty()) {
        lines << tr("PRX plugins made for GoldHEN only work if they do not need GoldHEN's own "
                    "functions.");
    }
    if (!mod.problem.empty()) {
        lines << tr("Cannot run: %1").arg(Q(mod.problem));
    }
    if (m_conflicts.contains(mod.id)) {
        lines << m_conflicts[mod.id];
    }
    m_details->setText(lines.join("\n\n"));

    for (const auto& def : mod.settings) {
        std::string* value = &choice.values[def.id];
        QWidget* editor = nullptr;
        switch (def.type) {
        case SettingType::Bool: {
            auto* box = new QCheckBox(m_settingsHost);
            box->setChecked(*value == "1");
            connect(box, &QCheckBox::toggled, this, [value](bool on) { *value = on ? "1" : "0"; });
            editor = box;
            break;
        }
        case SettingType::Int: {
            auto* spin = new QSpinBox(m_settingsHost);
            spin->setRange(def.min, def.max);
            spin->setValue(std::atoi(value->c_str()));
            connect(spin, &QSpinBox::valueChanged, this,
                    [value](int number) { *value = std::to_string(number); });
            editor = spin;
            break;
        }
        case SettingType::Choice: {
            auto* combo = new QComboBox(m_settingsHost);
            for (const auto& option : def.options) {
                combo->addItem(Q(option));
            }
            combo->setCurrentText(Q(*value));
            connect(combo, &QComboBox::currentTextChanged, this,
                    [value](const QString& text) { *value = text.toStdString(); });
            editor = combo;
            break;
        }
        case SettingType::Text: {
            auto* line = new QLineEdit(Q(*value), m_settingsHost);
            connect(line, &QLineEdit::textChanged, this,
                    [value](const QString& text) { *value = text.toStdString(); });
            editor = line;
            break;
        }
        }
        editor->setToolTip(Q(def.description));
        auto* label = new QLabel(Q(def.name), m_settingsHost);
        label->setToolTip(Q(def.description));
        m_settingsForm->addRow(label, editor);
    }
    m_settingsHost->setEnabled(choice.enabled);
}

void ModsTab::Install(const QString& source, bool is_archive) {
    const fs::path src = Common::FS::PathFromQString(source);
    const fs::path parent = ModsDir() / m_serial;
    std::error_code ec;
    fs::create_directories(parent, ec);
    std::string name = src.stem().string();
    if (name.ends_with(".tar")) {
        name.resize(name.size() - 4);
    }
    const fs::path dest = FreeFolder(parent, fs::is_directory(src) ? src.filename().string() : name);
    fs::create_directories(dest, ec);

    QString error;
    if (is_archive) {
        // Windows' own tar (libarchive) reads zip, 7z, rar and tar.
        QProcess tar;
        tar.setProgram("tar");
        tar.setArguments({"-xf", source, "-C", QPath(dest)});
        tar.start();
        if (!tar.waitForFinished(300000) || tar.exitStatus() != QProcess::NormalExit ||
            tar.exitCode() != 0) {
            error = tr("Could not unpack the archive:\n%1")
                        .arg(QString::fromLocal8Bit(tar.readAllStandardError()).trimmed());
        } else {
            Flatten(dest);
        }
    } else if (fs::is_directory(src)) {
        // A folder is linked, not copied (2026-10-12 report: a 20 GB mod took 40 GB). A Windows
        // directory junction needs no admin rights and no space; deleting it (Explorer or
        // here) removes only the link. Copied only when the link cannot be made (e.g. a network
        // drive).
        bool linked = false;
#ifdef _WIN32
        std::error_code rm;
        fs::remove(dest, rm); // the empty folder made above; mklink wants to create it
        QProcess mklink;
        mklink.setProgram("cmd.exe");
        mklink.setNativeArguments(QString("/c mklink /J \"%1\" \"%2\"")
                                      .arg(QString::fromStdWString(dest.native()),
                                           QString::fromStdWString(src.native())));
        mklink.start();
        linked = mklink.waitForFinished(30000) && mklink.exitStatus() == QProcess::NormalExit &&
                 mklink.exitCode() == 0 && fs::is_directory(dest, rm);
        if (linked) {
            LOG_INFO(Config, "BBSC: mod folder linked (no copy): {} -> {}", dest.string(),
                     src.string());
            QMessageBox::information(
                this, tr("Install Mod"),
                tr("The mod is linked to its folder, not copied, so it takes no extra space:\n%1\n\n"
                   "Keep that folder where it is - the mod reads its files from there.")
                    .arg(QString::fromStdWString(src.native())));
        } else {
            fs::create_directories(dest, rm);
        }
#endif
        if (!linked) {
            fs::copy(src, dest, fs::copy_options::recursive | fs::copy_options::overwrite_existing,
                     ec);
        }
    } else {
        fs::copy_file(src, dest / src.filename(), fs::copy_options::overwrite_existing, ec);
    }
    if (error.isEmpty() && ec) {
        error = tr("Could not copy the mod: %1").arg(Q(ec.message()));
    }
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Install Mod"), error);
        fs::remove_all(dest, ec); // our own half-made folder
        Rescan();
        return;
    }

    // Find it, switch it on, and put it at the top of the load order.
    m_mods = BbscMods::ModsForGame(ModsDir(), m_serial, m_choices, BbscBuiltIn());
    std::string id;
    for (const ModInfo& mod : m_mods) {
        if (mod.folder == dest) {
            id = mod.id;
        }
    }
    if (!id.empty()) {
        m_choices[id].enabled = true;
        for (auto& [other, choice] : m_choices) {
            ++choice.order;
        }
        m_choices[id].order = 0;
    }
    Rescan(id);
    const auto it = std::ranges::find(m_mods, id, &ModInfo::id);
    QMessageBox::information(
        this, tr("Install Mod"),
        it == m_mods.end()
            ? tr("Copied to %1, but nothing usable was found in it.").arg(QPath(dest))
            : tr("Installed %1 [%2]. Press Save to keep it on.")
                  .arg(Q(it->name))
                  .arg(Q(it->Kinds())));
}

void ModsTab::Save() {
    if (!BbscMods::SaveChoices(ModsDir(), m_serial, m_choices)) {
        LOG_ERROR(Config, "BBSC mods: could not save the choices for {}", m_serial);
    }
}

namespace {

// ---- shadNet account creation (the Create request of shadnet-sample "register") ----

void PutVarint(QByteArray& out, quint64 value) {
    while (value >= 0x80) {
        out.append(static_cast<char>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    out.append(static_cast<char>(value));
}

void PutString(QByteArray& out, int field, const QByteArray& text) {
    if (text.isEmpty()) {
        return;
    }
    PutVarint(out, (static_cast<quint64>(field) << 3) | 2);
    PutVarint(out, static_cast<quint64>(text.size()));
    out.append(text);
}

void PutLE(QByteArray& out, quint64 value, int bytes) {
    for (int i = 0; i < bytes; ++i) {
        out.append(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
}

bool ReadExact(QTcpSocket& socket, QByteArray& out, qint64 size, int timeout_ms) {
    out.clear();
    while (out.size() < size) {
        if (socket.bytesAvailable() == 0 && !socket.waitForReadyRead(timeout_ms)) {
            return false;
        }
        out.append(socket.read(size - out.size()));
    }
    return true;
}

QString ErrorText(int error) {
    switch (error) {
    case 0:
        return QObject::tr("Account created on the server.");
    case 11:
        return QObject::tr("That name is taken on this server.");
    case 13:
        return QObject::tr("That email is already used on this server.");
    case 12:
        return QObject::tr("The server does not accept that email provider.");
    case 23:
        return QObject::tr("Wrong or missing registration key - ask the host for it.");
    case 1:
    case 3:
        return QObject::tr("The server rejected the name, password or email "
                           "(letters, numbers, - and _ only).");
    case 4:
        return QObject::tr("Too many attempts - wait a minute and try again.");
    case 24:
        return QObject::tr("The server could not save the account (database error).");
    default:
        return QObject::tr("The server refused (error %1).").arg(error);
    }
}

std::pair<bool, QString> CreateAccountBlocking(QString address, QString npid, QString password,
                                               QString email, QString key) {
    constexpr int Timeout = 8000;
    constexpr int HeaderSize = 15;
    QString host = address.trimmed();
    quint16 port = 31313;
    if (const int colon = host.lastIndexOf(':'); colon > 0) {
        bool ok = false;
        const int value = host.mid(colon + 1).toInt(&ok);
        if (!ok || value <= 0 || value > 65535) {
            return {false, QObject::tr("The server address should look like host:31313.")};
        }
        port = static_cast<quint16>(value);
        host = host.left(colon);
    }

    QTcpSocket socket;
    socket.connectToHost(host, port);
    if (!socket.waitForConnected(Timeout)) {
        return {false, QObject::tr("Could not reach %1:%2 - is the server on, and are you on the "
                                   "same VPN / address?")
                           .arg(host)
                           .arg(port)};
    }

    // The server greets every connection with a ServerInfo packet (type 3).
    QByteArray header, payload;
    if (!ReadExact(socket, header, HeaderSize, Timeout) || header[0] != 3) {
        return {false, QObject::tr("That address answered, but it is not a shadNet server.")};
    }
    const auto size_of = [](const QByteArray& h) {
        return static_cast<quint32>(static_cast<quint8>(h[3])) |
               (static_cast<quint32>(static_cast<quint8>(h[4])) << 8) |
               (static_cast<quint32>(static_cast<quint8>(h[5])) << 16) |
               (static_cast<quint32>(static_cast<quint8>(h[6])) << 24);
    };
    if (!ReadExact(socket, payload, size_of(header) - HeaderSize, Timeout)) {
        return {false, QObject::tr("That address answered, but it is not a shadNet server.")};
    }

    // RegistrationRequest { npid = 1; password = 2; avatar_url = 3; email = 4; secret_key = 5; }
    QByteArray proto;
    PutString(proto, 1, npid.toUtf8());
    PutString(proto, 2, password.toUtf8());
    PutString(proto, 4, email.toUtf8());
    PutString(proto, 5, key.toUtf8());
    QByteArray packet;
    packet.append(char(0));                             // Request
    PutLE(packet, 2, 2);                                // CommandType::Create
    PutLE(packet, HeaderSize + 4 + proto.size(), 4);    // total size
    PutLE(packet, 1, 8);                                // packet id
    PutLE(packet, static_cast<quint64>(proto.size()), 4);
    packet.append(proto);
    socket.write(packet);
    if (!socket.waitForBytesWritten(Timeout)) {
        return {false, QObject::tr("The connection dropped while sending.")};
    }

    // Skip notifications until the Create reply (type 1, command 2); its first byte is the error.
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < Timeout) {
        if (!ReadExact(socket, header, HeaderSize, Timeout) ||
            !ReadExact(socket, payload, size_of(header) - HeaderSize, Timeout)) {
            break;
        }
        const int command = static_cast<quint8>(header[1]) | (static_cast<quint8>(header[2]) << 8);
        if (header[0] == 1 && command == 2) {
            const int error = payload.isEmpty() ? 1 : static_cast<quint8>(payload[0]);
            return {error == 0, ErrorText(error)};
        }
    }
    return {false, QObject::tr("No answer from the server.")};
}

} // namespace

// ======================================================================== Accounts tab

AccountsTab::AccountsTab(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);

    // Server
    auto* serverGroup = new QGroupBox(tr("shadNet server"), this);
    auto* serverForm = new QFormLayout(serverGroup);
    auto* cardRow = new QHBoxLayout();
    m_cardBox = new QComboBox(serverGroup);
    auto* importCard = new QPushButton(tr("Import Card..."), serverGroup);
    auto* openCards = new QPushButton(tr("Open Cards Folder"), serverGroup);
    cardRow->addWidget(m_cardBox, 1);
    cardRow->addWidget(importCard);
    cardRow->addWidget(openCards);
    serverForm->addRow(tr("Server card:"), cardRow);
    m_server = new QLineEdit(Q(EmulatorSettings.GetShadNetServer()), serverGroup);
    m_server->setPlaceholderText("host:31313");
    serverForm->addRow(tr("Server address:"), m_server);
    auto* cardNote = new QLabel(tr("A server card is a small file from the host (name, address, "
                                   "registration key). Put it in the cards folder."),
                                serverGroup);
    cardNote->setWordWrap(true);
    serverForm->addRow(cardNote);
    layout->addWidget(serverGroup);

    // Create account
    auto* createGroup = new QGroupBox(tr("Create an account on this server"), this);
    auto* createForm = new QFormLayout(createGroup);
    m_npid = new QLineEdit(createGroup);
    m_npid->setPlaceholderText(tr("letters, numbers, - and _"));
    m_password = new QLineEdit(createGroup);
    m_password->setEchoMode(QLineEdit::Password);
    m_email = new QLineEdit(createGroup);
    m_email->setPlaceholderText(tr("optional"));
    m_key = new QLineEdit(createGroup);
    m_key->setEchoMode(QLineEdit::Password);
    m_key->setPlaceholderText(tr("from the host, if the server needs one"));
    m_userBox = new QComboBox(createGroup);
    m_useForUser = new QCheckBox(tr("Use the new account for this local user"), createGroup);
    m_useForUser->setChecked(true);
    m_create = new QPushButton(tr("Create Account on Server"), createGroup);
    m_result = new QLabel(createGroup);
    m_result->setWordWrap(true);
    createForm->addRow(tr("Account ID (NPID):"), m_npid);
    createForm->addRow(tr("Password:"), m_password);
    createForm->addRow(tr("Email:"), m_email);
    createForm->addRow(tr("Registration key:"), m_key);
    createForm->addRow(tr("Local user:"), m_userBox);
    createForm->addRow(QString(), m_useForUser);
    createForm->addRow(m_create);
    createForm->addRow(m_result);
    layout->addWidget(createGroup);

    // Users
    auto* usersRow = new QHBoxLayout();
    auto* manage = new QPushButton(tr("User Manager..."), this);
    usersRow->addWidget(new QLabel(tr("Local users, default user and their shadNet IDs:"), this));
    usersRow->addWidget(manage);
    usersRow->addStretch();
    layout->addLayout(usersRow);
    auto* note = new QLabel(tr("The account is made on the server, so it works from any PC."),
                            this);
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addStretch();

    LoadCards();
    FillUsers();

    connect(openCards, &QPushButton::clicked, this, [] { OpenFolder(UserDir() / "servers"); });
    connect(importCard, &QPushButton::clicked, this, [this] {
        // A card from a host's GENERATE-CARD: copied into the cards folder and picked.
        const QString file = QFileDialog::getOpenFileName(this, tr("Import server card"), QString(),
                                                          tr("Server cards (*.json)"));
        if (file.isEmpty()) {
            return;
        }
        QString address;
        try {
            std::ifstream in(Common::FS::PathFromQString(file));
            const json j = json::parse(in, nullptr, true, true);
            address = Q(j.value("shadnet", ""));
        } catch (...) {
        }
        if (address.isEmpty()) {
            QMessageBox::warning(this, tr("Import Card"),
                                 tr("That file is not a BBSC server card (no shadnet address)."));
            return;
        }
        const fs::path folder = UserDir() / "servers";
        std::error_code ec;
        fs::create_directories(folder, ec);
        const fs::path source = Common::FS::PathFromQString(file);
        fs::copy_file(source, folder / source.filename(), fs::copy_options::overwrite_existing, ec);
        if (ec) {
            QMessageBox::warning(this, tr("Import Card"),
                                 tr("Could not copy the card: %1").arg(Q(ec.message())));
            return;
        }
        m_server->setText(address);
        LoadCards();
        m_result->setStyleSheet(QString());
        m_result->setText(tr("Card imported. Now type a name and password and press Create "
                             "Account on Server."));
    });
    connect(m_cardBox, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index > 0 && index <= static_cast<int>(m_cards.size())) {
            m_server->setText(m_cards[index - 1].address);
            m_key->setText(m_cards[index - 1].key);
        }
    });
    connect(manage, &QPushButton::clicked, this, [this] {
        UserManagerDialog dialog(this);
        dialog.exec();
        FillUsers();
    });
    connect(m_create, &QPushButton::clicked, this, [this] { CreateAccount(); });
    connect(&m_job, &QFutureWatcher<std::pair<bool, QString>>::finished, this, [this] {
        const auto [ok, message] = m_job.result();
        m_create->setEnabled(true);
        m_result->setText(message);
        m_result->setStyleSheet(ok ? "color: #60d060;" : "color: #ff7070;");
        if (ok && m_useForUser->isChecked()) {
            const u32 id = m_userBox->currentData().toUInt();
            if (User* user = UserManagement.GetUserByID(id)) {
                user->shadnet_enabled = true;
                user->shadnet_npid = m_npid->text().trimmed().toStdString();
                user->shadnet_password = m_password->text().toStdString();
                user->shadnet_email = m_email->text().trimmed().toStdString();
                UserManagement.Save();
                m_result->setText(message + " " + tr("User %1 now uses it.").arg(id));
                FillUsers();
            }
        }
    });
}

void AccountsTab::LoadCards() {
    m_cards.clear();
    m_cardBox->clear();
    m_cardBox->addItem(tr("(type the address)"));
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(UserDir() / "servers", ec)) {
        if (entry.path().extension() != ".json") {
            continue;
        }
        try {
            std::ifstream file(entry.path());
            const json j = json::parse(file, nullptr, true, true);
            ServerCard card{Q(j.value("name", entry.path().stem().string())),
                            Q(j.value("shadnet", "")), Q(j.value("registration_key", ""))};
            if (!card.address.isEmpty()) {
                m_cards.push_back(card);
            }
        } catch (const std::exception& e) {
            LOG_ERROR(Config, "BBSC: could not read server card: {}", e.what());
        }
    }
    const QSignalBlocker block(m_cardBox);
    for (int i = 0; i < static_cast<int>(m_cards.size()); i++) {
        m_cardBox->addItem(m_cards[i].name);
        if (m_cards[i].address == m_server->text()) {
            m_cardBox->setCurrentIndex(i + 1);
            m_key->setText(m_cards[i].key);
        }
    }
}

void AccountsTab::FillUsers() {
    const u32 current = m_userBox->currentData().toUInt();
    m_userBox->clear();
    for (const User& user : UserManagement.GetAllUsers()) {
        QString label = QString("%1  %2").arg(user.user_id).arg(Q(user.user_name));
        if (user.shadnet_enabled && !user.shadnet_npid.empty()) {
            label += "  (shadNet: " + Q(user.shadnet_npid) + ")";
        }
        m_userBox->addItem(label, static_cast<uint>(user.user_id));
        if (static_cast<u32>(user.user_id) == current) {
            m_userBox->setCurrentIndex(m_userBox->count() - 1);
        }
    }
}

void AccountsTab::CreateAccount() {
    const QString npid = m_npid->text().trimmed();
    if (npid.isEmpty() || m_password->text().isEmpty() || m_server->text().trimmed().isEmpty()) {
        m_result->setStyleSheet("color: #ff7070;");
        m_result->setText(tr("Fill in the server address, the account ID and the password."));
        return;
    }
    QString email = m_email->text().trimmed();
    if (email.isEmpty()) {
        email = npid + "@shadnet.local";
        m_email->setText(email);
    }
    m_create->setEnabled(false);
    m_result->setStyleSheet(QString());
    m_result->setText(tr("Contacting the server..."));
    m_job.setFuture(QtConcurrent::run(CreateAccountBlocking, m_server->text(), npid,
                                      m_password->text(), email, m_key->text()));
}

void AccountsTab::Save() {
    EmulatorSettings.SetShadNetServer(m_server->text().trimmed().toStdString());
}

// ======================================================================== Patches tab

namespace {

// The game's version (APP_VER), from its folder in the game folders; an update folder wins.
QString FindGameVersion(const std::string& serial) {
    QString version;
    std::error_code ec;
    for (const fs::path& dir : EmulatorSettings.GetGameInstallDirs()) {
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (!entry.is_directory()) {
                continue;
            }
            const fs::path sfo = entry.path() / "sce_sys" / "param.sfo";
            PSF psf;
            if (!fs::exists(sfo, ec) || !psf.Open(sfo)) {
                continue;
            }
            const auto title = psf.GetString("TITLE_ID");
            if (!title || *title != serial) {
                continue;
            }
            if (const auto ver = psf.GetString("APP_VER"); ver && Q(std::string(*ver)) > version) {
                version = Q(std::string(*ver));
            }
        }
    }
    return version;
}

} // namespace

PatchesTab::PatchesTab(const std::string& serial, std::shared_ptr<gui_settings> gui,
                       std::shared_ptr<IpcClient> ipc, QWidget* parent)
    : QWidget(parent), m_serial(serial), m_gui(std::move(gui)), m_ipc(std::move(ipc)) {
    auto* layout = new QVBoxLayout(this);
    auto* buttons = new QHBoxLayout();
    m_download = new QPushButton(tr("Download / Update Patches"), this);
    auto* open = new QPushButton(tr("Open Patches Folder"), this);
    buttons->addWidget(m_download);
    buttons->addWidget(open);
    buttons->addStretch();
    layout->addLayout(buttons);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    m_listHost = new QWidget(scroll);
    m_listLayout = new QVBoxLayout(m_listHost);
    scroll->setWidget(m_listHost);
    layout->addWidget(scroll, 1);

    auto* note = new QLabel(tr("Patches from the shadPS4 and GoldHEN patch repositories (the same "
                               "files as the Cheats / Patches window). Hover a patch for its "
                               "note. Patches are made by their authors, not by BBSC."),
                            this);
    note->setWordWrap(true);
    layout->addWidget(note);

    connect(open, &QPushButton::clicked, this, [] {
        OpenFolder(Common::FS::GetUserPath(Common::FS::PathType::PatchesDir));
    });
    connect(m_download, &QPushButton::clicked, this, [this] { Download(); });
    Load();
}

void PatchesTab::Load() {
    while (QLayoutItem* item = m_listLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_entries.clear();
    m_version = FindGameVersion(m_serial);

    const fs::path root = Common::FS::GetUserPath(Common::FS::PathType::PatchesDir);
    std::error_code ec;
    int shown = 0;
    for (const auto& repo : fs::directory_iterator(root, ec)) {
        if (!repo.is_directory() || !fs::exists(repo.path() / "files.json")) {
            continue;
        }
        // files.json: {"<xml file>": ["CUSA...", ...]}
        QString fileName;
        try {
            std::ifstream in(repo.path() / "files.json");
            const json files = json::parse(in, nullptr, true, true);
            for (const auto& [name, serials] : files.items()) {
                if (serials.is_array() &&
                    std::ranges::find(serials, json(m_serial)) != serials.end()) {
                    fileName = Q(name);
                    break;
                }
            }
        } catch (const std::exception& e) {
            LOG_ERROR(Config, "BBSC patches: bad files.json in {}: {}", repo.path().string(),
                      e.what());
        }
        if (fileName.isEmpty()) {
            continue;
        }
        const fs::path xmlPath = repo.path() / Common::FS::PathFromQString(fileName);
        QFile xml(QPath(xmlPath));
        if (!xml.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        auto* group = new QGroupBox(Q(repo.path().filename().string()), m_listHost);
        auto* groupLayout = new QVBoxLayout(group);
        QXmlStreamReader reader(&xml);
        while (!reader.atEnd() && !reader.hasError()) {
            reader.readNext();
            if (!reader.isStartElement() || reader.name() != QStringLiteral("Metadata")) {
                continue;
            }
            const auto attrs = reader.attributes();
            const QString appVer = attrs.value("AppVer").toString();
            const bool anyVersion = appVer == QStringLiteral("mask");
            if (!anyVersion && !m_version.isEmpty() && appVer != m_version) {
                continue;
            }
            PatchEntry entry;
            entry.file = QPath(xmlPath);
            entry.name = attrs.value("Name").toString();
            entry.appVer = appVer;
            QString label = entry.name;
            if (anyVersion) {
                label += tr("  (any version)");
            } else if (m_version.isEmpty()) {
                label += "  (" + appVer + ")";
            }
            entry.box = new QCheckBox(label, group);
            entry.box->setChecked(attrs.value("isEnabled").toString() == QStringLiteral("true"));
            QString tip = attrs.value("Note").toString();
            if (const QString author = attrs.value("Author").toString(); !author.isEmpty()) {
                tip += (tip.isEmpty() ? "" : "\n") + tr("Author: %1").arg(author);
            }
            entry.box->setToolTip(tip);
            groupLayout->addWidget(entry.box);
            m_entries.push_back(entry);
            ++shown;
        }
        if (groupLayout->count() == 0) {
            delete group;
        } else {
            m_listLayout->addWidget(group);
        }
    }
    m_listLayout->addStretch();
    if (shown == 0) {
        m_status->setText(tr("No patches for %1%2 yet. Press Download / Update Patches.")
                              .arg(Q(m_serial))
                              .arg(m_version.isEmpty() ? "" : " v" + m_version));
    } else {
        m_status->setText(tr("%1 patch(es) for %2%3.")
                              .arg(shown)
                              .arg(Q(m_serial))
                              .arg(m_version.isEmpty() ? "" : " v" + m_version));
    }
}

void PatchesTab::Download() {
    m_download->setEnabled(false);
    m_status->setText(tr("Downloading patches..."));
    const QString empty;
    auto* downloader = new CheatsPatches(m_gui, m_ipc, empty, empty, empty, empty, QPixmap(),
                                         nullptr);
    auto pending = std::make_shared<int>(2);
    connect(downloader, &CheatsPatches::downloadFinished, this, [this, downloader, pending] {
        if (--*pending > 0) {
            return;
        }
        downloader->createFilesJson("GoldHEN");
        downloader->createFilesJson("shadPS4");
        downloader->deleteLater();
        m_download->setEnabled(true);
        Load();
    });
    downloader->downloadPatches("GoldHEN", false);
    downloader->downloadPatches("shadPS4", false);
}

void PatchesTab::Save() {
    // Same edit as the Cheats / Patches window: rewrite isEnabled on the listed Metadata.
    std::map<QString, std::map<QString, bool>> wanted; // file -> "name|appver" -> on
    for (const auto& entry : m_entries) {
        wanted[entry.file][entry.name + "|" + entry.appVer] = entry.box->isChecked();
    }
    for (const auto& [path, states] : wanted) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        const QByteArray data = file.readAll();
        file.close();

        QString out;
        QXmlStreamWriter writer(&out);
        writer.setAutoFormatting(true);
        writer.writeStartDocument();
        QXmlStreamReader reader(data);
        bool changed = false;
        while (!reader.atEnd()) {
            reader.readNext();
            if (reader.isStartElement()) {
                writer.writeStartElement(reader.name().toString());
                const auto attrs = reader.attributes();
                const auto it = reader.name() == QStringLiteral("Metadata")
                                     ? states.find(attrs.value("Name").toString() + "|" +
                                                   attrs.value("AppVer").toString())
                                     : states.end();
                for (const QXmlStreamAttribute& attr : attrs) {
                    if (it != states.end() && attr.name() == QStringLiteral("isEnabled")) {
                        continue;
                    }
                    writer.writeAttribute(attr.name().toString(), attr.value().toString());
                }
                if (it != states.end()) {
                    const QString value = it->second ? "true" : "false";
                    changed |= attrs.value("isEnabled").toString() != value;
                    writer.writeAttribute("isEnabled", value);
                }
            } else if (reader.isEndElement()) {
                writer.writeEndElement();
            } else if (reader.isCharacters() && !reader.isWhitespace()) {
                writer.writeCharacters(reader.text().toString());
            }
        }
        writer.writeEndDocument();
        if (reader.hasError()) {
            LOG_ERROR(Config, "BBSC patches: could not read {}: {}", path.toStdString(),
                      reader.errorString().toStdString());
            continue;
        }
        if (changed && file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            QTextStream(&file) << out;
        }
    }
}

// ======================================================================== bundled emulator + mods

void SetUpBundle(const std::shared_ptr<gui_settings>& settings) {
    const fs::path base = Common::FS::PathFromQString(QCoreApplication::applicationDirPath());
    std::error_code ec;

    const fs::path emulator = base / "emulator" / "shadPS4.exe";
    if (fs::exists(emulator, ec)) {
        const QString path = QPath(emulator);
        bool listed = false;
        for (const auto& version : VersionManager::GetVersionList()) {
            listed |= version.name == "BBSC" && Q(version.path) == path;
        }
        if (!listed) {
            VersionManager::AddNewVersion(VersionManager::Version{
                .name = "BBSC",
                .path = path.toStdString(),
                .date = QDate::currentDate().toString("yyyy-MM-dd").toStdString(),
                .codename = "BBSC",
                .type = VersionManager::VersionType::Custom,
                .id = 0,
            });
        }
        const gui_value selectedOnce("bbsc", "bundleSelected", "");
        if (settings->GetValue(selectedOnce).toString() != path) {
            settings->SetValue(gui::vm_versionSelected, path);
            settings->SetValue(selectedOnce, path);
            LOG_INFO(Config, "BBSC: bundled emulator selected: {}", path.toStdString());
        }
    }

    // Only BBSC itself is installed from the package (about 1 MB). Before (2026-10-12 report): the
    // package's whole mods folder was copied at every start, so a 20 GB mod a player had put
    // there (BB Remaster) took 40 GB - once there, once in the user folder.
    const fs::path bundled = base / "mods" / "CUSA03173" / "bbsc";
    if (fs::is_directory(bundled, ec)) {
        const fs::path target = ModsDir() / "CUSA03173" / "bbsc";
        fs::create_directories(target, ec);
        fs::copy(bundled, target, fs::copy_options::recursive | fs::copy_options::update_existing,
                 ec);
        if (ec) {
            LOG_ERROR(Config, "BBSC: could not copy the bundled BBSC mod: {}", ec.message());
        }
    }
}

bool NoticesPresent() {
    const QString base = QCoreApplication::applicationDirPath();
    auto holds = [&base](const char* name, const char* text) {
        QFile file(base + "/" + name);
        return file.open(QIODevice::ReadOnly) && file.readAll().contains(text);
    };
    if (holds("LICENSE.txt", "GNU GENERAL PUBLIC LICENSE") &&
        holds("NOTICE.txt", "github.com/Classmg22/BBSC")) {
        return true;
    }
    LOG_ERROR(Config, "BBSC: LICENSE.txt / NOTICE.txt missing or changed next to the launcher");
    QMessageBox::critical(
        nullptr, "BBSC",
        "LICENSE.txt or NOTICE.txt is missing from the BBSC folder (or was changed).\n\n"
        "BBSC is free software under the GNU GPL, and those two files have to travel with it.\n"
        "Download the full package again from:\nhttps://github.com/Classmg22/BBSC");
    return false;
}

} // namespace Bbsc
