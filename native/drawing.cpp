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
void Surface::present(HWND window, int x, int y, bool move) {
    POINT destination{x, y}, source{};
    SIZE size{width, height};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(window, nullptr, move ? &destination : nullptr, &size, dc, &source, 0, &blend,
                        ULW_ALPHA);
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
const GlassPalette &glassPalette(bool dark) {
    static const GlassPalette light{
        Color(255, 250, 251, 253), Color(255, 250, 251, 253), Color(255, 255, 255, 255),
        Color(255, 28, 30, 36), Color(255, 76, 82, 95), Color(255, 104, 110, 123),
        Color(30, 41, 53, 76), Color(12, 35, 55, 89), Color(23, 35, 55, 89),
        Color(255, 41, 99, 196), Color(255, 153, 71, 20), Color(255, 204, 208, 215)};
    static const GlassPalette night{vibrancy::deep, vibrancy::mid, vibrancy::surface,
        vibrancy::ink, vibrancy::secondary, vibrancy::muted, vibrancy::border,
        vibrancy::hover, vibrancy::active, vibrancy::focus, vibrancy::warning, Color(255, 108, 110, 117)};
    return dark ? night : light;
}

std::unique_ptr<Surface> captureFrost(RECT bounds, bool simulated) {
    int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0) return {};
    // Blur a small in-memory sample once. Never sample the already visible panel.
    auto frost = std::make_unique<Surface>(std::max(1, width / 6), std::max(1, height / 6));
    if (simulated) {
        Graphics g(frost->bitmap.get());
        g.Clear(Color(255, 194, 213, 232));
        SolidBrush warm(Color(255, 244, 205, 185)), cool(Color(255, 171, 194, 231));
        g.FillEllipse(&warm, 0, 0, frost->width / 2, frost->height);
        g.FillEllipse(&cool, frost->width / 2, frost->height / 3, frost->width / 2, frost->height);
    } else {
        HDC screen = GetDC(nullptr);
        if (!screen) return {};
        SetStretchBltMode(frost->dc, HALFTONE);
        SetBrushOrgEx(frost->dc, 0, 0, nullptr);
        BOOL copied = StretchBlt(frost->dc, 0, 0, frost->width, frost->height, screen,
                                bounds.left, bounds.top, width, height, SRCCOPY);
        ReleaseDC(nullptr, screen);
        if (!copied) return {};
        GdiFlush();
    }
    size_t bytes = size_t(frost->width) * frost->height * 4;
    std::vector<BYTE> pass(frost->pixels, frost->pixels + bytes);
    for (bool horizontal : {true, false}) {
        for (int y = 0; y < frost->height; ++y)
            for (int x = 0; x < frost->width; ++x) {
                int sum[3]{};
                for (int offset = -4; offset <= 4; ++offset) {
                    int sx = std::clamp(x + (horizontal ? offset : 0), 0, frost->width - 1);
                    int sy = std::clamp(y + (horizontal ? 0 : offset), 0, frost->height - 1);
                    auto pixel = &pass[(sy * frost->width + sx) * 4];
                    for (int c = 0; c < 3; ++c) sum[c] += pixel[c];
                }
                auto dest = &frost->pixels[(y * frost->width + x) * 4];
                for (int c = 0; c < 3; ++c) dest[c] = BYTE(sum[c] / 9);
                dest[3] = 255;
            }
        if (horizontal) std::copy_n(frost->pixels, bytes, pass.data());
    }
    // Retain background colour through the material, without retaining text.
    for (size_t i = 0; i < bytes; i += 4) {
        float luminance = .0722f * frost->pixels[i] + .7152f * frost->pixels[i + 1] + .2126f * frost->pixels[i + 2];
        for (int c = 0; c < 3; ++c)
            frost->pixels[i + c] = BYTE(std::clamp(luminance + 1.22f * (frost->pixels[i + c] - luminance), 0.f, 255.f));
    }
    return frost;
}

void paintFrost(Graphics &g, RectF bounds, float radius, const GlassPalette &colors,
                const Surface *frost, RectF sourcePlacement) {
    if (radius > 0) {
        GraphicsPath path;
        roundPath(path, bounds, radius);
        auto saved = g.Save();
        g.SetClip(&path, CombineModeIntersect);
        paintFrost(g, bounds, 0, colors, frost, sourcePlacement);
        g.Restore(saved);
        bool dark = colors.ink.GetR() > 128;
        LinearGradientBrush rim(PointF(bounds.X, bounds.Y), PointF(bounds.X, bounds.GetBottom()),
                                Color(dark ? 90 : 205, 255, 255, 255), colors.border);
        Pen edge(&rim, 1);
        g.DrawPath(&edge, &path);
        return;
    }
    // Align the rectangular base and tint to device pixels. Each child paints
    // a slice of the same backdrop, so antialiased rectangle edges would seam.
    auto saved = g.Save();
    g.SetSmoothingMode(SmoothingModeNone);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    SolidBrush base(colors.mid);
    g.FillRectangle(&base, bounds);
    if (frost) {
        g.SetInterpolationMode(InterpolationModeHighQualityBilinear);
        g.DrawImage(frost->bitmap.get(), sourcePlacement);
        bool dark = colors.ink.GetR() > 128;
        SolidBrush tint(Color(dark ? 176 : 158, colors.mid.GetR(), colors.mid.GetG(), colors.mid.GetB()));
        g.FillRectangle(&tint, bounds);
        // The highlight and grain share parent coordinates, including clipped
        // child controls, so repainting and scrolling cannot produce seams.
        LinearGradientBrush sheen(PointF(sourcePlacement.X, sourcePlacement.Y),
                                  PointF(sourcePlacement.X, sourcePlacement.GetBottom()),
                                  Color(dark ? 12 : 38, 255, 255, 255), Color(0, 255, 255, 255));
        g.FillRectangle(&sheen, bounds);
        Surface grain(32, 32);
        unsigned seed = 0x435142u;
        for (int i = 0; i < 32 * 32; ++i) {
            seed = seed * 1664525u + 1013904223u;
            BYTE alpha = BYTE(2 + (seed >> 28) % 4);
            BYTE value = (seed & 0x10000) ? alpha : 0; // Premultiplied white or black.
            for (int c = 0; c < 3; ++c) grain.pixels[i * 4 + c] = value;
            grain.pixels[i * 4 + 3] = alpha;
        }
        TextureBrush texture(grain.bitmap.get(), WrapModeTile);
        texture.TranslateTransform(sourcePlacement.X, sourcePlacement.Y);
        g.FillRectangle(&texture, bounds);
    }
    g.Restore(saved);
}

void drawPanel(Graphics &g, RectF bounds, float radius, Color fill, Color border) {
    GraphicsPath path;
    roundPath(path, bounds, radius);
    SolidBrush background(fill);
    Pen edge(border, 0); // Hairline in device pixels, independent of DPI.
    g.FillPath(&background, &path);
    g.DrawPath(&edge, &path);
}
static bool usesCjkFont(const std::wstring &text) {
    return std::any_of(text.begin(), text.end(), [](wchar_t c) { return c > 0x2fff; });
}
float uiTextWidth(Graphics &g, const std::wstring &text, float size, bool bold) {
    Font font(usesCjkFont(text) ? L"Microsoft YaHei UI" : L"Segoe UI", size,
              bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    RectF bounds;
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.MeasureString(text.c_str(), -1, &font, PointF(0, 0), &bounds);
    return bounds.Width;
}
static void textImpl(Graphics &g, const std::wstring &text, RectF area, float size, Color c, bool bold,
              bool centered, bool glyph, bool ui = false) {
    FontFamily family(L"Segoe UI");
    FontFamily chinese(L"Microsoft YaHei UI");
    FontFamily *f = &family;
    if (usesCjkFont(text))
        f = &chinese;
    if (ui)
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    if (!f->IsAvailable()) f = &family;
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
void drawHeading(Graphics &g, const std::wstring &text, RectF bounds, float size, Color ink) {
    textImpl(g, text, bounds, size, ink, true, false, false, true);
}
void drawText(Graphics &g, const std::wstring &text, RectF area, float size, Color c, bool bold,
              bool centered, bool glyph) {
    textImpl(g, text, area, size, c, bold, centered, glyph);
}
void drawUiText(Graphics &g, const std::wstring &text, RectF area, float size, Color c, bool bold,
                bool centered) {
    // Codex desktop uses the system UI stack (Segoe UI on Windows).
    // Use Microsoft YaHei UI for CJK strings, avoiding the GDI+ serif fallback.
    auto previous = g.GetTextRenderingHint();
    textImpl(g, text, area, size, c, bold, centered, false, true);
    g.SetTextRenderingHint(previous);
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
static void drawWidgetInk(Graphics &g, RectF b, const Quotas &q, const Settings &s, bool stale, bool light, double hp,
                double wp) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    auto visible = s.visibility(q);
    int count = visible.count();
    if (!count)
        return;
    float width = b.Width / count, scale = b.Height / 32;
    auto pref = s.pref();
    Color track = light ? Color(255, 190, 198, 208) : Color(255, 85, 96, 112);
    Color ink = light ? Color(255, 32, 32, 32) : Color(255, 245, 245, 245);
    for (int i = 0; i < count; ++i) {
        bool hour = visible.hour && i == 0;
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
void drawWidget(Graphics &g, RectF bounds, const Quotas &q, const Settings &s, bool stale, bool light,
                double hp, double wp) {
    if (s.iconOpacity == 100) {
        drawWidgetInk(g, bounds, q, s, stale, light, hp, wp);
        return;
    }
    Surface layer(int(std::ceil(bounds.Width)), int(std::ceil(bounds.Height)));
    {
        Graphics ink(layer.bitmap.get());
        ink.Clear(Color(0, 0, 0, 0));
        drawWidgetInk(ink, RectF(0, 0, bounds.Width, bounds.Height), q, s, stale, light, hp, wp);
    }
    ColorMatrix matrix{{{1, 0, 0, 0, 0}, {0, 1, 0, 0, 0}, {0, 0, 1, 0, 0},
                        {0, 0, 0, s.iconOpacity / 100.f, 0}, {0, 0, 0, 0, 1}}};
    ImageAttributes attributes;
    attributes.SetColorMatrix(&matrix);
    g.DrawImage(layer.bitmap.get(), bounds, 0, 0, bounds.Width, bounds.Height, UnitPixel, &attributes);
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
Layout widgetLayout(const Settings &s, QuotaVisibility visible, int dpi, int available) {
    if (!visible.count())
        return {1, 1, RectF(0, 0, 1, 1)};
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
    measured.iconOpacity = 100;
    measured.showHour = visible.hour;
    measured.showWeek = visible.week;
    measured.preferences[measured.styleIndex()].animate = false;
    for (int attempt = 0; attempt < 6; ++attempt) {
        w = std::max(1, int(std::round(cell * visible.count() * scale)));
        h = std::max(1, int(std::round(32 * scale)));
        ink = {w, h, -1, -1};
        for (int value : {0, 68, 100, -1}) {
            Quotas q;
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
void drawWidgetBackground(Graphics &g, RectF bounds, const Settings &settings, bool light, bool hover) {
    int alpha = (255 * settings.backgroundOpacity + 50) / 100;
    if (hover) alpha = std::max(alpha, light ? 112 : 75);
    if (!alpha) return;
    bounds.Inflate(-.5f, -.5f);
    Color tint = glassPalette(settings.darkAppearance).surface;
    fillRound(g, bounds, std::min(8.f, bounds.Height / 6),
              Color(BYTE(alpha), tint.GetR(), tint.GetG(), tint.GetB()));
}
void paintWidget(Surface &image, const Layout &layout, const State &state, const Settings &s, bool hover,
                 double hp, double wp) {
    Graphics g(image.bitmap.get());
    g.Clear(Color(1, 0, 0, 0));
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    bool light = lightTheme();
    drawWidgetBackground(g, RectF(0, 0, float(image.width), float(image.height)), s, light, hover);
    drawWidget(g, layout.drawing, state.q, s, state.stale(), s.backgroundOpacity >= 52 ? !s.darkAppearance : light, hp, wp);
}
void renderPreviews(const fs::path &dir) {
    fs::create_directories(dir);
    Surface image(1080, 620);
    {
        Graphics g(image.bitmap.get());
        const auto &colors = glassPalette(false);
        g.Clear(colors.mid);
        drawHeading(g, L"原生版 · 七种任务栏样式", RectF(36, 18, 1000, 50), 25, colors.ink);
        drawText(g, L"模拟额度：5 小时 68%，每周 27%", RectF(36, 68, 1000, 30), 14,
                 colors.secondary);
        for (int i = 0; i < 7; ++i) {
            int y = 120 + i * 65;
            drawText(g, StyleNames[i], RectF(36, float(y), 240, 46), 16, colors.ink);
            Settings s;
            s.style = StyleIds[i];
            Quotas q;
            q.hour = WindowQuota{68, 0};
            q.week = WindowQuota{27, 0};
            auto l = widgetLayout(s, s.visibility(q), 144, 48);
            drawWidget(g, RectF(310, float(y + 3), float(l.width), float(l.height)), q, s, false, true);
            s.preferences[i].percent = false;
            drawWidget(g, RectF(700, float(y + 3), float(l.width), float(l.height)), q, s, false, true);
        }
    }
    image.save(dir / L"native-styles.png");
}
} // namespace quota
