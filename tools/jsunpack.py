"""JetStrike LZW unpacker: a literal port of the asm in LE object 2 (JS_CDROM.EXE 0x50000).

Packed file: u32 LE unpacked size, then an MSB-first bit stream of 9..12-bit codes.
  * dictionary entry = (prefix code, char); 0..255 are the bytes; `nxt` (last used entry) starts at 0xFF
  * code == (1 << bits) - 1 is an escape: bits += 1 (and if nxt == that code - 1, nxt += 1), read again
  * a new entry skips the index (1 << bits) - 1; the dictionary stops growing at 0xFFF
  * output lags one code: each step reads a code, adds an entry, then writes the string of the
    previous code; it stops once `size` bytes are written (the last code read is never written)

    python tools/jsunpack.py in.pax out.bin
"""
import sys


class _Bits:
    def __init__(self, data, pos):
        self.d, self.p, self.acc, self.n = data, pos, 0, 0

    def get(self, bits):
        while self.n < bits:
            b = self.d[self.p] if self.p < len(self.d) else 0
            self.p += 1
            self.acc = (self.acc << 8) | b
            self.n += 8
        self.n -= bits
        v = (self.acc >> self.n) & ((1 << bits) - 1)
        self.acc &= (1 << self.n) - 1
        return v


def unpack(data):
    size = int.from_bytes(data[:4], "little")
    prefix = list(range(256)) + [0] * (0x1000 - 256)
    char = list(range(256)) + [0] * (0x1000 - 256)
    prefix[:256] = [0] * 256
    state = {"bits": 9, "nxt": 0xFF}
    br = _Bits(data, 4)

    def code():
        while True:
            c = br.get(state["bits"])
            esc = (1 << state["bits"]) - 1
            if c != esc:
                return c
            state["bits"] += 1
            if esc - 1 == state["nxt"]:
                state["nxt"] += 1

    def add(pfx, ch):
        if state["nxt"] >= 0xFFF:
            return
        state["nxt"] += 1
        if state["nxt"] == (1 << state["bits"]) - 1:
            if state["nxt"] == 0xFFF:
                return
            state["nxt"] += 1
        prefix[state["nxt"]], char[state["nxt"]] = pfx, ch

    def root(c):
        while c >= 0x100:
            c = prefix[c]
        return c

    out = bytearray()
    old = code()
    while len(out) < size:
        c = code()
        add(old, root(old) if c > state["nxt"] else root(c))
        s, x = [], old
        while True:
            s.append(char[x & 0xFFF])
            if x < 0x100:
                break
            x = prefix[x]
        out.extend(reversed(s))
        old = c
    return bytes(out[:size]) if len(out) > size else bytes(out)


def is_packed(data):
    return len(data) >= 4 and 0 < int.from_bytes(data[:4], "little") < 0x400000


if __name__ == "__main__":
    src = open(sys.argv[1], "rb").read()
    dst = unpack(src)
    open(sys.argv[2], "wb").write(dst)
    print(f"{sys.argv[1]}: {len(src)} -> {len(dst)}")
