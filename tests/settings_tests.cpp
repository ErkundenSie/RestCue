#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../settings_store.hpp"
#include <cstdio>
#include <cstdlib>

namespace {
unsigned checks = 0;
std::wstring root;
void expect(bool ok, const char* description) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAILED: %s (Win32=%lu)\n", description, GetLastError()); std::exit(1); }
}
bool same(const restcue::Settings& a, const restcue::Settings& b) {
    return a.workMinutes == b.workMinutes && a.restSeconds == b.restSeconds &&
           a.opacity == b.opacity && a.launchAtStartup == b.launchAtStartup &&
           a.closeMinimizes == b.closeMinimizes && a.forceRest == b.forceRest;
}
void touch(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    expect(file != INVALID_HANDLE_VALUE, "create blocking fixture");
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
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
}

int main() {
    using restcue::Settings;
    using restcue::SettingsStore;
    wchar_t module[32768]{};
    GetModuleFileNameW(nullptr, module, ARRAYSIZE(module));
    root = module;
    root = root.substr(0, root.find_last_of(L"\\/")) + L"\\settings-test-" +
        std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    expect(CreateDirectoryW(root.c_str(), nullptr) != FALSE, "create isolated test directory");
    Cleanup cleanup;
    const auto portable = root + L"\\portable\\RestCue.ini";
    const auto user = root + L"\\user\\settings.ini";
    SettingsStore store(portable, user);
    const Settings original{20, 300, 50}, updated{25, 90, 65};
    expect(same(store.load(), Settings{}), "missing files use defaults");
    auto result = store.save(original);
    expect(result.success && result.path == portable, "prefer portable configuration");
    expect(same(SettingsStore(portable, user).load(), original), "configuration survives store recreation");
    result = store.save(original);
    expect(result.success, "saving unchanged settings succeeds");
    result = store.save(updated);
    expect(result.success && same(SettingsStore(portable, user).load(), updated), "repeated saves restore all updated fields");

    const auto blocker = root + L"\\blocked";
    touch(blocker);
    const auto blockedPath = blocker + L"\\RestCue.ini";
    SettingsStore fallback(blockedPath, user);
    result = fallback.save(original);
    expect(result.success && result.path == user && result.failures.size() == 1, "blocked portable path falls back to user path");
    expect(result.failures.front().error != ERROR_SUCCESS, "fallback retains a useful Win32 error");
    expect(same(SettingsStore(blockedPath, user).load(), original), "fallback configuration survives recreation");

    expect(SetFileAttributesW(portable.c_str(), FILE_ATTRIBUTE_READONLY) != FALSE, "make portable file read-only");
    Sleep(30);
    result = store.save(original);
    expect(result.success && result.path == user, "read-only portable file falls back without overwriting it");
    expect(same(SettingsStore(portable, user).load(), original), "new fallback takes precedence over stale portable file");
    expect(same(SettingsStore(portable, L"").load(), updated), "failed atomic replacement preserves the old file");
    SettingsStore blocked(portable, blockedPath);
    result = blocked.save(Settings{30, 600, 70});
    expect(!result.success && result.failures.size() == 2, "all blocked destinations report failure");
    expect(same(SettingsStore(portable, L"").load(), updated), "total save failure does not corrupt existing settings");
    WIN32_FIND_DATAW data{};
    HANDLE leftovers = FindFirstFileW((portable + L".*.tmp").c_str(), &data);
    expect(leftovers == INVALID_HANDLE_VALUE, "failed saves clean up temporary files");
    if (leftovers != INVALID_HANDLE_VALUE) FindClose(leftovers);
    SetFileAttributesW(portable.c_str(), FILE_ATTRIBUTE_NORMAL);

    const auto unicode = root + L"\\配置\\RestCue.ini";
    SettingsStore unicodeStore(unicode);
    expect(unicodeStore.save(updated).success, "Unicode configuration path saves");
    expect(same(SettingsStore(unicode).load(), updated), "Unicode configuration path loads");
    SettingsStore empty;
    result = empty.save(original);
    expect(!result.success && !result.failures.empty(), "missing destination produces a diagnostic");
    // Keep legacy product names only as migration fixtures.
    const auto legacyPath = root + L"\\legacy\\RestFade.ini";
    SettingsStore legacyStore(legacyPath);
    expect(legacyStore.save(original).success, "create legacy settings fixture");
    const auto renamedPath = root + L"\\renamed\\RestCue.ini";
    const auto renamedUserPath = root + L"\\renamed-user\\settings.ini";
    SettingsStore renamed(renamedPath, renamedUserPath, {legacyPath});
    expect(same(renamed.load(), original), "read legacy settings when new configuration is absent");
    const auto migrated = renamed.save(updated);
    expect(migrated.success && migrated.path == renamedPath, "save migrated settings under the new product name");
    expect(same(legacyStore.load(), original), "migration does not overwrite legacy settings");
    const Settings newerLegacy{31, 180, 75};
    Sleep(30);
    expect(legacyStore.save(newerLegacy).success, "update legacy fixture independently");
    expect(same(SettingsStore(renamedPath, renamedUserPath, {legacyPath}).load(), updated),
           "new product configuration takes priority over newer legacy files");
    SettingsStore blockedMigration(blockedPath, L"", {legacyPath});
    expect(!blockedMigration.save(original).success, "blocked new destinations do not write to legacy paths");
    expect(same(legacyStore.load(), newerLegacy), "failed save leaves legacy data intact");
    const auto malformed = root + L"\\malformed\\RestCue.ini";
    expect(SettingsStore(malformed).save(Settings{0, 0, 0}).success, "create invalid new configuration fixture");
    expect(same(SettingsStore(malformed, L"", {legacyPath}).load(), newerLegacy),
           "invalid new configuration falls back to valid legacy settings");
    expect(same(SettingsStore(blockedPath, L"", {malformed}).load(), Settings{}),
           "invalid legacy settings fall back to defaults");
    Settings startupOn = updated;
    startupOn.launchAtStartup = true;
    expect(store.save(startupOn).success && same(SettingsStore(portable, user).load(), startupOn),
           "startup preference round-trips");
    expect(store.save(updated).success && !SettingsStore(portable, user).load().launchAtStartup,
           "startup preference can be turned off");
    std::printf("%u settings checks passed\n", checks);
}
