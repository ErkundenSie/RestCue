#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace restcue::theme {
using namespace Gdiplus;
constexpr COLORREF Background = RGB(246, 244, 239);
constexpr COLORREF EditBackground = RGB(242, 245, 240);
constexpr COLORREF Foreground = RGB(37, 57, 50);
constexpr COLORREF MutedForeground = RGB(96, 113, 103);
constexpr COLORREF CardBackground = RGB(255, 254, 251);
constexpr COLORREF Accent = RGB(53, 115, 99);
inline Color opaque(COLORREF color) { return Color(255, GetRValue(color), GetGValue(color), GetBValue(color)); }
constexpr float MainWidth = 800, MainHeight = 532;
// Compact the settings window only; rest overlays retain their own screen-relative layout.
constexpr int SettingsScalePercent = 78;
constexpr float SettingsScale = SettingsScalePercent / 100.0f;

namespace settingsLayout {
struct Placement { int x, y, width, height; };
inline constexpr Placement Work{241, 199, 52, 30}, WorkUp{298, 196, 18, 16}, WorkDown{298, 216, 18, 16};
inline constexpr Placement RestMinutes{560, 199, 42, 30}, RestSeconds{662, 199, 42, 30};
inline constexpr Placement OpacityTrack{290, 333, 326, 28}, OpacityValue{650, 333, 52, 30};
inline constexpr Placement Startup{40, 410, 140, 32}, CloseMinimizes{190, 410, 230, 32}, ForceRest{430, 410, 150, 32};
inline constexpr Placement Apply{32, 452, 218, 48}, Pause{262, 452, 158, 48};
inline constexpr Placement Rest{432, 452, 158, 48}, Skip{602, 452, 166, 48};
}

class Runtime {
public:
    Runtime() { GdiplusStartupInput input; ok_ = GdiplusStartup(&token_, &input, nullptr) == Ok; }
    ~Runtime() { if (ok_) GdiplusShutdown(token_); }
    bool ok() const { return ok_; }
private:
    ULONG_PTR token_ = 0;
    bool ok_ = false;
};

// Reuse full-screen buffers: foliage is painted once, not rebuilt every frame.
class Surface {
public:
    ~Surface() { clear(); }
    Surface() = default;
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;
    bool resize(HDC reference, int width, int height) {
        if (dc_ && width == width_ && height == height_) return true;
        clear();
        if (width <= 0 || height <= 0) return false;
        dc_ = CreateCompatibleDC(reference);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        bitmap_ = CreateDIBSection(reference, &info, DIB_RGB_COLORS, &bits_, nullptr, 0);
        if (!dc_ || !bitmap_) { clear(); return false; }
        old_ = SelectObject(dc_, bitmap_);
        width_ = width; height_ = height;
        return true;
    }
    HDC dc() const { return dc_; }
    BYTE* bits() const { return static_cast<BYTE*>(bits_); }
    int width() const { return width_; }
    int height() const { return height_; }
private:
    void clear() {
        if (dc_ && old_) SelectObject(dc_, old_);
        if (bitmap_) DeleteObject(bitmap_);
        if (dc_) DeleteDC(dc_);
        dc_ = nullptr; bitmap_ = nullptr; old_ = nullptr; bits_ = nullptr; width_ = height_ = 0;
    }
    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ old_ = nullptr;
    void* bits_ = nullptr;
    int width_ = 0, height_ = 0;
};

inline void setup(Graphics& g) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.SetCompositingQuality(CompositingQualityHighQuality);
}

inline void rounded(GraphicsPath& path, RectF r, float radius) {
    const float d = std::min(radius * 2, std::min(r.Width, r.Height));
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    path.CloseFigure();
}

inline void box(Graphics& g, RectF r, float radius, Color top, Color bottom,
                Color border = Color(0, 0, 0, 0), float stroke = 1) {
    GraphicsPath shape;
    rounded(shape, r, radius);
    LinearGradientBrush fill(PointF(r.X, r.Y), PointF(r.X, r.GetBottom()), top, bottom);
    g.FillPath(&fill, &shape);
    if (border.GetA()) { Pen edge(border, stroke); g.DrawPath(&edge, &shape); }
}

inline void glow(Graphics& g, RectF area, Color color) {
    GraphicsPath shape;
    shape.AddEllipse(area);
    PathGradientBrush brush(&shape);
    brush.SetCenterColor(color);
    Color transparent(0, color.GetR(), color.GetG(), color.GetB());
    INT count = 1;
    brush.SetSurroundColors(&transparent, &count);
    brush.SetFocusScales(0.03f, 0.03f);
    g.FillPath(&brush, &shape);
}

inline void text(Graphics& g, const std::wstring& value, RectF area, float size,
                 Color color, bool bold = false, StringAlignment align = StringAlignmentNear,
                 const wchar_t* familyName = L"Microsoft YaHei UI") {
    // A world scale (the compact 78% layout, or a DPI change) would rasterize glyphs and
    // then stretch them. Draw text in device pixels at a whole-pixel size instead.
    Matrix transform;
    g.GetTransform(&transform);
    REAL m[6]{};
    transform.GetElements(m);
    const float sx = m[0];
    PointF corners[2] = {PointF(area.X, area.Y), PointF(area.GetRight(), area.GetBottom())};
    transform.TransformPoints(corners, 2);
    const bool scaled = std::fabs(sx - 1.0f) > 0.001f || std::fabs(m[1]) > 0.001f || std::fabs(m[2]) > 0.001f;
    if (scaled) g.ResetTransform();
    const float deviceSize = std::max(1.0f, std::round(size * std::fabs(sx)));
    FontFamily family(familyName);
    Font font(&family, scaled ? deviceSize : size, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    SolidBrush brush(color);
    StringFormat format;
    format.SetAlignment(align);
    format.SetLineAlignment(StringAlignmentCenter);
    format.SetFormatFlags(StringFormatFlagsNoWrap);
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    RectF target = area;
    if (scaled) {
        target = RectF(std::min(corners[0].X, corners[1].X), std::min(corners[0].Y, corners[1].Y),
                       std::fabs(corners[1].X - corners[0].X), std::fabs(corners[1].Y - corners[0].Y));
        target.X = std::round(target.X);
        target.Y = std::round(target.Y);
    }
    g.DrawString(value.c_str(), static_cast<INT>(value.size()), &font, target, &format, &brush);
    if (scaled) g.SetTransform(&transform);
}

inline void mainPanel(Graphics& g, float scale, const std::wstring& phase,
                      const std::wstring& countdown, bool active, int focusedControl = 0) {
    setup(g);
    g.ScaleTransform(scale, scale);
    SolidBrush canvas(opaque(Background));
    g.FillRectangle(&canvas, 0.0f, 0.0f, MainWidth, MainHeight);
    glow(g, RectF(-180, -230, 720, 390), Color(18, 163, 192, 165));
    const Color ink = opaque(Foreground), muted = opaque(MutedForeground);
    const Color card = opaque(CardBackground), edge(255, 218, 224, 215);
    const Color input = opaque(EditBackground), accent = opaque(Accent);

    text(g, L"RESTCUE  /  休息提醒", RectF(32, 20, 400, 22), 14.5f, Color(255, 83, 119, 102));
    text(g, L"给眼睛放个假", RectF(32, 46, 490, 42), 30, ink, true);
    text(g, L"安排好专注，也给眼睛一点留白。", RectF(33, 91, 480, 24), 16.5f, muted);

    box(g, RectF(568, 26, 200, 88), 18, Color(12, 88, 118, 92), Color(12, 88, 118, 92));
    box(g, RectF(568, 24, 200, 88), 18, Color(255, 231, 241, 233), Color(255, 239, 246, 238),
        Color(255, 198, 218, 203));
    SolidBrush dot(active ? Color(255, 61, 133, 103) : Color(255, 161, 117, 51));
    g.FillEllipse(&dot, 585.0f, 45.0f, 6.0f, 6.0f);
    text(g, phase, RectF(600, 36, 156, 23), 14.5f, Color(255, 71, 100, 86));
    text(g, countdown, RectF(583, 60, 170, 39), 30, Color(255, 42, 95, 78), true,
        StringAlignmentCenter, L"Segoe UI");

    auto settingCard = [&](RectF r) {
        box(g, RectF(r.X, r.Y + 2, r.Width, r.Height), 15, Color(10, 70, 88, 72), Color(10, 70, 88, 72));
        box(g, r, 15, card, card, edge);
    };
    settingCard(RectF(32, 132, 360, 138));
    settingCard(RectF(408, 132, 360, 138));
    settingCard(RectF(32, 286, 736, 112));
    text(g, L"专注时长", RectF(54, 150, 180, 30), 19, ink, true);
    text(g, L"每轮专注的分钟数", RectF(54, 204, 176, 25), 16, muted);
    text(g, L"休息时长", RectF(430, 150, 180, 30), 19, ink, true);
    text(g, L"给眼睛充个电", RectF(430, 204, 120, 25), 16, muted);
    text(g, L"遮罩浓度", RectF(54, 304, 210, 30), 19, ink, true);
    text(g, L"10%–95% · 越高遮挡越强", RectF(54, 340, 220, 25), 16, muted);

    auto inputBox = [&](RectF r, int id) {
        const bool focused = focusedControl == id || (id == 104 && focusedControl == 105);
        box(g, r, 10, input, input, focused ? accent : Color(255, 207, 217, 205), focused ? 1.5f : 1.0f);
    };
    inputBox(RectF(232, 190, 138, 48), 101);
    inputBox(RectF(552, 190, 92, 48), 102);
    inputBox(RectF(654, 190, 92, 48), 103);
    inputBox(RectF(638, 326, 108, 44), 104);
    text(g, L"分钟", RectF(321, 198, 43, 32), 17, muted);
    text(g, L"分", RectF(609, 198, 28, 32), 17, muted);
    text(g, L"秒", RectF(711, 198, 28, 32), 17, muted);
    text(g, L"%", RectF(711, 332, 26, 31), 17, muted);
    g.ResetTransform();
}

inline void checkBox(HDC dc, RectF bounds, const std::wstring& label, bool checked,
                     bool pressed, bool hover, bool focus) {
    Graphics g(dc);
    setup(g);
    g.Clear(opaque(Background));
    const float boxSize = std::min(18.0f, std::max(12.0f, bounds.Height - 10));
    const float y = (bounds.Height - boxSize) / 2 + (pressed ? 0.5f : 0);
    RectF mark(1.5f, y, boxSize, boxSize);
    const Color fill = checked ? opaque(Accent) : opaque(CardBackground);
    const Color border = (focus || hover || checked) ? opaque(Accent) : Color(255, 176, 196, 184);
    box(g, mark, 4, fill, fill, border, focus ? 1.6f : 1.2f);
    if (checked) {
        Pen pen(Color(255, 251, 254, 252), std::max(1.5f, boxSize * 0.12f));
        pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound); pen.SetLineJoin(LineJoinRound);
        const PointF points[] = {
            PointF(mark.X + boxSize * 0.22f, mark.Y + boxSize * 0.52f),
            PointF(mark.X + boxSize * 0.42f, mark.Y + boxSize * 0.74f),
            PointF(mark.X + boxSize * 0.80f, mark.Y + boxSize * 0.28f)
        };
        g.DrawLines(&pen, points, 3);
    }
    text(g, label, RectF(boxSize + 10, 0, std::max(1.0f, bounds.Width - boxSize - 12), bounds.Height),
         std::min(16.0f, bounds.Height * 0.52f), opaque(Foreground));
}

inline void stepper(HDC dc, RectF bounds, bool up, bool enabled,
                    bool pressed, bool hover, bool focus) {
    Graphics g(dc);
    setup(g);
    g.Clear(opaque(EditBackground));
    const float scale = bounds.Height / 16.0f;
    if (enabled && (pressed || hover || focus)) {
        const Color fill = pressed ? Color(255, 216, 232, 219) : Color(255, 231, 241, 230);
        box(g, RectF(1.5f * scale, scale, bounds.Width - 3 * scale, bounds.Height - 2 * scale),
            3.5f * scale, fill, fill, focus ? Color(150, 116, 155, 126) : Color(0, 0, 0, 0), .8f * scale);
    }
    const float cx = bounds.Width * .5f;
    const float cy = bounds.Height * .5f + (pressed ? .4f * scale : 0);
    const float direction = up ? 1.0f : -1.0f;
    const PointF points[] = {
        PointF(cx - 3.3f * scale, cy + direction * 1.65f * scale),
        PointF(cx, cy - direction * 1.65f * scale),
        PointF(cx + 3.3f * scale, cy + direction * 1.65f * scale)
    };
    Pen pen(opaque(!enabled ? RGB(158, 170, 158) : (pressed || hover ? Accent : MutedForeground)), 1.35f * scale);
    pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound); pen.SetLineJoin(LineJoinRound);
    g.DrawLines(&pen, points, 3);
}

inline void button(HDC dc, RectF bounds, const std::wstring& label, bool primary,
                   bool enabled, bool pressed, bool hover, bool focus) {
    Graphics g(dc);
    setup(g);
    g.Clear(opaque(Background));
    const float scale = bounds.Height / 48.0f;
    RectF r(1.5f, 1.5f, bounds.Width - 3, bounds.Height - 3);
    Color top = enabled ? Color(255, 255, 254, 251) : Color(255, 239, 241, 235);
    Color bottom = top;
    Color border = enabled ? Color(255, 207, 217, 205) : Color(255, 222, 226, 218);
    if (primary && enabled) {
        top = hover ? Color(255, 64, 126, 109) : Color(255, 60, 121, 104);
        bottom = hover ? Color(255, 54, 114, 97) : Color(255, 47, 103, 88);
        border = Color(255, 48, 108, 91);
    } else if (hover && enabled) {
        top = bottom = Color(255, 232, 241, 232); border = Color(255, 140, 172, 146);
    }
    if (pressed && enabled) {
        top = bottom = primary ? Color(255, 43, 92, 79) : Color(255, 218, 233, 219);
    }
    box(g, r, 10 * scale, top, bottom, focus ? opaque(Accent) : border, focus ? 1.6f : 1.0f);
    text(g, label, r, 17 * scale,
         !enabled ? Color(255, 141, 152, 141) : primary ? Color(255, 251, 254, 252) : opaque(Foreground),
         primary, StringAlignmentCenter);
}

inline void slider(HDC dc, int width, int height, unsigned opacity, bool focused) {
    Graphics g(dc);
    setup(g);
    g.Clear(opaque(CardBackground));
    const float h = static_cast<float>(height), left = h * .5f, right = width - left;
    const float x = left + (right - left) * (std::clamp(opacity, 10U, 95U) - 10) / 85.0f;
    Pen base(Color(255, 207, 220, 208), h * .13f);
    base.SetStartCap(LineCapRound); base.SetEndCap(LineCapRound);
    g.DrawLine(&base, left, h * .5f, right, h * .5f);
    Pen fill(opaque(Accent), h * .13f);
    fill.SetStartCap(LineCapRound); fill.SetEndCap(LineCapRound);
    g.DrawLine(&fill, left, h * .5f, x, h * .5f);
    SolidBrush rim(Color(255, 216, 231, 216)), core(Color(255, 255, 254, 251));
    g.FillEllipse(&rim, x - h * .32f, h * .18f, h * .64f, h * .64f);
    g.FillEllipse(&core, x - h * .23f, h * .27f, h * .46f, h * .46f);
    Pen outline(opaque(Accent), focused ? 1.8f : 1.3f);
    g.DrawEllipse(&outline, x - h * .23f, h * .27f, h * .46f, h * .46f);
    if (focused) {
        Pen focus(Color(100, 73, 134, 104), 1);
        g.DrawEllipse(&focus, x - h * .42f, h * .08f, h * .84f, h * .84f);
    }
}

constexpr float RestCueWidth = 440, RestCueHeight = 500;
constexpr float RestRingCenter = 220;

inline const wchar_t* restCueCaption(std::uint64_t remainingMs) {
    return remainingMs <= 15000 ? L"休息中 · 即将恢复" : L"休息一下，看看远处";
}

inline RECT restCardBounds(const RECT& monitor, UINT dpi) {
    const int screenW = monitor.right - monitor.left, screenH = monitor.bottom - monitor.top;
    const float scale = std::max(0.01f, std::min({dpi / 96.0f,
        (screenW - 48.0f) / RestCueWidth,
        (screenH - 64.0f) / (2 * (RestCueHeight - RestRingCenter))}));
    const int width = std::max(1, static_cast<int>(std::lround(RestCueWidth * scale)));
    const int height = std::max(1, static_cast<int>(std::lround(RestCueHeight * scale)));
    const int x = monitor.left + (screenW - width) / 2;
    // Center the ring itself, not the ring plus the instruction underneath it.
    const int y = monitor.top + screenH / 2 - width / 2;
    return {x, y, x + width, y + height};
}

class OverlayScene {
public:
    void paintShade(HDC target, int width, int height) {
        const bool recreate = shade_.width() != width || shade_.height() != height;
        if (!shade_.resize(target, width, height)) return;
        if (recreate) {
            Graphics g(shade_.dc());
            setup(g);
            LinearGradientBrush veil(PointF(0, 0), PointF(static_cast<float>(width), static_cast<float>(height)),
                Color(255, 12, 20, 26), Color(255, 20, 30, 36));
            g.FillRectangle(&veil, 0, 0, width, height);
        }
        BitBlt(target, 0, 0, width, height, shade_.dc(), 0, 0, SRCCOPY);
    }

    const Surface* renderCard(HDC reference, int width, int height, const std::wstring& countdown,
                              float progress, std::uint64_t remainingMs, bool forced = false) {
        if (!card_.resize(reference, width, height)) return nullptr;
        // Draw directly into premultiplied pixels. ClearType/DC painting cannot preserve
        // alpha, and would leave a solid disc or black fringes on a layered window.
        Bitmap pixels(width, height, width * 4, PixelFormat32bppPARGB, card_.bits());
        Graphics g(&pixels);
        setup(g);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        g.Clear(Color(0, 0, 0, 0));
        const float scale = std::min(width / RestCueWidth, height / RestCueHeight);
        g.ScaleTransform(scale, scale);
        constexpr float center = RestRingCenter, radius = 196, stroke = 28;
        Pen track(Color(70, 255, 255, 255), stroke);
        g.DrawEllipse(&track, center - radius, center - radius, radius * 2, radius * 2);
        // Cover the outer stroke too, so the gradient cannot wrap at the ring edges.
        LinearGradientBrush green(PointF(center, center - radius - stroke / 2),
                                  PointF(center, center + radius + stroke / 2),
                                  Color(255, 145, 196, 173), Color(255, 92, 159, 131));
        Pen arc(&green, stroke);
        arc.SetStartCap(LineCapRound); arc.SetEndCap(LineCapRound);
        const float sweep = std::clamp(progress, 0.0f, 1.0f) * 360.0f;
        if (sweep >= 359.9f)
            g.DrawEllipse(&arc, center - radius, center - radius, radius * 2, radius * 2);
        else if (sweep > 0)
            g.DrawArc(&arc, center - radius, center - radius, radius * 2, radius * 2, -90.0f, sweep);
        text(g, countdown, RectF(18, 145, 404, 100), 84, Color(255, 255, 255, 255),
             true, StringAlignmentCenter, L"Segoe UI");
        text(g, restCueCaption(remainingMs), RectF(30, 252, 380, 38), 24,
             Color(224, 255, 255, 255), false, StringAlignmentCenter);
        const Color instruction(190, 255, 255, 255);
        if (!forced) {
            // Small mouse outline, aligned with the instruction rather than a button.
            GraphicsPath mouse;
            rounded(mouse, RectF(83, 455, 20, 28), 10);
            Pen outline(instruction, 1.5f);
            g.DrawPath(&outline, &mouse);
            g.DrawLine(&outline, 83.0f, 467.0f, 103.0f, 467.0f);
            g.DrawLine(&outline, 93.0f, 455.0f, 93.0f, 467.0f);
            text(g, L"右键单击退出本次休息", RectF(115, 450, 242, 38), 19,
                 instruction, false, StringAlignmentCenter);
        } else {
            text(g, L"强制休息 · 请完成本次休息", RectF(10, 450, 420, 38), 19,
                 instruction, false, StringAlignmentCenter);
        }
        g.Flush(FlushIntentionSync);
        return &card_;
    }

private:
    Surface shade_, card_;
};

} // namespace restcue::theme
