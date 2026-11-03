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
Overlay::Overlay(Application *a, const Monitor &m, const Settings &s, const State &initial, WidgetKind type)
    : app(a), monitor(m), settings(s), kind(type) {
    registerClass(L"CodexQuotaBar.Native.Widget", procedure);
    registerClass(L"CodexQuotaBar.Native.Details", detailProcedure);
    // A desktop TOPMOST popup can still be covered when the shell raises its
    // taskbar for Quick Settings. A child follows the taskbar's shell band.
    host = app->taskbarProbe(m.handle);
    window = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW |
                                 (host ? 0 : WS_EX_TOPMOST),
                             L"CodexQuotaBar.Native.Widget", kind == WidgetKind::DeepSeek ? L"DeepSeekHarness" : L"Codex Quota Bar", host ? WS_CHILD : WS_POPUP,
                             0, 0, 1, 1, host ? host : app->controller, nullptr,
                             GetModuleHandleW(nullptr), this);
    HWND rejectedHost = nullptr;
    if (!window && host) {
        rejectedHost = host;
        SetPropW(app->controller, L"QuotaEmbedError", reinterpret_cast<HANDLE>(static_cast<INT_PTR>(GetLastError())));
        host = nullptr;
        window = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                                 L"CodexQuotaBar.Native.Widget", kind == WidgetKind::DeepSeek ? L"DeepSeekHarness" : L"Codex Quota Bar", WS_POPUP,
                                 0, 0, 1, 1, app->controller, nullptr, GetModuleHandleW(nullptr), this);
    }
    if (!window)
        throw std::runtime_error("Cannot create quota widget");
    if (rejectedHost) SetPropW(window, L"QuotaRejectedHost", rejectedHost);
    details = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                              L"CodexQuotaBar.Native.Details", kind == WidgetKind::DeepSeek ? L"DeepSeekHarness 详情" : L"额度详情", WS_POPUP, 0, 0, 1, 1, app->controller,
                              nullptr, GetModuleHandleW(nullptr), this);
    update(initial);
    position(true);
    SetWindowPos(window, host ? HWND_TOP : HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | (blocked ? 0 : SWP_SHOWWINDOW));
    render();
    SetTimer(window, 1, 50, nullptr);
}
Overlay::~Overlay() {
    dragging = false;
    if (window && GetCapture() == window) ReleaseCapture();
    if (details)
        DestroyWindow(details);
    if (window)
        DestroyWindow(window);
}
void Overlay::position(bool reset) {
    measure(reset);
    if (std::any_of(app->overlays.begin(), app->overlays.end(), [&](auto &o) { return o.get() == this; }))
        app->layoutWidgets(dragging ? this : nullptr);
    else
        place(x, settings.avoidIcons && host && !app->testing && !app->preview);
}
void Overlay::measure(bool reset) {
    RECT bounds{};
    std::optional<RECT> bar;
    if (auto barWindow = host ? host : app->taskbarProbe(monitor.handle); barWindow && GetWindowRect(barWindow, &bounds))
        bar = bounds;
    int available = bar ? bar->bottom - bar->top : int(40 * monitor.dpi / 96.);
    layout = kind == WidgetKind::DeepSeek ? deepSeekLayout(app->deepseek, settings, monitor.dpi, available)
                                         : widgetLayout(settings, settings.visibility(state.q), monitor.dpi, available);
    if (reset || !placed) {
        x = monitor.bounds.left + 16 * monitor.dpi / 96;
        if (kind == WidgetKind::DeepSeek)
            for (auto &other : app->overlays)
                if (other->kind == WidgetKind::Codex && other->monitor.handle == monitor.handle)
                    x = other->x + other->layout.width + 12 * monitor.dpi / 96;
    }
    x = std::clamp(x, int(monitor.bounds.left),
                   std::max(int(monitor.bounds.left), int(monitor.bounds.right) - layout.width));
    y = bar ? bar->top + (available - layout.height) / 2 : monitor.work.bottom - layout.height - 4;
    placed = true;
}
void Overlay::place(int target, bool hidden, bool animate) {
    x = target;
    bool visibilityChanged = blocked != hidden;
    blocked = hidden;
    if (visibilityChanged && blocked) { hideDetails(); ShowWindow(window, SW_HIDE); }
    RECT current{};
    bool known = GetWindowRect(window, &current);
    bool sameShape = known && current.top == y && current.right - current.left == layout.width &&
                     current.bottom - current.top == layout.height;
    bool canAnimate = !hidden && !dragging && sameShape && IsWindowVisible(window) && app->dragMotionProbe();
    if (slideStart >= 0 && slideTarget == x && canAnimate) return;
    if ((animate || slideStart >= 0) && canAnimate && std::abs(int64_t(current.left) - x) > 1) {
        slideFrom = visualX = current.left;
        slideTarget = x;
        slideStart = GetTickCount64();
        hover = false;
        hideDetails();
        SetTimer(window, 3, 16, nullptr);
        return;
    }
    slideStart = -1;
    KillTimer(window, 3);
    visualX = x;
    POINT local{visualX, y};
    if (host) ScreenToClient(host, &local);
    if (!sameShape || current.left != x) {
        SetWindowPos(window, nullptr, local.x, local.y, layout.width, layout.height,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        render();
    }
    if (visibilityChanged && !blocked) ShowWindow(window, SW_SHOWNOACTIVATE);
}
void Overlay::animatePosition(int64_t now) {
    if (slideStart < 0) return;
    bool done = now - slideStart >= SettleDuration || !app->dragMotionProbe();
    visualX = done ? slideTarget : settlePosition(slideFrom, slideTarget, now - slideStart);
    RECT current{};
    if (GetWindowRect(window, &current) && current.left != visualX) {
        POINT local{visualX, y};
        if (host) ScreenToClient(host, &local);
        SetWindowPos(window, nullptr, local.x, local.y, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
    }
    if (done) { slideStart = -1; KillTimer(window, 3); }
}
void Overlay::stopPositionAnimation() {
    if (slideStart < 0) return;
    RECT current{};
    if (GetWindowRect(window, &current)) x = visualX = current.left;
    slideStart = -1;
    KillTimer(window, 3);
}
void Overlay::beginDrag(POINT pointer) {
    for (auto &other : app->overlays)
        if (other->monitor.handle == monitor.handle) other->stopPositionAnimation();
    dragging = true;
    dragStart = pointer;
    origin = {x, y};
    dragPeerOrigin.reset();
    for (auto &other : app->overlays)
        if (other.get() != this && other->monitor.handle == monitor.handle && !other->blocked)
            dragPeerOrigin = other->x;
    hideDetails();
    SetWindowPos(window, host ? HWND_TOP : HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}
void Overlay::dragTo(POINT pointer) {
    if (!dragging) return;
    x = origin.x + pointer.x - dragStart.x;
    app->layoutWidgets(this);
}
void Overlay::finishDrag(POINT pointer, bool cancel) {
    if (!dragging) return;
    if (!cancel) x = origin.x + pointer.x - dragStart.x;
    else x = origin.x;
    dragging = false;
    hover = false;
    hideDetails();
    if (cancel) {
        if (dragPeerOrigin)
            for (auto &other : app->overlays)
                if (other.get() != this && other->monitor.handle == monitor.handle)
                    other->x = *dragPeerOrigin;
        app->layoutWidgets(nullptr, true);
    } else {
        app->layoutWidgets(this, true);
    }
    dragPeerOrigin.reset();
    render();
}
void Overlay::ensureLayer() {
    if (blocked) return;
    if (!host) {
        if (!(GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST))
            SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return;
    }
    RECT bounds{};
    if (!GetWindowRect(window, &bounds)) return;
    for (HWND sibling = GetWindow(window, GW_HWNDPREV); sibling;
         sibling = GetWindow(sibling, GW_HWNDPREV)) {
        // Our providers must not compete for HWND_TOP. Only recover when an
        // overlapping shell child has actually been placed above the widget.
        if (!IsWindowVisible(sibling) ||
            std::any_of(app->overlays.begin(), app->overlays.end(),
                        [&](auto &other) { return other->window == sibling; }))
            continue;
        RECT other{}, intersection{};
        if (GetWindowRect(sibling, &other) && IntersectRect(&intersection, &bounds, &other)) {
            SetWindowPos(window, HWND_TOP, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            break;
        }
    }
}
void Overlay::render() {
    if (!window || !placed)
        return;
    Surface image(layout.width, layout.height);
    auto now = GetTickCount64();
    if (kind == WidgetKind::DeepSeek)
        paintDeepSeek(image, app->deepseek, settings, hover);
    else
        paintWidget(image, layout, state, settings, hover, animation.progress(true, now),
                    animation.progress(false, now));
    // Child placement is already handled in parent coordinates by position().
    // Updating only the pixels avoids a second coordinate conversion by USER32.
    image.present(window, visualX, y, !host);
}
void Overlay::update(const State &value) {
    bool changed = settings.visibility(state.q) != settings.visibility(value.q);
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
    detailFrost.reset();
}
void Overlay::showDetails(const fs::path &capture) {
    if (kind == WidgetKind::DeepSeek) {
        showDeepSeekDetails(capture);
        return;
    }
    float d = monitor.dpi / 96.f;
    auto visible = settings.visibility(state.q);
    int width = int(372 * d), height = int((170 + 100 * visible.count()) * d);
    width = std::min(width, int(monitor.work.right - monitor.work.left));
    int left =
        std::clamp<LONG>(x, monitor.work.left, std::max(monitor.work.left, monitor.work.right - width));
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
        drawHeading(g, L"Codex 额度", RectF(20 * d, 12 * d, 124 * d, 32 * d), 20 * d, ink);
        drawUiText(g, state.q.planLabel(), RectF(154 * d, 12 * d, width - 174 * d, 32 * d),
                   14 * d, muted);
        drawUiText(g, visible.count() == 2 ? L"5 小时与每周额度" : visible.hour ? L"5 小时额度" : L"每周额度",
                 RectF(20 * d, 44 * d, width - 40 * d, 26 * d), 14 * d, muted);
        int row = 0;
        for (bool hour : {true, false}) {
            if (hour ? !visible.hour : !visible.week)
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
                      colors.deep);
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
                 state.stale() ? colors.warning : muted);
    }
    if (!capture.empty())
        image.save(capture);
    image.present(details, left, top);
    ShowWindow(details, SW_SHOWNOACTIVATE);
    SetWindowPos(details, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}
void Overlay::tick(const POINT *simulatedPointer) {
    if (blocked || slideStart >= 0) {
        hover = false;
        hideDetails();
        return;
    }
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
    case WM_NCDESTROY:
        self->window = nullptr;
        SetWindowLongPtrW(w, GWLP_USERDATA, 0);
        break;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
        if (wp == 3) self->animatePosition(GetTickCount64());
        else if (wp == 2) {
            self->render();
            if (!self->animation.running(GetTickCount64()))
                KillTimer(w, 2);
        } else
            self->tick();
        return 0;
    case WM_LBUTTONDOWN:
        { POINT pointer{}; GetCursorPos(&pointer); self->beginDrag(pointer); }
        SetCapture(w);
        return 0;
    case WM_MOUSEMOVE:
        if (self->dragging) {
            POINT p;
            GetCursorPos(&p);
            self->dragTo(p);
        }
        return 0;
    case WM_LBUTTONUP:
        { POINT pointer{}; GetCursorPos(&pointer); self->finishDrag(pointer); }
        ReleaseCapture();
        return 0;
    case WM_CAPTURECHANGED:
        self->finishDrag({}, true);
        return 0;
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
        self->hideDetails();
        self->hover = false;
        self->render();
        PostMessageW(self->app->controller, SettingsMessage, reinterpret_cast<WPARAM>(self->monitor.handle),
                     self->kind == WidgetKind::DeepSeek ? 3 : 0);
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
    if (msg == WM_NCCREATE)
        SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams));
    auto self = reinterpret_cast<Overlay *>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (msg == WM_MOUSEWHEEL && self && self->kind == WidgetKind::DeepSeek) {
        self->detailPage = std::max(0, self->detailPage + (GET_WHEEL_DELTA_WPARAM(wp) < 0 ? 1 : -1));
        self->showDeepSeekDetails();
        return 0;
    }
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
    } else if (!testing) {
        worker = std::thread(&Application::runWorker, this);
        deepSeekWorker = std::thread(&Application::runDeepSeekWorker, this);
        taskbarWorker = std::thread(&Application::runTaskbarWorker, this);
    }
}
Application::~Application() {
    stopping = true;
    *deepSeekSessionStop = true;
    wake.notify_all();
    if (worker.joinable())
        worker.join();
    if (deepSeekWorker.joinable())
        deepSeekWorker.join();
    if (taskbarWorker.joinable())
        taskbarWorker.join();
    settingsWindow.reset();
    overlays.clear();
    if (quitEvent)
        CloseHandle(quitEvent);
    if (controller) {
        MSG msg;
        while (PeekMessageW(&msg, controller, StateMessage, StateMessage, PM_REMOVE))
            delete reinterpret_cast<State *>(msg.lParam);
        while (PeekMessageW(&msg, controller, DeepSeekMessage, DeepSeekMessage, PM_REMOVE))
            delete reinterpret_cast<DeepSeekUpdate *>(msg.lParam);
        while (PeekMessageW(&msg, controller, TaskbarMessage, TaskbarMessage, PM_REMOVE))
            delete reinterpret_cast<TaskbarUpdate *>(msg.lParam);
        DestroyWindow(controller);
    }
}
void Application::stop() {
    stopping = true;
    *deepSeekSessionStop = true;
    wake.notify_all();
    PostQuitMessage(0);
}
void Application::refresh() {
    {
        std::lock_guard<std::mutex> guard(mutex);
        refreshRequested = true;
        deepSeekRefreshRequested = true;
    }
    wake.notify_all();
}
void Application::saved(const Settings &value) {
    {
        std::lock_guard<std::mutex> guard(mutex);
        settings = value;
        *deepSeekSessionStop = true;
        generation++;
        refreshRequested = true;
        deepSeekRefreshRequested = true;
    }
    wake.notify_all();
    state.interval = value.interval;
    taskbarSpaces.clear();
    deepseek = DeepSeekState{};
    if (!testing) {
        std::error_code ec;
        auto stamp = fs::last_write_time(dataDirectory() / L"settings.json", ec);
        if (!ec) settingsWrite = stamp;
    }
    checkHarness(true);
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
    for (auto &overlay : overlays) {
        auto found = std::find_if(widgetPositions.begin(), widgetPositions.end(), [&](auto &p) {
            return p.monitor == overlay->monitor.name && p.kind == overlay->kind;
        });
        int savedX = overlay->dragging ? int(overlay->origin.x) : overlay->x;
        for (auto &moving : overlays)
            if (moving.get() != overlay.get() && moving->dragging && moving->dragPeerOrigin &&
                moving->monitor.handle == overlay->monitor.handle)
                savedX = *moving->dragPeerOrigin;
        WidgetPosition p{overlay->monitor.name, overlay->kind, savedX};
        if (found == widgetPositions.end()) widgetPositions.push_back(p);
        else *found = p;
    }
    overlays.clear();
    for (auto &m : selectedMonitors(settings)) {
        if (state.active && settings.visibility(state.q).count())
            overlays.push_back(std::make_unique<Overlay>(this, m, settings, state));
        if (harnessActive)
            overlays.push_back(std::make_unique<Overlay>(this, m, settings, state, WidgetKind::DeepSeek));
    }
    for (auto &overlay : overlays)
        for (auto &p : widgetPositions)
            if (p.monitor == overlay->monitor.name && p.kind == overlay->kind) overlay->x = p.x;
    layoutWidgets();
}
void Application::layoutWidgets(Overlay *moving, bool animate) {
    for (auto &o : overlays) {
        o->measure();
        if (o->dragging) moving = o.get();
    }
    for (auto &monitor : selectedMonitors(settings)) {
        std::vector<Overlay *> group;
        for (auto &o : overlays) if (o->monitor.handle == monitor.handle) group.push_back(o.get());
        if (group.empty()) continue;
        RECT bar{};
        HWND host = group.front()->host;
        bool hasBar = host && GetWindowRect(host, &bar);
        RECT lane{monitor.bounds.left, group.front()->y, monitor.bounds.right,
                  group.front()->y + group.front()->layout.height};
        if (hasBar) {
            lane.left = std::max(bar.left, monitor.bounds.left);
            lane.right = std::min(bar.right, monitor.bounds.right);
            lane.top = bar.top; lane.bottom = bar.bottom;
        }
        int gap = std::max(1, 8 * monitor.dpi / 96);
        lane.left += gap; lane.right -= gap;
        std::vector<RECT> occupied;
        bool ready = !host || testing || preview;
        for (auto &space : taskbarSpaces)
            if (space.monitor == monitor.handle && space.host == host && hasBar && EqualRect(&space.bounds, &bar)) {
                occupied = space.occupied; ready = true; break;
            }
        bool avoid = group.front()->settings.avoidIcons;
        if (!avoid) { occupied.clear(); ready = true; }
        if (moving && moving->monitor.handle == monitor.handle) {
            auto dragX = freeTaskbarX(lane, moving->layout.width, moving->x, {}, 0);
            Overlay *peer = nullptr;
            if (moving->dragPeerOrigin)
                for (auto o : group) if (o != moving) peer = o;
            if (peer && dragX) {
                int anchor = *moving->dragPeerOrigin;
                RECT original{anchor, peer->y, anchor + peer->layout.width, peer->y + peer->layout.height};
                RECT dragged{*dragX, moving->y, *dragX + moving->layout.width, moving->y + moving->layout.height};
                RECT intersection{};
                bool overlaps = IntersectRect(&intersection, &dragged, &original);
                auto area = [](RECT r) { return int64_t(r.right - r.left) * (r.bottom - r.top); };
                bool swap = overlaps && area(intersection) * 2 >= area(original);
                std::optional<int> peerSlot, dropSlot;
                if (swap) {
                    bool peerGoesLeft = moving->origin.x < anchor;
                    int offset = (peerGoesLeft ? peer->layout.width : moving->layout.width) + gap;
                    int desiredLeft = peerGoesLeft ? anchor - offset : anchor;
                    // Reserve one contiguous slot so the two components stay adjacent.
                    if (auto left = freeTaskbarX(lane, moving->layout.width + peer->layout.width + gap,
                                                  desiredLeft, occupied, gap)) {
                        dropSlot = *left + (peerGoesLeft ? offset : 0);
                        peerSlot = *left + (peerGoesLeft ? 0 : offset);
                    }
                }
                peer->place(peerSlot.value_or(anchor), !ready, true);
                if (moving->dragging) {
                    moving->place(*dragX, !ready);
                } else if (dropSlot) {
                    moving->place(*dropSlot, !ready, true);
                } else {
                    // Pin the other component. Prefer the edge on the drop side,
                    // then search the rest of the taskbar for a valid slot.
                    occupied.push_back(original);
                    auto dropLane = lane;
                    int desired = *dragX;
                    if (overlaps) {
                        if (dragged.left + dragged.right <= original.left + original.right) {
                            desired = anchor - moving->layout.width - gap;
                            dropLane.right = std::min(dropLane.right, original.left - gap);
                        } else {
                            desired = original.right + gap;
                            dropLane.left = std::max(dropLane.left, original.right + gap);
                        }
                    }
                    auto slot = freeTaskbarX(dropLane, moving->layout.width, desired, occupied, gap);
                    if (!slot) slot = freeTaskbarX(lane, moving->layout.width, desired, occupied, gap);
                    moving->place(slot.value_or(moving->origin.x), !ready || !slot, true);
                }
            } else {
                auto slot = moving->dragging ? dragX :
                    freeTaskbarX(lane, moving->layout.width, moving->x, occupied, gap);
                moving->place(slot.value_or(moving->origin.x), !ready || !slot, animate);
            }
        } else if (!avoid) {
            for (auto o : group) o->place(o->x, false, animate);
        } else {
            std::vector<int> widths, desired;
            for (auto o : group) { widths.push_back(o->layout.width); desired.push_back(o->x); }
            auto arranged = arrangeTaskbarWidgets(lane, widths, desired, occupied, gap);
            for (size_t i = 0; i < group.size(); ++i)
                group[i]->place(arranged[i].value_or(group[i]->x), !ready || !arranged[i], animate);
        }
    }
}
void Application::checkTaskbars() {
    for (auto &o : overlays)
        if (!IsWindow(o->window) || (o->host != taskbarProbe(o->monitor.handle) && GetPropW(o->window, L"QuotaRejectedHost") != taskbarProbe(o->monitor.handle))) {
            // Explorer can destroy the child windows or replace the taskbar.
            rebuild();
            return;
        }
    layoutWidgets();
    for (auto &o : overlays) o->ensureLayer();
}
void Application::runTaskbarWorker() {
    auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    int seen = -1;
    while (!stopping) {
        Settings cfg;
        { std::lock_guard<std::mutex> lock(mutex); cfg = settings; seen = generation; }
        if (cfg.avoidIcons) {
            auto update = std::make_unique<TaskbarUpdate>();
            update->generation = seen;
            for (auto &monitor : selectedMonitors(cfg)) {
                if (stopping) break;
                update->spaces.push_back(readTaskbarSpace(monitor, stopping));
            }
            if (!stopping && PostMessageW(controller, TaskbarMessage, 0, reinterpret_cast<LPARAM>(update.get())))
                update.release();
        }
        std::unique_lock<std::mutex> lock(mutex);
        wake.wait_for(lock, std::chrono::milliseconds(1500), [&] { return stopping || seen != generation; });
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
}
void Application::apply(const State &value) {
    bool layoutChanged = value.active != state.active ||
                         settings.visibility(value.q) != settings.visibility(state.q);
    bool previewChanged = settingsWindow &&
                          settingsWindow->draft.visibility(value.q) != settingsWindow->draft.visibility(state.q);
    state = value;
    if (layoutChanged)
        rebuild();
    else
        for (auto &o : overlays)
            o->update(state);
    if (previewChanged)
        settingsWindow->styleChanged();
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
    case TaskbarMessage: {
        std::unique_ptr<TaskbarUpdate> update(reinterpret_cast<TaskbarUpdate *>(lp));
        if (!self->stopping && update->generation == self->generation && self->settings.avoidIcons) {
            self->taskbarSpaces = std::move(update->spaces);
            self->checkTaskbars();
        }
        return 0;
    }
    case DeepSeekMessage: {
        std::unique_ptr<DeepSeekUpdate> update(reinterpret_cast<DeepSeekUpdate *>(lp));
        if (!self->stopping) self->applyDeepSeek(*update);
        return 0;
    }
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
        if (lp == 3 && self->settingsWindow) self->settingsWindow->selectPage(3);
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
        self->checkHarness();
        self->checkTaskbars();
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
