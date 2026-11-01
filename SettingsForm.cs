using System.Runtime.InteropServices;
namespace CodexQuotaBar;

internal sealed class SettingsForm : Form
{
    [DllImport("user32.dll")] private static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr window);
    private readonly System.Windows.Forms.Timer previewTimer = new() { Interval = 16 };
    private readonly Color surface, sidebar, ink, muted, line;
    internal void Reveal(Screen screen)
    {
        StartPosition = FormStartPosition.Manual; ShowInTaskbar = true;
        WindowState = FormWindowState.Normal; TopMost = true;
        if (!Visible) Show();
        var area = screen.WorkingArea;
        Location = new(area.Left + Math.Max(0, (area.Width - Width) / 2), area.Top + Math.Max(0, (area.Height - Height) / 2));
        ShowWindow(Handle, 9); BringToFront(); Activate();
        if (!IsWindowVisible(Handle)) throw new InvalidOperationException("设置窗口未能显示");
    }
    public SettingsForm(Action? refresh = null, Action? exit = null, Settings? initialSettings = null)
    {
        var light = Artwork.LightTheme;
        surface = light ? Color.White : Color.FromArgb(24, 24, 24);
        sidebar = light ? Color.FromArgb(247, 247, 247) : Color.FromArgb(32, 32, 32);
        ink = light ? Color.FromArgb(13, 13, 13) : Color.FromArgb(244, 244, 244);
        muted = light ? Color.FromArgb(105, 105, 105) : Color.FromArgb(170, 170, 170);
        line = light ? Color.FromArgb(231, 231, 231) : Color.FromArgb(56, 56, 56);
        Font = new Font("Microsoft YaHei UI", 9F);
        AutoScaleMode = AutoScaleMode.Dpi;
        AutoScaleDimensions = new SizeF(96, 96);
        Text = "Codex Quota Bar 设置"; StartPosition = FormStartPosition.CenterScreen;
        ClientSize = new(900, 650); MinimumSize = new(800, 620);
        BackColor = surface; ForeColor = ink;
        var settings = initialSettings ?? Settings.Load();
        var shell = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 1, Margin = Padding.Empty };
        shell.ColumnStyles.Add(new(SizeType.Absolute, 190)); shell.ColumnStyles.Add(new(SizeType.Percent, 100));
        shell.RowStyles.Add(new(SizeType.Percent, 100));
        var nav = new Panel { Dock = DockStyle.Fill, BackColor = sidebar, Padding = new(16, 25, 16, 20), Margin = Padding.Empty };
        var navItems = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, FlowDirection = FlowDirection.TopDown, WrapContents = false };
        navItems.Controls.Add(Label("设置", 20, true, new(9, 0, 0, 25)));
        var navFooter = Label("Codex Quota Bar\n0.1.0", 9, false, Padding.Empty); navFooter.Dock = DockStyle.Bottom; navFooter.ForeColor = muted; navFooter.Height = 48;
        nav.Controls.Add(navItems); nav.Controls.Add(navFooter);
        var right = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 2, Margin = Padding.Empty, Padding = new(34, 26, 34, 18) };
        right.RowStyles.Add(new(SizeType.Percent, 100)); right.RowStyles.Add(new(SizeType.Absolute, 58));
        var host = new Panel { Dock = DockStyle.Fill, Margin = Padding.Empty };
        var pages = new List<Control>(); var navigation = new List<Button>();
        FlowLayoutPanel Page(string name, string description)
        {
            var page = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown, WrapContents = false, AutoScroll = true, Visible = false, Margin = Padding.Empty };
            var heading = Label(name, 21, true, new(0, 0, 0, 8)); page.Controls.Add(heading);
            page.Controls.Add(Label(description, 9, false, new(0, 0, 0, 24), muted));
            var index = pages.Count; pages.Add(page); host.Controls.Add(page);
            var button = Button(name); button.Width = 158; button.Height = 38; button.TextAlign = ContentAlignment.MiddleLeft; button.Padding = new(12, 0, 0, 0); button.Margin = new(0, 0, 0, 5);
            button.Click += (_, _) => SelectPage(index);
            navigation.Add(button); navItems.Controls.Add(button);
            return page;
        }
        void SelectPage(int index)
        {
            for (var i = 0; i < pages.Count; i++)
            {
                pages[i].Visible = i == index;
                navigation[i].BackColor = i == index ? (light ? Color.FromArgb(230, 230, 230) : Color.FromArgb(54, 54, 54)) : sidebar;
                navigation[i].AccessibleDescription = i == index ? "当前页面" : "";
            }
            pages[index].BringToFront();
        }
        var appearance = Page("外观", "调整任务栏组件的显示方式。所有选中的屏幕会使用相同设置。");
        var general = Page("常规", "管理显示屏幕，以及应用的启动与退出。");
        var advanced = Page("高级", "配置额度读取来源，或手动刷新额度。");
        var styles = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 200, AccessibleName = "任务栏样式", FlatStyle = FlatStyle.Flat, BackColor = sidebar, ForeColor = ink };
        foreach (var choice in WidgetStyles.Choices) styles.Items.Add(choice.Name);
        styles.SelectedIndex = Array.FindIndex(WidgetStyles.Choices, c => c.Id == WidgetStyles.Normalize(settings.WidgetStyle));
        Row(appearance, "任务栏样式", "选择额度的呈现方式。", styles);
        var letters = new SettingsToggle { Checked = settings.ShowDialLetters, AccessibleName = "仪表中间显示 H 和 W" };
        var preview = new SettingsPreviewPanel { Height = 76, BackColor = sidebar, Margin = new(0, 12, 0, 8) };
        long previewStarted = -1;
        preview.Paint += (_, e) =>
        {
            e.Graphics.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
            var selected = WidgetStyles.Choices[styles.SelectedIndex].Id;
            var size = WidgetStyles.SizeFor(selected, false, preview.DeviceDpi);
            var progress = previewStarted < 0 ? 1 : Math.Clamp((Environment.TickCount64 - previewStarted) / (double)QuotaAnimation.Duration, 0, 1);
            var fill = 100 * (1 - Math.Pow(1 - progress, 3));
            WidgetStyles.Draw(e.Graphics, new RectangleF((preview.Width - size.Width) / 2f, (preview.Height - size.Height) / 2f, size.Width, size.Height), new(new(100, null), new(100, null)), selected, letters.Checked, false, light, fill, fill);
        };
        appearance.Controls.Add(preview);
        appearance.Controls.Add(Label("预览使用模拟数据", 8, false, new(0, 0, 0, 18), muted));
        Row(appearance, "仪表中间显示 H 和 W", "H 表示 5 小时额度，W 表示周额度。", letters);
        var animate = new SettingsToggle { Checked = settings.AnimateQuotaReset, AccessibleName = "启动与额度重置动画" };
        Row(appearance, "启动与额度重置动画", "首次读取或额度重置后，圆环平滑填充至剩余额度。", animate);
        var demo = Button("预览重置动画"); demo.AutoSize = true; demo.Margin = new(0, 14, 0, 0);
        demo.Click += (_, _) => { previewStarted = Environment.TickCount64; previewTimer.Start(); preview.Invalidate(); };
        previewTimer.Tick += (_, _) => { preview.Invalidate(); if (Environment.TickCount64 - previewStarted >= QuotaAnimation.Duration) { previewTimer.Stop(); previewStarted = -1; } };
        appearance.Controls.Add(demo);
        void StyleChanged()
        {
            var gauges = WidgetStyles.Choices[styles.SelectedIndex].Id == "gauges";
            letters.Enabled = gauges; animate.Enabled = gauges; demo.Enabled = gauges;
            preview.Invalidate();
        }
        styles.SelectedIndexChanged += (_, _) => StyleChanged();
        letters.CheckedChanged += (_, _) => preview.Invalidate();
        StyleChanged();
        var display = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 200, AccessibleName = "显示屏幕", FlatStyle = FlatStyle.Flat, BackColor = sidebar, ForeColor = ink };
        var modes = new List<(string Label, string Mode, string? Id)> { ("所有屏幕", "all", null), ("仅主屏幕", "primary", null) };
        modes.AddRange(Screen.AllScreens.Select(s => ($"{s.DeviceName}{(s.Primary ? "（主屏）" : "")}", "selected", (string?)s.DeviceName)));
        foreach (var item in modes) display.Items.Add(item.Label);
        display.SelectedIndex = Math.Max(0, modes.FindIndex(m => m.Mode == settings.DisplayMode && (m.Id is null || settings.Displays.Contains(m.Id))));
        Row(general, "显示屏幕", "选择任务栏组件出现的位置。", display);
        var startup = new SettingsToggle { Checked = Startup.Enabled, AccessibleName = "随 Windows 登录启动" };
        Row(general, "随 Windows 登录启动", "启动轻量监听器，自动跟随 ChatGPT 和 Codex。", startup);
        Row(general, "跟随应用运行", "任一应用运行时显示；两者完全退出后停止读取。", Label("自动", 9, false, Padding.Empty, muted));
        Row(general, "Pro 账户", "识别为 Pro 时，仅显示周额度。", Label("自动", 9, false, Padding.Empty, muted));
        general.Controls.Add(Label("悬浮查看详情 · 右键打开设置 · 左键横向拖动", 9, false, new(0, 24, 0, 0), muted));
        var path = new TextBox { Text = settings.CodexPath ?? "", Dock = DockStyle.Fill, PlaceholderText = "自动查找 Codex CLI", BackColor = sidebar, ForeColor = ink, BorderStyle = BorderStyle.FixedSingle, AccessibleName = "Codex CLI 路径" };
        Row(advanced, "Codex CLI 路径", "留空时自动查找已安装的 Codex。", null);
        var pathRow = new TableLayoutPanel { Height = 40, ColumnCount = 2, Margin = new(0, 12, 0, 20) };
        pathRow.ColumnStyles.Add(new(SizeType.Percent, 100)); pathRow.ColumnStyles.Add(new(SizeType.Absolute, 85));
        var browse = Button("浏览…"); browse.Dock = DockStyle.Fill;
        browse.Click += (_, _) => { using var dialog = new OpenFileDialog { Filter = "Codex CLI|codex.exe|程序|*.exe" }; if (dialog.ShowDialog(this) == DialogResult.OK) path.Text = dialog.FileName; };
        pathRow.Controls.Add(path, 0, 0); pathRow.Controls.Add(browse, 1, 0); advanced.Controls.Add(pathRow);
        var refreshButton = Button("立即刷新"); refreshButton.Enabled = refresh is not null;
        refreshButton.Click += (_, _) => refresh?.Invoke();
        Row(advanced, "额度读取", "每 30 秒更新；读取失败时自动重试。", refreshButton);
        var exitButton = Button("退出额度工具"); exitButton.Enabled = exit is not null;
        exitButton.Click += (_, _) => exit?.Invoke();
        Row(advanced, "退出", "停止显示与后台监听，直到下次启动。", exitButton);
        var actions = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.RightToLeft, WrapContents = false, Padding = new(0, 12, 0, 0), Margin = Padding.Empty };
        var save = Button("保存更改"); save.BackColor = ink; save.ForeColor = surface;
        save.FlatAppearance.MouseOverBackColor = muted;
        var cancel = Button("取消"); cancel.Click += (_, _) => Close();
        save.Click += (_, _) =>
        {
            try
            {
                var chosen = modes[display.SelectedIndex];
                settings.CodexPath = string.IsNullOrWhiteSpace(path.Text) ? null : path.Text.Trim();
                settings.ShowDialLetters = letters.Checked; settings.AnimateQuotaReset = animate.Checked;
                settings.WidgetStyle = WidgetStyles.Choices[styles.SelectedIndex].Id;
                settings.DisplayMode = chosen.Mode; settings.Displays = chosen.Id is null ? [] : [chosen.Id];
                settings.Save(); Startup.Set(startup.Checked); DialogResult = DialogResult.OK; Close();
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException or System.Security.SecurityException)
            { MessageBox.Show(this, "无法保存设置或启动项，请检查用户权限。", Text); }
        };
        actions.Controls.Add(save); actions.Controls.Add(cancel);
        right.Controls.Add(host, 0, 0); right.Controls.Add(actions, 0, 1);
        shell.Controls.Add(nav, 0, 0); shell.Controls.Add(right, 1, 0); Controls.Add(shell);
        AcceptButton = save; CancelButton = cancel;
        void ResizeContent()
        {
            foreach (FlowLayoutPanel page in pages)
                foreach (Control child in page.Controls)
                    if (child is Panel)
                    {
                        var width = Math.Max(340, host.ClientSize.Width - SystemInformation.VerticalScrollBarWidth - 2);
                        if (child is TableLayoutPanel)
                        {
                            child.MinimumSize = new(width, child.MinimumSize.Height);
                            child.MaximumSize = new(width, 0);
                        }
                        child.Width = width;
                    }
        }
        host.SizeChanged += (_, _) => ResizeContent(); Shown += (_, _) => ResizeContent();
        SelectPage(0);
    }
    private Label Label(string text, float size, bool bold, Padding margin, Color? color = null) => new()
    {
        Text = text, AutoSize = true, Font = new Font(Font.FontFamily, size, bold ? FontStyle.Bold : FontStyle.Regular),
        ForeColor = color ?? ink, Margin = margin, MaximumSize = new(550, 0)
    };
    private Button Button(string text) => new()
    {
        Text = text, FlatStyle = FlatStyle.Flat, FlatAppearance = { BorderSize = 0, MouseOverBackColor = sidebar },
        BackColor = sidebar, ForeColor = ink, Height = 34, Width = 112, Cursor = Cursors.Hand, UseVisualStyleBackColor = false
    };
    private void Row(FlowLayoutPanel page, string title, string description, Control? value)
    {
        var row = new TableLayoutPanel { AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, MinimumSize = new(0, 88), ColumnCount = 2, RowCount = 1, Margin = Padding.Empty, Padding = new(0, 12, 0, 12) };
        row.RowStyles.Add(new(SizeType.AutoSize));
        row.ColumnStyles.Add(new(SizeType.Percent, 100)); row.ColumnStyles.Add(new(SizeType.AutoSize));
        var copy = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, FlowDirection = FlowDirection.TopDown, WrapContents = false, Margin = Padding.Empty };
        copy.Controls.Add(Label(title, 10, false, new(0, 0, 0, 5)));
        var detail = Label(description, 8.5f, false, Padding.Empty, muted); copy.Controls.Add(detail);
        copy.SizeChanged += (_, _) => detail.MaximumSize = new(Math.Max(100, copy.Width - 8), 0);
        row.Controls.Add(copy, 0, 0);
        if (value is not null) { value.Anchor = AnchorStyles.Right; value.Margin = new(18, 0, 0, 0); row.Controls.Add(value, 1, 0); }
        row.Paint += (_, e) => { using var pen = new Pen(line); e.Graphics.DrawLine(pen, 0, row.Height - 1, row.Width, row.Height - 1); };
        page.Controls.Add(row);
    }
    protected override void Dispose(bool disposing) { if (disposing) previewTimer.Dispose(); base.Dispose(disposing); }
}

internal sealed class SettingsPreviewPanel : Panel
{
    public SettingsPreviewPanel() { DoubleBuffered = true; ResizeRedraw = true; }
}

internal sealed class SettingsToggle : CheckBox
{
    public SettingsToggle()
    {
        AutoSize = false; Size = new(42, 26); Cursor = Cursors.Hand;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
        CheckedChanged += (_, _) => Invalidate();
    }
    protected override void OnPaint(PaintEventArgs e)
    {
        e.Graphics.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
        var light = Artwork.LightTheme; var scale = DeviceDpi / 96f;
        var rect = new RectangleF(1, (Height - 22 * scale) / 2, Width - 2, 22 * scale);
        using var path = HoverDetails.Round(rect, 11 * scale);
        using var track = new SolidBrush(!Enabled ? Color.Gray : Checked ? (light ? Color.FromArgb(32, 32, 32) : Color.FromArgb(225, 225, 225)) : (light ? Color.FromArgb(201, 201, 201) : Color.FromArgb(86, 86, 86)));
        e.Graphics.FillPath(track, path);
        using var knob = new SolidBrush(Checked && !light ? Color.FromArgb(32, 32, 32) : Color.White);
        var diameter = 16 * scale;
        e.Graphics.FillEllipse(knob, Checked ? rect.Right - diameter - 3 * scale : rect.Left + 3 * scale, rect.Top + 3 * scale, diameter, diameter);
        if (Focused && ShowFocusCues) ControlPaint.DrawFocusRectangle(e.Graphics, ClientRectangle);
    }
}
