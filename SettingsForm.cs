namespace CodexQuotaBar;

internal sealed class SettingsForm : Form
{
    public SettingsForm(Action? refresh = null, Action? exit = null, Settings? initialSettings = null)
    {
        Text = "Codex Quota Bar 设置"; StartPosition = FormStartPosition.CenterScreen;
        ClientSize = new(580, 500); MinimumSize = new(580, 535); AutoScaleMode = AutoScaleMode.Dpi;
        var settings = initialSettings ?? Settings.Load();
        var grid = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(20), ColumnCount = 2, RowCount = 10, AutoScroll = true };
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100)); grid.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        var description = new Label { Text = "额度显示设置 · 每 30 秒刷新 · Pro 自动仅显示周额度\nChatGPT 或 Codex 运行时显示，两者退出后停止读取。", AutoSize = true, Dock = DockStyle.Fill };
        grid.Controls.Add(description, 0, 0); grid.SetColumnSpan(description, 2);
        var label = new Label { Text = "Codex CLI 路径（留空则自动查找）", AutoSize = true, Margin = new(0, 18, 0, 6) };
        grid.Controls.Add(label, 0, 1); grid.SetColumnSpan(label, 2);
        var path = new TextBox { Text = settings.CodexPath ?? "", Dock = DockStyle.Fill };
        var browse = new Button { Text = "浏览…", AutoSize = true };
        browse.Click += (_, _) => { using var dialog = new OpenFileDialog { Filter = "Codex CLI|codex.exe|程序|*.exe" }; if (dialog.ShowDialog() == DialogResult.OK) path.Text = dialog.FileName; };
        grid.Controls.Add(path, 0, 2); grid.Controls.Add(browse, 1, 2);
        var startup = new CheckBox { Text = "随 Windows 登录启动轻量监听器", Checked = Startup.Enabled, AutoSize = true, Margin = new(0, 14, 0, 8) };
        grid.Controls.Add(startup, 0, 3); grid.SetColumnSpan(startup, 2);
        var display = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Dock = DockStyle.Fill };
        var modes = new List<(string Label, string Mode, string? Id)> { ("两个/所有屏幕", "all", null), ("仅主屏幕", "primary", null) };
        modes.AddRange(Screen.AllScreens.Select(s => ($"仅 {s.DeviceName}{(s.Primary ? "（主屏）" : "")}", "selected", (string?)s.DeviceName)));
        foreach (var item in modes) display.Items.Add(item.Label);
        display.SelectedIndex = Math.Max(0, modes.FindIndex(m => m.Mode == settings.DisplayMode && (m.Id is null || settings.Displays.Contains(m.Id))));
        grid.Controls.Add(display, 0, 4); grid.SetColumnSpan(display, 2);
        var letters = new CheckBox { Text = "仪表中间加入字母 H 和 W（5 小时额度 / 周额度）", Checked = settings.ShowDialLetters, AutoSize = true, Margin = new(0, 12, 0, 4) };
        grid.Controls.Add(letters, 0, 5); grid.SetColumnSpan(letters, 2);
        var styles = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Dock = DockStyle.Fill, AccessibleName = "任务栏样式" };
        foreach (var choice in WidgetStyles.Choices) styles.Items.Add(choice.Name);
        styles.SelectedIndex = Array.FindIndex(WidgetStyles.Choices, c => c.Id == WidgetStyles.Normalize(settings.WidgetStyle));
        var styleLabel = new Label { Text = "任务栏样式", AutoSize = true, Anchor = AnchorStyles.Left };
        grid.Controls.Add(styles, 0, 6); grid.Controls.Add(styleLabel, 1, 6);
        var preview = new Panel { Height = 52, Dock = DockStyle.Fill, BackColor = SystemColors.ControlLightLight, Margin = new(0, 10, 0, 6) };
        preview.Paint += (_, e) =>
        {
            var selected = WidgetStyles.Choices[styles.SelectedIndex].Id;
            var size = WidgetStyles.SizeFor(selected, false, preview.DeviceDpi);
            WidgetStyles.Draw(e.Graphics, new RectangleF(8, (preview.ClientSize.Height - size.Height) / 2f, size.Width, size.Height), new(new(68, null), new(27, null)), selected, letters.Checked, false, true);
        };
        styles.SelectedIndexChanged += (_, _) => { letters.Enabled = WidgetStyles.Choices[styles.SelectedIndex].Id == "gauges"; preview.Invalidate(); };
        letters.Enabled = WidgetStyles.Choices[styles.SelectedIndex].Id == "gauges";
        letters.CheckedChanged += (_, _) => preview.Invalidate();
        grid.Controls.Add(preview, 0, 7); grid.SetColumnSpan(preview, 2);
        var help = new Label { Text = "预览为模拟数据。保存后应用到所有选中屏幕。\n左键横向拖动，右键直接打开设置；悬浮查看详情。", AutoSize = true, Margin = new(0, 8, 0, 8) };
        grid.Controls.Add(help, 0, 8); grid.SetColumnSpan(help, 2);
        var save = new Button { Text = "保存", AutoSize = true, Anchor = AnchorStyles.Right };
        save.Click += (_, _) =>
        {
            try
            {
                var chosen = modes[display.SelectedIndex];
                settings.CodexPath = string.IsNullOrWhiteSpace(path.Text) ? null : path.Text.Trim();
                settings.ShowDialLetters = letters.Checked;
                settings.WidgetStyle = WidgetStyles.Choices[styles.SelectedIndex].Id;
                settings.DisplayMode = chosen.Mode; settings.Displays = chosen.Id is null ? [] : [chosen.Id];
                settings.Save(); Startup.Set(startup.Checked); DialogResult = DialogResult.OK; Close();
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException or System.Security.SecurityException)
            { MessageBox.Show("无法保存设置或启动项，请检查用户权限。", Text); }
        };
        var actions = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill, FlowDirection = FlowDirection.RightToLeft };
        var refreshButton = new Button { Text = "立即刷新", AutoSize = true, Enabled = refresh is not null };
        refreshButton.Click += (_, _) => refresh?.Invoke();
        var exitButton = new Button { Text = "退出额度工具", AutoSize = true, Enabled = exit is not null };
        exitButton.Click += (_, _) => exit?.Invoke();
        actions.Controls.Add(save); actions.Controls.Add(refreshButton); actions.Controls.Add(exitButton);
        grid.Controls.Add(actions, 0, 9); grid.SetColumnSpan(actions, 2); AcceptButton = save; Controls.Add(grid);
    }
}
