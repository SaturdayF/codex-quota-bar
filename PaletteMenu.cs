namespace CodexQuotaBar;

// Compact Office-style palette: automatic, theme columns, standard colors, custom.
internal sealed class PaletteMenu : ToolStripDropDown
{
    internal PalettePanel Palette { get; }
    internal PaletteMenu(Color current, Color automatic, Action<Color> selected, Action custom)
    {
        Padding = new Padding(1); AutoClose = true; DropShadowEnabled = true;
        Palette = new(current, automatic, color => { Close(); selected(color); }, () => { Close(); custom(); });
        var host = new ToolStripControlHost(Palette) { Margin = Padding.Empty, Padding = Padding.Empty, AutoSize = false, Size = Palette.Size };
        Items.Add(host);
    }
}

internal sealed class PalettePanel : UserControl
{
    internal static readonly string[] Themes = ["#FFFFFF", "#000000", "#E7E6E6", "#44546A", "#4472C4", "#ED7D31", "#FFC000", "#70AD47", "#2BBBAD", "#E74765"];
    internal static readonly string[] Standards = ["#C00000", "#FF0000", "#FFC000", "#FFFF00", "#92D050", "#00B050", "#00B0F0", "#0070C0", "#002060", "#7030A0"];
    internal readonly List<Button> Swatches = [];
    internal Button Automatic { get; }
    internal Button Custom { get; }
    internal PalettePanel(Color current, Color automatic, Action<Color> selected, Action custom)
    {
        AutoScaleMode = AutoScaleMode.Dpi; AutoScaleDimensions = new(96, 96);
        Size = new(294, 312); Font = new("Microsoft YaHei UI", 9);
        var light = Artwork.LightTheme;
        BackColor = light ? Color.White : Color.FromArgb(32, 32, 32);
        ForeColor = light ? Color.FromArgb(25, 25, 25) : Color.WhiteSmoke;
        var header = light ? Color.FromArgb(243, 244, 246) : Color.FromArgb(46, 46, 46);
        Automatic = new RoundedSettingsButton { Text = "自动", TextAlign = ContentAlignment.MiddleLeft, Padding = new(38, 0, 0, 0),
            Bounds = new(8, 8, 278, 30), BackColor = header, ForeColor = ForeColor, AccessibleName = "自动颜色（恢复默认）" };
        var automaticSample = new Panel { Bounds = new(12, 6, 18, 18), BackColor = automatic, Cursor = Cursors.Hand };
        automaticSample.Click += (_, _) => Automatic.PerformClick(); Automatic.Controls.Add(automaticSample);
        Automatic.Click += (_, _) => selected(automatic); Controls.Add(Automatic);
        void Header(string text, int y)
        {
            Controls.Add(new Label { Text = text, Bounds = new(8, y, 278, 25), Padding = new(8, 0, 0, 0), TextAlign = ContentAlignment.MiddleLeft,
                BackColor = header, ForeColor = light ? Color.FromArgb(45, 70, 115) : Color.FromArgb(180, 204, 244) });
        }
        Header("主题颜色", 41);
        for (var row = 0; row < 6; row++)
        for (var column = 0; column < 10; column++)
        {
            var basis = QuotaPalette.Parse(Themes[column], Color.Black);
            var color = ThemeShade(basis, column, row);
            AddSwatch(color, new(14 + column * 27, 73 + row * 23, 21, 21));
        }
        Header("标准色", 216);
        for (var column = 0; column < Standards.Length; column++)
            AddSwatch(QuotaPalette.Parse(Standards[column], Color.Black), new(14 + column * 27, 248, 21, 21));
        Custom = new RoundedSettingsButton { Text = "其他颜色…", Bounds = new(8, 278, 278, 28), BackColor = BackColor, ForeColor = ForeColor,
            TextAlign = ContentAlignment.MiddleLeft, Padding = new(8, 0, 0, 0), AccessibleName = "其他颜色" };
        Custom.Click += (_, _) => custom(); Controls.Add(Custom);
        void AddSwatch(Color color, Rectangle bounds)
        {
            var swatch = new PaletteSwatch(color, color.ToArgb() == current.ToArgb()) { Bounds = bounds, AccessibleName = "颜色 " + QuotaPalette.Hex(color) };
            swatch.Click += (_, _) => selected(color); Swatches.Add(swatch); Controls.Add(swatch);
        }
    }
    internal static Color ThemeShade(Color basis, int column, int row)
    {
        if (row == 0) return basis;
        if (column < 3)
        {
            int[][] grays = [[242, 217, 191, 166, 128], [128, 89, 64, 38, 13], [208, 175, 135, 89, 38]];
            var gray = grays[column][row - 1]; return Color.FromArgb(gray, gray, gray);
        }
        var amount = new[] { 0d, .8, .6, .4, -.25, -.5 }[row];
        int Mix(byte channel) => (int)Math.Round(amount >= 0 ? channel + (255 - channel) * amount : channel * (1 + amount));
        return Color.FromArgb(Mix(basis.R), Mix(basis.G), Mix(basis.B));
    }
}

internal sealed class PaletteSwatch : Button
{
    private readonly Color color;
    private readonly bool selected;
    private bool hover;
    internal PaletteSwatch(Color color, bool selected)
    {
        this.color = color; this.selected = selected;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
        Cursor = Cursors.Hand; FlatStyle = FlatStyle.Flat;
    }
    protected override void OnMouseEnter(EventArgs e) { hover = true; Invalidate(); base.OnMouseEnter(e); }
    protected override void OnMouseLeave(EventArgs e) { hover = false; Invalidate(); base.OnMouseLeave(e); }
    protected override void OnPaint(PaintEventArgs e)
    {
        e.Graphics.Clear(Parent?.BackColor ?? SystemColors.Control);
        using var brush = new SolidBrush(color); e.Graphics.FillRectangle(brush, 1, 1, Width - 2, Height - 2);
        using var border = new Pen(selected || hover || Focused ? Color.FromArgb(0, 120, 215) : Color.FromArgb(215, 218, 223), selected || hover || Focused ? 2 : 1);
        e.Graphics.DrawRectangle(border, 1, 1, Width - 3, Height - 3);
    }
    protected override void OnKeyDown(KeyEventArgs e)
    {
        var offset = e.KeyCode switch { Keys.Right => 1, Keys.Left => -1, Keys.Down => 10, Keys.Up => -10, _ => 0 };
        if (offset != 0 && Parent is PalettePanel panel)
        {
            var index = panel.Swatches.IndexOf(this); panel.Swatches[Math.Clamp(index + offset, 0, panel.Swatches.Count - 1)].Focus();
            e.Handled = e.SuppressKeyPress = true;
        }
        base.OnKeyDown(e);
    }
}
