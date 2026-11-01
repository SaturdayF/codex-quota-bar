using System.Globalization;
using System.Text.Json;

namespace CodexQuotaBar;

internal sealed record QuotaWindow(double Remaining, DateTimeOffset? ResetsAt);
internal sealed record Quotas(QuotaWindow? Hour, QuotaWindow? Week)
{
    public static Quotas Parse(JsonElement result)
    {
        JsonElement bucket;
        if (result.TryGetProperty("rateLimitsByLimitId", out var buckets) && buckets.ValueKind == JsonValueKind.Object)
        {
            // Never substitute a model-specific bucket for the general Codex bucket.
            if (!buckets.TryGetProperty("codex", out bucket)) return new(null, null);
        }
        else if (!result.TryGetProperty("rateLimits", out bucket)) return new(null, null);
        if (bucket.ValueKind != JsonValueKind.Object) return new(null, null);
        if (bucket.TryGetProperty("limitId", out var id) && id.ValueKind == JsonValueKind.String && id.GetString() != "codex") return new(null, null);
        QuotaWindow? hour = null, week = null;
        foreach (var name in new[] { "primary", "secondary" })
        {
            if (!bucket.TryGetProperty(name, out var w) || w.ValueKind != JsonValueKind.Object) continue;
            if (!w.TryGetProperty("windowDurationMins", out var duration) || duration.ValueKind != JsonValueKind.Number || !duration.TryGetInt32(out var mins)) continue;
            if (!w.TryGetProperty("usedPercent", out var used) || used.ValueKind != JsonValueKind.Number || !used.TryGetDouble(out var percent) || !double.IsFinite(percent)) continue;
            DateTimeOffset? reset = null;
            if (w.TryGetProperty("resetsAt", out var r) && r.ValueKind == JsonValueKind.Number && r.TryGetInt64(out var seconds))
            {
                try { reset = DateTimeOffset.FromUnixTimeSeconds(seconds); } catch (ArgumentOutOfRangeException) { }
            }
            var window = new QuotaWindow(Math.Clamp(100 - percent, 0, 100), reset);
            if (mins == 300) hour = window;
            if (mins == 10080) week = window;
        }
        return new(hour, week);
    }

    public static string Label(QuotaWindow? w) => w is null ? "--" : Math.Floor(w.Remaining).ToString("0", CultureInfo.InvariantCulture) + "%";
}

internal sealed record DisplayState(Quotas Quotas, DateTimeOffset? Updated, string? Error)
{
    public static DisplayState Empty => new(new(null, null), null, "等待首次读取");
    public bool Stale => Error is not null || Updated is null || DateTimeOffset.UtcNow - Updated > TimeSpan.FromSeconds(75);
    public string Tooltip(bool hour)
    {
        var w = hour ? Quotas.Hour : Quotas.Week;
        var title = hour ? "5 小时" : "每周";
        var reset = w?.ResetsAt is { } time ? time.ToLocalTime().ToString("MM/dd HH:mm") : "未知";
        var label = global::CodexQuotaBar.Quotas.Label(w);
        return $"Codex {title}剩余 {label}{(Stale ? " [过期/不可用]" : "")}\n重置 {reset}\n更新 {Updated?.ToLocalTime().ToString("HH:mm:ss") ?? "尚无"}";
    }
}
