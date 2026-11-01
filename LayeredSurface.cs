using System.Drawing.Imaging;
using System.Runtime.InteropServices;
namespace CodexQuotaBar;

internal static class LayeredSurface
{
    [StructLayout(LayoutKind.Sequential)] private struct Size { public int Width, Height; }
    [StructLayout(LayoutKind.Sequential)] private struct Position { public int X, Y; }
    [StructLayout(LayoutKind.Sequential, Pack = 1)] private struct Blend { public byte Operation, Flags, Alpha, Format; }
    [DllImport("user32.dll")] private static extern IntPtr GetDC(IntPtr window);
    [DllImport("user32.dll")] private static extern int ReleaseDC(IntPtr window, IntPtr dc);
    [DllImport("gdi32.dll")] private static extern IntPtr CreateCompatibleDC(IntPtr dc);
    [DllImport("gdi32.dll")] private static extern bool DeleteDC(IntPtr dc);
    [DllImport("gdi32.dll")] private static extern IntPtr SelectObject(IntPtr dc, IntPtr obj);
    [DllImport("gdi32.dll")] private static extern bool DeleteObject(IntPtr obj);
    [DllImport("user32.dll", SetLastError = true)] private static extern bool UpdateLayeredWindow(IntPtr window, IntPtr destinationDc, ref Position destination, ref Size size, IntPtr sourceDc, ref Position source, uint key, ref Blend blend, uint flags);
    internal static Bitmap Paint(System.Drawing.Size size, Quotas quotas, string style, bool letters, bool stale, bool light, bool hover, double? hourProgress = null, double? weekProgress = null, StylePreferences? preferences = null)
    {
        var image = new Bitmap(Math.Max(1, size.Width), Math.Max(1, size.Height), PixelFormat.Format32bppPArgb);
        using var g = Graphics.FromImage(image);
        // Alpha-zero pixels in a layered window pass mouse input to the taskbar.
        // A visually imperceptible 1/255-alpha input surface keeps gaps clickable,
        // including a fast right-click before the hover timer has fired.
        g.Clear(Color.FromArgb(1, Color.Black));
        g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
        g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;
        if (hover)
        {
            var scale = size.Height / 32f;
            using var path = HoverDetails.Round(new(.5f, .5f, size.Width - 1, size.Height - 1), 4 * scale);
            using var fill = new SolidBrush(Color.FromArgb(light ? 112 : 28, Color.White));
            g.FillPath(fill, path);
        }
        // Only the backdrop is translucent; data marks retain their own full alpha.
        WidgetStyles.Draw(g, new RectangleF(0, 0, size.Width, size.Height), quotas, style, letters, stale, light, hourProgress, weekProgress, preferences);
        return image;
    }
    internal static void Present(IntPtr window, Point location, Bitmap image)
    {
        var desktop = GetDC(IntPtr.Zero);
        var memory = CreateCompatibleDC(desktop);
        var bitmap = image.GetHbitmap(Color.FromArgb(0));
        var previous = SelectObject(memory, bitmap);
        try
        {
            var destination = new Position { X = location.X, Y = location.Y };
            var source = new Position();
            var size = new Size { Width = image.Width, Height = image.Height };
            var blend = new Blend { Operation = 0, Alpha = 255, Format = 1 };
            if (!UpdateLayeredWindow(window, desktop, ref destination, ref size, memory, ref source, 0, ref blend, 2))
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        }
        finally
        {
            SelectObject(memory, previous); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(IntPtr.Zero, desktop);
        }
    }
}
