# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#
# Draws the LiveArea background (sce_sys/livearea/contents/bg.png, 840x500) and the
# bubble preview (sce_sys/pic0.png, 960x544): a dark purple brick wall with worn
# texture, a soft light in the middle and darker edges. Fully procedural, so the
# output is our own art. Also the LiveArea's tap-to-start image (startup.png,
# 632x245, frame1 of template.xml, which has no gate and so no system Start
# button): the user's GH Vita lettering (tools/livearea/gh-vita-logo.png, on
# black) with the black around it made transparent, so only the lettering shows
# over the wall.
#
# Needs Pillow and numpy. Usage: python3 tools/make-livearea-art.py [repo root]
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

SS = 2  # supersampling factor
SEED = 11
PURPLE = 0.5  # strength of the purple wash over the orange-red bricks (0 = none)


def fractal(rng, h, w, base, octaves=5):
    """Value noise in [0,1]: random grids upscaled bicubically and summed."""
    out = np.zeros((h, w), np.float32)
    amp, total = 1.0, 0.0
    cell = base
    for _ in range(octaves):
        gh, gw = max(2, h // cell + 2), max(2, w // cell + 2)
        g = Image.fromarray((rng.random((gh, gw)) * 255).astype(np.uint8))
        g = np.asarray(g.resize((w, h), Image.BICUBIC), np.float32) / 255.0
        out += g * amp
        total += amp
        amp *= 0.55
        cell = max(1, cell // 2)
    return out / total


def render(w, h, dim):
    W, H = w * SS, h * SS
    rng = np.random.default_rng(SEED)
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)

    # Brick layout: running bond, rows offset by half a brick.
    bh = H / 10.5                # brick height incl. mortar
    bw = bh * 2.3                # brick width incl. mortar
    mortar = bh * 0.075
    row = np.floor(yy / bh)
    shift = (row % 2) * bw * 0.5 + (row * 37 % 5) * 0.0
    col = np.floor((xx + shift) / bw)
    fy = yy - row * bh           # position inside the brick
    fx = xx + shift - col * bw

    # Distance to the brick edge, with a little wobble so edges look hand-laid.
    wob = (fractal(rng, H, W, int(bh * 0.6), 3) - 0.5) * mortar * 1.2
    edge = np.minimum(np.minimum(fx, bw - fx), np.minimum(fy, bh - fy)) + wob
    brick_mask = np.clip((edge - mortar * 0.5) / (mortar * 0.9), 0, 1)

    # Per-brick colour: orange-red base with random tone and value.
    ids = (row * 131 + col * 17).astype(np.int64)
    uniq, inv = np.unique(ids, return_inverse=True)
    tone = rng.normal(0, 1, (len(uniq), 3)).astype(np.float32)
    tone = tone[inv.reshape(ids.shape)]
    base = np.array([132, 58, 36], np.float32)
    brick = base + tone[..., 0:1] * np.array([18, 9, 6]) + tone[..., 1:2] * np.array([4, 7, 2])

    # Surface wear: grainy noise, darker blotches and a few chipped spots.
    grain = fractal(rng, H, W, int(bh * 0.25), 5)
    blotch = fractal(rng, H, W, int(bh * 1.5), 4)
    chips = (fractal(rng, H, W, int(bh * 0.15), 3) > 0.70).astype(np.float32)
    chips = np.asarray(Image.fromarray((chips * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(SS)),
                       np.float32) / 255
    shade = 0.72 + 0.45 * grain - 0.40 * np.clip(blotch - 0.45, 0, 1) * 2 - 0.25 * chips
    brick = brick * shade[..., None]

    # Bevel: light from the top left catches the upper edges, lower edges fall dark.
    em = Image.fromarray((brick_mask * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(mortar * 1.6))
    em = np.asarray(em, np.float32) / 255
    gy, gx = np.gradient(em)
    bevel = -(gy * 0.8 + gx * 0.4) * bh * 0.45
    brick = brick * (1 + np.clip(bevel, -0.5, 0.5))[..., None]

    mortar_col = np.array([52, 34, 28], np.float32) * (0.7 + 0.5 * grain)[..., None]
    img = brick * brick_mask[..., None] + mortar_col * (1 - brick_mask[..., None])

    # Soft warm wash of light in the middle, darker toward the corners.
    nx, ny = (xx - W / 2) / (W * 0.55), (yy - H * 0.45) / (H * 0.65)
    light = np.exp(-(nx ** 2 + ny ** 2) * 1.4)
    img = img * (0.45 + 0.75 * light)[..., None] + light[..., None] * np.array([26, 12, 4])
    # Light purple wash over the warm bricks: mix in a purple-tinted copy of the
    # image's brightness.
    lum = img @ np.array([0.30, 0.55, 0.15], np.float32)
    purple = lum[..., None] * np.array([0.8, 0.6, 1.75], np.float32)
    img = img * (1 - PURPLE) + purple * PURPLE
    vig = np.clip(1 - 0.35 * (nx ** 2 + ny ** 2), 0.2, 1)
    img = img * vig[..., None] * dim

    out = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8))
    return out.resize((w, h), Image.LANCZOS)


def save_indexed(img, path):
    # The Vita's LiveArea wants 8-bit palette PNGs. Build the palette first, then map
    # with dithering; quantize() alone does not dither and leaves bands.
    # MAXCOVERAGE: MEDIANCUT left white logo entries that the dither scattered as
    # white dots over the bright middle of pic0.
    pal = img.quantize(colors=256, method=Image.Quantize.MAXCOVERAGE)
    img.quantize(palette=pal, dither=Image.Dither.FLOYDSTEINBERG).save(path, optimize=True)
    print("wrote", path)


def add_logo(img, icon_path, height, margin):
    """Puts the bubble icon's lettering in the bottom-right corner. The icon has a
    black background, so the alpha comes from the bright lettering, grown a little
    to keep its dark outline, with a soft shadow underneath."""
    icon = Image.open(icon_path).convert("RGB")
    lum = np.asarray(icon.convert("L"), np.float32)
    mask = Image.fromarray(((lum > 48) * 255).astype(np.uint8))
    x0, y0, x1, y1 = mask.getbbox()
    pad = 3
    box = (max(0, x0 - pad), max(0, y0 - pad), min(icon.width, x1 + pad), min(icon.height, y1 + pad))
    icon, mask = icon.crop(box), mask.crop(box)
    alpha = mask.filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.GaussianBlur(0.6))
    scale = height / icon.height
    size = (round(icon.width * scale), height)
    icon = icon.resize(size, Image.LANCZOS)
    alpha = alpha.resize(size, Image.LANCZOS)
    x, y = img.width - size[0] - margin, img.height - size[1] - margin
    shadow = Image.new("L", img.size, 0)
    shadow.paste(alpha, (x + 3, y + 4))
    shadow = shadow.filter(ImageFilter.GaussianBlur(6))
    out = Image.composite(Image.new("RGB", img.size, (0, 0, 0)), img, shadow.point(lambda v: v * 0.7))
    out.paste(icon, (x, y), alpha)
    return out


def make_startup(src_path, size=(632, 245), height=200):
    """The tap-to-start image: the lettering of src_path (white with black strokes, on black)
    without the black around it. The black connected to the image's border is
    the background; its rim gets alpha from brightness (anti-aliased edge), the
    black strokes inside the letters stay. A soft shadow keeps it readable on
    the wall. Scaled to `height` and centred in `size`."""
    im = np.asarray(Image.open(src_path).convert("RGB"), np.float32) / 255
    lum = im.mean(axis=2)
    m = Image.fromarray(np.pad(lum < 0.5, 1, constant_values=True).astype(np.uint8) * 255).copy()
    ImageDraw.floodfill(m, (0, 0), 128)
    bg = (np.asarray(m) == 128)[1:-1, 1:-1]
    rim = np.asarray(Image.fromarray(bg.astype(np.uint8) * 255).filter(ImageFilter.MaxFilter(5))) > 0
    alpha = np.where(rim, lum, 1.0)
    rgb = np.where(rim[..., None], 1.0, im)
    logo = Image.fromarray((np.dstack([rgb, alpha]) * 255).astype(np.uint8), "RGBA")
    shadow_a = Image.fromarray((alpha * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(5))
    shadow_a = shadow_a.filter(ImageFilter.GaussianBlur(5)).point(lambda v: int(v * 0.7))
    shadow = Image.new("RGBA", logo.size, (0, 0, 0, 0))
    shadow.putalpha(shadow_a)
    logo = Image.alpha_composite(shadow, logo)
    logo = logo.crop(logo.getchannel("A").point(lambda v: 255 if v > 40 else 0).getbbox())
    logo = logo.resize((round(logo.width * height / logo.height), height), Image.LANCZOS)
    out = Image.new("RGBA", size, (0, 0, 0, 0))
    out.alpha_composite(logo, ((size[0] - logo.width) // 2, (size[1] - logo.height) // 2))
    return out


def save_rgba(img, path):
    # A 32-bit RGBA PNG: frame images keep full alpha (the layout follows a
    # homebrew VPK's working template, 2026-11-19).
    img.save(path, optimize=True)
    print("wrote", path)


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..")
    save_indexed(render(840, 500, 0.6), os.path.join(root, "sce_sys/livearea/contents/bg.png"))
    pic = add_logo(render(960, 544, 1.0), os.path.join(root, "sce_sys/icon0.png"), 150, 28)
    save_indexed(pic, os.path.join(root, "sce_sys/pic0.png"))
    start = make_startup(os.path.join(root, "tools/livearea/gh-vita-logo.png"))
    save_rgba(start, os.path.join(root, "sce_sys/livearea/contents/startup.png"))


if __name__ == "__main__":
    main()
