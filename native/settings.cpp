#include "app.hpp"
#include <commdlg.h>
#include <windowsx.h>
namespace quota {
enum {
    NavAppearance = 100,
    NavGeneral,
    NavAdvanced,
    Save = 110,
    Cancel,
    Style = 200,
    Scale,
    HourColor,
    WeekColor,
    ResetColors,
    Letters,
    Remaining,
    Percent,
    Animate,
    Demo,
    Display = 220,
    Startup,
    Path = 240,
    Browse,
    Interval,
    Refresh,
    Exit,
    Uninstall
};
static std::wstring controlText(HWND w) {
    int n = GetWindowTextLengthW(w);
    std::wstring value(n + 1, L'\0');
    GetWindowTextW(w, value.data(), n + 1);
    value.resize(n);
    return value;
}
static void registerSettingsClass(const wchar_t *name, WNDPROC proc) {
    WNDCLASSEXW c{};
    c.cbSize = sizeof(c);
    c.hInstance = GetModuleHandleW(nullptr);
    c.lpfnWndProc = proc;
    c.lpszClassName = name;
    c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    c.hIcon = LoadIconW(c.hInstance, MAKEINTRESOURCEW(1));
    RegisterClassExW(&c);
}
static bool isToggle(int id) {
    return id == Letters || id == Remaining || id == Percent || id == Animate || id == Startup;
}
SettingsWindow::SettingsWindow(Application *a, HMONITOR requested)
    : app(a), draft(a->settings), startup(startupEnabled()) {
    auto all = monitors();
    for (auto &m : all)
        if (m.handle == requested || (!requested && m.primary))
            monitor = m;
    if (!monitor.handle && !all.empty())
        monitor = all.front();
    dpi = monitor.dpi;
    oldStyle = draft.styleIndex();
    registerSettingsClass(L"CodexQuotaBar.Native.Settings", procedure);
    registerSettingsClass(L"CodexQuotaBar.Native.Content", contentProcedure);
    registerSettingsClass(L"CodexQuotaBar.Native.Palette", paletteProcedure);
    window =
        CreateWindowExW(WS_EX_APPWINDOW, L"CodexQuotaBar.Native.Settings", L"Codex Quota Bar 设置",
                        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, monitor.work.left + 40, monitor.work.top + 40,
                        px(1000), px(730), nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (!window)
        throw std::runtime_error("Cannot create settings window");
    content = CreateWindowExW(0, L"CodexQuotaBar.Native.Content", L"",
                              WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN, 0, 0, 1, 1, window,
                              nullptr, GetModuleHandleW(nullptr), this);
    background = CreateSolidBrush(lightTheme() ? RGB(255, 255, 255) : RGB(32, 32, 32));
    sidebar = CreateSolidBrush(lightTheme() ? RGB(247, 247, 247) : RGB(26, 26, 26));
    font = CreateFontW(-px(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    createControls();
    selectPage(0);
    SetTimer(window, 1, 16, nullptr);
}
SettingsWindow::~SettingsWindow() {
    closePalette();
    if (window)
        DestroyWindow(window);
    if (font)
        DeleteObject(font);
    if (background)
        DeleteObject(background);
    if (sidebar)
        DeleteObject(sidebar);
}
HWND SettingsWindow::button(const wchar_t *text, int id, int p) {
    HWND parent = p < 0 ? window : content;
    HWND w =
        CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 1, 1,
                        parent, reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
    SetPropW(w, L"QuotaPage", reinterpret_cast<HANDLE>(INT_PTR(p + 2)));
    SendMessageW(w, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    controls.push_back(w);
    return w;
}
void SettingsWindow::createControls() {
    button(L"外观", NavAppearance, -1);
    button(L"常规", NavGeneral, -1);
    button(L"高级", NavAdvanced, -1);
    button(L"保存更改", Save, -1);
    button(L"取消", Cancel, -1);
    auto control = [&](const wchar_t *cls, const wchar_t *text, int id, int p, DWORD style) {
        HWND w = CreateWindowExW(cls == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, text,
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, 0, 0, 1, 1, content,
                                 reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
        SetPropW(w, L"QuotaPage", reinterpret_cast<HANDLE>(INT_PTR(p + 2)));
        SendMessageW(w, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        controls.push_back(w);
        return w;
    };
    styles = control(L"COMBOBOX", L"", Style, 0, CBS_DROPDOWNLIST | WS_VSCROLL);
    for (auto &n : StyleNames)
        SendMessageW(styles, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(n.c_str()));
    SendMessageW(styles, CB_SETCURSEL, draft.styleIndex(), 0);
    scale = control(TRACKBAR_CLASSW, L"显示大小", Scale, 0, TBS_NOTICKS | TBS_HORZ);
    SendMessageW(scale, TBM_SETRANGE, TRUE, MAKELPARAM(60, 140));
    SendMessageW(scale, TBM_SETPOS, TRUE, int(std::round(draft.scale * 100)));
    button(L"5 小时", HourColor, 0);
    button(L"每周", WeekColor, 0);
    button(L"默认颜色", ResetColors, 0);
    button(L"仪表中间显示 H 和 W", Letters, 0);
    button(L"在图标上展示余量", Remaining, 0);
    button(L"显示百分号", Percent, 0);
    button(L"启动与额度重置动画", Animate, 0);
    button(L"预览动画", Demo, 0);
    display = control(L"COMBOBOX", L"", Display, 1, CBS_DROPDOWNLIST | WS_VSCROLL);
    SendMessageW(display, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"所有屏幕"));
    SendMessageW(display, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"仅主屏幕"));
    int index = draft.displayMode == "primary" ? 1 : 0;
    auto all = monitors();
    for (size_t i = 0; i < all.size(); ++i) {
        auto name = all[i].name + (all[i].primary ? L"（主屏）" : L"");
        SendMessageW(display, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        if (draft.displayMode == "selected" && std::find(draft.displays.begin(), draft.displays.end(),
                                                         utf8(all[i].name)) != draft.displays.end())
            index = int(i) + 2;
    }
    SendMessageW(display, CB_SETCURSEL, index, 0);
    button(L"随 Windows 登录启动", Startup, 1);
    path = control(L"EDIT", draft.codexPath.c_str(), Path, 2, ES_AUTOHSCROLL);
    SendMessageW(path, EM_SETCUEBANNER, 0, reinterpret_cast<LPARAM>(L"自动查找 Codex CLI"));
    button(L"浏览…", Browse, 2);
    interval = control(L"COMBOBOX", L"", Interval, 2, CBS_DROPDOWNLIST);
    const wchar_t *labels[] = {L"15 秒", L"30 秒", L"1 分钟", L"2 分钟", L"5 分钟"};
    for (int i = 0; i < 5; ++i) {
        SendMessageW(interval, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(labels[i]));
        if (draft.interval == Intervals[i])
            SendMessageW(interval, CB_SETCURSEL, i, 0);
    }
    button(L"立即刷新", Refresh, 2);
    button(L"退出额度工具", Exit, 2);
    auto un = button(L"卸载程序", Uninstall, 2);
    EnableWindow(un, nativeInstalled(executable().parent_path()));
    styleChanged();
}
void SettingsWindow::styleChanged() {
    oldStyle = draft.styleIndex();
    EnableWindow(GetDlgItem(content, Letters), draft.style == "gauges");
    EnableWindow(GetDlgItem(content, Remaining), draft.style != "plain");
    EnableWindow(GetDlgItem(content, Demo), draft.pref().animate);
    previewStart = draft.pref().animate && motionEnabled() ? int64_t(GetTickCount64()) : -1;
    InvalidateRect(content, nullptr, FALSE);
    for (int id : {Letters, Remaining, Percent, Animate})
        InvalidateRect(GetDlgItem(content, id), nullptr, TRUE);
}
void SettingsWindow::layout() {
    RECT r;
    GetClientRect(window, &r);
    int w = r.right, h = r.bottom;
    MoveWindow(content, px(218), px(22), std::max(1, w - px(246)), std::max(1, h - px(100)), TRUE);
    RECT c;
    GetClientRect(content, &c);
    contentWidth = int(c.right * 96. / dpi);
    contentHeight = int(c.bottom * 96. / dpi);
    int needed = page == 0 ? 704 : page == 1 ? 420 : 540;
    scroll = std::clamp(scroll, 0, std::max(0, needed - contentHeight));
    SCROLLINFO si{sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS,   0,
                  needed - 1, UINT(std::max(1, contentHeight)), scroll,
                  0};
    SetScrollInfo(content, SB_VERT, &si, TRUE);
    auto place = [&](int id, double x, double y, double ww, double hh) {
        HWND ctrl = GetDlgItem(content, id);
        MoveWindow(ctrl, px(x), px(y - scroll), px(ww), px(hh), TRUE);
    };
    int right = contentWidth - 224;
    for (int i = 0; i < 3; ++i)
        MoveWindow(GetDlgItem(window, NavAppearance + i), px(15), px(82 + i * 43), px(166), px(38), TRUE);
    MoveWindow(GetDlgItem(window, Save), w - px(148), h - px(58), px(120), px(36), TRUE);
    MoveWindow(GetDlgItem(window, Cancel), w - px(256), h - px(58), px(96), px(36), TRUE);
    place(Style, right, 89, 210, 280);
    place(Scale, right - 24, 153, 180, 32);
    place(HourColor, contentWidth - 292, 212, 80, 32);
    place(WeekColor, contentWidth - 205, 212, 76, 32);
    place(ResetColors, contentWidth - 122, 212, 108, 32);
    place(Letters, contentWidth - 60, 386, 44, 26);
    place(Remaining, contentWidth - 60, 452, 44, 26);
    place(Percent, contentWidth - 60, 518, 44, 26);
    place(Animate, contentWidth - 60, 584, 44, 26);
    place(Demo, 0, 650, 122, 34);
    place(Display, right - 20, 89, 230, 300);
    place(Startup, contentWidth - 60, 155, 44, 26);
    place(Path, 0, 140, contentWidth - 112, 30);
    place(Browse, contentWidth - 100, 139, 86, 32);
    place(Interval, right + 60, 207, 150, 230);
    place(Refresh, contentWidth - 124, 273, 110, 34);
    place(Exit, contentWidth - 146, 339, 132, 34);
    place(Uninstall, contentWidth - 124, 405, 110, 34);
    InvalidateRect(window, nullptr, TRUE);
    InvalidateRect(content, nullptr, TRUE);
}
void SettingsWindow::selectPage(int index) {
    page = index;
    scroll = 0;
    closePalette();
    for (HWND w : controls) {
        int p = int(reinterpret_cast<INT_PTR>(GetPropW(w, L"QuotaPage"))) - 2;
        ShowWindow(w, p < 0 || p == page ? SW_SHOW : SW_HIDE);
    }
    layout();
}
void SettingsWindow::reveal(HMONITOR requested) {
    if (IsIconic(window))
        ShowWindow(window, SW_RESTORE);
    auto all = monitors();
    for (auto &m : all)
        if (m.handle == requested) {
            monitor = m;
            break;
        }
    int newDpi = monitor.dpi;
    if (newDpi != dpi) {
        dpi = newDpi;
        if (font)
            DeleteObject(font);
        font =
            CreateFontW(-px(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        for (HWND w : controls)
            SendMessageW(w, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    int width = std::min(px(1040), int(monitor.work.right - monitor.work.left) - px(24));
    int height = std::min(px(768), int(monitor.work.bottom - monitor.work.top) - px(24));
    int x = monitor.work.left + (monitor.work.right - monitor.work.left - width) / 2,
        y = monitor.work.top + (monitor.work.bottom - monitor.work.top - height) / 2;
    // Explicit visibility avoids inheriting SW_HIDE from a background listener's
    // STARTUPINFO on the first ShowWindow call.
    SetWindowPos(window, HWND_TOP, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    layout();
    BOOL dark = !lightTheme();
    DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
    SetForegroundWindow(window);
}
void SettingsWindow::draw(HDC dc, bool inner) {
    RECT rect;
    GetClientRect(inner ? content : window, &rect);
    if (rect.right <= 0 || rect.bottom <= 0)
        return;
    Surface image(rect.right, rect.bottom);
    {
        Graphics g(image.bitmap.get());
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        bool light = lightTheme();
        Color base = light ? Color(255, 255, 255, 255) : Color(255, 32, 32, 32),
              ink = light ? Color(255, 30, 30, 30) : Color(255, 240, 240, 240),
              muted = light ? Color(255, 110, 110, 110) : Color(255, 165, 165, 165),
              side = light ? Color(255, 247, 247, 247) : Color(255, 26, 26, 26);
        g.Clear(base);
        float d = dpi / 96.f;
        g.ScaleTransform(d, d);
        if (!inner) {
            SolidBrush b(side);
            g.FillRectangle(&b, RectF(0, 0, 196, float(rect.bottom / d)));
            drawText(g, L"设置", RectF(26, 22, 150, 44), 27, ink, true);
            drawText(g, L"Codex Quota Bar\n0.2.0", RectF(24, rect.bottom / d - 80, 155, 58), 11, muted);
            Pen line(light ? Color(255, 235, 235, 235) : Color(255, 55, 55, 55));
            g.DrawLine(&line, 218.f, rect.bottom / d - 78, rect.right / d - 28, rect.bottom / d - 78);
        } else {
            g.TranslateTransform(0, float(-scroll));
            const wchar_t *headings[] = {L"外观", L"常规", L"高级"};
            const wchar_t *subtitles[] = {L"调整任务栏组件的显示方式。",
                                          L"管理显示屏幕，以及应用的启动与退出。",
                                          L"配置额度读取来源，或手动刷新额度。"};
            drawText(g, headings[page], RectF(0, 0, float(contentWidth), 40), 25, ink, true);
            drawText(g, subtitles[page], RectF(0, 43, float(contentWidth), 26), 12, muted);
            auto row = [&](float y, const wchar_t *title, const wchar_t *description, float reserve = 240) {
                drawText(g, title, RectF(0, y, float(contentWidth) - reserve, 25), 14, ink);
                drawText(g, description, RectF(0, y + 25, float(contentWidth) - reserve, 25), 11, muted);
                Pen p(light ? Color(255, 237, 237, 237) : Color(255, 55, 55, 55));
                g.DrawLine(&p, 0.f, y + 59, float(contentWidth) - 14, y + 59);
            };
            if (page == 0) {
                row(82, L"任务栏样式", L"选择额度的呈现方式。");
                row(148, L"显示大小", L"整体缩放，过大时适配任务栏高度。");
                drawText(g, std::to_wstring(int(std::round(draft.scale * 100))) + L"%",
                         RectF(contentWidth - 70.f, 151, 56, 32), 13, muted, false, true);
                row(214, L"图标颜色", L"分别选择两种额度的颜色。", 310);
                fillRound(g, RectF(0, 286, contentWidth - 14.f, 62), 6, side);
                Quotas q;
                q.hour = WindowQuota{68, 0};
                q.week = WindowQuota{27, 0};
                auto layout = widgetLayout(draft, false, 96, 54);
                double t =
                    previewStart < 0 ? 1 : std::clamp((GetTickCount64() - previewStart) / 1200., 0., 1.);
                double progress = 1 - std::pow(1 - t, 3);
                drawWidget(g,
                           RectF((contentWidth - 14 - layout.width) / 2.f, 286 + (62 - layout.height) / 2.f,
                                 float(layout.width), float(layout.height)),
                           q, draft, false, light, progress, progress);
                drawText(g, L"预览使用模拟数据", RectF(0, 350, float(contentWidth), 24), 10, muted);
                row(380, L"仪表中间显示 H 和 W", L"H 表示 5 小时额度，W 表示周额度。", 110);
                row(446, L"在图标上展示余量", L"关闭后保持完整纯色图形；按样式分别保存。", 110);
                row(512, L"显示百分号", L"关闭后只显示数字；按样式分别保存。", 110);
                row(578, L"启动与额度重置动画", L"图形渐满；纯数字从 0 递增至真实值。", 110);
            } else if (page == 1) {
                row(82, L"显示屏幕", L"选择任务栏组件出现的位置。");
                row(148, L"随 Windows 登录启动", L"启动轻量监听器，自动跟随应用。", 110);
                row(214, L"跟随应用运行", L"任一应用运行时显示，两者完全退出后停止读取。", 80);
                drawText(g, L"自动", RectF(contentWidth - 80.f, 214, 65, 30), 13, muted, false, true);
                row(280, L"Pro 账户", L"识别为 Pro 时，仅显示周额度。", 80);
                drawText(g, L"自动", RectF(contentWidth - 80.f, 280, 65, 30), 13, muted, false, true);
                drawText(g, L"悬浮查看详情 · 右键打开设置 · 左键横向拖动",
                         RectF(0, 365, float(contentWidth), 32), 12, muted);
            } else {
                drawText(g, L"Codex CLI 路径", RectF(0, 82, float(contentWidth), 26), 14, ink);
                drawText(g, L"留空时自动查找已安装的 Codex。", RectF(0, 108, float(contentWidth), 26), 11,
                         muted);
                row(200, L"自动刷新间隔", L"保存后立即生效。");
                row(266, L"额度读取", L"读取失败时自动退避重试。");
                row(332, L"退出", L"停止显示与后台监听。");
                row(398, L"卸载",
                    nativeInstalled(executable().parent_path()) ? L"删除本工具及设置，卸载前会再次确认。"
                                                                : L"便携版退出后删除解压目录即可。");
                drawText(g, L"通过本机 Codex 读取，不保存登录信息。", RectF(0, 483, float(contentWidth), 32),
                         12, muted);
            }
        }
    }
    BitBlt(dc, 0, 0, rect.right, rect.bottom, image.dc, 0, 0, SRCCOPY);
}
void SettingsWindow::drawButton(const DRAWITEMSTRUCT &item) {
    int id = int(item.CtlID);
    Surface image(item.rcItem.right - item.rcItem.left, item.rcItem.bottom - item.rcItem.top);
    {
        Graphics g(image.bitmap.get());
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        bool light = lightTheme(), disabled = (item.itemState & ODS_DISABLED) != 0,
             pressed = (item.itemState & ODS_SELECTED) != 0;
        Color base = light ? Color(255, 255, 255, 255) : Color(255, 32, 32, 32),
              ink = light ? Color(255, 30, 30, 30) : Color(255, 240, 240, 240),
              side = light ? Color(255, 247, 247, 247) : Color(255, 26, 26, 26);
        if (id >= NavAppearance && id <= NavAdvanced)
            base = side;
        g.Clear(base);
        float d = dpi / 96.f;
        RectF area(1, 1, image.width - 2.f, image.height - 2.f);
        if (isToggle(id)) {
            bool enabled = id == Letters     ? draft.letters
                           : id == Remaining ? draft.pref().remaining
                           : id == Percent   ? draft.pref().percent
                           : id == Animate   ? draft.pref().animate
                                             : startup;
            Color track = enabled ? (light ? Color(255, 35, 35, 35) : Color(255, 225, 225, 225))
                                  : (light ? Color(255, 180, 180, 180) : Color(255, 85, 85, 85));
            if (disabled)
                track = Color(255, 205, 205, 205);
            fillRound(g, area, area.Height / 2, track);
            float r = area.Height - 6 * d;
            float left = enabled ? area.GetRight() - r - 3 * d : area.X + 3 * d;
            SolidBrush b(enabled && !light ? Color(255, 35, 35, 35) : Color(255, 255, 255, 255));
            g.FillEllipse(&b, RectF(left, area.Y + 3 * d, r, r));
        } else {
            Color fill = base, text = ink;
            bool nav = id >= NavAppearance && id <= NavAdvanced;
            if (nav && id - NavAppearance == page)
                fill = light ? Color(255, 230, 230, 230) : Color(255, 54, 54, 54);
            if (id == Save) {
                fill = ink;
                text = base;
            }
            if (id == HourColor || id == WeekColor) {
                fill = parseColor(id == HourColor ? draft.hourColor : draft.weekColor, ink);
                text = (fill.GetR() * 299 + fill.GetG() * 587 + fill.GetB() * 114) > 155000
                           ? Color(255, 15, 15, 15)
                           : Color(255, 255, 255, 255);
            }
            if (pressed)
                fill = light ? Color(255, 210, 210, 210) : Color(255, 75, 75, 75);
            fillRound(g, area, 8 * d, fill);
            if (!nav && id != Save && id != HourColor && id != WeekColor) {
                GraphicsPath p;
                roundPath(p, area, 8 * d);
                Pen line(light ? Color(255, 221, 221, 221) : Color(255, 72, 72, 72), 1);
                g.DrawPath(&line, &p);
            }
            if (disabled)
                text = Color(255, 150, 150, 150);
            RectF textArea = area;
            if (nav) {
                textArea.X += 12 * d;
                textArea.Width -= 18 * d;
            }
            drawText(g, controlText(item.hwndItem), textArea, 13 * d, text, id == Save, !nav);
        }
        if (item.itemState & ODS_FOCUS) {
            GraphicsPath p;
            roundPath(p, RectF(2, 2, image.width - 4.f, image.height - 4.f), 6 * d);
            Pen focus(Color(255, 0, 120, 215), 1);
            focus.SetDashStyle(DashStyleDot);
            g.DrawPath(&focus, &p);
        }
    }
    BitBlt(item.hDC, item.rcItem.left, item.rcItem.top, image.width, image.height, image.dc, 0, 0, SRCCOPY);
}
void SettingsWindow::command(int id, int notice) {
    if (loading)
        return;
    if (id != Style && notice != BN_CLICKED)
        return;
    if (id >= NavAppearance && id <= NavAdvanced) {
        selectPage(id - NavAppearance);
        return;
    }
    switch (id) {
    case Cancel:
        SendMessageW(window, WM_CLOSE, 0, 0);
        break;
    case Save:
        if (save())
            SendMessageW(window, WM_CLOSE, 0, 0);
        break;
    case Style:
        if (notice == CBN_SELCHANGE) {
            int n = int(SendMessageW(styles, CB_GETCURSEL, 0, 0));
            if (n >= 0 && n < 7) {
                draft.style = StyleIds[n];
                styleChanged();
            }
        }
        break;
    case Letters:
        draft.letters = !draft.letters;
        break;
    case Remaining:
        draft.preferences[draft.styleIndex()].remaining = !draft.pref().remaining;
        break;
    case Percent:
        draft.preferences[draft.styleIndex()].percent = !draft.pref().percent;
        break;
    case Animate:
        draft.preferences[draft.styleIndex()].animate = !draft.pref().animate;
        styleChanged();
        break;
    case Startup:
        startup = !startup;
        break;
    case Demo:
        if (motionEnabled())
            previewStart = GetTickCount64();
        break;
    case HourColor:
        openPalette(true);
        break;
    case WeekColor:
        openPalette(false);
        break;
    case ResetColors:
        draft.hourColor = "#219E86";
        draft.weekColor = "#648DE6";
        InvalidateRect(GetDlgItem(content, HourColor), nullptr, TRUE);
        InvalidateRect(GetDlgItem(content, WeekColor), nullptr, TRUE);
        break;
    case Browse: {
        wchar_t file[32768]{};
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = window;
        ofn.lpstrFile = file;
        ofn.nMaxFile = 32768;
        ofn.lpstrFilter = L"Codex CLI (codex.exe)\0codex.exe\0程序 (*.exe)\0*.exe\0\0";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&ofn))
            SetWindowTextW(path, file);
        break;
    }
    case Refresh:
        app->refresh();
        break;
    case Exit:
        app->stop();
        break;
    case Uninstall:
        if (nativeInstalled(executable().parent_path()) &&
            MessageBoxW(
                window,
                L"卸载本工具并删除设置、启动项和快捷方式？\n不会删除 ChatGPT、Codex 或它们的登录数据。",
                L"卸载 Codex Quota Bar", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) {
            if (launchUninstaller(window))
                app->stop();
            else
                MessageBoxW(window, L"无法启动卸载程序。", L"Codex Quota Bar", MB_OK | MB_ICONERROR);
        }
        break;
    }
    if (auto w = GetDlgItem(content, id))
        InvalidateRect(w, nullptr, TRUE);
    InvalidateRect(content, nullptr, FALSE);
}
bool SettingsWindow::save() {
    draft.codexPath = controlText(path);
    while (!draft.codexPath.empty() && iswspace(draft.codexPath.back()))
        draft.codexPath.pop_back();
    while (!draft.codexPath.empty() && iswspace(draft.codexPath.front()))
        draft.codexPath.erase(0, 1);
    if (!draft.codexPath.empty()) {
        try {
            findCodex(draft.codexPath);
        } catch (...) {
            MessageBoxW(window, L"请选择有效的 Codex CLI exe，或留空自动查找。", L"无法保存",
                        MB_OK | MB_ICONERROR);
            return false;
        }
    }
    int n = int(SendMessageW(interval, CB_GETCURSEL, 0, 0));
    if (n >= 0 && n < 5)
        draft.interval = Intervals[n];
    n = int(SendMessageW(display, CB_GETCURSEL, 0, 0));
    draft.displays.clear();
    if (n < 2)
        draft.displayMode = n == 1 ? "primary" : "all";
    else {
        auto all = monitors();
        if (n - 2 < int(all.size())) {
            draft.displayMode = "selected";
            draft.displays.push_back(utf8(all[n - 2].name));
        }
    }
    try {
        if (!setStartup(startup))
            throw std::runtime_error("Cannot save startup");
        draft.save();
        app->saved(draft);
        return true;
    } catch (...) {
        MessageBoxW(window, L"无法保存设置或启动项，请检查当前用户权限。", L"Codex Quota Bar",
                    MB_OK | MB_ICONERROR);
        return false;
    }
}
void SettingsWindow::capture(const fs::path &file) {
    RECT r;
    GetClientRect(window, &r);
    Surface image(r.right, r.bottom);
    draw(image.dc, false);
    struct Capture {
        HWND root;
        HDC dc;
    } capture{window, image.dc};
    EnumChildWindows(
        window,
        [](HWND child, LPARAM data) -> BOOL {
            auto &c = *reinterpret_cast<Capture *>(data);
            if (!IsWindowVisible(child))
                return TRUE;
            RECT bounds;
            GetClientRect(child, &bounds);
            POINT point{};
            MapWindowPoints(child, c.root, &point, 1);
            int saved = SaveDC(c.dc);
            HWND ancestor = GetParent(child);
            while (ancestor && ancestor != c.root) {
                RECT clip;
                GetClientRect(ancestor, &clip);
                MapWindowPoints(ancestor, c.root, reinterpret_cast<POINT *>(&clip), 2);
                IntersectClipRect(c.dc, clip.left, clip.top, clip.right, clip.bottom);
                ancestor = GetParent(ancestor);
            }
            IntersectClipRect(c.dc, point.x, point.y, point.x + bounds.right, point.y + bounds.bottom);
            SetViewportOrgEx(c.dc, point.x, point.y, nullptr);
            SendMessageW(child, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(c.dc), PRF_CLIENT | PRF_ERASEBKGND);
            RestoreDC(c.dc, saved);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&capture));
    for (int i = 0; i < image.width * image.height; ++i)
        image.pixels[i * 4 + 3] = 255;
    image.save(file);
}
void SettingsWindow::closePalette() {
    if (palette) {
        HWND old = palette;
        palette = nullptr;
        DestroyWindow(old);
    }
}
static const char *themes[] = {"#FFFFFF", "#000000", "#E7E6E6", "#44546A", "#4472C4",
                               "#ED7D31", "#FFC000", "#70AD47", "#2BBBAD", "#E74765"};
static const char *standards[] = {"#C00000", "#FF0000", "#FFC000", "#FFFF00", "#92D050",
                                  "#00B050", "#00B0F0", "#0070C0", "#002060", "#7030A0"};
static Color swatch(int index) {
    if (index >= 60)
        return parseColor(standards[index - 60], Color::Black);
    int col = index % 10, row = index / 10;
    Color base = parseColor(themes[col], Color::Black);
    if (row == 0)
        return base;
    if (col < 3) {
        int grays[3][5] = {{242, 217, 191, 166, 128}, {128, 89, 64, 38, 13}, {208, 175, 135, 89, 38}};
        BYTE gray = BYTE(grays[col][row - 1]);
        return Color(255, gray, gray, gray);
    }
    double amounts[] = {0, .8, .6, .4, -.25, -.5};
    double a = amounts[row];
    auto mix = [&](BYTE c) { return BYTE(std::round(a >= 0 ? c + (255 - c) * a : c * (1 + a))); };
    return Color(255, mix(base.GetR()), mix(base.GetG()), mix(base.GetB()));
}
void SettingsWindow::openPalette(bool hour) {
    closePalette();
    colorHour = hour;
    selectedColor = parseColor(hour ? draft.hourColor : draft.weekColor, Color::Black);
    RECT r;
    GetWindowRect(GetDlgItem(content, hour ? HourColor : WeekColor), &r);
    auto m = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(m, &info);
    int width = px(294), height = px(312),
        x = std::clamp(int(r.left), int(info.rcWork.left),
                       std::max(int(info.rcWork.left), int(info.rcWork.right) - width)),
        y = std::clamp(int(r.bottom + 4), int(info.rcWork.top),
                       std::max(int(info.rcWork.top), int(info.rcWork.bottom) - height));
    palette = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"CodexQuotaBar.Native.Palette", L"颜色",
                              WS_POPUP | WS_BORDER, x, y, width, height, window, nullptr,
                              GetModuleHandleW(nullptr), this);
    SetPropW(palette, L"Selection", reinterpret_cast<HANDLE>(INT_PTR(-1)));
    ShowWindow(palette, SW_SHOW);
    SetFocus(palette);
}
LRESULT CALLBACK SettingsWindow::paletteProcedure(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    auto self = reinterpret_cast<SettingsWindow *>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        self = static_cast<SettingsWindow *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
        SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self)
        return DefWindowProcW(w, msg, wp, lp);
    auto choose = [&](int index) {
        if (index == -3) {
            self->closePalette();
            return;
        }
        if (index == -2) {
            CHOOSECOLORW cc{};
            static COLORREF custom[16]{};
            auto c = self->selectedColor;
            cc.lStructSize = sizeof(cc);
            cc.hwndOwner = self->window;
            cc.rgbResult = RGB(c.GetR(), c.GetG(), c.GetB());
            cc.lpCustColors = custom;
            cc.Flags = CC_FULLOPEN | CC_RGBINIT;
            self->closePalette();
            if (!ChooseColorW(&cc))
                return;
            self->selectedColor =
                Color(255, GetRValue(cc.rgbResult), GetGValue(cc.rgbResult), GetBValue(cc.rgbResult));
        } else
            self->selectedColor =
                index < 0 ? parseColor(self->colorHour ? "#219E86" : "#648DE6", Color::Black) : swatch(index);
        if (self->colorHour)
            self->draft.hourColor = hexColor(self->selectedColor);
        else
            self->draft.weekColor = hexColor(self->selectedColor);
        self->closePalette();
        InvalidateRect(self->content, nullptr, FALSE);
        InvalidateRect(GetDlgItem(self->content, self->colorHour ? HourColor : WeekColor), nullptr, TRUE);
    };
    switch (msg) {
    case WM_KILLFOCUS:
        PostMessageW(self->window, WM_APP + 20, 0, 0);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_KEYDOWN: {
        int index = int(reinterpret_cast<INT_PTR>(GetPropW(w, L"Selection")));
        if (wp == VK_ESCAPE) {
            self->closePalette();
            return 0;
        }
        if (wp == VK_RETURN || wp == VK_SPACE) {
            choose(index);
            return 0;
        }
        if (wp == VK_LEFT)
            --index;
        if (wp == VK_RIGHT)
            ++index;
        if (wp == VK_UP)
            index -= 10;
        if (wp == VK_DOWN)
            index += 10;
        index = std::clamp(index, -2, 69);
        SetPropW(w, L"Selection", reinterpret_cast<HANDLE>(INT_PTR(index)));
        InvalidateRect(w, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONUP: {
        int x = int(GET_X_LPARAM(lp) * 96. / self->dpi), y = int(GET_Y_LPARAM(lp) * 96. / self->dpi);
        if (y < 38) {
            choose(-1);
            return 0;
        }
        if (y >= 278) {
            choose(-2);
            return 0;
        }
        if (x >= 14 && x < 284) {
            int col = (x - 14) / 27;
            if (col < 10 && x < 14 + col * 27 + 21) {
                if (y >= 73 && y < 211) {
                    int row = (y - 73) / 23;
                    if (row < 6 && y < 73 + row * 23 + 21)
                        choose(row * 10 + col);
                } else if (y >= 248 && y < 269)
                    choose(60 + col);
            }
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT r;
        GetClientRect(w, &r);
        Surface image(r.right, r.bottom);
        {
            Graphics g(image.bitmap.get());
            bool light = lightTheme();
            Color base = light ? Color(255, 255, 255, 255) : Color(255, 32, 32, 32),
                  ink = light ? Color(255, 30, 30, 30) : Color(255, 240, 240, 240),
                  header = light ? Color(255, 243, 244, 246) : Color(255, 46, 46, 46);
            g.Clear(base);
            g.ScaleTransform(self->dpi / 96.f, self->dpi / 96.f);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            fillRound(g, RectF(8, 8, 278, 30), 5, header);
            drawText(g, L"自动", RectF(44, 8, 235, 30), 14, ink);
            SolidBrush automatic(parseColor(self->colorHour ? "#219E86" : "#648DE6", Color::Black));
            g.FillRectangle(&automatic, RectF(18, 14, 18, 18));
            for (auto pair : {std::make_pair(41, L"主题颜色"), std::make_pair(216, L"标准色")}) {
                SolidBrush b(header);
                g.FillRectangle(&b, RectF(8, float(pair.first), 278, 25));
                drawText(g, pair.second, RectF(16, float(pair.first), 264, 25), 13, ink);
            }
            int selection = int(reinterpret_cast<INT_PTR>(GetPropW(w, L"Selection")));
            for (int i = 0; i < 70; ++i) {
                int x = 14 + (i % 10) * 27, y = i < 60 ? 73 + (i / 10) * 23 : 248;
                auto c = swatch(i);
                SolidBrush b(c);
                g.FillRectangle(&b, RectF(float(x), float(y), 21, 21));
                Pen pen(hexColor(c) == hexColor(self->selectedColor) || selection == i
                            ? Color(255, 0, 120, 215)
                            : Color(255, 216, 218, 223),
                        selection == i ? 2 : 1);
                g.DrawRectangle(&pen, RectF(float(x), float(y), 21, 21));
            }
            drawText(g, L"其他颜色…", RectF(16, 278, 264, 28), 13, ink);
        }
        BitBlt(dc, 0, 0, r.right, r.bottom, image.dc, 0, 0, SRCCOPY);
        EndPaint(w, &ps);
        return 0;
    }
    }
    return DefWindowProcW(w, msg, wp, lp);
}
LRESULT CALLBACK SettingsWindow::contentProcedure(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    auto self = reinterpret_cast<SettingsWindow *>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        self = static_cast<SettingsWindow *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
        SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self)
        return DefWindowProcW(w, msg, wp, lp);
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PRINTCLIENT:
        self->draw(reinterpret_cast<HDC>(wp), true);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT p;
        auto dc = BeginPaint(w, &p);
        self->draw(dc, true);
        EndPaint(w, &p);
        return 0;
    }
    case WM_COMMAND:
        self->command(LOWORD(wp), HIWORD(wp));
        return 0;
    case WM_DRAWITEM:
        self->drawButton(*reinterpret_cast<DRAWITEMSTRUCT *>(lp));
        return TRUE;
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lp) == self->scale) {
            self->draft.scale = SendMessageW(self->scale, TBM_GETPOS, 0, 0) / 100.;
            InvalidateRect(w, nullptr, FALSE);
        }
        return 0;
    case WM_VSCROLL: {
        SCROLLINFO si{sizeof(si), SIF_ALL};
        GetScrollInfo(w, SB_VERT, &si);
        int value = self->scroll;
        switch (LOWORD(wp)) {
        case SB_LINEUP:
            value -= 24;
            break;
        case SB_LINEDOWN:
            value += 24;
            break;
        case SB_PAGEUP:
            value -= int(si.nPage);
            break;
        case SB_PAGEDOWN:
            value += int(si.nPage);
            break;
        case SB_THUMBTRACK:
            value = si.nTrackPos;
            break;
        }
        self->scroll = std::clamp(value, 0, std::max(0, si.nMax - int(si.nPage) + 1));
        self->layout();
        return 0;
    }
    case WM_MOUSEWHEEL:
        self->scroll -= GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * 48;
        self->layout();
        return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wp);
        SetTextColor(dc, lightTheme() ? RGB(30, 30, 30) : RGB(240, 240, 240));
        SetBkColor(dc, lightTheme() ? RGB(255, 255, 255) : RGB(32, 32, 32));
        return reinterpret_cast<LRESULT>(self->background);
    }
    }
    return DefWindowProcW(w, msg, wp, lp);
}
LRESULT CALLBACK SettingsWindow::procedure(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    auto self = reinterpret_cast<SettingsWindow *>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        self = static_cast<SettingsWindow *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
        SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self)
        return DefWindowProcW(w, msg, wp, lp);
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PRINTCLIENT:
        self->draw(reinterpret_cast<HDC>(wp), false);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT p;
        auto dc = BeginPaint(w, &p);
        self->draw(dc, false);
        EndPaint(w, &p);
        return 0;
    }
    case WM_SIZE:
        if (self->content)
            self->layout();
        return 0;
    case WM_GETMINMAXINFO: {
        auto info = reinterpret_cast<MINMAXINFO *>(lp);
        info->ptMinTrackSize = {self->px(790), self->px(550)};
        return 0;
    }
    case WM_DPICHANGED: {
        self->dpi = HIWORD(wp);
        if (self->font)
            DeleteObject(self->font);
        self->font = CreateFontW(-self->px(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                                 L"Microsoft YaHei UI");
        for (HWND ctrl : self->controls)
            SendMessageW(ctrl, WM_SETFONT, reinterpret_cast<WPARAM>(self->font), TRUE);
        auto r = reinterpret_cast<RECT *>(lp);
        SetWindowPos(w, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        self->layout();
        return 0;
    }
    case WM_TIMER:
        if (self->previewStart >= 0) {
            if (GetTickCount64() - self->previewStart >= 1200)
                self->previewStart = -1;
            InvalidateRect(self->content, nullptr, FALSE);
        }
        return 0;
    case WM_APP + 20:
        if (self->palette && GetFocus() != self->palette)
            self->closePalette();
        return 0;
    case WM_DRAWITEM:
        self->drawButton(*reinterpret_cast<DRAWITEMSTRUCT *>(lp));
        return TRUE;
    case WM_COMMAND:
        self->command(LOWORD(wp), HIWORD(wp));
        return 0;
    case WM_CLOSE:
        self->closePalette();
        ShowWindow(w, SW_HIDE);
        self->draft = self->app->settings;
        self->closing = true;
        PostMessageW(self->app->controller, WM_APP + 10, 0, 0);
        return 0;
    case WM_SETTINGCHANGE:
        if (self->background)
            DeleteObject(self->background);
        self->background = CreateSolidBrush(lightTheme() ? RGB(255, 255, 255) : RGB(32, 32, 32));
        {
            BOOL dark = !lightTheme();
            DwmSetWindowAttribute(w, 20, &dark, sizeof(dark));
        }
        InvalidateRect(w, nullptr, TRUE);
        RedrawWindow(self->content, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}
} // namespace quota
