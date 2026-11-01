namespace CodexQuotaBar;

internal static class RefreshSchedule
{
    internal static double DelaySeconds(int interval, bool success, int failures) => success ? Settings.NormalizeRefreshInterval(interval)
        : Math.Min(300, Settings.NormalizeRefreshInterval(interval) * Math.Pow(2, Math.Max(0, failures - 1)));
}
