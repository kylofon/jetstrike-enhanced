#!/usr/bin/env python3
"""JetStrike MISC/JETSOUND.AAF extractor.

The file is an AMOS Professional sample bank ("AmBk", bank 5, "Samples ", 28
big-endian-indexed samples of signed 8-bit PCM, header rate 10000) followed by
raw signed 8-bit data with no headers. The DOS game ignores the AMOS headers:
it reads the whole file (0x48a7a bytes) as one blob, converts it to unsigned
(byte - 0x80) and plays slices described by a hard-coded table of 35 lengths at
JS.bin 0x80434 (slice k starts at sum(len[0..k-1]), plays len[k]-1 bytes).
Sfx_Play(id, ...) uses slice id-1; Sfx_PlayVoice(voice, k, ...) uses slice k.

Playback on Sound Blaster: 19920 Hz unsigned 8-bit mono, one source byte per
output byte (step 1) for every in-game request (step 2 only for freq > 20000,
never reached in practice), so slices are written at 19920 Hz.

usage:
  jssound.py all  [aaf] [outdir]   game slices -> work/sound/NN_name.wav (NN = Sfx id, 1-based)
  jssound.py amos [aaf] [outdir]   the 28 clean AMOS samples -> work/sound/amos/NN.wav
  jssound.py list [aaf]
"""
import os, struct, sys, wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AAF = os.path.join(ROOT, 'Game/MISC/JETSOUND.AAF')
OUT = os.path.join(ROOT, 'work/sound')
RATE = 19920                       # SB_Detect(..., 0x4dd0) -> DSP rate; 398 bytes / 50 Hz tick

# JS.bin 0x80434: slice lengths (entry 35 is not a length, the table has 35 entries)
LENGTHS = [5054, 4186, 834, 3696, 8020, 18990, 2216, 2160, 3580, 13866, 7942, 8256,
           5850, 4390, 5324, 46, 6398, 16394, 4976, 4850, 46, 5904, 3174, 46, 46,
           4984, 2688, 5240, 15904, 10880, 14070, 6292, 11660, 48766, 40466]

# Names by call site (Sfx id = slice+1). See port/formats/sound_intro.md.
NAMES = {
    1: 'gun_alt_enemyfire',        # Sfx_Play(1,14000,0x10) alt gun; EnemyGround/Convoy fire (vol 0xc)
    2: 'cannon',                   # player gun, ammo type 0 (freq 20000-1000*(n%10)); Weapon_FireSpecialA/B
    3: 'gun_report',               # always paired with 1 (freq 4500)
    4: 'explosion_a',              # explosions: Rand(1)*23+4 -> 4 or 27; also when DAT_8fff8==4 (freq 25000)
    5: 'weapon_release',           # Weapon_Fire when DAT_907c4==1
    6: 'explosion_rumble',         # Explosion_Damage, big blast (freq 12000-x)
    7: 'event_81',                 # Game_Run, DAT_90034==0x81
    8: 'launch_thump',             # Weapon_FireCamera, Player_Weapons (out of ammo), FUN_3978d
    9: 'player_hit',               # Game_Run after hit palette flash
    10: 'warning_tone',            # FUN_3f945, repeats every 0x18 frames while a warning is pending
    11: 'warning_damage',          # DAT_903fc = 0xb (damage accumulator)
    12: 'warning_c',               # DAT_903fc = 0xc (FUN_3f645)
    13: 'warning_d',               # DAT_903fc = 0xd (FUN_3f8c8)
    14: 'repeat_counter',          # Game_Run, while DAT_902ec counts down
    15: 'system_damage',           # Player_DamageSystems
    16: 'stub16',                  # 46 bytes (silence/click); FUN_389b0 random 1/20
    17: 'cannon_heavy',            # player gun, ammo type 1 (freq 10000-...)
    18: 'random_event',            # Game_Run random event (freq 5500)
    19: 'gear_toggle',             # Player_Update toggles DAT_8ff14 + HUD message (freq 4500)
    20: 'debris',                  # Explosion_Damage random 1/5
    21: 'stub21',                  # 46 bytes; Weapon_FireMissile random
    22: 'mine',                    # Mines_Update random 1/50
    23: 'stub23',                  # 46 bytes; delayed follow-up to 33
    24: 'stub24',                  # 46 bytes; Airbase_Update before WeaponSelect
    25: 'proximity',               # Player_Weapons near target (once)
    26: 'enemy_air_event',         # FUN_389b0 random 1/10
    27: 'explosion_b',             # second explosion variant
    28: 'unused28',
    29: 'engine_1',                # Engine_Sfx voice 0 loop (slice 0x1c), plane class 1
    30: 'unused30',
    31: 'engine_2',                # Engine_Sfx voice 0 loop (slice 0x1e), classes 2 and 12
    32: 'engine_3',                # Engine_Sfx voice 0 loop (slice 0x1f), class 3
    33: 'unused33',
    34: 'mission_event',           # FUN_14923 (start of mission / random), freq 4100
    35: 'unused35',
}


def write_wav(path, data_signed):
    with wave.open(path, 'wb') as w:
        w.setnchannels(1); w.setsampwidth(1); w.setframerate(RATE)
        w.writeframes(bytes((b + 0x80) & 0xff for b in data_signed))


def amos_samples(d):
    assert d[:4] == b'AmBk', 'not an AMOS bank'
    ln = struct.unpack_from('>I', d, 8)[0] & 0x0fffffff
    base = 20
    n = struct.unpack_from('>H', d, base)[0]
    offs = struct.unpack_from('>%dI' % n, d, base + 2)
    out = []
    for o in offs:
        q = base + o
        rate, size = struct.unpack_from('>HI', d, q + 8)
        out.append((q + 14, size, rate, d[q:q + 8]))
    return out, 8 + ln


def slices(d):
    pos = 0
    for i, l in enumerate(LENGTHS):
        yield i + 1, pos, l - 1
        pos += l


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'all'
    aaf = sys.argv[2] if len(sys.argv) > 2 else AAF
    out = sys.argv[3] if len(sys.argv) > 3 else OUT
    d = open(aaf, 'rb').read()
    if cmd == 'list':
        sm, end = amos_samples(d)
        print('AMOS bank: %d samples, ends at 0x%x, file 0x%x' % (len(sm), end, len(d)))
        for k, (o, s, r, nm) in enumerate(sm):
            print('  amos %2d data@0x%05x len %5d rate %d %r' % (k, o, s, r, nm))
        for sid, o, n in slices(d):
            print('sfx %2d @0x%05x len %5d %.3fs %s' % (sid, o, n, n / RATE, NAMES[sid]))
    elif cmd == 'all':
        os.makedirs(out, exist_ok=True)
        for sid, o, n in slices(d):
            sd = [b - 256 if b > 127 else b for b in d[o:o + n]]
            write_wav(os.path.join(out, '%02d_%s.wav' % (sid, NAMES[sid])), sd)
        print('wrote %d slices to %s' % (len(LENGTHS), out))
    elif cmd == 'amos':
        od = os.path.join(out, 'amos'); os.makedirs(od, exist_ok=True)
        sm, _ = amos_samples(d)
        for k, (o, s, r, nm) in enumerate(sm):
            write_wav(os.path.join(od, '%02d.wav' % k), [b - 256 if b > 127 else b for b in d[o:o + s]])
        print('wrote %d AMOS samples to %s' % (len(sm), od))
    else:
        print(__doc__)


if __name__ == '__main__':
    main()
