#include "drawing.hpp"
namespace quota {
Surface::Surface(int w, int h) : width(std::max(1, w)), height(std::max(1, h)) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    dc = CreateCompatibleDC(nullptr);
    void *data = nullptr;
    dib = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &data, nullptr, 0);
    if (!dc || !dib)
        throw std::runtime_error("Cannot create drawing surface");
    pixels = static_cast<BYTE *>(data);
    old = SelectObject(dc, dib);
    bitmap = std::make_unique<Bitmap>(width, height, width * 4, PixelFormat32bppPARGB, pixels);
}
Surface::~Surface() {
    bitmap.reset();
    if (dc && old)
        SelectObject(dc, old);
    if (dib)
        DeleteObject(dib);
    if (dc)
        DeleteDC(dc);
}
void Surface::present(HWND window, int x, int y) {
    POINT destination{x, y}, source{};
    SIZE size{width, height};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(window, nullptr, &destination, &size, dc, &source, 0, &blend, ULW_ALPHA);
}
void Surface::save(const fs::path &file) {
    CLSID png{0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
    if (bitmap->Save(file.c_str(), &png, nullptr) != Ok)
        throw std::runtime_error("Cannot save PNG");
}
void roundPath(GraphicsPath &p, RectF r, float radius) {
    float d = std::min({radius * 2, r.Width, r.Height});
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    p.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    p.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    p.CloseFigure();
}
void fillRound(Graphics &g, RectF r, float radius, Color c) {
    GraphicsPath p;
    roundPath(p, r, radius);
    SolidBrush brush(c);
    g.FillPath(&brush, &p);
}
void drawText(Graphics &g, const std::wstring &text, RectF area, float size, Color c, bool bold,
              bool centered, bool glyph) {
    FontFamily family(L"Segoe UI");
    FontFamily chinese(L"Microsoft YaHei UI");
    FontFamily *f = &family;
    if (std::any_of(text.begin(), text.end(), [](wchar_t x) { return x > 0x2fff; }))
        f = &chinese;
    SolidBrush brush(c);
    StringFormat format;
    format.SetFormatFlags(StringFormatFlagsNoWrap);
    format.SetLineAlignment(StringAlignmentCenter);
    format.SetAlignment(centered ? StringAlignmentCenter : StringAlignmentNear);
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    if (glyph) {
        GraphicsPath path;
        RectF ink;
        for (;;) {
            path.Reset();
            path.AddString(text.c_str(), -1, f, bold ? FontStyleBold : FontStyleRegular, size, PointF(0, 0),
                           StringFormat::GenericTypographic());
            path.GetBounds(&ink);
            if ((ink.Width <= area.Width && ink.Height <= area.Height) || size <= 1)
                break;
            size -= .25f;
        }
        Matrix m;
        m.Translate(area.X + area.Width / 2 - ink.X - ink.Width / 2,
                    area.Y + area.Height / 2 - ink.Y - ink.Height / 2);
        path.Transform(&m);
        g.FillPath(&brush, &path);
        return;
    }
    for (;;) {
        Font font(f, size, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
        RectF measure;
        g.MeasureString(text.c_str(), -1, &font, PointF(0, 0), &measure);
        if (measure.Width <= area.Width || size <= 6) {
            g.DrawString(text.c_str(), -1, &font, area, &format, &brush);
            return;
        }
        size -= .5f;
    }
}
static void progressOutline(Graphics &g, GraphicsPath &path, Color track, Color accent, float width,
                            double fill) {
    Pen base(track, width);
    base.SetLineJoin(LineJoinRound);
    g.DrawPath(&base, &path);
    if (fill <= 0)
        return;
    path.Flatten(nullptr, .1f);
    int count = path.GetPointCount();
    if (count < 2)
        return;
    std::vector<PointF> points(count);
    path.GetPathPoints(points.data(), count);
    points.push_back(points.front());
    auto dist = [](PointF a, PointF b) { return std::hypot(b.X - a.X, b.Y - a.Y); };
    float length = 0;
    for (size_t i = 1; i < points.size(); ++i)
        length += dist(points[i - 1], points[i]);
    float left = length * float(std::clamp(fill, 0., 100.) / 100);
    Pen pen(accent, width);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    for (size_t i = 1; i < points.size() && left > 0; ++i) {
        auto a = points[i - 1], b = points[i];
        float segment = dist(a, b);
        if (segment < .001f)
            continue;
        float t = std::min(1.f, left / segment);
        g.DrawLine(&pen, a, PointF(a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t));
        left -= segment;
    }
}
void drawWidget(Graphics &g, RectF b, const Quotas &q, const Settings &s, bool stale, bool light, double hp,
                double wp) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    int count = q.weekOnly() ? 1 : 2;
    float width = b.Width / count, scale = b.Height / 32;
    auto pref = s.pref();
    Color track = light ? Color(255, 190, 198, 208) : Color(255, 85, 96, 112);
    Color ink = light ? Color(255, 32, 32, 32) : Color(255, 245, 245, 245);
    for (int i = 0; i < count; ++i) {
        bool hour = !q.weekOnly() && i == 0;
        auto window = hour ? q.hour : q.week;
        Color accent = stale ? Color(255, 128, 128, 128)
                             : parseColor(hour ? s.hourColor : s.weekColor, Color(255, 33, 158, 134));
        double progress = pref.animate ? (hour ? hp : wp) : 1;
        double fill = window ? (pref.remaining ? window->remaining : 100) * progress : 0;
        RectF area(b.X + width * i + 4 * scale, b.Y + 2 * scale, width - 8 * scale, b.Height - 4 * scale),
            text = area;
        if (s.style == "gauges") {
            float diameter = 24 * scale;
            RectF ring(area.X, area.Y + (area.Height - diameter) / 2, diameter, diameter);
            Pen base(track, 2.8f * scale), pen(accent, 2.8f * scale);
            pen.SetStartCap(LineCapRound);
            pen.SetEndCap(LineCapRound);
            g.DrawEllipse(&base, ring);
            if (fill > 0)
                g.DrawArc(&pen, ring, -90, float(fill * 3.6));
            if (s.letters)
                drawText(g, hour ? L"H" : L"W",
                         RectF(ring.X + 3 * scale, ring.Y + 3 * scale, ring.Width - 6 * scale,
                               ring.Height - 6 * scale),
                         10.5f * scale, stale ? accent : ink, true, true, true);
            text = RectF(ring.GetRight() + 5 * scale, area.Y, area.GetRight() - ring.GetRight() - 5 * scale,
                         area.Height);
            drawText(g, numberLabel(window, pref.percent), text, 17 * scale, stale ? accent : ink, true,
                     true);
            continue;
        }
        if (s.style == "badges" || s.style == "ticks") {
            RectF tag(area.X + scale, area.Y + (area.Height - 20 * scale) / 2, 25 * scale, 20 * scale);
            if (s.style == "badges") {
                GraphicsPath path;
                roundPath(path, tag, 3 * scale);
                progressOutline(g, path, track, accent, 2.2f * scale, fill);
            }
            drawText(g, hour ? L"5h" : L"7d", tag, 10.5f * scale, accent, true, true);
            text = RectF(tag.GetRight() + 4 * scale, area.Y,
                         std::max(1.f, area.GetRight() - tag.GetRight() - 4 * scale), area.Height);
        }
        if (s.style == "outlined") {
            float d = std::max(1.f, std::min(area.Width, area.Height) - 3 * scale);
            RectF ring(area.X + (area.Width - d) / 2, area.Y + (area.Height - d) / 2, d, d);
            Pen base(track, 2 * scale), pen(accent, 2 * scale);
            g.DrawEllipse(&base, ring);
            if (fill > 0)
                g.DrawArc(&pen, ring, -90, float(fill * 3.6));
            text = RectF(ring.X + 2 * scale, ring.Y + 2 * scale, ring.Width - 4 * scale,
                         ring.Height - 4 * scale);
        }
        if (s.style == "underline" || s.style == "ticks") {
            RectF line(area.X, area.GetBottom() - 2.5f * scale, area.Width, 2.5f * scale);
            SolidBrush base(track), pen(accent);
            g.FillRectangle(&base, line);
            if (fill > 0)
                g.FillRectangle(&pen, RectF(line.X, line.Y, line.Width * float(fill / 100), line.Height));
            text.Height -= 4 * scale;
        }
        if (s.style == "side") {
            RectF bar(area.X, area.Y + 4 * scale, 3 * scale, area.Height - 8 * scale);
            SolidBrush base(track), pen(accent);
            g.FillRectangle(&base, bar);
            if (fill > 0)
                g.FillRectangle(&pen, RectF(bar.X, bar.GetBottom() - bar.Height * float(fill / 100),
                                            bar.Width, bar.Height * float(fill / 100)));
            text = RectF(area.X + 6 * scale, area.Y, area.Width - 6 * scale, area.Height);
        }
        auto label = numberLabel(window, pref.percent, s.style == "plain" ? progress : 1);
        drawText(g, label, text, (s.style == "outlined" ? 14 : 18) * scale,
                 stale || s.style == "plain" ? accent : ink, true, true, s.style == "outlined");
    }
}
static RECT inkBounds(Surface &surface) {
    RECT bounds{surface.width, surface.height, -1, -1};
    for (int y = 0; y < surface.height; ++y)
        for (int x = 0; x < surface.width; ++x)
            if (surface.pixels[(y * surface.width + x) * 4 + 3] > 1) {
                bounds.left = std::min<LONG>(bounds.left, x);
                bounds.right = std::max<LONG>(bounds.right, x);
                bounds.top = std::min<LONG>(bounds.top, y);
                bounds.bottom = std::max<LONG>(bounds.bottom, y);
            }
    if (bounds.right >= 0) {
        bounds.right++;
        bounds.bottom++;
    }
    return bounds;
}
Layout widgetLayout(const Settings &s, bool weekOnly, int dpi, int available) {
    int cell = s.style == "gauges"     ? 92
               : s.style == "badges"   ? 94
               : s.style == "outlined" ? 40
               : s.style == "ticks"    ? 82
                                       : 68;
    double scale = s.scale * dpi / 96.;
    int w = 1, h = 1;
    RECT ink{};
    available = std::max(8, available);
    Settings measured = s;
    measured.preferences[measured.styleIndex()].animate = false;
    for (int attempt = 0; attempt < 6; ++attempt) {
        w = std::max(1, int(std::round(cell * (weekOnly ? 1 : 2) * scale)));
        h = std::max(1, int(std::round(32 * scale)));
        ink = {w, h, -1, -1};
        for (int value : {0, 68, 100, -1}) {
            Quotas q;
            q.plan = weekOnly ? "pro" : "plus";
            if (value >= 0)
                q.hour = q.week = WindowQuota{double(value), 0};
            Surface image(w, h);
            {
                Graphics g(image.bitmap.get());
                g.Clear(Color(1, 0, 0, 0));
                drawWidget(g, RectF(0, 0, float(w), float(h)), q, measured, false, true);
            }
            auto b = inkBounds(image);
            if (b.right >= 0) {
                ink.left = std::min(ink.left, b.left);
                ink.top = std::min(ink.top, b.top);
                ink.right = std::max(ink.right, b.right);
                ink.bottom = std::max(ink.bottom, b.bottom);
            }
        }
        if (ink.bottom - ink.top <= available - 2)
            break;
        scale *= std::max(.1, (available - 2.) / (ink.bottom - ink.top)) * .99;
    }
    int height = std::min(h, available);
    return {w, height, RectF(0, float((height - (ink.bottom - ink.top)) / 2.) - ink.top, float(w), float(h))};
}
void paintWidget(Surface &image, const Layout &layout, const State &state, const Settings &s, bool hover,
                 double hp, double wp) {
    Graphics g(image.bitmap.get());
    g.Clear(Color(1, 0, 0, 0));
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    bool light = lightTheme();
    if (hover)
        fillRound(g, RectF(.5f, .5f, image.width - 1.f, image.height - 1.f), 4 * image.height / 32.f,
                  Color(light ? 112 : 28, 255, 255, 255));
    drawWidget(g, layout.drawing, state.q, s, state.stale(), light, hp, wp);
}
void renderPreviews(const fs::path &dir) {
    fs::create_directories(dir);
    Surface image(1080, 620);
    {
        Graphics g(image.bitmap.get());
        g.Clear(Color(255, 248, 248, 248));
        drawText(g, L"原生版 · 七种任务栏样式", RectF(36, 18, 1000, 50), 25, Color(255, 30, 30, 30), true);
        drawText(g, L"模拟额度：5 小时 68%，每周 27%", RectF(36, 68, 1000, 30), 14,
                 Color(255, 105, 105, 105));
        for (int i = 0; i < 7; ++i) {
            int y = 120 + i * 65;
            drawText(g, StyleNames[i], RectF(36, float(y), 240, 46), 16, Color(255, 40, 40, 40));
            Settings s;
            s.style = StyleIds[i];
            Quotas q;
            q.hour = WindowQuota{68, 0};
            q.week = WindowQuota{27, 0};
            auto l = widgetLayout(s, false, 144, 48);
            drawWidget(g, RectF(310, float(y + 3), float(l.width), float(l.height)), q, s, false, true);
            s.preferences[i].percent = false;
            drawWidget(g, RectF(700, float(y + 3), float(l.width), float(l.height)), q, s, false, true);
        }
    }
    image.save(dir / L"native-styles.png");
}
} // namespace quota
