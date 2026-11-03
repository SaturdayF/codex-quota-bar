#include "app.hpp"
#include "uninstall.hpp"
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
        else if (has(L"--self-test"))
            result = selfTests(after(L"--output", L"native-tests.txt"));
        else if (has(L"--ui-test"))
            result = uiTests(after(L"--output", L"native-ui-tests"));
        else if (has(L"--render-ui"))
            renderPreviews(after(L"--render-ui", L"native-previews"));
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
        else if (has(L"--probe")) {
            std::atomic<bool> canceled{false};
            Rpc rpc(findCodex(Settings::load().codexPath), &canceled);
            auto q = rpc.read();
            auto window = [](auto &w) -> json {
                return w ? json{{"remaining", w->remaining}, {"resetsAt", w->resets}} : json(nullptr);
            };
            json report = {{"hour", window(q.hour)}, {"week", window(q.week)},
                           {"plan", q.plan},         {"desktopRunning", desktopRunning()},
                           {"planLabel", utf8(q.planLabel())}, {"weekOnly", q.weekOnly()},
                           {"native", true},         {"version", VersionUtf8}};
            std::ofstream out(after(L"--output", L"native-probe.json"));
            out << report.dump(2);
        } else {
            bool preview = has(L"--preview");
            if (!preview) {
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
            MSG msg;
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
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
            std::ofstream out(file);
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
