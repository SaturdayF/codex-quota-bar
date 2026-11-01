"""Reproducible vector app-icon concepts and their review sheet (Pillow for PNG)."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "assets" / "icon-concepts"
OUT.mkdir(parents=True, exist_ok=True)
GREEN, BLUE, INK = "#219E86", "#648DE6", "#20252D"

def make_icon(kind, size=256):
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
    box((8,8,248,248), 54, "#FFFFFF" if kind == "E" else INK)
    if kind == "A":
        for bounds, end, color in [((35,56,137,158),205,GREEN), ((119,98,221,200),145,BLUE)]:
            arc(bounds,-90,269,"#46515F",15); arc(bounds,-90,end,color,15)
    elif kind == "B":
        arc((47,42,207,202),-90,269,"#46515F",23)
        arc((47,42,207,202),-90,35,BLUE,23); arc((47,42,207,202),55,235,GREEN,23)
        box((143,159,207,180),10,BLUE)
    elif kind == "C":
        box((43,57,213,105),16,"#38434F"); box((43,57,174,105),16,GREEN)
        box((43,145,213,193),16,"#38434F"); box((43,145,135,193),16,BLUE)
    elif kind == "D":
        box((65,44,191,64),10,BLUE); box((65,192,191,212),10,GREEN)
        draw.polygon([(75,77),(181,77),(128,125)],fill=BLUE)
        draw.polygon([(128,134),(75,180),(181,180)],fill=GREEN)
        svg.append(f'<path d="M75 77H181L128 125Z" fill="{BLUE}"/><path d="M128 134L75 180H181Z" fill="{GREEN}"/>')
    else:
        arc((42,48,166,172),-90,269,"#CAD2DD" if kind == "E" else "#596472",15)
        arc((42,48,166,172),-90,170,GREEN,15)
        blue_bounds=(127,113,207,193) if kind == "E" else (119,105,215,201)
        arc(blue_bounds,-90,269,"#CAD2DD" if kind == "E" else "#596472",15)
        arc(blue_bounds,-90,125,BLUE,15)
        arc((42,48,166,172),-25,20,GREEN,15)
        if kind == "F":
            draw.line((192,181,211,202),fill=BLUE,width=15)
            svg.append(f'<path d="M192 181L211 202" stroke="{BLUE}" stroke-width="15" stroke-linecap="round"/>')
    svg.append('</svg>')
    (OUT/f'{kind}.svg').write_text(''.join(svg),encoding="utf-8")
    if kind == "E":
        image.save(ROOT/"assets"/"app.png")
        image.save(ROOT/"assets"/"app.ico",sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
        (ROOT/"assets"/"app.svg").write_text(''.join(svg),encoding="utf-8")
    return image.resize((size,size),Image.Resampling.LANCZOS)

if __name__ == '__main__':
    sheet=Image.new("RGB",(1200,480),"#F5F6F8"); draw=ImageDraw.Draw(sheet)
    font=lambda size:ImageFont.truetype("C:/Windows/Fonts/msyh.ttc",size)
    draw.text((32,23),"Codex Quota Bar · 应用图标候选",font=font(26),fill=INK)
    names=["A  双环 · 对应两种额度","B  字母 Q · Quota","C  双进度 · 简洁直观","D  沙漏 · 时间窗口"]
    for i,(kind,name) in enumerate(zip("ABCD",names)):
        x=24+i*294
        draw.rounded_rectangle((x,80,x+276,454),radius=18,fill="white")
        icon=make_icon(kind,152); sheet.paste(icon,(x+62,110),icon)
        draw.text((x+16,284),name,font=font(17),fill=INK)
        draw.text((x+16,326),"小尺寸预览",font=font(14),fill="#717986")
        for j,size in enumerate([16,24,32,48]):
            small=icon.resize((size,size),Image.Resampling.LANCZOS)
            sheet.paste(small,(x+20+j*60,375-size//2),small)
    (ROOT/"docs"/"designs").mkdir(exist_ok=True)
    sheet.save(ROOT/"docs"/"designs"/"app-icon-concepts.png")
    revised=Image.new("RGB",(720,390),"#F5F6F8"); rd=ImageDraw.Draw(revised)
    for i,(kind,name) in enumerate([("E","E  大绿环 + 小蓝环 · 相扣"),("F","F  相扣双环 + Q 尾笔")]):
        x=20+i*350; rd.rounded_rectangle((x,20,x+330,370),radius=18,fill="white")
        icon=make_icon(kind,196); revised.paste(icon,(x+67,40),icon)
        rd.text((x+24,254),name,font=font(20),fill=INK)
        for j,size in enumerate([16,24,32,48]):
            small=icon.resize((size,size),Image.Resampling.LANCZOS)
            revised.paste(small,(x+40+j*67,325-size//2),small)
    revised.save(ROOT/"docs"/"designs"/"app-icon-interlocked.png")
