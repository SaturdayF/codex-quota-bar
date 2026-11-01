using System.Text.Json;
namespace CodexQuotaBar;

internal static class StyleTests
{
    internal static void Run(Action<bool, string> check)
    {
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
        form.ShowInTaskbar = false; form.StartPosition = FormStartPosition.Manual; form.Location = new(-30000, -30000); form.Show(); Application.DoEvents();
        IEnumerable<Control> Children(Control c) => c.Controls.Cast<Control>().SelectMany(child => new[] { child }.Concat(Children(child)));
        var controls = Children(form).ToArray();
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
        using var parent = new Panel { BackColor = Color.White };
        using var button = new RoundedSettingsButton { Size = new(112, 34), BackColor = Color.Black, ForeColor = Color.White, Text = "保存", FlatStyle = FlatStyle.Flat, UseVisualStyleBackColor = false };
        parent.Controls.Add(button); using var buttonImage = new Bitmap(112, 34); button.DrawToBitmap(buttonImage, button.ClientRectangle);
        var corner = buttonImage.GetPixel(0, 0);
        check((corner.A == 0 || corner.ToArgb() == Color.White.ToArgb()) && buttonImage.GetPixel(56, 3).ToArgb() == Color.Black.ToArgb(), "settings action buttons have visibly rounded corners");
        form.Close();
    }
}
