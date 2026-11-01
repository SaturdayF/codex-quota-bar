using System.Drawing.Drawing2D;
namespace CodexQuotaBar;

internal sealed class HoverDetails : Form
{
    private DisplayState state = DisplayState.Empty;
    public HoverDetails()
    {
        Text = "Codex 额度悬浮详情";
        FormBorderStyle = FormBorderStyle.None;
        ShowInTaskbar = false; TopMost = true; DoubleBuffered = true;
        // The renderer uses logical pixels explicitly; avoid a second WinForms scale pass.
        AutoScaleMode = AutoScaleMode.None;
        ClientSize = new(340, 372);
    }
    protected override bool ShowWithoutActivation => true;
    protected override CreateParams CreateParams { get { var p = base.CreateParams; p.ExStyle |= 0x08000080; return p; } }
    protected override void WndProc(ref Message m)
    {
        if (m.Msg == 0x21) { m.Result = new IntPtr(3); return; }
        base.WndProc(ref m);
    }
    public void UpdateState(DisplayState value)
    {
        state = value;
        Reflow();
        Invalidate();
    }
    private void Reflow()
    {
        var target = new Size((int)(340 * DeviceDpi / 96f), (int)((state.Quotas.WeekOnly ? 260 : 372) * DeviceDpi / 96f));
        if (ClientSize != target) ClientSize = target;
    }
    protected override void OnHandleCreated(EventArgs e) { base.OnHandleCreated(e); Reflow(); }
    protected override void OnDpiChanged(DpiChangedEventArgs e) { base.OnDpiChanged(e); Reflow(); }
    internal static GraphicsPath Round(RectangleF r, float radius)
    {
        var p = new GraphicsPath(); var d = radius * 2;
        p.AddArc(r.Left, r.Top, d, d, 180, 90); p.AddArc(r.Right - d, r.Top, d, d, 270, 90);
        p.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90); p.AddArc(r.Left, r.Bottom - d, d, d, 90, 90); p.CloseFigure(); return p;
    }
    protected override void OnSizeChanged(EventArgs e)
    {
        base.OnSizeChanged(e);
        if (ClientSize.Width < 1 || ClientSize.Height < 1) return;
        using var path = Round(new(0, 0, ClientSize.Width, ClientSize.Height), 14 * DeviceDpi / 96f);
        var old = Region; Region = new Region(path); old?.Dispose();
    }
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
        return $"{reset.ToLocalTime():MM/dd HH:mm} 重置   {remaining}";
    }
    protected override void OnPaint(PaintEventArgs e)
    {
        var light = Artwork.LightTheme;
        var scale = DeviceDpi / 96f;
        var g = e.Graphics;
        g.Clear(light ? Color.FromArgb(249, 251, 254) : Color.FromArgb(26, 32, 43));
        g.ScaleTransform(scale, scale);
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;
        var height = ClientSize.Height / scale;
        using var frame = Round(new(.5f, .5f, 339, height - 1), 14);
        using var border = new Pen(light ? Color.FromArgb(192, 206, 223) : Color.FromArgb(73, 91, 116));
        g.DrawPath(border, frame);
        using var foreground = new SolidBrush(light ? Color.FromArgb(30, 43, 61) : Color.FromArgb(240, 244, 250));
        using var secondary = new SolidBrush(light ? Color.FromArgb(92, 109, 131) : Color.FromArgb(164, 183, 207));
        using var title = new Font("Segoe UI", 19, FontStyle.Bold, GraphicsUnit.Pixel);
        using var body = new Font("Segoe UI", 12, FontStyle.Regular, GraphicsUnit.Pixel);
        using var labelFont = new Font("Segoe UI", 13, FontStyle.Bold, GraphicsUnit.Pixel);
        using var number = new Font("Segoe UI", 29, FontStyle.Bold, GraphicsUnit.Pixel);
        void Text(string text, Font font, Brush brush, float x, float y) => g.DrawString(text, font, brush, x, y);
        Text("Codex", title, foreground, 20, 16);
        Text("可用额度", body, secondary, 20, 43);
        var badgeText = state.Quotas.WeekOnly ? "Pro" : state.Quotas.PlanType ?? "额度监控";
        using var badgeFill = new SolidBrush(light ? Color.FromArgb(229, 236, 247) : Color.FromArgb(47, 62, 85));
        using var badgePath = Round(new(246, 20, 74, 25), 12);
        g.FillPath(badgeFill, badgePath);
        using var centered = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center };
        g.DrawString(badgeText, body, foreground, new RectangleF(246, 20, 74, 25), centered);
        void Row(string label, string letter, QuotaWindow? window, Color accent, int y)
        {
            using var surface = new SolidBrush(light ? Color.FromArgb(236, 241, 248) : Color.FromArgb(36, 46, 62));
            using var card = Round(new(16, y, 308, 102), 10); g.FillPath(surface, card);
            using var accentInk = new SolidBrush(accent);
            using var letterSurface = new SolidBrush(light ? Color.White : Color.FromArgb(26, 35, 49));
            using var letterPath = Round(new(30, y + 13, 24, 24), 7); g.FillPath(letterSurface, letterPath);
            g.DrawString(letter, labelFont, accentInk, new RectangleF(30, y + 13, 24, 24), centered);
            Text(label, labelFont, foreground, 62, y + 12);
            Text(window is null ? "暂未返回数据" : $"已用 {100 - window.Remaining:0.##}%", body, secondary, 62, y + 34);
            using var right = new StringFormat { Alignment = StringAlignment.Far };
            g.DrawString(window is null ? "--" : $"{window.Remaining:0.##}%", number, foreground, new RectangleF(200, y + 12, 108, 42), right);
            using var track = new SolidBrush(light ? Color.FromArgb(209, 220, 233) : Color.FromArgb(60, 76, 99));
            using var fill = new SolidBrush(accent);
            g.FillRectangle(track, 30, y + 62, 278, 4);
            if (window is not null) g.FillRectangle(fill, 30, y + 62, (float)(278 * window.Remaining / 100), 4);
            Text(ResetText(window), body, secondary, 30, y + 77);
        }
        if (!state.Quotas.WeekOnly) Row("5 小时额度", "H", state.Quotas.Hour, Artwork.HourColor, 76);
        Row("周额度", "W", state.Quotas.Week, Artwork.WeekColor, state.Quotas.WeekOnly ? 76 : 188);
        var updated = state.Updated is { } time ? time.ToLocalTime().ToString("HH:mm:ss") : "尚无";
        var footer = $"更新于 {updated} · 每 30 秒刷新\n" + (state.Error ?? (state.Stale ? "数据已过期" : ""));
        using var statusInk = new SolidBrush(state.Stale ? Color.FromArgb(193, 133, 49) : Artwork.HourColor);
        g.FillEllipse(statusInk, 22, height - 58, 6, 6);
        g.DrawString(footer, body, secondary, new RectangleF(34, height - 64, 286, 56));
    }
}
