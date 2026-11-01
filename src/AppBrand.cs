using System.Reflection;
namespace CodexQuotaBar;

internal static class AppBrand
{
    internal static string Version => Assembly.GetExecutingAssembly().GetName().Version!.ToString(3);
    internal static Icon Icon { get; } = LoadIcon();
    private static Icon LoadIcon()
    {
        using var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("CodexQuotaBar.AppIcon")!;
        using var icon = new Icon(stream);
        return (Icon)icon.Clone();
    }
}
