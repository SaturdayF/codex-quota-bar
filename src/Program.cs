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
        if (args.Contains("--layout-test")) return LayoutTests.Run();
        if (args.Contains("--settings-test")) return UiIntegrationTests.Run();
        if (args.Length == 2 && args[0] == "--render-ui") { DesignPreview.Render(args[1]); return 0; }
        if (args.Length == 2 && args[0] == "--render-motion") { DesignPreview.RenderMotion(args[1]); return 0; }
        if (args.Contains("--self-test")) return SelfTests.Run().GetAwaiter().GetResult();
        if (args.Contains("--probe"))
        {
            try
            {
                using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(30));
                using var server = AppServer.Start(CodexLocator.Find(Settings.Load().CodexPath), timeout.Token).GetAwaiter().GetResult();
                var q = server.Read(timeout.Token).GetAwaiter().GetResult();
                Console.WriteLine(JsonSerializer.Serialize(new { hour = q.Hour, week = q.Week, planType = q.PlanType, weekOnly = q.WeekOnly, desktopRunning = DesktopApps.AnyRunning(), displays = Screen.AllScreens.Select(s => new { s.DeviceName, s.Primary, s.Bounds }) }));
                return 0;
            }
            catch (Exception e) { Console.WriteLine(JsonSerializer.Serialize(new { error = e.GetType().Name, message = "无法读取额度；请检查网络、Codex 路径和登录状态。" })); return 1; }
        }
        if (args.Contains("--settings")) { Application.Run(new SettingsForm()); return 0; }
        if (args.Length > 0 && !args.Contains("--smoke-ui")) { Console.WriteLine("Unknown command-line option."); return 2; }
        using var singleton = new Mutex(true, @"Local\CodexQuotaBar.Listener", out var first);
        if (!first) return 0;
        try
        {
            using var quitSignal = new EventWaitHandle(false, EventResetMode.ManualReset, AppLifetime.QuitSignalName);
            quitSignal.Reset();
            using var context = new QuotaContext(() => quitSignal.WaitOne(0));
            using var smokeTimer = new System.Windows.Forms.Timer { Interval = 7000 };
            if (args.Contains("--smoke-ui")) { smokeTimer.Tick += (_, _) => context.ExitThread(); smokeTimer.Start(); }
            Application.Run(context); return 0;
        }
        finally { singleton.ReleaseMutex(); }
    }
}
