using System.Collections.Concurrent;
using System.Diagnostics;
using System.Text;
using System.Text.Json;

namespace CodexQuotaBar;

internal sealed class RpcFailure(int code) : Exception("Codex 额度接口返回错误")
{
    public int Code { get; } = code;
}

internal sealed class AppServer : IDisposable
{
    private readonly Process process;
    private readonly CancellationTokenSource lifetime = new();
    private readonly ConcurrentDictionary<int, TaskCompletionSource<JsonElement>> pending = new();
    private readonly SemaphoreSlim writer = new(1, 1);
    private int nextId;
    private int disposed;
    private string? planType;
    public event Action<Quotas>? Updated;

    private AppServer(Process process)
    {
        this.process = process;
        _ = ReadLoop();
        // Drain stderr without persisting it: the CLI can include private account details in errors.
        _ = DrainErrors();
    }

    public static async Task<AppServer> Start(string executable, CancellationToken token, bool fake = false)
    {
        Directory.CreateDirectory(Settings.DataDirectory);
        var info = new ProcessStartInfo(executable)
        {
            UseShellExecute = false, CreateNoWindow = true,
            RedirectStandardInput = true, RedirectStandardOutput = true, RedirectStandardError = true,
            StandardInputEncoding = new UTF8Encoding(false), StandardOutputEncoding = Encoding.UTF8,
            WorkingDirectory = Settings.DataDirectory
        };
        info.ArgumentList.Add(fake ? "--fake-server" : "app-server");
        var child = Process.Start(info) ?? throw new IOException("无法启动 Codex app-server");
        var server = new AppServer(child);
        try
        {
            await server.Request("initialize", new { clientInfo = new { name = "codex_quota_bar", title = "Codex Quota Bar", version = "0.1.0" } }, token);
            await server.Send(new { method = "initialized", @params = new { } }, token);
            return server;
        }
        catch { server.Dispose(); throw; }
    }

    private async Task DrainErrors()
    {
        try { while (await process.StandardError.ReadLineAsync(lifetime.Token) is not null) { } }
        catch (Exception e) when (e is OperationCanceledException or IOException or ObjectDisposedException) { }
    }
    private async Task ReadLoop()
    {
        try
        {
            while (await process.StandardOutput.ReadLineAsync(lifetime.Token) is { } line)
            {
                using var doc = JsonDocument.Parse(line);
                var message = doc.RootElement;
                if (message.TryGetProperty("id", out var id) && id.ValueKind == JsonValueKind.Number && id.TryGetInt32(out var number) && pending.TryRemove(number, out var completion))
                {
                    if (message.TryGetProperty("error", out var error))
                        completion.TrySetException(new RpcFailure(error.TryGetProperty("code", out var code) ? code.GetInt32() : -1));
                    else if (message.TryGetProperty("result", out var result)) completion.TrySetResult(result.Clone());
                    else completion.TrySetException(new IOException("Codex 返回无效响应"));
                }
                else if (message.TryGetProperty("method", out var method) && method.GetString() == "account/rateLimits/updated" && message.TryGetProperty("params", out var args))
                {
                    // Other buckets must not clear or replace the general Codex quota.
                    if (args.TryGetProperty("rateLimits", out var bucket) && bucket.TryGetProperty("limitId", out var bucketId)
                        && bucketId.ValueKind == JsonValueKind.String && bucketId.GetString() != "codex") continue;
                    var quotas = Quotas.Parse(args);
                    Updated?.Invoke(quotas with { PlanType = quotas.PlanType ?? planType });
                }
            }
        }
        catch (Exception e) when (e is IOException or JsonException or OperationCanceledException or ObjectDisposedException) { }
        finally
        {
            foreach (var pair in pending) pair.Value.TrySetException(new IOException("Codex 连接已关闭"));
            pending.Clear();
        }
    }
    private async Task Send(object message, CancellationToken token)
    {
        await writer.WaitAsync(token);
        try
        {
            ObjectDisposedException.ThrowIf(disposed != 0, this);
            await process.StandardInput.WriteLineAsync(JsonSerializer.Serialize(message).AsMemory(), token);
            await process.StandardInput.FlushAsync(token);
        }
        finally { writer.Release(); }
    }
    private async Task<JsonElement> Request(string method, object args, CancellationToken token)
    {
        var id = Interlocked.Increment(ref nextId);
        var completion = new TaskCompletionSource<JsonElement>(TaskCreationOptions.RunContinuationsAsynchronously);
        pending[id] = completion;
        try
        {
            await Send(new { id, method, @params = args }, token);
            return await completion.Task.WaitAsync(TimeSpan.FromSeconds(20), token);
        }
        finally { pending.TryRemove(id, out _); }
    }
    public async Task<Quotas> Read(CancellationToken token)
    {
        // Read only the plan; never persist or expose the returned account identity.
        try
        {
            var account = await Request("account/read", new { refreshToken = false }, token);
            planType = account.TryGetProperty("account", out var value) && value.ValueKind == JsonValueKind.Object
                && value.TryGetProperty("planType", out var plan) && plan.ValueKind == JsonValueKind.String ? plan.GetString() : null;
        }
        catch (RpcFailure) { planType = null; }
        var quotas = Quotas.Parse(await Request("account/rateLimits/read", new { }, token));
        return quotas with { PlanType = quotas.PlanType ?? planType };
    }
    public void Dispose()
    {
        if (Interlocked.Exchange(ref disposed, 1) != 0) return;
        lifetime.Cancel();
        foreach (var pair in pending) pair.Value.TrySetCanceled();
        try { if (!process.HasExited) process.Kill(entireProcessTree: true); }
        catch (Exception e) when (e is InvalidOperationException or System.ComponentModel.Win32Exception) { }
        process.Dispose();
    }
}
