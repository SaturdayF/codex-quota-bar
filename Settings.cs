using System.Text.Json;
using Microsoft.Win32;

namespace CodexQuotaBar;

internal sealed class Settings
{
    public string DisplayMode { get; set; } = "all"; // all, primary, selected
    public List<string> Displays { get; set; } = [];
    public string? CodexPath { get; set; }
    public bool ShowDialLetters { get; set; } = true;
    public static string DataDirectory => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "CodexQuotaBar");
    public static string FilePath => Path.Combine(DataDirectory, "settings.json");
    public static Settings Load()
    {
        try
        {
            var loaded = JsonSerializer.Deserialize<Settings>(File.ReadAllText(FilePath)) ?? new();
            loaded.Displays ??= [];
            return loaded;
        }
        catch (Exception e) when (e is IOException or JsonException or UnauthorizedAccessException) { return new(); }
    }
    public void Save()
    {
        Directory.CreateDirectory(DataDirectory);
        var temporary = FilePath + ".tmp";
        File.WriteAllText(temporary, JsonSerializer.Serialize(this, new JsonSerializerOptions { WriteIndented = true }));
        File.Move(temporary, FilePath, overwrite: true);
    }
    public IEnumerable<Screen> SelectedScreens()
    {
        var all = Screen.AllScreens;
        if (DisplayMode == "all") return all;
        if (DisplayMode == "primary") return all.Where(s => s.Primary);
        var selected = all.Where(s => Displays.Contains(s.DeviceName)).ToArray();
        // A disconnected selected monitor falls back to the primary, without losing the saved choice.
        return selected.Length == 0 ? all.Where(s => s.Primary) : selected;
    }
}

internal static class Startup
{
    private const string Key = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string Name = "CodexQuotaBar";
    public static bool Enabled
    {
        get { using var key = Registry.CurrentUser.OpenSubKey(Key); return key?.GetValue(Name) is string; }
    }
    public static void Set(bool enabled)
    {
        using var key = Registry.CurrentUser.CreateSubKey(Key);
        if (enabled) key.SetValue(Name, $"\"{Environment.ProcessPath}\"");
        else key.DeleteValue(Name, throwOnMissingValue: false);
    }
}
