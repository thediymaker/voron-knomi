"""Collect round PNGs (as base64) for the concepts page into sim/out/gallery/images.json.

Run from the repo root after `make -C sim` has built sim/build/{sim,mock}.
Device screenshots are read from sim/out/device/*.bmp when present.
"""
import base64
import glob
import io
import json
import os
import shutil
import subprocess

from PIL import Image, ImageDraw

mask = Image.new("L", (240, 240), 0)
ImageDraw.Draw(mask).ellipse((0, 0, 239, 239), fill=255)


def encode(path):
    im = Image.open(path).convert("RGBA")
    im.putalpha(mask)
    buf = io.BytesIO()
    im.save(buf, "PNG", optimize=True)
    return base64.b64encode(buf.getvalue()).decode()


devices = {}
for f in glob.glob("sim/out/device/*.bmp"):
    devices[os.path.basename(f)] = f
saved = {}
for name, f in devices.items():
    saved[name] = open(f, "rb").read()

shutil.rmtree("sim/out", ignore_errors=True)
os.makedirs("sim/out/mock")
os.makedirs("sim/out/device")
os.makedirs("sim/out/gallery")
for name, data in saved.items():
    open(f"sim/out/device/{name}", "wb").write(data)

subprocess.run(["sim/build/mock"], check=True, stdout=subprocess.DEVNULL)
subprocess.run(["sim/build/sim"], check=True, stdout=subprocess.DEVNULL)

images = {}
for f in glob.glob("sim/out/mock/*.ppm"):
    images[os.path.basename(f)[:-4]] = encode(f)
for f in glob.glob("sim/out/*.ppm"):
    images["v2-" + os.path.basename(f)[:-4]] = encode(f)
for f in glob.glob("sim/out/device/*.bmp"):
    images["dev-" + os.path.basename(f)[:-4]] = encode(f)
json.dump(images, open("sim/out/gallery/images.json", "w"))

# README / install-site screenshots.
DOCS = {
    "face": "v2-01-idle-neon", "print-start": "v2-02-start-bed", "soak": "v2-04-soak",
    "saying": "v2-05-printing-saying", "print": "v2-06-print", "temps": "v2-07-temps",
    "done": "v2-08-done-confetti", "night": "v2-14-night-clock", "lanes": "v2-10-lanes-idle",
    "lanes-loading": "v2-11-lanes-loading", "lanes-error": "v2-12-lanes-error",
    "lanes-command": "v2-13b-lanes-T2-armed", "lanes-8": "v2-12b-lanes-8", "theme-cyan": "v2-15-theme-cyan",
    "theme-magenta": "v2-16-theme-magenta-temps", "theme-amber": "v2-17-theme-amber-print",
    "idex-concept": "d-idex", "idex-mirror-concept": "e-idex-mirror",
}
os.makedirs("docs/images", exist_ok=True)
for name, key in DOCS.items():
    open(f"docs/images/{name}.png", "wb").write(base64.b64decode(images[key]))
print(f"{len(DOCS)} screenshots in docs/images")
print(f"{len(images)} images, {sum(len(v) for v in images.values()) // 1024} KB")
