"""Compare the port's C LZW unpacker (jsenh --lzw-dump) with tools/jsunpack.py, byte for byte.

    python tools/lzw_check.py [path/to/jsenh.exe]      (default jsport/build/jsenh.exe)

Unpacks every PAX/SPX/TLX/MXP/DX0/DX1 file of Game/ with both and reports mismatches.
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from jsunpack import unpack  # noqa: E402

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
GAME = os.path.join(ROOT, "Game")
OUT = os.path.join(ROOT, "work", "lzw_c")
EXTS = (".PAX", ".SPX", ".TLX", ".MXP", ".DX0", ".DX1")


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "jsport", "build", "jsenh.exe")
    subprocess.run([exe, "--game-dir", GAME, "--lzw-dump", OUT], check=True)
    ok = bad = 0
    per_ext = {}
    for sub in sorted(os.listdir(GAME)):
        d = os.path.join(GAME, sub)
        if not os.path.isdir(d):
            continue
        for name in sorted(os.listdir(d)):
            ext = os.path.splitext(name)[1].upper()
            if ext not in EXTS:
                continue
            ref = unpack(open(os.path.join(d, name), "rb").read())
            got_path = os.path.join(OUT, "%s_%s.bin" % (sub, name))
            got = open(got_path, "rb").read() if os.path.exists(got_path) else None
            if got == ref:
                ok += 1
                per_ext[ext] = per_ext.get(ext, 0) + 1
            else:
                bad += 1
                print("MISMATCH %s/%s: python %d bytes, C %s" % (sub, name, len(ref),
                                                              "missing" if got is None else "%d bytes" % len(got)))
    print("identical: %d (%s), mismatches: %d" % (ok, ", ".join("%s %d" % kv for kv in sorted(per_ext.items())), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
