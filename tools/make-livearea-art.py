# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#
# Draws the LiveArea background (sce_sys/livearea/contents/bg.png, 840x500) and the
# bubble preview (sce_sys/pic0.png, 960x544) from code: an empty concert stage with a
# lighting truss, spotlight beams, haze, speaker stacks, a five-lane note highway and
# crowd hands. Everything is procedural, so the output is our own art.
# Needs Pillow and numpy. Usage: python3 tools/make-livearea-art.py [repo root]
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

SS = 2  # supersampling factor
FRETS = [(40, 220, 90), (235, 50, 50), (250, 215, 40), (40, 120, 245), (250, 140, 30)]


def blur(img, r):
    return img.filter(ImageFilter.GaussianBlur(r))


def add(base, layer):
    """Additive blend of two RGB images."""
    a = np.asarray(base, dtype=np.int32) + np.asarray(layer, dtype=np.int32)
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))


def over(base, rgba):
    base = base.convert("RGBA")
    base.alpha_composite(rgba)
    return base.convert("RGB")


def render(w, h, dim):
    W, H = w * SS, h * SS
    rng = np.random.default_rng(7)
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)

    # Backdrop: deep purple at the top fading to near black, plus a warm floor glow.
    t = yy / H
    bg = np.zeros((H, W, 3), np.float32)
    bg[..., 0] = 26 * (1 - t) + 8 * t
    bg[..., 1] = 10 * (1 - t) + 6 * t
    bg[..., 2] = 44 * (1 - t) + 12 * t
    vx, vy = W * 0.5, H * 0.40  # vanishing point of the highway
    glow = np.exp(-(((xx - vx) / (W * 0.30)) ** 2 + ((yy - vy) / (H * 0.18)) ** 2))
    bg += glow[..., None] * np.array([70, 30, 90], np.float32)
    img = Image.fromarray(np.clip(bg, 0, 255).astype(np.uint8))

    # Spotlight beams from the truss (additive, blurred).
    beams = Image.new("RGB", (W, H))
    d = ImageDraw.Draw(beams)
    lights = [(0.12, (120, 60, 200), 0.30), (0.30, (40, 140, 230), -0.10), (0.50, (230, 230, 255), 0.0),
              (0.70, (40, 140, 230), 0.10), (0.88, (230, 60, 140), -0.30)]
    for fx, col, lean in lights:
        x0, y0 = fx * W, H * 0.07
        x1 = x0 + lean * W + (0.5 - fx) * W * 0.6
        spread = W * 0.11
        c = tuple(int(v * 0.35) for v in col)
        d.polygon([(x0 - W * 0.006, y0), (x0 + W * 0.006, y0), (x1 + spread, H), (x1 - spread, H)], fill=c)
    beams = blur(beams, 18 * SS)
    img = add(img, beams)

    # Haze: soft noise clouds.
    n = rng.random((H // 32 + 2, W // 32 + 2)).astype(np.float32)
    haze = Image.fromarray((n * 255).astype(np.uint8)).resize((W, H), Image.BICUBIC)
    haze = np.asarray(blur(haze, 20 * SS), np.float32) / 255.0
    hz = (haze ** 2 * 28 * (1 - t * 0.6))[..., None] * np.array([0.8, 0.7, 1.0])
    img = add(img, Image.fromarray(np.clip(hz, 0, 255).astype(np.uint8)))

    # Truss across the top with lamp heads.
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    ty0, ty1 = int(H * 0.035), int(H * 0.075)
    d.rectangle([0, ty0, W, ty1], fill=(18, 16, 22, 255))
    step = int(H * 0.04)
    for x in range(-step, W + step, step):
        d.line([(x, ty0), (x + step, ty1)], fill=(52, 48, 60, 255), width=max(1, SS * 2))
        d.line([(x + step, ty0), (x, ty1)], fill=(52, 48, 60, 255), width=max(1, SS * 2))
    d.line([(0, ty0), (W, ty0)], fill=(70, 66, 80, 255), width=SS * 3)
    d.line([(0, ty1), (W, ty1)], fill=(70, 66, 80, 255), width=SS * 3)
    img = over(img, layer)
    flares = Image.new("RGB", (W, H))
    d = ImageDraw.Draw(flares)
    for fx, col, _ in lights:
        x0, y0, r = fx * W, ty1 + H * 0.012, H * 0.022
        d.rectangle([x0 - r * 0.9, ty1 - SS, x0 + r * 0.9, y0 + r * 0.3], fill=(24, 22, 28))
        d.ellipse([x0 - r * 0.7, y0 - r * 0.4, x0 + r * 0.7, y0 + r * 0.8], fill=col)
    img = add(img, blur(flares, 10 * SS))
    img = add(img, blur(flares, 2 * SS))

    # Speaker stacks on both sides of the stage.
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    cab_w, cab_h = W * 0.15, H * 0.20
    for side in (0, 1):
        for row in range(3):
            for col in range(2 if row < 2 else 1):
                ox = W * 0.015 + col * cab_w * 0.98
                if side:
                    ox = W - ox - cab_w
                oy = H * 0.92 - (row + 1) * cab_h
                d.rounded_rectangle([ox, oy, ox + cab_w, oy + cab_h], radius=SS * 6, fill=(14, 13, 18, 255),
                                    outline=(46, 42, 56, 255), width=SS * 2)
                for i in range(2):
                    for j in range(2):
                        cx = ox + cab_w * (0.28 + 0.44 * i)
                        cy = oy + cab_h * (0.28 + 0.44 * j)
                        r = cab_w * 0.17
                        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(8, 8, 11, 255), outline=(40, 38, 48, 255),
                                  width=SS * 2)
                        d.ellipse([cx - r * 0.3, cy - r * 0.3, cx + r * 0.3, cy + r * 0.3], fill=(28, 26, 34, 255))
    img = over(img, layer)

    # Note highway: five lanes converging on the vanishing point, fading with distance.
    hw = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(hw)
    half_bot, half_top = W * 0.26, W * 0.035
    top_y, bot_y = vy + H * 0.02, H * 1.0

    def lane_x(u, y):  # u in [0,1] across the highway, y in screen space
        k = (y - top_y) / (bot_y - top_y)
        half = half_top + (half_bot - half_top) * k
        return vx - half + 2 * half * u

    d.polygon([(lane_x(0, top_y), top_y), (lane_x(1, top_y), top_y), (lane_x(1, bot_y), bot_y),
               (lane_x(0, bot_y), bot_y)], fill=(10, 8, 16, 235))
    # Beat lines, spaced by perspective.
    for i in range(1, 14):
        z = i / 14.0
        y = top_y + (bot_y - top_y) * (z ** 2.2)
        a = int(40 + 120 * z)
        d.line([(lane_x(0, y), y), (lane_x(1, y), y)], fill=(150, 140, 190, a), width=max(1, int(SS * (0.5 + 2 * z))))
    hw_np = np.asarray(hw, np.float32)
    fade = np.clip((yy - top_y) / (H * 0.25), 0, 1)
    hw_np[..., 3] *= fade
    img = over(img, Image.fromarray(hw_np.astype(np.uint8)))

    # Lane strings and rails (glowing).
    strings = Image.new("RGB", (W, H))
    d = ImageDraw.Draw(strings)
    for i, col in enumerate(FRETS):
        u = (i + 0.5) / 5
        c = tuple(int(v * 0.55) for v in col)
        d.line([(lane_x(u, top_y), top_y), (lane_x(u, bot_y), bot_y)], fill=c, width=SS * 2)
    for u in (0.0, 1.0):
        d.line([(lane_x(u, top_y), top_y), (lane_x(u, bot_y), bot_y)], fill=(170, 150, 220), width=SS * 3)
    s_np = np.asarray(strings, np.float32) * np.clip((yy - top_y) / (H * 0.3), 0, 1)[..., None]
    strings = Image.fromarray(s_np.astype(np.uint8))
    img = add(img, blur(strings, 6 * SS))
    img = add(img, strings)

    # Notes: a few gems coming down the lanes, plus the strike line.
    gems = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    gl = Image.new("RGB", (W, H))
    d, dg = ImageDraw.Draw(gems), ImageDraw.Draw(gl)
    notes = [(0, 0.76), (2, 0.62), (1, 0.47), (3, 0.36), (4, 0.55), (2, 0.28), (0, 0.22), (4, 0.18), (1, 0.70)]
    strike_y = top_y + (bot_y - top_y) * 0.80
    for lane, z in sorted(notes, key=lambda n: n[1]):
        y = top_y + (bot_y - top_y) * (z ** 1.6)
        k = (y - top_y) / (bot_y - top_y)
        lane_w = (half_top + (half_bot - half_top) * k) * 2 / 5
        cx, rx, ry = lane_x((lane + 0.5) / 5, y), lane_w * 0.36, lane_w * 0.17
        col = FRETS[lane]
        a = int(255 * min(1, 0.55 + k * 1.5))
        d.ellipse([cx - rx, y - ry + ry * 0.35, cx + rx, y + ry + ry * 0.35], fill=(10, 10, 14, a))
        d.ellipse([cx - rx, y - ry, cx + rx, y + ry], fill=col + (a,), outline=(255, 255, 255, a), width=SS * 2)
        d.ellipse([cx - rx * 0.28, y - ry * 0.32, cx + rx * 0.28, y + ry * 0.22], fill=(255, 255, 255, int(a * 0.55)))
        dg.ellipse([cx - rx * 1.3, y - ry * 1.6, cx + rx * 1.3, y + ry * 1.6], fill=tuple(int(v * k * 0.8) for v in col))
    img = add(img, blur(gl, 10 * SS))
    img = over(img, gems)
    ring = Image.new("RGB", (W, H))
    d = ImageDraw.Draw(ring)
    k = (strike_y - top_y) / (bot_y - top_y)
    lane_w = (half_top + (half_bot - half_top) * k) * 2 / 5
    for i, col in enumerate(FRETS):
        cx = lane_x((i + 0.5) / 5, strike_y)
        rx, ry = lane_w * 0.38, lane_w * 0.18
        d.ellipse([cx - rx, strike_y - ry, cx + rx, strike_y + ry], outline=col, width=SS * 4)
    img = add(img, blur(ring, 8 * SS))
    img = add(img, ring)

    # Crowd hands and heads along the bottom edge, rim-lit by the stage.
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    rim = Image.new("RGB", (W, H))
    dr = ImageDraw.Draw(rim)
    x = -W * 0.02
    while x < W * 1.02:
        hr = H * rng.uniform(0.032, 0.045)
        hy = H - hr * rng.uniform(0.0, 0.5)
        d.ellipse([x - hr, hy - hr, x + hr, hy + hr * 1.2], fill=(5, 4, 8, 255))
        dr.arc([x - hr, hy - hr, x + hr, hy + hr], 200, 340, fill=(90, 60, 130), width=SS * 2)
        if rng.random() < 0.38:  # raised arm with a fist or horns
            ang = math.radians(rng.uniform(-25, 25))
            length = H * rng.uniform(0.10, 0.17)
            bx, by = x + hr * rng.choice([-0.9, 0.9]), hy + hr * 0.6
            ex, ey = bx + math.sin(ang) * length, by - math.cos(ang) * length
            d.line([(bx, by), (ex, ey)], fill=(5, 4, 8, 255), width=int(hr * 0.55))
            fr = hr * 0.38
            d.ellipse([ex - fr, ey - fr, ex + fr, ey + fr], fill=(5, 4, 8, 255))
            if rng.random() < 0.6:
                for s in (-1, 1):
                    d.line([(ex + s * fr * 0.5, ey), (ex + s * fr * 0.8, ey - fr * 2.0)], fill=(5, 4, 8, 255),
                           width=int(fr * 0.45))
            dr.line([(bx, by), (ex, ey)], fill=(60, 40, 90), width=SS)
        x += hr * rng.uniform(1.3, 1.9)
    img = add(img, blur(rim, SS))
    img = over(img, layer)

    # Vignette and overall dimming so the LiveArea text stays readable.
    vig = 1 - 0.55 * (((xx - W / 2) / (W * 0.62)) ** 2 + ((yy - H * 0.45) / (H * 0.75)) ** 2)
    a = np.asarray(img, np.float32) * np.clip(vig, 0.25, 1)[..., None] * dim
    img = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))
    return img.resize((w, h), Image.LANCZOS)


def save_indexed(img, path):
    # The Vita's LiveArea wants 8-bit palette PNGs.
    # Build the palette first, then map with dithering; quantize() alone does not
    # dither and leaves visible bands in the light beams.
    pal = img.quantize(colors=256, method=Image.Quantize.MEDIANCUT)
    img.quantize(palette=pal, dither=Image.Dither.FLOYDSTEINBERG).save(path, optimize=True)
    print("wrote", path)


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..")
    save_indexed(render(840, 500, 0.85), os.path.join(root, "sce_sys/livearea/contents/bg.png"))
    save_indexed(render(960, 544, 1.0), os.path.join(root, "sce_sys/pic0.png"))


if __name__ == "__main__":
    main()
