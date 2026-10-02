"""Writes app.ico: the jet drawn in icon.cpp, for Explorer. Needs Pillow.

    python make_icon.py
"""
from pathlib import Path

from PIL import Image

ROWS = [
    "................",
    ".......##.......",
    ".......##.......",
    "......#cc#......",
    "......#cc#......",
    "......####......",
    ".....######.....",
    "...##########...",
    ".##############.",
    ".WWWWWW##WWWWWW.",
    ".......##.......",
    ".......##.......",
    ".....######.....",
    "....##WWWW##....",
    ".......oo.......",
    ".......oo.......",
]
COLOURS = {"#": 0xAAAAAA, "W": 0x555555, "c": 0x55FFFF, "o": 0xFFAA00, ".": 0x0055AA}

tile = Image.new("RGBA", (16, 16))
for y, row in enumerate(ROWS):
    for x, c in enumerate(row):
        rgb = COLOURS[c]
        tile.putpixel((x, y), (rgb >> 16, (rgb >> 8) & 0xFF, rgb & 0xFF, 255))

sizes = [16, 20, 24, 32, 48, 64, 256]
big = tile.resize((256, 256), Image.NEAREST)
big.save(Path(__file__).with_name("app.ico"), sizes=[(s, s) for s in sizes])
