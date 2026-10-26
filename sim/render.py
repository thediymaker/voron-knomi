"""Turn the simulator's PPM frames into round PNGs plus a contact sheet."""
import glob
import os
import sys

from PIL import Image, ImageDraw

OUT = sys.argv[1] if len(sys.argv) > 1 else "sim/out"
frames = sorted(glob.glob(f"{OUT}/*.ppm"))
mask = Image.new("L", (240, 240), 0)
ImageDraw.Draw(mask).ellipse((0, 0, 239, 239), fill=255)

tiles = []
for f in frames:
    img = Image.open(f).convert("RGB")
    framed = Image.new("RGB", (260, 280), (40, 40, 44))
    framed.paste(img, (10, 10), mask)
    ImageDraw.Draw(framed).text((10, 258), os.path.basename(f)[:-4], fill=(200, 200, 200))
    framed.save(f[:-4] + ".png")
    os.remove(f)
    tiles.append(framed)

cols = 6
rows = (len(tiles) + cols - 1) // cols
sheet = Image.new("RGB", (cols * 260, rows * 280), (40, 40, 44))
for i, t in enumerate(tiles):
    sheet.paste(t, ((i % cols) * 260, (i // cols) * 280))
sheet.save(f"{OUT}/sheet.png")
print(f"{OUT}/sheet.png ({len(tiles)} frames)")
