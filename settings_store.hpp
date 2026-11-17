#pragma once

#include <windows.h>
#include <shlobj.h>
#include <algorithm>
#include <atomic>
#include <string>
#include <utility>
#include <vector>

namespace restcue {

struct Settings {
    unsigned workMinutes = 45;
    unsigned restSeconds = 300;
    unsigned opacity = 70;
    bool launchAtStartup = false;
    bool closeMinimizes = true;
    bool forceRest = false;
};

struct SaveFailure { std::wstring path; DWORD error; };
struct SaveResult {
    bool success = false;
    std::wstring path;
    std::vector<SaveFailure> failures;
};

class SettingsStore {
public:
    SettingsStore(std::wstring portable = {}, std::wstring user = {}, std::vector<std::wstring> legacy = {})
        : legacyPaths_(std::move(legacy)) {
        if (!portable.empty()) paths_.push_back(std::move(portable));
        if (!user.empty() && (paths_.empty() || user != paths_.front())) paths_.push_back(std::move(user));
    }

    static SettingsStore forApplication() {
        std::wstring portable, user;
        std::vector<std::wstring> legacy;
        std::vector<wchar_t> module(32768);
        const DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
        if (length && length < module.size()) {
            portable.assign(module.data(), length);
            const auto slash = portable.find_last_of(L"\\/");
            if (slash != std::wstring::npos) {
                const auto folder = portable.substr(0, slash + 1);
                portable = folder + L"RestCue.ini";
                legacy.push_back(folder + L"RestFade.ini");
            } else portable.clear();
        }
        wchar_t directory[MAX_PATH]{};
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA,
                                      nullptr, SHGFP_TYPE_CURRENT, directory))) {
            user = std::wstring(directory) + L"\\RestCue\\settings.ini";
            legacy.push_back(std::wstring(directory) + L"\\RestFade\\settings.ini");
        }
        return SettingsStore(std::move(portable), std::move(user), std::move(legacy));
    }

    Settings load() const {
        struct Candidate { std::wstring path; FILETIME modified; bool legacy; };
        std::vector<Candidate> files;
        auto append = [&](const std::vector<std::wstring>& paths, bool legacy) {
            for (const auto& path : paths) {
                WIN32_FILE_ATTRIBUTE_DATA data{};
                if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) &&
                    !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    files.push_back({path, data.ftLastWriteTime, legacy});
                }
            }
        };
        append(paths_, false);
        append(legacyPaths_, true);
        // Prefer valid RestCue files; legacy files are read-only migration sources.
        // Within each group, a newer fallback save must not be shadowed by an older portable file.
        std::stable_sort(files.begin(), files.end(), [](const Candidate& a, const Candidate& b) {
            if (a.legacy != b.legacy) return !a.legacy;
            return CompareFileTime(&a.modified, &b.modified) > 0;
        });
        for (const auto& file : files) {
            HANDLE input = CreateFileW(file.path.c_str(), GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (input == INVALID_HANDLE_VALUE) continue;
            CloseHandle(input);
            const auto work = GetPrivateProfileIntW(L"Timer", L"WorkMinutes", 0, file.path.c_str());
            const auto rest = GetPrivateProfileIntW(L"Timer", L"RestSeconds", 0, file.path.c_str());
            const auto opacity = GetPrivateProfileIntW(L"Timer", L"Opacity", 0, file.path.c_str());
            if (work >= 1 && work <= 1440 && rest >= 1 && rest <= 7200 && opacity >= 10 && opacity <= 95) {
                const auto startup = GetPrivateProfileIntW(L"Timer", L"LaunchAtStartup", 0, file.path.c_str());
                const auto closeMinimizes = GetPrivateProfileIntW(L"Timer", L"CloseMinimizes", 1, file.path.c_str());
                const auto forceRest = GetPrivateProfileIntW(L"Timer", L"ForceRest", 0, file.path.c_str());
                return {static_cast<unsigned>(work), static_cast<unsigned>(rest),
                        static_cast<unsigned>(opacity), startup != 0, closeMinimizes != 0, forceRest != 0};
            }
        }
        return {};
    }

    SaveResult save(const Settings& settings) const {
        SaveResult result;
        for (const auto& path : paths_) {
            DWORD error = ERROR_SUCCESS;
            if (writeAtomic(path, settings, error)) {
                result.success = true;
                result.path = path;
                return result;
            }
            result.failures.push_back({path, error});
        }
        if (paths_.empty()) result.failures.push_back({L"(configuration path unavailable)", ERROR_PATH_NOT_FOUND});
        return result;
    }

private:
    static bool writeAtomic(const std::wstring& path, const Settings& settings, DWORD& error) {
        const auto slash = path.find_last_of(L"\\/");
        if (slash == std::wstring::npos) { error = ERROR_BAD_PATHNAME; return false; }
        const auto directory = path.substr(0, slash);
        if (!CreateDirectoryW(directory.c_str(), nullptr)) {
            const DWORD createError = GetLastError();
            if (createError != ERROR_ALREADY_EXISTS) { error = createError; return false; }
        }
        const std::string data = "[Timer]\r\nWorkMinutes=" + std::to_string(settings.workMinutes) +
            "\r\nRestSeconds=" + std::to_string(settings.restSeconds) +
            "\r\nOpacity=" + std::to_string(settings.opacity) +
            "\r\nLaunchAtStartup=" + (settings.launchAtStartup ? "1" : "0") +
            "\r\nCloseMinimizes=" + (settings.closeMinimizes ? "1" : "0") +
            "\r\nForceRest=" + (settings.forceRest ? "1" : "0") + "\r\n";
        static std::atomic<unsigned long> sequence{0};
        std::wstring temporary;
        HANDLE output = INVALID_HANDLE_VALUE;
        for (unsigned attempt = 0; attempt < 20; ++attempt) {
            temporary = path + L"." + std::to_wstring(GetCurrentProcessId()) + L"." +
                std::to_wstring(sequence.fetch_add(1)) + L".tmp";
            output = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                                 CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (output != INVALID_HANDLE_VALUE) break;
            error = GetLastError();
            if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) return false;
        }
        if (output == INVALID_HANDLE_VALUE) return false;
        DWORD written = 0;
        bool ok = WriteFile(output, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) != FALSE;
        if (!ok) error = GetLastError();
        else if (written != data.size()) { ok = false; error = ERROR_WRITE_FAULT; }
        if (ok && !FlushFileBuffers(output)) { ok = false; error = GetLastError(); }
        if (!CloseHandle(output) && ok) { ok = false; error = GetLastError(); }
        if (ok && !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            ok = false;
            error = GetLastError();
        }
        if (!ok) DeleteFileW(temporary.c_str());
        return ok;
    }

    std::vector<std::wstring> paths_;
    std::vector<std::wstring> legacyPaths_;
};

} // namespace restcue
