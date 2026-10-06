#!/usr/bin/env python3
"""JetStrike Game/MAP/ extractor: tilesets, maps, parallax backdrops.

Usage:
  python tools/jsmap.py all                 # everything into work/maps/
  python tools/jsmap.py map JETMAP01        # one level render
  python tools/jsmap.py tileset JETICONS    # one tileset sheet
  python tools/jsmap.py parallax JETICONS   # DX0/DX1 x P00/P01/P10 renders

Formats are documented in port/formats/maps.md.
"""
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from jsunpack import unpack  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAPDIR = os.path.join(ROOT, 'Game', 'MAP')
OUT = os.path.join(ROOT, 'work', 'maps')

# map family (MXP prefix) -> (VAL file, tileset base) as named by DATA/M0..M3 records
FAMILIES = {
    'JETMAP': ('JETMAP1', 'JETICONS'),
    'ROCKMA': ('ROCKMAP1', 'ROCKICON'),
    'JUNGLE': ('JUNGLEMA', 'JUNGLEIC'),
    'SEAMAP': ('SEAMAP1', 'SEAICONS'),
    'CITYMA': ('CITYMAP1', 'CITYICON'),
    'ICEMAP': ('ICEMAP1', 'ICEICONS'),
    'NDMAP0': ('NDMAP1', 'ICEICONS'),
    'BONUS0': ('BONUS1', 'AEROICON'),
    'AOLIM0': ('AOLIM1', 'AEROICON'),
    'TRAINI': ('TRAINING', 'TRAINING'),
    'PRACTI': ('PRACTISE', 'ROCKICON'),
}

TRANSPARENT = 0x80  # tile pixel showing the parallax backdrop (detail on)


def rd(name):
    with open(os.path.join(MAPDIR, name), 'rb') as f:
        return f.read()


def vga_palette():
    """256x3 uint8 (8-bit) palette with tileset at 128.., backdrop at 192.."""
    return np.zeros((256, 3), np.uint8)


def load_tileset_pal(base, pal):
    p = np.frombuffer(rd(base + '.PAL'), np.uint8)[:0xC0].reshape(64, 3)  # 6-bit
    pal[128:192] = (p.astype(np.int32) * 255 // 63).astype(np.uint8)


def load_backdrop_pal(base, ext, pal):
    d = np.frombuffer(rd(base + '.' + ext), np.uint8)
    b = d[0:0xC0:4][:48]  # byte 0 of each LE dword, 8-bit -> game uses >>2
    pal[192:208] = ((b.astype(np.int32) >> 2) * 255 // 63).reshape(16, 3).astype(np.uint8)


def load_tiles(base):
    """-> uint8 array (256, 16, 16) of palette indices (planar-decoded)."""
    raw = np.frombuffer(unpack(rd(base + '.TLX')), np.uint8)
    assert raw.size == 0x10000
    t = raw.reshape(256, 4, 16, 4)          # tile, plane, row, byte-in-row
    # pixel x = byte*4 + plane
    return t.transpose(0, 2, 3, 1).reshape(256, 16, 16)


def load_mxp(name):
    m = unpack(rd(name + '.MXP'))
    w = (m[0] << 8) | m[1]
    h = (m[2] << 8) | m[3]
    grid = np.frombuffer(m, np.uint8, w * h, 4).reshape(h, w)
    return grid


def load_backdrop(base, dx):
    d = np.frombuffer(unpack(rd(base + '.' + dx)), np.uint8)
    assert d.size == 0x28000
    return ((d.astype(np.int32) - 0x40) & 0xFF).astype(np.uint8).reshape(512, 320)


def render_grid(grid, tiles, pal):
    h, w = grid.shape
    img = tiles[grid].transpose(0, 2, 1, 3).reshape(h * 16, w * 16)
    return Image.fromarray(pal[img], 'RGB')


def tileset_sheet(base):
    pal = vga_palette()
    load_tileset_pal(base, pal)
    tiles = load_tiles(base)
    img = np.zeros((16 * 17, 16 * 17), np.uint8) + 0  # 1-px grid lines (index 0 = black)
    for i in range(256):
        r, c = divmod(i, 16)
        img[r * 17:r * 17 + 16, c * 17:c * 17 + 16] = tiles[i]
    pal[0] = (40, 0, 40)
    im = Image.fromarray(pal[img], 'RGB').resize((16 * 17 * 2,) * 2, Image.NEAREST)
    im.save(os.path.join(OUT, 'tiles_%s.png' % base))


def parallax(base):
    for dx in ('DX0', 'DX1'):
        bd = load_backdrop(base, dx)
        for pe in (('P00', 'P01') if dx == 'DX0' else ('P10',)):
            pal = vga_palette()
            load_backdrop_pal(base, pe, pal)
            Image.fromarray(pal[bd], 'RGB').save(
                os.path.join(OUT, 'parallax_%s_%s_%s.png' % (base, dx, pe)))


def render_map(name, with_backdrop=None):
    fam = FAMILIES[name[:6]]
    tsb = fam[1]
    pal = vga_palette()
    load_tileset_pal(tsb, pal)
    tiles = load_tiles(tsb)
    grid = load_mxp(name)
    im = render_grid(grid, tiles, pal)  # transparent 0x80 = colour 128 (detail off)
    im.save(os.path.join(OUT, '%s.png' % name))
    if with_backdrop:
        # Detail-on look with the camera at y such that backdrop row = (camY+0x7d8)/9.25,
        # x = camX/4.1667 (mod 320). Composited per 320-px screen strip.
        load_backdrop_pal(tsb, 'P00', pal)
        bd = load_backdrop(tsb, 'DX0')
        h, w = grid.shape
        pix = tiles[grid].transpose(0, 2, 1, 3).reshape(h * 16, w * 16)
        out = pix.copy()
        # one camera per 320x176 screen-sized cell
        for cam_y in range(0, h * 16, 176):
            by = int((cam_y + 0x7d8) / 9.25)
            for sx in range(0, w * 16, 320):
                bx = int(sx / 4.1666666666) % 320
                seg = out[cam_y:cam_y + 176, sx:sx + 320]
                ys, xs = np.nonzero(seg == TRANSPARENT)
                lin = np.clip((ys + by) * 320 + bx + xs, 0, 0x28000 - 1)  # linear, as the asm reads it
                seg[ys, xs] = bd.reshape(-1)[lin]
        Image.fromarray(pal[out], 'RGB').save(os.path.join(OUT, '%s_detail.png' % name))
    return grid


def all_():
    os.makedirs(OUT, exist_ok=True)
    tsets = sorted({v[1] for v in FAMILIES.values()})
    for t in tsets:
        tileset_sheet(t)
        parallax(t)
        print('tileset', t)
    for f in sorted(os.listdir(MAPDIR)):
        if f.upper().endswith('.MXP'):
            n = f[:-4].upper()
            g = render_map(n, with_backdrop=True)
            print('map', n, g.shape[1], 'x', g.shape[0])


def main(argv):
    if not argv or argv[0] == 'all':
        all_()
        return
    os.makedirs(OUT, exist_ok=True)
    cmd, arg = argv[0], argv[1].upper()
    if cmd == 'map':
        render_map(arg, True)
    elif cmd == 'tileset':
        tileset_sheet(arg)
    elif cmd == 'parallax':
        parallax(arg)
    else:
        print(__doc__)


if __name__ == '__main__':
    main(sys.argv[1:])
