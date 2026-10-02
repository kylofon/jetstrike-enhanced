#!/usr/bin/env python3
"""Rip JetStrike CD audio tracks from Jetstrik/CD/Jetstrike.cue + .img.

The .img is raw 2352-byte sectors (the .sub subchannel file is separate).
Audio sectors are 16-bit signed little-endian stereo PCM, 44100 Hz.
Each audio track runs from its INDEX 01 to the next track's INDEX 01 (or EOF).

usage: cdrip.py [cue] [outdir]     (defaults: Jetstrik/CD/Jetstrike.cue Game/MUSIC)
"""
import os, re, sys, struct, wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SECTOR = 2352


def parse_cue(path):
    tracks, cur, img = [], None, None
    for line in open(path, encoding='latin-1'):
        line = line.strip()
        m = re.match(r'FILE "(.+)" BINARY', line)
        if m:
            img = os.path.join(os.path.dirname(path), m.group(1))
        m = re.match(r'TRACK (\d+) (\S+)', line)
        if m:
            cur = {'num': int(m.group(1)), 'type': m.group(2)}
            tracks.append(cur)
        m = re.match(r'INDEX 0?1 (\d+):(\d+):(\d+)', line)
        if m and cur is not None:
            mm, ss, ff = map(int, m.groups())
            cur['start'] = (mm * 60 + ss) * 75 + ff
    return img, tracks


def main():
    cue = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'Jetstrik/CD/Jetstrike.cue')
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, 'Game/MUSIC')
    img, tracks = parse_cue(cue)
    total = os.path.getsize(img) // SECTOR
    os.makedirs(out, exist_ok=True)
    with open(img, 'rb') as f:
        for i, t in enumerate(tracks):
            end = tracks[i + 1]['start'] if i + 1 < len(tracks) else total
            n = end - t['start']
            if t['type'] != 'AUDIO':
                print(f"track {t['num']:2d} {t['type']} {n} sectors (data, skipped)")
                continue
            f.seek(t['start'] * SECTOR)
            data = f.read(n * SECTOR)
            fn = os.path.join(out, f"TRACK{t['num']:02d}.WAV")
            with wave.open(fn, 'wb') as w:
                w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100)
                w.writeframes(data)
            sec = n / 75
            print(f"track {t['num']:2d} {n:6d} sectors {int(sec // 60)}:{sec % 60:05.2f} -> {fn}")


if __name__ == '__main__':
    main()
