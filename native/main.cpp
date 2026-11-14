#include "app.hpp"
#include "uninstall.hpp"
namespace quota {
int liveGlassTests(const fs::path &, std::ostream &);
int liveAnalyticsGlassTests(const fs::path &, std::ostream &);
int syntheticUiTests(const fs::path &);
}
using namespace quota;
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    using DpiFn = BOOL(WINAPI *)(HANDLE);
    auto dpi = reinterpret_cast<DpiFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (dpi)
        dpi(reinterpret_cast<HANDLE>(-4));
    else
        SetProcessDPIAware();
    int count = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::wstring> args;
    for (int i = 1; i < count; ++i)
        args.push_back(argv[i]);
    LocalFree(argv);
    auto has = [&](const wchar_t *flag) { return std::find(args.begin(), args.end(), flag) != args.end(); };
    auto after = [&](const wchar_t *flag, fs::path fallback) {
        auto it = std::find(args.begin(), args.end(), flag);
        return it != args.end() && it + 1 != args.end() ? fs::path(*(it + 1)) : fallback;
    };
    if (has(L"--fake-server"))
        return fakeServer();
    if (!args.empty() && args.front() == L"--update-helper")
        return runUpdateHelper(args);
    ULONG_PTR token = 0;
    GdiplusStartupInput input;
    if (GdiplusStartup(&token, &input, nullptr) != Ok)
        return 1;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    int result = 0;
    HANDLE singleton = nullptr;
    try {
        if (has(L"--uninstall-test-child"))
            Sleep(100);
        else if (has(L"--uninstall-plan")) {
            auto plan = portableUninstallPlan(executable());
            plan["userData"] = utf8(dataDirectory().wstring());
            std::ofstream out(after(L"--output", L"uninstall-plan.json"));
            out << plan.dump(2);
        }
        else if (has(L"--uninstall-user-data"))
            result = removeUninstallUserData(has(L"-WhatIf")) ? 0 : 1;
        else if (has(L"--hub-fixture")) {
            auto dir=after(L"--output",L"hub-fixture");fs::create_directories(dir);HubSettings cfg;cfg.mode=2;cfg.port=0;cfg.deviceId="synthetic-host";HubRuntime runtime;runtime.start(cfg,"synthetic-test-secret-only",dir,true);
            saveAnalyticsJson(dir/L"ready.json",{{"port",runtime.port()}});auto deadline=GetTickCount64()+60000;while(GetTickCount64()<deadline&&!fs::exists(dir/L"stop"))Sleep(50);runtime.stop();
        }
        else if (has(L"--hub-render-ui")) {
            auto dir=after(L"--output",L"hub-ui");fs::create_directories(dir);Application app(false,true);app.hubConfig.mode=2;app.hubConfig.port=17321;app.hubConfig.deviceId="simulated-windows";auto fixture=app.hubConfig;fixture.port=0;app.hub.start(fixture,"synthetic-test-secret-only",dir/L"synthetic-store",true);auto data=hubLocalSnapshot(demoUsageReport(utcNow()),State{},fixture.deviceId);data["hostname"]="Simulated Windows";app.hub.publish(data);app.showSettings();auto &ui=*app.settingsWindow;ui.command(105,BN_CLICKED);if(ui.page!=5||!IsWindowVisible(GetDlgItem(ui.content,700)))throw std::runtime_error("Hub navigation/controls inaccessible");
            for(int mode=0;mode<3;++mode){SendDlgItemMessageW(ui.content,700,CB_SETCURSEL,mode,0);ui.command(700,CBN_SELCHANGE);if(bool(IsWindowEnabled(GetDlgItem(ui.content,701)))!=(mode==1)||bool(IsWindowEnabled(GetDlgItem(ui.content,702)))!=(mode==2))throw std::runtime_error("Hub mode field availability mismatch");}
            if(!(GetWindowLongPtrW(GetDlgItem(ui.content,703),GWL_STYLE)&ES_PASSWORD))throw std::runtime_error("Hub secret is not masked");ui.command(708,BN_CLICKED);if(!GetPropW(ui.window,L"HubHistoryMonthly"))throw std::runtime_error("Hub monthly history unavailable");ui.command(708,BN_CLICKED);
            std::ofstream(dir/L"checks.txt")<<"PASS Hub navigation and visible controls\nPASS local/client/host field availability\nPASS shared secret masking\nPASS daily/monthly history switching\n";
            ui.capture(dir/L"hub-light-simulated.png");ui.scrollTo(850);ui.capture(dir/L"hub-totals-light-simulated.png");ui.draft.darkAppearance=true;ui.applyAppearance();ui.capture(dir/L"hub-totals-dark-simulated.png");ui.scrollTo(0);ui.capture(dir/L"hub-dark-simulated.png");
        }
        else if (has(L"--hub-test")) {
            auto dir=after(L"--output",L"hub-tests");fs::create_directories(dir);std::ofstream log(dir/L"checks.txt");hubTests(log,dir);
        }
        else if (has(L"--self-test"))
            result = selfTests(after(L"--output", L"native-tests.txt"));
        else if (has(L"--glass-test")) {
            auto dir = after(L"--output", L"native-glass-tests");
            fs::create_directories(dir);
            std::ofstream log(dir / L"checks.txt");
            liveGlassTests(dir, log);
        }
        else if (has(L"--analytics-glass-test")) {
            auto dir = after(L"--output", L"analytics-glass-tests"); fs::create_directories(dir);
            std::ofstream log(dir / L"checks.txt"); liveAnalyticsGlassTests(dir, log);
        }
        else if (has(L"--analytics-ui-test")) {
            analyticsUiTests(after(L"--output", L"analytics-ui-tests"));
        }
        else if (has(L"--ui-test-synthetic"))
            result = syntheticUiTests(after(L"--output", L"native-ui-tests"));
        else if (has(L"--ui-test"))
            result = uiTests(after(L"--output", L"native-ui-tests"));
        else if (has(L"--deepseek-ui-test")) {
            deepSeekUiTests(after(L"--output", L"deepseek-ui-tests"));
            result = 0;
        }
        else if (has(L"--render-ui"))
            renderPreviews(after(L"--render-ui", L"native-previews"));
        else if (has(L"--edge-token-probe") || has(L"--browser-token-probe")) {
            // Status only: never persist or print browser credentials.
            std::atomic<bool> cancel{false};
            json report;
            try {
                auto browser = defaultPlatformBrowser();
                bool edgeOnly = has(L"--edge-token-probe");
                if (!edgeOnly && browser.kind == BrowserKind::Unsupported) throw std::runtime_error("Unsupported browser");
                auto root = edgeOnly ? edgeUserData() : browser.userData;
                auto profile = browserProfileName(root);
                auto acquired = acquireBrowserPlatformToken(root, profile, cancel, edgeOnly ? L"Edge" : browser.name);
                report = {{"browser", utf8(edgeOnly ? L"Edge" : browser.name)}, {"profileAvailable", true}, {"validated", !acquired.token.empty()}, {"error", utf8(acquired.error)}};
            } catch (...) { report = {{"profileAvailable", false}, {"validated", false}, {"error", "Browser profile unavailable or unsupported"}}; }
            std::ofstream out(after(L"--output", L"edge-token-probe.json"));
            out << report.dump(2);
            result = report["validated"].get<bool>() ? 0 : 1;
        }
        else if (has(L"--deepseek-probe")) {
            // Status-only diagnostics: never serialize credentials, raw API
            // responses, account identifiers, balances or individual usage.
            std::atomic<bool> cancel{false};
            auto state = readDeepSeek(deepSeekCredential(), cancel);
            json report = {{"balanceAvailable", state.balanceAt > 0}, {"costAvailable", state.costAt > 0},
                           {"tokensAvailable", state.tokensAt > 0}, {"modelCount", state.models.size()},
                           {"balanceError", utf8(state.balanceError)}, {"costError", utf8(state.costError)},
                           {"tokensError", utf8(state.tokensError)}};
            std::ofstream out(after(L"--output", L"deepseek-probe.json"));
            out << report.dump(2);
            result = state.balanceAt && state.costAt && state.tokensAt ? 0 : 1;
        }
        else if (has(L"--usage-probe")) {
            std::atomic<bool> cancel{false};
            UsageScanOptions options; options.home = codexHome(); options.cache = after(L"--cache", {});
            auto report = scanCodexUsage(options, cancel);
            size_t unpriced = 0, attributed = 0;
            for (const auto &entry : report.entries) { if (!entry.priced) ++unpriced; if (!entry.account.empty()) ++attributed; }
            json status{{"available",report.available},{"files",report.files},{"pending",report.pending},
                        {"partial",report.partial},{"entryCount",report.entries.size()},
                        {"unpricedEntries",unpriced},{"attributedEntries",attributed},
                        {"cacheWarning",!report.error.empty()},{"pricingDate",UsagePricingDate}};
            std::ofstream out(after(L"--output",L"usage-probe.json")); out << status.dump(2);
            result = report.available ? 0 : 1;
        }
        else if (has(L"--probe")) {
            std::atomic<bool> canceled{false};
            Rpc rpc(findCodex(Settings::load().codexPath), &canceled);
            auto q = rpc.read();
            auto window = [](auto &w) -> json {
                return w ? json{{"remaining", w->remaining}, {"resetsAt", w->resets}} : json(nullptr);
            };
            json report = {{"hour", window(q.hour)}, {"week", window(q.week)},
                           {"extraWindowCount", q.extraWindows.size()}, {"historyOwnerAvailable", !q.accountKey.empty()},
                           {"plan", q.plan},         {"desktopRunning", desktopRunning()},
                           {"planLabel", utf8(q.planLabel())}, {"weekOnly", q.weekOnly()},
                           {"native", true},         {"version", VersionUtf8}};
            std::ofstream out(after(L"--output", L"native-probe.json"));
            out << report.dump(2);
        } else {
            bool preview = has(L"--preview");
            auto readyName = after(L"--update-ready", {}).wstring();
            HANDLE ready = nullptr;
            if (!preview && after(L"--update-version", {}).wstring() == Version &&
                readyName.rfind(L"Local\\CodexQuotaBar.Update-", 0) == 0)
                ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, readyName.c_str());
            std::unique_ptr<void, decltype(&CloseHandle)> readyEvent(ready, CloseHandle);
            if (!preview) {
                HANDLE updating = OpenMutexW(SYNCHRONIZE, FALSE, UpdateLock);
                if (updating) {
                    CloseHandle(updating);
                    if (!readyEvent) {
                        CoUninitialize();
                        GdiplusShutdown(token);
                        return 0;
                    }
                }
                singleton = CreateMutexW(nullptr, FALSE, L"Local\\CodexQuotaBar.Listener");
                if (GetLastError() == ERROR_ALREADY_EXISTS) {
                    if (has(L"--settings")) {
                        auto existing = FindWindowW(L"CodexQuotaBar.Native.Controller", nullptr);
                        if (existing)
                            PostMessageW(existing, SettingsMessage, 0, 0);
                        else
                            MessageBoxW(nullptr, L"旧版本正在运行。请退出旧版本后启动原生版。",
                                        L"Codex Quota Bar", MB_OK);
                    }
                    if (singleton)
                        CloseHandle(singleton);
                    CoUninitialize();
                    GdiplusShutdown(token);
                    return 0;
                }
            }
            Application app(preview);
            if (has(L"--settings"))
                app.showSettings();
            if (has(L"--smoke-ui"))
                SetTimer(app.controller, 2, 7000, nullptr);
            if (readyEvent) PostMessageW(app.controller, WM_NULL, 0, 0);
            MSG msg;
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                if (readyEvent && msg.message == WM_NULL && msg.hwnd == app.controller) {
                    SetEvent(readyEvent.get());
                    readyEvent.reset();
                }
                if (msg.message == WM_TIMER && msg.hwnd == app.controller && msg.wParam == 2) {
                    app.stop();
                    continue;
                }
                if (app.settingsWindow && IsWindowVisible(app.settingsWindow->window) &&
                    !app.settingsWindow->palette) {
                    if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE &&
                        !SendMessageW(app.settingsWindow->styles, CB_GETDROPPEDSTATE, 0, 0)) {
                        SendMessageW(app.settingsWindow->window, WM_CLOSE, 0, 0);
                        continue;
                    }
                    if (IsDialogMessageW(app.settingsWindow->window, &msg))
                        continue;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    } catch (const std::exception &e) {
        result = 1;
        if (has(L"--output")) {
            auto file = after(L"--output", L"native-error.txt");
            if (fs::is_directory(file))
                file /= L"error.txt";
            std::ofstream out(file, has(L"--self-test") ? std::ios::app : std::ios::out);
            out << "Error: " << e.what();
        } else if (!has(L"--uninstall-user-data"))
            MessageBoxW(nullptr, L"程序无法完成操作，请检查运行目录、Codex 路径和登录状态。",
                        L"Codex Quota Bar", MB_OK | MB_ICONERROR);
    }
    if (singleton)
        CloseHandle(singleton);
    CoUninitialize();
    GdiplusShutdown(token);
    return result;
}
