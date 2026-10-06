"""LE (DOS/4GW linear executable) parser.

Reads a Watcom/DOS4GW LE executable, loads every object at its preferred base address,
applies the internal fixups and writes a flat image (base = lowest object base) that Ghidra
imports as a raw x86:LE:32 binary.

    python tools/lefile.py Game/JS_CDROM.EXE work/JS
      -> work/JS.bin      flat image, loaded at objects[0].base
         work/JS.json     header, objects, entry, fixup summary
         work/JS_fixups.csv  every fixup: site, type, target
"""
import json
import struct
import sys
from pathlib import Path

SRC_NAMES = {0: "byte", 2: "sel16", 3: "ptr16:16", 5: "off16", 6: "ptr16:32", 7: "off32", 8: "rel32"}
FLAT_SEL = 0  # selectors have no meaning in the flat image


class LEFile:
    def __init__(self, path):
        self.path = Path(path)
        d = self.data = self.path.read_bytes()
        if d[:2] != b"MZ":
            raise ValueError("not an MZ executable")
        self.le_off = struct.unpack_from("<I", d, 0x3C)[0]
        if d[self.le_off:self.le_off + 2] != b"LE":
            raise ValueError("no LE header")
        h = self.le_off
        u32 = lambda o: struct.unpack_from("<I", d, h + o)[0]
        self.num_pages = u32(0x14)
        self.eip_obj, self.eip = u32(0x18), u32(0x1C)
        self.esp_obj, self.esp = u32(0x20), u32(0x24)
        self.page_size = u32(0x28)
        self.last_page = u32(0x2C)
        obj_tab, nobj, pmap = u32(0x40), u32(0x44), u32(0x48)
        self.fix_page_tab, self.fix_rec_tab = h + u32(0x68), h + u32(0x6C)
        self.data_pages = u32(0x80)
        self.objects = []
        for i in range(nobj):
            vs, base, fl, pidx, pcnt, _ = struct.unpack_from("<6I", d, h + obj_tab + 24 * i)
            self.objects.append(dict(n=i + 1, vsize=vs, base=base, flags=fl, page_idx=pidx, pages=pcnt))
        # LE page map: 3-byte page number (big endian) + flags byte
        self.page_map = []
        for i in range(self.num_pages):
            b = d[h + pmap + 4 * i: h + pmap + 4 * i + 4]
            self.page_map.append(((b[0] << 16) | (b[1] << 8) | b[2], b[3]))
        self.fixups = []

    def page_bytes(self, page):  # page: 1-based logical page
        num, fl = self.page_map[page - 1]
        if fl != 0:
            raise NotImplementedError(f"page {page} flags {fl:#x} (iterated/invalid)")
        size = self.last_page if page == self.num_pages else self.page_size
        off = self.data_pages + (num - 1) * self.page_size
        return self.data[off:off + size]

    def load(self):
        lo = min(o["base"] for o in self.objects)
        hi = max(o["base"] + max(o["vsize"], o["pages"] * self.page_size) for o in self.objects)
        self.base = lo
        img = bytearray(hi - lo)
        page_va = {}
        for o in self.objects:
            for k in range(o["pages"]):
                p = o["page_idx"] + k
                va = o["base"] + k * self.page_size
                page_va[p] = (o, va)
                b = self.page_bytes(p)
                img[va - lo: va - lo + len(b)] = b
        self.img = img
        for p in range(1, self.num_pages + 1):
            if p in page_va:
                self._page_fixups(p, page_va[p][1])
        return img

    def _put(self, va, size, val):
        o = va - self.base
        if 0 <= o and o + size <= len(self.img):
            self.img[o:o + size] = (val & ((1 << (8 * size)) - 1)).to_bytes(size, "little")

    def _page_fixups(self, page, page_va):
        d = self.data
        start, end = struct.unpack_from("<II", d, self.fix_page_tab + 4 * (page - 1))
        p, end = self.fix_rec_tab + start, self.fix_rec_tab + end
        objs = {o["n"]: o for o in self.objects}
        while p < end:
            src, flg = d[p], d[p + 1]
            p += 2
            stype = src & 0x0F
            if src & 0x10:
                cnt = d[p]; p += 1
                srcoffs = None
            else:
                srcoffs = [struct.unpack_from("<h", d, p)[0]]; p += 2
            if flg & 3 != 0:
                raise NotImplementedError(f"import/entry fixup on page {page} flags {flg:#x}")
            if flg & 0x40:
                tobj = struct.unpack_from("<H", d, p)[0]; p += 2
            else:
                tobj = d[p]; p += 1
            toff = 0
            if stype != 2:
                if flg & 0x10:
                    toff = struct.unpack_from("<I", d, p)[0]; p += 4
                else:
                    toff = struct.unpack_from("<H", d, p)[0]; p += 2
            if srcoffs is None:
                srcoffs = list(struct.unpack_from(f"<{cnt}h", d, p)); p += 2 * cnt
            target = objs[tobj]["base"] + toff
            for so in srcoffs:
                site = page_va + so
                self.fixups.append((site, stype, tobj, toff, target))
                if stype == 7:
                    self._put(site, 4, target)
                elif stype == 5:
                    self._put(site, 2, target)
                elif stype == 8:
                    self._put(site, 4, target - (site + 4))
                elif stype == 2:
                    self._put(site, 2, FLAT_SEL)
                elif stype == 6:
                    self._put(site, 4, target); self._put(site + 4, 2, FLAT_SEL)
                elif stype == 3:
                    self._put(site, 2, target); self._put(site + 2, 2, FLAT_SEL)
                elif stype == 0:
                    self._put(site, 1, target)
                else:
                    raise NotImplementedError(f"source type {src:#x}")

    def entry_va(self):
        return self.objects[self.eip_obj - 1]["base"] + self.eip


def main():
    exe, out = sys.argv[1], Path(sys.argv[2])
    out.parent.mkdir(parents=True, exist_ok=True)
    le = LEFile(exe)
    img = le.load()
    out.with_suffix(".bin").write_bytes(img)
    counts = {}
    for f in le.fixups:
        counts[SRC_NAMES.get(f[1], f[1])] = counts.get(SRC_NAMES.get(f[1], f[1]), 0) + 1
    info = dict(file=str(exe), le_offset=le.le_off, page_size=le.page_size, base=le.base,
                size=len(img), entry=le.entry_va(),
                stack=le.objects[le.esp_obj - 1]["base"] + le.esp,
                objects=le.objects, fixups=counts)
    out.with_suffix(".json").write_text(json.dumps(info, indent=1))
    with open(str(out) + "_fixups.csv", "w") as f:
        f.write("site,type,obj,offset,target\n")
        for s, t, o, off, tg in le.fixups:
            f.write(f"{s:08x},{SRC_NAMES.get(t, t)},{o},{off:x},{tg:08x}\n")
    print(f"{exe}: base {le.base:#x} size {len(img):#x} entry {le.entry_va():#x} fixups {counts}")
    for o in le.objects:
        print(f"  obj {o['n']}: {o['base']:#010x} vsize {o['vsize']:#x} flags {o['flags']:#x} pages {o['pages']}")


if __name__ == "__main__":
    main()
