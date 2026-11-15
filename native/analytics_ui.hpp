#pragma once
#include "analytics.hpp"
#include "drawing.hpp"
namespace quota {
struct Application;
enum AnalyticsControl {
    UsageSeven = 500, UsageThirty, UsageCost, UsageTokens, UsageDay, UsageHour,
    UsageModels, UsageProjects, UsageSessions, UsageAccounts, UsageRefresh, UsageQuota, UsageClear, UsagePrevious, UsageNext
};
struct AnalyticsHit { RectF rect; int action = 0; int64_t value = 0; std::string key; };
struct UsageGroupRow { std::string key; std::wstring title, subtitle; UsageTotal total; };
struct UsagePlotBin { int64_t at = 0; std::wstring label; UsageTotal total; };
struct UsageView {
    int days = 7, group = 0, hovered = -1, keyboard = -1, groupPage = 0;
    int64_t selectedDay = 0;
    bool tokens = false, hourly = false, dirty = true;
    std::string project;
    UsageTotal total;
    std::vector<UsagePlotBin> bins;
    std::vector<UsageGroupRow> groups;
    std::map<int64_t, int64_t> heat;
    int64_t coverageStart = 0;
    std::vector<AnalyticsHit> hits;
    void prepare(const UsageReport &report);
};
struct QuotaCardView {
    bool history = false;
    int selected = 0, extraScroll = 0, hoveredDay = -1;
    std::vector<AnalyticsHit> hits;
    std::vector<NamedQuota> windows;
};
bool quotaCardHover(QuotaCardView &view, float x, float y);
int usageRowsEnd(const UsageView &view);
int usagePageHeight(const UsageView &view, const UsageReport &report);
int quotaCardHeight(const State &state, const Settings &settings, int maximum, bool history = false);
void paintQuotaCard(Graphics &g, float width, float height, const State &state, const Settings &settings,
                    const QuotaHistory &history, QuotaCardView &view, bool simulated, const UsageReport *usage = nullptr);
bool quotaCardAction(QuotaCardView &view, int action, HWND window, const State &state);
struct QuotaPopup {
    Application *app;
    HWND window = nullptr;
    Settings settings;
    QuotaCardView view;
    BackdropMode backdrop = BackdropMode::Solid;
    std::unique_ptr<Surface> frost;
    int dpi = 96;
    bool simulated = false;
    ULONGLONG lastInside = 0, lastPaint = 0;
    QuotaPopup(Application *owner, HWND parent, const Settings &appearance);
    ~QuotaPopup();
    void reveal();
    void draw(HDC dc);
    void capture(const fs::path &file);
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
};
int analyticsUiTests(const fs::path &directory);
} // namespace quota
