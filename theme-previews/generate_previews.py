from PIL import Image, ImageDraw, ImageFont, ImageFilter
import math
from pathlib import Path

OUT = Path(__file__).resolve().parent
W, H = 1920, 1080
FONT_REG = r"C:\Windows\Fonts\msyh.ttc"
FONT_BOLD = r"C:\Windows\Fonts\msyhbd.ttc"

THEMES = {
    "01-electric-blue": {
        "name": "经典电光蓝 · ELECTRIC BLUE & CYAN",
        "bg0": "#060B18", "bg1": "#0B1120", "panel": "#0B1B38",
        "panel2": "#102A50", "main": "#0066FF", "accent": "#00F2FE",
        "text": "#C9F8FF", "muted": "#78AFC2", "value": "#F2F86A", "curve2": "#FF746C",
    },
    "02-ai-violet": {
        "name": "AI 极光紫 · ELECTRIC VIOLET & INDIGO",
        "bg0": "#080611", "bg1": "#0D0B18", "panel": "#171333",
        "panel2": "#29194F", "main": "#6366F1", "accent": "#C05CFF",
        "text": "#F1DFFF", "muted": "#AC91C4", "value": "#FFE46A", "curve2": "#EC4899",
    },
    "03-cyber-mint": {
        "name": "赛博薄荷绿 · CYBER MINT & MATRIX GREEN",
        "bg0": "#040806", "bg1": "#090D10", "panel": "#09221B",
        "panel2": "#103A2C", "main": "#10B981", "accent": "#00F5D4",
        "text": "#C9FFF3", "muted": "#78B6A5", "value": "#D6FF4F", "curve2": "#FF746C",
    },
    "04-space-gray": {
        "name": "暗黑钛金灰 · MONOCHROME & SPACE GRAY",
        "bg0": "#050506", "bg1": "#0A0A0C", "panel": "#17191D",
        "panel2": "#292C32", "main": "#9CA3AF", "accent": "#F4F4F5",
        "text": "#FFFFFF", "muted": "#A1A1AA", "value": "#FFFFFF", "curve2": "#FF5500",
    },
}


def rgb(value, alpha=255):
    value = value.lstrip("#")
    return tuple(int(value[i:i + 2], 16) for i in (0, 2, 4)) + (alpha,)


def mix(a, b, t, alpha=255):
    aa, bb = rgb(a), rgb(b)
    return tuple(int(aa[i] * (1 - t) + bb[i] * t) for i in range(3)) + (alpha,)


def font(size, bold=False):
    return ImageFont.truetype(FONT_BOLD if bold else FONT_REG, size)


def gradient_background(theme):
    image = Image.new("RGBA", (W, H), rgb(theme["bg0"]))
    px = image.load()
    c0, c1 = rgb(theme["bg0"]), rgb(theme["bg1"])
    for y in range(H):
        for x in range(W):
            t = min(1.0, 0.20 + 0.52 * y / H + 0.28 * x / W)
            px[x, y] = tuple(int(c0[i] * (1 - t) + c1[i] * t) for i in range(3)) + (255,)
    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gd.ellipse((W * .21, -H * .60, W * .79, H * .30), fill=rgb(theme["main"], 85))
    glow = glow.filter(ImageFilter.GaussianBlur(150))
    image.alpha_composite(glow)
    grid = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    g = ImageDraw.Draw(grid)
    for x in range(0, W, 48):
        g.line((x, 0, x, H), fill=rgb(theme["accent"], 10), width=1)
    for y in range(0, H, 48):
        g.line((0, y, W, y), fill=rgb(theme["accent"], 10), width=1)
    image.alpha_composite(grid)
    return image


def glow_line(image, points, color, width=2, blur=8):
    layer = Image.new("RGBA", image.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    d.line(points, fill=rgb(color, 100), width=width + 7, joint="curve")
    image.alpha_composite(layer.filter(ImageFilter.GaussianBlur(blur)))
    ImageDraw.Draw(image).line(points, fill=rgb(color), width=width, joint="curve")


def panel(image, box, theme, title=""):
    x0, y0, x1, y1 = box
    glow = Image.new("RGBA", image.size, (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gd.rounded_rectangle(box, radius=12, outline=rgb(theme["main"], 115), width=7)
    image.alpha_composite(glow.filter(ImageFilter.GaussianBlur(12)))
    d = ImageDraw.Draw(image)
    d.rounded_rectangle(box, radius=12, fill=rgb(theme["panel"], 238), outline=rgb(theme["main"], 150), width=1)
    if title:
        d.rounded_rectangle((x0, y0, x1, y0 + 44), radius=12, fill=rgb(theme["panel2"], 245))
        d.rectangle((x0, y0 + 30, x1, y0 + 44), fill=rgb(theme["panel2"], 245))
        d.line((x0 + 12, y0 + 44, x1 - 12, y0 + 44), fill=rgb(theme["accent"], 95), width=1)
        d.text(((x0 + x1) / 2, y0 + 22), title, font=font(18, True), fill=rgb(theme["text"]), anchor="mm")
    cut = 18
    c = rgb(theme["accent"], 220)
    for points in [((x0, y0 + cut), (x0, y0), (x0 + cut, y0)),
                   ((x1 - cut, y0), (x1, y0), (x1, y0 + cut)),
                   ((x0, y1 - cut), (x0, y1), (x0 + cut, y1)),
                   ((x1 - cut, y1), (x1, y1), (x1, y1 - cut))]:
        d.line(points, fill=c, width=2)


def gauge(image, cx, cy, radius, theme, value, caption):
    d = ImageDraw.Draw(image)
    arc_box = (cx - radius, cy - radius, cx + radius, cy + radius)
    glow = Image.new("RGBA", image.size, (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gd.arc(arc_box, 135, 405, fill=rgb(theme["accent"], 160), width=12)
    image.alpha_composite(glow.filter(ImageFilter.GaussianBlur(9)))
    d.arc(arc_box, 135, 405, fill=rgb(theme["accent"]), width=5)
    d.arc((cx - radius + 9, cy - radius + 9, cx + radius - 9, cy + radius - 9), 135, 405, fill=rgb(theme["main"], 170), width=2)
    for i in range(31):
        angle = math.radians(135 + i * 9)
        r1 = radius - (15 if i % 5 == 0 else 10)
        x1, y1 = cx + math.cos(angle) * r1, cy + math.sin(angle) * r1
        x2, y2 = cx + math.cos(angle) * (radius - 3), cy + math.sin(angle) * (radius - 3)
        d.line((x1, y1, x2, y2), fill=rgb(theme["value"] if i % 5 == 0 else theme["text"], 220), width=2 if i % 5 == 0 else 1)
    ratio = (value + 3000) / 6000
    angle = math.radians(135 + 270 * ratio)
    nx, ny = cx + math.cos(angle) * (radius - 25), cy + math.sin(angle) * (radius - 25)
    glow_line(image, [(cx, cy), (nx, ny)], theme["value"], 2, 4)
    d = ImageDraw.Draw(image)
    d.ellipse((cx - 5, cy - 5, cx + 5, cy + 5), fill=rgb(theme["value"]))
    d.text((cx, cy + 27), f"{value:.0f}", font=font(17, True), fill=rgb(theme["value"]), anchor="mm")
    d.text((cx, cy + radius + 15), caption, font=font(12), fill=rgb(theme["muted"]), anchor="mm")


def chart(image, box, theme, compact=False):
    x0, y0, x1, y1 = box
    d = ImageDraw.Draw(image)
    for i in range(1, 5):
        y = y0 + (y1 - y0) * i / 5
        d.line((x0, y, x1, y), fill=rgb(theme["accent"], 35), width=1)
    for i in range(1, 8):
        x = x0 + (x1 - x0) * i / 8
        d.line((x, y0, x, y1), fill=rgb(theme["accent"], 22), width=1)
    p1, p2 = [], []
    count = 80
    for i in range(count):
        x = x0 + (x1 - x0) * i / (count - 1)
        a = math.sin(i * .13) * .32 + math.cos(i * .047) * .18
        b = math.cos(i * .11 + .4) * .36 - math.sin(i * .061) * .16
        p1.append((x, (y0 + y1) / 2 - a * (y1 - y0)))
        p2.append((x, (y0 + y1) / 2 - b * (y1 - y0)))
    glow_line(image, p1, theme["accent"], 3 if not compact else 2, 6)
    glow_line(image, p2, theme["curve2"], 3 if not compact else 2, 5)


def compass(image, cx, cy, radius, theme):
    d = ImageDraw.Draw(image)
    for add, width, alpha in [(19, 2, 80), (10, 2, 120), (0, 7, 255), (-12, 2, 160)]:
        d.ellipse((cx - radius - add, cy - radius - add, cx + radius + add, cy + radius + add), outline=rgb(theme["accent"], alpha), width=width)
    glow = Image.new("RGBA", image.size, (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse((cx - radius, cy - radius, cx + radius, cy + radius), outline=rgb(theme["main"], 130), width=16)
    image.alpha_composite(glow.filter(ImageFilter.GaussianBlur(15)))
    for i in range(72):
        angle = math.radians(i * 5 - 90)
        outer = radius - 5
        inner = radius - (27 if i % 9 == 0 else 15)
        color = theme["value"] if i % 9 == 0 else theme["text"]
        d.line((cx + math.cos(angle) * inner, cy + math.sin(angle) * inner,
                cx + math.cos(angle) * outer, cy + math.sin(angle) * outer), fill=rgb(color, 230), width=2 if i % 9 == 0 else 1)
    d.polygon([(cx, cy - radius + 48), (cx - 12, cy + 28), (cx, cy + 16), (cx + 12, cy + 28)], fill=rgb(theme["value"]))
    d.ellipse((cx - 8, cy - 8, cx + 8, cy + 8), fill=rgb(theme["accent"]))
    d.text((cx, cy + 65), "15.0°", font=font(30, True), fill=rgb(theme["value"]), anchor="mm")
    d.text((cx, cy + 98), "航向角 / HEADING", font=font(13), fill=rgb(theme["muted"]), anchor="mm")


def metric(draw, x, y, name, value, theme):
    draw.text((x, y), name, font=font(13), fill=rgb(theme["muted"]))
    draw.text((x, y + 23), value, font=font(19, True), fill=rgb(theme["value"]))


def make_dashboard(key, theme):
    image = gradient_background(theme)
    d = ImageDraw.Draw(image)
    d.text((70, 42), "SYSTEM 01 · ONLINE", font=font(13, True), fill=rgb(theme["accent"]), anchor="lm")
    d.text((W / 2, 50), "水下清洁机器人智能监控系统", font=font(37, True), fill=rgb(theme["text"]), anchor="mm")
    d.text((W - 70, 42), theme["name"], font=font(13, True), fill=rgb(theme["muted"]), anchor="rm")
    glow_line(image, [(580, 82), (1340, 82)], theme["main"], 2, 14)

    nav_y0, nav_y1 = 102, 171
    labels = ["路径规划", "健康监控", "视频监控", "数据管理"]
    for i, text in enumerate(labels):
        x0 = 46 + i * 350
        x1 = x0 + 300
        active = i == 1
        d.rounded_rectangle((x0, nav_y0, x1, nav_y1), radius=27, fill=rgb(theme["panel2"] if active else theme["panel"], 240), outline=rgb(theme["accent"] if active else theme["main"], 220 if active else 100), width=2 if active else 1)
        if active:
            glow_line(image, [(x0 + 55, nav_y1 - 2), (x1 - 55, nav_y1 - 2)], theme["accent"], 3, 9)
        d.text(((x0 + x1) / 2, (nav_y0 + nav_y1) / 2), text, font=font(22 if active else 19, active), fill=rgb(theme["text"] if active else theme["muted"]), anchor="mm")

    left = (30, 200, 505, 1045)
    center = (522, 200, 1395, 1045)
    right = (1412, 200, 1890, 1045)
    panel(image, left, theme, "推进器状态 / THRUSTERS")
    panel(image, center, theme, "姿态与航向 / ATTITUDE & HEADING")
    panel(image, right, theme, "环境与系统状态 / SYSTEM STATUS")

    positions = [(145, 330), (385, 330), (145, 545), (385, 545), (145, 760), (385, 760)]
    values = [-2250, -2380, -2180, -2310, -2450, -2290]
    for i, ((cx, cy), val) in enumerate(zip(positions, values)):
        gauge(image, cx, cy, 82, theme, val, f"推进器 {i + 1}")
    chart(image, (55, 890, 480, 1012), theme, True)

    d.rounded_rectangle((580, 260, 1338, 320), radius=6, fill=rgb(theme["bg0"], 235), outline=rgb(theme["main"], 100), width=1)
    for i, tick in enumerate(["−60", "−30", "0", "30", "60"]):
        x = 630 + i * 165
        d.text((x, 290), tick, font=font(15), fill=rgb(theme["muted"]), anchor="mm")
        d.line((x, 308, x, 319), fill=rgb(theme["accent"]), width=1)
    compass(image, 960, 620, 225, theme)
    gauge(image, 670, 890, 82, theme, 82, "纵倾角 / PITCH")
    gauge(image, 1250, 890, 82, theme, 239, "横倾角 / ROLL")
    d.text((960, 890), "转艏控制力", font=font(16), fill=rgb(theme["muted"]), anchor="mm")
    d.text((960, 930), "30 Nm", font=font(30, True), fill=rgb(theme["value"]), anchor="mm")
    d.text((960, 985), "实时姿态数据 · 120 Hz", font=font(13), fill=rgb(theme["muted"]), anchor="mm")

    panel(image, (1425, 255, 1877, 515), theme, "环境信息 / ENVIRONMENT")
    metric(d, 1455, 325, "绝对流速", "0.5 m/s", theme)
    metric(d, 1668, 325, "相对流速", "0.8 m/s", theme)
    metric(d, 1455, 407, "水温", "18.6 ℃", theme)
    metric(d, 1668, 407, "当前深度", "12.4 m", theme)
    panel(image, (1425, 530, 1877, 755), theme, "电池信息 / BATTERY")
    bx, by, br = 1651, 652, 70
    d.ellipse((bx - br, by - br, bx + br, by + br), outline=rgb(theme["main"], 90), width=15)
    d.arc((bx - br, by - br, bx + br, by + br), -90, 169, fill=rgb(theme["accent"]), width=9)
    d.text((bx, by - 4), "72%", font=font(28, True), fill=rgb(theme["text"]), anchor="mm")
    d.text((bx, by + 37), "48.2 V", font=font(14), fill=rgb(theme["muted"]), anchor="mm")
    panel(image, (1425, 770, 1877, 1028), theme, "系统状态 / STATUS")
    statuses = [("定位", True), ("摄像头", True), ("通讯状态", True), ("惯导", False), ("推进器", True), ("深度计", True)]
    for i, (name, ok) in enumerate(statuses):
        x = 1470 + (i % 2) * 205
        y = 850 + (i // 2) * 58
        c = theme["accent"] if ok else "#FFD45A"
        glow = Image.new("RGBA", image.size, (0, 0, 0, 0))
        ImageDraw.Draw(glow).ellipse((x - 10, y - 10, x + 10, y + 10), fill=rgb(c, 150))
        image.alpha_composite(glow.filter(ImageFilter.GaussianBlur(8)))
        d = ImageDraw.Draw(image)
        d.ellipse((x - 6, y - 6, x + 6, y + 6), fill=rgb(c))
        d.text((x + 23, y), name, font=font(15), fill=rgb(theme["text"]), anchor="lm")

    d.text((32, H - 18), "QT 5.15.2 · QPAINTER UI COLOR PREVIEW", font=font(11), fill=rgb(theme["muted"], 180), anchor="lm")
    d.text((W - 32, H - 18), key.upper(), font=font(11, True), fill=rgb(theme["accent"], 210), anchor="rm")
    path = OUT / f"{key}.png"
    image.convert("RGB").save(path, "PNG", optimize=True)
    return image


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    images = []
    for key, theme in THEMES.items():
        images.append((key, theme, make_dashboard(key, theme)))
    sheet = Image.new("RGB", (1920, 1080), (5, 7, 8))
    for i, (key, theme, image) in enumerate(images):
        thumb = image.convert("RGB").resize((944, 531), Image.Resampling.LANCZOS)
        x = 8 + (i % 2) * 960
        y = 8 + (i // 2) * 540
        sheet.paste(thumb, (x, y))
        draw = ImageDraw.Draw(sheet)
        draw.rounded_rectangle((x + 10, y + 10, x + 58, y + 58), radius=12, fill=rgb(theme["bg0"])[:3], outline=rgb(theme["accent"])[:3], width=2)
        draw.text((x + 34, y + 34), str(i + 1), font=font(24, True), fill=rgb(theme["accent"])[:3], anchor="mm")
    sheet.save(OUT / "00-all-themes-comparison.png", "PNG", optimize=True)
    print("Generated:")
    for file in sorted(OUT.glob("*.png")):
        print(file)


if __name__ == "__main__":
    main()
