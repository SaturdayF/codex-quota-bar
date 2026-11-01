using System.Runtime.InteropServices;

namespace CodexQuotaBar;

internal sealed class Overlay : Form
{
    private readonly Screen screen;
    private DisplayState state = DisplayState.Empty;
    private readonly System.Windows.Forms.Timer hoverTimer = new() { Interval = 75 };
    private readonly HoverDetails details = new();
    private readonly Func<Point> pointer;
    private readonly bool showLetters;
    private readonly string style;
    private readonly bool animateReset;
    private readonly QuotaAnimation animation = new();
    private readonly System.Windows.Forms.Timer animationTimer = new() { Interval = 16 };
    private bool positioning;
    [System.ComponentModel.DesignerSerializationVisibility(System.ComponentModel.DesignerSerializationVisibility.Hidden)]
    internal bool InteractionSuspended { get; set; }
    internal event Action? SettingsRequested;
    private long hoverStarted, lastInside;
    private long lastSettingsRequest = -1000;
    internal bool HoverHighlighted { get; private set; }
    internal bool DetailsVisible => details.Visible;
    internal Rectangle DetailsBounds => details.Bounds;
    private bool dragging;
    private Point dragStart;
    private Point locationStart;
    private IntPtr foregroundHook;
    private readonly WinEventCallback foregroundCallback;
    private delegate void WinEventCallback(IntPtr hook, uint eventId, IntPtr window, int objectId, int childId, uint thread, uint time);
    [DllImport("user32.dll")] private static extern IntPtr SetWinEventHook(uint min, uint max, IntPtr module, WinEventCallback callback, uint process, uint thread, uint flags);
    [DllImport("user32.dll")] private static extern bool UnhookWinEvent(IntPtr hook);
    [DllImport("user32.dll")] private static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] private static extern IntPtr FindWindow(string className, string? windowName);
    [DllImport("user32.dll")] private static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string className, string? title);
    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] private static extern IntPtr MonitorFromWindow(IntPtr window, uint flags);
    [DllImport("user32.dll")] private static extern IntPtr MonitorFromPoint(Point point, uint flags);
    [StructLayout(LayoutKind.Sequential)] private struct Rect { public int Left, Top, Right, Bottom; }

    public Overlay(Screen screen, ContextMenuStrip menu, Func<Point>? pointer = null, bool showLetters = true, string style = "gauges", bool animateReset = true)
    {
        this.screen = screen;
        this.pointer = pointer ?? (() => Cursor.Position);
        this.showLetters = showLetters;
        this.style = WidgetStyles.Normalize(style);
        this.animateReset = animateReset;
        animationTimer.Tick += (_, _) =>
        {
            if (!animation.Running(Environment.TickCount64)) animationTimer.Stop();
            RenderSurface();
        };
        Text = "Codex Quota Bar";
        FormBorderStyle = FormBorderStyle.None; ShowInTaskbar = false; TopMost = true;
        AutoScaleMode = AutoScaleMode.None;
        DoubleBuffered = true;
        foregroundCallback = (_, _, _, _, _, _, _) =>
        {
            if (!IsDisposed && IsHandleCreated)
            {
                try { BeginInvoke(new Action(EnsureAboveTaskbar)); } catch (InvalidOperationException) { }
            }
        };
        hoverTimer.Tick += (_, _) => CheckHover();
        hoverTimer.Start();
        MouseDown += (_, e) => { EnsureAboveTaskbar(); if (e.Button == MouseButtons.Left) { dragging = true; Capture = true; dragStart = Cursor.Position; locationStart = Location; } };
        MouseMove += (_, _) => { if (dragging) Location = new(locationStart.X + Cursor.Position.X - dragStart.X, locationStart.Y); };
        MouseUp += (_, e) =>
        {
            dragging = false; Capture = false;
            Location = new(Math.Clamp(Left, screen.Bounds.Left, Math.Max(screen.Bounds.Left, screen.Bounds.Right - Width)), Top);
            EnsureAboveTaskbar();
            if (e.Button == MouseButtons.Right) RequestSettings();
        };
    }
    private void RequestSettings()
    {
        // Hover suspension must not block reactivating an already open settings window.
        if (IsDisposed || Environment.TickCount64 - lastSettingsRequest < 200) return;
        lastSettingsRequest = Environment.TickCount64;
        details.Hide(); HoverHighlighted = false; RenderSurface();
        SettingsRequested?.Invoke();
    }
    private void CheckHover()
    {
        if (!Visible || InteractionSuspended) { details.Hide(); if (HoverHighlighted) { HoverHighlighted = false; RenderSurface(); } return; }
        var now = Environment.TickCount64;
        var point = pointer();
        var onWidget = Bounds.Contains(point);
        var inside = onWidget || (details.Visible && details.Bounds.Contains(point));
        if (inside)
        {
            lastInside = now;
            if (!HoverHighlighted) { HoverHighlighted = true; hoverStarted = now; RenderSurface(); }
            if (!dragging && onWidget && now - hoverStarted >= 225 && !details.Visible)
            {
                details.UpdateState(state);
                details.Place(Bounds, screen.WorkingArea);
                details.Show();
                details.Place(Bounds, screen.WorkingArea);
            }
        }
        else if (HoverHighlighted && now - lastInside > 180)
        {
            HoverHighlighted = false; details.Hide(); RenderSurface();
        }
        if (dragging) details.Hide();
    }
    protected override void OnHandleCreated(EventArgs e)
    {
        base.OnHandleCreated(e);
        foregroundHook = SetWinEventHook(3, 3, IntPtr.Zero, foregroundCallback, 0, 0, 0);
    }
    protected override void OnHandleDestroyed(EventArgs e)
    {
        if (foregroundHook != IntPtr.Zero) { UnhookWinEvent(foregroundHook); foregroundHook = IntPtr.Zero; }
        base.OnHandleDestroyed(e);
    }
    protected override void WndProc(ref Message m)
    {
        if (m.Msg == 0x007B)
        {
            RequestSettings();
            m.Result = IntPtr.Zero; return;
        }
        // WS_EX_NOACTIVATE alone does not cover every mouse activation path.
        if (m.Msg == 0x0021) { m.Result = new IntPtr(3); return; } // WM_MOUSEACTIVATE / MA_NOACTIVATE
        base.WndProc(ref m);
    }
    public void EnsureAboveTaskbar()
    {
        if (!IsDisposed && IsHandleCreated && Visible)
            SetWindowPos(Handle, new IntPtr(-1), 0, 0, 0, 0, 0x0013); // TOPMOST, no move/resize/activation
    }
    protected override bool ShowWithoutActivation => true;
    protected override CreateParams CreateParams { get { var p = base.CreateParams; p.ExStyle |= 0x08000000 | 0x00000080 | 0x00080000; return p; } }
    public void UpdateState(DisplayState current)
    {
        animation.Update(current, Environment.TickCount64, DateTimeOffset.UtcNow,
            animateReset && style == "gauges" && QuotaAnimation.MotionEnabled);
        if (animation.Running(Environment.TickCount64)) animationTimer.Start();
        state = current;
        if (IsHandleCreated) PositionAtTaskbar(resetHorizontal: false);
        details.UpdateState(state);
        if (details.Visible) details.Place(Bounds, screen.WorkingArea);
        RenderSurface();
        EnsureAboveTaskbar();
    }
    protected override void OnShown(EventArgs e) { base.OnShown(e); BeginInvoke(new Action(() => PositionAtTaskbar())); }
    protected override void OnDpiChanged(DpiChangedEventArgs e) { base.OnDpiChanged(e); if (!IsDisposed) BeginInvoke(new Action(() => PositionAtTaskbar())); }
    public void PositionAtTaskbar(bool resetHorizontal = true)
    {
        if (positioning || IsDisposed) return;
        positioning = true;
        try {
        var scale = DeviceDpi / 96f;
        var bounds = screen.Bounds;
        var taskbar = TaskbarBounds(screen);
        var desired = WidgetStyles.SizeFor(style, state.Quotas.WeekOnly, DeviceDpi);
        Size = new(desired.Width, taskbar is { } b ? Math.Min(desired.Height, b.Height) : desired.Height);
        // Anchor to each display's left edge, away from the clock and notification icons.
        var leftInset = (int)(16 * scale);
        var x = Math.Clamp(resetHorizontal ? bounds.Left + leftInset : Left, bounds.Left, Math.Max(bounds.Left, bounds.Right - Width));
        Location = new(x, taskbar is { } bar ? bar.Top + (bar.Height - Height) / 2 : screen.WorkingArea.Bottom - Height - 4);
        RenderSurface();
        EnsureAboveTaskbar();
        } finally { positioning = false; }
    }
    internal static Rectangle? TaskbarBounds(Screen screen)
    {
        var monitor = MonitorFromPoint(new Point(screen.Bounds.Left + 1, screen.Bounds.Top + 1), 2);
        var primary = FindWindow("Shell_TrayWnd", null);
        if (primary != IntPtr.Zero && MonitorFromWindow(primary, 2) == monitor && GetWindowRect(primary, out var p)) return Rectangle.FromLTRB(p.Left, p.Top, p.Right, p.Bottom);
        var after = IntPtr.Zero;
        while ((after = FindWindowEx(IntPtr.Zero, after, "Shell_SecondaryTrayWnd", null)) != IntPtr.Zero)
            if (MonitorFromWindow(after, 2) == monitor && GetWindowRect(after, out var r)) return Rectangle.FromLTRB(r.Left, r.Top, r.Right, r.Bottom);
        return null;
    }
    protected override void OnPaint(PaintEventArgs e)
    {
        RenderSurface();
    }
    private void RenderSurface()
    {
        if (IsDisposed || !IsHandleCreated || ClientSize.Width < 1 || ClientSize.Height < 1) return;
        var now = Environment.TickCount64;
        using var image = LayeredSurface.Paint(ClientSize, state.Quotas, style, showLetters, state.Stale, Artwork.LightTheme, HoverHighlighted,
            animation.Fill(true, state.Quotas.Hour, now), animation.Fill(false, state.Quotas.Week, now));
        LayeredSurface.Present(Handle, Location, image);
    }
    protected override void Dispose(bool disposing)
    {
        if (disposing) { hoverTimer.Dispose(); animationTimer.Dispose(); details.Dispose(); }
        base.Dispose(disposing);
    }
}
