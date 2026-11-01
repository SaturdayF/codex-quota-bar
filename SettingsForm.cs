namespace CodexQuotaBar;

internal sealed class SettingsForm : Form
{
    public SettingsForm()
    {
        Text = "Codex Quota Bar 设置"; StartPosition = FormStartPosition.CenterScreen;
        ClientSize = new(530, 310); MinimumSize = new(500, 340); AutoScaleMode = AutoScaleMode.Dpi;
        var settings = Settings.Load();
        var grid = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(20), ColumnCount = 2, RowCount = 7 };
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100)); grid.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        var description = new Label { Text = "底线数字 + % · 每 30 秒刷新\nChatGPT 或 Codex 运行时显示，两者退出后停止读取。", AutoSize = true, Dock = DockStyle.Fill };
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
        var help = new Label { Text = "主屏使用系统托盘；副屏使用自绘双数字，可横向拖动位置。\n系统托盘若收起，可在 Windows 任务栏设置中设为始终显示。", AutoSize = true, Margin = new(0, 14, 0, 8) };
        grid.Controls.Add(help, 0, 5); grid.SetColumnSpan(help, 2);
        var save = new Button { Text = "保存", AutoSize = true, Anchor = AnchorStyles.Right };
        save.Click += (_, _) =>
        {
            try
            {
                var chosen = modes[display.SelectedIndex];
                settings.CodexPath = string.IsNullOrWhiteSpace(path.Text) ? null : path.Text.Trim();
                settings.DisplayMode = chosen.Mode; settings.Displays = chosen.Id is null ? [] : [chosen.Id];
                settings.Save(); Startup.Set(startup.Checked); DialogResult = DialogResult.OK; Close();
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException or System.Security.SecurityException)
            { MessageBox.Show("无法保存设置或启动项，请检查用户权限。", Text); }
        };
        grid.Controls.Add(save, 1, 6); AcceptButton = save; Controls.Add(grid);
    }
}
