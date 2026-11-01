using System.Runtime.InteropServices;
namespace CodexQuotaBar;

internal static class UiIntegrationTests
{
    [DllImport("user32.dll")] private static extern IntPtr SendMessage(IntPtr hwnd, uint msg, IntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll")] private static extern bool ShowWindow(IntPtr hwnd, int command);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] private static extern bool IsIconic(IntPtr hwnd);
    internal static int Run()
    {
        using var context = new QuotaContext();
        using var timer = new System.Windows.Forms.Timer { Interval = 100 };
        var started = Environment.TickCount64; var phaseStarted = started; var phase = 0; var exit = 1; var screenIndex = 0; var tested = 0;
        void RightClick(Overlay overlay)
        {
            var position = new IntPtr((overlay.ClientSize.Height / 2 << 16) | (overlay.ClientSize.Width / 2));
            SendMessage(overlay.Handle, 0x0204, new IntPtr(2), position);
            SendMessage(overlay.Handle, 0x0205, IntPtr.Zero, position);
        }
        Application.ThreadException += (_, e) => { Console.WriteLine(e.Exception.ToString()); context.ExitThread(); };
        timer.Tick += (_, _) =>
        {
            if (Environment.TickCount64 - started > 15000)
            {
                Console.WriteLine("FAIL settings route timeout; open forms: " + string.Join(",", Application.OpenForms.Cast<Form>().Select(f => f.GetType().Name + ":" + f.Visible)));
                context.ExitThread(); return;
            }
            if (phase == 0)
            {
                var overlays = Application.OpenForms.OfType<Overlay>().Where(f => f.Visible).OrderBy(f => f.Left).ToArray();
                if (overlays.Length == 0) return;
                if (screenIndex >= overlays.Length) { exit = 0; Console.WriteLine($"PASS settings opened from actual right-button messages on {tested} displays"); context.ExitThread(); return; }
                RightClick(overlays[screenIndex]); phase = 1;
            }
            else if (phase == 2 || phase == 4)
            {
                if (Environment.TickCount64 - phaseStarted < 300) return;
                var overlay = Application.OpenForms.OfType<Overlay>().Where(f => f.Visible).OrderBy(f => f.Left).ElementAt(screenIndex);
                RightClick(overlay); phase++;
            }
            else
            {
                var settings = Application.OpenForms.OfType<SettingsForm>().FirstOrDefault(f => f.Visible && IsWindowVisible(f.Handle) && !IsIconic(f.Handle));
                if (settings is null) return;
                if (Application.OpenForms.OfType<SettingsForm>().Count(f => f.Visible) != 1)
                { Console.WriteLine("FAIL duplicate settings window"); context.ExitThread(); return; }
                if (Application.OpenForms.OfType<Overlay>().Any(f => f.InteractionSuspended))
                { Console.WriteLine("FAIL opening settings disabled hover"); context.ExitThread(); return; }
                if (phase == 1)
                {
                    Console.WriteLine("PASS right-button release opens native-visible settings on display " + screenIndex);
                    // Reproduce the exact production state: HWND hidden even though the
                    // Form instance remains alive. Right-click must Show, not just Activate.
                    ShowWindow(settings.Handle, 0); phase = 2; phaseStarted = Environment.TickCount64; return;
                }
                if (phase == 3)
                {
                    Console.WriteLine("PASS right-click restores hidden settings and leaves hover enabled");
                    settings.WindowState = FormWindowState.Minimized; phase = 4; phaseStarted = Environment.TickCount64; return;
                }
                Console.WriteLine("PASS right-click restores minimized settings without duplicate windows");
                settings.Close(); tested++; screenIndex++; phase = 0;
            }
        };
        timer.Start(); Application.Run(context); return exit;
    }
}
