#include "app.hpp"
#include <commdlg.h>
#include <windowsx.h>
namespace quota {
enum {
    NavAppearance = 100,
    NavGeneral,
    NavAdvanced,
    NavDeepSeek,
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
    ShowHour,
    ShowWeek,
    ResetQuotas,
    AvoidIcons,
    Path = 240,
    Browse,
    Interval,
    Refresh,
    Exit,
    Uninstall,
    OpenFolder,
    BackgroundOpacity,
    IconOpacity,
    DeepSeekEnabled = 260,
    DeepSeekToken = 262,
    DeepSeekInterval,
    DeepSeekRefresh,
    DeepSeekClear,
    DeepSeekPlatform,
    DeepSeekHelp,
    DeepSeekAlwaysShow
};
static std::wstring controlText(HWND w) {
    int n = GetWindowTextLengthW(w);
    std::wstring value(n + 1, L'\0');
    GetWindowTextW(w, value.data(), n + 1);
    value.resize(n);
    return value;
}
enum class ControlSkin : DWORD_PTR { Button, Edit, Combo, Slider };

static void paintControl(HWND w, HDC dc, ControlSkin skin) {
    RECT rect{};
    GetClientRect(w, &rect);
    if (rect.right < 4 || rect.bottom < 4) return;
    float d = GetDpiForWindow(w) / 96.f;
    bool focused = GetFocus() == w;
    if (skin == ControlSkin::Edit) {
        wchar_t cue[256]{};
        if (!focused && !GetWindowTextLengthW(w) &&
            SendMessageW(w, EM_GETCUEBANNER, reinterpret_cast<WPARAM>(cue), std::size(cue))) {
            HBRUSH fill = CreateSolidBrush(vibrancy::inputBrush);
            FillRect(dc, &rect, fill);
            DeleteObject(fill);
            int saved = SaveDC(dc);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(158, 158, 160));
            if (auto font = SendMessageW(w, WM_GETFONT, 0, 0))
                SelectObject(dc, reinterpret_cast<HFONT>(font));
            RECT label = rect;
            InflateRect(&label, -int(8 * d), 0);
            DrawTextW(dc, cue, -1, &label, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            RestoreDC(dc, saved);
        }
        HBRUSH edge = CreateSolidBrush(focused ? RGB(163, 190, 255) : RGB(51, 51, 53));
        FrameRect(dc, &rect, edge);
        DeleteObject(edge);
        return;
    }
    Surface surface(rect.right, rect.bottom);
    {
        Graphics g(surface.bitmap.get());
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.Clear(vibrancy::mid);
        RectF bounds(.5f, .5f, rect.right - 1.f, rect.bottom - 1.f);
        if (skin == ControlSkin::Combo) {
            drawPanel(g, bounds, 6 * d, vibrancy::deep);
            drawUiText(g, controlText(w), RectF(8 * d, 0, rect.right - 32 * d, float(rect.bottom)),
                       15 * d, vibrancy::ink);
            float x = rect.right - 13 * d, y = rect.bottom / 2.f;
            Pen chevron(vibrancy::secondary, 1);
            PointF arrow[] = {{x - 4 * d, y - 2 * d}, {x, y + 2 * d}, {x + 4 * d, y - 2 * d}};
            g.DrawLines(&chevron, arrow, 3);
        } else {
            RECT channel{}, thumb{};
            SendMessageW(w, TBM_GETCHANNELRECT, 0, reinterpret_cast<LPARAM>(&channel));
            SendMessageW(w, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumb));
            float y = (thumb.top + thumb.bottom) / 2.f;
            fillRound(g, RectF(float(channel.left), y - 2 * d, float(channel.right - channel.left), 4 * d),
                      2 * d, vibrancy::deep);
            float x = (thumb.left + thumb.right) / 2.f;
            drawPanel(g, RectF(x - 5 * d, y - 7 * d, 10 * d, 14 * d), 3 * d, vibrancy::surface);
            Pen mark(vibrancy::secondary, 1);
            g.DrawLine(&mark, x, y - 3 * d, x, y + 3 * d);
        }
        if (focused) {
            GraphicsPath path;
            roundPath(path, bounds, 6 * d);
            Pen edge(vibrancy::focus, 1);
            g.DrawPath(&edge, &path);
        }
    }
    BitBlt(dc, 0, 0, surface.width, surface.height, surface.dc, 0, 0, SRCCOPY);
}

static LRESULT CALLBACK controlSkin(HWND w, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) {
    auto skin = static_cast<ControlSkin>(data);
    if (msg == WM_NCDESTROY) {
        RemovePropW(w, L"VibrancyHover");
        RemoveWindowSubclass(w, controlSkin, id);
    } else if (msg == WM_MOUSEMOVE && skin == ControlSkin::Button && !GetPropW(w, L"VibrancyHover")) {
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, w, 0};
        if (TrackMouseEvent(&track)) {
            SetPropW(w, L"VibrancyHover", reinterpret_cast<HANDLE>(1));
            InvalidateRect(w, nullptr, FALSE);
        }
    } else if (msg == WM_MOUSELEAVE && skin == ControlSkin::Button) {
        RemovePropW(w, L"VibrancyHover");
        InvalidateRect(w, nullptr, FALSE);
    } else if (msg == WM_PAINT && (skin == ControlSkin::Combo || skin == ControlSkin::Slider)) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        paintControl(w, dc, skin);
        EndPaint(w, &ps);
        return 0;
    } else if (msg == WM_PRINTCLIENT && skin != ControlSkin::Button) {
        if (skin == ControlSkin::Edit) DefSubclassProc(w, msg, wp, lp);
        paintControl(w, reinterpret_cast<HDC>(wp), skin);
        return 0;
    } else if (msg == WM_PAINT && skin == ControlSkin::Edit) {
        auto result = DefSubclassProc(w, msg, wp, lp);
        HDC dc = GetDC(w);
        if (dc) {
            paintControl(w, dc, skin);
            ReleaseDC(w, dc);
        }
        return result;
    }
    auto result = DefSubclassProc(w, msg, wp, lp);
    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS) InvalidateRect(w, nullptr, FALSE);
    return result;
}

static void drawComboOption(const DRAWITEMSTRUCT &item) {
    Graphics g(item.hDC);
    RectF bounds(float(item.rcItem.left), float(item.rcItem.top),
                 float(item.rcItem.right - item.rcItem.left), float(item.rcItem.bottom - item.rcItem.top));
    SolidBrush bg(item.itemState & ODS_SELECTED ? vibrancy::surface : vibrancy::deep);
    g.FillRectangle(&bg, bounds);
    if (item.itemID == UINT(-1)) return;
    auto index = item.itemID;
    auto length = SendMessageW(item.hwndItem, CB_GETLBTEXTLEN, index, 0);
    if (length == CB_ERR) return;
    std::wstring label(size_t(length) + 1, L'\0');
    SendMessageW(item.hwndItem, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(label.data()));
    label.resize(size_t(length));
    float d = GetDpiForWindow(item.hwndItem) / 96.f;
    bounds.X += 8 * d;
    bounds.Width -= 12 * d;
    drawUiText(g, label, bounds, 15 * d, vibrancy::ink);
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
    return id == Letters || id == Remaining || id == Percent || id == Animate || id == Startup ||
           id == ShowHour || id == ShowWeek || id == DeepSeekEnabled || id == DeepSeekAlwaysShow || id == AvoidIcons;
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
    registerSettingsClass(L"CodexQuotaBar.Native.Settings", procedure);
    registerSettingsClass(L"CodexQuotaBar.Native.Content", contentProcedure);
    registerSettingsClass(L"CodexQuotaBar.Native.Viewport", viewportProcedure);
    registerSettingsClass(L"CodexQuotaBar.Native.Palette", paletteProcedure);
    window =
        CreateWindowExW(WS_EX_APPWINDOW, L"CodexQuotaBar.Native.Settings", L"Codex Quota Bar 设置",
                        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, monitor.work.left + 40, monitor.work.top + 40,
                        px(1000), px(730), nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (!window)
        throw std::runtime_error("Cannot create settings window");
    viewport = CreateWindowExW(WS_EX_COMPOSITED, L"CodexQuotaBar.Native.Viewport", L"",
                              WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN, 0, 0, 1, 1, window,
                              nullptr, GetModuleHandleW(nullptr), this);
    content = CreateWindowExW(0, L"CodexQuotaBar.Native.Content", L"",
                              WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 1, 1, viewport,
                              nullptr, GetModuleHandleW(nullptr), this);
    if (!viewport || !content) throw std::runtime_error("Cannot create settings viewport");
    background = CreateSolidBrush(vibrancy::inputBrush);
    font = CreateFontW(-px(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
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
}
HWND SettingsWindow::button(const wchar_t *text, int id, int p) {
    HWND parent = p < 0 ? window : content;
    HWND w =
        CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 1, 1,
                        parent, reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
    SetPropW(w, L"QuotaPage", reinterpret_cast<HANDLE>(INT_PTR(p + 2)));
    SendMessageW(w, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    SetWindowSubclass(w, controlSkin, 1, DWORD_PTR(ControlSkin::Button));
    controls.push_back(w);
    return w;
}
void SettingsWindow::createControls() {
    button(L"外观", NavAppearance, -1);
    button(L"常规", NavGeneral, -1);
    button(L"高级", NavAdvanced, -1);
    button(L"DeepSeek 相关设置", NavDeepSeek, -1);
    button(L"保存更改", Save, -1);
    button(L"取消", Cancel, -1);
    auto control = [&](const wchar_t *cls, const wchar_t *text, int id, int p, DWORD style) {
        bool combo = std::wstring_view(cls) == L"COMBOBOX";
        bool edit = std::wstring_view(cls) == L"EDIT";
        if (combo) style |= CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;
        HWND w = CreateWindowExW(0, cls, text,
                                 WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | style, 0, 0, 1, 1, content,
                                 reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
        SetPropW(w, L"QuotaPage", reinterpret_cast<HANDLE>(INT_PTR(p + 2)));
        SendMessageW(w, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        if (combo) {
            SendMessageW(w, CB_SETITEMHEIGHT, WPARAM(-1), px(24));
            SendMessageW(w, CB_SETITEMHEIGHT, 0, px(26));
        }
        if (edit) SendMessageW(w, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(px(8), px(8)));
        SetWindowSubclass(w, controlSkin, 1, DWORD_PTR(combo ? ControlSkin::Combo :
                                                         edit ? ControlSkin::Edit : ControlSkin::Slider));
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
    opacitySlider = control(TRACKBAR_CLASSW, L"底色不透明度", BackgroundOpacity, 0, TBS_NOTICKS | TBS_HORZ);
    SendMessageW(opacitySlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessageW(opacitySlider, TBM_SETPOS, TRUE, draft.backgroundOpacity);
    iconOpacitySlider = control(TRACKBAR_CLASSW, L"Codex 图标不透明度", IconOpacity, 0, TBS_NOTICKS | TBS_HORZ);
    SendMessageW(iconOpacitySlider, TBM_SETRANGE, TRUE, MAKELPARAM(10, 100));
    SendMessageW(iconOpacitySlider, TBM_SETPOS, TRUE, draft.iconOpacity);
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
    button(L"显示 5 小时额度", ShowHour, 1);
    button(L"显示周额度", ShowWeek, 1);
    button(L"恢复套餐默认", ResetQuotas, 1);
    button(L"图标自动避让", AvoidIcons, 1);
    button(L"自动跟随 DeepSeekHarness", DeepSeekEnabled, 3);
    button(L"未启动时也显示余额", DeepSeekAlwaysShow, 3);
    deepSeekToken = control(L"EDIT", L"", DeepSeekToken, 3, ES_PASSWORD | ES_AUTOHSCROLL);
    SendMessageW(deepSeekToken, EM_SETLIMITTEXT, 2560, 0);
    bool hasToken = !app->testing && !deepSeekCredential().empty();
    SendMessageW(deepSeekToken, EM_SETCUEBANNER, 0, reinterpret_cast<LPARAM>(hasToken ? L"已保存；留空保持不变" : L"粘贴平台 userToken；输入将被隐藏"));
    deepSeekInterval = control(L"COMBOBOX", L"", DeepSeekInterval, 3, CBS_DROPDOWNLIST);
    const wchar_t *deepIntervals[] = {L"1 分钟", L"2 分钟", L"5 分钟"};
    for (auto label : deepIntervals) SendMessageW(deepSeekInterval, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    SendMessageW(deepSeekInterval, CB_SETCURSEL, draft.deepSeekInterval == 120 ? 1 : draft.deepSeekInterval == 300 ? 2 : 0, 0);
    button(L"立即刷新", DeepSeekRefresh, 3);
    button(L"清除凭证", DeepSeekClear, 3);
    button(L"打开 DeepSeek 平台", DeepSeekPlatform, 3);
    button(L"凭证填写说明", DeepSeekHelp, 3);
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
    button(L"卸载程序", Uninstall, 2);
    button(L"打开文件夹", OpenFolder, 2);
    styleChanged();
}
void SettingsWindow::styleChanged() {
    EnableWindow(GetDlgItem(content, Letters), draft.style == "gauges");
    EnableWindow(GetDlgItem(content, Remaining), draft.style != "plain");
    EnableWindow(GetDlgItem(content, Demo), draft.pref().animate);
    previewStart = draft.pref().animate && motionEnabled() ? int64_t(GetTickCount64()) : -1;
    InvalidateRect(content, nullptr, FALSE);
    for (int id : {Letters, Remaining, Percent, Animate, ShowHour, ShowWeek})
        InvalidateRect(GetDlgItem(content, id), nullptr, TRUE);
}
Settings SettingsWindow::previewSettings() const {
    auto preview = draft;
    auto visible = draft.visibility(app->state.q);
    preview.showHour = visible.hour;
    preview.showWeek = visible.week;
    return preview;
}
void SettingsWindow::layout() {
    if (layingOut) return;
    layingOut = true;
    struct ResetLayout { bool &flag; ~ResetLayout() { flag = false; } } reset{layingOut};
    // Keep inactive pages hidden and stationary. Suppressing only the parent's
    // redraw does not stop native child controls from repainting during moves.
    for (HWND ctrl : controls) {
        int ownerPage = int(reinterpret_cast<INT_PTR>(GetPropW(ctrl, L"QuotaPage"))) - 2;
        if (ownerPage >= 0 && ownerPage != page && (GetWindowLongPtrW(ctrl, GWL_STYLE) & WS_VISIBLE)) {
            SendMessageW(ctrl, WM_CANCELMODE, 0, 0);
            SetWindowPos(ctrl, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_HIDEWINDOW | SWP_NOREDRAW);
        }
    }
    RECT r;
    GetClientRect(window, &r);
    int w = r.right, h = r.bottom;
    SetWindowPos(viewport, nullptr, px(218), px(22), std::max(1, w - px(246)), std::max(1, h - px(100)),
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS);
    RECT c;
    GetClientRect(viewport, &c);
    contentWidth = int(c.right * 96. / dpi);
    contentHeight = int(c.bottom * 96. / dpi);
    int needed = page == 0 ? 836 : page == 1 ? 620 : page == 3 ? 608 : 606;
    scroll = std::clamp(scroll, 0, std::max(0, needed - contentHeight));
    SCROLLINFO si{sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS,   0,
                  needed - 1, UINT(std::max(1, contentHeight)), scroll,
                  0};
    SetScrollInfo(viewport, SB_VERT, &si, TRUE);
    // Scrollbar visibility can change the usable width between pages.
    GetClientRect(viewport, &c);
    contentWidth = int(c.right * 96. / dpi);
    SetWindowPos(content, nullptr, 0, -px(scroll), c.right, std::max<int>(c.bottom, px(needed)),
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS);
    struct Placement { HWND window; int x, y, width, height; };
    std::vector<Placement> placements;
    auto place = [&](int id, double x, double y, double ww, double hh) {
        HWND ctrl = GetDlgItem(content, id);
        int ownerPage = int(reinterpret_cast<INT_PTR>(GetPropW(ctrl, L"QuotaPage"))) - 2;
        if (ownerPage == page) placements.push_back({ctrl, px(x), px(y), px(ww), px(hh)});
    };
    int right = contentWidth - 224;
    for (int i = 0; i < 4; ++i)
        SetWindowPos(GetDlgItem(window, NavAppearance + i), nullptr, px(15), px(82 + i * 43), px(166), px(38),
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS);
    SetWindowPos(GetDlgItem(window, Save), nullptr, w - px(148), h - px(58), px(120), px(36),
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS);
    SetWindowPos(GetDlgItem(window, Cancel), nullptr, w - px(256), h - px(58), px(96), px(36),
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS);
    place(Style, right, 89, 210, 280);
    place(Scale, right - 24, 153, 180, 32);
    place(HourColor, contentWidth - 292, 212, 80, 32);
    place(WeekColor, contentWidth - 205, 212, 76, 32);
    place(ResetColors, contentWidth - 122, 212, 108, 32);
    place(Letters, contentWidth - 60, 518, 44, 26);
    place(Remaining, contentWidth - 60, 584, 44, 26);
    place(Percent, contentWidth - 60, 650, 44, 26);
    place(Animate, contentWidth - 60, 716, 44, 26);
    place(BackgroundOpacity, right - 24, 279, 180, 32);
    place(IconOpacity, right - 24, 345, 180, 32);
    place(Demo, 0, 782, 122, 34);
    place(Display, right - 20, 89, 230, 300);
    place(Startup, contentWidth - 60, 155, 44, 26);
    place(ShowHour, contentWidth - 60, 221, 44, 26);
    place(ShowWeek, contentWidth - 60, 287, 44, 26);
    place(ResetQuotas, contentWidth - 154, 353, 140, 34);
    place(AvoidIcons, contentWidth - 60, 419, 44, 26);
    place(Path, 0, 140, contentWidth - 112, 30);
    place(Browse, contentWidth - 100, 139, 86, 32);
    place(Interval, right + 60, 207, 150, 230);
    place(Refresh, contentWidth - 124, 273, 110, 34);
    place(Exit, contentWidth - 146, 339, 132, 34);
    place(Uninstall, contentWidth - 124, 405, 110, 34);
    place(OpenFolder, contentWidth - 146, 471, 132, 34);
    place(DeepSeekEnabled, contentWidth - 60, 89, 44, 26);
    place(DeepSeekAlwaysShow, contentWidth - 60, 155, 44, 26);
    place(DeepSeekToken, 0, 271, contentWidth - 14, 30);
    place(DeepSeekInterval, contentWidth - 164, 329, 150, 220);
    place(DeepSeekRefresh, contentWidth - 124, 395, 110, 34);
    place(DeepSeekClear, contentWidth - 124, 461, 110, 34);
    place(DeepSeekPlatform, contentWidth - 194, 209, 180, 28);
    place(DeepSeekHelp, contentWidth - 164, 241, 150, 26);
    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS | SWP_SHOWWINDOW;
    HDWP batch = BeginDeferWindowPos(int(placements.size()));
    for (auto &p : placements)
        if (batch) batch = DeferWindowPos(batch, p.window, nullptr, p.x, p.y, p.width, p.height, flags);
    bool applied = batch && EndDeferWindowPos(batch);
    if (!applied)
        for (auto &p : placements) SetWindowPos(p.window, nullptr, p.x, p.y, p.width, p.height, flags);
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
}
void SettingsWindow::scrollTo(int value) {
    SCROLLINFO si{sizeof(si), SIF_RANGE | SIF_PAGE};
    GetScrollInfo(viewport, SB_VERT, &si);
    value = std::clamp(value, 0, std::max(0, si.nMax - int(si.nPage) + 1));
    if (value == scroll) return;
    scroll = value;
    SCROLLINFO position{sizeof(position), SIF_POS}; position.nPos = scroll;
    SetScrollInfo(viewport, SB_VERT, &position, TRUE);
    // One coherent canvas moves inside a stationary composited viewport.
    // Native controls keep their own coordinates and full unclipped buffers.
    SetWindowPos(content, nullptr, 0, -px(scroll), 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS);
    RedrawWindow(viewport, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
}
void SettingsWindow::selectPage(int index) {
    page = index;
    scroll = 0;
    wheelRemainder = 0;
    closePalette();
    for (HWND combo : {styles, display, interval, deepSeekInterval}) SendMessageW(combo, CB_SHOWDROPDOWN, FALSE, 0);
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
            CreateFontW(-px(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
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
    BOOL dark = TRUE;
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
        auto base = inner ? vibrancy::mid : vibrancy::surface;
        auto ink = vibrancy::ink, muted = vibrancy::secondary, side = vibrancy::deep;
        g.Clear(base);
        float d = dpi / 96.f;
        g.ScaleTransform(d, d);
        if (!inner) {
            SolidBrush b(side);
            g.FillRectangle(&b, RectF(0, 0, 196, float(rect.bottom / d)));
            drawHeading(g, L"设置", RectF(26, 22, 150, 44), 29);
            drawUiText(g, L"Codex Quota Bar\n" + std::wstring(Version), RectF(24, rect.bottom / d - 80, 155, 58), 14, muted);
            Pen line(vibrancy::border, 0);
            g.DrawLine(&line, 196.f, 0.f, 196.f, float(rect.bottom / d));
            g.DrawLine(&line, 218.f, rect.bottom / d - 78, rect.right / d - 28, rect.bottom / d - 78);
        } else {
            const wchar_t *headings[] = {L"外观", L"常规", L"高级", L"DeepSeek 相关设置"};
            const wchar_t *subtitles[] = {L"调整任务栏组件的显示方式。",
                                          L"管理显示屏幕，以及应用的启动与退出。",
                                          L"配置额度读取来源，或手动刷新额度。",
                                          L"可随 Harness 启停，也可独立显示余额。"};
            drawHeading(g, headings[page], RectF(0, 0, float(contentWidth), 40), 27);
            drawUiText(g, subtitles[page], RectF(0, 43, float(contentWidth), 26), 14, muted);
            auto row = [&](float y, const wchar_t *title, const wchar_t *description, float reserve = 240) {
                drawUiText(g, title, RectF(0, y, float(contentWidth) - reserve, 25), 16, ink);
                drawUiText(g, description, RectF(0, y + 25, float(contentWidth) - reserve, 25), 14, muted);
                Pen p(vibrancy::border, 0);
                g.DrawLine(&p, 0.f, y + 59, float(contentWidth) - 14, y + 59);
            };
            if (page == 0) {
                row(82, L"任务栏样式", L"选择额度的呈现方式。");
                row(148, L"显示大小", L"整体缩放，过大时适配任务栏高度。");
                drawUiText(g, std::to_wstring(int(std::round(draft.scale * 100))) + L"%",
                         RectF(contentWidth - 70.f, 151, 56, 32), 13, muted, false, true);
                row(214, L"图标颜色", L"分别选择两种额度的颜色。", 310);
                row(274, L"底色不透明度", L"适用于两个组件，0% 为透明。");
                drawUiText(g, std::to_wstring(draft.backgroundOpacity) + L"%",
                           RectF(contentWidth - 70.f, 277, 56, 32), 13, muted, false, true);
                row(340, L"Codex 图标不透明度", L"调节额度图形和数字，最低 10%。");
                drawUiText(g, std::to_wstring(draft.iconOpacity) + L"%",
                           RectF(contentWidth - 70.f, 343, 56, 32), 13, muted, false, true);
                drawPanel(g, RectF(.5f, 418, contentWidth - 15.f, 62), 10, vibrancy::surface);
                Quotas q;
                q.hour = WindowQuota{68, 0};
                q.week = WindowQuota{27, 0};
                auto preview = previewSettings();
                auto visible = preview.visibility(q);
                auto layout = widgetLayout(preview, visible, 96, 54);
                double t =
                    previewStart < 0 ? 1 : std::clamp((GetTickCount64() - previewStart) / 1200., 0., 1.);
                double progress = 1 - std::pow(1 - t, 3);
                if (!visible.count())
                    drawUiText(g, L"已隐藏所有额度", RectF(0, 430, contentWidth - 14.f, 36),
                               14, muted, false, true);
                else {
                    RectF bounds((contentWidth - 14 - layout.width) / 2.f, 418 + (62 - layout.height) / 2.f,
                                 float(layout.width), float(layout.height));
                    drawWidgetBackground(g, bounds, preview, false, false);
                    drawWidget(g, bounds, q, preview, false, false, progress, progress);
                }
                drawUiText(g, L"预览使用模拟数据", RectF(0, 482, float(contentWidth), 24), 12, muted);
                row(512, L"仪表中间显示 H 和 W", L"H 表示 5 小时额度，W 表示周额度。", 110);
                row(578, L"在图标上展示余量", L"关闭后保持完整纯色图形；按样式分别保存。", 110);
                row(644, L"显示百分号", L"关闭后只显示数字；按样式分别保存。", 110);
                row(710, L"启动与额度重置动画", L"图形渐满；纯数字从 0 递增至真实值。", 110);
            } else if (page == 1) {
                row(82, L"显示屏幕", L"选择任务栏组件出现的位置。");
                row(148, L"随 Windows 登录启动", L"启动轻量监听器，自动跟随应用。", 110);
                row(214, L"显示 5 小时额度", L"默认 Plus 显示，Pro 系列隐藏。", 110);
                row(280, L"显示周额度", L"默认显示；可与 5 小时额度独立选择。", 110);
                row(346, L"按套餐自动选择", L"恢复 Plus 两项、Pro 系列仅周额度。", 170);
                row(412, L"图标自动避让", L"避开任务栏按钮与其他组件；空间不足时暂时隐藏。", 110);
                row(478, L"跟随应用运行", L"各组件随对应应用启动显示、退出关闭。", 80);
                drawUiText(g, L"自动", RectF(contentWidth - 80.f, 478, 65, 30), 13, muted, false, true);
                drawUiText(g, L"两项关闭时隐藏组件，可从开始菜单打开设置。",
                         RectF(0, 557, float(contentWidth), 32), 14, muted);
            } else if (page == 2) {
                drawUiText(g, L"Codex CLI 路径", RectF(0, 82, float(contentWidth), 26), 16, ink);
                drawUiText(g, L"留空时自动查找已安装的 Codex。", RectF(0, 108, float(contentWidth), 26), 14,
                         muted);
                row(200, L"自动刷新间隔", L"保存后立即生效。");
                row(266, L"额度读取", L"读取失败时自动退避重试。");
                row(332, L"退出", L"停止显示与后台监听。");
                row(398, L"卸载", L"删除本工具及设置，保留目录中的其他文件。");
                row(464, L"程序所在文件夹", L"在文件资源管理器中打开。");
                drawUiText(g, L"通过本机 Codex 读取，不保存登录信息。", RectF(0, 549, float(contentWidth), 32),
                         14, muted);
            } else {
                row(82, L"自动跟随 DeepSeekHarness", L"Harness 运行时自动显示并刷新。", 110);
                row(148, L"未启动时也显示余额", L"默认关闭；开启后独立显示并刷新余额和账单。", 110);
                drawUiText(g, L"平台登录凭证 userToken", RectF(0, 214, contentWidth - 210.f, 26), 16, ink);
                drawUiText(g, L"同一平台账户的余额、费用和 Token 明细。", RectF(0, 240, contentWidth - 175.f, 26), 14, muted);
                row(322, L"自动刷新间隔", L"失败时保留旧数据并退避重试。", 190);
                row(388, L"读取最新数据", L"先保存凭证；组件显示期间可刷新。", 150);
                row(454, L"本机登录态", clearDeepSeekCredentials ? L"保存后将清除原凭证，可填写新凭证替换。" : L"留空保留原凭证，清除在保存后生效。", 150);
                drawUiText(g, L"北京时间 UTC+8 · 平台账单可能延迟更新。", RectF(0, 530, float(contentWidth), 26), 13, muted);
                drawUiText(g, L"平台登录态保存在本机 Windows 凭据管理器。", RectF(0, 563, float(contentWidth), 26), 12, muted);
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
        bool disabled = (item.itemState & ODS_DISABLED) != 0;
        bool pressed = (item.itemState & ODS_SELECTED) != 0;
        bool hot = (item.itemState & ODS_HOTLIGHT) != 0 || GetPropW(item.hwndItem, L"VibrancyHover");
        bool nav = id >= NavAppearance && id <= NavDeepSeek;
        auto base = nav ? vibrancy::deep : (GetParent(item.hwndItem) == window ? vibrancy::surface : vibrancy::mid);
        g.Clear(base);
        float d = dpi / 96.f;
        RectF area(1, 1, image.width - 2.f, image.height - 2.f);
        if (isToggle(id)) {
            auto visible = draft.visibility(app->state.q);
            bool enabled = id == Letters     ? draft.letters
                           : id == Remaining ? draft.pref().remaining
                           : id == Percent   ? draft.pref().percent
                           : id == Animate   ? draft.pref().animate
                           : id == ShowHour  ? visible.hour
                           : id == ShowWeek  ? visible.week
                           : id == DeepSeekEnabled ? draft.deepSeekEnabled
                           : id == DeepSeekAlwaysShow ? draft.deepSeekAlwaysShow
                           : id == AvoidIcons ? draft.avoidIcons
                                             : startup;
            drawPanel(g, area, 7 * d, enabled ? vibrancy::surface : vibrancy::deep);
            float size = area.Height - 8 * d;
            float left = enabled ? area.GetRight() - size - 4 * d : area.X + 4 * d;
            fillRound(g, RectF(left, area.Y + 4 * d, size, size), 4 * d,
                      disabled ? vibrancy::muted : enabled ? vibrancy::ink : vibrancy::secondary);
        } else {
            auto fill = nav ? base : id == Save ? vibrancy::surface : vibrancy::deep;
            fillRound(g, area, 8 * d, fill);
            if (pressed || (nav && id - NavAppearance == page))
                fillRound(g, area, 8 * d, vibrancy::active);
            else if (hot && !disabled)
                fillRound(g, area, 8 * d, vibrancy::hover);
            if (!nav) {
                GraphicsPath p;
                roundPath(p, area, 8 * d);
                Pen border(vibrancy::border, 1);
                g.DrawPath(&border, &p);
            }
            RectF textArea = area;
            if (nav) {
                textArea.X += 12 * d;
                textArea.Width -= 18 * d;
            } else if (id == HourColor || id == WeekColor) {
                auto color = parseColor(id == HourColor ? draft.hourColor : draft.weekColor, vibrancy::ink);
                fillRound(g, RectF(9 * d, (image.height - 10 * d) / 2.f, 10 * d, 10 * d), 2 * d, color);
                textArea.X += 13 * d;
                textArea.Width -= 13 * d;
            }
            drawUiText(g, controlText(item.hwndItem), textArea, 15 * d,
                       disabled ? vibrancy::muted : vibrancy::ink, id == Save, !nav);
        }
        if (item.itemState & ODS_FOCUS) {
            GraphicsPath p;
            roundPath(p, RectF(2, 2, image.width - 4.f, image.height - 4.f), 6 * d);
            Pen focus(vibrancy::focus, 1);
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
    if (id >= NavAppearance && id <= NavDeepSeek) {
        selectPage(id - NavAppearance);
        return;
    }
    switch (id) {
    case DeepSeekEnabled:
        draft.deepSeekEnabled = !draft.deepSeekEnabled;
        break;
    case DeepSeekAlwaysShow:
        draft.deepSeekAlwaysShow = !draft.deepSeekAlwaysShow;
        break;
    case DeepSeekRefresh:
        app->refreshDeepSeek();
        break;
    case DeepSeekClear:
        clearDeepSeekCredentials = true;
        SetWindowTextW(deepSeekToken, L"");
        SendMessageW(deepSeekToken, EM_SETCUEBANNER, 0, reinterpret_cast<LPARAM>(L"保存后清除；或输入新平台凭证"));
        break;
    case DeepSeekPlatform:
        ShellExecuteW(window, L"open", L"https://platform.deepseek.com/usage", nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case DeepSeekHelp:
        MessageBoxW(window,
                    L"1. 点击“打开 DeepSeek 平台”，在浏览器中自行登录。\n"
                    L"2. 按 F12，在 Application / 应用 → Local Storage / 本地存储中选择 https://platform.deepseek.com。\n"
                    L"3. 找到 userToken，将令牌或含 value 的 JSON 填入“平台登录凭证”，点击“保存更改”。\n\n"
                    L"平台凭证同时用于同一账户的余额与账单。\n"
                    L"userToken 属于平台登录凭证，请仅保存在本机。无需填写 API Key 或账号密码。",
                    L"DeepSeek 凭证填写说明", MB_OK | MB_ICONINFORMATION);
        break;
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
        styleChanged();
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
    case AvoidIcons:
        draft.avoidIcons = !draft.avoidIcons;
        break;
    case ShowHour:
        draft.showHour = !draft.visibility(app->state.q).hour;
        styleChanged();
        break;
    case ShowWeek:
        draft.showWeek = !draft.visibility(app->state.q).week;
        styleChanged();
        break;
    case ResetQuotas:
        draft.showHour.reset();
        draft.showWeek.reset();
        styleChanged();
        InvalidateRect(GetDlgItem(content, ShowHour), nullptr, TRUE);
        InvalidateRect(GetDlgItem(content, ShowWeek), nullptr, TRUE);
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
    case OpenFolder:
        if (reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open", executable().parent_path().c_str(),
                                                   nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
            MessageBoxW(window, L"无法打开程序所在文件夹。", L"Codex Quota Bar", MB_OK | MB_ICONERROR);
        break;
    case Uninstall:
        if (MessageBoxW(
                window,
                L"卸载本工具并删除设置、启动项和快捷方式？\n不会删除 ChatGPT、Codex 或它们的登录数据。",
                L"卸载 Codex Quota Bar", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) {
            if (launchUninstaller(window))
                app->stop();
            else
                MessageBoxW(window, L"无法启动卸载程序，请检查目录权限或系统 PowerShell 是否可用。",
                            L"Codex Quota Bar", MB_OK | MB_ICONERROR);
        }
        break;
    }
    if (auto w = GetDlgItem(content, id))
        InvalidateRect(w, nullptr, TRUE);
    InvalidateRect(content, nullptr, FALSE);
}
bool SettingsWindow::save() {
    auto newToken = normalizeDeepSeekCredential(utf8(controlText(deepSeekToken)));
    if (!validDeepSeekCredential(newToken)) {
        MessageBoxW(window, L"请填写平台 userToken 或其本地存储值，不接受 API Key 或含无效字符的登录凭证。", L"无法保存 DeepSeek 凭证", MB_OK | MB_ICONERROR);
        return false;
    }
    int deepInterval = int(SendMessageW(deepSeekInterval, CB_GETCURSEL, 0, 0));
    draft.deepSeekInterval = deepInterval == 1 ? 120 : deepInterval == 2 ? 300 : 60;
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
    auto oldToken = deepSeekCredential();
    bool credentialsTouched = false;
    try {
        if (!setStartup(startup))
            throw std::runtime_error("Cannot save startup");
        if (clearDeepSeekCredentials || !newToken.empty()) {
            saveDeepSeekCredential(newToken); credentialsTouched = true;
        }
        draft.save();
        app->saved(draft);
        return true;
    } catch (...) {
        if (credentialsTouched) {
            try { saveDeepSeekCredential(oldToken); } catch (...) {}
        }
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
            auto ink = vibrancy::ink, header = vibrancy::surface;
            g.Clear(vibrancy::mid);
            g.ScaleTransform(self->dpi / 96.f, self->dpi / 96.f);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            fillRound(g, RectF(8, 8, 278, 30), 5, header);
            drawUiText(g, L"自动", RectF(44, 8, 235, 30), 16, ink);
            SolidBrush automatic(parseColor(self->colorHour ? "#219E86" : "#648DE6", Color::Black));
            g.FillRectangle(&automatic, RectF(18, 14, 18, 18));
            for (auto pair : {std::make_pair(41, L"主题颜色"), std::make_pair(216, L"标准色")}) {
                SolidBrush b(header);
                g.FillRectangle(&b, RectF(8, float(pair.first), 278, 25));
                drawUiText(g, pair.second, RectF(16, float(pair.first), 264, 25), 13, ink);
            }
            int selection = int(reinterpret_cast<INT_PTR>(GetPropW(w, L"Selection")));
            for (int i = 0; i < 70; ++i) {
                int x = 14 + (i % 10) * 27, y = i < 60 ? 73 + (i / 10) * 23 : 248;
                auto c = swatch(i);
                SolidBrush b(c);
                g.FillRectangle(&b, RectF(float(x), float(y), 21, 21));
                Pen pen(hexColor(c) == hexColor(self->selectedColor) || selection == i
                            ? vibrancy::focus : vibrancy::border, 0);
                g.DrawRectangle(&pen, RectF(float(x), float(y), 21, 21));
            }
            drawUiText(g, L"其他颜色…", RectF(16, 278, 264, 28), 13, ink);
        }
        BitBlt(dc, 0, 0, r.right, r.bottom, image.dc, 0, 0, SRCCOPY);
        EndPaint(w, &ps);
        return 0;
    }
    }
    return DefWindowProcW(w, msg, wp, lp);
}
LRESULT CALLBACK SettingsWindow::viewportProcedure(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE)
        SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams));
    auto self = reinterpret_cast<SettingsWindow *>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (self) {
        if ((msg == WM_MOUSEWHEEL || msg == WM_VSCROLL) && self->content)
            return SendMessageW(self->content, msg, wp, lp);
        if (msg == WM_ERASEBKGND) return 1;
        if (msg == WM_PAINT) {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(w, &paint);
            if (self->background) FillRect(dc, &paint.rcPaint, self->background);
            EndPaint(w, &paint);
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
    case WM_DRAWITEM: {
        auto &item = *reinterpret_cast<DRAWITEMSTRUCT *>(lp);
        if (item.CtlType == ODT_COMBOBOX) drawComboOption(item);
        else self->drawButton(item);
        return TRUE;
    }
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lp) == self->scale) {
            self->draft.scale = SendMessageW(self->scale, TBM_GETPOS, 0, 0) / 100.;
            InvalidateRect(w, nullptr, FALSE);
        } else if (reinterpret_cast<HWND>(lp) == self->opacitySlider) {
            self->draft.backgroundOpacity = int(SendMessageW(self->opacitySlider, TBM_GETPOS, 0, 0));
            InvalidateRect(w, nullptr, FALSE);
        } else if (reinterpret_cast<HWND>(lp) == self->iconOpacitySlider) {
            self->draft.iconOpacity = int(SendMessageW(self->iconOpacitySlider, TBM_GETPOS, 0, 0));
            InvalidateRect(w, nullptr, FALSE);
        }
        return 0;
    case WM_VSCROLL: {
        SCROLLINFO si{sizeof(si), SIF_ALL};
        GetScrollInfo(self->viewport, SB_VERT, &si);
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
        self->scrollTo(value);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        self->wheelRemainder += GET_WHEEL_DELTA_WPARAM(wp);
        int steps = self->wheelRemainder / WHEEL_DELTA;
        self->wheelRemainder %= WHEEL_DELTA;
        if (steps) self->scrollTo(self->scroll - steps * 48);
        return 0;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wp);
        SetTextColor(dc, RGB(244, 244, 245));
        SetBkColor(dc, vibrancy::inputBrush);
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
    case WM_MOUSEWHEEL:
        if (self->content) return SendMessageW(self->content, msg, wp, lp);
        break;
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
        self->font = CreateFontW(-self->px(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
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
        {
            BOOL dark = TRUE;
            DwmSetWindowAttribute(w, 20, &dark, sizeof(dark));
        }
        InvalidateRect(w, nullptr, TRUE);
        RedrawWindow(self->content, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}
} // namespace quota
