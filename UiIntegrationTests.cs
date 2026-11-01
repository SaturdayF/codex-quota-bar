using System.Runtime.InteropServices;
namespace CodexQuotaBar;

internal static class UiIntegrationTests
{
    [DllImport("user32.dll")] private static extern IntPtr SendMessage(IntPtr hwnd, uint msg, IntPtr wparam, IntPtr lparam);
    internal static int Run()
    {
        using var context = new QuotaContext();
        using var timer = new System.Windows.Forms.Timer { Interval = 100 };
        var started = Environment.TickCount64; var phase = 0; var exit = 1;
        Application.ThreadException += (_, e) => { Console.WriteLine(e.Exception.ToString()); context.ExitThread(); };
        timer.Tick += (_, _) =>
        {
            if (Environment.TickCount64 - started > 6000)
            {
                Console.WriteLine("FAIL settings route timeout; open forms: " + string.Join(",", Application.OpenForms.Cast<Form>().Select(f => f.GetType().Name + ":" + f.Visible)));
                context.ExitThread(); return;
            }
            if (phase == 0)
            {
                var overlay = Application.OpenForms.OfType<Overlay>().FirstOrDefault(f => f.Visible);
                if (overlay is null) return;
                phase = 1;
                SendMessage(overlay.Handle, 0x7B, overlay.Handle, new IntPtr(-1));
            }
            else
            {
                var settings = Application.OpenForms.OfType<SettingsForm>().FirstOrDefault(f => f.Visible);
                if (settings is null) return;
                Console.WriteLine("PASS actual application controller opens settings on widget right-click");
                settings.Close(); exit = 0; context.ExitThread();
            }
        };
        timer.Start(); Application.Run(context); return exit;
    }
}
