using System.Runtime.InteropServices;
namespace CodexQuotaBar;

internal static class UiIntegrationTests
{
    [DllImport("user32.dll")] private static extern IntPtr SendMessage(IntPtr hwnd, uint msg, IntPtr wparam, IntPtr lparam);
    internal static int Run()
    {
        using var context = new QuotaContext();
        using var timer = new System.Windows.Forms.Timer { Interval = 100 };
        var started = Environment.TickCount64; var phase = 0; var exit = 1; var screenIndex = 0; var tested = 0;
        Application.ThreadException += (_, e) => { Console.WriteLine(e.Exception.ToString()); context.ExitThread(); };
        timer.Tick += (_, _) =>
        {
            if (Environment.TickCount64 - started > 10000)
            {
                Console.WriteLine("FAIL settings route timeout; open forms: " + string.Join(",", Application.OpenForms.Cast<Form>().Select(f => f.GetType().Name + ":" + f.Visible)));
                context.ExitThread(); return;
            }
            if (phase == 0)
            {
                var overlays = Application.OpenForms.OfType<Overlay>().Where(f => f.Visible).OrderBy(f => f.Left).ToArray();
                if (overlays.Length == 0) return;
                if (screenIndex >= overlays.Length) { exit = 0; Console.WriteLine($"PASS settings opened from actual right-button messages on {tested} displays"); context.ExitThread(); return; }
                var overlay = overlays[screenIndex];
                phase = 1;
                // Exercise WinForms' real button path, rather than only the downstream
                // menu message. No physical mouse movement or saved setting changes.
                var x = overlay.ClientSize.Width / 2; var y = overlay.ClientSize.Height / 2;
                var position = new IntPtr((y << 16) | x);
                SendMessage(overlay.Handle, 0x0204, new IntPtr(2), position); // WM_RBUTTONDOWN
                SendMessage(overlay.Handle, 0x0205, IntPtr.Zero, position); // WM_RBUTTONUP
                // It must already be open before any WM_CONTEXTMENU fallback is sent.
                if (!Application.OpenForms.OfType<SettingsForm>().Any(f => f.Visible))
                { Console.WriteLine("FAIL right-button release did not open settings"); context.ExitThread(); return; }
                SendMessage(overlay.Handle, 0x7B, overlay.Handle, new IntPtr(-1));
            }
            else
            {
                var settings = Application.OpenForms.OfType<SettingsForm>().FirstOrDefault(f => f.Visible);
                if (settings is null) return;
                if (Application.OpenForms.OfType<SettingsForm>().Count(f => f.Visible) != 1)
                { Console.WriteLine("FAIL duplicate settings window"); context.ExitThread(); return; }
                Console.WriteLine("PASS right-button release opens exactly one settings window on display " + screenIndex);
                settings.Close(); tested++; screenIndex++; phase = 0;
            }
        };
        timer.Start(); Application.Run(context); return exit;
    }
}
