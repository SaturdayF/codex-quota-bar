using System.Diagnostics;

namespace CodexQuotaBar;

internal static class Uninstaller
{
    internal static string InstalledDirectory => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs", "CodexQuotaBar", AppBrand.Version);
    internal static bool NativeInstalled(string directory)
    {
        var marker = Path.Combine(directory, "installer.ini");
        if (!File.Exists(marker) || !File.Exists(Path.Combine(directory, "unins000.exe"))) return false;
        try { var lines = File.ReadAllLines(marker); return lines.Contains("Product=CodexQuotaBar") && lines.Contains("Version=" + AppBrand.Version); }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { return false; }
    }
    internal static bool IsInstalled(string? executable, string installedDirectory) => executable is not null &&
        string.Equals(Path.GetFullPath(executable), Path.Combine(Path.GetFullPath(installedDirectory), "CodexQuotaBar.exe"), StringComparison.OrdinalIgnoreCase);
    internal static bool Available => NativeInstalled(AppContext.BaseDirectory) ||
        IsInstalled(Environment.ProcessPath, InstalledDirectory) && File.Exists(Path.Combine(InstalledDirectory, "uninstall.ps1"));
    internal static void Launch()
    {
        if (!Available) throw new InvalidOperationException("一键卸载仅适用于已安装的程序。便携版可退出后删除解压目录。");
        if (NativeInstalled(AppContext.BaseDirectory))
        {
            using var native = Process.Start(new ProcessStartInfo(Path.Combine(AppContext.BaseDirectory, "unins000.exe"), "/SILENT /NORESTART /RemoveSettings") { UseShellExecute = true })
                ?? throw new IOException("无法启动卸载程序。");
            AppLifetime.RequestQuit(); return;
        }
        var helperDirectory = Path.Combine(Path.GetTempPath(), "CodexQuotaBar-Uninstall-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(helperDirectory);
        var helper = Path.Combine(helperDirectory, "uninstall.ps1");
        try
        {
            File.Copy(Path.Combine(InstalledDirectory, "uninstall.ps1"), helper);
            var start = new ProcessStartInfo(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "WindowsPowerShell", "v1.0", "powershell.exe"))
            { UseShellExecute = false, CreateNoWindow = true, WorkingDirectory = helperDirectory };
            foreach (var argument in new[] { "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", helper,
                "-WaitForProcessId", Environment.ProcessId.ToString(), "-Version", AppBrand.Version, "-RemoveSettings", "-DeleteHelper", "-ShowResult" }) start.ArgumentList.Add(argument);
            using var process = Process.Start(start) ?? throw new IOException("无法启动卸载程序。");
            AppLifetime.RequestQuit();
        }
        catch
        {
            File.Delete(helper); Directory.Delete(helperDirectory); throw;
        }
    }
}
