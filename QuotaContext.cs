using System.Diagnostics;
using Microsoft.Win32;

namespace CodexQuotaBar;

internal sealed class QuotaContext : ApplicationContext
{
    private Settings settings = Settings.Load();
    private readonly Control dispatcher = new();
    private readonly System.Windows.Forms.Timer watcher = new() { Interval = 2000 };
    private readonly NotifyIcon hourIcon = new();
    private readonly NotifyIcon weekIcon = new();
    private readonly ContextMenuStrip menu = new();
    private readonly List<Overlay> overlays = [];
    private readonly SemaphoreSlim refreshGate = new(1, 1);
    private CancellationTokenSource? session;
    private AppServer? server;
    private DisplayState state = DisplayState.Empty;
    private string layout = "";
    private bool active;
    private bool quitting;
    private Icon? hourArtwork, weekArtwork;
    private DateTime settingsWrite;
    private SettingsForm? settingsWindow;
    private readonly System.Windows.Forms.Timer settingsOpener = new() { Interval = 50 };
    private Screen? requestedSettingsScreen;

    public QuotaContext()
    {
        _ = dispatcher.Handle;
        hourIcon.ContextMenuStrip = weekIcon.ContextMenuStrip = menu;
        hourIcon.MouseClick += (_, e) => { if (e.Button == MouseButtons.Left) Details(); };
        weekIcon.MouseClick += (_, e) => { if (e.Button == MouseButtons.Left) Details(); };
        watcher.Tick += (_, _) => Tick();
        settingsOpener.Tick += (_, _) => { settingsOpener.Stop(); ShowSettings(); };
        menu.Opening += (_, _) => BuildMenu();
        SystemEvents.DisplaySettingsChanged += DisplaysChanged;
        SystemEvents.UserPreferenceChanged += PreferencesChanged;
        Tick();
        watcher.Start();
    }
    private void Dispatch(Action action)
    {
        if (quitting || dispatcher.IsDisposed) return;
        try { dispatcher.BeginInvoke(new Action(() => { if (!quitting) action(); })); }
        catch (InvalidOperationException) { }
    }
    private void Tick()
    {
        var write = File.Exists(Settings.FilePath) ? File.GetLastWriteTimeUtc(Settings.FilePath) : DateTime.MinValue;
        if (write != settingsWrite)
        {
            settingsWrite = write; settings = Settings.Load(); layout = "";
        }
        var running = DesktopApps.AnyRunning();
        if (running != active)
        {
            active = running;
            if (active)
            {
                state = DisplayState.Empty;
                session = new CancellationTokenSource();
                _ = RunSession(session.Token);
            }
            else
            {
                session?.Cancel(); server?.Dispose(); server = null;
                session?.Dispose(); session = null;
                hourIcon.Visible = weekIcon.Visible = false;
                ClearOverlays(); state = DisplayState.Empty;
            }
            layout = "";
        }
        if (!active) return;
        var selected = settings.SelectedScreens().ToArray();
        var signature = string.Join("|", selected.Select(s => $"{s.DeviceName}:{s.Bounds}:{s.Primary}"));
        if (signature != layout)
        {
            layout = signature; ClearOverlays();
            // Windows decides which NotifyIcons go into overflow. Use the same visible
            // taskbar surface on every selected display rather than depending on pinning.
            hourIcon.Visible = weekIcon.Visible = false;
            foreach (var screen in selected)
            {
                var overlay = new Overlay(screen, menu, showLetters: settings.ShowDialLetters, style: settings.WidgetStyle);
                // WM_CONTEXTMENU is already delivered on this UI thread.
                overlay.SettingsRequested += () => QueueSettings(Screen.FromControl(overlay));
                overlays.Add(overlay);
                overlay.Show(); overlay.PositionAtTaskbar(); overlay.UpdateState(state);
            }
        }
        Render();
    }
    private void ClearOverlays()
    {
        foreach (var overlay in overlays) overlay.Dispose();
        overlays.Clear();
    }
    private async Task RunSession(CancellationToken token)
    {
        var failures = 0;
        try
        {
            while (!token.IsCancellationRequested)
            {
                var success = await Refresh(token);
                failures = success ? 0 : Math.Min(failures + 1, 4);
                await Task.Delay(TimeSpan.FromSeconds(success ? 30 : Math.Min(300, 30 * Math.Pow(2, failures - 1))), token);
            }
        }
        catch (OperationCanceledException) { }
    }
    private async Task<bool> Refresh(CancellationToken token)
    {
        if (!await refreshGate.WaitAsync(0, token)) return true;
        try
        {
            if (server is null)
            {
                var created = await AppServer.Start(CodexLocator.Find(settings.CodexPath), token);
                if (token.IsCancellationRequested) { created.Dispose(); token.ThrowIfCancellationRequested(); }
                server = created;
                created.Updated += q => Dispatch(() => { if (active && !token.IsCancellationRequested) Apply(q); });
            }
            var quotas = await server.Read(token);
            if (!token.IsCancellationRequested && active) Apply(quotas);
            return true;
        }
        catch (OperationCanceledException) { throw; }
        catch (Exception e) when (e is IOException or InvalidOperationException or System.ComponentModel.Win32Exception or TimeoutException or RpcFailure or System.Text.Json.JsonException)
        {
            if (!token.IsCancellationRequested && active)
            {
                state = state with { Error = e switch
                {
                    FileNotFoundException => "找不到 Codex，请在设置中选择 codex.exe",
                    RpcFailure => "额度查询失败：请检查网络和 Codex 的 ChatGPT 登录状态",
                    TimeoutException => "读取超时，将自动重试",
                    _ => "Codex 连接失败，将自动重试"
                } };
                Render();
            }
            server?.Dispose(); server = null;
            return false;
        }
        finally { refreshGate.Release(); }
    }
    private void Apply(Quotas quotas)
    {
        state = new(quotas, DateTimeOffset.UtcNow, null);
        Render();
    }
    private string artworkKey = "";
    private void Render()
    {
        if (!active || quitting) return;
        var h = Quotas.Label(state.Quotas.Hour); var w = Quotas.Label(state.Quotas.Week);
        var key = $"{h}|{w}|{state.Stale}|{Artwork.LightTheme}";
        if (key != artworkKey)
        {
            artworkKey = key;
            var newH = Artwork.MakeIcon(h, Artwork.HourColor, state.Stale);
            var newW = Artwork.MakeIcon(w, Artwork.WeekColor, state.Stale);
            hourIcon.Icon = newH; weekIcon.Icon = newW;
            hourArtwork?.Dispose(); weekArtwork?.Dispose(); hourArtwork = newH; weekArtwork = newW;
        }
        hourIcon.Text = state.Tooltip(true); weekIcon.Text = state.Tooltip(false);
        foreach (var overlay in overlays) overlay.UpdateState(state);
    }
    private void BuildMenu()
    {
        menu.Items.Clear();
        menu.Items.Add("查看额度详情", null, (_, _) => Details());
        menu.Items.Add("立即刷新", null, async (_, _) => { if (session is { } s) { try { await Refresh(s.Token); } catch (OperationCanceledException) { } } });
        var displays = new ToolStripMenuItem("显示屏幕");
        displays.DropDownItems.Add(Choice("两个/所有屏幕", settings.DisplayMode == "all", () => { settings.DisplayMode = "all"; SaveSettings(); }));
        displays.DropDownItems.Add(Choice("仅主屏幕", settings.DisplayMode == "primary", () => { settings.DisplayMode = "primary"; SaveSettings(); }));
        foreach (var screen in Screen.AllScreens)
        {
            var id = screen.DeviceName;
            displays.DropDownItems.Add(Choice($"仅 {id}{(screen.Primary ? "（主屏）" : "")}", settings.DisplayMode == "selected" && settings.Displays.SequenceEqual([id]), () =>
            { settings.DisplayMode = "selected"; settings.Displays = [id]; SaveSettings(); }));
        }
        menu.Items.Add(displays);
        menu.Items.Add("设置…", null, (_, _) => QueueSettings(Screen.PrimaryScreen!));
        menu.Items.Add(Choice("随 Windows 登录启动监听器", Startup.Enabled, () =>
        {
            try { Startup.Set(!Startup.Enabled); }
            catch (Exception e) when (e is UnauthorizedAccessException or System.Security.SecurityException) { MessageBox.Show("无法修改当前用户启动项。", "Codex Quota Bar"); }
        }));
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add("退出额度工具（停止监听）", null, (_, _) => ExitThread());
    }
    private static ToolStripMenuItem Choice(string text, bool selected, Action action)
    {
        var item = new ToolStripMenuItem(text) { Checked = selected };
        item.Click += (_, _) => action(); return item;
    }
    private void SaveSettings()
    {
        try { settings.Save(); layout = ""; Tick(); }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { MessageBox.Show("无法保存显示设置。", "Codex Quota Bar"); }
    }
    private void QueueSettings(Screen screen)
    {
        requestedSettingsScreen = screen;
        // Finish the mouse-up/capture cycle before displaying another top-level window.
        settingsOpener.Start();
    }
    private void ShowSettings()
    {
        if (quitting) return;
        try
        {
            if (settingsWindow is null || settingsWindow.IsDisposed)
            {
                settingsWindow = new SettingsForm(
                    refresh: async () => { if (session is { } s) { try { await Refresh(s.Token); } catch (OperationCanceledException) { } } },
                    exit: () => ExitThread());
                settingsWindow.FormClosed += (_, _) =>
                {
                    settingsWindow = null;
                    if (!quitting) { settings = Settings.Load(); layout = ""; Tick(); }
                };
            }
            settingsWindow.Reveal(requestedSettingsScreen ?? Screen.PrimaryScreen!);
        }
        catch (Exception error) when (error is not OutOfMemoryException)
        {
            // A failed or hidden settings window must never leave hover disabled.
            var failed = settingsWindow; settingsWindow = null;
            failed?.Dispose();
            foreach (var overlay in overlays) overlay.InteractionSuspended = false;
            try
            {
                Directory.CreateDirectory(Settings.DataDirectory);
                File.WriteAllText(Path.Combine(Settings.DataDirectory, "settings-ui-error.txt"), DateTimeOffset.UtcNow + "\n" + error);
            }
            catch (Exception logError) when (logError is IOException or UnauthorizedAccessException) { }
        }
    }
    private void Details()
    {
        string Describe(string title, QuotaWindow? w) => $"{title}剩余：{(w is null ? "不可用" : w.Remaining.ToString("0.##") + "%")}\n重置：{w?.ResetsAt?.ToLocalTime().ToString("yyyy-MM-dd HH:mm:ss") ?? "未知"}";
        MessageBox.Show((state.Quotas.WeekOnly ? "" : Describe("5 小时", state.Quotas.Hour) + "\n\n") + Describe("每周", state.Quotas.Week)
            + $"\n\n最后成功读取：{state.Updated?.ToLocalTime().ToString("HH:mm:ss") ?? "尚无"}"
            + $"\n{state.Error ?? (state.Stale ? "数据已过期" : "每 30 秒读取一次")}", "Codex 额度详情", MessageBoxButtons.OK, MessageBoxIcon.Information);
    }
    private void DisplaysChanged(object? sender, EventArgs e) => Dispatch(() => { layout = ""; Tick(); });
    private void PreferencesChanged(object sender, UserPreferenceChangedEventArgs e) => Dispatch(() => { layout = ""; Tick(); });
    protected override void ExitThreadCore()
    {
        quitting = true; watcher.Stop(); session?.Cancel(); server?.Dispose();
        settingsOpener.Stop(); settingsOpener.Dispose();
        settingsWindow?.Close();
        ClearOverlays(); hourIcon.Visible = weekIcon.Visible = false;
        hourIcon.Dispose(); weekIcon.Dispose(); hourArtwork?.Dispose(); weekArtwork?.Dispose();
        menu.Dispose(); watcher.Dispose(); dispatcher.Dispose();
        SystemEvents.DisplaySettingsChanged -= DisplaysChanged;
        SystemEvents.UserPreferenceChanged -= PreferencesChanged;
        base.ExitThreadCore();
    }
}
