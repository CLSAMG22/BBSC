// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

// bbsc-launch.exe - starts shadPS4 with the BBSC mod DLL loaded into it.
//
//   bbsc-launch.exe [--emu <shadPS4.exe>] [--dll <bbsc.dll>] [--window left|right] [--choose]
//                   [--cwd <folder>] [shadPS4 arguments...]
//
// --cwd: the folder the emulator starts in (default: the emulator's own folder). shadPS4 uses the
// "user" folder of that folder when there is one (portable), else %APPDATA%\shadPS4 - a portable
// Qt launcher keeps "user" next to the launcher, not next to the emulator version it runs.
//
// Double-clicked with no arguments: the first time it asks (Windows file picker) for
// Bloodborne's eboot.bin and, if there is no shadPS4.exe next to it, for the emulator; the
// choices are kept in bbsc-launch.ini next to it. --choose asks again.
//
// Defaults: shadPS4.exe and bbsc.dll next to this launcher. Every other argument is passed to
// shadPS4 unchanged (e.g. --game <path to eboot.bin>). The emulator is created suspended, the DLL
// is loaded into it by a remote LoadLibraryW, and only then is the emulator let go - so the mod is
// in place before the game's first instruction. The launcher waits for the emulator to close and
// returns its exit code.

#include <windows.h>
#include <commdlg.h>

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace {

std::wstring HereFile(const wchar_t* name) {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir{path};
    const size_t slash = dir.find_last_of(L"\\/");
    return (slash == std::wstring::npos ? L"" : dir.substr(0, slash + 1)) + name;
}

std::wstring Quote(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) {
        return arg;
    }
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (const wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
        } else if (c == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out += c;
            backslashes = 0;
            continue;
        } else {
            backslashes = 0;
        }
        out += c;
    }
    out.append(backslashes * 2, L'\\');
    return out + L"\"";
}

struct FindWindowArgs {
    DWORD pid;
    HWND found;
};

BOOL CALLBACK FindProcessWindow(HWND window, LPARAM param) {
    auto* args = reinterpret_cast<FindWindowArgs*>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == args->pid && IsWindowVisible(window) && GetWindow(window, GW_OWNER) == nullptr &&
        GetWindowTextLengthW(window) > 0) {
        args->found = window;
        return FALSE;
    }
    return TRUE;
}

// --window left|right: puts the emulator's window on that half of the main screen once it
// appears, and keeps it there for the first seconds (the emulator sizes its window while starting).
void PlaceWindow(DWORD pid, const std::wstring& side) {
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    // left / right: half the screen; tl / tr / bl / br: a quarter (4-window tests, 2026-10-07).
    const bool quarter = side == L"tl" || side == L"tr" || side == L"bl" || side == L"br";
    const int half = (work.right - work.left) / 2;
    const bool right = side == L"right" || side == L"tr" || side == L"br";
    const bool bottom = side == L"bl" || side == L"br";
    const int full_height = work.bottom - work.top;
    const int x = right ? work.left + half : work.left;
    const int y = bottom ? work.top + full_height / 2 : work.top;
    const int height = quarter ? full_height / 2 : full_height;
    DWORD placed_at = 0;
    for (int i = 0; i < 480; ++i) { // up to 2 minutes for the window to appear
        FindWindowArgs args{pid, nullptr};
        EnumWindows(FindProcessWindow, reinterpret_cast<LPARAM>(&args));
        if (args.found != nullptr) {
            RECT now{};
            GetWindowRect(args.found, &now);
            if (now.left != x || now.top != y || now.right - now.left != half ||
                now.bottom - now.top != height) {
                ShowWindow(args.found, SW_RESTORE);
                SetWindowPos(args.found, nullptr, x, y, half, height,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            if (placed_at == 0) {
                placed_at = GetTickCount();
            } else if (GetTickCount() - placed_at > 15'000) {
                return;
            }
        }
        Sleep(250);
    }
}

DWORD WINAPI PlaceWindowThread(void* param) {
    auto* args = static_cast<std::pair<DWORD, std::wstring>*>(param);
    PlaceWindow(args->first, args->second);
    delete args;
    return 0;
}


// ---- Remembered choices: bbsc-launch.ini next to the launcher (game=..., emulator=...) ---------

std::wstring IniPath() {
    return HereFile(L"bbsc-launch.ini");
}

std::wstring LoadSetting(const wchar_t* key) {
    wchar_t value[MAX_PATH * 2]{};
    GetPrivateProfileStringW(L"bbsc", key, L"", value, MAX_PATH * 2, IniPath().c_str());
    return value;
}

void SaveSetting(const wchar_t* key, const std::wstring& value) {
    WritePrivateProfileStringW(L"bbsc", key, value.c_str(), IniPath().c_str());
}

bool FileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return !path.empty() && attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

void Message(const std::wstring& text, UINT icon = MB_ICONINFORMATION) {
    MessageBoxW(nullptr, text.c_str(), L"BBSC", MB_OK | icon | MB_SETFOREGROUND);
}

// A Windows "open file" window. Empty if the player cancelled.
std::wstring PickFile(const wchar_t* title, const wchar_t* filter) {
    wchar_t path[MAX_PATH]{};
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.lpstrTitle = title;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&dialog) ? std::wstring{path} : std::wstring{};
}

// The folder of eboot.bin should hold sce_sys\param.sfo naming CUSA03173.
bool IsBloodborne(const std::wstring& eboot) {
    const std::wstring sfo =
        eboot.substr(0, eboot.find_last_of(L"\\/") + 1) + L"sce_sys\\param.sfo";
    HANDLE file = CreateFileW(sfo.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    std::string data(64 * 1024, '\0');
    DWORD read = 0;
    ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read, nullptr);
    CloseHandle(file);
    data.resize(read);
    return data.find("CUSA03173") != std::string::npos;
}

std::wstring ChooseGame() {
    Message(L"Please pick your Bloodborne game file.\n\n"
            L"It is the file eboot.bin in your Bloodborne folder (the one named CUSA03173).\n"
            L"You only have to do this once.");
    for (;;) {
        const std::wstring picked =
            PickFile(L"BBSC - pick Bloodborne's eboot.bin", L"eboot.bin\0eboot.bin\0All files\0*.*\0");
        if (picked.empty()) {
            return {};
        }
        if (IsBloodborne(picked)) {
            SaveSetting(L"game", picked);
            return picked;
        }
        if (MessageBoxW(nullptr,
                        L"That folder does not look like Bloodborne CUSA03173 (no sce_sys\\param.sfo "
                        L"with CUSA03173 next to it).\n\nUse it anyway?",
                        L"BBSC", MB_YESNO | MB_ICONWARNING | MB_SETFOREGROUND) == IDYES) {
            SaveSetting(L"game", picked);
            return picked;
        }
    }
}

std::wstring ChooseEmulator() {
    Message(L"Please pick the shadPS4.exe you play BBSC with.\n\nYou only have to do this once.");
    const std::wstring picked =
        PickFile(L"BBSC - pick shadPS4.exe", L"shadPS4.exe\0*.exe\0All files\0*.*\0");
    if (!picked.empty()) {
        SaveSetting(L"emulator", picked);
    }
    return picked;
}

void DefaultSwitch(const wchar_t* name, const wchar_t* value) {
    if (GetEnvironmentVariableW(name, nullptr, 0) == 0) {
        SetEnvironmentVariableW(name, value);
    }
}

int Fail(const wchar_t* what, HANDLE process = nullptr) {
    const DWORD error = GetLastError();
    Message(std::wstring(what) + L"\n\n(Windows error " + std::to_wstring(error) + L")",
            MB_ICONERROR);
    if (process != nullptr) {
        TerminateProcess(process, 1);
    }
    return 1;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    std::wstring emulator;
    std::wstring dll = HereFile(L"bbsc.dll");
    std::wstring window_side;
    std::wstring start_dir;
    bool choose = false;
    bool no_mod = false; // --no-mod: start the emulator as it is (comparison runs)
    bool has_game = false;
    std::vector<std::wstring> passthrough;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--choose") {
            choose = true;
        } else if (arg == L"--no-mod") {
            no_mod = true;
        } else if (arg == L"--emu" && i + 1 < argc) {
            emulator = argv[++i];
        } else if (arg == L"--dll" && i + 1 < argc) {
            dll = argv[++i];
        } else if (arg == L"--cwd" && i + 1 < argc) {
            start_dir = argv[++i];
        } else if (arg == L"--window" && i + 1 < argc) {
            window_side = argv[++i];
        } else {
            has_game = has_game || arg == L"--game" || arg == L"-g";
            passthrough.push_back(arg);
        }
    }
    // The emulator: --emu, else shadPS4.exe next to the launcher, else the one picked before,
    // else ask. The game: --game, else the eboot.bin picked before, else ask.
    // BBSC_EMULATOR: another program to start instead (the bbport Windows build sets its game exe
    // through BB_PROBE, so bbsc-launch stands in for bb-probe.exe and starts it from here).
    if (emulator.empty()) {
        wchar_t from_env[MAX_PATH]{};
        if (GetEnvironmentVariableW(L"BBSC_EMULATOR", from_env, MAX_PATH) > 0) {
            emulator = from_env;
        }
    }
    // bbport (Windows): bb-probe.exe gets the prepared game image from bbport's run.py - no
    // --game, and it runs in the folder run.py started it in (paths relative to the port).
    const std::wstring emulator_name =
        emulator.substr(emulator.find_last_of(L"\\/") == std::wstring::npos
                            ? 0
                            : emulator.find_last_of(L"\\/") + 1);
    const bool bbport = _wcsicmp(emulator_name.c_str(), L"bb-probe.exe") == 0;
    has_game = has_game || bbport;
    if (emulator.empty()) {
        emulator = HereFile(L"shadPS4.exe");
        if (!FileExists(emulator)) {
            emulator = choose ? std::wstring{} : LoadSetting(L"emulator");
            if (!FileExists(emulator)) {
                emulator = ChooseEmulator();
                if (emulator.empty()) {
                    return 1;
                }
            }
        }
    }
    // Only shadPS4 takes --game; other hosts (bbport's bb-probe, bbhost) get their own arguments,
    // and keep our output handles (their launchers log what the game writes).
    bool foreign = false;
    {
        const size_t cut = emulator.find_last_of(L"\\/");
        const std::wstring name = emulator.substr(cut == std::wstring::npos ? 0 : cut + 1);
        foreign = _wcsnicmp(name.c_str(), L"shadps4", 7) != 0;
        has_game = has_game || foreign;
    }
    if (!has_game) {
        std::wstring game = choose ? std::wstring{} : LoadSetting(L"game");
        if (!FileExists(game)) {
            game = ChooseGame();
            if (game.empty()) {
                return 1;
            }
        }
        passthrough.push_back(L"--game");
        passthrough.push_back(game);
    }
    // The BBSC game build does all of this itself; with the mod it must not (the mod does it).
    // Set only if the player did not set them; a build without BBSC ignores them.
    if (!no_mod) {
        DefaultSwitch(L"SHADPS4_BBSC_PERSIST", L"0");
        DefaultSwitch(L"SHADPS4_BLOODBORNE_SEAMLESS_COOP", L"0");
        DefaultSwitch(L"SHADPS4_BLOODBORNE_RE_TRACE", L"0");
    }
    wchar_t full_dll[MAX_PATH]{};
    if (GetFullPathNameW(dll.c_str(), MAX_PATH, full_dll, nullptr) == 0 ||
        (!no_mod && GetFileAttributesW(full_dll) == INVALID_FILE_ATTRIBUTES)) {
        return Fail((L"mod DLL not found: " + dll).c_str());
    }
    if (GetFileAttributesW(emulator.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return Fail((L"emulator not found: " + emulator).c_str());
    }

    std::wstring command = Quote(emulator);
    for (const auto& arg : passthrough) {
        command += L" " + Quote(arg);
    }
    std::wstring working_dir = emulator;
    const size_t slash = working_dir.find_last_of(L"\\/");
    working_dir = slash == std::wstring::npos ? L"." : working_dir.substr(0, slash);
    if (!start_dir.empty()) {
        working_dir = start_dir;
    } else if (bbport) {
        wchar_t current[MAX_PATH]{};
        GetCurrentDirectoryW(MAX_PATH, current);
        working_dir = current;
    }

    STARTUPINFOW startup{sizeof(startup)};
    if (foreign) {
        startup.dwFlags |= STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    }
    PROCESS_INFORMATION info{};
    // bbport: its launcher sends bb-probe's output to user\last_run.log through our handles.
    if (!CreateProcessW(emulator.c_str(), command.data(), nullptr, nullptr, foreign ? TRUE : FALSE,
                        CREATE_SUSPENDED,
                        nullptr, working_dir.c_str(), &startup, &info)) {
        return Fail(L"could not start the emulator");
    }

    // Load the DLL in the suspended emulator: its first thread runs the loader's process setup.
    if (!no_mod) {
    const size_t bytes = (wcslen(full_dll) + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(info.hProcess, nullptr, bytes, MEM_COMMIT | MEM_RESERVE,
                                  PAGE_READWRITE);
    if (remote == nullptr || !WriteProcessMemory(info.hProcess, remote, full_dll, bytes, nullptr)) {
        return Fail(L"could not pass the DLL path to the emulator", info.hProcess);
    }
    auto* load_library = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    HANDLE loader = CreateRemoteThread(info.hProcess, nullptr, 0, load_library, remote, 0, nullptr);
    if (loader == nullptr) {
        return Fail(L"could not load the mod into the emulator", info.hProcess);
    }
    WaitForSingleObject(loader, 30'000);
    DWORD loaded = 0;
    GetExitCodeThread(loader, &loaded);
    CloseHandle(loader);
    VirtualFreeEx(info.hProcess, remote, 0, MEM_RELEASE);
    if (loaded == 0) {
        return Fail(L"the emulator refused the mod DLL (LoadLibraryW returned 0)", info.hProcess);
    }
    } // !no_mod
    ResumeThread(info.hThread);
    CloseHandle(info.hThread);
    if (window_side == L"left" || window_side == L"right" || window_side == L"tl" ||
        window_side == L"tr" || window_side == L"bl" || window_side == L"br") {
        auto* args = new std::pair<DWORD, std::wstring>(info.dwProcessId, window_side);
        if (HANDLE placer = CreateThread(nullptr, 0, PlaceWindowThread, args, 0, nullptr)) {
            CloseHandle(placer);
        }
    }
    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(info.hProcess, &code);
    CloseHandle(info.hProcess);
    return static_cast<int>(code);
}
