namespace CodexQuotaBar;

internal static class CodexLocator
{
    public static string Find(string? configured)
    {
        if (!string.IsNullOrWhiteSpace(configured))
        {
            if (File.Exists(configured) && configured.EndsWith(".exe", StringComparison.OrdinalIgnoreCase)) return configured;
            throw new InvalidOperationException("设置中的 Codex 路径不是有效的 exe");
        }
        var candidates = new List<string>();
        foreach (var folder in (Environment.GetEnvironmentVariable("PATH") ?? "").Split(Path.PathSeparator))
        {
            if (string.IsNullOrWhiteSpace(folder)) continue;
            var dir = folder.Trim('"');
            // npm's codex.cmd is not launched through a shell. Find its bundled executable instead.
            candidates.Add(Path.Combine(dir, "node_modules", "@openai", "codex", "node_modules", "@openai", "codex-win32-x64", "vendor", "x86_64-pc-windows-msvc", "codex", "codex.exe"));
            candidates.Add(Path.Combine(dir, "node_modules", "@openai", "codex", "vendor", "x86_64-pc-windows-msvc", "codex", "codex.exe"));
            if (!dir.Contains("WindowsApps", StringComparison.OrdinalIgnoreCase)) candidates.Add(Path.Combine(dir, "codex.exe"));
        }
        var bundled = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "OpenAI", "Codex", "bin");
        if (Directory.Exists(bundled))
        {
            try { candidates.AddRange(Directory.GetDirectories(bundled).OrderByDescending(Directory.GetLastWriteTimeUtc).Select(p => Path.Combine(p, "codex.exe"))); }
            catch (UnauthorizedAccessException) { }
        }
        return candidates.FirstOrDefault(File.Exists) ?? throw new FileNotFoundException("找不到 Codex CLI，请在设置中选择 codex.exe");
    }
}
