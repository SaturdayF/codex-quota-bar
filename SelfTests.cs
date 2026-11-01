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
            var previousContext = SynchronizationContext.Current;
            try { StyleTests.Run(Check); }
            finally { SynchronizationContext.SetSynchronizationContext(previousContext); }
            var now = DateTimeOffset.UtcNow;
            var animation = new QuotaAnimation();
            DisplayState Sample(double h, double w, DateTimeOffset reset) => new(new(new(h, reset), new(w, reset), "plus"), now, null);
            var old = Sample(4, 12, now.AddSeconds(-1));
            animation.Update(old, 0, now, true);
            Check(animation.Running(0) && animation.Fill(true, old.Quotas.Hour, 0) == 0, "first successful read animates from an empty ring");
            var renewed = Sample(100, 97, now.AddHours(5));
            animation.Update(renewed, 100, now, true);
            Check(animation.Fill(true, renewed.Quotas.Hour, 100) == 0 && animation.Fill(false, renewed.Quotas.Week, 100) == 0, "both renewed windows start empty");
            var middle = animation.Fill(true, renewed.Quotas.Hour, 700);
            Check(middle is > 0 and < 100 && renewed.Quotas.Hour?.Remaining == 100, "ring interpolates without changing true percentage");
            animation.Update(renewed, 700, now, true);
            Check(animation.Fill(true, renewed.Quotas.Hour, 700) == middle, "repeated polling does not restart animation");
            Check(!animation.Running(1300) && animation.Fill(false, renewed.Quotas.Week, 1300) is null, "animation completes and returns to actual remaining value");
            var single = new QuotaAnimation(); single.Update(old, 0, now, false);
            var hourOnly = renewed with { Quotas = renewed.Quotas with { Week = old.Quotas.Week } };
            single.Update(hourOnly, 100, now, true);
            Check(single.Fill(true, hourOnly.Quotas.Hour, 100) == 0 && single.Fill(false, hourOnly.Quotas.Week, 100) is null, "only renewed window animates");
            single.Update(hourOnly, 120, now, false);
            Check(!single.Running(120), "animation preference disables running animation");
            var future = new QuotaAnimation(); future.Update(Sample(4, 12, now.AddMinutes(2)), 0, now, false); future.Update(renewed, 100, now, true);
            Check(!future.Running(100), "percentage correction before reset does not animate");
            var failed = new QuotaAnimation(); failed.Update(old, 0, now, false); failed.Update(renewed with { Error = "offline" }, 100, now, true);
            Check(!failed.Running(100), "failed read cannot confirm renewal");
            var waiting = new QuotaAnimation(); waiting.Update(DisplayState.Empty, 0, now, true);
            Check(!waiting.Running(0), "waiting for startup data does not animate fabricated values");
            waiting.Update(renewed with { Quotas = renewed.Quotas with { PlanType = "pro", Hour = null } }, 100, now, true);
            Check(waiting.Fill(true, null, 100) is null && waiting.Fill(false, renewed.Quotas.Week, 100) == 0, "Pro startup animates only available weekly ring");
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
            Check(WidgetStyles.Choices.Length == 7 && WidgetStyles.Normalize("unknown") == "gauges", "seven selectable styles and safe unknown-style fallback");
            Check(System.Text.Json.JsonSerializer.Deserialize<Settings>("{\"WidgetStyle\":\"badges\"}")?.WidgetStyle == "badges", "style setting round-trips");
            using (var idle = LayeredSurface.Paint(new(184, 32), general, "gauges", true, false, true, false))
            using (var hover = LayeredSurface.Paint(new(184, 32), general, "gauges", true, false, true, true))
            {
                Check(idle.GetPixel(92, 16).A == 1, "invisible input surface prevents right-click through transparent gaps");
                Check(hover.GetPixel(92, 16).A is > 1 and < 255 && hover.GetPixel(0, 0).A <= 1, "hover backdrop is translucent with visually transparent rounded corners");
                Check(Enumerable.Range(4, 22).Any(x => Enumerable.Range(4, 23).Any(y => hover.GetPixel(x, y).A == 255)), "gauge marks stay opaque on translucent hover background");
            }
            foreach (var dpi in new[] { 96, 120, 144, 192 })
            foreach (var letter in new[] { "H", "W" })
            {
                var scale = dpi / 96f;
                using var image = new Bitmap((int)(100 * scale), (int)(36 * scale));
                using var graphics = Graphics.FromImage(image);
                graphics.Clear(Color.White);
                Artwork.Gauge(graphics, new(0, 0, 90 * scale, 30 * scale), new(68, null), letter, Artwork.HourColor, true, false, true);
                var targetX = 12.5f * scale; var targetY = 15 * scale;
                var pixels = new List<Point>();
                for (var y = (int)(7 * scale); y < (int)(23 * scale); y++)
                for (var x = (int)(5 * scale); x < (int)(20 * scale); x++)
                    if (image.GetPixel(x, y).ToArgb() == Color.FromArgb(30, 38, 48).ToArgb()) pixels.Add(new(x, y));
                Check(pixels.Count > 0 && Math.Abs((pixels.Min(p => p.X) + pixels.Max(p => p.X) + 1) / 2f - targetX) <= 1
                    && Math.Abs((pixels.Min(p => p.Y) + pixels.Max(p => p.Y) + 1) / 2f - targetY) <= 1, $"{letter} visible glyph centered at {dpi} DPI");
            }
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
