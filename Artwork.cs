using System.Drawing.Drawing2D;
using System.Drawing.Text;
using System.Runtime.InteropServices;

namespace CodexQuotaBar;

internal static class Artwork
{
    public static void Gauge(Graphics g, RectangleF area, QuotaWindow? window, string letter, Color accent, bool showLetter, bool stale, bool light, double? ringRemaining = null, bool showPercent = true)
    {
        var scale = area.Height / 30f;
        var size = 23 * scale;
        var circle = new RectangleF(area.X + scale, area.Y + (area.Height - size) / 2, size, size);
        using var track = new Pen(light ? Color.FromArgb(182, 192, 204) : Color.FromArgb(87, 99, 117), 2.5f * scale);
        using var ink = new Pen(stale ? Color.Gray : accent, 2.5f * scale);
        g.DrawEllipse(track, circle);
        var fill = ringRemaining ?? window?.Remaining ?? 0;
        if (window is not null && fill > 0) g.DrawArc(ink, circle, -90, (float)(Math.Clamp(fill, 0, 100) * 3.6));
        using var text = new SolidBrush(stale ? (light ? Color.DimGray : Color.Silver) : light ? Color.FromArgb(30, 38, 48) : Color.FromArgb(240, 244, 250));
        using var format = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center };
        using var labelFont = new Font("Segoe UI", 10 * scale, FontStyle.Bold, GraphicsUnit.Pixel);
        if (showLetter)
        {
            // Center the visible glyph, not the font's line box (which includes
            // ascent/descent padding and looks offset at mixed monitor DPI).
            using var glyph = new GraphicsPath();
            glyph.AddString(letter, labelFont.FontFamily, (int)FontStyle.Bold, 10 * scale, PointF.Empty, StringFormat.GenericTypographic);
            var inkBounds = glyph.GetBounds();
            using var transform = new Matrix();
            transform.Translate(circle.Left + circle.Width / 2 - (inkBounds.Left + inkBounds.Width / 2), circle.Top + circle.Height / 2 - (inkBounds.Top + inkBounds.Height / 2));
            glyph.Transform(transform);
            g.FillPath(text, glyph);
        }
        var numberArea = new RectangleF(circle.Right + 6 * scale, area.Y, Math.Max(1, area.Right - circle.Right - 6 * scale), area.Height);
        var label = Quotas.Label(window, showPercent);
        var fontSize = 17 * scale;
        while (fontSize > 10 * scale)
        {
            using var candidate = new Font("Segoe UI", fontSize, FontStyle.Bold, GraphicsUnit.Pixel);
            if (g.MeasureString(label, candidate).Width <= numberArea.Width) break;
            fontSize -= scale;
        }
        using var fitted = new Font("Segoe UI", fontSize, FontStyle.Bold, GraphicsUnit.Pixel);
        g.DrawString(label, fitted, text, numberArea, format);
        if (stale) { using var dot = new SolidBrush(accent); g.FillEllipse(dot, area.Right - 3 * scale, area.Top, 3 * scale, 3 * scale); }
    }
    public static readonly Color HourColor = Color.FromArgb(33, 158, 134);
    public static readonly Color WeekColor = Color.FromArgb(100, 141, 230);
    [DllImport("user32.dll")] private static extern bool DestroyIcon(IntPtr handle);
    public static bool LightTheme
    {
        get
        {
            using var key = Microsoft.Win32.Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize");
            return key?.GetValue("SystemUsesLightTheme") is int value && value != 0;
        }
    }
    public static Icon MakeIcon(string text, Color accent, bool stale)
    {
        using var bitmap = new Bitmap(64, 64);
        using (var g = Graphics.FromImage(bitmap))
        {
            g.Clear(Color.Transparent);
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
            Draw(g, new RectangleF(0, 0, 64, 64), text, accent, stale, LightTheme);
        }
        var handle = bitmap.GetHicon();
        try { using var borrowed = Icon.FromHandle(handle); return (Icon)borrowed.Clone(); }
        finally { DestroyIcon(handle); }
    }
    public static void Draw(Graphics g, RectangleF area, string text, Color accent, bool stale, bool light)
    {
        var size = area.Height * .64f;
        using var format = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center };
        // Four characters (100%) must fit a single notification slot.
        while (size > area.Height * .25f)
        {
            using var candidate = new Font("Segoe UI", size, FontStyle.Bold, GraphicsUnit.Pixel);
            if (g.MeasureString(text, candidate).Width <= area.Width + 1) break;
            size -= 1;
        }
        using var font = new Font("Segoe UI", size, FontStyle.Bold, GraphicsUnit.Pixel);
        using var brush = new SolidBrush(stale ? (light ? Color.DimGray : Color.Silver) : (light ? Color.FromArgb(30, 34, 40) : Color.FromArgb(243, 245, 248)));
        var textArea = new RectangleF(area.X, area.Y, area.Width, area.Height * .85f);
        g.DrawString(text, font, brush, textArea, format);
        using var underline = new SolidBrush(stale ? Color.Gray : accent);
        g.FillRectangle(underline, area.X + area.Width * .1f, area.Bottom - area.Height * .12f, area.Width * .8f, area.Height * .065f);
        if (stale)
        {
            // A status dot distinguishes stale data even when colors are hard to perceive.
            using var dot = new SolidBrush(accent);
            g.FillEllipse(dot, area.Right - area.Height * .12f, area.Top, area.Height * .1f, area.Height * .1f);
        }
    }
}
