namespace CodexQuotaBar;

internal sealed class HoverDetails : Form
{
    private DisplayState state = DisplayState.Empty;
    public HoverDetails()
    {
        Text = "Codex 额度悬浮详情";
        FormBorderStyle = FormBorderStyle.None;
        ShowInTaskbar = false; TopMost = true; DoubleBuffered = true;
        AutoScaleMode = AutoScaleMode.Dpi;
        ClientSize = new(310, 238);
    }
    protected override bool ShowWithoutActivation => true;
    protected override CreateParams CreateParams { get { var p = base.CreateParams; p.ExStyle |= 0x08000080; return p; } }
    protected override void WndProc(ref Message m)
    {
        if (m.Msg == 0x21) { m.Result = new IntPtr(3); return; }
        base.WndProc(ref m);
    }
    public void UpdateState(DisplayState value) { state = value; Invalidate(); }
    public void Place(Rectangle anchor, Rectangle workingArea)
    {
        var gap = (int)(8 * DeviceDpi / 96f);
        var x = Math.Clamp(anchor.Left, workingArea.Left, Math.Max(workingArea.Left, workingArea.Right - Width));
        var y = anchor.Top - Height - gap;
        if (y < workingArea.Top) y = anchor.Bottom + gap;
        Location = new(x, Math.Clamp(y, workingArea.Top, Math.Max(workingArea.Top, workingArea.Bottom - Height)));
    }
    private static string ResetText(QuotaWindow? window)
    {
        if (window?.ResetsAt is not { } reset) return "重置时间未知";
        var left = reset - DateTimeOffset.UtcNow;
        var remaining = left <= TimeSpan.Zero ? "等待服务端更新" : left.TotalDays >= 1 ? $"{(int)left.TotalDays} 天 {left.Hours} 小时后" : $"{(int)left.TotalHours} 小时 {left.Minutes} 分后";
        return $"{reset.ToLocalTime():MM/dd HH:mm} 重置 · {remaining}";
    }
    protected override void OnPaint(PaintEventArgs e)
    {
        var light = Artwork.LightTheme;
        var scale = DeviceDpi / 96f;
        var g = e.Graphics;
        g.Clear(light ? Color.FromArgb(250, 251, 253) : Color.FromArgb(30, 33, 40));
        using var border = new Pen(light ? Color.FromArgb(186, 196, 210) : Color.FromArgb(86, 96, 112));
        g.DrawRectangle(border, 0, 0, Width - 1, Height - 1);
        using var foreground = new SolidBrush(light ? Color.FromArgb(30, 38, 48) : Color.FromArgb(240, 243, 248));
        using var secondary = new SolidBrush(light ? Color.FromArgb(83, 95, 110) : Color.FromArgb(179, 190, 206));
        using var title = new Font("Segoe UI", 10, FontStyle.Bold);
        using var body = new Font("Segoe UI", 9);
        void Text(string text, Font font, Brush brush, int x, int y) => g.DrawString(text, font, brush, x * scale, y * scale);
        Text("Codex 额度", title, foreground, 16, 12);
        void Row(string label, QuotaWindow? window, Color accent, int y)
        {
            Text(label, body, foreground, 16, y);
            var value = window is null ? "不可用" : $"剩余 {window.Remaining:0.##}%";
            Text(value, title, foreground, 160, y - 1);
            using var track = new SolidBrush(light ? Color.FromArgb(224, 230, 238) : Color.FromArgb(57, 66, 80));
            using var fill = new SolidBrush(accent);
            g.FillRectangle(track, 16 * scale, (y + 25) * scale, 278 * scale, 4 * scale);
            if (window is not null) g.FillRectangle(fill, 16 * scale, (y + 25) * scale, (float)(278 * window.Remaining / 100) * scale, 4 * scale);
            Text(ResetText(window), body, secondary, 16, y + 34);
        }
        Row("5 小时窗口", state.Quotas.Hour, Artwork.HourColor, 44);
        Row("每周窗口", state.Quotas.Week, Artwork.WeekColor, 112);
        var updated = state.Updated is { } time ? time.ToLocalTime().ToString("HH:mm:ss") : "尚无";
        var footer = $"更新于 {updated} · 每 30 秒刷新\n" + (state.Error ?? (state.Stale ? "数据已过期" : ""));
        g.DrawString(footer, body, secondary, new RectangleF(16 * scale, 185 * scale, 278 * scale, 49 * scale));
    }
}
