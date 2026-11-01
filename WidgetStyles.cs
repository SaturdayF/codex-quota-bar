namespace CodexQuotaBar;

internal static class WidgetStyles
{
    internal static readonly (string Id, string Name)[] Choices =
    [ ("gauges", "双仪表"), ("underline", "细线（底线数字）"), ("badges", "双徽标"), ("ticks", "双刻度"),
      ("plain", "纯数字"), ("outlined", "圆框数字"), ("side", "侧边数字") ];
    internal static string Normalize(string? style) => Choices.Any(c => c.Id == style) ? style! : "gauges";
    internal static Size SizeFor(string style, bool weekOnly, int dpi)
    {
        var cell = Normalize(style) switch { "gauges" => 92, "badges" => 94, "outlined" => 74, "ticks" => 82, _ => 68 };
        return new((int)(cell * (weekOnly ? 1 : 2) * dpi / 96f), (int)(32 * dpi / 96f));
    }
    internal static void Draw(Graphics g, RectangleF bounds, Quotas quotas, string style, bool letters, bool stale, bool light, double? hourFill = null, double? weekFill = null)
    {
        style = Normalize(style);
        var count = quotas.WeekOnly ? 1 : 2;
        var width = bounds.Width / count;
        var windows = quotas.WeekOnly ? new[] { (quotas.Week, "W", Artwork.WeekColor) } : new[] { (quotas.Hour, "H", Artwork.HourColor), (quotas.Week, "W", Artwork.WeekColor) };
        for (var i = 0; i < count; i++)
        {
            var (window, letter, accent) = windows[i];
            var area = new RectangleF(bounds.Left + width * i + 4, bounds.Top + 2, width - 8, bounds.Height - 4);
            if (style == "gauges") { Artwork.Gauge(g, area, window, letter, accent, letters, stale, light, letter == "H" ? hourFill : weekFill); continue; }
            if (style == "underline") { Artwork.Draw(g, area, Quotas.Label(window), accent, stale, light); continue; }
            var scale = bounds.Height / 32f;
            using var ink = new SolidBrush(stale ? Color.Gray : light ? Color.FromArgb(32, 32, 32) : Color.FromArgb(245, 245, 245));
            using var colored = new SolidBrush(stale ? Color.Gray : accent);
            using var pen = new Pen(stale ? Color.Gray : accent, scale);
            using var center = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center };
            using var font = new Font("Segoe UI", (Quotas.Label(window).Length >= 4 ? 15 : 18) * scale, FontStyle.Bold, GraphicsUnit.Pixel);
            using var small = new Font("Segoe UI", 10 * scale, FontStyle.Regular, GraphicsUnit.Pixel);
            var textArea = area;
            if (style == "badges" || style == "ticks")
            {
                var tag = new RectangleF(area.Left, area.Top + (area.Height - 18 * scale) / 2, 21 * scale, 18 * scale);
                if (style == "badges") g.DrawRectangle(pen, tag.X, tag.Y, tag.Width, tag.Height);
                g.DrawString(letter == "H" ? "5h" : "7d", small, colored, tag, center);
                textArea = new(tag.Right + 3 * scale, area.Top, area.Right - tag.Right - 3 * scale, area.Height);
            }
            if (style == "outlined") g.DrawEllipse(pen, area);
            if (style == "side") g.FillRectangle(colored, area.Left, area.Top + 5 * scale, 2 * scale, area.Height - 10 * scale);
            if (style == "ticks")
            {
                using var track = new SolidBrush(light ? Color.FromArgb(195, 202, 212) : Color.FromArgb(83, 92, 105));
                g.FillRectangle(track, area.Left, area.Bottom - 2 * scale, area.Width, 2 * scale);
                if (window is not null) g.FillRectangle(colored, area.Left, area.Bottom - 2 * scale, area.Width * (float)(window.Remaining / 100), 2 * scale);
                textArea.Height -= 3 * scale;
            }
            g.DrawString(Quotas.Label(window), font, ink, textArea, center);
        }
    }
}
