using System.Drawing.Drawing2D;
namespace CodexQuotaBar;

internal sealed class RoundedSettingsButton : Button
{
    private bool hovered, pressed;
    public RoundedSettingsButton()
    {
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
    }
    protected override void OnMouseEnter(EventArgs e) { hovered = true; Invalidate(); base.OnMouseEnter(e); }
    protected override void OnMouseLeave(EventArgs e) { hovered = pressed = false; Invalidate(); base.OnMouseLeave(e); }
    protected override void OnMouseDown(MouseEventArgs e) { pressed = e.Button == MouseButtons.Left; Invalidate(); base.OnMouseDown(e); }
    protected override void OnMouseUp(MouseEventArgs e) { pressed = false; Invalidate(); base.OnMouseUp(e); }
    private Color BackgroundSurface()
    {
        for (var parent = Parent; parent is not null; parent = parent.Parent)
            if (parent.BackColor.A == 255) return parent.BackColor;
        return SystemColors.Control;
    }
    protected override void OnPaintBackground(PaintEventArgs e) => e.Graphics.Clear(BackgroundSurface());
    protected override void OnPaint(PaintEventArgs e)
    {
        // Flat Button painting can bypass OnPaintBackground. Paint every buffer pixel,
        // including corners, instead of leaving transparent pixels that appear black.
        e.Graphics.Clear(BackgroundSurface());
        if (Width < 3 || Height < 3) return;
        e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
        var fill = hovered && Enabled ? FlatAppearance.MouseOverBackColor : BackColor;
        if (fill.IsEmpty) fill = BackColor;
        if (pressed) fill = ControlPaint.Dark(fill, .08f);
        var radius = Math.Min(8 * DeviceDpi / 96f, (Height - 2) / 2f);
        using var shape = HoverDetails.Round(new(.5f, .5f, Width - 1, Height - 1), radius);
        using var brush = new SolidBrush(fill); e.Graphics.FillPath(brush, shape);
        var text = new Rectangle(Padding.Left + 8, Padding.Top, Math.Max(1, Width - Padding.Horizontal - 16), Math.Max(1, Height - Padding.Vertical));
        var align = TextAlign is ContentAlignment.TopLeft or ContentAlignment.MiddleLeft or ContentAlignment.BottomLeft ? TextFormatFlags.Left : TextFormatFlags.HorizontalCenter;
        TextRenderer.DrawText(e.Graphics, Text, Font, text, Enabled ? ForeColor : SystemColors.GrayText,
            align | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis);
        if (Focused && ShowFocusCues)
        {
            using var focus = HoverDetails.Round(new(3, 3, Width - 6, Height - 6), Math.Max(1, radius - 2));
            using var pen = new Pen(ForeColor); e.Graphics.DrawPath(pen, focus);
        }
    }
}
