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
    check(q.planLabel() == L"Pro", "plan title canonical casing");
    q.plan = "plus";
    check(q.planLabel() == L"Plus", "Plus plan title");
    q.plan = "enterprise";
    check(q.planLabel() == L"Enterprise", "Enterprise plan title");
    q.plan = "future_plan";
    check(q.planLabel() == L"future_plan", "unrecognized plan preserved");
    q.plan.clear();
    check(q.planLabel() == L"套餐未知", "missing plan never inferred from quota windows");
    for (auto plan : {"pro", "prolite", "promax", "ProLite", "PROMAX"}) {
        auto pro = Quotas::parse({{"rateLimits", {{"planType", plan}, {"primary", nullptr},
                                                  {"secondary", quota(9, 10080)}}}});
        check(pro.weekOnly() && !pro.hour && pro.week && pro.week->remaining == 91,
              std::string("weekly-only Pro payload ") + plan);
        pro.hour = WindowQuota{68, 1000};
        check(pro.weekOnly(), std::string("Pro display preference with both windows ") + plan);
    }
    q.plan = "prolite";
    check(q.planLabel() == L"Pro Lite", "Pro Lite plan label");
    q.plan = "promax";
    check(q.planLabel() == L"Pro Max", "Pro Max plan label");
    for (auto plan : {"free", "go", "plus", "team", "self_serve_business_prolite",
                      "self_serve_business_usage_based", "business", "ent26", "enterprise_cbp_automation",
                      "enterprise_cbp_usage_based", "enterprise", "edu", "edu_plus", "edu_pro", "sci",
                      "sci_plus", "unknown", "future_pro_tier"}) {
        q.plan = plan;
        check(!q.weekOnly(), std::string("non-consumer-Pro retains both available windows ") + plan);
    }
    q.hour.reset();
    check(q.weekOnly() && q.planLabel() == L"future_pro_tier",
          "unknown plan with only weekly data hides missing hour without inferring plan");
    q.week.reset();
    check(!q.weekOnly(), "no quota data does not imply weekly-only plan");
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
    Settings visibility;
    Quotas account;
    account.plan = "plus";
    account.week = WindowQuota{27, 1000};
    check(visibility.visibility(account) == QuotaVisibility{true, true},
          "Plus defaults to both quotas even before hour data arrives");
    for (auto plan : {"pro", "prolite", "promax"}) {
        account.plan = plan;
        check(visibility.visibility(account) == QuotaVisibility{false, true},
              std::string("default weekly selection ") + plan);
        visibility.showHour = true;
        visibility.showWeek = false;
        check(visibility.visibility(account) == QuotaVisibility{true, false},
              std::string("manual hour-only selection overrides ") + plan);
        visibility.showHour.reset();
        visibility.showWeek.reset();
    }
    for (int mask = 0; mask < 4; ++mask) {
        visibility.showHour = bool(mask & 1);
        visibility.showWeek = bool(mask & 2);
        auto restored = Settings::fromJson(visibility.toJson());
        check(restored.showHour == visibility.showHour && restored.showWeek == visibility.showWeek,
              "quota visibility round trip " + std::to_string(mask));
    }
    auto automatic = Settings::fromJson({{"ShowHourQuota", nullptr}, {"ShowWeekQuota", "invalid"}});
    check(!automatic.showHour.has_value() && !automatic.showWeek.has_value(),
          "missing null and malformed visibility settings follow account defaults");
    visibility.showHour.reset();
    check(!Settings::fromJson(visibility.toJson()).showHour.has_value(),
          "automatic hour choice survives settings round trip");
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
        for (auto plan : {"plus", "pro", "prolite", "promax"})
            for (int dpi : {96, 144, 192}) {
                draw.style = style;
                actual.q.plan = plan;
                auto l = widgetLayout(draw, draw.visibility(actual.q), dpi, int(40 * dpi / 96.));
                check(l.width > 0 && l.height <= 40 * dpi / 96,
                      "taskbar layout " + style + " " + plan + " " + std::to_string(dpi));
                Surface start(l.width, l.height), finish(l.width, l.height);
                paintWidget(start, l, actual, draw, false, 0, 0);
                paintWidget(finish, l, actual, draw, false, 1, 1);
                check(memcmp(start.pixels, finish.pixels, l.width * l.height * 4) != 0,
                      "animated pixel frames " + style + " " + plan + " " + std::to_string(dpi));
            }
    draw = Settings{};
    for (auto &style : StyleIds) {
        draw.style = style;
        actual.q.plan = "prolite";
        for (int dpi : {96, 144, 192}) {
            std::vector<BYTE> hourPixels;
            for (int mask = 0; mask < 4; ++mask) {
                draw.showHour = bool(mask & 1);
                draw.showWeek = bool(mask & 2);
                auto visible = draw.visibility(actual.q);
                auto selected = widgetLayout(draw, visible, dpi, 40 * dpi / 96);
                Surface image(selected.width, selected.height);
                paintWidget(image, selected, actual, draw, false);
                bool hasInk = false;
                for (int pixel = 0; pixel < image.width * image.height; ++pixel)
                    hasInk = hasInk || image.pixels[pixel * 4 + 3] > 1;
                check(hasInk == (mask != 0),
                      "selected quota pixels " + style + " " + std::to_string(mask) + " " + std::to_string(dpi));
                std::vector<BYTE> pixels(image.pixels, image.pixels + image.width * image.height * 4);
                if (mask == 1)
                    hourPixels = pixels;
                if (mask == 2)
                    check(hourPixels != pixels, "hour-only and week-only render distinct data " + style);
            }
        }
    }
    draw = Settings{};
    draw.style = "outlined";
    auto l = widgetLayout(draw, draw.visibility(actual.q), 144, 60);
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
    app.state.q.plan = "plus";
    app.settings = Settings{};
    app.rebuild();
    for (size_t i = 0; i < app.overlays.size(); ++i) {
        auto &overlay = *app.overlays[i];
        auto bar = taskbar(overlay.monitor.handle);
        RECT r;
        GetWindowRect(overlay.window, &r);
        log << "INFO monitor " << i << " widget " << r.left << ',' << r.top << ',' << r.right << ','
            << r.bottom << " intended " << overlay.x << ',' << overlay.y << " host " << overlay.host
            << " parent " << GetParent(overlay.window) << " style "
            << GetWindowLongPtrW(overlay.window, GWL_STYLE) << std::endl;
        if (bar)
            log << "INFO taskbar " << bar->left << ',' << bar->top << ',' << bar->right << ',' << bar->bottom
                << std::endl;
        check(!bar || std::abs((r.top + r.bottom) - (bar->top + bar->bottom)) <= 1,
              "taskbar vertical center monitor " + std::to_string(i));
        check(!bar || (GetParent(overlay.window) == taskbarWindow(overlay.monitor.handle) &&
                      (GetWindowLongPtrW(overlay.window, GWL_STYLE) & WS_CHILD)),
              "widget embedded in monitor taskbar " + std::to_string(i));
        check(GetAncestor(overlay.details, GA_ROOT) == overlay.details &&
                  GetWindow(overlay.details, GW_OWNER) == app.controller,
              "details remain independent of taskbar clipping " + std::to_string(i));
        auto foreground = GetForegroundWindow();
        auto oldX = overlay.x;
        overlay.x += 23;
        overlay.position();
        overlay.render();
        RECT moved{};
        GetWindowRect(overlay.window, &moved);
        check(moved.left == overlay.x && moved.top == overlay.y,
              "layered child uses screen coordinates after drag monitor " + std::to_string(i));
        check(GetForegroundWindow() == foreground, "widget placement preserves focus");
        overlay.x = oldX;
        overlay.position();
        overlay.render();
        POINT point{r.left + 2, r.top + 2};
        overlay.tick(&point);
        overlay.hoverStart = GetTickCount64() - 300;
        overlay.tick(&point);
        check(overlay.hover && IsWindowVisible(overlay.details),
              "hover backdrop and details monitor " + std::to_string(i));
        overlay.showDetails(dir / (L"details-monitor-" + std::to_wstring(i) + L".png"));
        if (i == 0) {
            auto regular = overlay.state;
            overlay.state.q.plan = "pro";
            overlay.showDetails(dir / L"details-pro.png");
            overlay.state = regular;
        }
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
    // Verify a live account-plan change rebuilds both the widget and hover card.
    auto regularState = app.state;
    for (auto plan : {"pro", "prolite", "promax", "future_weekly_plan"}) {
        auto weekly = regularState;
        weekly.q.plan = plan;
        weekly.q.hour.reset();
        app.apply(weekly);
        for (size_t i = 0; i < app.overlays.size(); ++i) {
            auto &overlay = *app.overlays[i];
            auto bar = taskbar(overlay.monitor.handle);
            auto expected = widgetLayout(app.settings, app.settings.visibility(weekly.q), overlay.monitor.dpi,
                                        bar ? bar->bottom - bar->top : int(40 * overlay.monitor.dpi / 96.));
            check(overlay.state.q.weekOnly() && overlay.layout.width == expected.width,
                  std::string("weekly-only widget after account update ") + plan);
            overlay.showDetails(dir / wide(std::string("details-") + plan + "-monitor-" + std::to_string(i) + ".png"));
            RECT card{};
            GetWindowRect(overlay.details, &card);
            check(card.bottom - card.top == int(270 * overlay.monitor.dpi / 96.f),
                  std::string("weekly-only details omit 5h row ") + plan);
            overlay.hideDetails();
        }
        app.apply(regularState);
        check(!app.overlays.front()->state.q.weekOnly(), "Plus restores two-window widget");
    }
    app.showSettings();
    auto &settings = *app.settingsWindow;
    auto proState = regularState;
    proState.q.plan = "prolite";
    proState.q.hour.reset();
    settings.previewStart = -1;
    app.apply(proState);
    check(!motionEnabled() || settings.previewStart >= 0,
          "account-default change refreshes open settings preview");
    settings.selectPage(1);
    check(settings.previewSettings().visibility(proState.q) == QuotaVisibility{false, true},
          "preview follows Pro defaults before manual selection");
    settings.capture(dir / L"settings-general-pro-default.png");
    settings.command(222, BN_CLICKED);
    check(settings.previewSettings().visibility(proState.q) == QuotaVisibility{true, true},
          "Pro can enable 5h in settings and preview");
    app.saved(settings.draft);
    check(app.overlays.front()->settings.visibility(proState.q).count() == 2,
          "saved Pro 5h override reaches taskbar");
    settings.command(223, BN_CLICKED);
    app.saved(settings.draft);
    auto &hourOnly = *app.overlays.front();
    check(hourOnly.settings.visibility(proState.q) == QuotaVisibility{true, false},
          "week can be disabled independently for hour-only taskbar");
    hourOnly.showDetails(dir / L"details-manual-hour-only.png");
    RECT hourCard{};
    GetWindowRect(hourOnly.details, &hourCard);
    check(hourCard.bottom - hourCard.top == int(270 * hourOnly.monitor.dpi / 96.f),
          "hour-only details have one row even for a Pro account");
    hourOnly.hideDetails();
    settings.command(222, BN_CLICKED);
    app.saved(settings.draft);
    check(app.overlays.empty() && IsWindowVisible(settings.window),
          "disabling both hides widgets while settings remain accessible");
    settings.selectPage(0);
    settings.capture(dir / L"settings-all-hidden.png");
    settings.command(224, BN_CLICKED);
    app.saved(settings.draft);
    check(!settings.draft.showHour.has_value() && !settings.draft.showWeek.has_value() &&
              app.overlays.front()->settings.visibility(proState.q) == QuotaVisibility{false, true},
          "reset restores automatic Pro defaults");
    app.apply(regularState);
    check(app.settings.visibility(regularState.q) == QuotaVisibility{true, true},
          "automatic selection follows next Plus account");
    settings.selectPage(0);
    auto previewPixels = [&](int elapsed) {
        settings.previewStart = elapsed < 0 ? -1 : int64_t(GetTickCount64()) - elapsed;
        RECT r{};
        GetClientRect(settings.content, &r);
        Surface image(r.right, r.bottom);
        settings.draw(image.dc, true);
        GdiFlush();
        std::vector<BYTE> pixels;
        for (int y = settings.px(286); y < settings.px(348); ++y)
            pixels.insert(pixels.end(), image.pixels + y * image.width * 4,
                          image.pixels + (y + 1) * image.width * 4);
        return pixels;
    };
    for (bool enabled : {false, true}) {
        settings.previewStart = -1;
        settings.command(206, BN_CLICKED);
        check(settings.draft.pref().remaining == enabled, "remaining toggle updates draft");
        check(!motionEnabled() || settings.previewStart >= 0, "remaining toggle restarts preview animation");
        if (motionEnabled()) {
            auto start = previewPixels(0), middle = previewPixels(400), end = previewPixels(-1);
            check(start != middle && middle != end, "remaining toggle changes actual preview pixels over time");
        }
    }
    settings.command(208, BN_CLICKED);
    settings.command(206, BN_CLICKED);
    check(settings.previewStart < 0, "remaining preview respects disabled animation");
    settings.command(206, BN_CLICKED);
    settings.command(208, BN_CLICKED);
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
    // Inspect the actual child-window pixels after partial clipping and scrolling,
    // rather than only WM_PRINTCLIENT, which always constructs a clean picture.
    settings.previewStart = -1;
    auto readControl = [&](HWND child) {
        RECT r{};
        GetClientRect(child, &r);
        Surface image(r.right, r.bottom);
        HDC dc = GetDC(child);
        BitBlt(image.dc, 0, 0, r.right, r.bottom, dc, 0, 0, SRCCOPY);
        ReleaseDC(child, dc);
        GdiFlush();
        return std::vector<BYTE>(image.pixels, image.pixels + r.right * r.bottom * 4);
    };
    RECT originalBounds{};
    GetWindowRect(settings.window, &originalBounds);
    SetWindowPos(settings.window, nullptr, 0, 0, originalBounds.right - originalBounds.left,
                 settings.px(520), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    for (int offset : {19, 93, 157, 211, 37, 0}) {
        settings.scroll = offset;
        settings.layout();
        for (int id : {202, 203, 205, 206, 207, 208}) {
            HWND child = GetDlgItem(settings.content, id);
            RECT r{}, client{};
            GetWindowRect(child, &r);
            GetClientRect(settings.content, &client);
            MapWindowPoints(settings.content, nullptr, reinterpret_cast<POINT *>(&client), 2);
            if (r.top < client.top || r.bottom > client.bottom) continue;
            auto moved = readControl(child);
            RedrawWindow(child, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            check(moved == readControl(child), "scroll repaint matches clean control " +
                  std::to_string(id) + " at " + std::to_string(offset));
        }
    }
    SetWindowPos(settings.window, nullptr, 0, 0, originalBounds.right - originalBounds.left,
                 originalBounds.bottom - originalBounds.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    settings.scroll = 0;
    settings.layout();
    settings.capture(dir / L"settings-appearance.png");
    ShowWindow(settings.window, SW_MINIMIZE);
    settings.reveal(nullptr);
    check(IsWindowVisible(settings.window) && !IsIconic(settings.window), "minimized settings recover");
    ShowWindow(settings.window, SW_HIDE);
    settings.reveal(nullptr);
    check(IsWindowVisible(settings.window), "hidden settings recover");
    app.settingsWindow.reset();
    // Exercise shell elevation and host loss without changing the real taskbar
    // or restarting the user's Explorer process.
    {
        Application fixture(false, true);
        fixture.settings.displayMode = "primary";
        auto monitor = selectedMonitors(fixture.settings).front();
        RECT bar = taskbar(monitor.handle).value_or(
            RECT{monitor.bounds.left, monitor.work.bottom - 48, monitor.bounds.right, monitor.work.bottom});
        auto makeHost = [&] {
            return CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"Quota test taskbar",
                                   WS_POPUP | WS_VISIBLE, bar.left, bar.top, bar.right - bar.left,
                                   bar.bottom - bar.top, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        };
        HWND host = makeHost();
        check(host != nullptr, "isolated taskbar fixture created");
        fixture.taskbarProbe = [&](HMONITOR) { return host; };
        fixture.state = app.state;
        fixture.rebuild();
        auto &widget = *fixture.overlays.front();
        auto foreground = GetForegroundWindow();
        SetWindowPos(host, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        RECT r{};
        GetWindowRect(widget.window, &r);
        POINT p{r.left + (r.right - r.left) / 2, r.top + (r.bottom - r.top) / 2};
        widget.tick(&p);
        check(IsWindowVisible(widget.window) && WindowFromPoint(p) == widget.window,
              "elevated taskbar still exposes widget hit surface");
        check(GetForegroundWindow() == foreground, "taskbar maintenance does not steal focus");
        DestroyWindow(host);
        host = nullptr;
        fixture.checkTaskbars();
        check(IsWindow(fixture.overlays.front()->window) && !fixture.overlays.front()->host,
              "destroyed taskbar recovers as temporary popup");
        host = makeHost();
        fixture.checkTaskbars();
        check(GetParent(fixture.overlays.front()->window) == host,
              "replacement taskbar reattaches widget");
        fixture.overlays.clear();
        DestroyWindow(host);
    }
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
