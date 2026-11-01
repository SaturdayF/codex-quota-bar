using System.Runtime.InteropServices;
namespace CodexQuotaBar;

internal static class LayoutTests
{
    [DllImport("user32.dll")] private static extern IntPtr SendMessage(IntPtr hwnd, uint message, IntPtr wparam, IntPtr lparam);
    internal static int Run()
    {
        Console.OutputEncoding = new System.Text.UTF8Encoding(false);
        using var menu = new ContextMenuStrip();
        using var context = new ApplicationContext();
        using var timer = new System.Windows.Forms.Timer { Interval = 100 };
        var forms = new List<(Screen Screen, Overlay Form)>();
        var index = 0; var phase = 0; var exit = 1; var started = Environment.TickCount64;
        void Clear() { foreach (var entry in forms) entry.Form.Dispose(); forms.Clear(); }
        timer.Tick += (_, _) =>
        {
            try
            {
                if (phase == 0)
                {
                    foreach (var screen in Screen.AllScreens)
                    {
                        var form = new Overlay(screen, menu, () => new Point(int.MinValue, int.MinValue), style: WidgetStyles.Choices[index].Id, widgetScale: index % 2 == 0 ? 1.4 : .6);
                        forms.Add((screen, form)); form.Show(); form.PositionAtTaskbar();
                    }
                    phase = 1; started = Environment.TickCount64; return;
                }
                if (Environment.TickCount64 - started < 350) return;
                foreach (var entry in forms)
                {
                    var bar = Overlay.TaskbarBounds(entry.Screen) ?? throw new Exception("Taskbar not found");
                    var delta = Math.Abs((entry.Form.Top + entry.Form.Bottom - bar.Top - bar.Bottom) / 2.0);
                    if (delta > .5) throw new Exception($"Center offset {delta} on {entry.Screen.DeviceName}");
                    if (entry.Form.Height > bar.Height) throw new Exception("Scaled widget exceeds taskbar height");
                    if (phase == 1)
                    {
                        var requests = 0;
                        entry.Form.SettingsRequested += () => requests++;
                        SendMessage(entry.Form.Handle, 0x7B, entry.Form.Handle, new IntPtr(-1));
                        if (requests != 1 || entry.Form.ContextMenuStrip is not null) throw new Exception("Right click did not route directly to settings");
                        entry.Form.UpdateState(new(new(null, new(27, null), "pro"), DateTimeOffset.UtcNow, null));
                    }
                }
                if (phase == 1) { phase = 2; started = Environment.TickCount64; return; }
                Console.WriteLine($"PASS {WidgetStyles.Choices[index].Name}: both layouts centered on {forms.Count} displays; right-click requests settings");
                Clear(); index++;
                if (index == WidgetStyles.Choices.Length) { exit = 0; context.ExitThread(); }
                else phase = 0;
            }
            catch (Exception e) { Console.WriteLine("FAIL " + e.Message); Clear(); context.ExitThread(); }
        };
        timer.Start(); Application.Run(context); Clear(); return exit;
    }
}
