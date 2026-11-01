namespace CodexQuotaBar;

internal static class AppLifetime
{
    internal const string QuitSignalName = @"Local\CodexQuotaBar.Quit";
    internal static void RequestQuit(string name = QuitSignalName)
    {
        try { if (EventWaitHandle.TryOpenExisting(name, out var signal)) { using (signal) signal.Set(); } }
        catch (UnauthorizedAccessException) { /* The helper still has a bounded process-exit fallback. */ }
    }
}
