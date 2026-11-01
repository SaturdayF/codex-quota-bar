#include "app.hpp"
namespace quota {
int selfTests(const fs::path &output) {
    std::ofstream log(output);
    int checks = 0;
    auto check = [&](bool value, const std::string &title) {
        if (!value) {
            log << "FAIL " << title << std::endl;
            throw std::runtime_error("Self test: " + title);
        }
        ++checks;
        log << "PASS " << title << std::endl;
    };
    auto quota = [](double used, int mins, int64_t reset = 1000) -> json {
        return {{"usedPercent", used}, {"windowDurationMins", mins}, {"resetsAt", reset}};
    };
    auto q = Quotas::parse(
        {{"rateLimits",
          {{"limitId", "codex"}, {"primary", quota(32, 300)}, {"secondary", quota(73, 10080)}}}});
    check(q.hour && q.hour->remaining == 68 && q.week && q.week->remaining == 27,
          "300 / 10080 minute windows");
    check(!Quotas::parse({{"rateLimits", {{"limitId", "other"}, {"primary", quota(32, 300)}}}}).hour,
          "model bucket not substituted");
    check(!Quotas::parse({{"rateLimitsByLimitId", {{"other", {{"primary", quota(32, 300)}}}}},
                          {"rateLimits", {{"primary", quota(10, 300)}}}})
               .hour,
          "bucket map without codex does not fall back");
    q = Quotas::parse(
        {{"rateLimits",
          {{"primary", quota(-20, 300)}, {"secondary", quota(120, 10080)}, {"planType", "Pro"}}}});
    check(q.hour->remaining == 100 && q.week->remaining == 0 && q.weekOnly(),
          "clamp and case insensitive Pro");
    check(
        !Quotas::parse({{"rateLimits", {{"primary", {{"usedPercent", "32"}, {"windowDurationMins", 300}}}}}})
             .hour,
        "malformed data stays missing");
    check(!Quotas::parse({{"rateLimits", {{"primary", quota(0, 60)}}}}).hour,
          "unknown window remains missing");
    check(numberLabel({}, true) == L"--", "missing label");
    check(numberLabel(WindowQuota{68.9, 0}, true) == L"68%", "percent floor");
    check(numberLabel(WindowQuota{68.9, 0}, false, .5) == L"34", "numeric animation targets true value");
    Settings s =
        Settings::fromJson({{"WidgetStyle", "unknown"},
                            {"WidgetScale", 9},
                            {"RefreshIntervalSeconds", 13},
                            {"HourColorHex", "bad"},
                            {"StyleOptions", {{"plain", {{"Animate", false}, {"ShowPercent", false}}}}},
                            {"SomeFutureField", 123}});
    check(s.style == "gauges" && s.scale == 1.4 && s.interval == 30, "settings normalization");
    check(s.hourColor == "#219E86" && !s.preferences[4].percent && !s.preferences[4].animate,
          "legacy style options migrated");
    check(s.toJson()["SomeFutureField"] == 123, "unknown settings retained");
    check(Settings::fromJson({{"StyleOptions", nullptr}, {"Displays", nullptr}, {"CodexPath", nullptr}})
              .displays.empty(),
          "nullable old configuration");
    for (int i = 0; i < 7; ++i) {
        s.style = StyleIds[i];
        s.preferences[i] = {i % 2 == 0, i % 2 != 0, i % 2 == 0};
        auto restored = Settings::fromJson(s.toJson());
        check(restored.style == s.style && restored.pref().remaining == s.pref().remaining &&
                  restored.pref().percent == s.pref().percent && restored.pref().animate == s.pref().animate,
              "style round trip " + StyleIds[i]);
    }
    check(quote(L"C:\\路径 空格\\a.exe") == L"\"C:\\路径 空格\\a.exe\"", "Unicode command quoting");
    check(quote(L"tail\\") == L"\"tail\\\\\"", "trailing slash quoting");
    check(wide(utf8(L"中文 🙂")) == L"中文 🙂", "UTF8 round trip");
    check(desktopBinary(L"ChatGPT.exe", L"") && !desktopBinary(L"codex.exe", L"C:/cli/bin/codex.exe") &&
              !desktopBinary(L"codex.exe", L"C:/app/resources/codex.exe") &&
              desktopBinary(L"Codex.exe", L"C:/Programs/Codex/app/Codex.exe"),
          "desktop / CLI discrimination");
    State state;
    state.error.clear();
    state.updated = 1000;
    state.q.hour = WindowQuota{25, 999};
    state.q.week = WindowQuota{30, 2000};
    state.q.plan = "plus";
    Animation a;
    a.update(state, true, 100, 1000);
    check(a.progress(true, 100) == 0 && a.progress(false, 1300) == 1, "startup animation duration");
    a.update(state, true, 1400, 1000);
    check(!a.running(1400), "polling does not replay animation");
    state.q.hour = WindowQuota{100, 3000};
    a.update(state, true, 1500, 1000);
    check(a.running(1500) && a.progress(true, 1500) == 0 && a.progress(false, 1500) == 1,
          "confirmed reset independent animation");
    a.update(state, false, 1600, 1000);
    check(!a.running(1600), "animation off");
    a.restart(1700);
    check(a.progress(true, 1700) == 0, "style switch replay");
    state.error = L"error";
    a.update(state, true, 3000, 1000);
    check(!a.running(3000), "stale state does not restart");
    check(!State{}.stale() == false, "initial stale state");
    state.updated = 1000;
    state.error.clear();
    state.interval = 300;
    check(!state.stale(1700) && state.stale(1800), "stale threshold scales with refresh");
    Settings draw;
    State actual;
    actual.error.clear();
    actual.updated = utcNow();
    actual.q.hour = WindowQuota{68, 0};
    actual.q.week = WindowQuota{27, 0};
    for (auto &style : StyleIds)
        for (bool pro : {false, true})
            for (int dpi : {96, 144, 192}) {
                draw.style = style;
                actual.q.plan = pro ? "pro" : "plus";
                auto l = widgetLayout(draw, pro, dpi, int(40 * dpi / 96.));
                check(l.width > 0 && l.height <= 40 * dpi / 96,
                      "taskbar layout " + style + (pro ? " pro " : " ") + std::to_string(dpi));
                Surface start(l.width, l.height), finish(l.width, l.height);
                paintWidget(start, l, actual, draw, false, 0, 0);
                paintWidget(finish, l, actual, draw, false, 1, 1);
                check(memcmp(start.pixels, finish.pixels, l.width * l.height * 4) != 0,
                      "animated pixel frames " + style + (pro ? " pro " : " ") + std::to_string(dpi));
            }
    draw = Settings{};
    draw.style = "outlined";
    auto l = widgetLayout(draw, false, 144, 60);
    Surface transparent(l.width, l.height);
    paintWidget(transparent, l, actual, draw, false);
    check(transparent.pixels[3] == 1, "transparent gaps preserve right-click hit surface");
    Settings disconnected;
    disconnected.displayMode = "selected";
    disconnected.displays = {"disconnected"};
    auto selected = selectedMonitors(disconnected);
    check(!selected.empty() && selected.front().primary, "disconnected display fallback");
    std::atomic<bool> stop{false};
    int notifications = 0;
    {
        Rpc server(executable(), &stop, [&](Quotas) { ++notifications; }, true);
        auto rpc = server.read();
        check(rpc.hour && rpc.hour->remaining == 68 && rpc.week && rpc.week->remaining == 27 &&
                  rpc.plan == "plus",
              "real process JSON-RPC pipe initialize/account/quota round trip");
    }
    check(notifications == 1, "live Codex event applied; unrelated model event ignored");
    auto fixture =
        fs::temp_directory_path() / (L"quota-native-marker-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                                     std::to_wstring(GetTickCount64()));
    check(fs::create_directory(fixture), "isolated uninstall marker fixture");
    auto marker = fixture / L"installer.ini";
    {
        std::ofstream out(fixture / L"unins000.exe");
        out << "fixture only";
    }
    WritePrivateProfileStringW(L"Installation", L"Product", L"CodexQuotaBar", marker.c_str());
    WritePrivateProfileStringW(L"Installation", L"Version", Version, marker.c_str());
    check(nativeInstalled(fixture), "native uninstall marker recognizes current version");
    WritePrivateProfileStringW(L"Installation", L"Product", L"OtherProduct", marker.c_str());
    check(!nativeInstalled(fixture), "native uninstall rejects unrelated marker");
    fs::remove(marker);
    fs::remove(fixture / L"unins000.exe");
    fs::remove(fixture);
    log << checks << " native checks passed.\n";
    return 0;
}
static void pumpUntil(const std::function<bool()> &ready) {
    MSG message;
    auto deadline = GetTickCount64() + 4000;
    while (!ready() && GetTickCount64() < deadline) {
        if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message != WM_QUIT) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        } else
            Sleep(1);
    }
}
int uiTests(const fs::path &dir) {
    fs::create_directories(dir);
    std::ofstream log(dir / L"checks.txt");
    int count = 0;
    auto check = [&](bool ok, const std::string &name) {
        log << (ok ? "PASS " : "FAIL ") << name << std::endl;
        if (!ok)
            throw std::runtime_error(name);
        ++count;
    };
    Application app(false, true);
    app.state.active = true;
    app.state.error.clear();
    app.state.updated = utcNow();
    app.state.q.hour = WindowQuota{68, utcNow() + 1800};
    app.state.q.week = WindowQuota{27, utcNow() + 3600};
    app.settings = Settings{};
    app.rebuild();
    for (size_t i = 0; i < app.overlays.size(); ++i) {
        auto &overlay = *app.overlays[i];
        auto bar = taskbar(overlay.monitor.handle);
        RECT r;
        GetWindowRect(overlay.window, &r);
        check(!bar || std::abs((r.top + r.bottom) - (bar->top + bar->bottom)) <= 1,
              "taskbar vertical center monitor " + std::to_string(i));
        POINT point{r.left + 2, r.top + 2};
        overlay.tick(&point);
        overlay.hoverStart = GetTickCount64() - 300;
        overlay.tick(&point);
        check(overlay.hover && IsWindowVisible(overlay.details),
              "hover backdrop and details monitor " + std::to_string(i));
        SendMessageW(overlay.window, WM_RBUTTONUP, 0, 0);
        pumpUntil([&] { return app.settingsWindow && IsWindowVisible(app.settingsWindow->window); });
        check(app.settingsWindow && IsWindowVisible(app.settingsWindow->window),
              "right click opens settings monitor " + std::to_string(i));
        RECT settings;
        GetWindowRect(app.settingsWindow->window, &settings);
        auto work = overlay.monitor.work;
        check(std::abs((settings.left + settings.right) - (work.left + work.right)) <= 2 &&
                  std::abs((settings.top + settings.bottom) - (work.top + work.bottom)) <= 2,
              "first visible settings center monitor " + std::to_string(i));
        app.settingsWindow->previewStart = -1;
        app.settingsWindow->capture(dir / (L"settings-monitor-" + std::to_wstring(i) + L".png"));
        SendMessageW(app.settingsWindow->window, WM_CLOSE, 0, 0);
        pumpUntil([&] { return !app.settingsWindow; });
        check(!app.settingsWindow, "closed settings released");
        overlay.tick(&point);
        overlay.hoverStart = GetTickCount64() - 300;
        overlay.tick(&point);
        check(IsWindowVisible(overlay.details),
              "hover restored after right-click close monitor " + std::to_string(i));
        overlay.hideDetails();
    }
    app.showSettings();
    auto &settings = *app.settingsWindow;
    settings.selectPage(0);
    for (int i = 0; i < 7; ++i) {
        SendMessageW(settings.styles, CB_SETCURSEL, i, 0);
        settings.command(200, CBN_SELCHANGE);
        check(settings.draft.style == StyleIds[i], "settings style " + StyleIds[i]);
        check(settings.previewStart < 0 || GetTickCount64() - settings.previewStart < 100,
              "style change restarts preview");
    }
    settings.command(207, BN_CLICKED);
    check(!settings.draft.pref().percent, "percent option changes selected style");
    SendMessageW(settings.styles, CB_SETCURSEL, 0, 0);
    settings.command(200, CBN_SELCHANGE);
    check(settings.draft.pref().percent, "other style keeps independent percent option");
    SendMessageW(settings.styles, CB_SETCURSEL, 6, 0);
    settings.command(200, CBN_SELCHANGE);
    check(!settings.draft.pref().percent, "switch back preserves draft preference");
    settings.command(208, BN_CLICKED);
    check(!settings.draft.pref().animate && settings.previewStart < 0, "animation disabled remains static");
    SendMessageW(settings.scale, TBM_SETPOS, TRUE, 120);
    SendMessageW(settings.content, WM_HSCROLL, TB_THUMBPOSITION, reinterpret_cast<LPARAM>(settings.scale));
    check(std::abs(settings.draft.scale - 1.2) < .001, "display size slider changes preview scale");
    settings.openPalette(true);
    check(IsWindowVisible(settings.palette), "theme palette opens");
    SendMessageW(settings.palette, WM_LBUTTONUP, 0, MAKELPARAM(settings.px(18 + 7 * 27), settings.px(78)));
    check(!settings.palette && settings.draft.hourColor == "#70AD47", "palette color updates draft");
    settings.command(204, BN_CLICKED);
    check(settings.draft.hourColor == "#219E86" && settings.draft.weekColor == "#648DE6",
          "default colors restore");
    settings.selectPage(1);
    settings.capture(dir / L"settings-general.png");
    settings.selectPage(2);
    settings.capture(dir / L"settings-advanced.png");
    settings.selectPage(0);
    settings.capture(dir / L"settings-appearance.png");
    ShowWindow(settings.window, SW_MINIMIZE);
    settings.reveal(nullptr);
    check(IsWindowVisible(settings.window) && !IsIconic(settings.window), "minimized settings recover");
    ShowWindow(settings.window, SW_HIDE);
    settings.reveal(nullptr);
    check(IsWindowVisible(settings.window), "hidden settings recover");
    app.settingsWindow.reset();
    app.state = State{};
    app.rebuild();
    std::atomic<bool> desktop{false};
    app.desktopProbe = [&] { return desktop.load(); };
    app.fakeRpc = true;
    app.worker = std::thread(&Application::runWorker, &app);
    desktop = true;
    pumpUntil([&] { return app.state.active && app.state.updated && app.state.q.hour.has_value(); });
    check(app.state.active && app.state.q.hour && app.state.q.hour->remaining == 68 && !app.overlays.empty(),
          "desktop starts worker, reads fake subprocess and creates widgets");
    auto next = app.settings;
    next.interval = 15;
    app.saved(next);
    check(app.state.interval == 15, "new refresh interval applied without restart");
    desktop = false;
    pumpUntil([&] { return !app.state.active; });
    check(!app.state.active && app.overlays.empty(),
          "last desktop exit stops quota session and hides widgets");
    desktop = true;
    pumpUntil([&] { return app.state.active && app.state.updated; });
    check(app.state.active && app.state.updated && !app.overlays.empty(),
          "listener survives and follows next desktop start");
    app.stopping = true;
    app.wake.notify_all();
    app.worker.join();
    log << count << " native UI checks passed.\n";
    return 0;
}
} // namespace quota
