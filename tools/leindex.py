"""Function-start candidates for an LE flat image (tools/lefile.py output).

Sources:
  * Watcom prologues "push imm32; call __CHK" (__CHK = most common target),
  * off32 fixups from the data objects into the code objects that point at a plausible
    first instruction (function-pointer tables: input handlers, state tables, ...).

    python tools/leindex.py work/JS  ->  work/JS_starts.txt
"""
import csv
import json
import sys
from collections import Counter
from pathlib import Path

PROLOGUE_FIRST = {0x68, 0x53, 0x51, 0x52, 0x55, 0x56, 0x57, 0x60, 0x83, 0x8B, 0xB8}


def main():
    stem = Path(sys.argv[1])
    info = json.loads(stem.with_suffix(".json").read_text())
    img = stem.with_suffix(".bin").read_bytes()
    base = info["base"]
    code = [(o["base"], o["base"] + o["vsize"]) for o in info["objects"] if o["flags"] & 4 and o["pages"]]
    in_code = lambda a: any(lo <= a < hi for lo, hi in code)
    rd32 = lambda o: int.from_bytes(img[o:o + 4], "little", signed=True)

    hits = Counter()
    sites = []
    for i in range(len(img) - 10):
        if img[i] == 0x68 and img[i + 5] == 0xE8 and in_code(base + i):
            t = base + i + 10 + rd32(i + 6)
            hits[t] += 1
            sites.append((base + i, t))
    chk = hits.most_common(1)[0][0]
    starts = {s for s, t in sites if t == chk}
    starts.add(chk)
    starts.add(info["entry"])
    n_pro = len(starts)

    data = [(o["base"], o["base"] + o["vsize"]) for o in info["objects"] if not o["flags"] & 4]
    n_ptr = 0
    with open(str(stem) + "_fixups.csv") as f:
        for r in csv.DictReader(f):
            site, tgt = int(r["site"], 16), int(r["target"], 16)
            if r["type"] != "off32" or not in_code(tgt) or in_code(site):
                continue
            if not any(lo <= site < hi for lo, hi in data):
                continue
            prev = img[tgt - base - 1]
            if img[tgt - base] in PROLOGUE_FIRST and prev in (0xC3, 0xC2, 0x90, 0x00, 0xCC) and tgt not in starts:
                starts.add(tgt)
                n_ptr += 1
    out = Path(str(stem) + "_starts.txt")
    out.write_text("".join(f"{a:08x}\n" for a in sorted(starts)))
    print(f"__CHK {chk:#x}; {n_pro} prologue starts, {n_ptr} from data pointers -> {out}")


if __name__ == "__main__":
    main()
