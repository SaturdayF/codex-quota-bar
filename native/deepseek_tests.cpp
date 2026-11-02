#include "app.hpp"
namespace quota {
static json envelope(const json &data) { return {{"code", 0}, {"data", {{"biz_code", 0}, {"biz_data", data}}}}; }
static json balanceFixture() {
    return envelope({{"normal_wallets", {{{"currency", "CNY"}, {"balance", "88.4200"}},
                                         {{"currency", "USD"}, {"balance", "2.55"}}}}});
}
static json costFixture(int64_t day) {
    auto series = [&](const char *model, const char *cost) {
        return json{{"model", model}, {"api_key", {{"tracking_id", "fixture-only"}}},
                    {"buckets", {{{"time", day - 86400}, {"cost", "999"}},
                                 {{"time", day}, {"cost", cost}}, {{"time", day + 86400}, {"cost", "999"}}}}};
    };
    return envelope({{"start", day}, {"end", day + 86400}, {"bucket", "1d"},
                     {"data", {{{"currency", "CNY"}, {"series", {series("deepseek-v4-pro", "0.12"),
                                                                    series("deepseek-v4-pro", "0.23"),
                                                                    series("deepseek-v4-flash", "0.00001")}}},
                               {{"currency", "USD"}, {"series", {series("deepseek-v4-pro", "0.05")}}}}}});
}
static json tokenFixture(int64_t day) {
    auto series = [&](const char *model, int hit, int miss, int output, int requests) {
        return json{{"model", model}, {"api_key", {{"tracking_id", "fixture-only"}}},
                    {"buckets", {{{"time", day}, {"usage", {{"PROMPT_CACHE_HIT_TOKEN", std::to_string(hit)},
                                                             {"PROMPT_CACHE_MISS_TOKEN", std::to_string(miss)},
                                                             {"RESPONSE_TOKEN", std::to_string(output)},
                                                             {"REQUEST", std::to_string(requests)}}}}}}};
    };
    return envelope({{"start", day}, {"end", day + 86400}, {"bucket", "1d"},
                     {"series", {series("deepseek-v4-pro", 10, 20, 30, 3), series("deepseek-v4-pro", 1, 2, 3, 4),
                                  series("deepseek-v4-flash", 100, 200, 300, 2)}}});
}
static DeepSeekTransport fixtureTransport(int64_t day) {
    return [=](const std::wstring &, const std::wstring &path, const std::string &, std::atomic<bool> &) -> json {
        if (path == L"/api/v0/users/get_user_summary") return balanceFixture();
        if (path.find(L"/cost?") != std::wstring::npos) return costFixture(day);
        if (path.find(L"/amount?") != std::wstring::npos) return tokenFixture(day);
        throw std::runtime_error("Unexpected fixture endpoint");
    };
}
int deepSeekTests(std::ostream &log) {
    int checks = 0;
    auto check = [&](bool ok, const std::string &name) {
        log << (ok ? "PASS " : "FAIL ") << name << std::endl;
        if (!ok) throw std::runtime_error(name);
        ++checks;
    };
    auto rejected = [](const std::function<void()> &call) {
        try { call(); return false; } catch (const std::exception &) { return true; }
    };
    check(beijingDay(1704038399) == 1703952000 && beijingDay(1704038400) == 1704038400,
          "DeepSeek Beijing midnight boundary uses UTC+8");
    check(beijingDate(1704038399) == L"2023-12-31" && beijingDate(1704038400) == L"2024-01-01",
          "DeepSeek Beijing date independent of OS timezone");
    check(decimalMoney("0.00000001") == 1 && decimalMoney("-2.50") == -250000000 && decimalMoney(0) == 0,
          "DeepSeek exact fixed-point money including tiny and negative balances");
    for (auto bad : {"NaN", "1x", "", "1.000000001", "9223372036854775808", "--1"})
        check(rejected([&] { decimalMoney(bad); }), std::string("DeepSeek rejects invalid amount ") + bad);
    check(moneyLabel(1, "CNY") == L"¥<0.01" && moneyLabel({}, "USD") == L"--", "small spend is never rounded to fake zero");
    check(moneyLabel(1, "CNY", 8) == L"¥0.00000001" && moneyLabel(35001000, "CNY", 8) == L"¥0.35001",
          "detail amounts retain exact precision without redundant trailing zeros");
    check(validDeepSeekCredential("fixture-only") && !validDeepSeekCredential("Bearer fixture") &&
              !validDeepSeekCredential("key\r\nInjected: value"), "DeepSeek credentials reject header injection");
    auto config = Settings::fromJson({{"DeepSeekEnabled", true}, {"DeepSeekRefreshSeconds", 120}});
    check(normalizeDeepSeekCredential("  fixture-session\r\n") == "fixture-session" &&
              normalizeDeepSeekCredential(R"({"value":"fixture-session"})") == "fixture-session",
          "platform session import accepts raw token and browser storage JSON");
    check(normalizeDeepSeekCredential("Bearer fixture-session") == "fixture-session" &&
              !validDeepSeekCredential(normalizeDeepSeekCredential(R"({"value":"sk-fixture"})")),
          "formatted API key cannot bypass session-only credential validation");
    auto roundTrip = Settings::fromJson(config.toJson());
    check(roundTrip.deepSeekEnabled && roundTrip.deepSeekInterval == 120, "DeepSeek opt-in settings round trip");
    check(!Settings{}.deepSeekEnabled && Settings::fromJson({{"DeepSeekRefreshSeconds", 3}}).deepSeekInterval == 60,
          "DeepSeek disabled by default and refresh interval normalized");
    check(config.toJson().dump().find("fixture-only") == std::string::npos, "DeepSeek credentials are not serialized in settings");
    int64_t now = 1704067200, day = beijingDay(now);
    DeepSeekState data;
    data.day = day;
    auto preciseBalance = balanceFixture();
    preciseBalance["data"]["biz_data"]["normal_wallets"][0]["balance"] = "100.12345678";
    parseDeepSeekBalance(data, preciseBalance);
    check(data.balances[0].total == 10012345678LL && data.available, "platform balance parser preserves eight decimal places");
    auto malformed = balanceFixture();
    malformed["data"]["biz_data"]["normal_wallets"][0]["balance"] = "malformed";
    auto before = data.balances[0].total;
    check(rejected([&] { parseDeepSeekBalance(data, malformed); }) && data.balances[0].total == before,
          "invalid balance response does not partially replace verified data");
    parseDeepSeekBalance(data, balanceFixture());
    parseDeepSeekCosts(data, costFixture(day));
    parseDeepSeekTokens(data, tokenFixture(day));
    check(data.costs["CNY"] == 35001000 && data.costs["USD"] == 5000000,
          "daily costs aggregate keys and filter out-of-day buckets without mixing currencies");
    check(data.models.size() == 2 && data.models[0].hit == 11 && data.models[0].miss == 22 &&
              data.models[0].output == 33 && data.models[0].requests == 7,
          "daily token counts aggregate per model and keep requests separate");
    data.costAt = now;
    check(data.todayCost("CNY", now) == 35001000 && !data.todayCost("CNY", day + 86400),
          "yesterday cost can never be displayed as today's total");
    auto wrongWindow = costFixture(day);
    wrongWindow["data"]["biz_data"]["start"] = day - 86400;
    check(rejected([&] { parseDeepSeekCosts(data, wrongWindow); }), "wrong reporting range is rejected");
    auto missingCounters = tokenFixture(day);
    missingCounters["data"]["biz_data"]["series"][0]["buckets"][0]["usage"].erase("REQUEST");
    check(rejected([&] { parseDeepSeekTokens(data, missingCounters); }), "incomplete usage is unavailable rather than zero");
    auto expired = envelope(json::object()); expired["data"]["biz_code"] = 40003;
    check(rejected([&] { parseDeepSeekCosts(data, expired); }), "expired platform session rejected");
    DeepSeekState empty; empty.day = day;
    parseDeepSeekCosts(empty, envelope({{"start", day}, {"end", day + 86400}, {"bucket", "1d"}, {"data", json::array()}}));
    check(empty.costs["CNY"] == 0, "valid empty daily bill reports zero");
    std::atomic<bool> stop{false};
    int calls = 0;
    auto transport = fixtureTransport(day);
    auto full = readDeepSeek("platform-fixture", stop,
        [&](auto &host, auto &path, auto &credential, auto &cancel) {
            ++calls;
            check(host == L"platform.deepseek.com" && credential == "platform-fixture",
                  "platform mode scopes balance and bill to the same credential");
            if (path.find(L"?") != std::wstring::npos)
                check(path.find(L"tz=28800") != std::wstring::npos, "daily query explicitly carries Beijing timezone");
            return transport(host, path, credential, cancel);
        }, now);
    check(calls == 3 && full.balanceAt && full.costAt && full.tokensAt && full.costError.empty(),
          "DeepSeek balance cost and token integration round trip");
    calls = 0;
    auto missingSession = readDeepSeek("", stop,
        [&](auto &host, auto &path, auto &credential, auto &cancel) {
            ++calls; return transport(host, path, credential, cancel);
        }, now);
    check(calls == 0 && !missingSession.balanceAt && !missingSession.costAt && !missingSession.costError.empty(),
          "missing platform session performs no requests or API-key fallback");
    auto apiKeyRejected = readDeepSeek("sk-fixture-not-a-session", stop,
        [&](auto &host, auto &path, auto &credential, auto &cancel) {
            ++calls; return transport(host, path, credential, cancel);
        }, now);
    check(calls == 0 && !apiKeyRejected.balanceAt && !apiKeyRejected.costAt,
          "API key is rejected before storage or HTTP dispatch");
    check(rejected([&] { saveDeepSeekCredential("sk-fixture-not-a-session"); }),
          "API key is rejected before Windows Credential Manager writes");
    auto costFailure = readDeepSeek("platform-fixture", stop,
        [&](auto &host, auto &path, auto &credential, auto &cancel) -> json {
            if (path.find(L"/cost?") != std::wstring::npos) throw std::runtime_error("fixture failure");
            return transport(host, path, credential, cancel);
        }, now);
    check(costFailure.balanceAt && !costFailure.costAt && costFailure.tokensAt && !costFailure.costError.empty(),
          "independent failure preserves balance and token success");
    calls = 0; stop = true;
    readDeepSeek("platform-fixture", stop, [&](auto &, auto &, auto &, auto &) -> json { ++calls; return {}; }, now);
    check(calls == 0, "cancelled DeepSeek refresh performs no requests");
    return checks;
}
int deepSeekUiTests(const fs::path &dir) {
    fs::create_directories(dir);
    std::ofstream log(dir / L"deepseek-checks.txt");
    int checks = 0;
    auto check = [&](bool ok, const std::string &name) {
        log << (ok ? "PASS " : "FAIL ") << name << std::endl;
        if (!ok) throw std::runtime_error(name);
        ++checks;
    };
    auto pump = [&](const std::function<bool()> &ready) {
        auto deadline = GetTickCount64() + 4000;
        MSG msg{};
        while (!ready() && GetTickCount64() < deadline) {
            if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message != WM_QUIT) { TranslateMessage(&msg); DispatchMessageW(&msg); }
            } else Sleep(1);
        }
        return ready();
    };
    Application app(false, true);
    app.settings = Settings{};
    app.settings.deepSeekEnabled = true;
    app.state.active = true;
    app.state.error.clear(); app.state.updated = utcNow();
    app.state.q.plan = "prolite"; app.state.q.week = WindowQuota{27, utcNow() + 3600};
    std::atomic<bool> stop{false};
    auto now = utcNow();
    app.deepseek = readDeepSeek("fixture-only", stop, fixtureTransport(beijingDay(now)), now);
    app.rebuild();
    auto selected = selectedMonitors(app.settings);
    check(app.overlays.size() == selected.size() * 2, "Codex and DeepSeek coexist on each selected monitor");
    for (auto &overlay : app.overlays) if (overlay->kind == WidgetKind::DeepSeek) {
        RECT r{}; GetWindowRect(overlay->window, &r);
        check(GetParent(overlay->window) == taskbarWindow(overlay->monitor.handle), "DeepSeek embedded in correct taskbar");
        auto codex = std::find_if(app.overlays.begin(), app.overlays.end(), [&](auto &o) {
            return o->kind == WidgetKind::Codex && o->monitor.handle == overlay->monitor.handle;
        });
        check(codex != app.overlays.end() && r.left > (*codex)->x + (*codex)->layout.width, "DeepSeek does not overlap Codex default placement");
        POINT p{r.left + 5, r.top + 5}; overlay->tick(&p);
        overlay->hoverStart = GetTickCount64() - 300; overlay->tick(&p);
        check(IsWindowVisible(overlay->details), "DeepSeek hover opens daily detail card");
        overlay->showDetails(dir / L"deepseek-details.png");
        auto originalModels = app.deepseek.models;
        for (int i = 0; i < 10; ++i) {
            auto model = originalModels.front(); model.name = "fixture-model-" + std::to_string(i);
            app.deepseek.models.push_back(std::move(model));
        }
        overlay->showDetails(dir / L"deepseek-details-scroll.png");
        RECT details{}; GetWindowRect(overlay->details, &details);
        check(details.top >= overlay->monitor.work.top && details.bottom <= overlay->monitor.work.bottom,
              "many-model detail card stays within monitor work area");
        SendMessageW(overlay->details, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
        check(overlay->detailPage > 0, "DeepSeek detail wheel reveals additional models");
        app.deepseek.models = originalModels;
        overlay->detailPage = 0;
        Surface widget(overlay->layout.width, overlay->layout.height);
        paintDeepSeek(widget, app.deepseek, app.settings, true);
        widget.save(dir / L"deepseek-widget.png");
        SendMessageW(overlay->details, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
        overlay->hideDetails();
        SendMessageW(overlay->window, WM_RBUTTONUP, 0, 0);
        check(pump([&] { return app.settingsWindow && app.settingsWindow->page == 3; }), "DeepSeek right-click opens its settings page");
        break;
    }
    auto &settings = *app.settingsWindow;
    check(GetDlgItem(settings.content, 261) == nullptr, "DeepSeek has no API-key input control");
    check(GetWindowLongPtrW(settings.deepSeekToken, GWL_STYLE) & ES_PASSWORD, "platform session input is masked");
    settings.capture(dir / L"deepseek-settings.png");
    settings.command(260, BN_CLICKED);
    check(!settings.draft.deepSeekEnabled, "DeepSeek setting toggles off");
    settings.command(260, BN_CLICKED);
    settings.command(265, BN_CLICKED);
    check(settings.clearDeepSeekCredentials, "credential deletion is staged until Save");
    auto nextState = app.state; nextState.active = false;
    app.apply(nextState);
    check(app.overlays.size() == selected.size() && app.overlays.front()->kind == WidgetKind::DeepSeek,
          "DeepSeek remains visible after Codex exits");
    auto before = app.deepseek.balanceAt;
    app.applyDeepSeek({app.generation - 1, DeepSeekState{}});
    check(app.deepseek.balanceAt == before, "superseded credential-generation response is ignored");
    app.deepSeekProbe = [&](std::atomic<bool> &cancel) {
        return readDeepSeek("fixture-only", cancel, fixtureTransport(beijingDay(utcNow())));
    };
    app.deepseek = DeepSeekState{};
    app.deepSeekWorker = std::thread(&Application::runDeepSeekWorker, &app);
    app.refreshDeepSeek();
    check(pump([&] { return app.deepseek.costAt > 0 && app.deepseek.costError.empty(); }), "DeepSeek independent worker publishes successful result");
    auto disabled = app.settings; disabled.deepSeekEnabled = false;
    app.saved(disabled);
    check(app.overlays.empty(), "disabling DeepSeek removes independent widget");
    app.applyDeepSeek({app.generation - 1, DeepSeekState{}});
    check(!app.deepseek.costAt, "disabled provider cannot be repopulated by late response");
    app.stopping = true; app.wake.notify_all(); app.deepSeekWorker.join();
    log << checks << " DeepSeek UI checks passed.\n";
    return checks;
}
} // namespace quota
