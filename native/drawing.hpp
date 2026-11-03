#pragma once
#include "core.hpp"
namespace quota {
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
