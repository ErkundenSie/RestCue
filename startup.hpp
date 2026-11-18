#pragma once

#include <windows.h>
#include <winreg.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>
#include <cwchar>
#include <functional>
#include <string>
#include <utility>

namespace restcue {

inline std::wstring executablePath() {
    std::wstring module(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (!length || length >= module.size()) return {};
    module.resize(length);
    return module;
}

inline std::wstring startupCommandFor(const std::wstring& exePath) {
    return exePath.empty() ? std::wstring{} : L"\"" + exePath + L"\" --tray";
}

inline bool runRegistryMatches(HKEY root, const wchar_t* subKey, const wchar_t* valueName,
                               const std::wstring& expected) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    DWORD bytes = 0, type = 0;
    LONG status = RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &bytes);
    if (status != ERROR_SUCCESS || type != REG_SZ || bytes < sizeof(wchar_t)) {
        RegCloseKey(key);
        return false;
    }
    std::wstring data(bytes / sizeof(wchar_t), L'\0');
    status = RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<BYTE*>(data.data()), &bytes);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) return false;
    data.resize(wcslen(data.c_str()));
    return data == expected;
}

inline bool hasRunRegistryValue(HKEY root, const wchar_t* subKey, const wchar_t* valueName) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    const LONG status = RegQueryValueExW(key, valueName, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

inline bool writeRunRegistryValue(HKEY root, const wchar_t* subKey, const wchar_t* valueName,
                                  const std::wstring& command, bool enabled, DWORD* errorOut = nullptr) {
    HKEY key = nullptr;
    LONG status = RegOpenKeyExW(root, subKey, 0, KEY_SET_VALUE, &key);
    if (status != ERROR_SUCCESS) {
        status = RegCreateKeyExW(root, subKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    }
    if (status != ERROR_SUCCESS) {
        if (errorOut) *errorOut = static_cast<DWORD>(status);
        return false;
    }
    if (!enabled) {
        status = RegDeleteValueW(key, valueName);
        RegCloseKey(key);
        if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
        if (errorOut) *errorOut = static_cast<DWORD>(status);
        return status == ERROR_SUCCESS;
    }
    if (command.empty()) {
        RegCloseKey(key);
        if (errorOut) *errorOut = ERROR_INSUFFICIENT_BUFFER;
        return false;
    }
    status = RegSetValueExW(key, valueName, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(command.c_str()),
        static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (errorOut) *errorOut = static_cast<DWORD>(status);
    return status == ERROR_SUCCESS;
}

inline bool writeStartupShortcutFile(const std::wstring& linkPath, const std::wstring& exePath,
                                     bool enabled, DWORD* errorOut = nullptr) {
    if (!enabled) {
        if (linkPath.empty()) return true;
        if (DeleteFileW(linkPath.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND ||
            GetLastError() == ERROR_PATH_NOT_FOUND)
            return true;
        if (errorOut) *errorOut = GetLastError();
        return false;
    }
    if (linkPath.empty()) {
        if (errorOut) *errorOut = ERROR_PATH_NOT_FOUND;
        return false;
    }
    if (exePath.empty()) {
        if (errorOut) *errorOut = ERROR_INSUFFICIENT_BUFFER;
        return false;
    }
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninit = (init == S_OK || init == S_FALSE);
    HRESULT hr = (init == RPC_E_CHANGED_MODE) ? S_OK : init;
    IShellLinkW* link = nullptr;
    if (SUCCEEDED(hr))
        hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                              reinterpret_cast<void**>(&link));
    if (SUCCEEDED(hr)) hr = link->SetPath(exePath.c_str());
    if (SUCCEEDED(hr)) hr = link->SetArguments(L"--tray");
    if (SUCCEEDED(hr)) hr = link->SetDescription(L"RestCue");
    IPersistFile* file = nullptr;
    if (SUCCEEDED(hr)) hr = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file));
    if (SUCCEEDED(hr)) hr = file->Save(linkPath.c_str(), TRUE);
    if (file) file->Release();
    if (link) link->Release();
    if (uninit) CoUninitialize();
    if (FAILED(hr)) {
        if (errorOut) *errorOut = HRESULT_CODE(hr);
        return false;
    }
    return true;
}

class StartupManager {
public:
    using WriteStep = std::function<bool(bool enabled, DWORD* errorOut)>;
    using QueryStep = std::function<bool()>;

    StartupManager() = default;

    StartupManager(WriteStep writeRegistry, WriteStep writeShortcut,
                   QueryStep matchesExpected, QueryStep hasRegistryEntry = {})
        : writeRegistry_(std::move(writeRegistry)),
          writeShortcut_(std::move(writeShortcut)),
          matchesExpected_(std::move(matchesExpected)),
          hasRegistryEntry_(std::move(hasRegistryEntry)) {}

    static StartupManager forApplication() {
        constexpr const wchar_t* RunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
        constexpr const wchar_t* ValueName = L"RestCue";
        const auto exe = executablePath();
        const auto command = startupCommandFor(exe);
        std::wstring shortcut;
        wchar_t startupDir[MAX_PATH]{};
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_STARTUP, nullptr, SHGFP_TYPE_CURRENT, startupDir))) {
            shortcut = std::wstring(startupDir) + L"\\RestCue.lnk";
        }
        return StartupManager(
            [command](bool enabled, DWORD* err) {
                return writeRunRegistryValue(HKEY_CURRENT_USER, RunKey, ValueName, command, enabled, err);
            },
            [shortcut, exe](bool enabled, DWORD* err) {
                return writeStartupShortcutFile(shortcut, exe, enabled, err);
            },
            [command]() {
                return !command.empty() && runRegistryMatches(HKEY_CURRENT_USER, RunKey, ValueName, command);
            },
            []() {
                return hasRunRegistryValue(HKEY_CURRENT_USER, RunKey, ValueName);
            });
    }

    bool apply(bool enabled, DWORD* errorOut = nullptr) const {
        DWORD registryError = ERROR_ACCESS_DENIED, shortcutError = ERROR_ACCESS_DENIED;
        if (enabled) {
            if (writeRegistry_ && writeRegistry_(true, &registryError)) {
                if (writeShortcut_) writeShortcut_(false, nullptr);
                return true;
            }
            if (writeShortcut_ && writeShortcut_(true, &shortcutError)) return true;
            if (matchesExpected_ && matchesExpected_()) return true;
            if (errorOut) *errorOut = registryError ? registryError : shortcutError;
            return false;
        }
        const bool regOk = !writeRegistry_ || writeRegistry_(false, &registryError) ||
                           (hasRegistryEntry_ && !hasRegistryEntry_());
        const bool lnkOk = !writeShortcut_ || writeShortcut_(false, &shortcutError);
        if (regOk && lnkOk) return true;
        if (errorOut) *errorOut = !regOk ? registryError : shortcutError;
        return false;
    }

private:
    WriteStep writeRegistry_;
    WriteStep writeShortcut_;
    QueryStep matchesExpected_;
    QueryStep hasRegistryEntry_;
};

} // namespace restcue
