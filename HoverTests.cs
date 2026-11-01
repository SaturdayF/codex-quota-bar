namespace CodexQuotaBar;

internal static class HoverTests
{
    [STAThread]
    internal static int Run()
    {
        using var menu = new ContextMenuStrip();
        var point = new Point(int.MinValue, int.MinValue);
        using var overlay = new Overlay(Screen.PrimaryScreen!, menu, () => point);
        using var timer = new System.Windows.Forms.Timer { Interval = 75 };
        var phase = 0; var started = Environment.TickCount64; var phaseStarted = started; var exit = 1;
        overlay.Show(); overlay.PositionAtTaskbar();
        point = new Point(overlay.Left + overlay.Width / 2, overlay.Top + overlay.Height / 2);
        timer.Tick += (_, _) =>
        {
            if (Environment.TickCount64 - started > 4000) { Console.WriteLine("FAIL hover state timeout"); overlay.Close(); return; }
            if (phase == 0 && overlay.HoverHighlighted && overlay.DetailsVisible)
            {
                Console.WriteLine("PASS transparent interior hover highlights and opens details");
                point = new Point(overlay.DetailsBounds.Left + 20, overlay.DetailsBounds.Top + 20); phase = 1; phaseStarted = Environment.TickCount64;
            }
            else if (phase == 1 && Environment.TickCount64 - phaseStarted > 350)
            {
                if (!overlay.HoverHighlighted || !overlay.DetailsVisible) { Console.WriteLine("FAIL pointer entering popup closed it"); overlay.Close(); return; }
                Console.WriteLine("PASS pointer can enter the popup without closing it");
                point = new Point(int.MinValue, int.MinValue); phase = 2;
            }
            else if (phase == 2 && !overlay.HoverHighlighted && !overlay.DetailsVisible)
            {
                Console.WriteLine("PASS leaving closes details and removes border"); exit = 0; overlay.Close();
            }
        };
        timer.Start(); Application.Run(overlay); return exit;
    }
}
