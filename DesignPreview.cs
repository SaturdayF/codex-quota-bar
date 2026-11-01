namespace CodexQuotaBar;

internal static class DesignPreview
{
    internal static void Render(string directory)
    {
        Directory.CreateDirectory(directory);
        RenderStyles(directory);
        var sample = new Quotas(new(68, DateTimeOffset.UtcNow.AddHours(2)), new(27, DateTimeOffset.UtcNow.AddDays(3)), "plus");
        var state = new DisplayState(sample, DateTimeOffset.UtcNow, null);
        using var normal = new HoverDetails(); using var pro = new HoverDetails();
        normal.UpdateState(state); pro.UpdateState(state with { Quotas = sample with { PlanType = "pro" } });
        _ = normal.Handle; _ = pro.Handle;
        using var normalImage = new Bitmap(normal.Width, normal.Height);
        using var proImage = new Bitmap(pro.Width, pro.Height);
        normal.DrawToBitmap(normalImage, normal.ClientRectangle); pro.DrawToBitmap(proImage, pro.ClientRectangle);
        var width = normal.Width + pro.Width + 72;
        using var board = new Bitmap(width, Math.Max(normal.Height, pro.Height) + 192);
        using var g = Graphics.FromImage(board);
        g.Clear(Artwork.LightTheme ? Color.FromArgb(228, 234, 242) : Color.FromArgb(17, 23, 33));
        using var ink = new SolidBrush(Artwork.LightTheme ? Color.FromArgb(30, 43, 61) : Color.WhiteSmoke);
        using var title = new Font("Segoe UI", 15, FontStyle.Bold, GraphicsUnit.Pixel);
        using var small = new Font("Segoe UI", 12, FontStyle.Regular, GraphicsUnit.Pixel);
        g.DrawString("双仪表 + H/W   ·   非 Pro", title, ink, 24, 18);
        g.DrawString("Pro   ·   仅周额度", title, ink, normal.Width + 48, 18);
        g.DrawImage(normalImage, 24, 50); g.DrawImage(proImage, normal.Width + 48, 50);
        var bottom = Math.Max(normal.Height, pro.Height) + 72;
        Artwork.Gauge(g, new(28, bottom, 84, 30), sample.Hour, "H", Artwork.HourColor, true, false, Artwork.LightTheme);
        Artwork.Gauge(g, new(120, bottom, 84, 30), sample.Week, "W", Artwork.WeekColor, true, false, Artwork.LightTheme);
        Artwork.Gauge(g, new(normal.Width + 52, bottom, 84, 30), sample.Week, "W", Artwork.WeekColor, true, false, Artwork.LightTheme);
        g.DrawString("关闭 H/W 标签", small, ink, 28, bottom + 48);
        Artwork.Gauge(g, new(150, bottom + 42, 84, 30), sample.Hour, "H", Artwork.HourColor, false, false, Artwork.LightTheme);
        Artwork.Gauge(g, new(242, bottom + 42, 84, 30), sample.Week, "W", Artwork.WeekColor, false, false, Artwork.LightTheme);
        g.DrawString("原生界面渲染 · 所有额度与重置时间均为模拟示例", small, ink, 24, board.Height - 27);
        board.Save(Path.Combine(directory, "dial-ui-preview.png"));
        using var hoverBoard = new Bitmap(680, 172);
        using (var hg = Graphics.FromImage(hoverBoard))
        {
            hg.Clear(Color.FromArgb(224, 215, 204));
            using var caption = new SolidBrush(Color.FromArgb(45, 45, 45));
            hg.DrawString("普通状态", small, caption, 24, 15);
            hg.DrawString("悬浮状态：仅圆角半透明底色", small, caption, 336, 15);
            using var idleSurface = LayeredSurface.Paint(new(276, 48), sample, "gauges", true, false, true, false);
            using var hoverSurface = LayeredSurface.Paint(new(276, 48), sample, "gauges", true, false, true, true);
            hg.DrawImageUnscaled(idleSurface, 24, 48); hg.DrawImageUnscaled(hoverSurface, 336, 48);
            hg.DrawString("模拟数据 · 底色半透明，数字与环形仪表保持清晰", small, caption, 24, 128);
        }
        hoverBoard.Save(Path.Combine(directory, "hover-highlight-preview.png"));
        using var settings = new SettingsForm(initialSettings: new Settings());
        settings.ShowInTaskbar = false;
        settings.StartPosition = FormStartPosition.Manual;
        settings.Location = new(-30000, -30000);
        settings.Show(); Application.DoEvents(); settings.PerformLayout();
        using var settingsImage = new Bitmap(settings.Width, settings.Height);
        settings.DrawToBitmap(settingsImage, new Rectangle(Point.Empty, settings.Size));
        settingsImage.Save(Path.Combine(directory, "settings-preview.png"));
        settings.Hide();
        using var palette = new PalettePanel(Artwork.HourColor, Artwork.HourColor, _ => { }, () => { });
        using var paletteHost = new Form { ShowInTaskbar = false, StartPosition = FormStartPosition.Manual, Location = new(-30000, -30000), ClientSize = palette.Size, FormBorderStyle = FormBorderStyle.None };
        paletteHost.Controls.Add(palette); paletteHost.Show(); Application.DoEvents();
        using var paletteImage = new Bitmap(palette.Width, palette.Height);
        palette.DrawToBitmap(paletteImage, palette.ClientRectangle);
        paletteImage.Save(Path.Combine(directory, "color-palette-preview.png"));
        paletteHost.Hide();
    }
    private static void RenderStyles(string directory)
    {
        using var board = new Bitmap(1140, 710);
        using var g = Graphics.FromImage(board); g.Clear(Color.FromArgb(247, 247, 247));
        g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
        using var title = new Font("Microsoft YaHei UI", 17, FontStyle.Bold, GraphicsUnit.Pixel);
        using var text = new Font("Microsoft YaHei UI", 12, FontStyle.Regular, GraphicsUnit.Pixel);
        using var ink = new SolidBrush(Color.FromArgb(30, 30, 30));
        g.DrawString("七种样式 · 全部使用模拟额度 25% / 75%", title, ink, 24, 18);
        var names = new[] { "余量显示", "完整纯色", "隐藏百分号", "动画中途" };
        for (var column = 0; column < 4; column++) g.DrawString(names[column], text, ink, 168 + 240 * column, 57);
        for (var row = 0; row < WidgetStyles.Choices.Length; row++)
        {
            var style = WidgetStyles.Choices[row]; var y = 95 + row * 82;
            g.DrawString(style.Name, text, ink, 24, y + 6);
            for (var column = 0; column < 4; column++)
            {
                var size = WidgetStyles.SizeFor(style.Id, false, 96);
                using var image = LayeredSurface.Paint(size, new(new(25, null), new(75, null)), style.Id, true, false, true, false,
                    column == 3 ? .5 : null, column == 3 ? .5 : null, new() { ShowRemaining = column != 1, ShowPercent = column != 2 });
                g.DrawImageUnscaled(image, 168 + 240 * column, y);
            }
        }
        g.DrawString("纯数字无图形余量开关；其动画为 0 → 真实值。其余样式仅图形参与动画。", text, ink, 24, 680);
        board.Save(Path.Combine(directory, "style-options-preview.png"));
    }
}
