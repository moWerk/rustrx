#!/usr/bin/env python3
"""Stitch phone screenshots side by side into one wide image for the forum,
which puts margins between separate pictures.
usage: stitch.py OUT.png SHOT.png [SHOT.png ...]   (3 or 4 shots work best)"""
import sys
from PIL import Image

out, shots = sys.argv[1], sys.argv[2:]
images = [Image.open(s).convert("RGB") for s in shots]
height = 1200                                   # forum-friendly, keeps text legible
scaled = [im.resize((round(im.width * height / im.height), height), Image.LANCZOS) for im in images]
gap = 24
width = sum(im.width for im in scaled) + gap * (len(scaled) - 1)
canvas = Image.new("RGB", (width, height), (18, 18, 22))
x = 0
for im in scaled:
    canvas.paste(im, (x, 0))
    x += im.width + gap
canvas.save(out, optimize=True)
print(out, canvas.size)
