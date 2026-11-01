using System.Runtime.InteropServices;

namespace CodexQuotaBar;

internal sealed class Overlay : Form
{
    private readonly Screen screen;
    private DisplayState state = DisplayState.Empty;
    private readonly ToolTip tip = new() { AutoPopDelay = 15000 };
    private bool dragging;
    private Point dragStart;
    private Point locationStart;
    [DllImport("user32.dll")] private static extern IntPtr FindWindow(string className, string? windowName);
    [DllImport("user32.dll")] private static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string className, string? title);
    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] private static extern IntPtr MonitorFromWindow(IntPtr window, uint flags);
    [DllImport("user32.dll")] private static extern IntPtr MonitorFromPoint(Point point, uint flags);
    [StructLayout(LayoutKind.Sequential)] private struct Rect { public int Left, Top, Right, Bottom; }

    public Overlay(Screen screen, ContextMenuStrip menu)
    {
        this.screen = screen;
        Text = "Codex Quota Bar";
        FormBorderStyle = FormBorderStyle.None; ShowInTaskbar = false; TopMost = true;
        AutoScaleMode = AutoScaleMode.Dpi;
        DoubleBuffered = true;
        ContextMenuStrip = menu;
        MouseDown += (_, e) => { if (e.Button == MouseButtons.Left) { dragging = true; dragStart = Cursor.Position; locationStart = Location; } };
        MouseMove += (_, _) => { if (dragging) Location = new(locationStart.X + Cursor.Position.X - dragStart.X, locationStart.Y); };
        MouseUp += (_, _) => { dragging = false; Location = new(Math.Clamp(Left, screen.Bounds.Left, Math.Max(screen.Bounds.Left, screen.Bounds.Right - Width)), Top); };
    }
    protected override bool ShowWithoutActivation => true;
    protected override CreateParams CreateParams { get { var p = base.CreateParams; p.ExStyle |= 0x08000000 | 0x00000080; return p; } }
    public void UpdateState(DisplayState current)
    {
        state = current;
        tip.SetToolTip(this, state.Tooltip(true) + "\n\n" + state.Tooltip(false) + (state.Error is null ? "" : "\n" + state.Error));
        Invalidate();
    }
    public void PositionAtTaskbar()
    {
        var scale = DeviceDpi / 96f;
        Size = new((int)(118 * scale), (int)(32 * scale));
        var bounds = screen.Bounds;
        var taskbar = TaskbarBounds(screen);
        // Reserve space for clock/tray; users may drag horizontally to fit their taskbar.
        Location = new(Math.Max(bounds.Left, bounds.Right - Width - (int)(235 * scale)), taskbar is { } bar ? bar.Top + Math.Max(0, (bar.Height - Height) / 2) : screen.WorkingArea.Bottom - Height - 4);
        BackColor = Artwork.LightTheme ? Color.FromArgb(234, 237, 242) : Color.FromArgb(36, 39, 46);
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
        e.Graphics.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;
        var half = ClientSize.Width / 2f;
        Artwork.Draw(e.Graphics, new(2, 1, half - 4, ClientSize.Height - 2), Quotas.Label(state.Quotas.Hour), Artwork.HourColor, state.Stale, Artwork.LightTheme);
        Artwork.Draw(e.Graphics, new(half + 2, 1, half - 4, ClientSize.Height - 2), Quotas.Label(state.Quotas.Week), Artwork.WeekColor, state.Stale, Artwork.LightTheme);
    }
    protected override void Dispose(bool disposing) { if (disposing) tip.Dispose(); base.Dispose(disposing); }
}
