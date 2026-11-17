#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../theme.hpp"
#include <cstdio>

bool save(restcue::theme::Surface& surface, const wchar_t* path) {
    Gdiplus::Bitmap image(static_cast<HBITMAP>(GetCurrentObject(surface.dc(), OBJ_BITMAP)), nullptr);
    const CLSID png{0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
    return image.Save(path, &png, nullptr) == Gdiplus::Ok;
}

int main() {
    using namespace restcue::theme;
    namespace layout = settingsLayout;
    Runtime runtime;
    if (!runtime.ok()) return 1;
    HDC screen = GetDC(nullptr);
    Surface main, overlay;
    const float scale = 1.25f * SettingsScale;
    auto pixel = [&](int value) { return static_cast<int>(std::lround(value * scale)); };
    if (!main.resize(screen, pixel(static_cast<int>(MainWidth)), pixel(static_cast<int>(MainHeight))) ||
        !overlay.resize(screen, 1280, 800)) return 2;
    ReleaseDC(nullptr, screen);
    {
        Gdiplus::Graphics g(main.dc());
        mainPanel(g, scale, L"工作中 · 距离休息", L"16:15", true);
        g.ScaleTransform(scale, scale);
        auto number = [&](const wchar_t* label, layout::Placement p) {
            text(g, label, Gdiplus::RectF(static_cast<float>(p.x), static_cast<float>(p.y),
                 static_cast<float>(p.width), static_cast<float>(p.height)), 21.3f,
                 opaque(Foreground), false, Gdiplus::StringAlignmentCenter);
        };
        number(L"20", layout::Work);
        number(L"5", layout::RestMinutes);
        number(L"0", layout::RestSeconds);
        number(L"55", layout::OpacityValue);
    }
    auto child = [&](layout::Placement p, const wchar_t* label, bool primary, bool enabled) {
        Surface part;
        const int width = pixel(p.width), height = pixel(p.height);
        part.resize(main.dc(), width, height);
        button(part.dc(), Gdiplus::RectF(0, 0, static_cast<float>(width), static_cast<float>(height)), label,
               primary, enabled, false, false, false);
        BitBlt(main.dc(), pixel(p.x), pixel(p.y), width, height, part.dc(), 0, 0, SRCCOPY);
    };
    auto arrow = [&](layout::Placement p, bool up) {
        Surface part;
        const int width = pixel(p.width), height = pixel(p.height);
        part.resize(main.dc(), width, height);
        stepper(part.dc(), Gdiplus::RectF(0, 0, static_cast<float>(width), static_cast<float>(height)),
                up, true, false, false, false);
        BitBlt(main.dc(), pixel(p.x), pixel(p.y), width, height, part.dc(), 0, 0, SRCCOPY);
    };
    arrow(layout::WorkUp, true);
    arrow(layout::WorkDown, false);
    child(layout::Apply, L"保存", true, true);
    child(layout::Pause, L"暂停", false, true);
    child(layout::Rest, L"立即休息", false, true);
    child(layout::Skip, L"跳过休息", false, false);
    Surface track;
    const auto p = layout::OpacityTrack;
    track.resize(main.dc(), pixel(p.width), pixel(p.height));
    slider(track.dc(), pixel(p.width), pixel(p.height), 55, false);
    BitBlt(main.dc(), pixel(p.x), pixel(p.y), pixel(p.width), pixel(p.height), track.dc(), 0, 0, SRCCOPY);
    OverlayScene scene;
    scene.paintShade(overlay.dc(), 1280, 800);
    const RECT monitor{0, 0, 1280, 800};
    const auto bounds = restCardBounds(monitor, 120);
    const auto* restCard = scene.renderCard(overlay.dc(), bounds.right - bounds.left,
        bounds.bottom - bounds.top, L"04:49", .72f);
    if (!restCard) return 3;
    Gdiplus::Bitmap cardImage(restCard->width(), restCard->height(), restCard->width() * 4,
                              PixelFormat32bppPARGB, restCard->bits());
    {
        Gdiplus::Graphics composite(overlay.dc());
        setup(composite);
        composite.DrawImage(&cardImage, bounds.left, bounds.top, restCard->width(), restCard->height());
    }
    const bool a = save(main, L"build/main-preview.png");
    const bool b = save(overlay, L"build/overlay-preview.png");
    std::printf("Preview PNGs: main=%d overlay=%d\n", a, b);
    return a && b ? 0 : 3;
}
