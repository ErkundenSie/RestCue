#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../startup.hpp"
#include <cstdio>
#include <cstdlib>

namespace {
unsigned checks = 0;
std::wstring root;

void expect(bool ok, const char* description) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s (Win32=%lu)\n", description, GetLastError());
        std::exit(1);
    }
}

bool exists(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool readShortcut(const std::wstring& linkPath, std::wstring& targetOut, std::wstring& argsOut) {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninit = (init == S_OK || init == S_FALSE);
    HRESULT hr = (init == RPC_E_CHANGED_MODE) ? S_OK : init;
    IShellLinkW* link = nullptr;
    IPersistFile* file = nullptr;
    if (SUCCEEDED(hr))
        hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                              reinterpret_cast<void**>(&link));
    if (SUCCEEDED(hr)) hr = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file));
    if (SUCCEEDED(hr)) hr = file->Load(linkPath.c_str(), STGM_READ);
    wchar_t target[MAX_PATH]{}, args[MAX_PATH]{};
    if (SUCCEEDED(hr)) hr = link->GetPath(target, ARRAYSIZE(target), nullptr, SLGP_RAWPATH);
    if (SUCCEEDED(hr)) hr = link->GetArguments(args, ARRAYSIZE(args));
    if (file) file->Release();
    if (link) link->Release();
    if (uninit) CoUninitialize();
    if (FAILED(hr)) return false;
    targetOut = target;
    argsOut = args;
    return true;
}

void removeTree(const std::wstring& directory) {
    WIN32_FIND_DATAW data{};
    HANDLE search = FindFirstFileW((directory + L"\\*").c_str(), &data);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) continue;
            const auto path = directory + L"\\" + data.cFileName;
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) removeTree(path);
            else { SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileW(path.c_str()); }
        } while (FindNextFileW(search, &data));
        FindClose(search);
    }
    RemoveDirectoryW(directory.c_str());
}

struct Cleanup { ~Cleanup() { if (!root.empty()) removeTree(root); } };
} // namespace

int main() {
    using namespace restcue;

    expect(startupCommandFor(L"").empty(), "empty executable path yields empty startup command");
    expect(startupCommandFor(L"C:\\Program Files\\RestCue\\RestCue.exe") ==
           L"\"C:\\Program Files\\RestCue\\RestCue.exe\" --tray",
           "startup command quotes executable path and passes --tray");

    const auto self = executablePath();
    expect(!self.empty(), "executablePath resolves running binary");
    root = self.substr(0, self.find_last_of(L"\\/")) + L"\\startup-test-" +
        std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    expect(CreateDirectoryW(root.c_str(), nullptr) != FALSE, "create isolated startup test directory");
    Cleanup cleanup;

    const auto linkPath = root + L"\\RestCue.lnk";
    DWORD error = ERROR_SUCCESS;
    expect(!writeStartupShortcutFile(L"", self, true, &error) && error == ERROR_PATH_NOT_FOUND,
           "enabling shortcut without path reports ERROR_PATH_NOT_FOUND");
    expect(!writeStartupShortcutFile(linkPath, L"", true, &error) && error == ERROR_INSUFFICIENT_BUFFER,
           "enabling shortcut without executable reports ERROR_INSUFFICIENT_BUFFER");
    expect(writeStartupShortcutFile(linkPath, self, true, &error) && exists(linkPath),
           "writeStartupShortcutFile creates .lnk file");

    std::wstring loadedTarget, loadedArgs;
    expect(readShortcut(linkPath, loadedTarget, loadedArgs), "saved .lnk can be loaded via IShellLinkW");
    expect(CompareStringOrdinal(loadedTarget.c_str(), -1, self.c_str(), -1, TRUE) == CSTR_EQUAL,
           "shortcut target matches executable path");
    expect(loadedArgs == L"--tray", "shortcut passes --tray argument");

    expect(writeStartupShortcutFile(linkPath, self, false, &error) && !exists(linkPath),
           "disabling shortcut deletes .lnk file");
    expect(writeStartupShortcutFile(linkPath, self, false, &error),
           "disabling absent shortcut is idempotent");
    expect(writeStartupShortcutFile(L"", self, false, &error),
           "disabling with empty shortcut path succeeds cleanly");

    // Verify primary registry -> shortcut fallback -> cleanup orchestration.
    bool regWritable = false;
    bool regPresent = false;
    StartupManager manager(
        [&](bool enabled, DWORD* err) {
            if (!regWritable) {
                if (err) *err = ERROR_ACCESS_DENIED;
                return false;
            }
            regPresent = enabled;
            return true;
        },
        [&](bool enabled, DWORD* err) {
            return writeStartupShortcutFile(linkPath, self, enabled, err);
        },
        [&]() { return regPresent; },
        [&]() { return regPresent; });

    expect(manager.apply(true, &error) && exists(linkPath) && !regPresent,
           "blocked registry falls back to creating startup shortcut");
    expect(manager.apply(false, &error) && !exists(linkPath),
           "disabling startup succeeds even when registry write is blocked if entry is absent");

    expect(writeStartupShortcutFile(linkPath, self, true, &error) && exists(linkPath),
           "seed stale shortcut before registry success");
    regWritable = true;
    expect(manager.apply(true, &error) && regPresent && !exists(linkPath),
           "successful registry registration cleans up stale shortcut to prevent double launch");
    expect(manager.apply(false, &error) && !regPresent && !exists(linkPath),
           "disabling clears registry entry");

    // Verify already-matching registry acceptance and failure reporting when all backends fail.
    const auto brokenLink = root + L"\\missing-dir\\nested\\RestCue.lnk";
    regWritable = false;
    regPresent = true;
    StartupManager readOnlyMatching(
        [&](bool, DWORD* err) { if (err) *err = ERROR_ACCESS_DENIED; return false; },
        [&](bool enabled, DWORD* err) { return writeStartupShortcutFile(brokenLink, self, enabled, err); },
        [&]() { return regPresent; },
        [&]() { return regPresent; });
    expect(readOnlyMatching.apply(true, &error),
           "enabling succeeds when Run value already matches despite write lock");
    error = ERROR_SUCCESS;
    expect(!readOnlyMatching.apply(false, &error) && error == ERROR_ACCESS_DENIED,
           "disabling reports error when locked registry still holds an active entry");

    regPresent = false;
    error = ERROR_SUCCESS;
    expect(!readOnlyMatching.apply(true, &error) && error == ERROR_ACCESS_DENIED,
           "enabling reports primary error when both registry and shortcut fail");

    std::printf("%u startup checks passed\n", checks);
    return 0;
}
