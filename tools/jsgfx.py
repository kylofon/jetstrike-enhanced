"""JetStrike graphics decoder: PAX pictures, SPX sprite banks, fonts.  See port/formats/gfx.md.

    python tools/jsgfx.py all                 # everything -> work/gfx, work/spx, work/fonts
    python tools/jsgfx.py Game/GFX/CARRIER.PAX
    python tools/jsgfx.py Game/PLANE/F-16G.SPX
    python tools/jsgfx.py Game/MISC/BIGFNT.RAW
"""
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from jsunpack import unpack  # noqa: E402

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
GAME = os.path.join(ROOT, "Game")
WORK = os.path.join(ROOT, "work")

SPRITE_COLOR_OFFSET = 0x40   # byte at 0x80008; added to every sprite pixel at load
TRANSPARENT = 0x40           # '@' after the offset, i.e. raw 0


def v6to8(v):
    return (v << 2) | (v >> 4)


# ---------------------------------------------------------------- palettes
def pax_palette(path):
    """Pic_LoadPax / Pic_LoadHudPanel: first 0xC0 bytes, R<->B swapped, >>2 -> 6-bit.
    Returns 256x3 8-bit list (entries 0..63 filled)."""
    raw = bytearray(open(path, "rb").read()[:0xC0].ljust(0xC0, b"\0"))
    pal = [0] * 768
    for i in range(64):
        r, g, b = raw[i * 3 + 2], raw[i * 3 + 1], raw[i * 3]   # swapped
        pal[i * 3:i * 3 + 3] = [v6to8(r >> 2), v6to8(g >> 2), v6to8(b >> 2)]
    return pal


def sprite_palette(pal=None):
    """data/jetsprit.pal: 96 bytes of 6-bit RGB -> entries 64..95 (and copy at 96..127)."""
    pal = list(pal) if pal else [0] * 768
    raw = open(os.path.join(GAME, "DATA", "JETSPRIT.PAL"), "rb").read()[:0x60]
    for i, v in enumerate(raw):
        pal[0xC0 + i] = v6to8(v & 63)
        pal[0x120 + i] = v6to8(v & 63)
    return pal


# ---------------------------------------------------------------- PAX
def decode_pax(path):
    """-> (HxW uint8 array, palette). Pictures are linear 320-wide; height = size // 320."""
    data = unpack(open(path, "rb").read())
    h = len(data) // 320
    img = np.frombuffer(data[:h * 320], np.uint8).reshape(h, 320)
    pal = pax_palette(os.path.splitext(path)[0] + ".PAL")
    return img, pal, len(data)


def save_pax(path, outdir):
    img, pal, size = decode_pax(path)
    im = Image.fromarray(img, "P")
    im.putpalette(pal)
    name = os.path.splitext(os.path.basename(path))[0]
    out = os.path.join(outdir, name + ".png")
    im.save(out)
    print(f"{name}: {size} bytes -> 320x{img.shape[0]} (+{size - 320 * img.shape[0]} tail) {out}")
    return im


# ---------------------------------------------------------------- SPX
def parse_spx(data, offset=0):
    """Unpacked bank -> list of dict(hx, hy, w, h, pix) with pix HxW raw bytes (0 = transparent).
    Entry: u16 hotx, u16 hoty, u16 w/4, u16 h, then 4 planes of (w/4)*h bytes, row-major;
    plane p column c is pixel x = 4c + p."""
    frames, p = [], 0
    while p + 8 <= len(data):
        hx, hy, w4, h = struct.unpack_from("<4H", data, p)
        n = w4 * 4 * h
        body = np.frombuffer(data[p + 8:p + 8 + n], np.uint8)
        if len(body) < n:
            break
        pix = np.zeros((h, w4 * 4), np.uint8)
        for pl in range(4):
            pix[:, pl::4] = body[pl * w4 * h:(pl + 1) * w4 * h].reshape(h, w4)
        frames.append(dict(hx=hx, hy=hy, w=w4 * 4, h=h, pix=pix, off=p))
        p += 8 + n
    return frames, p


def frame_rgba(f, pal):
    idx = f["pix"].astype(int) + SPRITE_COLOR_OFFSET
    pal = np.array(pal, np.uint8).reshape(256, 3)
    rgba = np.zeros(idx.shape + (4,), np.uint8)
    rgba[..., :3] = pal[idx & 255]
    rgba[..., 3] = np.where(idx == TRANSPARENT, 0, 255)
    return Image.fromarray(rgba, "RGBA")


def contact_sheet(frames, pal, title, cols=10, scale=2):
    cw = max([f["w"] for f in frames] + [24]) * scale + 6
    ch = max([f["h"] for f in frames] + [8]) * scale + 16
    rows = (len(frames) + cols - 1) // cols
    sheet = Image.new("RGBA", (cols * cw, rows * ch + 14), (40, 40, 60, 255))
    d = ImageDraw.Draw(sheet)
    d.text((2, 1), title, fill=(255, 255, 0, 255))
    for i, f in enumerate(frames):
        x, y = (i % cols) * cw, (i // cols) * ch + 14
        d.rectangle([x + 1, y + 11, x + cw - 2, y + ch - 2], outline=(70, 70, 90, 255))
        d.text((x + 2, y), f"{i + 1}", fill=(200, 200, 200, 255))
        im = frame_rgba(f, pal).resize((f["w"] * scale, f["h"] * scale), Image.NEAREST)
        sheet.alpha_composite(im, (x + 3, y + 12))
        # hotspot marker
        hx, hy = x + 3 + f["hx"] * scale, y + 12 + f["hy"] * scale
        if f["hx"] <= f["w"] and f["hy"] <= f["h"]:
            d.point([(hx, hy)], fill=(255, 0, 0, 255))
    return sheet


def save_spx(path, outdir, pal=None, frames_too=False):
    data = unpack(open(path, "rb").read())
    frames, end = parse_spx(data)
    pal = pal or sprite_palette()
    name = os.path.splitext(os.path.basename(path))[0]
    sheet = contact_sheet(frames, pal, f"{name}: {len(frames)} frames")
    out = os.path.join(outdir, name + ".png")
    sheet.save(out)
    if frames_too:
        fd = os.path.join(outdir, name)
        os.makedirs(fd, exist_ok=True)
        for i, f in enumerate(frames):
            frame_rgba(f, pal).save(os.path.join(fd, f"{i + 1:03d}.png"))
    mx = max((int(f["pix"].max()) for f in frames), default=0)
    print(f"{name}: {len(data)} bytes, {len(frames)} frames, parsed {end}, max raw colour {mx} -> {out}")
    return frames


# ---------------------------------------------------------------- fonts
SMALL_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!?(),.-"   # string at 0x80bf0
BIG_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!?"          # string at 0x80c1c


def small_glyph(raw, ch):
    """Text_DrawSmall 0x124e0 addressing -> (yoffset, 16xN array)."""
    i = SMALL_CHARS.index(ch)
    base, rows, dy = i * 0x50, 5, 0
    if ch == ",":
        base, rows, dy = i * 0x50 + 0x10, 3, 3
    elif ch == "-":
        base, rows, dy = i * 0x50 - 0x60, 1, 2
    elif ch == ".":
        base, rows, dy = i * 0x50 - 0x10, 1, 4
    elif i >= 0x11:
        base += 0x10
    elif ch == "Q":
        rows = 6
    a = np.frombuffer(raw[base:base + rows * 16], np.uint8).reshape(rows, 16)
    return dy, a


def big_glyph(raw, ch):
    i = BIG_CHARS.index(ch)
    base, rows = i * 0x90, 9
    if i == 0x10:
        rows = 10
    elif i > 0x10:
        base += 0x10
    return 0, np.frombuffer(raw[base:base + rows * 16], np.uint8).reshape(rows, 16)


def digit_glyphs():
    """Text_DrawDigit 0x12831: 3x5 digits, 5 bytes per digit at 0x80019, bit2=left."""
    js = open(os.path.join(WORK, "JS.bin"), "rb").read()
    t = js[0x80019 - 0x10000:0x80019 - 0x10000 + 50]
    return [np.array([[(t[d * 5 + r] >> (2 - c)) & 1 for c in range(3)] for r in range(5)], np.uint8)
            for d in range(10)]


def save_fonts(outdir):
    for fname, chars, getg, adv in (("SMALLFNT", SMALL_CHARS, small_glyph, 2),
                                     ("BIGFNT", BIG_CHARS, big_glyph, 0)):
        raw = open(os.path.join(GAME, "MISC", fname + ".RAW"), "rb").read()
        # raw dump: whole file as 16-wide strip
        n = len(raw) // 16
        strip = np.frombuffer(raw[:n * 16], np.uint8).reshape(n, 16)
        lut = np.zeros(256, np.uint8)
        lut[1], lut[5] = 255, 90
        lut[[v for v in set(raw) if v not in (0, 1, 5)]] = 170
        Image.fromarray(lut[strip]).resize((64, n * 4), Image.NEAREST).save(
            os.path.join(outdir, fname + "_raw.png"))
        # rendered charset, glyphs advanced like the game (width = rightmost lit column)
        canvas = np.full((16, 640), 40, np.uint8)
        x = 1
        for ch in chars:
            dy, g = getg(raw, ch)
            sub = canvas[2 + dy:2 + dy + g.shape[0], x:x + 16]
            sub[g == 1] = 255
            sub[g == 5] = 0
            lit = np.nonzero(g.any(axis=0))[0]
            x += (lit.max() if len(lit) else 0) + adv
        if fname == "SMALLFNT":
            x += 4
            for g in digit_glyphs():
                canvas[2:7, x:x + 3][g == 1] = 255
                x += 4
        Image.fromarray(canvas[:, :x + 1]).resize(((x + 1) * 4, 64), Image.NEAREST).save(
            os.path.join(outdir, fname + ".png"))
        print(f"{fname}: {len(raw)} bytes, {n} rows of 16 -> {outdir}")


# ---------------------------------------------------------------- driver
def outdir(sub):
    d = os.path.join(WORK, sub)
    os.makedirs(d, exist_ok=True)
    return d


def run_all():
    gfx = outdir("gfx")
    for f in sorted(os.listdir(os.path.join(GAME, "GFX"))):
        if f.upper().endswith(".PAX"):
            save_pax(os.path.join(GAME, "GFX", f), gfx)
    spx = outdir("spx")
    save_spx(os.path.join(GAME, "DATA", "JETSTRIK.SPX"), spx, frames_too=True)
    for f in sorted(os.listdir(os.path.join(GAME, "PLANE"))):
        if f.upper().endswith(".SPX"):
            save_spx(os.path.join(GAME, "PLANE", f), spx)
    save_fonts(outdir("fonts"))


def main(argv):
    if not argv:
        print(__doc__)
        return
    for a in argv:
        if a.lower() == "all":
            run_all()
        elif a.upper().endswith(".PAX"):
            save_pax(a, outdir("gfx"))
        elif a.upper().endswith(".SPX"):
            save_spx(a, outdir("spx"), frames_too=True)
        elif a.upper().endswith(".RAW"):
            save_fonts(outdir("fonts"))
        else:
            print("unknown file type:", a)


if __name__ == "__main__":
    main(sys.argv[1:])
