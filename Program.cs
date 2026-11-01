using System.Text.Json;

namespace CodexQuotaBar;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        if (args.Contains("--fake-server")) return SelfTests.FakeServer();
        ApplicationConfiguration.Initialize();
        if (args.Contains("--hover-test")) return HoverTests.Run();
        if (args.Contains("--self-test")) return SelfTests.Run().GetAwaiter().GetResult();
        if (args.Contains("--probe"))
        {
            try
            {
                using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(30));
                using var server = AppServer.Start(CodexLocator.Find(Settings.Load().CodexPath), timeout.Token).GetAwaiter().GetResult();
                var q = server.Read(timeout.Token).GetAwaiter().GetResult();
                Console.WriteLine(JsonSerializer.Serialize(new { hour = q.Hour, week = q.Week, desktopRunning = DesktopApps.AnyRunning(), displays = Screen.AllScreens.Select(s => new { s.DeviceName, s.Primary, s.Bounds }) }));
                return 0;
            }
            catch (Exception e) { Console.WriteLine(JsonSerializer.Serialize(new { error = e.GetType().Name, message = "无法读取额度；请检查网络、Codex 路径和登录状态。" })); return 1; }
        }
        if (args.Contains("--settings")) { Application.Run(new SettingsForm()); return 0; }
        using var singleton = new Mutex(true, @"Local\CodexQuotaBar.Listener", out var first);
        if (!first) return 0;
        try
        {
            using var context = new QuotaContext();
            using var smokeTimer = new System.Windows.Forms.Timer { Interval = 7000 };
            if (args.Contains("--smoke-ui")) { smokeTimer.Tick += (_, _) => context.ExitThread(); smokeTimer.Start(); }
            Application.Run(context); return 0;
        }
        finally { singleton.ReleaseMutex(); }
    }
}
