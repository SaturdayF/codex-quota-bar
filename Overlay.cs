using System.Runtime.InteropServices;

namespace CodexQuotaBar;

internal sealed class Overlay : Form
{
    private readonly Screen screen;
    private DisplayState state = DisplayState.Empty;
    private readonly System.Windows.Forms.Timer hoverTimer = new() { Interval = 75 };
    private readonly HoverDetails details = new();
    private readonly Func<Point> pointer;
    private long hoverStarted, lastInside;
    internal bool HoverHighlighted { get; private set; }
    internal bool DetailsVisible => details.Visible;
    internal Rectangle DetailsBounds => details.Bounds;
    private bool dragging;
    private Point dragStart;
    private Point locationStart;
    private readonly Color clearKey = Color.FromArgb(255, 0, 255);
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

    public Overlay(Screen screen, ContextMenuStrip menu, Func<Point>? pointer = null)
    {
        this.screen = screen;
        this.pointer = pointer ?? (() => Cursor.Position);
        Text = "Codex Quota Bar";
        FormBorderStyle = FormBorderStyle.None; ShowInTaskbar = false; TopMost = true;
        AutoScaleMode = AutoScaleMode.Dpi;
        DoubleBuffered = true;
        BackColor = TransparencyKey = clearKey;
        foregroundCallback = (_, _, _, _, _, _, _) =>
        {
            if (!IsDisposed && IsHandleCreated)
            {
                try { BeginInvoke(new Action(EnsureAboveTaskbar)); } catch (InvalidOperationException) { }
            }
        };
        ContextMenuStrip = menu;
        hoverTimer.Tick += (_, _) => CheckHover();
        hoverTimer.Start();
        MouseDown += (_, e) => { EnsureAboveTaskbar(); if (e.Button == MouseButtons.Left) { dragging = true; Capture = true; dragStart = Cursor.Position; locationStart = Location; } };
        MouseMove += (_, _) => { if (dragging) Location = new(locationStart.X + Cursor.Position.X - dragStart.X, locationStart.Y); };
        MouseUp += (_, _) => { dragging = false; Capture = false; Location = new(Math.Clamp(Left, screen.Bounds.Left, Math.Max(screen.Bounds.Left, screen.Bounds.Right - Width)), Top); EnsureAboveTaskbar(); };
    }
    private void CheckHover()
    {
        if (!Visible) { details.Hide(); return; }
        var now = Environment.TickCount64;
        var point = pointer();
        var onWidget = Bounds.Contains(point);
        var inside = onWidget || (details.Visible && details.Bounds.Contains(point));
        if (inside)
        {
            lastInside = now;
            if (!HoverHighlighted) { HoverHighlighted = true; hoverStarted = now; Invalidate(); }
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
            HoverHighlighted = false; details.Hide(); Invalidate();
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
    protected override CreateParams CreateParams { get { var p = base.CreateParams; p.ExStyle |= 0x08000000 | 0x00000080; return p; } }
    public void UpdateState(DisplayState current)
    {
        state = current;
        details.UpdateState(state);
        Invalidate();
        EnsureAboveTaskbar();
    }
    public void PositionAtTaskbar()
    {
        var scale = DeviceDpi / 96f;
        Size = new((int)(118 * scale), (int)(32 * scale));
        var bounds = screen.Bounds;
        var taskbar = TaskbarBounds(screen);
        // Anchor to each display's left edge, away from the clock and notification icons.
        var leftInset = (int)(16 * scale);
        var x = Math.Min(bounds.Left + leftInset, Math.Max(bounds.Left, bounds.Right - Width));
        Location = new(x, taskbar is { } bar ? bar.Top + Math.Max(0, (bar.Height - Height) / 2) : screen.WorkingArea.Bottom - Height - 4);
        BackColor = TransparencyKey = clearKey;
        EnsureAboveTaskbar();
    }
    private static Rectangle? TaskbarBounds(Screen screen)
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
        base.OnPaint(e);
        if (HoverHighlighted)
        {
            using var border = new Pen(Artwork.LightTheme ? Color.FromArgb(83, 108, 137) : Color.FromArgb(176, 196, 223));
            e.Graphics.DrawRectangle(border, 0, 0, ClientSize.Width - 1, ClientSize.Height - 1);
        }
        e.Graphics.TextRenderingHint = System.Drawing.Text.TextRenderingHint.SingleBitPerPixelGridFit;
        var half = ClientSize.Width / 2f;
        Artwork.Draw(e.Graphics, new(2, 1, half - 4, ClientSize.Height - 2), Quotas.Label(state.Quotas.Hour), Artwork.HourColor, state.Stale, Artwork.LightTheme);
        Artwork.Draw(e.Graphics, new(half + 2, 1, half - 4, ClientSize.Height - 2), Quotas.Label(state.Quotas.Week), Artwork.WeekColor, state.Stale, Artwork.LightTheme);
    }
    protected override void Dispose(bool disposing)
    {
        if (disposing) { hoverTimer.Dispose(); details.Dispose(); }
        base.Dispose(disposing);
    }
}
