"""Store art for Boom Boom Rocket Revolution: original artwork (night city, fireworks, rockets).

Outputs (PNG): capsule 600x900, header 920x430, hero 3840x1240 (no text), logo (transparent).
"""
import math
import random
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

FONT = "/home/claude/art/fonts/Bungee-Regular.ttf"
SUB_FONT = "/home/claude/art/fonts/Exo2.ttf"
OUT = sys.argv[1] if len(sys.argv) > 1 else "/home/claude/art/out"

# The four button colours: green (A), red (B), blue (X), yellow (Y), plus magenta / cyan accents.
COLOURS = [(80, 255, 120), (255, 70, 70), (70, 150, 255), (255, 220, 60), (255, 90, 230), (90, 240, 255)]


def blur(a, r):
    img = Image.fromarray(np.clip(a * 255, 0, 255).astype(np.uint8))
    return np.asarray(img.filter(ImageFilter.GaussianBlur(r))).astype(np.float32) / 255


def sky(w, h):
    y = np.linspace(0, 1, h)[:, None]
    top = np.array([6, 6, 26]) / 255
    mid = np.array([30, 12, 58]) / 255
    low = np.array([70, 24, 70]) / 255
    t = y[..., None]
    col = np.where(t < 0.6, top + (mid - top) * (t / 0.6), mid + (low - mid) * ((t - 0.6) / 0.4))
    img = np.broadcast_to(col, (h, w, 3)).copy()
    rng = np.random.default_rng(3)
    stars = rng.random((h, w)) > 0.9985
    img[stars] += 0.5 * rng.random((stars.sum(), 1))
    return img


def firework(light, cx, cy, radius, colour, rng, strands=90, scale=1.0):
    """Additive spark burst drawn into a float RGB light buffer."""
    h, w, _ = light.shape
    layer = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(layer)
    c = tuple(int(v) for v in colour)
    for i in range(strands):
        ang = 2 * math.pi * i / strands + rng.uniform(-0.03, 0.03)
        r = radius * rng.uniform(0.75, 1.0)
        steps = 14
        for s in range(steps):
            f = (s + 1) / steps
            # sparks droop a little under gravity
            x = cx + math.cos(ang) * r * f
            y = cy + math.sin(ang) * r * f + (f ** 2) * radius * 0.12
            fade = (1 - f * 0.65)
            size = max(1, int(scale * (2.6 - 1.6 * f)))
            cc = tuple(int(v * fade) for v in c)
            d.ellipse([x - size, y - size, x + size, y + size], fill=cc)
        tip_x = cx + math.cos(ang) * r
        tip_y = cy + math.sin(ang) * r + radius * 0.12
        s2 = max(1, int(3.2 * scale))
        d.ellipse([tip_x - s2, tip_y - s2, tip_x + s2, tip_y + s2], fill=(255, 255, 255))
    a = np.asarray(layer).astype(np.float32) / 255
    light += a + blur(a, 6 * scale) * 1.6 + blur(a, 26 * scale) * 1.2
    # bright core
    core = Image.new("RGB", (w, h))
    ImageDraw.Draw(core).ellipse([cx - radius * 0.12, cy - radius * 0.12, cx + radius * 0.12, cy + radius * 0.12],
                                 fill=c)
    light += blur(np.asarray(core).astype(np.float32) / 255, radius * 0.25) * 1.5


def rocket(light, x0, y0, x1, y1, colour, scale=1.0):
    h, w, _ = light.shape
    layer = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(layer)
    n = 40
    for i in range(n):
        f = i / (n - 1)
        x = x0 + (x1 - x0) * f
        y = y0 + (y1 - y0) * f
        k = f ** 2
        c = tuple(int(v * k) for v in colour)
        s = max(1, int(scale * (1 + 2.5 * k)))
        d.ellipse([x - s, y - s, x + s, y + s], fill=c)
    s = int(5 * scale)
    d.ellipse([x1 - s, y1 - s, x1 + s, y1 + s], fill=(255, 255, 255))
    a = np.asarray(layer).astype(np.float32) / 255
    light += a + blur(a, 5 * scale) * 1.5 + blur(a, 18 * scale)


def city(w, h, horizon, rng, glow_colours):
    """Skyline silhouette mask and window lights, both float arrays."""
    mask = Image.new("L", (w, h), 0)
    win = Image.new("RGB", (w, h), (0, 0, 0))
    dm = ImageDraw.Draw(mask)
    dw = ImageDraw.Draw(win)
    unit = h / 900
    x = -10
    while x < w:
        bw = int(rng.uniform(40, 120) * unit * 1.4)
        bh = int(rng.uniform(0.08, 0.32) * h)
        if rng.random() < 0.12:
            bh = int(rng.uniform(0.32, 0.45) * h)
        top = horizon - bh
        dm.rectangle([x, top, x + bw, horizon], fill=255)
        if rng.random() < 0.3:  # spire / antenna
            sx = x + bw // 2
            dm.rectangle([sx - max(1, int(2 * unit)), top - int(40 * unit), sx + max(1, int(2 * unit)), top], fill=255)
        # windows
        gx, gy = max(6, int(14 * unit)), max(8, int(18 * unit))
        tint = glow_colours[int(rng.integers(len(glow_colours)))]
        for wy in range(top + gy, horizon - gy, gy):
            for wx in range(x + gx // 2, x + bw - gx // 2, gx):
                if rng.random() < 0.38:
                    warm = np.array([255, 200, 120])
                    t = np.array(tint)
                    m = rng.uniform(0.0, 0.7)
                    c = (warm * (1 - m) + t * m) * rng.uniform(0.35, 0.9)
                    ww = max(2, int(5 * unit))
                    dw.rectangle([wx, wy, wx + ww, wy + max(2, int(7 * unit))], fill=tuple(int(v) for v in c))
        x += bw + int(rng.uniform(0, 12) * unit)
    return np.asarray(mask).astype(np.float32) / 255, np.asarray(win).astype(np.float32) / 255


def scene(w, h, seed, bursts, horizon_frac=0.78):
    rng_py = random.Random(seed)
    rng = np.random.default_rng(seed)
    img = sky(w, h)
    horizon = int(h * horizon_frac)
    light = np.zeros_like(img)
    scale = max(0.6, min(w, h) / 700)
    used = []
    for (fx, fy, fr, ci) in bursts:
        cx, cy, r = fx * w, fy * h, fr * min(w, h)
        firework(light, cx, cy, r, COLOURS[ci], rng_py, strands=int(70 + 40 * fr * 3), scale=scale)
        used.append(COLOURS[ci])
    # rockets climbing from the city
    for i in range(5):
        x0 = rng_py.uniform(0.05, 0.95) * w
        y1 = rng_py.uniform(0.35, 0.6) * h
        rocket(light, x0 + rng_py.uniform(-40, 40) * scale, horizon, x0, y1, COLOURS[rng_py.randrange(4)], scale)
    mask, win = city(w, h, horizon, rng, used or COLOURS)
    # the fireworks' light falls on the buildings: tint from the blurred light
    spill = blur(light, max(w, h) * 0.05) * 0.9
    building = np.array([10, 8, 22]) / 255 + spill * 0.55
    sky_lit = img + light
    out = sky_lit * (1 - mask[..., None]) + (building + win * 1.2) * mask[..., None]
    # water: mirror of the scene above the horizon, darkened and rippled
    water_h = h - horizon
    if water_h > 0:
        above = out[max(0, horizon - water_h):horizon][::-1]
        if above.shape[0] < water_h:
            pad = np.repeat(above[-1:], water_h - above.shape[0], axis=0)
            above = np.concatenate([above, pad], axis=0)
        ys = np.arange(water_h)[:, None]
        shift = (np.sin(ys * 0.21 + np.arange(w)[None, :] * 0.004) * 3 * scale).astype(int)
        cols = np.clip(np.arange(w)[None, :] + shift, 0, w - 1)
        rippled = above[ys, cols]
        # soften: reflections on moving water are smeared, more vertically than across
        rimg = to_img(rippled).resize((w, max(1, water_h // 3)), Image.BILINEAR).resize((w, water_h), Image.BILINEAR)
        rippled = np.asarray(rimg.filter(ImageFilter.GaussianBlur(1.5 * scale))).astype(np.float32) / 255
        depth = np.linspace(0.75, 0.3, water_h)[:, None, None]
        water = rippled * depth * np.array([0.8, 0.9, 1.1]) + np.array([4, 6, 20]) / 255
        out[horizon:] = water
    # gentle tone mapping
    out = 1 - np.exp(-out * 1.15)
    return np.clip(out, 0, 1)


def to_img(a):
    return Image.fromarray((np.clip(a, 0, 1) * 255).astype(np.uint8))


def draw_logo(width, with_glow=True):
    """Title on a transparent background: BOOM BOOM ROCKET over REVOLUTION."""
    big = ImageFont.truetype(FONT, int(width * 0.105))
    sub = ImageFont.truetype(SUB_FONT, int(width * 0.062))
    try:
        sub.set_variation_by_axes([800])
    except Exception:
        pass
    line1, line2 = "BOOM BOOM", "ROCKET"
    pad = int(width * 0.06)
    tmp = ImageDraw.Draw(Image.new("L", (1, 1)))
    b1 = tmp.textbbox((0, 0), line1, font=big)
    b2 = tmp.textbbox((0, 0), line2, font=big)
    b3 = tmp.textbbox((0, 0), "REVOLUTION", font=sub)
    gap = int(width * 0.012)
    h = (b1[3] - b1[1]) + (b2[3] - b2[1]) + (b3[3] - b3[1]) + gap * 4 + pad * 2
    w = width
    canvas = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    y = pad

    def centred(text, font, top, fill, stroke, stroke_fill):
        bb = tmp.textbbox((0, 0), text, font=font, stroke_width=stroke)
        x = (w - (bb[2] - bb[0])) // 2 - bb[0]
        layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        ImageDraw.Draw(layer).text((x, top - bb[1]), text, font=font, fill=fill, stroke_width=stroke,
                                   stroke_fill=stroke_fill)
        return layer, top + (bb[3] - bb[1])

    stroke = max(2, int(width * 0.006))
    l1, y = centred(line1, big, y, (255, 214, 64, 255), stroke, (120, 20, 80, 255))
    y += gap
    l2, y = centred(line2, big, y, (255, 120, 40, 255), stroke, (120, 20, 80, 255))
    y += gap * 2
    l3, y = centred("REVOLUTION", sub, y, (110, 235, 255, 255), max(1, stroke // 2), (20, 30, 90, 255))
    text = Image.alpha_composite(Image.alpha_composite(l1, l2), l3)
    if with_glow:
        glow = text.filter(ImageFilter.GaussianBlur(width * 0.012))
        a = np.asarray(glow).astype(np.float32)
        a[..., 3] *= 0.9
        glow = Image.fromarray(a.astype(np.uint8))
        text = Image.alpha_composite(glow, text)
    return text.crop(text.getbbox())


def paste_logo(base, logo_width, centre_x, top):
    logo = draw_logo(logo_width)
    base = base.convert("RGBA")
    x = int(centre_x - logo.width / 2)
    base.alpha_composite(logo, (x, int(top)))
    return base.convert("RGB"), logo.height


def main():
    import os
    os.makedirs(OUT, exist_ok=True)
    # Hero 3840x1240: wide panorama, calm space in the middle for the logo layer.
    hero = scene(3840, 1240, 11, [
        (0.12, 0.28, 0.24, 0), (0.30, 0.18, 0.17, 3), (0.70, 0.22, 0.20, 2), (0.88, 0.30, 0.25, 1),
        (0.52, 0.12, 0.10, 4), (0.04, 0.10, 0.10, 5), (0.97, 0.08, 0.09, 3)], horizon_frac=0.80)
    to_img(hero).save(f"{OUT}/hero_3840x1240.png")
    # Header 920x430: art plus logo.
    header = scene(920, 430, 21, [
        (0.12, 0.30, 0.30, 0), (0.88, 0.28, 0.32, 1), (0.50, 0.12, 0.16, 2), (0.30, 0.08, 0.12, 3),
        (0.72, 0.06, 0.12, 4)], horizon_frac=0.82)
    img, _ = paste_logo(to_img(header), 560, 460, 40)
    img.save(f"{OUT}/header_920x430.png")
    # Capsule 600x900: tall, logo near the top, fireworks around and below it.
    cap = scene(600, 900, 31, [
        (0.25, 0.55, 0.32, 0), (0.78, 0.50, 0.30, 1), (0.50, 0.40, 0.18, 2), (0.15, 0.15, 0.18, 3),
        (0.85, 0.12, 0.20, 4), (0.55, 0.66, 0.14, 3)], horizon_frac=0.80)
    img, _ = paste_logo(to_img(cap), 640, 300, 60)
    img.save(f"{OUT}/capsule_600x900.png")
    # Logo 1280 wide, transparent background.
    draw_logo(1792).save(f"{OUT}/logo_1280.png")  # trimmed to about 1280 wide
    # Square icon 512x512 (useful for store listings / launcher icon).
    sq = scene(512, 512, 41, [(0.5, 0.38, 0.42, 3), (0.2, 0.22, 0.18, 0), (0.82, 0.25, 0.2, 2)],
               horizon_frac=0.80)
    to_img(sq).save(f"{OUT}/icon_512.png")
    print("done")


if __name__ == "__main__":
    main()
