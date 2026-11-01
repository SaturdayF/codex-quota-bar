namespace CodexQuotaBar;

// First valid data and confirmed renewal provide progress; raw quotas remain actual.
internal sealed class QuotaAnimation
{
    [System.Runtime.InteropServices.DllImport("user32.dll")]
    private static extern bool SystemParametersInfo(uint action, uint parameter, out int value, uint flags);
    internal static bool MotionEnabled => !SystemParametersInfo(0x1042, 0, out var enabled, 0) || enabled != 0;
    internal const int Duration = 1200;
    private Quotas? previous;
    private long hourStarted = -1, weekStarted = -1;
    internal bool Running(long now) => Active(hourStarted, now) || Active(weekStarted, now);
    private static bool Active(long start, long now) => start >= 0 && now - start < Duration;
    internal void Update(DisplayState state, long now, DateTimeOffset utcNow, bool enabled)
    {
        if (!enabled) { hourStarted = weekStarted = -1; }
        if (state.Stale) return;
        if (enabled && previous is null)
        {
            if (state.Quotas.Hour is not null) hourStarted = now;
            if (state.Quotas.Week is not null) weekStarted = now;
        }
        if (enabled && previous is { } old && old.PlanType == state.Quotas.PlanType)
        {
            if (Renewed(old.Hour, state.Quotas.Hour, utcNow)) hourStarted = now;
            if (Renewed(old.Week, state.Quotas.Week, utcNow)) weekStarted = now;
        }
        previous = state.Quotas;
    }
    private static bool Renewed(QuotaWindow? before, QuotaWindow? after, DateTimeOffset now) =>
        before is not null && after is not null && before.ResetsAt is { } reset && reset <= now &&
        after.Remaining > before.Remaining + 1 &&
        (after.ResetsAt is null || after.ResetsAt >= reset);
    internal double? Fill(bool hour, QuotaWindow? target, long now)
    {
        return target is null || Progress(hour, now) is not { } progress ? null : target.Remaining * progress;
    }
    internal double? Progress(bool hour, long now)
    {
        var start = hour ? hourStarted : weekStarted;
        if (!Active(start, now)) return null;
        var progress = Math.Clamp((now - start) / (double)Duration, 0, 1);
        return 1 - Math.Pow(1 - progress, 3);
    }
}
