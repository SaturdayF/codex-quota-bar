using System.Globalization;
namespace CodexQuotaBar;

internal sealed record QuotaPalette(Color Hour, Color Week)
{
    internal static QuotaPalette Default => new(Artwork.HourColor, Artwork.WeekColor);
    internal static Color Parse(string? text, Color fallback) => text is { Length: 7 } && text[0] == '#' &&
        int.TryParse(text.AsSpan(1), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var rgb)
        ? Color.FromArgb(255, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255) : fallback;
    internal static string Hex(Color color) => "#" + (color.ToArgb() & 0xFFFFFF).ToString("X6", CultureInfo.InvariantCulture);
    internal static Color TextOn(Color color)
    {
        double Linear(byte value) { var c = value / 255d; return c <= .04045 ? c / 12.92 : Math.Pow((c + .055) / 1.055, 2.4); }
        return .2126 * Linear(color.R) + .7152 * Linear(color.G) + .0722 * Linear(color.B) > .179 ? Color.Black : Color.White;
    }
}
