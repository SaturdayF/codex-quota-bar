#include "app.hpp"
namespace quota {
static void registerClass(const wchar_t *name, WNDPROC proc, UINT style = 0) {
    WNDCLASSEXW c{};
    c.cbSize = sizeof(c);
    c.hInstance = GetModuleHandleW(nullptr);
    c.lpszClassName = name;
    c.lpfnWndProc = proc;
    c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    c.hIcon = LoadIconW(c.hInstance, MAKEINTRESOURCEW(1));
    c.hIconSm = c.hIcon;
    c.style = style;
    RegisterClassExW(&c);
}
static bool inside(RECT r, POINT p) {
    return PtInRect(&r, p) != FALSE;
}
Overlay::Overlay(Application *a, const Monitor &m, const Settings &s, const State &initial)
    : app(a), monitor(m), settings(s) {
    registerClass(L"CodexQuotaBar.Native.Widget", procedure);
    registerClass(L"CodexQuotaBar.Native.Details", detailProcedure);
    window = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                             L"CodexQuotaBar.Native.Widget", L"Codex Quota Bar", WS_POPUP, m.bounds.left + 16,
                             m.bounds.top, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), this);
    details = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                              L"CodexQuotaBar.Native.Details", L"额度详情", WS_POPUP, 0, 0, 1, 1, window,
                              nullptr, GetModuleHandleW(nullptr), this);
    update(initial);
    position(true);
    SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    render();
    SetTimer(window, 1, 50, nullptr);
}
Overlay::~Overlay() {
    if (details)
        DestroyWindow(details);
    if (window)
        DestroyWindow(window);
}
void Overlay::position(bool reset) {
    auto bar = taskbar(monitor.handle);
    int available = bar ? bar->bottom - bar->top : int(40 * monitor.dpi / 96.);
    layout = widgetLayout(settings, state.q.weekOnly(), monitor.dpi, available);
    if (reset || !placed)
        x = monitor.bounds.left + 16 * monitor.dpi / 96;
    x = std::clamp(x, int(monitor.bounds.left),
                   std::max(int(monitor.bounds.left), int(monitor.bounds.right) - layout.width));
    y = bar ? bar->top + (available - layout.height) / 2 : monitor.work.bottom - layout.height - 4;
    placed = true;
    SetWindowPos(window, HWND_TOPMOST, x, y, layout.width, layout.height, SWP_NOACTIVATE);
}
void Overlay::render() {
    if (!window || !placed)
        return;
    Surface image(layout.width, layout.height);
    auto now = GetTickCount64();
    paintWidget(image, layout, state, settings, hover, animation.progress(true, now),
                animation.progress(false, now));
    image.present(window, x, y);
}
void Overlay::update(const State &value) {
    bool changed = state.q.weekOnly() != value.q.weekOnly();
    state = value;
    animation.update(state, settings.pref().animate && motionEnabled(), GetTickCount64(), utcNow());
    if (animation.running(GetTickCount64()))
        SetTimer(window, 2, 16, nullptr);
    if (!placed || changed)
        position();
    render();
    if (IsWindowVisible(details))
        showDetails();
}
void Overlay::hideDetails() {
    if (details)
        ShowWindow(details, SW_HIDE);
}
void Overlay::showDetails(const fs::path &capture) {
    float d = monitor.dpi / 96.f;
    int width = int(372 * d), height = int((state.q.weekOnly() ? 270 : 370) * d);
    width = std::min(width, int(monitor.work.right - monitor.work.left));
    int left =
        std::clamp<LONG>(x, monitor.work.left, std::max(monitor.work.left, monitor.work.right - width));
    int top = std::max(int(monitor.work.top), y - height - int(12 * d));
    Surface image(width, height);
    {
        Graphics g(image.bitmap.get());
        g.Clear(Color(0, 0, 0, 0));
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        bool light = lightTheme();
        Color ink = light ? Color(255, 30, 30, 30) : Color(255, 240, 240, 240);
        Color muted = light ? Color(255, 105, 105, 105) : Color(255, 178, 178, 178);
        fillRound(g, RectF(1, 1, width - 2.f, height - 2.f), 8 * d,
                  light ? Color(252, 249, 249, 249) : Color(252, 38, 38, 38));
        GraphicsPath border;
        roundPath(border, RectF(1, 1, width - 2.f, height - 2.f), 8 * d);
        Pen pen(light ? Color(255, 218, 218, 218) : Color(255, 75, 75, 75), 1);
        g.DrawPath(&pen, &border);
        drawUiText(g, L"Codex 额度", RectF(20 * d, 12 * d, width - 40 * d, 32 * d), 20 * d, ink, true);
        drawUiText(g, state.q.weekOnly() ? L"Pro · 每周额度" : L"5 小时与每周额度",
                 RectF(20 * d, 44 * d, width - 40 * d, 26 * d), 14 * d, muted);
        int row = 0;
        for (bool hour : {true, false}) {
            if (hour && state.q.weekOnly())
                continue;
            auto q = hour ? state.q.hour : state.q.week;
            float yy = (86 + row * 100) * d;
            Color c = parseColor(hour ? settings.hourColor : settings.weekColor, Color(255, 33, 158, 134));
            drawUiText(g, hour ? L"5 小时" : L"每周", RectF(20 * d, yy, 100 * d, 25 * d), 16 * d, ink, true);
            std::wostringstream precise;
            if (q)
                precise << std::fixed << std::setprecision(2) << q->remaining << L"%";
            else
                precise << L"--";
            drawUiText(g, precise.str(), RectF(155 * d, yy, width - 175 * d, 32 * d), 24 * d, c, true, true);
            fillRound(g, RectF(20 * d, yy + 40 * d, width - 40 * d, 4 * d), 2 * d,
                      light ? Color(255, 222, 225, 230) : Color(255, 73, 78, 85));
            if (q && q->remaining > 0)
                fillRound(g, RectF(20 * d, yy + 40 * d, (width - 40 * d) * float(q->remaining / 100), 4 * d),
                          2 * d, c);
            drawUiText(g, L"重置 " + timeLabel(q ? q->resets : 0),
                     RectF(20 * d, yy + 51 * d, width - 40 * d, 26 * d), 14 * d, muted);
            ++row;
        }
        float footer = (86 + row * 100) * d;
        drawUiText(g,
                 L"更新 " + (state.updated ? timeLabel(state.updated, false) : L"尚无") + L"   ·   " +
                     std::to_wstring(state.interval) + L" 秒刷新",
                 RectF(20 * d, footer, width - 40 * d, 28 * d), 14 * d, muted);
        std::wstring status =
            state.error.empty() ? (state.stale() ? L"数据已过期，等待更新" : L"右键打开设置") : state.error;
        drawUiText(g, status, RectF(20 * d, footer + 30 * d, width - 40 * d, 28 * d), 14 * d,
                 state.stale() ? Color(255, 170, 110, 55) : muted);
    }
    if (!capture.empty())
        image.save(capture);
    image.present(details, left, top);
    ShowWindow(details, SW_SHOWNOACTIVATE);
    SetWindowPos(details, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}
void Overlay::tick(const POINT *simulatedPointer) {
    POINT p;
    if (simulatedPointer)
        p = *simulatedPointer;
    else
        GetCursorPos(&p);
    RECT bounds;
    GetWindowRect(window, &bounds);
    RECT card;
    GetWindowRect(details, &card);
    bool on = inside(bounds, p);
    bool within = on || (IsWindowVisible(details) && inside(card, p));
    auto now = GetTickCount64();
    if (app->settingsWindow && IsWindowVisible(app->settingsWindow->window)) {
        hideDetails();
        if (hover) {
            hover = false;
            render();
        }
        return;
    }
    if (within) {
        lastInside = now;
        if (!hover) {
            hover = true;
            hoverStart = now;
            render();
        }
        if (on && !dragging && now - hoverStart >= 225 && !IsWindowVisible(details))
            showDetails();
    } else if (hover && now - lastInside > 180) {
        hover = false;
        hideDetails();
        render();
    }
    if (dragging)
        hideDetails();
    SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}
LRESULT CALLBACK Overlay::procedure(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    auto self = reinterpret_cast<Overlay *>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        self = static_cast<Overlay *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
        SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self)
        return DefWindowProcW(w, msg, wp, lp);
    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
        if (wp == 2) {
            self->render();
            if (!self->animation.running(GetTickCount64()))
                KillTimer(w, 2);
        } else
            self->tick();
        return 0;
    case WM_LBUTTONDOWN:
        self->dragging = true;
        GetCursorPos(&self->dragStart);
        self->origin = {self->x, self->y};
        SetCapture(w);
        return 0;
    case WM_MOUSEMOVE:
        if (self->dragging) {
            POINT p;
            GetCursorPos(&p);
            self->x = self->origin.x + p.x - self->dragStart.x;
            self->position();
            self->render();
        }
        return 0;
    case WM_LBUTTONUP:
        self->dragging = false;
        ReleaseCapture();
        return 0;
    case WM_CAPTURECHANGED:
        self->dragging = false;
        return 0;
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
        self->hideDetails();
        self->hover = false;
        self->render();
        PostMessageW(self->app->controller, SettingsMessage, reinterpret_cast<WPARAM>(self->monitor.handle),
                     0);
        return 0;
    case WM_DPICHANGED:
        self->monitor.dpi = HIWORD(wp);
        PostMessageW(w, WM_APP + 4, 0, 0);
        return 0;
    case WM_APP + 4:
        if (self->window) {
            self->position();
            self->render();
        }
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}
LRESULT CALLBACK Overlay::detailProcedure(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_MOUSEACTIVATE)
        return MA_NOACTIVATE;
    return DefWindowProcW(w, msg, wp, lp);
}
Application::Application(bool previewMode, bool test) : preview(previewMode), testing(test) {
    registerClass(L"CodexQuotaBar.Native.Controller", procedure);
    controller = CreateWindowExW(WS_EX_TOOLWINDOW, L"CodexQuotaBar.Native.Controller",
                                 L"Codex Quota Bar Native Controller", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                                 GetModuleHandleW(nullptr), this);
    if (!controller)
        throw std::runtime_error("Cannot create controller");
    if (!testing && !preview) {
        quitEvent = CreateEventW(nullptr, TRUE, FALSE, L"Local\\CodexQuotaBar.Quit");
        ResetEvent(quitEvent);
    }
    SetTimer(controller, 1, 500, nullptr);
    if (preview) {
        state.active = true;
        state.error.clear();
        state.updated = utcNow();
        state.q.hour = WindowQuota{68, utcNow() + 1800};
        state.q.week = WindowQuota{27, utcNow() + 86400};
        state.q.plan = "plus";
        rebuild();
    } else if (!testing)
        worker = std::thread(&Application::runWorker, this);
}
Application::~Application() {
    stopping = true;
    wake.notify_all();
    if (worker.joinable())
        worker.join();
    settingsWindow.reset();
    overlays.clear();
    if (quitEvent)
        CloseHandle(quitEvent);
    if (controller) {
        MSG msg;
        while (PeekMessageW(&msg, controller, StateMessage, StateMessage, PM_REMOVE))
            delete reinterpret_cast<State *>(msg.lParam);
        DestroyWindow(controller);
    }
}
void Application::stop() {
    stopping = true;
    wake.notify_all();
    PostQuitMessage(0);
}
void Application::refresh() {
    {
        std::lock_guard<std::mutex> guard(mutex);
        refreshRequested = true;
    }
    wake.notify_all();
}
void Application::saved(const Settings &value) {
    {
        std::lock_guard<std::mutex> guard(mutex);
        settings = value;
        generation++;
        refreshRequested = true;
    }
    wake.notify_all();
    state.interval = value.interval;
    rebuild();
}
void Application::checkSettingsFile() {
    if (testing)
        return;
    std::error_code ec;
    auto write = fs::last_write_time(dataDirectory() / L"settings.json", ec);
    if (!ec && write != settingsWrite) {
        settingsWrite = write;
        saved(Settings::load());
    }
}
void Application::rebuild() {
    overlays.clear();
    if (!state.active)
        return;
    for (auto &m : selectedMonitors(settings))
        overlays.push_back(std::make_unique<Overlay>(this, m, settings, state));
}
void Application::apply(const State &value) {
    bool layoutChanged = value.active != state.active || value.q.weekOnly() != state.q.weekOnly();
    state = value;
    if (layoutChanged)
        rebuild();
    else
        for (auto &o : overlays)
            o->update(state);
}
void Application::showSettings(HMONITOR m) {
    if (settingsWindow && settingsWindow->closing)
        settingsWindow.reset();
    if (!settingsWindow)
        settingsWindow = std::make_unique<SettingsWindow>(this, m);
    settingsWindow->reveal(m);
}
void Application::runWorker() {
    State current;
    std::unique_ptr<Rpc> rpc;
    bool active = false;
    int failures = 0, seenGeneration = -1;
    ULONGLONG next = 0, lastWatch = 0;
    Settings cfg;
    auto emit = [&] {
        auto copy = new State(current);
        if (!PostMessageW(controller, StateMessage, 0, reinterpret_cast<LPARAM>(copy)))
            delete copy;
    };
    while (!stopping) {
        auto now = GetTickCount64();
        bool force = false;
        {
            std::lock_guard<std::mutex> guard(mutex);
            if (seenGeneration != generation) {
                if (cfg.codexPath != settings.codexPath)
                    rpc.reset();
                cfg = settings;
                seenGeneration = generation;
                current.interval = cfg.interval;
                next = 0;
            }
            force = refreshRequested;
            refreshRequested = false;
        }
        if (now - lastWatch >= 2000 || !lastWatch) {
            lastWatch = now;
            bool running = desktopProbe();
            if (running != active) {
                active = running;
                rpc.reset();
                failures = 0;
                next = 0;
                current = State{};
                current.active = active;
                current.interval = cfg.interval;
                emit();
            }
        }
        if (active && (now >= next || force)) {
            try {
                if (!rpc)
                    rpc = std::make_unique<Rpc>(
                        fakeRpc ? executable() : findCodex(cfg.codexPath), &stopping,
                        [&](Quotas q) {
                            current.q = q;
                            current.error.clear();
                            current.updated = utcNow();
                            emit();
                        },
                        fakeRpc, [&] { return !desktopProbe(); });
                current.q = rpc->read();
                current.updated = utcNow();
                current.error.clear();
                failures = 0;
                next = GetTickCount64() + cfg.interval * 1000;
                emit();
            } catch (const std::exception &) {
                rpc.reset();
                if (stopping)
                    break;
                current.error = L"读取失败，请检查 Codex 路径、网络和登录状态";
                failures = std::min(failures + 1, 6);
                next =
                    GetTickCount64() + int(std::min(300., cfg.interval * std::pow(2., failures - 1))) * 1000;
                emit();
            }
        }
        if (rpc) {
            try {
                rpc->poll();
            } catch (...) {
                rpc.reset();
                next = 0;
            }
        }
        std::unique_lock<std::mutex> lock(mutex);
        wake.wait_for(lock, std::chrono::milliseconds(100),
                      [&] { return stopping || refreshRequested || seenGeneration != generation; });
    }
    rpc.reset();
}
LRESULT CALLBACK Application::procedure(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    auto self = reinterpret_cast<Application *>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        self = static_cast<Application *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
        SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self)
        return DefWindowProcW(w, msg, wp, lp);
    switch (msg) {
    case WM_APP + 10:
        if (self->settingsWindow && self->settingsWindow->closing)
            self->settingsWindow.reset();
        return 0;
    case StateMessage: {
        std::unique_ptr<State> state(reinterpret_cast<State *>(lp));
        if (!self->stopping)
            self->apply(*state);
        return 0;
    }
    case SettingsMessage:
        self->showSettings(reinterpret_cast<HMONITOR>(wp));
        return 0;
    case RebuildMessage:
        self->rebuild();
        return 0;
    case WM_TIMER:
        if (self->quitEvent && WaitForSingleObject(self->quitEvent, 0) == WAIT_OBJECT_0) {
            self->stop();
            return 0;
        }
        self->checkSettingsFile();
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
        self->rebuild();
        if (self->settingsWindow)
            SendMessageW(self->settingsWindow->window, WM_SETTINGCHANGE, wp, lp);
        return 0;
    case WM_CLOSE:
        self->stop();
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}
} // namespace quota
