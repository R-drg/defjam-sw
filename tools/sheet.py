#!/usr/bin/env python3
"""Contact sheet of RECOMP_GL_DUMP frames: tools/sheet.py DIR OUT.png [N]"""
import glob, sys
from PIL import Image
fs = sorted(glob.glob(sys.argv[1] + "/*.bmp"))
n = int(sys.argv[3]) if len(sys.argv) > 3 else 12
sel = fs[::max(1, len(fs) // n)][:n]
if not sel:
    sys.exit("no frames in " + sys.argv[1])
w, h, cols = 320, 240, 4
sheet = Image.new("RGB", (w * cols, h * ((len(sel) + cols - 1) // cols)))
for i, f in enumerate(sel):
    im = Image.open(f).convert("RGB")
    print(f.rsplit("/", 1)[-1], im.size, "max", max(im.getextrema(), key=lambda e: e[1])[1])
    sheet.paste(im.resize((w, h)), ((i % cols) * w, (i // cols) * h))
sheet.save(sys.argv[2])
