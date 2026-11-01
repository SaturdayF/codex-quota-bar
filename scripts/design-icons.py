"""Regenerate the approved white interlocked-ring SVG, PNG and ICO (requires Pillow)."""
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
GREEN, BLUE = "#219E86", "#648DE6"

def make_icon(size=256):
    image = Image.new("RGBA", (256, 256))
    draw = ImageDraw.Draw(image)
    svg = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256">']
    def box(bounds, radius, fill):
        draw.rounded_rectangle(bounds, radius, fill)
        x,y,r,b = bounds
        svg.append(f'<rect x="{x}" y="{y}" width="{r-x}" height="{b-y}" rx="{radius}" fill="{fill}"/>')
    def arc(bounds, start, end, fill, width):
        import math
        draw.arc(bounds, start, end, fill, width)
        x,y,r,b = bounds; radius=(r-x)/2; cx=(x+r)/2; cy=(y+b)/2
        # Pillow strokes inwards; use its stroke center radius in the SVG.
        mid=radius-width/2
        def mid_point(angle):
            a=math.radians(angle); return f'{cx+mid*math.cos(a):.3f},{cy+mid*math.sin(a):.3f}'
        svg.append(f'<path d="M {mid_point(start)} A {mid},{mid} 0 {int(end-start>180)} 1 {mid_point(end)}" fill="none" stroke="{fill}" stroke-width="{width}"/>')
    box((8,8,248,248), 54, "#FFFFFF")
    arc((42,48,166,172),-90,269,"#CAD2DD",15)
    arc((42,48,166,172),-90,170,GREEN,15)
    arc((127,113,207,193),-90,269,"#CAD2DD",15)
    arc((127,113,207,193),-90,125,BLUE,15)
    arc((42,48,166,172),-25,20,GREEN,15)
    svg.append('</svg>')
    image.save(ROOT/"assets"/"app.png")
    image.save(ROOT/"assets"/"app.ico",sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
    (ROOT/"assets"/"app.svg").write_text(''.join(svg),encoding="utf-8")
    return image.resize((size,size),Image.Resampling.LANCZOS)

if __name__ == '__main__':
    make_icon()
