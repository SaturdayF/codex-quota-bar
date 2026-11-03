#pragma once
#include "core.hpp"
namespace quota {
namespace vibrancy {
inline const Color deep(255, 28, 28, 30), mid(255, 44, 44, 46), surface(255, 58, 58, 60);
inline const Color ink(255, 244, 244, 245), secondary(255, 193, 193, 195), muted(255, 158, 158, 160);
inline const Color border(25, 255, 255, 255), hover(18, 255, 255, 255), active(26, 255, 255, 255);
inline const Color focus(255, 163, 190, 255), warning(255, 235, 190, 142);
constexpr COLORREF inputBrush = RGB(28, 28, 30);
}
void drawPanel(Graphics &g, RectF bounds, float radius, Color fill);
void drawHeading(Graphics &g, const std::wstring &text, RectF bounds, float size);
struct Surface {
    int width, height;
    HDC dc = nullptr;
    HBITMAP dib = nullptr;
    HGDIOBJ old = nullptr;
    BYTE *pixels = nullptr;
    std::unique_ptr<Bitmap> bitmap;
    Surface(int w, int h);
    ~Surface();
    Surface(const Surface &) = delete;
    void present(HWND window, int x, int y, bool move = true);
    void save(const fs::path &file);
};
void roundPath(GraphicsPath &path, RectF area, float radius);
void fillRound(Graphics &g, RectF area, float radius, Color c);
void drawText(Graphics &g, const std::wstring &text, RectF area, float size, Color c, bool bold = false,
              bool centered = false, bool glyph = false);
float uiTextWidth(Graphics &g, const std::wstring &text, float size, bool bold);
void drawUiText(Graphics &g, const std::wstring &text, RectF area, float size, Color c,
                bool bold = false, bool centered = false);
void drawWidget(Graphics &g, RectF area, const Quotas &q, const Settings &s, bool stale, bool light,
                double hourProgress = 1, double weekProgress = 1);
struct Layout {
    int width = 1, height = 1;
    RectF drawing;
};
Layout widgetLayout(const Settings &s, QuotaVisibility visible, int dpi, int available);
void drawWidgetBackground(Graphics &g, RectF bounds, const Settings &settings, bool light, bool hover);
void paintWidget(Surface &surface, const Layout &layout, const State &state, const Settings &settings,
                 bool hover, double h = 1, double w = 1);
void renderPreviews(const fs::path &dir);
} // namespace quota
