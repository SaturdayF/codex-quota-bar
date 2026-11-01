using System.Text.Json;
namespace CodexQuotaBar;

internal static class PaletteScheduleTests
{
    internal static void Run(Action<bool, string> check)
    {
        var settings = JsonSerializer.Deserialize<Settings>("{\"HourColorHex\":\"#E84A38\",\"WeekColorHex\":\"#9B42D2\",\"RefreshIntervalSeconds\":120}")!;
        var palette = settings.Palette();
        check(QuotaPalette.Hex(palette.Hour) == "#E84A38" && QuotaPalette.Hex(palette.Week) == "#9B42D2" && settings.RefreshIntervalSeconds == 120,
            "two colors and refresh interval load independently");
        var reloaded = JsonSerializer.Deserialize<Settings>(JsonSerializer.Serialize(settings))!;
        check(reloaded.Palette() == palette && reloaded.RefreshIntervalSeconds == 120, "palette and interval survive JSON round-trip");
        check(QuotaPalette.Parse("transparent", Color.Red) == Color.Red && QuotaPalette.Parse("#00FFFFFF", Color.Red) == Color.Red &&
            QuotaPalette.Parse(null, Color.Red) == Color.Red, "invalid or transparent colors cannot create invisible icons");
        check(QuotaPalette.TextOn(Color.Black) == Color.White && QuotaPalette.TextOn(Color.White) == Color.Black, "color-picker text contrasts with chosen color");
        foreach (var style in WidgetStyles.Choices)
        {
            var size = WidgetStyles.SizeFor(style.Id, false, 144);
            using var image = LayeredSurface.Paint(size, new(new(68, null), new(27, null)), style.Id, true, false, true, false, palette: palette);
            var hour = false; var week = false;
            for (var y = 0; y < image.Height; y++)
            for (var x = 0; x < image.Width; x++)
            {
                var color = image.GetPixel(x, y).ToArgb();
                hour |= color == palette.Hour.ToArgb(); week |= color == palette.Week.ToArgb();
            }
            check(hour && week, $"{style.Id} renders both independently selected colors");
        }
        check(Settings.RefreshOptions.Select(o => o.Seconds).SequenceEqual(new[] { 15, 30, 60, 120, 300 }) && Settings.NormalizeRefreshInterval(-1) == 30,
            "five refresh choices and invalid-value fallback");
        foreach (var option in Settings.RefreshOptions)
            check(RefreshSchedule.DelaySeconds(option.Seconds, true, 0) == option.Seconds && RefreshSchedule.DelaySeconds(option.Seconds, false, 1) == option.Seconds
                && RefreshSchedule.DelaySeconds(option.Seconds, false, 6) == 300, $"refresh {option.Seconds}s applies on success and backs off to 300s on failure");
        var now = DateTimeOffset.UtcNow;
        var q = new Quotas(new(68, null), new(27, null));
        check(!new DisplayState(q, now.AddSeconds(-100), null, 300).Stale && new DisplayState(q, now.AddSeconds(-100), null, 30).Stale,
            "long refresh interval does not prematurely mark valid data stale");
        check(new DisplayState(q, now.AddSeconds(-800), null, 300).Stale && new DisplayState(q, now, "offline", 300).Stale,
            "expired or failed reads remain stale for long intervals");
    }
}
