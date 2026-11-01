using System.Drawing.Drawing2D;
using System.Globalization;
namespace CodexQuotaBar;

internal static class WidgetStyles
{
    internal static readonly (string Id, string Name)[] Choices =
    [ ("gauges", "双仪表"), ("underline", "细线（底线数字）"), ("badges", "双徽标"), ("ticks", "双刻度"),
      ("plain", "纯数字"), ("outlined", "圆框数字"), ("side", "侧边数字") ];
    internal static string Normalize(string? style) => Choices.Any(c => c.Id == style) ? style! : "gauges";
    internal static Size SizeFor(string style, bool weekOnly, int dpi)
    {
        var cell = Normalize(style) switch { "gauges" => 92, "badges" => 94, "outlined" => 40, "ticks" => 82, _ => 68 };
        return new((int)(cell * (weekOnly ? 1 : 2) * dpi / 96f), (int)(32 * dpi / 96f));
    }
    internal static double GraphicFill(QuotaWindow? window, StylePreferences preferences, double? progress) =>
        window is null ? 0 : (preferences.ShowRemaining ? window.Remaining : 100) * Math.Clamp(progress ?? 1, 0, 1);
    internal static string NumberLabel(QuotaWindow? window, bool showPercent, double? progress = null) =>
        window is null || progress is null ? Quotas.Label(window, showPercent) :
        Math.Floor(window.Remaining * Math.Clamp(progress.Value, 0, 1)).ToString("0", CultureInfo.InvariantCulture) + (showPercent ? "%" : "");
    internal static RectangleF CircleIn(RectangleF area, float inset)
    {
        var size = Math.Max(1, Math.Min(area.Width, area.Height) - inset * 2);
        return new(area.Left + (area.Width - size) / 2, area.Top + (area.Height - size) / 2, size, size);
    }
    internal static void Draw(Graphics g, RectangleF bounds, Quotas quotas, string style, bool letters, bool stale, bool light,
        double? hourProgress = null, double? weekProgress = null, StylePreferences? preferences = null, QuotaPalette? palette = null)
    {
        style = Normalize(style); preferences ??= new(); palette ??= QuotaPalette.Default;
        var count = quotas.WeekOnly ? 1 : 2; var width = bounds.Width / count;
        var windows = quotas.WeekOnly ? new[] { (quotas.Week, "W", palette.Week) } : new[] { (quotas.Hour, "H", palette.Hour), (quotas.Week, "W", palette.Week) };
        var scale = bounds.Height / 32f;
        using var center = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center, FormatFlags = StringFormatFlags.NoWrap };
        var trackColor = light ? Color.FromArgb(190, 198, 208) : Color.FromArgb(85, 96, 112);
        for (var i = 0; i < count; i++)
        {
            var (window, letter, color) = windows[i]; var accent = stale ? Color.Gray : color;
            var area = new RectangleF(bounds.Left + width * i + 4 * scale, bounds.Top + 2 * scale, width - 8 * scale, bounds.Height - 4 * scale);
            var progress = preferences.Animate ? (letter == "H" ? hourProgress : weekProgress) : null;
            var fill = GraphicFill(window, preferences, progress);
            if (style == "gauges")
            {
                Artwork.Gauge(g, area, window, letter, accent, letters, stale, light, fill, preferences.ShowPercent); continue;
            }
            using var ink = new SolidBrush(stale ? Color.Gray : style == "plain" ? accent : light ? Color.FromArgb(32, 32, 32) : Color.FromArgb(245, 245, 245));
            using var colored = new SolidBrush(accent);
            using var track = new SolidBrush(trackColor);
            var label = NumberLabel(window, preferences.ShowPercent, style == "plain" ? progress : null);
            var textArea = area;
            if (style is "badges" or "ticks")
            {
                var tag = new RectangleF(area.Left + scale, area.Top + (area.Height - 20 * scale) / 2, 25 * scale, 20 * scale);
                if (style == "badges")
                {
                    using var path = HoverDetails.Round(tag, 3 * scale);
                    ProgressOutline(g, path, trackColor, accent, 2.2f * scale, fill);
                }
                using var small = new Font("Segoe UI", 10.5f * scale, FontStyle.Bold, GraphicsUnit.Pixel);
                g.DrawString(letter == "H" ? "5h" : "7d", small, colored, tag, center);
                textArea = new(tag.Right + 4 * scale, area.Top, Math.Max(1, area.Right - tag.Right - 4 * scale), area.Height);
            }
            if (style == "outlined")
            {
                var circle = CircleIn(area, 1.5f * scale);
                using var pen = new Pen(accent, 2 * scale);
                using var basePen = new Pen(trackColor, 2 * scale);
                g.DrawEllipse(basePen, circle);
                if (fill > 0) g.DrawArc(pen, circle, -90, (float)(3.6 * fill));
                textArea = RectangleF.Inflate(circle, -2 * scale, -2 * scale);
            }
            if (style is "underline" or "ticks")
            {
                var line = new RectangleF(area.Left, area.Bottom - 2.5f * scale, area.Width, 2.5f * scale);
                g.FillRectangle(track, line);
                if (fill > 0) g.FillRectangle(colored, line.X, line.Y, line.Width * (float)(fill / 100), line.Height);
                textArea.Height -= 4 * scale;
            }
            if (style == "side")
            {
                var bar = new RectangleF(area.Left, area.Top + 4 * scale, 3 * scale, area.Height - 8 * scale);
                g.FillRectangle(track, bar);
                if (fill > 0) g.FillRectangle(colored, bar.Left, bar.Bottom - bar.Height * (float)(fill / 100), bar.Width, bar.Height * (float)(fill / 100));
                textArea = new(area.Left + 6 * scale, area.Top, area.Width - 6 * scale, area.Height);
            }
            if (style == "outlined") DrawCenteredGlyph(g, label, textArea, ink, 14 * scale);
            else DrawNumber(g, label, textArea, ink, center, 18 * scale, 6 * scale);
        }
    }
    private static void DrawCenteredGlyph(Graphics g, string label, RectangleF area, Brush ink, float size)
    {
        using var family = new FontFamily("Segoe UI");
        using var glyph = new GraphicsPath();
        while (true)
        {
            glyph.Reset();
            glyph.AddString(label, family, (int)FontStyle.Bold, size, PointF.Empty, StringFormat.GenericTypographic);
            var bounds = glyph.GetBounds();
            if ((bounds.Width <= area.Width && bounds.Height <= area.Height) || size <= 1) break;
            size -= .25f;
        }
        var visible = glyph.GetBounds();
        using var transform = new Matrix();
        transform.Translate(area.Left + area.Width / 2 - (visible.Left + visible.Width / 2),
            area.Top + area.Height / 2 - (visible.Top + visible.Height / 2));
        glyph.Transform(transform); g.FillPath(ink, glyph);
    }
    private static void DrawNumber(Graphics g, string label, RectangleF area, Brush ink, StringFormat format, float size, float minimum)
    {
        while (size > minimum)
        {
            using var candidate = new Font("Segoe UI", size, FontStyle.Bold, GraphicsUnit.Pixel);
            var measured = g.MeasureString(label, candidate, int.MaxValue, format);
            if (measured.Width <= area.Width && measured.Height <= area.Height) break;
            size -= .5f;
        }
        using var font = new Font("Segoe UI", Math.Max(minimum, size), FontStyle.Bold, GraphicsUnit.Pixel);
        g.DrawString(label, font, ink, area, format);
    }
    // Flatten the rounded badge contour, then draw only the consumed length.
    private static void ProgressOutline(Graphics g, GraphicsPath outline, Color track, Color accent, float thickness, double percent)
    {
        using var basePen = new Pen(track, thickness) { LineJoin = LineJoin.Round };
        g.DrawPath(basePen, outline);
        if (percent <= 0) return;
        using var flattened = (GraphicsPath)outline.Clone(); flattened.Flatten(null, .1f);
        var points = flattened.PathPoints.ToList(); points.Add(points[0]);
        float Distance(PointF a, PointF b) => MathF.Sqrt((b.X - a.X) * (b.X - a.X) + (b.Y - a.Y) * (b.Y - a.Y));
        var length = Enumerable.Range(1, points.Count - 1).Sum(i => Distance(points[i - 1], points[i]));
        var left = length * (float)(Math.Clamp(percent, 0, 100) / 100);
        using var pen = new Pen(accent, thickness) { StartCap = LineCap.Round, EndCap = LineCap.Round, LineJoin = LineJoin.Round };
        for (var i = 1; i < points.Count && left > 0; i++)
        {
            var a = points[i - 1]; var b = points[i]; var segment = Distance(a, b);
            if (segment < .001f) continue;
            var ratio = Math.Min(1, left / segment);
            g.DrawLine(pen, a, new PointF(a.X + (b.X - a.X) * ratio, a.Y + (b.Y - a.Y) * ratio));
            left -= segment;
        }
    }
}
