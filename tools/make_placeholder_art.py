#!/usr/bin/env python3
"""Draw the placeholder loading-screen logo (assets/defjam_logo.png) and the
NRO icon (assets/icon.jpg) as plain text, so the repo ships no game art.
Swap in real art and re-run tools/make_logo.py to change them.

    python3 tools/make_placeholder_art.py     # needs Pillow
"""
import os
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
RED = (200, 24, 30)
WHITE = (240, 240, 240)


def text_block(size, lines, bg):
    im = Image.new("RGBA", size, bg)
    d = ImageDraw.Draw(im)
    y = 0
    sizes = []
    for text, pt, col in lines:
        f = ImageFont.truetype(FONT, pt)
        box = d.textbbox((0, 0), text, font=f)
        sizes.append((text, f, col, box))
        y += box[3] - box[1] + pt // 4
    y = (size[1] - y) // 2
    for text, f, col, box in sizes:
        w = box[2] - box[0]
        d.text(((size[0] - w) // 2 - box[0], y - box[1]), text, font=f, fill=col)
        y += box[3] - box[1] + f.size // 4
    return im


logo = text_block((1280, 400), [("DEF JAM", 190, RED),
                                ("FIGHT FOR NY", 96, WHITE)], (0, 0, 0, 0))
logo.save(os.path.join(ROOT, "assets", "defjam_logo.png"))
icon = text_block((256, 256), [("DEF", 84, RED), ("JAM", 84, RED),
                               ("FFNY", 40, WHITE)], (8, 10, 13, 255))
icon.convert("RGB").save(os.path.join(ROOT, "assets", "icon.jpg"), quality=92)
print("wrote assets/defjam_logo.png, assets/icon.jpg")
