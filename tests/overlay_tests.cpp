#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../theme.hpp"
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
unsigned checks = 0;
void expect(bool ok, const char* message) {
    ++checks;
    if (!ok) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}
}

int main() {
    using namespace restcue::theme;
    Runtime runtime;
    expect(runtime.ok(), "GDI+ initializes");
    const RECT monitors[] = {{0, 0, 1920, 1080}, {-1920, -134, 0, 946}, {0, 0, 320, 240}};
    for (const auto& monitor : monitors) {
        for (const UINT dpi : {96U, 120U, 192U}) {
            const auto pane = restCardBounds(monitor, dpi);
            const int width = pane.right - pane.left;
            expect(std::abs(pane.left + pane.right - monitor.left - monitor.right) <= 2,
                   "ring centers horizontally, including negative monitor origins");
            expect(std::abs(pane.top * 2 + width - monitor.top - monitor.bottom) <= 2,
                   "ring centers vertically, independently of the instruction");
            expect(pane.left >= monitor.left && pane.right <= monitor.right &&
                   pane.top >= monitor.top && pane.bottom <= monitor.bottom,
                   "ring and instruction fit the monitor");
        }
    }
    expect(std::wstring(restCueCaption(300000)) == L"休息一下，看看远处", "long rests use a relaxed message");
    expect(std::wstring(restCueCaption(15001)) == L"休息一下，看看远处", "recovery message is not shown early");
    expect(std::wstring(restCueCaption(15000)) == L"眨眨眼，准备继续吧", "recovery message starts in the last 15 seconds");
    expect(std::wstring(restCueCaption(0)) == L"眨眨眼，准备继续吧", "recovery message remains stable at the endpoint");
    expect(std::wstring(restCueCaption(9000, 10000)) == L"休息一下，看看远处", "short rests show primary prompt first");
    expect(std::wstring(restCueCaption(3000, 10000)) == L"眨眨眼，准备继续吧", "short rests switch in the final third");
    HDC dc = GetDC(nullptr);
    OverlayScene scene;
    Surface shade;
    expect(shade.resize(dc, 64, 64), "allocate shade test surface");
    scene.paintShade(shade.dc(), 64, 64);
    const auto shadeColor = GetPixel(shade.dc(), 0, 0);
    expect(GetRValue(shadeColor) < 24 && GetGValue(shadeColor) < 32 && GetBValue(shadeColor) < 48,
           "shade is slightly darker without changing the selected opacity");
    const auto* frame = scene.renderCard(dc, 440, 500, L"00:28", .72f, 28000);
    expect(frame && frame->bits(), "render transparent frame");
    auto alpha = [&](int x, int y) { return frame->bits()[(y * frame->width() + x) * 4 + 3]; };
    expect(alpha(0, 0) == 0 && alpha(220, 110) == 0, "corners and ring interior have no opaque disc");
    expect(alpha(220, 24) == 255, "progress arc is opaque");
    expect(alpha(220, 12) == 255 && alpha(220, 36) == 255 &&
           alpha(220, 8) == 0 && alpha(220, 40) == 0, "progress arc uses a thicker 28px stroke");
    const auto* top = frame->bits() + (24 * frame->width() + 220) * 4;
    expect(top[1] >= 150 && top[1] <= 200 && top[2] >= 90 && top[0] >= 120 &&
           top[1] > top[0] && top[1] > top[2], "arc uses muted green instead of fluorescent green");
    auto greenAt = [&](int y) { return frame->bits()[(y * frame->width() + 220) * 4 + 1]; };
    expect(greenAt(12) >= greenAt(36) && greenAt(428) <= greenAt(404),
           "gradient stays smooth across the outer stroke without color wrapping");
    unsigned transparent = 0, white = 0, green = 0;
    bool premultiplied = true;
    for (int i = 0; i < 440 * 500; ++i) {
        const auto* p = frame->bits() + i * 4;
        if (!p[3]) ++transparent;
        if (p[3] > 240 && p[0] > 230 && p[1] > 230 && p[2] > 230) ++white;
        if (p[3] > 240 && p[1] >= 150 && p[1] <= 200 &&
            p[1] > p[0] + 15 && p[1] > p[2] + 25 && p[2] >= 80) ++green;
        if (p[0] > p[3] || p[1] > p[3] || p[2] > p[3]) premultiplied = false;
    }
    expect(transparent > 440U * 500U * 3U / 4U, "most of the cue is transparent");
    expect(white > 1000 && green > 3000, "white text and muted green arc rendered");
    expect(premultiplied, "pixels are premultiplied for UpdateLayeredWindow");
    unsigned footerAlpha = 0;
    for (int y = 445; y < 500; ++y)
        for (int x = 0; x < 440; ++x)
            footerAlpha = std::max<unsigned>(footerAlpha, alpha(x, y));
    expect(footerAlpha >= 160 && footerAlpha <= 190, "exit hint is legible but visually secondary");
    std::vector<BYTE> normal(frame->bits(), frame->bits() + 440 * 500 * 4);
    frame = scene.renderCard(dc, 440, 500, L"00:28", .72f, 15000);
    expect(frame && !std::equal(normal.begin() + 440 * 250 * 4, normal.begin() + 440 * 292 * 4,
                               frame->bits() + 440 * 250 * 4), "rendered caption changes in the last 15 seconds");
    frame = scene.renderCard(dc, 440, 500, L"00:28", .72f, 28000, true);
    expect(frame && !std::equal(normal.begin() + 440 * 445 * 4, normal.end(), frame->bits() + 440 * 445 * 4),
           "forced rest replaces the exit instruction");
    for (float progress : {0.0f, 1.0f}) {
        frame = scene.renderCard(dc, 550, 625, L"00:00", progress, 0);
        expect(frame && frame->width() == 550 && frame->height() == 625, "DPI resize and progress endpoints render");
    }
    if (dc) ReleaseDC(nullptr, dc);
    std::cout << checks << " overlay checks passed\n";
}
