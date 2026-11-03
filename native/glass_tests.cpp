#include "app.hpp"
namespace quota {
void activateTestWindow(HWND window) {
    DWORD current = GetCurrentThreadId();
    DWORD foreground = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    bool attached = foreground && foreground != current && AttachThreadInput(current, foreground, TRUE);
    SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(window);
    SetActiveWindow(window);
    if (attached) AttachThreadInput(current, foreground, FALSE);
}
// Synthetic windows only: screen reads are restricted to this fixture's bounds.
int liveGlassTests(const fs::path &dir, std::ostream &log) {
    int count = 0;
    auto check = [&](bool ok, const char *name) {
        log << (ok ? "PASS " : "FAIL ") << name << std::endl;
        if (!ok) throw std::runtime_error(name);
        ++count;
    };
    struct Background { COLORREF left = RGB(224, 40, 40), right = left; int split = 0; } bg;
    WNDCLASSW c{};
    c.hInstance = GetModuleHandleW(nullptr);
    c.lpszClassName = L"CodexQuotaBar.Test.LiveGlass";
    c.lpfnWndProc = [](HWND w, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
        if (msg == WM_NCCREATE)
            SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams));
        if (msg == WM_PAINT) {
            auto bg = reinterpret_cast<Background *>(GetWindowLongPtrW(w, GWLP_USERDATA));
            PAINTSTRUCT ps{}; auto dc = BeginPaint(w, &ps);
            RECT r{}; GetClientRect(w, &r);
            auto fill = CreateSolidBrush(bg->right); FillRect(dc, &r, fill); DeleteObject(fill);
            r.right = bg->split;
            fill = CreateSolidBrush(bg->left); FillRect(dc, &r, fill); DeleteObject(fill);
            EndPaint(w, &ps); return 0;
        }
        return DefWindowProcW(w, msg, wp, lp);
    };
    RegisterClassW(&c);
    auto monitor = monitors().front();
    auto work = monitor.work;
    HWND previous = GetForegroundWindow();
    HWND back = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, c.lpszClassName, L"Synthetic glass backdrop",
                                WS_POPUP | WS_VISIBLE, work.left, work.top, work.right - work.left, work.bottom - work.top,
                                nullptr, nullptr, c.hInstance, &bg);
    struct Cleanup { HWND back, previous; ~Cleanup() {
        DestroyWindow(back);
        if (IsWindow(previous)) SetForegroundWindow(previous);
    } } cleanup{back, previous};
    Application fixture(false, true);
    KillTimer(fixture.controller, 1);
    fixture.settings = Settings{};
    SettingsWindow settings(&fixture, monitor.handle);
    settings.simulatedGlass = false;
    settings.applyAppearance();
    settings.reveal(monitor.handle);
    KillTimer(settings.window, 1);
    settings.previewStart = -1;
    int width = std::min(settings.px(800), int(work.right - work.left) - 160);
    int height = std::min(settings.px(580), int(work.bottom - work.top) - 40);
    SetWindowPos(settings.window, HWND_TOPMOST, work.left + 20, work.top + 20, width, height, SWP_SHOWWINDOW);
    activateTestWindow(settings.window);
    auto settle = [] {
        ULONGLONG until = GetTickCount64() + 300;
        do {
            MSG m{};
            while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&m); DispatchMessageW(&m);
            }
            Sleep(5);
        } while (GetTickCount64() < until);
        DwmFlush();
    };
    auto repaintBackground = [&] {
        InvalidateRect(back, nullptr, FALSE); UpdateWindow(back); settle();
    };
    auto point = [&](int x, int y) { POINT p{x, y}; ClientToScreen(settings.window, &p); return p; };
    auto sample = [](POINT p) { HDC dc = GetDC(nullptr); auto value = GetPixel(dc, p.x, p.y); ReleaseDC(nullptr, dc); return value; };
    auto difference = [](COLORREF a, COLORREF b) {
        return std::abs(int(GetRValue(a)) - int(GetRValue(b))) + std::abs(int(GetGValue(a)) - int(GetGValue(b))) +
               std::abs(int(GetBValue(a)) - int(GetBValue(b)));
    };
    auto capture = [&](const wchar_t *name, HWND target = nullptr) {
        RECT r{}; GetWindowRect(target ? target : settings.window, &r);
        Surface image(r.right - r.left, r.bottom - r.top);
        HDC dc = GetDC(nullptr);
        BitBlt(image.dc, 0, 0, image.width, image.height, dc, r.left, r.top, SRCCOPY);
        ReleaseDC(nullptr, dc); GdiFlush();
        for (int i = 0; i < image.width * image.height; ++i) image.pixels[i * 4 + 3] = 255;
        image.save(dir / name);
    };
    settle();
    check(!settings.settingsFrost, "live settings never capture or cache desktop pixels");
    if (settings.backdrop == BackdropMode::Solid) {
        log << "SKIP live compositor checks: composition or transparency unavailable\n";
        return count;
    }
    for (bool dark : {false, true}) {
        settings.draft.darkAppearance = dark; settings.applyAppearance();
        activateTestWindow(settings.window);
        bg.left = bg.right = RGB(224, 40, 40); repaintBackground();
        POINT body = point(settings.px(235), settings.px(94));
        POINT title = point(width / 2, -settings.px(12));
        log << "INFO glass visible=" << IsWindowVisible(settings.window) << " iconic=" << IsIconic(settings.window) << " foreground=" << (GetForegroundWindow() == settings.window) << " bodyRoot=" << (GetAncestor(WindowFromPoint(body), GA_ROOT) == settings.window) << std::endl;
        check(GetAncestor(WindowFromPoint(body), GA_ROOT) == settings.window, "live glass fixture is visible above its synthetic background");
        auto red = sample(body), redTitle = sample(title);
        bg.left = bg.right = RGB(40, 70, 224); repaintBackground();
        check(difference(red, sample(body)) > 20, "live glass follows changes behind the settings content without repainting it");
        if (settings.backdrop == BackdropMode::Acrylic)
            check(difference(redTitle, sample(title)) > 20, "native title bar follows the same live Acrylic background");
        capture(dark ? L"live-dark-blue.png" : L"live-light-blue.png");
    }
    settings.draft.darkAppearance = false; settings.applyAppearance();
    POINT body = point(settings.px(235), settings.px(94));
    bg.left = RGB(224, 40, 40); bg.right = RGB(40, 70, 224); bg.split = body.x - work.left + 50;
    repaintBackground();
    auto before = sample(body);
    RECT bounds{}; GetWindowRect(settings.window, &bounds);
    SetWindowPos(settings.window, nullptr, bounds.left + 120, bounds.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    settle();
    body = point(settings.px(235), settings.px(94));
    check(difference(before, sample(body)) > 20, "moving settings samples its new screen position through DWM");
    capture(L"live-moved.png");
    SetWindowPos(settings.window, nullptr, 0, 0, width - 50, height - 30, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    bg.left = bg.right = RGB(224, 40, 40); repaintBackground();
    before = sample(body);
    bg.left = bg.right = RGB(40, 70, 224); repaintBackground();
    check(difference(before, sample(body)) > 20, "resizing preserves live glass composition");
    settings.selectPage(2); settle();
    capture(L"live-inputs.png");
    ShowWindow(settings.window, SW_HIDE);

    // Check the real non-layered details presentation, with synthetic quota data.
    State state; state.active = true; state.updated = utcNow(); state.q.plan = "plus";
    state.q.hour = WindowQuota{68, utcNow() + 1800}; state.q.week = WindowQuota{27, utcNow() + 3600};
    Overlay detail(&fixture, monitor, fixture.settings, state);
    KillTimer(detail.window, 1);
    detail.x = work.left + 30; detail.y = work.bottom - 20;
    fixture.testing = false;
    detail.showDetails();
    fixture.testing = true;
    bg.left = bg.right = RGB(224, 40, 40); repaintBackground();
    POINT detailPoint{settings.px(350), settings.px(75)}; ClientToScreen(detail.details, &detailPoint);
    auto red = sample(detailPoint);
    bg.left = bg.right = RGB(40, 70, 224); repaintBackground();
    log << "INFO detail red=" << red << " blue=" << sample(detailPoint) << " root=" << (GetAncestor(WindowFromPoint(detailPoint), GA_ROOT) == detail.details) << std::endl;
    check(!detail.detailFrost && difference(red, sample(detailPoint)) > 20,
          "hover details also use live glass without a cached desktop image");
    capture(L"live-details.png", detail.details);
    return count;
}
} // namespace quota
