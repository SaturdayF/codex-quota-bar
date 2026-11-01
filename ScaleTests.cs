using System.Text.Json;
namespace CodexQuotaBar;

internal static class ScaleTests
{
    internal static void Run(Action<bool, string> check)
    {
        check(JsonSerializer.Deserialize<Settings>("{\"WidgetScale\":1.2}")?.WidgetScale == 1.2 && new Settings().WidgetScale == 1,
            "size preference persists and old settings keep 100 percent");
        check(WidgetLayout.NormalizeScale(double.NaN) == 1 && WidgetLayout.NormalizeScale(4) == 1.4 && WidgetLayout.NormalizeScale(.1) == .6,
            "size preference rejects nonfinite and out-of-range values");
        foreach (var dpi in new[] { 96, 144, 192 })
        foreach (var style in WidgetStyles.Choices)
        {
            var height = 32 * dpi / 96;
            var options = new StylePreferences();
            var small = WidgetLayout.Create(style.Id, false, dpi, .6, height, true, options);
            var large = WidgetLayout.Create(style.Id, false, dpi, 1.4, height, true, options);
            var fits = large.SurfaceSize.Height <= height && large.SurfaceSize.Width > small.SurfaceSize.Width;
            foreach (var weekOnly in new[] { false, true })
            foreach (var percent in new[] { true, false })
            {
                var preferences = options with { ShowPercent = percent };
                var layout = WidgetLayout.Create(style.Id, weekOnly, dpi, 1.4, height, true, preferences);
                using var image = LayeredSurface.Paint(layout.SurfaceSize, new(new(68, null), new(27, null), weekOnly ? "pro" : null), style.Id, true, false, true, false,
                    preferences: preferences, drawingBounds: layout.DrawingBounds);
                var ink = WidgetLayout.InkBounds(image);
                fits &= !ink.IsEmpty && ink.Top >= 0 && ink.Bottom <= height && Math.Abs((ink.Top + ink.Bottom) / 2f - layout.SurfaceSize.Height / 2f) <= 1.5;
                // A fully opaque mark at a window edge would indicate cropping.
                fits &= Enumerable.Range(0, image.Width).All(x => image.GetPixel(x, 0).A <= 1 && image.GetPixel(x, image.Height - 1).A <= 1);
            }
            check(fits, $"{style.Id} scales and fits taskbar without clipping at {dpi} DPI, dual/Pro, percent on/off");
        }
    }
}
