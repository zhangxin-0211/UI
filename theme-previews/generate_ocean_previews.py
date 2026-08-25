from pathlib import Path
import importlib.util
import math

from PIL import Image, ImageDraw, ImageFilter, ImageFont


HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("base_preview", HERE / "generate_previews.py")
base = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(base)


OCEAN_THEMES = {
    "05-deep-sea-electric": {
        "number": 1,
        "name": "深海电光风 · DEEP SEA ELECTRIC",
        "bg0": "#050813", "bg1": "#0A0D18", "panel": "#10172A",
        "panel2": "#172650", "main": "#2563EB", "accent": "#A855F7",
        "text": "#E4F2FF", "muted": "#83A7C9", "value": "#8DEBFF", "curve2": "#C084FC",
        "gradients": ["#2563EB", "#3B82F6", "#A855F7", "#C084FC"],
        "description": "深蓝海沟 · 科技电光蓝 · 霓虹紫点缀",
    },
    "06-ocean-minimal-luxury": {
        "number": 2,
        "name": "极简海洋高级感 · OCEAN MINIMAL LUXURY",
        "bg0": "#070A10", "bg1": "#0B0F17", "panel": "#101A2C",
        "panel2": "#132B54", "main": "#1D4ED8", "accent": "#60A5FA",
        "text": "#EBF5FF", "muted": "#8CA9C2", "value": "#B9E1FF", "curve2": "#8B5CF6",
        "gradients": ["#1D4ED8", "#2563EB", "#60A5FA", "#8B5CF6"],
        "description": "曜石冷黑 · 深邃钴蓝 · 柔和极光紫",
    },
    "07-cyber-ice-ocean": {
        "number": 3,
        "name": "赛博冰蓝海洋 · CYBER ICE OCEAN",
        "bg0": "#030A12", "bg1": "#071526", "panel": "#081C30",
        "panel2": "#102951", "main": "#00D2FF", "accent": "#D946EF",
        "text": "#E5FBFF", "muted": "#80B8C8", "value": "#B9FFF5", "curve2": "#9333EA",
        "gradients": ["#00D2FF", "#22D3EE", "#D946EF", "#9333EA"],
        "description": "冰霜青蓝 · 深水靛蓝 · 赛博洋紫",
    },
}


def rgba(hex_color, alpha=255):
    h = hex_color.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4)) + (alpha,)


def lerp_color(a, b, t, alpha=255):
    ca, cb = rgba(a), rgba(b)
    return tuple(round(ca[i] * (1 - t) + cb[i] * t) for i in range(3)) + (alpha,)


def multi_color(stops, t, alpha=255):
    t = max(0.0, min(1.0, t))
    pos = t * (len(stops) - 1)
    index = min(len(stops) - 2, int(pos))
    return lerp_color(stops[index], stops[index + 1], pos - index, alpha)


def rounded_gradient(image, box, stops, alpha=80, diagonal=True, radius=12):
    x0, y0, x1, y1 = map(int, box)
    width, height = max(1, x1 - x0), max(1, y1 - y0)
    gradient = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    draw = ImageDraw.Draw(gradient)
    if diagonal:
        total = width + height
        for k in range(total):
            color = multi_color(stops, k / max(1, total - 1), alpha)
            draw.line((k, 0, 0, k), fill=color, width=2)
    else:
        for x in range(width):
            draw.line((x, 0, x, height), fill=multi_color(stops, x / max(1, width - 1), alpha), width=1)
    mask = Image.new("L", (width, height), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, width - 1, height - 1), radius=radius, fill=255)
    layer = Image.new("RGBA", image.size, (0, 0, 0, 0))
    layer.paste(gradient, (x0, y0), mask)
    image.alpha_composite(layer)


def gradient_line(image, points, stops, width=3, glow=11):
    aura = Image.new("RGBA", image.size, (0, 0, 0, 0))
    ad = ImageDraw.Draw(aura)
    count = max(1, len(points) - 1)
    for i in range(count):
        ad.line((points[i], points[i + 1]), fill=multi_color(stops, i / count, 120), width=width + 8)
    image.alpha_composite(aura.filter(ImageFilter.GaussianBlur(glow)))
    draw = ImageDraw.Draw(image)
    for i in range(count):
        draw.line((points[i], points[i + 1]), fill=multi_color(stops, i / count), width=width)


def ocean_atmosphere(image, theme):
    stops = theme["gradients"]
    overlay = Image.new("RGBA", image.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(overlay)

    # Deep-water light shafts and soft bioluminescent blooms.
    draw.polygon([(520, 0), (840, 0), (1110, 1080), (820, 1080)], fill=rgba(stops[1], 18))
    draw.polygon([(1050, 0), (1250, 0), (1540, 1080), (1310, 1080)], fill=rgba(stops[2], 11))
    for cx, cy, radius, color, alpha in [
        (350, 160, 280, stops[0], 45), (1480, 260, 330, stops[2], 36), (930, 990, 420, stops[1], 28)
    ]:
        draw.ellipse((cx - radius, cy - radius, cx + radius, cy + radius), fill=rgba(color, alpha))
    overlay = overlay.filter(ImageFilter.GaussianBlur(100))
    image.alpha_composite(overlay)

    # Ocean-current contour waves stay behind data and remain understated.
    wave_layer = Image.new("RGBA", image.size, (0, 0, 0, 0))
    for row in range(7):
        pts = []
        y_base = 185 + row * 132
        for x in range(-30, 1950, 24):
            y = y_base + math.sin(x * 0.006 + row * 0.75) * 15 + math.sin(x * 0.013) * 4
            pts.append((x, y))
        ImageDraw.Draw(wave_layer).line(pts, fill=rgba(stops[row % len(stops)], 18), width=1)
    image.alpha_composite(wave_layer)


def enhance_dashboard(image, theme):
    stops = theme["gradients"]
    ocean_atmosphere(image, theme)

    # Layered gradients on the main industrial surfaces.
    for box, alpha in [
        ((30, 200, 505, 1045), 28), ((522, 200, 1395, 1045), 25), ((1412, 200, 1890, 1045), 28),
        ((1425, 255, 1877, 515), 24), ((1425, 530, 1877, 755), 24), ((1425, 770, 1877, 1028), 24),
    ]:
        rounded_gradient(image, box, [stops[0], stops[1], stops[2]], alpha=alpha, radius=12)

    # Selected navigation uses the full marine aurora gradient.
    rounded_gradient(image, (396, 102, 696, 171), stops, alpha=85, diagonal=False, radius=27)

    # Top title beam and compass halo use different gradients to create depth.
    beam = [(575 + i * 8, 82 + math.sin(i * .24) * 1.3) for i in range(96)]
    gradient_line(image, beam, [stops[0], stops[1], stops[2], stops[3]], width=3, glow=13)

    # Add colored outer accents around the center instrument without covering data.
    draw = ImageDraw.Draw(image)
    compass_box = (716, 376, 1204, 864)
    for inset, alpha, width in [(0, 120, 2), (8, 80, 2), (18, 50, 1)]:
        color = stops[(inset // 8) % len(stops)]
        draw.arc((compass_box[0] + inset, compass_box[1] + inset, compass_box[2] - inset, compass_box[3] - inset), 202, 334, fill=rgba(color, alpha), width=width)
        draw.arc((compass_box[0] + inset, compass_box[1] + inset, compass_box[2] - inset, compass_box[3] - inset), 18, 150, fill=rgba(stops[-1], alpha), width=width)

    # Scheme badge and exact gradient palette chips.
    badge = (30, 22, 94, 86)
    rounded_gradient(image, badge, stops, alpha=220, diagonal=False, radius=14)
    d = ImageDraw.Draw(image)
    d.rounded_rectangle(badge, radius=14, outline=rgba(theme["text"], 230), width=2)
    d.text((62, 54), str(theme["number"]), font=base.font(28, True), fill=rgba(theme["text"]), anchor="mm")
    d.text((115, 91), theme["description"], font=base.font(13), fill=rgba(theme["muted"]), anchor="lm")
    for i, color in enumerate(stops):
        x0 = 1655 + i * 52
        d.rounded_rectangle((x0, 77, x0 + 43, 91), radius=7, fill=rgba(color), outline=rgba(theme["text"], 100), width=1)

    return image


def generate():
    images = []
    for key, theme in OCEAN_THEMES.items():
        image = base.make_dashboard(key, theme)
        image = enhance_dashboard(image, theme)
        image.convert("RGB").save(HERE / f"{key}.png", "PNG", optimize=True)
        images.append((key, theme, image.convert("RGB")))

    canvas = Image.new("RGB", (2560, 960), (3, 7, 13))
    draw = ImageDraw.Draw(canvas)
    draw.text((1280, 46), "海洋交互界面 · 三种渐变配色对比", font=base.font(34, True), fill=(230, 247, 255), anchor="mm")
    draw.text((1280, 83), "OCEAN INTERACTION UI · GRADIENT COLOR STUDY", font=base.font(15), fill=(111, 159, 187), anchor="mm")
    for i, (key, theme, image) in enumerate(images):
        thumb = image.resize((820, 461), Image.Resampling.LANCZOS)
        x, y = 20 + i * 845, 126
        canvas.paste(thumb, (x, y))
        d = ImageDraw.Draw(canvas)
        d.rounded_rectangle((x, 610, x + 820, 920), radius=18, fill=rgba(theme["bg1"])[:3], outline=rgba(theme["main"])[:3], width=2)
        d.text((x + 35, 650), f"方案 {theme['number']}", font=base.font(22, True), fill=rgba(theme["accent"])[:3])
        d.text((x + 35, 695), theme["name"], font=base.font(20, True), fill=rgba(theme["text"])[:3])
        d.text((x + 35, 735), theme["description"], font=base.font(16), fill=rgba(theme["muted"])[:3])
        for j, color in enumerate(theme["gradients"]):
            sx = x + 35 + j * 120
            d.rounded_rectangle((sx, 785, sx + 102, 825), radius=12, fill=rgba(color)[:3])
            d.text((sx + 51, 849), color, font=base.font(12), fill=rgba(theme["muted"])[:3], anchor="mm")
    canvas.save(HERE / "08-ocean-gradient-themes-comparison.png", "PNG", optimize=True)

    print("Generated ocean previews:")
    for key in OCEAN_THEMES:
        print(HERE / f"{key}.png")
    print(HERE / "08-ocean-gradient-themes-comparison.png")


if __name__ == "__main__":
    generate()
