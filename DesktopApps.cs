using System.Diagnostics;

namespace CodexQuotaBar;

internal static class DesktopApps
{
    public static bool IsDesktopBinary(string name, string? path)
    {
        // Codex's CLI and app-server also use codex.exe. Never let a CLI keep the display alive.
        if (name.Equals("ChatGPT", StringComparison.OrdinalIgnoreCase)) return true;
        if (!name.Equals("Codex", StringComparison.OrdinalIgnoreCase) || path is null) return false;
        var p = path.Replace('/', '\\');
        return !p.Contains("\\bin\\", StringComparison.OrdinalIgnoreCase)
            && !p.Contains("\\vendor\\", StringComparison.OrdinalIgnoreCase)
            && !p.Contains("\\resources\\", StringComparison.OrdinalIgnoreCase)
            && (p.Contains("\\app\\", StringComparison.OrdinalIgnoreCase) || p.Contains("\\Programs\\Codex\\", StringComparison.OrdinalIgnoreCase));
    }
    public static bool AnyRunning()
    {
        foreach (var name in new[] { "ChatGPT", "Codex" })
        foreach (var process in Process.GetProcessesByName(name))
        {
            using (process)
            {
                try
                {
                    if (process.HasExited) continue;
                    // ChatGPT can be protected by MSIX; its process name still identifies the desktop app.
                    if (name == "ChatGPT" || IsDesktopBinary(name, process.MainModule?.FileName)) return true;
                }
                catch (Exception e) when (e is System.ComponentModel.Win32Exception or InvalidOperationException) { }
            }
        }
        return false;
    }
}
