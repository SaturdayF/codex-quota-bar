#include "app.hpp"
namespace quota {
static std::pair<std::wstring, std::wstring> deepSeekLabels(const DeepSeekState &state) {
    auto currency = state.currency();
    std::optional<int64_t> balance;
    for (auto &wallet : state.balances) if (wallet.currency == currency) balance = wallet.total;
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
    return {int(std::ceil(42 * d + text)), height, RectF()};
}
void paintDeepSeek(Surface &image, const DeepSeekState &state, const Settings &settings, bool hover) {
    Graphics g(image.bitmap.get());
    g.Clear(Color(1, 0, 0, 0));
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    bool light = lightTheme();
    float d = image.height / 32.f;
    bool darkInk = settings.backgroundOpacity >= 52 ? !settings.darkAppearance : light;
    Color ink = darkInk ? vibrancy::deep : vibrancy::ink;
    Color muted = darkInk ? Color(255, 65, 65, 67) : vibrancy::secondary;
    drawWidgetBackground(g, RectF(0, 0, float(image.width), float(image.height)), settings, light, hover);
    fillRound(g, RectF(5 * d, 5 * d, 24 * d, 22 * d), 5 * d, vibrancy::surface);
    drawUiText(g, L"DSH", RectF(5 * d, 5 * d, 24 * d, 22 * d), 11 * d, Color(255, 255, 255, 255), true, true);
    auto [balance, today] = deepSeekLabels(state);
    drawUiText(g, balance, RectF(37 * d, 0, image.width - 42 * d, 16 * d),
               12 * d, state.balanceError.empty() ? ink : muted, true);
    drawUiText(g, today,
               RectF(37 * d, 16 * d, image.width - 42 * d, 16 * d), 12 * d,
               state.costError.empty() ? ink : muted);
}
void Overlay::showDeepSeekDetails(const fs::path &capture) {
    auto &data = app->deepseek;
    float d = monitor.dpi / 96.f;
    int balanceRows = std::max(1, int(data.balances.size()));
    bool today = data.day == beijingDay(utcNow());
    int costRows = today && data.costAt ? std::max(1, int(data.costs.size())) : 1;
    float base = 172.f + 28 * (balanceRows + costRows);
    float available = (monitor.work.bottom - monitor.work.top) / d - 24;
    int visibleRows = std::max(0, int((available - base - 110) / 100));
    int totalRows = today ? int(data.models.size()) : 0;
    detailPage = std::clamp(detailPage, 0, std::max(0, totalRows - visibleRows));
    int rows = std::min(visibleRows, totalRows);
    int width = std::min(int(500 * d), int(monitor.work.right - monitor.work.left));
    int height = std::min(int((base + rows * 100 + 110) * d), int(monitor.work.bottom - monitor.work.top) - int(12 * d));
    int left = std::clamp<LONG>(x, monitor.work.left, std::max(monitor.work.left, monitor.work.right - width));
    int top = std::max(int(monitor.work.top), y - height - int(12 * d));
    if (!IsWindowVisible(details) || !detailFrost)
        detailFrost = captureFrost({left, top, left + width, top + height}, app->testing || app->preview || !capture.empty());
    const auto &colors = glassPalette(settings.darkAppearance);
    Surface image(width, height);
    {
        Graphics g(image.bitmap.get());
        g.Clear(Color(0, 0, 0, 0));
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        auto ink = colors.ink, muted = colors.secondary;
        RectF glass(1, 1, width - 2.f, height - 2.f);
        paintFrost(g, glass, 16 * d, colors, detailFrost.get(), glass);
        auto accent = colors.focus;
        auto line = [&](const std::wstring &text, float y, float size, Color color, bool bold = false) {
            drawUiText(g, text, RectF(20 * d, y * d, width - 40 * d, 28 * d), size * d, color, bold);
        };
        drawHeading(g, L"DeepSeekHarness · 当日消耗", RectF(20 * d, 14 * d, width - 40 * d, 28 * d), 20 * d, ink);
        line(beijingDate(utcNow()) + L" · 北京时间 00:00 起 · 账户汇总", 49, 13, muted);
        float yy = 88;
        for (int i = 0; i < balanceRows; ++i) {
            line(i < int(data.balances.size())
                     ? L"账户余额 " + moneyLabel(data.balances[i].total, data.balances[i].currency, 8) + L" " + wide(data.balances[i].currency)
                     : L"账户余额 --", yy, 16, ink, true);
            yy += 28;
        }
        if (today && data.costAt && !data.costs.empty()) {
            for (auto &cost : data.costs) {
                line(L"今日费用 " + moneyLabel(cost.second, cost.first, 8) + L" " + wide(cost.first), yy, 16, accent, true);
                yy += 28;
            }
        } else { line(L"今日费用 --", yy, 16, accent, true); yy += 28; }
        int64_t hit = 0, miss = 0, output = 0, requests = 0;
        if (today && data.tokensAt)
            for (auto &row : data.models) {
                hit += row.hit; miss += row.miss; output += row.output; requests += row.requests;
            }
        line(today && data.tokensAt
                 ? L"输入 " + std::to_wstring(hit + miss) + L" · 输出 " + std::to_wstring(output) + L" · 请求 " + std::to_wstring(requests)
                 : L"Token 与请求明细尚不可用", yy + 8, 13, muted);
        yy += 50;
        for (int i = 0; i < rows; ++i) {
            auto &row = data.models[detailPage + i];
            line(wide(row.name), yy, 15, ink, true);
            std::wstring costs;
            for (auto &cost : row.costs) {
                if (!costs.empty()) costs += L" / ";
                costs += moneyLabel(cost.second, cost.first, 8) + L" " + wide(cost.first);
            }
            line(L"费用 " + (costs.empty() ? L"--" : costs) + L"   ·   请求 " +
                     (data.tokensAt ? std::to_wstring(row.requests) : L"--"), yy + 27, 13, muted);
            line(data.tokensAt ? L"输入命中 " + std::to_wstring(row.hit) + L" / 未命中 " + std::to_wstring(row.miss) +
                                     L" · 输出 " + std::to_wstring(row.output)
                               : L"Token 明细尚不可用", yy + 52, 12, muted);
            yy += 100;
        }
        if (!totalRows)
            line(data.costAt && today ? L"今天暂无模型消耗明细" :
                 !data.costError.empty() || !data.tokensError.empty() ? L"明细暂不可用，详见下方读取状态" :
                 L"正在读取模型费用与 Token 明细", yy, 13, muted);
        else if (rows < totalRows)
            line(L"模型 " + std::to_wstring(detailPage + 1) + L"–" + std::to_wstring(detailPage + rows) +
                     L" / " + std::to_wstring(totalRows) + L" · 滚轮查看更多", yy, 12, muted);
        auto footer = height / d - 96;
        line(L"余额更新 " + (data.balanceAt ? timeLabel(data.balanceAt, false) : L"尚无") + L" · 账单更新 " +
                 (data.costAt && today ? timeLabel(data.costAt, false) : L"尚无"), footer, 12, muted);
        std::wstring error = !data.balanceError.empty() ? data.balanceError : !data.costError.empty() ? data.costError : data.tokensError;
        if (!today && data.costAt) error = L"日期已切换，正在读取今日账单";
        line(error.empty() ? L"平台账单可能延迟更新 · 右键打开 DeepSeek 相关设置" : error,
             footer + 30, 12, error.empty() ? muted : colors.warning);
        line(L"平台账户账单 · 不按模型单价估算费用", footer + 56, 12, muted);
    }
    if (!capture.empty()) image.save(capture);
    image.present(details, left, top);
    ShowWindow(details, SW_SHOWNOACTIVATE);
    SetWindowPos(details, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}
void Application::refreshDeepSeek() {
    { std::lock_guard<std::mutex> guard(mutex); deepSeekRefreshRequested = true; }
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
            previous.costs.clear(); previous.models.clear();
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
                if (!current.costAt && previous.costAt) {
                    current.costs = previous.costs; current.costAt = previous.costAt;
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
