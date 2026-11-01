using System.Text.Json;
namespace CodexQuotaBar;

internal static class StyleTests
{
    internal static void Run(Action<bool, string> check)
    {
        Color? chosen = null; var customRequested = false;
        using (var colors = new PalettePanel(Color.Red, Artwork.HourColor, c => chosen = c, () => customRequested = true))
        {
            check(colors.Swatches.Count == 70, "palette offers 60 theme shades and 10 standard colors");
            colors.Swatches[64].PerformClick();
            check(chosen?.ToArgb() == Color.FromArgb(146, 208, 80).ToArgb(), "palette standard swatch selects its exact color");
            colors.Automatic.PerformClick();
            check(chosen == Artwork.HourColor, "automatic color restores corresponding quota default");
            colors.Custom.PerformClick(); check(customRequested, "palette custom action opens extended color flow");
        }
        var old = JsonSerializer.Deserialize<Settings>("{\"AnimateQuotaReset\":false}")!;
        check(!old.ForStyle("plain").Animate && old.ForStyle("badges").ShowPercent, "old settings preserve disabled animation and percentage default");
        var settings = new Settings();
        settings.StyleOptions["badges"] = new() { ShowRemaining = false, ShowPercent = false, Animate = false };
        var reloaded = JsonSerializer.Deserialize<Settings>(JsonSerializer.Serialize(settings))!;
        check(!reloaded.ForStyle("badges").ShowRemaining && !reloaded.ForStyle("badges").ShowPercent && !reloaded.ForStyle("badges").Animate
            && reloaded.ForStyle("ticks").ShowRemaining && reloaded.ForStyle("ticks").Animate, "style options round-trip independently");
        var q = new Quotas(new(25, null), new(75, null));
        check(WidgetStyles.NumberLabel(q.Hour, true, 0) == "0%" && WidgetStyles.NumberLabel(q.Hour, false, .5) == "12"
            && WidgetStyles.NumberLabel(q.Hour, true, 1) == "25%" && WidgetStyles.NumberLabel(null, false, .5) == "--", "plain counter ends at actual quota and preserves unavailable values");
        check(WidgetStyles.GraphicFill(q.Hour, new(), .5) == 12.5 && WidgetStyles.GraphicFill(q.Hour, new() { ShowRemaining = false }, .5) == 50,
            "solid mode animates to full geometry independently of remaining quota");
        foreach (var dpi in new[] { 96, 120, 144, 192 })
        {
            var size = WidgetStyles.SizeFor("outlined", false, dpi);
            var circle = WidgetStyles.CircleIn(new(0, 0, size.Width / 2, size.Height), dpi / 96f);
            check(circle.Width == circle.Height, $"outlined geometry is a true circle at {dpi} DPI");
        }
        foreach (var dpi in new[] { 96, 120, 144, 192 })
        foreach (var showPercent in new[] { false, true })
        {
            var size = WidgetStyles.SizeFor("outlined", false, dpi);
            var centered = true;
            foreach (double? value in new double?[] { 0, 7, 27, 68, 100, null })
            {
                var window = value is { } glyphValue ? new QuotaWindow(glyphValue, null) : null;
                using var image = LayeredSurface.Paint(size, new(window, window), "outlined", true, false, true, false,
                    preferences: new() { ShowPercent = showPercent, Animate = false });
                var pixels = new List<Point>();
                for (var y = 0; y < image.Height; y++)
                for (var x = 0; x < image.Width / 2; x++)
                {
                    var color = image.GetPixel(x, y);
                    if (color.A > 128 && color.R < 100 && Math.Abs(color.R - color.G) < 5 && Math.Abs(color.G - color.B) < 5) pixels.Add(new(x, y));
                }
                centered &= pixels.Count > 0 && Math.Abs((pixels.Min(p => p.X) + pixels.Max(p => p.X) + 1) / 2f - size.Width / 4f) <= 1
                    && Math.Abs((pixels.Min(p => p.Y) + pixels.Max(p => p.Y) + 1) / 2f - size.Height / 2f) <= 1;
            }
            check(centered, $"outlined visible numbers and -- centered at {dpi} DPI, percent={showPercent}");
        }
        bool Different(Bitmap a, Bitmap b)
        {
            for (var y = 0; y < a.Height; y++)
            for (var x = 0; x < a.Width; x++)
                if (a.GetPixel(x, y) != b.GetPixel(x, y)) return true;
            return false;
        }
        foreach (var style in WidgetStyles.Choices)
        foreach (var light in new[] { true, false })
        {
            var size = WidgetStyles.SizeFor(style.Id, false, 144);
            using var regular = LayeredSurface.Paint(size, q, style.Id, true, false, light, false);
            using var noPercent = LayeredSurface.Paint(size, q, style.Id, true, false, light, false, preferences: new() { ShowPercent = false });
            using var animated = LayeredSurface.Paint(size, q, style.Id, true, false, light, false, .4, .4);
            check(Different(regular, noPercent) && Different(regular, animated), $"{style.Id} percentage and animation reach renderer ({(light ? "light" : "dark")})");
            using var disabled = LayeredSurface.Paint(size, q, style.Id, true, false, light, false, .4, .4, new() { Animate = false });
            check(!Different(regular, disabled), $"{style.Id} disabled animation stays at real values ({(light ? "light" : "dark")})");
            if (style.Id != "plain")
            {
                using var solid = LayeredSurface.Paint(size, q, style.Id, true, false, light, false, preferences: new() { ShowRemaining = false });
                check(Different(regular, solid), $"{style.Id} remaining switch changes geometry ({(light ? "light" : "dark")})");
            }
        }
        foreach (var style in new[] { "underline", "ticks" })
        {
            var size = WidgetStyles.SizeFor(style, false, 96);
            using var image = LayeredSurface.Paint(size, q, style, true, false, true, false);
            check(image.GetPixel(7, 29).ToArgb() == Artwork.HourColor.ToArgb() && image.GetPixel(size.Width / 2 - 8, 29).ToArgb() != Artwork.HourColor.ToArgb(), $"{style} progress grows left to right");
        }
        using var form = new SettingsForm(initialSettings: new Settings());
        var settingsDesignWidth = form.ClientSize.Width; var settingsDesignDpi = form.DeviceDpi;
        form.ShowInTaskbar = false; form.StartPosition = FormStartPosition.Manual; form.Location = new(-30000, -30000); form.Show(); Application.DoEvents();
        IEnumerable<Control> Children(Control c) => c.Controls.Cast<Control>().SelectMany(child => new[] { child }.Concat(Children(child)));
        var controls = Children(form).ToArray();
        var refreshChoices = controls.OfType<ComboBox>().Single(c => c.AccessibleName == "刷新时间间隔");
        check(refreshChoices.Items.Count == 5 && refreshChoices.SelectedIndex == 1, "settings exposes five refresh choices with 30-second default");
        check(controls.OfType<RoundedSettingsButton>().Single(c => c.AccessibleName == "5 小时图标颜色").BackColor == Artwork.HourColor &&
            controls.OfType<RoundedSettingsButton>().Single(c => c.AccessibleName == "周额度图标颜色").BackColor == Artwork.WeekColor,
            "settings exposes independent hour and week color pickers");
        var choice = controls.OfType<ComboBox>().Single(c => c.AccessibleName == "任务栏样式");
        var remaining = controls.OfType<SettingsToggle>().Single(c => c.AccessibleName == "在图标上展示余量");
        var percent = controls.OfType<SettingsToggle>().Single(c => c.AccessibleName == "显示百分号");
        var animate = controls.OfType<SettingsToggle>().Single(c => c.AccessibleName == "启动与额度重置动画");
        choice.SelectedIndex = 2; remaining.Checked = false; percent.Checked = false; animate.Checked = false;
        choice.SelectedIndex = 3;
        check(remaining.Checked && percent.Checked && animate.Checked, "switching style restores its own option values");
        choice.SelectedIndex = 2;
        check(!remaining.Checked && !percent.Checked && !animate.Checked, "switching back retains unsaved badge preferences");
        choice.SelectedIndex = 4;
        check(!remaining.Enabled && percent.Enabled && animate.Enabled, "plain style has percent and animation but no geometry option");
        var preview = controls.OfType<SettingsPreviewPanel>().Single();
        using var switched = new Bitmap(preview.Width, preview.Height); preview.DrawToBitmap(switched, preview.ClientRectangle);
        var tick = Environment.TickCount64;
        while (Environment.TickCount64 - tick < 150) { Application.DoEvents(); Thread.Sleep(5); }
        using var inMotion = new Bitmap(preview.Width, preview.Height); preview.DrawToBitmap(inMotion, preview.ClientRectangle);
        check(!QuotaAnimation.MotionEnabled || Different(switched, inMotion), "switching to animated style replays preview automatically");
        choice.SelectedIndex = 2;
        using var staticStart = new Bitmap(preview.Width, preview.Height); preview.DrawToBitmap(staticStart, preview.ClientRectangle);
        tick = Environment.TickCount64;
        while (Environment.TickCount64 - tick < 150) { Application.DoEvents(); Thread.Sleep(5); }
        using var staticEnd = new Bitmap(preview.Width, preview.Height); preview.DrawToBitmap(staticEnd, preview.ClientRectangle);
        check(!Different(staticStart, staticEnd), "switching to animation-disabled style remains static");
        var sizing = controls.OfType<TrackBar>().Single(c => c.AccessibleName == "显示大小");
        sizing.Value = 60;
        using var smallPreview = new Bitmap(preview.Width, preview.Height); preview.DrawToBitmap(smallPreview, preview.ClientRectangle);
        sizing.Value = 140;
        using var largePreview = new Bitmap(preview.Width, preview.Height); preview.DrawToBitmap(largePreview, preview.ClientRectangle);
        check(Different(smallPreview, largePreview), "display size slider updates the native preview");
        var expectedWidth = (int)Math.Round(settingsDesignWidth * form.DeviceDpi / (double)settingsDesignDpi);
        check(settingsDesignWidth >= 1000 && form.Width <= Screen.FromControl(form).WorkingArea.Width && form.ClientSize.Width >= expectedWidth - 2,
            "settings keeps wider dimensions after per-monitor DPI changes");
        using var parent = new Panel { BackColor = Color.White };
        using var button = new RoundedSettingsButton { Size = new(112, 34), BackColor = Color.Black, ForeColor = Color.White, Text = "保存", FlatStyle = FlatStyle.Flat, UseVisualStyleBackColor = false };
        parent.Controls.Add(button); using var buttonImage = new Bitmap(112, 34); button.DrawToBitmap(buttonImage, button.ClientRectangle);
        var corner = buttonImage.GetPixel(0, 0);
        check(corner.ToArgb() == Color.White.ToArgb() && buttonImage.GetPixel(56, 3).ToArgb() == Color.Black.ToArgb(), "rounded button paints opaque parent-colored corners instead of black/transparent residue");
        foreach (var background in new[] { Color.White, Color.FromArgb(247, 247, 247), Color.FromArgb(32, 32, 32) })
        foreach (var width in new[] { 112, 158, 204 })
        {
            parent.BackColor = background; button.Width = width;
            using var rendered = new Bitmap(width, button.Height); button.DrawToBitmap(rendered, button.ClientRectangle);
            check(new[] { new Point(0, 0), new Point(width - 1, 0), new Point(0, button.Height - 1), new Point(width - 1, button.Height - 1) }
                .All(p => rendered.GetPixel(p.X, p.Y).ToArgb() == background.ToArgb()), $"button corners repaint cleanly on background {background.R}, width {width}");
        }
        form.Close();
    }
}
