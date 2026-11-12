# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#
# Makes the LiveArea background (sce_sys/livearea/contents/bg.png, 840x500) and the
# bubble preview (sce_sys/pic0.png, 960x544) from a stage photo.
#
# Source photo: "Red curtain stage" by nooooodles on Unsplash
# (https://unsplash.com/photos/red-curtain-stage-m3th3rIQ9-w), Unsplash License.
# Download it at 1920 px wide:
#   curl -L "https://images.unsplash.com/photo-1562329265-95a6d7a83440?w=1920&q=90&fm=jpg" -o stage.jpg
#
# Needs Pillow and numpy. Usage: python3 tools/make-livearea-art.py stage.jpg [repo root]
import os
import sys

import numpy as np
from PIL import Image, ImageEnhance

CROP_TOP = 0.16  # fraction of the photo height above the crop (keeps the chandelier and the curtain)


def frame(photo, w, h, dim):
    pw, ph = photo.size
    ch = round(pw * h / w)
    top = round(ph * CROP_TOP)
    top = max(0, min(top, ph - ch))
    img = photo.crop((0, top, pw, top + ch)).resize((w, h), Image.LANCZOS)
    img = ImageEnhance.Contrast(img).enhance(1.08)
    # Soft vignette, then overall dimming so LiveArea text stays readable.
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    vig = 1 - 0.45 * (((xx - w / 2) / (w * 0.7)) ** 2 + ((yy - h * 0.55) / (h * 0.9)) ** 2)
    a = np.asarray(img, np.float32) * np.clip(vig, 0.3, 1)[..., None] * dim
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))


def save_indexed(img, path):
    # The Vita's LiveArea wants 8-bit palette PNGs. Build the palette first, then map
    # with dithering; quantize() alone does not dither and leaves bands.
    pal = img.quantize(colors=256, method=Image.Quantize.MEDIANCUT)
    img.quantize(palette=pal, dither=Image.Dither.FLOYDSTEINBERG).save(path, optimize=True)
    print("wrote", path)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__ or "usage: make-livearea-art.py stage.jpg [repo root]")
    photo = Image.open(sys.argv[1]).convert("RGB")
    root = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), "..")
    save_indexed(frame(photo, 840, 500, 0.8), os.path.join(root, "sce_sys/livearea/contents/bg.png"))
    save_indexed(frame(photo, 960, 544, 1.0), os.path.join(root, "sce_sys/pic0.png"))


if __name__ == "__main__":
    main()
