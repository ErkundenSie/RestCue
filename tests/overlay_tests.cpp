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
    HDC dc = GetDC(nullptr);
    OverlayScene scene;
    const auto* frame = scene.renderCard(dc, 440, 500, L"00:28", .72f);
    expect(frame && frame->bits(), "render transparent frame");
    auto alpha = [&](int x, int y) { return frame->bits()[(y * frame->width() + x) * 4 + 3]; };
    expect(alpha(0, 0) == 0 && alpha(220, 110) == 0, "corners and ring interior have no opaque disc");
    expect(alpha(220, 24) == 255, "progress arc is opaque");
    unsigned transparent = 0, white = 0, green = 0;
    bool premultiplied = true;
    for (int i = 0; i < 440 * 500; ++i) {
        const auto* p = frame->bits() + i * 4;
        if (!p[3]) ++transparent;
        if (p[3] > 240 && p[0] > 230 && p[1] > 230 && p[2] > 230) ++white;
        if (p[3] > 240 && p[1] > 200 && p[2] < 180) ++green;
        if (p[0] > p[3] || p[1] > p[3] || p[2] > p[3]) premultiplied = false;
    }
    expect(transparent > 440U * 500U * 3U / 4U, "most of the cue is transparent");
    expect(white > 1000 && green > 3000, "white text and vivid green arc rendered");
    expect(premultiplied, "pixels are premultiplied for UpdateLayeredWindow");
    std::vector<BYTE> normal(frame->bits(), frame->bits() + 440 * 500 * 4);
    frame = scene.renderCard(dc, 440, 500, L"00:28", .72f, true);
    expect(frame && !std::equal(normal.begin() + 440 * 445 * 4, normal.end(), frame->bits() + 440 * 445 * 4),
           "forced rest replaces the exit instruction");
    for (float progress : {0.0f, 1.0f}) {
        frame = scene.renderCard(dc, 550, 625, L"00:00", progress);
        expect(frame && frame->width() == 550 && frame->height() == 625, "DPI resize and progress endpoints render");
    }
    if (dc) ReleaseDC(nullptr, dc);
    std::cout << checks << " overlay checks passed\n";
}
