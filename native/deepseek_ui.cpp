#include "app.hpp"
namespace quota {
std::optional<int64_t> deepSeekPrimaryBalance(const DeepSeekState &state) {
    auto currency = state.currency();
    for (auto &wallet : state.balances)
        if (wallet.currency == currency) return wallet.total;
    return {};
}
static std::pair<std::wstring, std::wstring> deepSeekLabels(const DeepSeekState &state,
                                                            std::optional<int64_t> balanceOverride = std::nullopt) {
    auto currency = state.currency();
    auto balance = balanceOverride ? balanceOverride : deepSeekPrimaryBalance(state);
    std::string billing = state.costs.count(currency) ? currency : state.costs.empty() ? currency : state.costs.begin()->first;
    return {L"余额 " + moneyLabel(balance, currency), L"今日 " + moneyLabel(state.todayCost(billing), billing)};
}
Layout deepSeekLayout(const DeepSeekState &state, const Settings &settings, int dpi, int available) {
    double scale = std::min(settings.scale * dpi / 96., std::max(8, available - 2) / 32.);
    int height = std::max(1, int(32 * scale));
    float d = height / 32.f;
    Bitmap sample(1, 1);
    Graphics graphics(&sample);
    auto [balance, today] = deepSeekLabels(state);
    float text = std::max(uiTextWidth(graphics, balance, 12 * d, true),
                          uiTextWidth(graphics, today, 12 * d, false));
    return {int(std::ceil((settings.deepSeekShowBadge ? 42 : 10) * d + text)), height, RectF()};
}
void paintDeepSeek(Surface &image, const DeepSeekState &state, const Settings &settings, bool hover,
                   std::optional<int64_t> balanceOverride) {
    Graphics g(image.bitmap.get());
    g.Clear(Color(1, 0, 0, 0));
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    bool light = lightTheme();
    float d = image.height / 32.f;
    bool darkInk = settings.backgroundOpacity >= 52 ? !settings.darkAppearance : light;
    Color ink = darkInk ? vibrancy::deep : vibrancy::ink;
    Color muted = darkInk ? Color(255, 65, 65, 67) : vibrancy::secondary;
    drawWidgetBackground(g, RectF(0, 0, float(image.width), float(image.height)), settings, light, hover);
    if (settings.deepSeekShowBadge) {
        fillRound(g, RectF(5 * d, 5 * d, 24 * d, 22 * d), 5 * d, vibrancy::surface);
        drawUiText(g, L"DSH", RectF(5 * d, 5 * d, 24 * d, 22 * d), 11 * d, Color(255, 255, 255, 255), true, true);
    }
    float left = (settings.deepSeekShowBadge ? 37 : 5) * d;
    auto [balance, today] = deepSeekLabels(state, balanceOverride);
    drawUiText(g, balance, RectF(left, 0, image.width - left - 5 * d, 16 * d),
               12 * d, state.balanceError.empty() ? ink : muted, true);
    drawUiText(g, today,
               RectF(left, 16 * d, image.width - left - 5 * d, 16 * d), 12 * d,
               state.costError.empty() ? ink : muted);
}
// Renders a money value with a larger integer part while the decimals keep the
// body size, e.g. a big "3" before ".2521095" in the account summary.
static void moneyLine(Graphics &g, const std::wstring &prefix, const std::wstring &label,
                      const std::wstring &suffix, float y, float d, int width, float normal, float big,
                      Color color) {
    struct Segment {
        std::wstring text;
        float size;
    };
    std::vector<Segment> segments;
    segments.push_back({prefix, normal});
    auto digit = label.find_first_of(L"0123456789");
    if (digit == std::wstring::npos || label.find(L'<') != std::wstring::npos ||
        label.find(L'>') != std::wstring::npos) {
        segments.push_back({label, normal});
    } else {
        std::wstring number = label.substr(digit);
        auto dot = number.find(L'.');
        segments.push_back({label.substr(0, digit), normal});
        segments.push_back({dot == std::wstring::npos ? number : number.substr(0, dot), big});
        if (dot != std::wstring::npos) segments.push_back({number.substr(dot), normal});
    }
    segments.push_back({suffix, normal});
    float x = 20 * d;
    for (size_t i = 0; i < segments.size(); ++i) {
        const auto &seg = segments[i];
        if (seg.text.empty()) continue;
        // Bottom aligned so every baseline matches the large integer's.
        float inset = 0.21f * (big - seg.size) * d;
        drawUiText(g, seg.text, RectF(x, y * d, std::max(1.f, width - 20 * d - x), 28 * d - inset), seg.size,
                   color, true, false, true);
        // Advance by the tight typographic width so the decimals sit next to the
        // integer instead of a padded 1/6-em gap away.
        x += uiTextWidth(g, seg.text, seg.size, true, true) + 0.5f;
    }
}
// Three-digit grouping for full token counts, e.g. 2,766,081.
static std::wstring groupDigits(int64_t value) {
    std::wstring text = std::to_wstring(value);
    std::wstring out;
    out.reserve(text.size() + text.size() / 3);
    int count = 0;
    for (auto it = text.rbegin(); it != text.rend(); ++it) {
        if (count && count % 3 == 0) out += L',';
        out += *it;
        ++count;
    }
    std::reverse(out.begin(), out.end());
    return out;
}
// Floors a token count into K/M/B/T units, e.g. 2766081 -> 2M.
static std::wstring compactCount(int64_t value) {
    if (value >= 1000000000000LL) return std::to_wstring(value / 1000000000000LL) + L"T";
    if (value >= 1000000000LL) return std::to_wstring(value / 1000000000LL) + L"B";
    if (value >= 1000000LL) return std::to_wstring(value / 1000000LL) + L"M";
    if (value >= 1000LL) return std::to_wstring(value / 1000LL) + L"K";
    return std::to_wstring(value);
}
// Short label for an API-key tracking id.
static std::wstring shortKey(const std::string &key) {
    auto text = wide(key);
    if (text.size() > 14) text = text.substr(0, 13) + L"…";
    return text;
}
void Overlay::showDeepSeekDetails(const fs::path &capture) {
    auto &data = app->deepseek;
    const DeepSeekKeyStats *keyStats = nullptr;
    if (!app->deepSeekKeyFilter.empty()) {
        auto it = data.byKey.find(app->deepSeekKeyFilter);
        if (it != data.byKey.end()) keyStats = &it->second;
    }
    const auto &models = keyStats ? keyStats->models : data.models;
    const auto &costs = keyStats ? keyStats->costs : data.costs;
    const auto &hourly = keyStats ? keyStats->hourly : data.hourly;
    const auto &hourlyTokens = keyStats ? keyStats->hourlyTokens : data.hourlyTokens;
    float d = monitor.dpi / 96.f;
    int balanceRows = std::max(1, int(data.balances.size()));
    bool today = data.day == beijingDay(utcNow());
    int costRows = today && data.costAt ? std::max(1, int(costs.size())) : 1;
    bool showChart = today && data.costAt;
    float chartSpace = showChart ? 100.f : 0.f;
    // Header, balances, cost lines and the token summary are fixed; the hourly
    // chart now sits at the bottom so it absorbs the former empty gap.
    float contentTop = 138.f + 28 * (balanceRows + costRows);
    float available = (monitor.work.bottom - monitor.work.top) / d - 24;
    int visibleRows = std::max(0, int((available - contentTop - chartSpace - 110) / 100));
    int totalRows = today ? int(models.size()) : 0;
    detailPage = std::clamp(detailPage, 0, std::max(0, totalRows - visibleRows));
    int rows = std::min(visibleRows, totalRows);
    bool statusLine = !totalRows || rows < totalRows;
    int width = std::min(int(500 * d), int(monitor.work.right - monitor.work.left));
    int height = std::min(int((contentTop + rows * 100 + (statusLine ? 28 : 0) + chartSpace + 82) * d),
                          int(monitor.work.bottom - monitor.work.top) - int(12 * d));
    int left = std::clamp<LONG>(x, monitor.work.left, std::max(monitor.work.left, monitor.work.right - width));
    int top = std::max(int(monitor.work.top), y - height - int(12 * d));
    bool simulated = app->testing || app->preview || !capture.empty();
    if (simulated && !detailFrost) detailFrost = previewFrost(width, height);
    detailBackdrop = configureBackdrop(details, settings.darkAppearance);
    const auto &colors = glassPalette(settings.darkAppearance);
    auto rendered = std::make_unique<Surface>(width, height);
    auto &image = *rendered;
    {
        Graphics g(image.bitmap.get());
        g.Clear(Color(0, 0, 0, 0));
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        auto ink = colors.ink, muted = colors.secondary;
        RectF glass(1, 1, width - 2.f, height - 2.f);
        if (simulated) paintFrost(g, glass, 16 * d, colors, detailFrost.get(), glass);
        else paintBackdrop(g, colors, detailBackdrop);
        auto accent = colors.focus;
        auto line = [&](const std::wstring &text, float y, float size, Color color, bool bold = false) {
            drawUiText(g, text, RectF(20 * d, y * d, width - 40 * d, 28 * d), size * d, color, bold);
        };
        drawHeading(g, L"DeepSeek · 当日消耗", RectF(20 * d, 14 * d, width - 40 * d, 28 * d), 20 * d, ink);
        line(beijingDate(utcNow()) + L" · 北京时间 00:00 起 · 账户汇总", 49, 13, muted);
        float yy = 88;
        for (int i = 0; i < balanceRows; ++i) {
            if (i < int(data.balances.size()))
                moneyLine(g, L"账户余额 ", moneyLabel(data.balances[i].total, data.balances[i].currency, 8),
                          L" " + wide(data.balances[i].currency), yy, d, width, 16, 24, ink);
            else
                line(L"账户余额 --", yy, 16, ink, true);
            yy += 28;
        }
        if (today && data.costAt && !costs.empty()) {
            for (auto &cost : costs) {
                moneyLine(g, L"今日费用 ", moneyLabel(cost.second, cost.first, 2), L" " + wide(cost.first),
                          yy, d, width, 16, 24, accent);
                yy += 28;
            }
        } else { line(L"今日费用 --", yy, 16, accent, true); yy += 28; }
        int64_t hit = 0, miss = 0, output = 0, requests = 0;
        if (today && data.tokensAt)
            for (auto &row : models) {
                hit += row.hit; miss += row.miss; output += row.output; requests += row.requests;
            }
        line(today && data.tokensAt
                 ? L"输入命中 " + groupDigits(hit) + L" / 未命中 " + groupDigits(miss) + L" · 输出 " +
                       groupDigits(output) + L" · 请求 " + groupDigits(requests)
                 : L"Token 与请求明细尚不可用", yy + 8, 13, muted);
        yy += 50;
        for (int i = 0; i < rows; ++i) {
            auto &row = models[detailPage + i];
            line(wide(row.name), yy, 15, ink, true);
            std::wstring costs;
            for (auto &cost : row.costs) {
                if (!costs.empty()) costs += L" / ";
                costs += moneyLabel(cost.second, cost.first, 2) + L" " + wide(cost.first);
            }
            line(L"费用 " + (costs.empty() ? L"--" : costs) + L"   ·   请求 " +
                     (data.tokensAt ? groupDigits(row.requests) : L"--"), yy + 27, 13, muted);
            line(data.tokensAt ? L"输入命中 " + groupDigits(row.hit) + L" / 未命中 " + groupDigits(row.miss) +
                                     L" · 输出 " + groupDigits(row.output)
                               : L"Token 明细尚不可用", yy + 52, 12, muted);
            yy += 100;
        }
        bool drewStatus = false;
        if (!totalRows) {
            line(data.costAt && today ? L"今天暂无模型消耗明细" :
                 !data.costError.empty() || !data.tokensError.empty() ? L"明细暂不可用，详见下方读取状态" :
                 L"正在读取模型费用与 Token 明细", yy, 13, muted);
            drewStatus = true;
        } else if (rows < totalRows) {
            line(L"模型 " + std::to_wstring(detailPage + 1) + L"–" + std::to_wstring(detailPage + rows) +
                     L" / " + std::to_wstring(totalRows) + L" · 滚轮查看更多", yy, 12, muted);
            drewStatus = true;
        }
        if (drewStatus) yy += 28;
        if (showChart) {
            // 24-hour Beijing spend bars drawn last so they fill the space above
            // the footer instead of leaving a large empty strip.
            auto billing = data.billingCurrency();
            auto found = hourly.find(billing);
            const std::array<int64_t, 24> *hours = found == hourly.end() ? nullptr : &found->second;
            int64_t peak = 0;
            if (hours)
                for (auto value : *hours) peak = std::max(peak, value);
            bool dark = colors.ink.GetR() > 128;
            // Cohesive blue gradient plus a soft top gloss; every bar shares it.
            Color topColor = dark ? Color(255, 146, 188, 246) : Color(255, 124, 170, 238);
            Color bottomColor = dark ? Color(255, 84, 132, 216) : Color(255, 66, 116, 204);
            float chartLeft = 20 * d, chartRight = width - 20 * d;
            float baseline = (yy + 70) * d;
            float barGap = 3 * d;
            float barW = std::max(1.f, (chartRight - chartLeft - 23 * barGap) / 24);
            float barTop = (yy + 2) * d;
            chartVisible = true;
            chartBilling = billing;
            chartLeftPx = chartLeft;
            chartTopPx = barTop;
            chartBaselinePx = baseline;
            chartBarW = barW;
            chartBarGap = barGap;
            if (hours) chartValues = *hours;
            else chartValues.fill(0);
            Pen rule(dark ? Color(56, 255, 255, 255) : Color(56, 40, 70, 120), 1);
            g.DrawLine(&rule, chartLeft, baseline, chartRight, baseline);
            if (peak > 0 && hours) {
                for (int h = 0; h < 24; ++h) {
                    auto value = (*hours)[size_t(h)];
                    if (value <= 0) continue;
                    float barHeight = std::max(2 * d, float(value) / peak * (baseline - barTop));
                    RectF bar(chartLeft + h * (barW + barGap), baseline - barHeight, barW, barHeight);
                    float radius = std::min(bar.Width * .5f, 4 * d);
                    GraphicsPath path;
                    path.StartFigure();
                    path.AddLine(bar.X, bar.GetBottom(), bar.X, bar.Y + radius);
                    path.AddArc(RectF(bar.X, bar.Y, 2 * radius, 2 * radius), 180, 90);
                    path.AddLine(bar.X + radius, bar.Y, bar.GetRight() - radius, bar.Y);
                    path.AddArc(RectF(bar.GetRight() - 2 * radius, bar.Y, 2 * radius, 2 * radius), 270, 90);
                    path.AddLine(bar.GetRight(), bar.Y + radius, bar.GetRight(), bar.GetBottom());
                    path.CloseFigure();
                    // Vertical gradient stays within one blue hue for every bar.
                    LinearGradientBrush fill(PointF(bar.X, bar.Y), PointF(bar.X, bar.GetBottom()),
                                             topColor, bottomColor);
                    g.FillPath(&fill, &path);
                    // Smooth full-height gloss fade so there is no hard colour
                    // seam where a half-height highlight band would end.
                    auto saved = g.Save();
                    g.SetClip(&path);
                    LinearGradientBrush gloss(PointF(bar.X, bar.Y), PointF(bar.X, bar.GetBottom()),
                                              Color(66, 255, 255, 255), Color(0, 255, 255, 255));
                    g.FillRectangle(&gloss, bar);
                    g.Restore(saved);
                    Pen rim(Color(72, 255, 255, 255), 1);
                    g.DrawPath(&rim, &path);
                }
            }
            for (int h : {0, 6, 12, 18, 23}) {
                std::wstring label = std::to_wstring(h) + L"h";
                drawUiText(g, label, RectF(chartLeft + h * (barW + barGap) - 4 * d, (yy + 71) * d, 22 * d, 13 * d),
                           11 * d, muted);
            }
            drawUiText(g, peak > 0 ? L"每小时消耗 · 峰值 " + moneyLabel(peak, billing, 2) : L"今日暂无分时消耗",
                       RectF(20 * d, (yy + 86) * d, width - 40 * d, 14 * d), 11 * d, muted);
            if (peak > 0 && chartHover >= 0 && chartHover < 24) {
                // Hovering a bar reveals that hour's spend and token usage.
                int h = chartHover;
                float slotX = chartLeft + h * (barW + barGap);
                SolidBrush light(dark ? Color(44, 255, 255, 255) : Color(24, 60, 120, 220));
                g.FillRectangle(&light, RectF(slotX - 1 * d, barTop, barW + 2 * d, baseline - barTop));
                std::wstring caption = std::to_wstring(h) + L":00–" + std::to_wstring(h + 1) + L":00";
                auto &usage = hourlyTokens[size_t(h)];
                float tw = 200 * d, th = 74 * d;
                float tx = std::clamp(slotX + barW / 2 - tw / 2, 6 * d, width - tw - 6 * d);
                float ty = std::max(6 * d, barTop - th - 8 * d);
                fillRound(g, RectF(tx, ty, tw, th), 9 * d, dark ? Color(240, 38, 42, 50) : Color(240, 252, 253, 255));
                GraphicsPath border;
                roundPath(border, RectF(tx, ty, tw, th), 9 * d);
                Pen edge(colors.border, 1);
                g.DrawPath(&edge, &border);
                drawUiText(g, caption, RectF(tx, ty + 4 * d, tw, 18 * d), 11 * d, muted, false, true);
                drawUiText(g, moneyLabel(chartValues[size_t(h)], billing, 2), RectF(tx, ty + 21 * d, tw, 24 * d),
                           14 * d, ink, true, true);
                drawUiText(g, L"输入命中 " + compactCount(usage.hit) + L" / 未命中 " + compactCount(usage.miss),
                           RectF(tx, ty + 48 * d, tw, 20 * d), 10 * d, muted, false, true);
            }
        } else {
            chartVisible = false;
            chartHover = -1;
        }
        auto footer = height / d - 70;
        line(L"余额更新 " + (data.balanceAt ? timeLabel(data.balanceAt, false) : L"尚无") + L" · 账单更新 " +
                 (data.costAt && today ? timeLabel(data.costAt, false) : L"尚无"), footer, 12, muted);
        std::wstring error = !data.balanceError.empty() ? data.balanceError : !data.costError.empty() ? data.costError : data.tokensError;
        if (!today && data.costAt) error = L"日期已切换，正在读取今日账单";
        line(error.empty() ? L"平台账单可能延迟更新 · 右键打开 DeepSeek 相关设置" : error,
             footer + 30, 12, error.empty() ? muted : colors.warning);
        // API-key filter dropdown, drawn last so its menu overlays the content.
        std::vector<std::string> keys;
        for (auto &entry : data.byKey) keys.push_back(entry.first);
        ddVisible = keys.size() > 1;
        if (ddVisible) {
            bool dark = colors.ink.GetR() > 128;
            ddX = width - 20 * d - 150 * d;
            ddY = 14 * d;
            ddW = 150 * d;
            ddH = 26 * d;
            ddItemH = 24 * d;
            ddKeys = keys;
            ddCount = int(keys.size()) + 1;
            fillRound(g, RectF(ddX, ddY, ddW, ddH), 8 * d,
                      dark ? Color(220, 48, 52, 60) : Color(220, 246, 248, 252));
            Pen edge(colors.border, 1);
            GraphicsPath button;
            roundPath(button, RectF(ddX, ddY, ddW, ddH), 8 * d);
            g.DrawPath(&edge, &button);
            std::wstring label = app->deepSeekKeyFilter.empty() ? L"全部 API Key"
                                                               : shortKey(app->deepSeekKeyFilter);
            drawUiText(g, label, RectF(ddX + 10 * d, ddY, ddW - 34 * d, ddH), 11 * d, ink, false, false);
            drawUiText(g, L"▾", RectF(ddX + ddW - 24 * d, ddY, 18 * d, ddH), 12 * d, muted, false, true);
            if (keyMenuOpen) {
                float menuY = ddY + ddH + 2 * d;
                float menuH = ddItemH * float(ddCount);
                fillRound(g, RectF(ddX, menuY, ddW, menuH), 8 * d,
                          dark ? Color(245, 40, 44, 52) : Color(245, 252, 253, 255));
                GraphicsPath menu;
                roundPath(menu, RectF(ddX, menuY, ddW, menuH), 8 * d);
                g.DrawPath(&edge, &menu);
                for (int i = 0; i < ddCount; ++i) {
                    std::wstring text = i == 0 ? L"全部 API Key" : shortKey(keys[size_t(i - 1)]);
                    bool current = i == 0 ? app->deepSeekKeyFilter.empty()
                                          : (!app->deepSeekKeyFilter.empty() &&
                                             keys[size_t(i - 1)] == app->deepSeekKeyFilter);
                    drawUiText(g, text, RectF(ddX + 10 * d, menuY + i * ddItemH, ddW - 20 * d, ddItemH), 10.5f * d,
                               current ? accent : muted, current, false);
                }
            }
        } else {
            keyMenuOpen = 0;
        }
    }
    if (!capture.empty()) image.save(capture);
    presentDetails(std::move(rendered), left, top);
}
void Application::refreshDeepSeek(bool reveal) {
    { std::lock_guard<std::mutex> guard(mutex); deepSeekRefreshRequested = true; }
    if (reveal) deepSeekRevealAt = int64_t(GetTickCount64());
    wake.notify_all();
}
void Application::checkHarness(bool force) {
    bool active = settings.deepSeekAlwaysShow || (settings.deepSeekEnabled && harnessProbe());
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (active == harnessActive && !force) return;
        *deepSeekSessionStop = true;
        deepSeekSessionStop = std::make_shared<std::atomic<bool>>(!active);
        harnessActive = active;
        ++harnessSession;
        deepSeekRefreshRequested = active;
    }
    deepseek = DeepSeekState{};
    wake.notify_all();
    rebuild();
}
void Application::applyDeepSeek(const DeepSeekUpdate &update) {
    if (update.generation != generation || update.session != harnessSession || !harnessActive) return;
    deepseek = update.state;
    layoutWidgets();
    for (auto &overlay : overlays)
        if (overlay->kind == WidgetKind::DeepSeek) {
            overlay->render();
            if (IsWindowVisible(overlay->details)) overlay->showDeepSeekDetails();
        }
}
void Application::runDeepSeekWorker() {
    int seen = -1, session = -1, failures = 0;
    Settings cfg;
    DeepSeekState previous;
    ULONGLONG next = 0;
    auto emit = [&](const DeepSeekState &value) {
        auto update = new DeepSeekUpdate{seen, value, session};
        if (!PostMessageW(controller, DeepSeekMessage, 0, reinterpret_cast<LPARAM>(update))) delete update;
    };
    while (!stopping) {
        bool force = false;
        bool active = false;
        std::shared_ptr<std::atomic<bool>> cancel;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (seen != generation || session != harnessSession) {
                cfg = settings; seen = generation; session = harnessSession;
                next = 0; failures = 0; previous = DeepSeekState{};
            }
            active = harnessActive;
            cancel = deepSeekSessionStop;
            force = deepSeekRefreshRequested;
            deepSeekRefreshRequested = false;
        }
        auto day = beijingDay(utcNow());
        if (active && previous.day != day) {
            previous.day = day;
            previous.costAt = previous.tokensAt = 0;
            previous.costs.clear(); previous.hourly.clear(); previous.models.clear();
            previous.hourlyTokens.fill(DeepSeekHourUsage{});
            previous.byKey.clear();
            previous.costError = L"正在读取今日账单";
            emit(previous);
            next = 0;
        }
        if (active && !*cancel && (force || GetTickCount64() >= next)) {
            DeepSeekState current;
            try { current = deepSeekProbe(*cancel); }
            catch (...) {
                current.day = day;
                current.balanceError = current.costError = L"DeepSeek 读取失败，请检查网络与凭证";
            }
            if (stopping) break;
            if (*cancel) continue;
            if (current.day == previous.day) {
                if (!current.balanceAt) { current.balances = previous.balances; current.balanceAt = previous.balanceAt; }
                if (!current.tokensAt && previous.tokensAt) current.hourlyTokens = previous.hourlyTokens;
                if ((!current.costAt && previous.costAt) || (!current.tokensAt && previous.tokensAt))
                    current.byKey = previous.byKey;
                if (!current.costAt && previous.costAt) {
                    current.costs = previous.costs; current.hourly = previous.hourly; current.costAt = previous.costAt;
                    for (auto &old : previous.models) {
                        auto found = std::find_if(current.models.begin(), current.models.end(), [&](auto &r) { return r.name == old.name; });
                        if (found == current.models.end()) {
                            DeepSeekModel retained; retained.name = old.name; retained.costs = old.costs;
                            current.models.push_back(std::move(retained));
                        }
                        else found->costs = old.costs;
                    }
                }
            }
            previous = current;
            emit(current);
            bool ok = current.balanceError.empty() && current.costError.empty();
            failures = ok ? 0 : std::min(4, failures + 1);
            next = GetTickCount64() + std::min(600000, cfg.deepSeekInterval * 1000 * (1 << std::max(0, failures - 1)));
        }
        std::unique_lock<std::mutex> lock(mutex);
        wake.wait_for(lock, std::chrono::milliseconds(250), [&] {
            return stopping || deepSeekRefreshRequested || seen != generation || session != harnessSession;
        });
    }
}
} // namespace quota
