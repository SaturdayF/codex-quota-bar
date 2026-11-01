namespace CodexQuotaBar;

internal sealed record WidgetLayout(Size SurfaceSize, RectangleF DrawingBounds)
{
    internal static double NormalizeScale(double value) => double.IsFinite(value) ? Math.Clamp(value, .6, 1.4) : 1;
    internal static Rectangle InkBounds(Bitmap image)
    {
        var left = image.Width; var top = image.Height; var right = -1; var bottom = -1;
        for (var y = 0; y < image.Height; y++)
        for (var x = 0; x < image.Width; x++)
            if (image.GetPixel(x, y).A > 1)
            { left = Math.Min(left, x); right = Math.Max(right, x); top = Math.Min(top, y); bottom = Math.Max(bottom, y); }
        return right < 0 ? Rectangle.Empty : Rectangle.FromLTRB(left, top, right + 1, bottom + 1);
    }
    internal static WidgetLayout Create(string style, bool weekOnly, int dpi, double requestedScale, int availableHeight, bool letters, StylePreferences preferences)
    {
        var basis = WidgetStyles.SizeFor(style, weekOnly, dpi);
        var scale = NormalizeScale(requestedScale);
        var maximumHeight = Math.Max(8, availableHeight);
        Size drawingSize = basis; Rectangle ink = Rectangle.Empty;
        // Measure visible marks rather than the nominal 32px box, whose padding can
        // be used for larger text. Fit the whole design uniformly if space runs out.
        for (var attempt = 0; attempt < 6; attempt++)
        {
            drawingSize = new(Math.Max(1, (int)Math.Round(basis.Width * scale)), Math.Max(1, (int)Math.Round(basis.Height * scale)));
            ink = Rectangle.Empty;
            foreach (double? value in new double?[] { 0, 68, 100, null })
            {
                var window = value is { } v ? new QuotaWindow(v, null) : null;
                using var image = LayeredSurface.Paint(drawingSize, new(window, window, weekOnly ? "pro" : null), style, letters, false, true, false,
                    preferences: preferences with { Animate = false });
                var bounds = InkBounds(image);
                if (!bounds.IsEmpty) ink = ink.IsEmpty ? bounds : Rectangle.Union(ink, bounds);
            }
            if (ink.Height <= maximumHeight - 2) break;
            scale *= Math.Max(.1, (maximumHeight - 2.0) / ink.Height) * .99;
        }
        var height = Math.Min(drawingSize.Height, maximumHeight);
        var offsetY = (height - ink.Height) / 2f - ink.Top;
        return new(new(drawingSize.Width, height), new(0, offsetY, drawingSize.Width, drawingSize.Height));
    }
}
