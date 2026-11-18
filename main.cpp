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
#include <commctrl.h>
#include <dwmapi.h>
#include <strsafe.h>
#include <algorithm>
#include <cwchar>
#include <string>
#include <vector>
#include "timer.hpp"
#include "settings_store.hpp"
#include "startup.hpp"
#include "theme.hpp"
#include "resource.h"

namespace {
using Settings = restcue::Settings;
using namespace restcue::theme::controlId;

constexpr wchar_t MainClass[] = L"RestCue.MainWindow";
constexpr wchar_t OverlayClass[] = L"RestCue.Overlay";
constexpr wchar_t RestCardClass[] = L"RestCue.RestCard";
constexpr wchar_t SliderClass[] = L"RestCue.OpacitySlider";
constexpr wchar_t Title[] = L"RestCue · 休息提醒";
constexpr UINT TrayMessage = WM_APP + 1;
constexpr UINT_PTR TickId = 1;
constexpr DWORD MainStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
constexpr DWORD MainExStyle = WS_EX_CONTROLPARENT;
constexpr DWORD DwmUseImmersiveDarkMode = 20;
constexpr DWORD DwmCaptionColor = 35;
constexpr DWORD DwmTextColor = 36;

struct Control { HWND hwnd; int x, y, width, height; bool heading; };

struct RestWindow {
    explicit RestWindow(bool isCard) : card(isCard) {}
    bool card;
    BYTE alpha = 0;
    restcue::theme::OverlayScene scene;
};

struct MainPanelCache {
    restcue::theme::Surface surface;
    std::wstring phase, countdown;
    int focus = -1, width = 0, height = 0;
    bool active = false;
};

struct AppState {
    Settings settings;
    restcue::SettingsStore settingsStore;
    restcue::StartupManager startup;
    restcue::Timer timer;

    HINSTANCE instance = nullptr;
    HWND mainWindow = nullptr;
    HWND previousForeground = nullptr;
    HFONT uiFont = nullptr;
    HFONT headingFont = nullptr;
    HBRUSH darkBrush = nullptr;
    HBRUSH editBrush = nullptr;

    std::vector<Control> controls;
    std::vector<HWND> overlays;
    std::vector<HWND> restCards;
    MainPanelCache mainCache;

    NOTIFYICONDATAW tray{};
    UINT taskbarCreated = 0;
    bool trayVisible = false;
    bool trayPromoted = false;
    int trayPromoteTries = 0;

    bool fittingWindow = false;
    bool restoreSettingsAfterRest = false;
    ULONGLONG overlayStarted = 0;
    std::uint64_t lastOverlayFrame = 0;
    std::wstring lastStatus;
};

AppState app;

void refresh();
void command(int id);
void updateRestCue(HWND hwnd, BYTE alpha);
LRESULT CALLBACK overlayProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK sliderProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK buttonProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

// --- DPI & Window Layout ---

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
    HICON icon = static_cast<HICON>(LoadImageW(app.instance, MAKEINTRESOURCEW(IDI_RESTCUE),
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
    const int span = contentSpan(app.mainWindow);
    HFONT oldUi = app.uiFont, oldHeading = app.headingFont;
    app.uiFont = makeFont(16, span);
    app.headingFont = makeFont(18, span, FW_SEMIBOLD);
    for (const auto& c : app.controls) {
        MoveWindow(c.hwnd, place(c.x, span), place(c.y, span),
                   place(c.width, span), place(c.height, span), TRUE);
        SendMessageW(c.hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(c.heading ? app.headingFont : app.uiFont), TRUE);
        InvalidateRect(c.hwnd, nullptr, FALSE);
    }
    if (oldUi) DeleteObject(oldUi);
    if (oldHeading) DeleteObject(oldHeading);
}

// Keep the client exactly scaled(MainWidth/MainHeight, dpi). Windows' suggested
// WM_DPICHANGED size scales the outer frame linearly, so the client drifts and the
// painted panel no longer lines up with the child controls.
void fitMainWindow(HWND hwnd, UINT dpi, const RECT* suggested) {
    if (!hwnd || !dpi || app.fittingWindow) return;
    app.fittingWindow = true;
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
    app.fittingWindow = false;
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

// --- Settings Form & Controls ---

HWND addControl(const wchar_t* cls, const wchar_t* text, int id,
                int x, int y, int w, int h, DWORD style = 0, bool heading = false) {
    HWND hwnd = CreateWindowExW(0,
        cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, app.mainWindow,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), app.instance, nullptr);
    app.controls.push_back({hwnd, x, y, w, h, heading});
    SetWindowSubclass(hwnd, childDpiProc, 2, 0);
    if (wcscmp(cls, L"BUTTON") == 0) SetWindowSubclass(hwnd, buttonProc, 1, 0);
    return hwnd;
}

void setNumber(int id, unsigned value) {
    SetDlgItemTextW(app.mainWindow, id, std::to_wstring(value).c_str());
}

bool readNumber(int id, unsigned& value) {
    wchar_t text[32]{};
    const int length = GetWindowTextLengthW(GetDlgItem(app.mainWindow, id));
    if (length < 1 || length > 6) return false;
    GetDlgItemTextW(app.mainWindow, id, text, 32);
    value = 0;
    for (int i = 0; i < length; ++i) {
        if (text[i] < L'0' || text[i] > L'9') return false;
        value = value * 10 + static_cast<unsigned>(text[i] - L'0');
    }
    return true;
}

bool boxChecked(int id) {
    return SendMessageW(GetDlgItem(app.mainWindow, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void loadSettings() {
    app.settingsStore = restcue::SettingsStore::forApplication();
    app.startup = restcue::StartupManager::forApplication();
    app.settings = app.settingsStore.load();
}

bool applySettings() {
    unsigned work = 0, minutes = 0, seconds = 0, opacity = 0;
    const bool numbersOk = readNumber(WorkEdit, work) && readNumber(RestMinEdit, minutes) &&
        readNumber(RestSecEdit, seconds) && readNumber(OpacityEdit, opacity) &&
        minutes <= Settings::MaxRestMinutes && seconds <= Settings::MaxRestSecondsPart;
    const Settings updated{work, minutes * 60 + seconds, opacity, boxChecked(Startup),
                           boxChecked(CloseMinimizes), boxChecked(ForceRest)};
    if (!numbersOk || !updated.valid()) {
        MessageBoxW(app.mainWindow,
            L"请输入有效设置：\n\n工作间隔：1～1440 分钟\n休息时长：1 秒～120 分钟（秒数填 0～59）\n遮罩浓度：10～95%",
            Title, MB_OK | MB_ICONWARNING);
        return false;
    }
    const auto saved = app.settingsStore.save(updated);
    if (!saved.success) {
        std::wstring message = L"无法保存设置。本次运行仍会生效，但重启后可能无法恢复。\n\n";
        for (const auto& failure : saved.failures) {
            wchar_t description[512]{};
            FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr, failure.error, 0, description, ARRAYSIZE(description), nullptr);
            message += failure.path + L"\n错误 " + std::to_wstring(failure.error) + L"：" + description + L"\n";
        }
        message += L"请检查上述目录的写入权限或文件是否只读；可将程序放在可写目录中运行。";
        MessageBoxW(app.mainWindow, message.c_str(), Title, MB_OK | MB_ICONWARNING);
    } else if (updated.launchAtStartup != app.settings.launchAtStartup || updated.launchAtStartup) {
        DWORD startupError = ERROR_SUCCESS;
        if (!app.startup.apply(updated.launchAtStartup, &startupError)) {
            wchar_t description[512]{};
            FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr, startupError, 0, description, ARRAYSIZE(description), nullptr);
            const std::wstring message = L"设置已保存，但无法写入开机自启。\n错误 " +
                std::to_wstring(startupError) + L"：" + description;
            MessageBoxW(app.mainWindow, message.c_str(), Title, MB_OK | MB_ICONWARNING);
        }
    }
    app.settings = updated;
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
        SendDlgItemMessageW(app.mainWindow, id, EM_SETLIMITTEXT, 6, 0);
    }
    setNumber(WorkEdit, app.settings.workMinutes);
    setNumber(RestMinEdit, app.settings.restSeconds / 60);
    setNumber(RestSecEdit, app.settings.restSeconds % 60);
    setNumber(OpacityEdit, app.settings.opacity);
    SendDlgItemMessageW(app.mainWindow, Startup, BM_SETCHECK,
                        app.settings.launchAtStartup ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(app.mainWindow, CloseMinimizes, BM_SETCHECK,
                        app.settings.closeMinimizes ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(app.mainWindow, ForceRest, BM_SETCHECK,
                        app.settings.forceRest ? BST_CHECKED : BST_UNCHECKED, 0);
    layoutControls();
}

// --- System Tray ---

bool sameExePath(const wchar_t* left, const wchar_t* right) {
    return left && right && CompareStringOrdinal(left, -1, right, -1, TRUE) == CSTR_EQUAL;
}

// Windows 11 puts new icons behind the overflow arrow. IsPromoted=1 shows ours
// in the visible tray. IsPromoted=0 is the user hiding it, so leave that alone.
void promoteTrayIcon() {
    if (app.trayPromoted || !app.trayVisible || app.trayPromoteTries > 200) return;
    ++app.trayPromoteTries;
    const auto path = restcue::executablePath();
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
            if (!(read == ERROR_SUCCESS && ptype == REG_DWORD && (promoted == 0 || promoted == 1))) {
                const DWORD show = 1;
                RegSetValueExW(sub, L"IsPromoted", 0, REG_DWORD,
                               reinterpret_cast<const BYTE*>(&show), sizeof(show));
            }
            app.trayPromoted = true;
        }
        RegCloseKey(sub);
        if (app.trayPromoted) break;
    }
    RegCloseKey(root);
}

void addTray() {
    app.tray = {};
    app.tray.cbSize = sizeof(app.tray);
    app.tray.hWnd = app.mainWindow;
    app.tray.uID = 1;
    app.tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    app.tray.uCallbackMessage = TrayMessage;
    const int iconSize = GetSystemMetrics(SM_CXSMICON);
    app.tray.hIcon = loadAppIcon(iconSize, iconSize);
    if (!app.tray.hIcon) app.tray.hIcon = loadAppIcon(16, 16);
    if (!app.tray.hIcon) app.tray.hIcon = loadAppIcon(24, 24);
    StringCchCopyW(app.tray.szTip, ARRAYSIZE(app.tray.szTip), Title);
    app.trayVisible = Shell_NotifyIconW(NIM_ADD, &app.tray) != FALSE;
    if (!app.trayVisible) app.trayVisible = Shell_NotifyIconW(NIM_MODIFY, &app.tray) != FALSE;
    app.trayPromoted = false;
    app.trayPromoteTries = 0;
    if (app.trayVisible) promoteTrayIcon();
    else ShowWindow(app.mainWindow, SW_RESTORE);
}

void showMain() {
    ShowWindow(app.mainWindow, SW_RESTORE);
    fitMainWindow(app.mainWindow, dpiFor(app.mainWindow), nullptr);
    SetForegroundWindow(app.mainWindow);
}

void trayMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    const auto phase = app.timer.phase();
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
    SetForegroundWindow(app.mainWindow);
    const int id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                  point.x, point.y, 0, app.mainWindow, nullptr);
    PostMessageW(app.mainWindow, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (id) command(id);
}

// --- Multi-Monitor Rest Overlays & Scheduler Sync ---

std::wstring clockText(std::uint64_t milliseconds) {
    const auto seconds = (milliseconds + 999) / 1000;
    wchar_t buffer[40]{};
    swprintf_s(buffer, ARRAYSIZE(buffer), L"%02llu:%02llu",
               static_cast<unsigned long long>(seconds / 60),
               static_cast<unsigned long long>(seconds % 60));
    return buffer;
}

bool ownsOverlay(HWND hwnd) {
    return std::find(app.overlays.begin(), app.overlays.end(), hwnd) != app.overlays.end() ||
           std::find(app.restCards.begin(), app.restCards.end(), hwnd) != app.restCards.end();
}

void hideOverlays() {
    const bool restoreFocus = ownsOverlay(GetForegroundWindow());
    const bool restoreSettings = app.restoreSettingsAfterRest;
    app.restoreSettingsAfterRest = false;
    for (HWND hwnd : app.restCards) DestroyWindow(hwnd);
    app.restCards.clear();
    for (HWND hwnd : app.overlays) DestroyWindow(hwnd);
    app.overlays.clear();
    if (restoreSettings && IsWindow(app.mainWindow)) ShowWindow(app.mainWindow, SW_SHOWNOACTIVATE);
    if (restoreFocus && IsWindow(app.previousForeground)) SetForegroundWindow(app.previousForeground);
    app.previousForeground = nullptr;
}

BOOL CALLBACK createMonitorOverlay(HMONITOR, HDC, LPRECT rect, LPARAM) {
    HWND shade = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        OverlayClass, L"RestCue 休息遮罩", WS_POPUP,
        rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
        app.mainWindow, nullptr, app.instance, nullptr);
    if (!shade) return TRUE;
    SetLayeredWindowAttributes(shade, 0, 0, LWA_ALPHA);
    app.overlays.push_back(shade);
    ShowWindow(shade, SW_SHOWNOACTIVATE);

    const auto bounds = restcue::theme::restCardBounds(*rect, dpiFor(shade));
    HWND card = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        RestCardClass, L"RestCue 休息倒计时", WS_POPUP,
        bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
        shade, nullptr, app.instance, reinterpret_cast<LPVOID>(1));
    if (card) {
        // Per-pixel alpha: only the ring and text are visible, with no card background.
        updateRestCue(card, 0);
        app.restCards.push_back(card);
        ShowWindow(card, SW_SHOWNOACTIVATE);
    }
    return TRUE;
}

void showOverlays() {
    app.previousForeground = GetForegroundWindow();
    app.overlayStarted = GetTickCount64();
    app.lastOverlayFrame = 0;
    app.restoreSettingsAfterRest = app.trayVisible && IsWindowVisible(app.mainWindow) && !IsIconic(app.mainWindow);
    if (app.restoreSettingsAfterRest) ShowWindow(app.mainWindow, SW_HIDE);
    EnumDisplayMonitors(nullptr, nullptr, createMonitorOverlay, 0);
    if (app.overlays.empty() && app.restoreSettingsAfterRest) {
        ShowWindow(app.mainWindow, SW_SHOWNOACTIVATE);
        app.restoreSettingsAfterRest = false;
    }
    // Shade opacity follows the user's setting; the separate card stays readable.
}

void refresh() {
    const auto now = GetTickCount64();
    app.timer.tick(now);
    const auto phase = app.timer.phase();
    if (phase == restcue::Phase::Resting) {
        if (app.overlays.empty()) showOverlays();
        const auto remaining = app.timer.remaining(now);
        const auto elapsed = now >= app.overlayStarted ? now - app.overlayStarted : 0;
        const auto alpha = restcue::fadeAlpha(elapsed, remaining, app.timer.restDuration(), app.settings.opacity);
        const auto frame = now / 100;
        for (HWND hwnd : app.overlays) SetLayeredWindowAttributes(hwnd, 0, static_cast<BYTE>(alpha), LWA_ALPHA);
        const auto cardAlpha = restcue::fadeAlpha(elapsed, remaining, app.timer.restDuration(), 100);
        for (HWND hwnd : app.restCards) {
            const auto* state = reinterpret_cast<RestWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (frame != app.lastOverlayFrame || (state && state->alpha != cardAlpha))
                updateRestCue(hwnd, static_cast<BYTE>(cardAlpha));
        }
        app.lastOverlayFrame = frame;
    } else if (!app.overlays.empty() || !app.restCards.empty()) {
        hideOverlays();
    }

    std::wstring text;
    switch (phase) {
    case restcue::Phase::Working: text = L"工作中 · 距离下次休息 " + clockText(app.timer.remaining(now)); break;
    case restcue::Phase::Resting: text = L"休息中 · 剩余 " + clockText(app.timer.remaining(now)); break;
    case restcue::Phase::Paused: text = L"已暂停 · 剩余 " + clockText(app.timer.remaining(now)); break;
    default: text = L"已停止 · 点击“保存”启用提醒"; break;
    }
    if (app.trayVisible && !app.trayPromoted) promoteTrayIcon();
    if (text != app.lastStatus) {
        SetDlgItemTextW(app.mainWindow, Status, text.c_str());
        app.lastStatus = text;
        const int span = contentSpan(app.mainWindow);
        RECT badge{place(555, span), place(25, span), place(785, span), place(135, span)};
        InvalidateRect(app.mainWindow, &badge, FALSE);
        if (app.trayVisible) {
            StringCchCopyW(app.tray.szTip, ARRAYSIZE(app.tray.szTip), (L"RestCue · " + text).c_str());
            app.tray.uFlags = NIF_TIP;
            Shell_NotifyIconW(NIM_MODIFY, &app.tray);
        }
    }
    const wchar_t* label = phase == restcue::Phase::Paused ? L"继续" : L"暂停";
    wchar_t current[16]{};
    GetDlgItemTextW(app.mainWindow, Toggle, current, ARRAYSIZE(current));
    if (wcscmp(label, current) != 0) SetDlgItemTextW(app.mainWindow, Toggle, label);
    const bool forced = boxChecked(ForceRest);
    const bool canPause = phase != restcue::Phase::Stopped && !(forced && phase == restcue::Phase::Resting);
    const bool canSkip = phase == restcue::Phase::Resting && !forced;
    if ((IsWindowEnabled(GetDlgItem(app.mainWindow, Toggle)) != FALSE) != canPause)
        EnableWindow(GetDlgItem(app.mainWindow, Toggle), canPause);
    if ((IsWindowEnabled(GetDlgItem(app.mainWindow, Skip)) != FALSE) != canSkip)
        EnableWindow(GetDlgItem(app.mainWindow, Skip), canSkip);
}

void command(int id) {
    const auto now = GetTickCount64();
    switch (id) {
    case Apply:
        if (!applySettings()) return;
        app.timer.start(GetTickCount64(), static_cast<std::uint64_t>(app.settings.workMinutes) * 60000,
                        static_cast<std::uint64_t>(app.settings.restSeconds) * 1000);
        break;
    case Toggle:
        if (boxChecked(ForceRest) && app.timer.phase() == restcue::Phase::Resting) break;
        if (app.timer.phase() == restcue::Phase::Paused) app.timer.resume(now);
        else app.timer.pause(now);
        break;
    case RestNow:
        // Preview uses the saved/applied configuration, not uncommitted edits.
        hideOverlays();
        app.timer.restNow(now);
        break;
    case Skip:
        if (!boxChecked(ForceRest)) app.timer.skipRest(now);
        break;
    case Stop: app.timer.stop(); break;
    case IncreaseWork:
    case DecreaseWork: {
        unsigned value = Settings::MinWorkMinutes;
        readNumber(WorkEdit, value);
        const int next = std::clamp(static_cast<int>(value) + (id == IncreaseWork ? 1 : -1),
                                    static_cast<int>(Settings::MinWorkMinutes),
                                    static_cast<int>(Settings::MaxWorkMinutes));
        setNumber(WorkEdit, static_cast<unsigned>(next));
        break;
    }
    case CloseMinimizes:
        app.settings.closeMinimizes = boxChecked(CloseMinimizes);
        app.settingsStore.save(app.settings);
        return;
    case ForceRest:
        app.settings.forceRest = boxChecked(ForceRest);
        app.settingsStore.save(app.settings);
        for (HWND card : app.restCards) InvalidateRect(card, nullptr, FALSE);
        return;
    case Open: showMain(); break;
    case Exit: DestroyWindow(app.mainWindow); return;
    default: return;
    }
    refresh();
}

// --- Custom Control & Window Painting ---

void drawMain(HDC dc) {
    RECT client{};
    GetClientRect(app.mainWindow, &client);
    if (client.right <= 0 || client.bottom <= 0) return;
    std::wstring phase;
    switch (app.timer.phase()) {
    case restcue::Phase::Working: phase = L"工作中 · 距离休息"; break;
    case restcue::Phase::Resting: phase = L"休息中 · 剩余时间"; break;
    case restcue::Phase::Paused: phase = L"已暂停 · 剩余时间"; break;
    default: phase = L"已停止提醒"; break;
    }
    const auto countdown = clockText(app.timer.remaining(GetTickCount64()));
    const int focus = GetDlgCtrlID(GetFocus());
    const bool active = app.timer.phase() == restcue::Phase::Working || app.timer.phase() == restcue::Phase::Resting;
    // Keep the last frame. A mouse move can invalidate a few letters; re-rendering
    // ClearType text into that spot makes those letters flicker.
    auto& cache = app.mainCache;
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
    constexpr int minOpacity = static_cast<int>(Settings::MinOpacity);
    constexpr int maxOpacity = static_cast<int>(Settings::MaxOpacity);
    constexpr int opacityRange = maxOpacity - minOpacity;
    unsigned value = app.settings.opacity;
    unsigned input = 0;
    if (readNumber(OpacityEdit, input))
        value = std::clamp(input, Settings::MinOpacity, Settings::MaxOpacity);
    auto fromPoint = [&] {
        RECT rect{};
        GetClientRect(hwnd, &rect);
        const float edge = rect.bottom * .5f;
        const int x = static_cast<short>(LOWORD(lParam));
        const float fraction = (x - edge) / std::max(1.0f, rect.right - edge * 2);
        const auto next = static_cast<unsigned>(std::clamp(
            static_cast<int>(std::lround(minOpacity + opacityRange * fraction)), minOpacity, maxOpacity));
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
            restcue::theme::slider(buffer.dc(), rect.right, rect.bottom, value, GetFocus() == hwnd,
                                   Settings::MinOpacity, Settings::MaxOpacity);
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
        case VK_HOME: next = minOpacity; break;
        case VK_END: next = maxOpacity; break;
        case VK_PRIOR: next += 10; break;
        case VK_NEXT: next -= 10; break;
        default: return DefWindowProcW(hwnd, message, wParam, lParam);
        }
        setNumber(OpacityEdit, static_cast<unsigned>(std::clamp(next, minOpacity, maxOpacity)));
        return 0;
    }
    case WM_MOUSEWHEEL:
        setNumber(OpacityEdit, static_cast<unsigned>(std::clamp(static_cast<int>(value) +
            (static_cast<short>(HIWORD(wParam)) > 0 ? 1 : -1), minOpacity, maxOpacity)));
        return 0;
    case WM_SETFOCUS: case WM_KILLFOCUS:
        InvalidateRect(hwnd, nullptr, FALSE);
        InvalidateRect(app.mainWindow, nullptr, FALSE);
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
    const auto remaining = app.timer.remaining(GetTickCount64());
    const auto* surface = state->scene.renderCard(screen, width, height, clockText(remaining),
        static_cast<float>(remaining) / static_cast<float>(std::max<std::uint64_t>(1, app.timer.restDuration())),
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
        const auto remaining = app.timer.remaining(GetTickCount64());
        const auto* surface = state->scene.renderCard(dc, client.right, client.bottom, clockText(remaining),
            static_cast<float>(remaining) / static_cast<float>(std::max<std::uint64_t>(1, app.timer.restDuration())),
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

// --- Window Procedures ---

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
    case WM_CLOSE:
        if (!boxChecked(ForceRest)) PostMessageW(app.mainWindow, WM_COMMAND, Skip, 0);
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
    if (app.taskbarCreated && message == app.taskbarCreated) {
        addTray();
        app.lastStatus.clear();
        refresh();
        return 0;
    }
    switch (message) {
    case WM_CREATE: {
        app.mainWindow = hwnd;
        app.darkBrush = CreateSolidBrush(restcue::theme::Background);
        app.editBrush = CreateSolidBrush(restcue::theme::EditBackground);
        BOOL dark = FALSE;
        DwmSetWindowAttribute(hwnd, DwmUseImmersiveDarkMode, &dark, sizeof(dark));
        COLORREF caption = restcue::theme::Background, foreground = restcue::theme::Foreground;
        DwmSetWindowAttribute(hwnd, DwmCaptionColor, &caption, sizeof(caption));
        DwmSetWindowAttribute(hwnd, DwmTextColor, &foreground, sizeof(foreground));
        createControls();
        addTray();
        app.timer.start(GetTickCount64(), static_cast<std::uint64_t>(app.settings.workMinutes) * 60000,
                        static_cast<std::uint64_t>(app.settings.restSeconds) * 1000);
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
        if (wParam == SIZE_MINIMIZED) app.restoreSettingsAfterRest = false;
        if (wParam == SIZE_MINIMIZED && app.trayVisible) ShowWindow(hwnd, SW_HIDE);
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
        if (!app.overlays.empty()) hideOverlays();
        refresh();
        return 0;
    case WM_POWERBROADCAST:
        if (wParam == PBT_APMSUSPEND) hideOverlays();
        if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) {
            app.timer.onSystemResume(GetTickCount64());
            refresh();
        }
        return TRUE;
    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(wParam), restcue::theme::Foreground);
        SetBkColor(reinterpret_cast<HDC>(wParam), restcue::theme::EditBackground);
        return reinterpret_cast<LRESULT>(app.editBrush);
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wParam), restcue::theme::Foreground);
        SetBkColor(reinterpret_cast<HDC>(wParam), restcue::theme::Background);
        return reinterpret_cast<LRESULT>(app.darkBrush);
    case WM_CLOSE:
        app.restoreSettingsAfterRest = false;
        if (boxChecked(CloseMinimizes) && app.trayVisible) ShowWindow(hwnd, SW_HIDE);
        else DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        app.restoreSettingsAfterRest = false;
        KillTimer(hwnd, TickId);
        hideOverlays();
        app.mainCache.surface.clear();
        if (app.trayVisible) Shell_NotifyIconW(NIM_DELETE, &app.tray);
        if (app.uiFont) { DeleteObject(app.uiFont); app.uiFont = nullptr; }
        if (app.headingFont) { DeleteObject(app.headingFont); app.headingFont = nullptr; }
        if (app.darkBrush) { DeleteObject(app.darkBrush); app.darkBrush = nullptr; }
        if (app.editBrush) { DeleteObject(app.editBrush); app.editBrush = nullptr; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR commandLine, int showCommand) {
    app.instance = hInstance;
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
    if (app.settings.launchAtStartup) app.startup.apply(true, nullptr);
    app.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.hInstance = app.instance;
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
        nullptr, nullptr, app.instance, nullptr);
    if (!hwnd) {
        MessageBoxW(nullptr, L"无法创建主窗口或计时器。", Title, MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        return 1;
    }
    if (!(startInTray && app.trayVisible)) {
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
