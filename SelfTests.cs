using System.Text.Json;

namespace CodexQuotaBar;

internal static class SelfTests
{
    private static int count;
    private static void Check(bool value, string name)
    {
        if (!value) throw new Exception("FAIL: " + name);
        count++; Console.WriteLine("PASS: " + name);
    }
    private static Quotas Parse(string text) { using var doc = JsonDocument.Parse(text); return Quotas.Parse(doc.RootElement); }
    public static async Task<int> Run()
    {
        try
        {
            var swapped = Parse("""{"rateLimits":{"primary":{"windowDurationMins":10080,"usedPercent":57},"secondary":{"windowDurationMins":300,"usedPercent":28}}}""");
            Check(swapped.Hour?.Remaining == 72 && swapped.Week?.Remaining == 43, "window durations, not primary/secondary positions");
            Check(Parse("""{"rateLimits":{"primary":{"windowDurationMins":300,"usedPercent":101}}}""").Hour?.Remaining == 0, "over-limit clamping");
            Check(Parse("""{"rateLimits":{"primary":{"windowDurationMins":300,"usedPercent":-2}}}""").Hour?.Remaining == 100, "negative input clamping");
            Check(Parse("""{"rateLimits":{"primary":{"windowDurationMins":15,"usedPercent":25},"secondary":null}}""").Hour is null, "unknown window is unavailable");
            Check(Parse("""{"rateLimits":{"primary":{"windowDurationMins":300,"usedPercent":null},"secondary":{"windowDurationMins":null,"usedPercent":20}}}""").Hour is null, "null usage and duration stay unavailable");
            Check(Parse("""{"rateLimits":{"primary":{"windowDurationMins":300,"usedPercent":25,"resetsAt":null}}}""").Hour?.Remaining == 75, "nullable reset does not discard valid usage");
            Check(Parse("""{"rateLimits":{"primary":{"windowDurationMins":300,"usedPercent":25}},"rateLimitsByLimitId":{"other":{"primary":{"windowDurationMins":300,"usedPercent":0}}}}""").Hour is null, "never substitute other bucket");
            var general = Parse("""{"rateLimitsByLimitId":{"codex":{"primary":{"windowDurationMins":300,"usedPercent":28},"secondary":{"windowDurationMins":10080,"usedPercent":57}},"model":{"primary":{"windowDurationMins":300,"usedPercent":0}}}}""");
            Check(general.Hour?.Remaining == 72 && general.Week?.Remaining == 43, "select general Codex bucket");
            Check(Parse("""{"rateLimits":{"planType":"pro","primary":{"windowDurationMins":300,"usedPercent":10}}}""").WeekOnly, "explicit Pro plan selects weekly-only layout");
            Check(!general.WeekOnly && !new Quotas(null, null, "plus").WeekOnly, "unknown and Plus plans keep both windows");
            Check(new Quotas(null, null, "Pro").WeekOnly && !new Quotas(null, null, "professional").WeekOnly, "Pro classification is exact and case-insensitive");
            Check(System.Text.Json.JsonSerializer.Deserialize<Settings>("{\"ShowDialLetters\":false}")?.ShowDialLetters == false, "H/W setting round-trips disabled state");
            Check(Quotas.Label(new(0.8, null)) == "0%" && Quotas.Label(new(100, null)) == "100%" && Quotas.Label(null) == "--", "percent labels and conservative rounding");
            Check(!DesktopApps.IsDesktopBinary("codex", @"C:\Users\demo\AppData\Local\OpenAI\Codex\bin\version\codex.exe"), "own app-server cannot keep listener active");
            Check(DesktopApps.IsDesktopBinary("ChatGPT", null), "protected ChatGPT process remains detectable");
            Check(DesktopApps.IsDesktopBinary("Codex", @"C:\Program Files\WindowsApps\OpenAI.Codex\app\Codex.exe"), "desktop Codex process recognized");
            Check(new DisplayState(general, DateTimeOffset.UtcNow.AddMinutes(-2), null).Stale, "stale success has visible status");
            Check(new DisplayState(general, DateTimeOffset.UtcNow, "offline").Stale, "network failure preserves but marks numbers");
            using (var icon = Artwork.MakeIcon("100%", Artwork.HourColor, false)) Check(icon.Handle != IntPtr.Zero, "100% icon rendering and handle ownership");
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(15));
            using var server = await AppServer.Start(Environment.ProcessPath!, timeout.Token, fake: true);
            var changed = new TaskCompletionSource<Quotas>(TaskCreationOptions.RunContinuationsAsynchronously);
            server.Updated += q => changed.TrySetResult(q);
            var read = await server.Read(timeout.Token);
            Check(read.Hour?.Remaining == 72 && read.Week?.Remaining == 43, "real subprocess JSON-RPC initialization and read");
            Check(read.WeekOnly, "account/read supplies Pro plan when quota bucket omits it");
            var notification = await changed.Task.WaitAsync(TimeSpan.FromSeconds(5));
            Check(notification.Hour?.Remaining == 71, "streamed quota update notification");
            Check(notification.WeekOnly, "streamed quota updates preserve account plan");
            // The fake server returns a structured error on the next query.
            try { await server.Read(timeout.Token); Check(false, "RPC error propagation"); }
            catch (RpcFailure e) { Check(e.Code == -32603, "RPC error propagation"); }
            Console.WriteLine($"{count} checks passed."); return 0;
        }
        catch (Exception e) { Console.WriteLine(e.ToString()); return 1; }
    }
    public static int FakeServer()
    {
        var reads = 0; var initialized = false;
        while (Console.ReadLine() is { } line)
        {
            using var doc = JsonDocument.Parse(line);
            var message = doc.RootElement;
            var method = message.GetProperty("method").GetString();
            if (method == "initialized") { initialized = true; continue; }
            var id = message.GetProperty("id").GetInt32();
            if (method == "initialize") Console.WriteLine(JsonSerializer.Serialize(new { id, result = new { userAgent = "test" } }));
            else if (method == "account/read" && initialized) Console.WriteLine(JsonSerializer.Serialize(new { id, result = new { account = new { type = "chatgpt", planType = "pro" } } }));
            else if (method == "account/rateLimits/read" && initialized && ++reads == 1)
            {
                Console.WriteLine(JsonSerializer.Serialize(new { id, result = new { rateLimits = new { primary = new { windowDurationMins = 300, usedPercent = 28 }, secondary = new { windowDurationMins = 10080, usedPercent = 57 } } } }));
                Console.WriteLine("""{"method":"account/rateLimits/updated","params":{"rateLimits":{"limitId":"codex_other","primary":{"windowDurationMins":300,"usedPercent":99}}}}""");
                Console.WriteLine("""{"method":"account/rateLimits/updated","params":{"rateLimits":{"limitId":"codex","primary":{"windowDurationMins":300,"usedPercent":29},"secondary":{"windowDurationMins":10080,"usedPercent":57}}}}""");
            }
            else Console.WriteLine(JsonSerializer.Serialize(new { id, error = new { code = -32603, message = "simulated network failure" } }));
            Console.Out.Flush();
        }
        return 0;
    }
}
