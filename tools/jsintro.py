#!/usr/bin/env python3
"""JetStrike INTRO/ data extractor (formats used by INTRO.EXE).

usage: jsintro.py all [introdir] [outdir]      (defaults: Game/INTRO  work/intro)

Writes PNGs (with the intro palette embedded in INTRO.EXE) and WAVs:
  palette.png            the 192-colour intro palette (colours 192..255 are black)
  til_panorama.png       INTRO.TIL as the 384x256 scrolling background (tile map = identity)
  til_city.png           same, with the CITY.PTT light points plotted like the intro does
  raw/NN_WxH.png         the 97 INTRO.RAW sprites (index 0 = transparent -> alpha)
  frameN.png             FRAME1..8.RAW overlays (320 x h, 0 = transparent)
  bigfont.png            the 38 glyphs (16x9) of BIGFNT2.RAW+BIGFNT1.RAW
  aoset.png              AOSET.RAW 320x240 with AOSET.PAL (only referenced by dead code)
  sam/NN.wav             INTRO.SAM slices, signed 8-bit mono at 13333 Hz
See port/formats/sound_intro.md.
"""
import os, struct, sys, wave
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INTRO_BIN = os.path.join(ROOT, 'work/INTRO.bin')       # flat image, base 0x10000

PAL_ADDR = 0x6036c            # 0x60 bytes 4-bit (x4 -> 6-bit) + 0x1e0 bytes 8-bit (>>2 -> 6-bit)
SAM_LENS = [48973, 48973, 29747, 40575, 41493, 20997, 12401, 15917]   # INTRO.EXE 0x6070d
SAM_RATE = 13333              # SB: 40000 Hz output, step 40000/12000 = 3 source bytes per output byte
FRAME_H = [21, 40, 86, 151, 180, 203, 217, 240]
ALPHABET = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!?'


def intro_palette():
    b = open(INTRO_BIN, 'rb').read()
    raw = bytearray(b[PAL_ADDR - 0x10000:PAL_ADDR - 0x10000 + 0x240])
    for i in range(0x60):
        raw[i] = (raw[i] << 2) & 0xff
    for i in range(0x60, 0x240):
        raw[i] >>= 2
    pal6 = list(raw) + [0] * (0x300 - 0x240)
    return [min(255, v * 255 // 63) for v in pal6]


def pal_from_file(path):
    return [min(255, v * 255 // 63) for v in open(path, 'rb').read()[:768]]


def img(w, h, pix, pal, transparent=False):
    im = Image.frombytes('P', (w, h), bytes(pix))
    im.putpalette(pal)
    if transparent:
        im.info['transparency'] = 0
    return im


def read_sprites(d):
    """INTRO.RAW: 97 blocks; block = w*h bytes; first 4 bytes = BE16 w/16, BE16 h.
    The game zeroes the header and draws w*h bytes from block+4 (masked &0x1f)."""
    out, pos = [], 0
    for _ in range(97):
        w = ((d[pos] << 8) | d[pos + 1]) * 16
        h = (d[pos + 2] << 8) | d[pos + 3]
        blk = bytearray(d[pos + 4:pos + 4 + w * h])
        blk += bytes(w * h - len(blk))          # last 4 bytes come from the next (zeroed) header
        out.append((w, h, bytes(x & 0x1f for x in blk)))
        pos += w * h
    return out


def til_panorama(d):
    """INTRO.TIL: 384 tiles x 256 bytes, planar: byte = tile[plane*0x40 + row*4 + col], x = col*4+plane."""
    W, H = 24 * 16, 16 * 16
    pix = bytearray(W * H)
    for t in range(384):
        tx, ty = (t % 24) * 16, (t // 24) * 16
        base = t * 256
        for y in range(16):
            for x in range(16):
                pix[(ty + y) * W + tx + x] = d[base + (x & 3) * 0x40 + y * 4 + (x >> 2)]
    return W, H, pix


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'all'
    src = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, 'Game/INTRO')
    out = sys.argv[3] if len(sys.argv) > 3 else os.path.join(ROOT, 'work/intro')
    if cmd != 'all':
        print(__doc__); return
    f = lambda n: open(os.path.join(src, n), 'rb').read()
    pal = intro_palette()
    os.makedirs(os.path.join(out, 'raw'), exist_ok=True)
    os.makedirs(os.path.join(out, 'sam'), exist_ok=True)

    img(16, 12, [i for i in range(192)], pal).resize((256, 192), Image.NEAREST).save(os.path.join(out, 'palette.png'))

    W, H, pix = til_panorama(f('INTRO.TIL'))
    img(W, H, pix, pal).save(os.path.join(out, 'til_panorama.png'))
    # CITY.PTT: 405 x (int32 x, int32 y, int32 colour); drawn at (x, 0x9e + y - 2) only over colours 0, 0x20..0x24
    city = f('CITY.PTT')
    lit = bytearray(pix)
    for i in range(len(city) // 12):
        x, y, c = struct.unpack_from('<3i', city, i * 12)
        y = 0x9e + y - 2
        if 0 <= x < W and 0 <= y < H and lit[y * W + x] in (0, 0x20, 0x21, 0x22, 0x23, 0x24):
            lit[y * W + x] = c
    img(W, H, lit, pal).save(os.path.join(out, 'til_city.png'))

    for k, (w, h, p) in enumerate(read_sprites(f('INTRO.RAW')), 1):
        img(w, h, p, pal, True).save(os.path.join(out, 'raw', '%02d_%dx%d.png' % (k, w, h)))

    for n, h in enumerate(FRAME_H, 1):
        d = f('FRAME%d.RAW' % n)
        img(320, h, d[:320 * h], pal, True).save(os.path.join(out, 'frame%d.png' % n))

    fnt = f('BIGFNT2.RAW')[:0xc80] + f('BIGFNT1.RAW')[:0x9d0]
    sheet = Image.new('P', (len(ALPHABET) * 17, 9)); sheet.putpalette(pal)
    for k in range(len(ALPHABET)):
        o = k * 0x90 + (0x10 if k < 17 else 0x20)
        sheet.paste(img(16, 9, fnt[o:o + 0x90], pal), (k * 17, 0))
    sheet.resize((sheet.width * 3, 27), Image.NEAREST).save(os.path.join(out, 'bigfont.png'))

    img(320, 240, f('AOSET.RAW'), pal_from_file(os.path.join(src, 'AOSET.PAL'))).save(os.path.join(out, 'aoset.png'))

    sam = f('INTRO.SAM'); pos = 0
    for k, n in enumerate(SAM_LENS):
        with wave.open(os.path.join(out, 'sam', '%02d.wav' % k), 'wb') as w:
            w.setnchannels(1); w.setsampwidth(1); w.setframerate(SAM_RATE)
            w.writeframes(bytes((b + 0x80) & 0xff for b in sam[pos:pos + n]))
        pos += n
    print('wrote', out)


if __name__ == '__main__':
    main()
