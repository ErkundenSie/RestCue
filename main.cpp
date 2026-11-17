#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winreg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>
#include <shlobj.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <algorithm>
#include <cwchar>
#include <string>
#include <vector>
#include "timer.hpp"
#include "settings_store.hpp"
#include "theme.hpp"
#include "resource.h"
#include <strsafe.h>

#ifdef _MSC_VER
#pragma comment(lib, "advapi32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

namespace {
constexpr wchar_t MainClass[] = L"RestCue.MainWindow";
constexpr wchar_t OverlayClass[] = L"RestCue.Overlay";
constexpr wchar_t RestCardClass[] = L"RestCue.RestCard";
constexpr wchar_t SliderClass[] = L"RestCue.OpacitySlider";
constexpr wchar_t Title[] = L"RestCue · 休息提醒";
constexpr UINT TrayMessage = WM_APP + 1;
constexpr UINT_PTR TickId = 1;
constexpr int WorkEdit = 101, RestMinEdit = 102, RestSecEdit = 103, OpacityEdit = 104, OpacitySlider = 105;
constexpr int IncreaseWork = 211, DecreaseWork = 212;
constexpr int Apply = 201, Toggle = 202, RestNow = 203, Skip = 204, Startup = 206, CloseMinimizes = 207, ForceRest = 208;
constexpr int Status = 301, Open = 302, Exit = 303, Stop = 304;
constexpr DWORD MainStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
constexpr DWORD MainExStyle = WS_EX_CONTROLPARENT;

using Settings = restcue::Settings;
Settings settings;
restcue::SettingsStore settingsStore;

struct Control { HWND hwnd; int x, y, width, height; bool heading; };
struct RestWindow {
    explicit RestWindow(bool isCard) : card(isCard) {}
    bool card;
    BYTE alpha = 0;
    restcue::theme::OverlayScene scene;
};
HINSTANCE instance = nullptr;
HWND mainWindow = nullptr;
HWND previousForeground = nullptr;
HFONT uiFont = nullptr, headingFont = nullptr;
HBRUSH darkBrush = nullptr, editBrush = nullptr;
std::vector<Control> controls;
std::vector<HWND> overlays, restCards;
bool restoreSettingsAfterRest = false;
restcue::Timer timer;
std::wstring lastStatus;
NOTIFYICONDATAW tray{};
bool trayVisible = false;
UINT taskbarCreated = 0;
ULONGLONG overlayStarted = 0;
std::uint64_t lastOverlayFrame = 0;

void refresh();
void command(int id);
void updateRestCue(HWND hwnd, BYTE alpha);
LRESULT CALLBACK overlayProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK sliderProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK buttonProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

UINT dpiFor(HWND hwnd) {
    using GetDpi = UINT(WINAPI*)(HWND);
    static auto getDpi = reinterpret_cast<GetDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (getDpi && hwnd) {
        const UINT dpi = getDpi(hwnd);
        if (dpi) return dpi;
    }
    HDC dc = GetDC(hwnd);
    const UINT dpi = dc ? static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSY)) : 96;
    if (dc) ReleaseDC(hwnd, dc);
    return dpi ? dpi : 96;
}

int scaled(int value, UINT dpi) {
    return MulDiv(value, static_cast<int>(dpi) * restcue::theme::SettingsScalePercent, 9600);
}

void adjustWindowForDpi(RECT& rect, UINT dpi) {
    using Adjust = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    static auto adjust = reinterpret_cast<Adjust>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
    if (!adjust || !adjust(&rect, MainStyle, FALSE, MainExStyle, dpi))
        AdjustWindowRectEx(&rect, MainStyle, FALSE, MainExStyle);
}

int contentSpan(HWND hwnd) {
    RECT client{};
    GetClientRect(hwnd, &client);
    return std::max(1, static_cast<int>(client.right));
}

int place(int value, int span) {
    return MulDiv(value, span, static_cast<int>(restcue::theme::MainWidth));
}

HICON loadAppIcon(int width, int height) {
    HICON icon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_RESTCUE),
                                               IMAGE_ICON, width, height, LR_SHARED));
    return icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);
}

HFONT makeFont(int points, int span, int weight = FW_NORMAL) {
    const int design = static_cast<int>(restcue::theme::MainWidth);
    return CreateFontW(-MulDiv(points, span * 96, design * 72), 0, 0, 0, weight,
                       FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
}

void layoutControls() {
    const int span = contentSpan(mainWindow);
    HFONT oldUi = uiFont, oldHeading = headingFont;
    uiFont = makeFont(16, span);
    headingFont = makeFont(18, span, FW_SEMIBOLD);
    for (const auto& c : controls) {
        MoveWindow(c.hwnd, place(c.x, span), place(c.y, span),
                   place(c.width, span), place(c.height, span), TRUE);
        SendMessageW(c.hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(c.heading ? headingFont : uiFont), TRUE);
        InvalidateRect(c.hwnd, nullptr, FALSE);
    }
    if (oldUi) DeleteObject(oldUi);
    if (oldHeading) DeleteObject(oldHeading);
}

bool fittingWindow = false;

// Keep the client exactly scaled(MainWidth/MainHeight, dpi). Windows' suggested
// WM_DPICHANGED size scales the outer frame linearly, so the client drifts and the
// painted panel no longer lines up with the child controls.
void fitMainWindow(HWND hwnd, UINT dpi, const RECT* suggested) {
    if (!hwnd || !dpi || fittingWindow) return;
    fittingWindow = true;
    UINT target = dpi;
    bool move = suggested != nullptr;
    const int x = suggested ? suggested->left : 0;
    const int y = suggested ? suggested->top : 0;
    bool changed = move;
    for (int pass = 0; pass < 2; ++pass) {
        const int wantW = scaled(static_cast<int>(restcue::theme::MainWidth), target);
        const int wantH = scaled(static_cast<int>(restcue::theme::MainHeight), target);
        RECT client{};
        GetClientRect(hwnd, &client);
        if (move) {
            RECT rect{0, 0, wantW, wantH};
            adjustWindowForDpi(rect, target);
            SetWindowPos(hwnd, nullptr, x, y, rect.right - rect.left, rect.bottom - rect.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            move = false;
            GetClientRect(hwnd, &client);
        }
        if (client.right != wantW || client.bottom != wantH) {
            RECT wr{};
            GetWindowRect(hwnd, &wr);
            SetWindowPos(hwnd, nullptr, 0, 0,
                         (wr.right - wr.left) + (wantW - client.right),
                         (wr.bottom - wr.top) + (wantH - client.bottom),
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            changed = true;
        }
        const UINT actual = dpiFor(hwnd);
        if (!actual || actual == target) break;
        target = actual;
    }
    if (changed) layoutControls();
    fittingWindow = false;
    if (changed)
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME | RDW_UPDATENOW);
}

LRESULT CALLBACK childDpiProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR) {
    // Per-monitor v2 asks DefWindowProc to rescale child windows after the parent has
    // already placed them. Swallow that so a monitor change cannot shift the controls
    // off the painted cards.
    if (message == WM_DPICHANGED || message == WM_DPICHANGED_BEFOREPARENT) return 0;
    if (message == WM_GETDPISCALEDSIZE) {
        RECT rect{};
        GetWindowRect(hwnd, &rect);
        if (auto* size = reinterpret_cast<SIZE*>(lParam)) {
            size->cx = rect.right - rect.left;
            size->cy = rect.bottom - rect.top;
        }
        return TRUE;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, childDpiProc, id);
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

HWND addControl(const wchar_t* cls, const wchar_t* text, int id,
                int x, int y, int w, int h, DWORD style = 0, bool heading = false) {
    HWND hwnd = CreateWindowExW(0,
        cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, mainWindow,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    controls.push_back({hwnd, x, y, w, h, heading});
    SetWindowSubclass(hwnd, childDpiProc, 2, 0);
    if (wcscmp(cls, L"BUTTON") == 0) SetWindowSubclass(hwnd, buttonProc, 1, 0);
    return hwnd;
}

void setNumber(int id, unsigned value) {
    SetDlgItemTextW(mainWindow, id, std::to_wstring(value).c_str());
}

bool readNumber(int id, unsigned& value) {
    wchar_t text[32]{};
    const int length = GetWindowTextLengthW(GetDlgItem(mainWindow, id));
    if (length < 1 || length > 6) return false;
    GetDlgItemTextW(mainWindow, id, text, 32);
    value = 0;
    for (int i = 0; i < length; ++i) {
        if (text[i] < L'0' || text[i] > L'9') return false;
        value = value * 10 + static_cast<unsigned>(text[i] - L'0');
    }
    return true;
}

void loadSettings() {
    settingsStore = restcue::SettingsStore::forApplication();
    settings = settingsStore.load();
}

std::wstring modulePath() {
    wchar_t module[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, module, ARRAYSIZE(module));
    if (!length || length >= ARRAYSIZE(module)) return {};
    return module;
}

std::wstring startupCommand() {
    const auto path = modulePath();
    return path.empty() ? path : L"\"" + path + L"\" --tray";
}

bool runValueMatches(const std::wstring& command) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    DWORD bytes = 0, type = 0;
    LONG status = RegQueryValueExW(key, L"RestCue", nullptr, &type, nullptr, &bytes);
    if (status != ERROR_SUCCESS || type != REG_SZ || bytes < sizeof(wchar_t)) {
        RegCloseKey(key);
        return false;
    }
    std::wstring data(bytes / sizeof(wchar_t), L'\0');
    status = RegQueryValueExW(key, L"RestCue", nullptr, &type, reinterpret_cast<BYTE*>(data.data()), &bytes);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) return false;
    data.resize(wcslen(data.c_str()));
    return data == command;
}

bool writeRunKey(bool enabled, DWORD* errorOut) {
    HKEY key = nullptr;
    LONG status = RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &key);
    if (status != ERROR_SUCCESS) {
        status = RegCreateKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    }
    if (status != ERROR_SUCCESS) {
        if (errorOut) *errorOut = static_cast<DWORD>(status);
        return false;
    }
    if (!enabled) {
        status = RegDeleteValueW(key, L"RestCue");
        RegCloseKey(key);
        if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
        if (errorOut) *errorOut = static_cast<DWORD>(status);
        return status == ERROR_SUCCESS;
    }
    const auto command = startupCommand();
    if (command.empty()) {
        RegCloseKey(key);
        if (errorOut) *errorOut = ERROR_INSUFFICIENT_BUFFER;
        return false;
    }
    status = RegSetValueExW(key, L"RestCue", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(command.c_str()),
        static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (errorOut) *errorOut = static_cast<DWORD>(status);
    return status == ERROR_SUCCESS;
}

bool writeStartupShortcut(bool enabled, DWORD* errorOut) {
    wchar_t startup[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_STARTUP, nullptr, SHGFP_TYPE_CURRENT, startup))) {
        if (errorOut) *errorOut = ERROR_PATH_NOT_FOUND;
        return false;
    }
    const std::wstring linkPath = std::wstring(startup) + L"\\RestCue.lnk";
    if (!enabled) {
        if (DeleteFileW(linkPath.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND) return true;
        if (errorOut) *errorOut = GetLastError();
        return false;
    }
    const auto path = modulePath();
    if (path.empty()) {
        if (errorOut) *errorOut = ERROR_INSUFFICIENT_BUFFER;
        return false;
    }
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninit = init == S_OK;
    HRESULT hr = (init == RPC_E_CHANGED_MODE) ? S_OK : init;
    IShellLinkW* link = nullptr;
    if (SUCCEEDED(hr))
        hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                              reinterpret_cast<void**>(&link));
    if (SUCCEEDED(hr)) hr = link->SetPath(path.c_str());
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

bool setLaunchAtStartup(bool enabled, DWORD* errorOut) {
    DWORD registryError = ERROR_ACCESS_DENIED, shortcutError = ERROR_ACCESS_DENIED;
    if (writeRunKey(enabled, &registryError)) return true;
    if (writeStartupShortcut(enabled, &shortcutError)) return true;
    const auto command = startupCommand();
    if (enabled && !command.empty() && runValueMatches(command)) return true;
    if (errorOut) *errorOut = registryError ? registryError : shortcutError;
    return false;
}

bool boxChecked(int id) {
    return SendMessageW(GetDlgItem(mainWindow, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
}

bool applySettings() {
    unsigned work = 0, minutes = 0, seconds = 0, opacity = 0;
    if (!readNumber(WorkEdit, work) || !readNumber(RestMinEdit, minutes) ||
        !readNumber(RestSecEdit, seconds) || !readNumber(OpacityEdit, opacity) ||
        work < 1 || work > 1440 || minutes > 120 || seconds > 59 ||
        minutes * 60 + seconds < 1 || minutes * 60 + seconds > 7200 ||
        opacity < 10 || opacity > 95) {
        MessageBoxW(mainWindow,
            L"请输入有效设置：\n\n工作间隔：1～1440 分钟\n休息时长：1 秒～120 分钟（秒数填 0～59）\n遮罩浓度：10～95%",
            Title, MB_OK | MB_ICONWARNING);
        return false;
    }
    Settings updated{work, minutes * 60 + seconds, opacity, boxChecked(Startup), boxChecked(CloseMinimizes),
                    boxChecked(ForceRest)};
    const auto saved = settingsStore.save(updated);
    if (!saved.success) {
        std::wstring message = L"无法保存设置。本次运行仍会生效，但重启后可能无法恢复。\n\n";
        for (const auto& failure : saved.failures) {
            wchar_t description[512]{};
            FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr, failure.error, 0, description, ARRAYSIZE(description), nullptr);
            message += failure.path + L"\n错误 " + std::to_wstring(failure.error) + L"：" + description + L"\n";
        }
        message += L"请检查上述目录的写入权限或文件是否只读；可将程序放在可写目录中运行。";
        MessageBoxW(mainWindow, message.c_str(), Title, MB_OK | MB_ICONWARNING);
    } else if (updated.launchAtStartup != settings.launchAtStartup || updated.launchAtStartup) {
        DWORD startupError = ERROR_SUCCESS;
        if (!setLaunchAtStartup(updated.launchAtStartup, &startupError)) {
            wchar_t description[512]{};
            FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr, startupError, 0, description, ARRAYSIZE(description), nullptr);
            const std::wstring message = L"设置已保存，但无法写入开机自启。\n错误 " +
                std::to_wstring(startupError) + L"：" + description;
            MessageBoxW(mainWindow, message.c_str(), Title, MB_OK | MB_ICONWARNING);
        }
    }
    settings = updated;
    return true;
}

void createControls() {
    namespace layout = restcue::theme::settingsLayout;
    const DWORD editStyle = WS_TABSTOP | ES_NUMBER | ES_CENTER | ES_AUTOHSCROLL;
    const DWORD buttonStyle = WS_TABSTOP | BS_OWNERDRAW;
    auto add = [&](const wchar_t* cls, const wchar_t* label, int id, layout::Placement p, DWORD style) {
        return addControl(cls, label, id, p.x, p.y, p.width, p.height, style);
    };
    add(L"EDIT", L"", WorkEdit, layout::Work, editStyle);
    add(L"BUTTON", L"增加专注分钟", IncreaseWork, layout::WorkUp, BS_OWNERDRAW);
    add(L"BUTTON", L"减少专注分钟", DecreaseWork, layout::WorkDown, BS_OWNERDRAW);
    add(L"EDIT", L"", RestMinEdit, layout::RestMinutes, editStyle);
    add(L"EDIT", L"", RestSecEdit, layout::RestSeconds, editStyle);
    add(SliderClass, L"遮罩浓度", OpacitySlider, layout::OpacityTrack, WS_TABSTOP);
    add(L"EDIT", L"", OpacityEdit, layout::OpacityValue, editStyle);
    // Retain a standard text control for automation/accessibility status queries.
    addControl(L"STATIC", L"", Status, 0, 0, 0, 0);
    add(L"BUTTON", L"开机自启", Startup, layout::Startup, WS_TABSTOP | BS_AUTOCHECKBOX);
    add(L"BUTTON", L"关闭时最小化到托盘", CloseMinimizes, layout::CloseMinimizes, WS_TABSTOP | BS_AUTOCHECKBOX);
    add(L"BUTTON", L"强制休息", ForceRest, layout::ForceRest, WS_TABSTOP | BS_AUTOCHECKBOX);
    add(L"BUTTON", L"保存", Apply, layout::Apply, buttonStyle);
    add(L"BUTTON", L"暂停", Toggle, layout::Pause, buttonStyle);
    add(L"BUTTON", L"立即休息", RestNow, layout::Rest, buttonStyle);
    add(L"BUTTON", L"跳过休息", Skip, layout::Skip, buttonStyle);
    for (int id : {WorkEdit, RestMinEdit, RestSecEdit, OpacityEdit}) {
        SendDlgItemMessageW(mainWindow, id, EM_SETLIMITTEXT, 6, 0);
    }
    setNumber(WorkEdit, settings.workMinutes);
    setNumber(RestMinEdit, settings.restSeconds / 60);
    setNumber(RestSecEdit, settings.restSeconds % 60);
    setNumber(OpacityEdit, settings.opacity);
    SendDlgItemMessageW(mainWindow, Startup, BM_SETCHECK,
                        settings.launchAtStartup ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(mainWindow, CloseMinimizes, BM_SETCHECK,
                        settings.closeMinimizes ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(mainWindow, ForceRest, BM_SETCHECK,
                        settings.forceRest ? BST_CHECKED : BST_UNCHECKED, 0);
    layoutControls();
}

bool trayPromoted = false;
int trayPromoteTries = 0;

bool sameExePath(const wchar_t* left, const wchar_t* right) {
    return left && right && CompareStringOrdinal(left, -1, right, -1, TRUE) == CSTR_EQUAL;
}

// Windows 11 puts new icons behind the overflow arrow. IsPromoted=1 shows ours
// in the visible tray. IsPromoted=0 is the user hiding it, so leave that alone.
void promoteTrayIcon() {
    if (trayPromoted || !trayVisible || trayPromoteTries > 200) return;
    ++trayPromoteTries;
    const auto path = modulePath();
    if (path.empty()) return;
    HKEY root = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\NotifyIconSettings", 0,
                      KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE, &root) != ERROR_SUCCESS)
        return;
    wchar_t name[256];
    for (DWORD index = 0;; ++index) {
        DWORD nameLen = ARRAYSIZE(name);
        if (RegEnumKeyExW(root, index, name, &nameLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        HKEY sub = nullptr;
        if (RegOpenKeyExW(root, name, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &sub) != ERROR_SUCCESS) continue;
        wchar_t exe[1024]{};
        DWORD bytes = sizeof(exe), type = 0;
        const LONG query = RegQueryValueExW(sub, L"ExecutablePath", nullptr, &type,
                                             reinterpret_cast<BYTE*>(exe), &bytes);
        if (query == ERROR_SUCCESS && type == REG_SZ && sameExePath(exe, path.c_str())) {
            DWORD promoted = 0, pbytes = sizeof(promoted), ptype = 0;
            const LONG read = RegQueryValueExW(sub, L"IsPromoted", nullptr, &ptype,
                                                reinterpret_cast<BYTE*>(&promoted), &pbytes);
            if (!(read == ERROR_SUCCESS && ptype == REG_DWORD && promoted == 0)) {
                if (!(read == ERROR_SUCCESS && ptype == REG_DWORD && promoted == 1)) {
                    const DWORD show = 1;
                    RegSetValueExW(sub, L"IsPromoted", 0, REG_DWORD,
                                   reinterpret_cast<const BYTE*>(&show), sizeof(show));
                }
            }
            trayPromoted = true;
        }
        RegCloseKey(sub);
        if (trayPromoted) break;
    }
    RegCloseKey(root);
}

void addTray() {
    tray = {};
    tray.cbSize = sizeof(tray);
    tray.hWnd = mainWindow;
    tray.uID = 1;
    tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    tray.uCallbackMessage = TrayMessage;
    const int iconSize = GetSystemMetrics(SM_CXSMICON);
    tray.hIcon = loadAppIcon(iconSize, iconSize);
    if (!tray.hIcon) tray.hIcon = loadAppIcon(16, 16);
    if (!tray.hIcon) tray.hIcon = loadAppIcon(24, 24);
    StringCchCopyW(tray.szTip, ARRAYSIZE(tray.szTip), Title);
    trayVisible = Shell_NotifyIconW(NIM_ADD, &tray) != FALSE;
    if (!trayVisible) trayVisible = Shell_NotifyIconW(NIM_MODIFY, &tray) != FALSE;
    trayPromoted = false;
    trayPromoteTries = 0;
    if (trayVisible) promoteTrayIcon();
    else ShowWindow(mainWindow, SW_RESTORE);
}

std::wstring clockText(std::uint64_t milliseconds) {
    const auto seconds = (milliseconds + 999) / 1000;
    wchar_t buffer[40]{};
    swprintf_s(buffer, ARRAYSIZE(buffer), L"%02llu:%02llu",
               static_cast<unsigned long long>(seconds / 60),
               static_cast<unsigned long long>(seconds % 60));
    return buffer;
}

void showMain() {
    ShowWindow(mainWindow, SW_RESTORE);
    fitMainWindow(mainWindow, dpiFor(mainWindow), nullptr);
    SetForegroundWindow(mainWindow);
}

bool ownsOverlay(HWND hwnd) {
    return std::find(overlays.begin(), overlays.end(), hwnd) != overlays.end() ||
           std::find(restCards.begin(), restCards.end(), hwnd) != restCards.end();
}

void hideOverlays() {
    const bool restoreFocus = ownsOverlay(GetForegroundWindow());
    const bool restoreSettings = restoreSettingsAfterRest;
    restoreSettingsAfterRest = false;
    for (HWND hwnd : restCards) DestroyWindow(hwnd);
    restCards.clear();
    for (HWND hwnd : overlays) DestroyWindow(hwnd);
    overlays.clear();
    if (restoreSettings && IsWindow(mainWindow)) ShowWindow(mainWindow, SW_SHOWNOACTIVATE);
    if (restoreFocus && IsWindow(previousForeground)) SetForegroundWindow(previousForeground);
    previousForeground = nullptr;
}

BOOL CALLBACK createMonitorOverlay(HMONITOR, HDC, LPRECT rect, LPARAM) {
    HWND shade = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        OverlayClass, L"RestCue 休息遮罩", WS_POPUP,
        rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
        mainWindow, nullptr, instance, nullptr);
    if (!shade) return TRUE;
    SetLayeredWindowAttributes(shade, 0, 0, LWA_ALPHA);
    overlays.push_back(shade);
    ShowWindow(shade, SW_SHOWNOACTIVATE);

    const auto bounds = restcue::theme::restCardBounds(*rect, dpiFor(shade));
    HWND card = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        RestCardClass, L"RestCue 休息倒计时", WS_POPUP,
        bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
        shade, nullptr, instance, reinterpret_cast<LPVOID>(1));
    if (card) {
        // Per-pixel alpha: only the ring and text are visible, with no card background.
        updateRestCue(card, 0);
        restCards.push_back(card);
        ShowWindow(card, SW_SHOWNOACTIVATE);
    }
    return TRUE;
}

void showOverlays() {
    previousForeground = GetForegroundWindow();
    overlayStarted = GetTickCount64();
    lastOverlayFrame = 0;
    restoreSettingsAfterRest = trayVisible && IsWindowVisible(mainWindow) && !IsIconic(mainWindow);
    if (restoreSettingsAfterRest) ShowWindow(mainWindow, SW_HIDE);
    EnumDisplayMonitors(nullptr, nullptr, createMonitorOverlay, 0);
    if (overlays.empty() && restoreSettingsAfterRest) {
        ShowWindow(mainWindow, SW_SHOWNOACTIVATE);
        restoreSettingsAfterRest = false;
    }
    // Shade opacity follows the user's setting; the separate card stays readable.
}

void refresh() {
    const auto now = GetTickCount64();
    timer.tick(now);
    const auto phase = timer.phase();
    if (phase == restcue::Phase::Resting) {
        if (overlays.empty()) showOverlays();
        const auto remaining = timer.remaining(now);
        const auto alpha = restcue::fadeAlpha(now >= overlayStarted ? now - overlayStarted : 0,
                                               remaining, timer.restDuration(), settings.opacity);
        const auto frame = now / 100;
        for (HWND hwnd : overlays) SetLayeredWindowAttributes(hwnd, 0, static_cast<BYTE>(alpha), LWA_ALPHA);
        const auto cardAlpha = restcue::fadeAlpha(now >= overlayStarted ? now - overlayStarted : 0,
                                                  remaining, timer.restDuration(), 100);
        for (HWND hwnd : restCards) {
            const auto* state = reinterpret_cast<RestWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (frame != lastOverlayFrame || (state && state->alpha != cardAlpha))
                updateRestCue(hwnd, static_cast<BYTE>(cardAlpha));
        }
        lastOverlayFrame = frame;
    } else if (!overlays.empty() || !restCards.empty()) {
        hideOverlays();
    }

    std::wstring text;
    switch (phase) {
    case restcue::Phase::Working: text = L"工作中 · 距离下次休息 " + clockText(timer.remaining(now)); break;
    case restcue::Phase::Resting: text = L"休息中 · 剩余 " + clockText(timer.remaining(now)); break;
    case restcue::Phase::Paused: text = L"已暂停 · 剩余 " + clockText(timer.remaining(now)); break;
    default: text = L"已停止 · 点击“保存”启用提醒"; break;
    }
    if (trayVisible && !trayPromoted) promoteTrayIcon();
    if (text != lastStatus) {
        SetDlgItemTextW(mainWindow, Status, text.c_str());
        lastStatus = text;
        const int span = contentSpan(mainWindow);
        RECT badge{place(555, span), place(25, span), place(785, span), place(135, span)};
        InvalidateRect(mainWindow, &badge, FALSE);
        if (trayVisible) {
            StringCchCopyW(tray.szTip, ARRAYSIZE(tray.szTip), (L"RestCue · " + text).c_str());
            tray.uFlags = NIF_TIP;
            Shell_NotifyIconW(NIM_MODIFY, &tray);
        }
    }
    const wchar_t* label = phase == restcue::Phase::Paused ? L"继续" : L"暂停";
    wchar_t current[16]{};
    GetDlgItemTextW(mainWindow, Toggle, current, ARRAYSIZE(current));
    if (wcscmp(label, current) != 0) SetDlgItemTextW(mainWindow, Toggle, label);
    const bool forced = boxChecked(ForceRest);
    const bool canPause = phase != restcue::Phase::Stopped && !(forced && phase == restcue::Phase::Resting);
    const bool canSkip = phase == restcue::Phase::Resting && !forced;
    if ((IsWindowEnabled(GetDlgItem(mainWindow, Toggle)) != FALSE) != canPause) EnableWindow(GetDlgItem(mainWindow, Toggle), canPause);
    if ((IsWindowEnabled(GetDlgItem(mainWindow, Skip)) != FALSE) != canSkip) EnableWindow(GetDlgItem(mainWindow, Skip), canSkip);
}

void trayMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    const auto phase = timer.phase();
    AppendMenuW(menu, MF_STRING, Open, L"打开设置");
    const bool canPause = phase != restcue::Phase::Stopped &&
        !(boxChecked(ForceRest) && phase == restcue::Phase::Resting);
    AppendMenuW(menu, MF_STRING | (canPause ? 0 : MF_GRAYED), Toggle,
                phase == restcue::Phase::Paused ? L"继续提醒" : L"暂停提醒");
    AppendMenuW(menu, MF_STRING, RestNow, L"立即休息");
    AppendMenuW(menu, MF_STRING | (phase == restcue::Phase::Resting && !boxChecked(ForceRest) ? 0 : MF_GRAYED),
                Skip, L"跳过本次休息");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, Exit, L"退出");
    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(mainWindow);
    const int id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                  point.x, point.y, 0, mainWindow, nullptr);
    PostMessageW(mainWindow, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (id) command(id);
}

void command(int id) {
    const auto now = GetTickCount64();
    switch (id) {
    case Apply:
        if (!applySettings()) return;
        timer.start(GetTickCount64(), static_cast<std::uint64_t>(settings.workMinutes) * 60000,
                    static_cast<std::uint64_t>(settings.restSeconds) * 1000);
        break;
    case Toggle:
        if (boxChecked(ForceRest) && timer.phase() == restcue::Phase::Resting) break;
        if (timer.phase() == restcue::Phase::Paused) timer.resume(now);
        else timer.pause(now);
        break;
    case RestNow:
        // Preview uses the saved/applied configuration, not uncommitted edits.
        hideOverlays();
        timer.restNow(now);
        break;
    case Skip:
        if (!boxChecked(ForceRest)) timer.skipRest(now);
        break;
    case Stop: timer.stop(); break;
    case IncreaseWork:
    case DecreaseWork: {
        unsigned value = 1;
        readNumber(WorkEdit, value);
        const int next = std::clamp(static_cast<int>(value) + (id == IncreaseWork ? 1 : -1), 1, 1440);
        setNumber(WorkEdit, static_cast<unsigned>(next));
        break;
    }
    case CloseMinimizes:
        settings.closeMinimizes = boxChecked(CloseMinimizes);
        settingsStore.save(settings);
        return;
    case ForceRest:
        settings.forceRest = boxChecked(ForceRest);
        settingsStore.save(settings);
        for (HWND card : restCards) InvalidateRect(card, nullptr, FALSE);
        return;
    case Open: showMain(); break;
    case Exit: DestroyWindow(mainWindow); return;
    default: return;
    }
    refresh();
}

void drawMain(HDC dc) {
    RECT client{};
    GetClientRect(mainWindow, &client);
    if (client.right <= 0 || client.bottom <= 0) return;
    std::wstring phase;
    switch (timer.phase()) {
    case restcue::Phase::Working: phase = L"工作中 · 距离休息"; break;
    case restcue::Phase::Resting: phase = L"休息中 · 剩余时间"; break;
    case restcue::Phase::Paused: phase = L"已暂停 · 剩余时间"; break;
    default: phase = L"已停止提醒"; break;
    }
    const auto countdown = clockText(timer.remaining(GetTickCount64()));
    const int focus = GetDlgCtrlID(GetFocus());
    const bool active = timer.phase() == restcue::Phase::Working || timer.phase() == restcue::Phase::Resting;
    // Keep the last frame. A mouse move can invalidate a few letters; re-rendering
    // ClearType text into that spot makes those letters flicker.
    struct Cache {
        restcue::theme::Surface surface;
        std::wstring phase, countdown;
        int focus = -1, width = 0, height = 0;
        bool active = false;
    };
    static Cache cache;
    if (cache.width != client.right || cache.height != client.bottom || cache.phase != phase ||
        cache.countdown != countdown || cache.focus != focus || cache.active != active || !cache.surface.dc()) {
        if (!cache.surface.resize(dc, client.right, client.bottom)) return;
        Gdiplus::Graphics graphics(cache.surface.dc());
        graphics.Clear(restcue::theme::opaque(restcue::theme::Background));
        const float scale = static_cast<float>(client.right) / restcue::theme::MainWidth;
        restcue::theme::mainPanel(graphics, scale, phase, countdown, active, focus);
        cache.phase = phase;
        cache.countdown = countdown;
        cache.focus = focus;
        cache.width = client.right;
        cache.height = client.bottom;
        cache.active = active;
    }
    BitBlt(dc, 0, 0, client.right, client.bottom, cache.surface.dc(), 0, 0, SRCCOPY);
}

void blitBuffered(HDC dc, int width, int height, void (*paint)(HDC, int, int, LPARAM), LPARAM param) {
    restcue::theme::Surface buffer;
    if (width <= 0 || height <= 0 || !buffer.resize(dc, width, height)) return;
    paint(buffer.dc(), width, height, param);
    BitBlt(dc, 0, 0, width, height, buffer.dc(), 0, 0, SRCCOPY);
}

struct ButtonPaint {
    HWND hwnd;
    const wchar_t* label;
    UINT id;
    UINT state;
};

void paintButton(HDC dc, int width, int height, LPARAM param) {
    const auto* item = reinterpret_cast<const ButtonPaint*>(param);
    const Gdiplus::RectF rect(0, 0, static_cast<float>(width), static_cast<float>(height));
    RECT bounds{0, 0, width, height};
    POINT point{};
    GetCursorPos(&point);
    ScreenToClient(item->hwnd, &point);
    const bool hover = PtInRect(&bounds, point) != FALSE;
    const bool pressed = (item->state & ODS_SELECTED) != 0;
    const bool focus = (item->state & ODS_FOCUS) != 0;
    const bool enabled = !(item->state & ODS_DISABLED);
    if (item->id == IncreaseWork || item->id == DecreaseWork)
        restcue::theme::stepper(dc, rect, item->id == IncreaseWork, enabled, pressed, hover, focus);
    else
        restcue::theme::button(dc, rect, item->label, item->id == Apply, enabled, pressed, hover, focus);
}

void drawButton(const DRAWITEMSTRUCT& item) {
    wchar_t label[80]{};
    GetWindowTextW(item.hwndItem, label, ARRAYSIZE(label));
    RECT bounds{};
    GetClientRect(item.hwndItem, &bounds);
    ButtonPaint paint{item.hwndItem, label, static_cast<UINT>(item.CtlID), item.itemState};
    blitBuffered(item.hDC, bounds.right, bounds.bottom, paintButton, reinterpret_cast<LPARAM>(&paint));
}

void paintCheck(HDC dc, int width, int height, LPARAM param) {
    HWND hwnd = reinterpret_cast<HWND>(param);
    wchar_t label[80]{};
    GetWindowTextW(hwnd, label, ARRAYSIZE(label));
    RECT bounds{0, 0, width, height};
    POINT point{};
    GetCursorPos(&point);
    ScreenToClient(hwnd, &point);
    const bool hover = PtInRect(&bounds, point) != FALSE;
    restcue::theme::checkBox(dc, Gdiplus::RectF(0, 0, static_cast<float>(width), static_cast<float>(height)),
        label, SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED,
        (GetKeyState(VK_LBUTTON) & 0x8000) && hover, hover, GetFocus() == hwnd);
}

LRESULT CALLBACK buttonProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR) {
    const int controlId = GetDlgCtrlID(hwnd);
    const bool customCheck = controlId == Startup || controlId == CloseMinimizes || controlId == ForceRest;
    if (message == WM_ERASEBKGND) return 1;
    if (customCheck && message == WM_PAINT) {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT bounds{};
        GetClientRect(hwnd, &bounds);
        blitBuffered(dc, bounds.right, bounds.bottom, paintCheck, reinterpret_cast<LPARAM>(hwnd));
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (message == WM_MOUSEMOVE) {
        TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tracking);
        if (!GetPropW(hwnd, L"RestCueHover")) {
            SetPropW(hwnd, L"RestCueHover", reinterpret_cast<HANDLE>(1));
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        // The standard button procedure invalidates on every move, which flashes the label.
        // Captured moves still need the default handler so a click can be cancelled.
        if (GetCapture() != hwnd) return 0;
    } else if (message == WM_MOUSELEAVE || message == WM_SETFOCUS || message == WM_KILLFOCUS) {
        RemovePropW(hwnd, L"RestCueHover");
        InvalidateRect(hwnd, nullptr, FALSE);
    } else if (message == WM_NCDESTROY) {
        RemovePropW(hwnd, L"RestCueHover");
        RemoveWindowSubclass(hwnd, buttonProc, id);
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK sliderProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    unsigned value = settings.opacity;
    unsigned input = 0;
    if (readNumber(OpacityEdit, input)) value = std::clamp(input, 10U, 95U);
    auto fromPoint = [&] {
        RECT rect{};
        GetClientRect(hwnd, &rect);
        const float edge = rect.bottom * .5f;
        const int x = static_cast<short>(LOWORD(lParam));
        const float fraction = (x - edge) / std::max(1.0f, rect.right - edge * 2);
        const auto next = static_cast<unsigned>(std::clamp(static_cast<int>(std::lround(10 + 85 * fraction)), 10, 95));
        setNumber(OpacityEdit, next);
    };
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rect{};
        GetClientRect(hwnd, &rect);
        restcue::theme::Surface buffer;
        if (buffer.resize(dc, rect.right, rect.bottom)) {
            restcue::theme::slider(buffer.dc(), rect.right, rect.bottom, value, GetFocus() == hwnd);
            BitBlt(dc, 0, 0, rect.right, rect.bottom, buffer.dc(), 0, 0, SRCCOPY);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_GETDLGCODE: return DLGC_WANTARROWS;
    case WM_LBUTTONDOWN: SetFocus(hwnd); SetCapture(hwnd); fromPoint(); return 0;
    case WM_MOUSEMOVE: if (GetCapture() == hwnd) fromPoint(); return 0;
    case WM_LBUTTONUP: if (GetCapture() == hwnd) { fromPoint(); ReleaseCapture(); } return 0;
    case WM_KEYDOWN: {
        int next = static_cast<int>(value);
        switch (wParam) {
        case VK_LEFT: case VK_DOWN: --next; break;
        case VK_RIGHT: case VK_UP: ++next; break;
        case VK_HOME: next = 10; break;
        case VK_END: next = 95; break;
        case VK_PRIOR: next += 10; break;
        case VK_NEXT: next -= 10; break;
        default: return DefWindowProcW(hwnd, message, wParam, lParam);
        }
        setNumber(OpacityEdit, static_cast<unsigned>(std::clamp(next, 10, 95)));
        return 0;
    }
    case WM_MOUSEWHEEL:
        setNumber(OpacityEdit, static_cast<unsigned>(std::clamp(static_cast<int>(value) +
            (static_cast<short>(HIWORD(wParam)) > 0 ? 1 : -1), 10, 95)));
        return 0;
    case WM_SETFOCUS: case WM_KILLFOCUS:
        InvalidateRect(hwnd, nullptr, FALSE);
        InvalidateRect(mainWindow, nullptr, FALSE);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void updateRestCue(HWND hwnd, BYTE alpha) {
    auto* state = reinterpret_cast<RestWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!state || !state->card) return;
    RECT rect{};
    GetWindowRect(hwnd, &rect);
    const int width = rect.right - rect.left, height = rect.bottom - rect.top;
    HDC screen = GetDC(nullptr);
    const auto remaining = timer.remaining(GetTickCount64());
    const auto* surface = state->scene.renderCard(screen, width, height, clockText(remaining),
        static_cast<float>(remaining) / static_cast<float>(std::max<std::uint64_t>(1, timer.restDuration())),
        remaining, boxChecked(ForceRest));
    if (surface) {
        POINT destination{rect.left, rect.top}, source{0, 0};
        SIZE size{width, height};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
        if (UpdateLayeredWindow(hwnd, screen, &destination, &size, surface->dc(), &source, 0, &blend, ULW_ALPHA))
            state->alpha = alpha;
    }
    if (screen) ReleaseDC(nullptr, screen);
}

void drawRestSurface(HWND hwnd, HDC dc) {
    RECT client{};
    GetClientRect(hwnd, &client);
    auto* state = reinterpret_cast<RestWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!state) return;
    if (state->card) {
        const auto remaining = timer.remaining(GetTickCount64());
        const auto* surface = state->scene.renderCard(dc, client.right, client.bottom, clockText(remaining),
            static_cast<float>(remaining) / static_cast<float>(std::max<std::uint64_t>(1, timer.restDuration())),
            remaining, boxChecked(ForceRest));
        if (surface) BitBlt(dc, 0, 0, client.right, client.bottom, surface->dc(), 0, 0, SRCCOPY);
    } else {
        state->scene.paintShade(dc, client.right, client.bottom);
    }
}

void drawOverlay(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    auto* state = reinterpret_cast<RestWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (state && state->card) updateRestCue(hwnd, state->alpha);
    else drawRestSurface(hwnd, dc);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK overlayProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(new RestWindow(create->lpCreateParams != nullptr)));
        return 0;
    }
    case WM_NCDESTROY:
        delete reinterpret_cast<RestWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    case WM_PAINT: drawOverlay(hwnd); return 0;
    case WM_PRINTCLIENT: drawRestSurface(hwnd, reinterpret_cast<HDC>(wParam)); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_LBUTTONUP: return 0;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return TRUE;
    case WM_RBUTTONUP:
        if (!boxChecked(ForceRest)) PostMessageW(mainWindow, WM_COMMAND, Skip, 0);
        return 0;
    case WM_CLOSE:
        if (!boxChecked(ForceRest)) PostMessageW(mainWindow, WM_COMMAND, Skip, 0);
        return 0;
    case WM_DPICHANGED: {
        const RECT* rect = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd, HWND_TOPMOST, rect->left, rect->top, rect->right - rect->left,
                     rect->bottom - rect->top, SWP_NOACTIVATE);
        const auto* state = reinterpret_cast<RestWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (state && state->card) updateRestCue(hwnd, state->alpha);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK mainProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (taskbarCreated && message == taskbarCreated) {
        addTray();
        lastStatus.clear();
        refresh();
        return 0;
    }
    switch (message) {
    case WM_CREATE: {
        mainWindow = hwnd;
        darkBrush = CreateSolidBrush(restcue::theme::Background);
        editBrush = CreateSolidBrush(restcue::theme::EditBackground);
        BOOL dark = FALSE;
        DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
        COLORREF caption = restcue::theme::Background, foreground = restcue::theme::Foreground;
        DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption));
        DwmSetWindowAttribute(hwnd, 36, &foreground, sizeof(foreground));
        createControls();
        addTray();
        timer.start(GetTickCount64(), static_cast<std::uint64_t>(settings.workMinutes) * 60000,
                    static_cast<std::uint64_t>(settings.restSeconds) * 1000);
        if (!SetTimer(hwnd, TickId, 50, nullptr)) return -1;
        refresh();
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        drawMain(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_PRINTCLIENT: drawMain(reinterpret_cast<HDC>(wParam)); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_DRAWITEM:
        if (wParam) { drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam)); return TRUE; }
        break;
    case WM_TIMER: if (wParam == TickId) refresh(); return 0;
    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED) command(LOWORD(wParam));
        else if (LOWORD(wParam) == OpacityEdit && HIWORD(wParam) == EN_CHANGE)
            InvalidateRect(GetDlgItem(hwnd, OpacitySlider), nullptr, FALSE);
        else if (HIWORD(wParam) == EN_SETFOCUS || HIWORD(wParam) == EN_KILLFOCUS)
            InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case TrayMessage:
        if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK || lParam == NIN_KEYSELECT) showMain();
        else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) trayMenu();
        return 0;
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) restoreSettingsAfterRest = false;
        if (wParam == SIZE_MINIMIZED && trayVisible) ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_GETDPISCALEDSIZE: {
        const UINT dpi = static_cast<UINT>(wParam);
        auto* size = reinterpret_cast<SIZE*>(lParam);
        if (!dpi || !size) break;
        RECT rect{0, 0, scaled(static_cast<int>(restcue::theme::MainWidth), dpi),
                        scaled(static_cast<int>(restcue::theme::MainHeight), dpi)};
        adjustWindowForDpi(rect, dpi);
        size->cx = rect.right - rect.left;
        size->cy = rect.bottom - rect.top;
        return TRUE;
    }
    case WM_DPICHANGED: {
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        const UINT dpi = LOWORD(wParam);
        if (suggested && dpi) fitMainWindow(hwnd, dpi, suggested);
        return 0;
    }
    case WM_DISPLAYCHANGE:
        if (!overlays.empty()) hideOverlays();
        refresh();
        return 0;
    case WM_POWERBROADCAST:
        if (wParam == PBT_APMSUSPEND) hideOverlays();
        if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) {
            timer.onSystemResume(GetTickCount64());
            refresh();
        }
        return TRUE;
    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(wParam), restcue::theme::Foreground);
        SetBkColor(reinterpret_cast<HDC>(wParam), restcue::theme::EditBackground);
        return reinterpret_cast<LRESULT>(editBrush);
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wParam), restcue::theme::Foreground);
        SetBkColor(reinterpret_cast<HDC>(wParam), restcue::theme::Background);
        return reinterpret_cast<LRESULT>(darkBrush);
    case WM_CLOSE:
        restoreSettingsAfterRest = false;
        if (boxChecked(CloseMinimizes) && trayVisible) ShowWindow(hwnd, SW_HIDE);
        else DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        restoreSettingsAfterRest = false;
        KillTimer(hwnd, TickId);
        hideOverlays();
        if (trayVisible) Shell_NotifyIconW(NIM_DELETE, &tray);
        if (uiFont) DeleteObject(uiFont);
        if (headingFont) DeleteObject(headingFont);
        if (darkBrush) DeleteObject(darkBrush);
        if (editBrush) DeleteObject(editBrush);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR commandLine, int showCommand) {
    instance = hInstance;
    const bool startInTray = commandLine && wcsstr(commandLine, L"--tray") != nullptr;
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\RestCue.SingleInstance");
    if (!mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (!startInTray) {
            HWND existing = FindWindowW(MainClass, nullptr);
            if (existing) { ShowWindow(existing, SW_RESTORE); SetForegroundWindow(existing); }
        }
        CloseHandle(mutex);
        return 0;
    }
    restcue::theme::Runtime graphics;
    if (!graphics.ok()) {
        MessageBoxW(nullptr, L"无法初始化图形绘制引擎。", Title, MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        return 1;
    }
    INITCOMMONCONTROLSEX common{sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&common);
    using SetAwareness = BOOL(WINAPI*)(HANDLE);
    auto setAwareness = reinterpret_cast<SetAwareness>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (!setAwareness || !setAwareness(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)))) SetProcessDPIAware();
    loadSettings();
    if (settings.launchAtStartup) setLaunchAtStartup(true, nullptr);
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.hInstance = instance;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = loadAppIcon(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    cls.hIconSm = loadAppIcon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    cls.hbrBackground = nullptr;
    cls.lpszClassName = MainClass;
    cls.lpfnWndProc = mainProc;
    const bool registeredMain = RegisterClassExW(&cls) != 0;
    cls.lpszClassName = OverlayClass;
    cls.lpfnWndProc = overlayProc;
    cls.hbrBackground = nullptr;
    const bool registeredOverlay = RegisterClassExW(&cls) != 0;
    cls.lpszClassName = RestCardClass;
    const bool registeredRestCard = RegisterClassExW(&cls) != 0;
    cls.lpszClassName = SliderClass;
    cls.lpfnWndProc = sliderProc;
    const bool registeredSlider = RegisterClassExW(&cls) != 0;
    if (!registeredMain || !registeredOverlay || !registeredRestCard || !registeredSlider) {
        MessageBoxW(nullptr, L"窗口初始化失败。", Title, MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        return 1;
    }

    UINT dpi = 96;
    HDC screen = GetDC(nullptr);
    if (screen) { dpi = static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSY)); ReleaseDC(nullptr, screen); }
    if (!dpi) dpi = 96;
    RECT rect{0, 0, scaled(static_cast<int>(restcue::theme::MainWidth), dpi),
                    scaled(static_cast<int>(restcue::theme::MainHeight), dpi)};
    adjustWindowForDpi(rect, dpi);
    HWND hwnd = CreateWindowExW(MainExStyle, MainClass, Title, MainStyle,
        CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, instance, nullptr);
    if (!hwnd) {
        MessageBoxW(nullptr, L"无法创建主窗口或计时器。", Title, MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        return 1;
    }
    if (!(startInTray && trayVisible)) {
        ShowWindow(hwnd, showCommand);
        UpdateWindow(hwnd);
    }
    fitMainWindow(hwnd, dpiFor(hwnd), nullptr);
    MSG message{};
    BOOL result;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN &&
            (message.hwnd == GetDlgItem(hwnd, WorkEdit) || message.hwnd == GetDlgItem(hwnd, RestMinEdit) ||
             message.hwnd == GetDlgItem(hwnd, RestSecEdit) || message.hwnd == GetDlgItem(hwnd, OpacityEdit))) {
            command(Apply);
            continue;
        }
        // Only the settings window needs dialog-style Tab/Enter navigation.
        if ((message.hwnd == hwnd || IsChild(hwnd, message.hwnd)) && IsDialogMessageW(hwnd, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (IsWindow(hwnd)) DestroyWindow(hwnd);
    CloseHandle(mutex);
    return result == -1 ? 1 : static_cast<int>(message.wParam);
}
