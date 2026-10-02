"""JetStrike DATA/ file decoder (JS_CDROM.EXE 1994).  See port/formats/data.md.

    python tools/jsdata.py all  [game_dir] [out_dir]    dump everything (default Game/ -> work/data/)
    python tools/jsdata.py save <js_save.00N>           decode a save game
    python tools/jsdata.py mission <M0..M3> <index>     print one mission record

Loader semantics follow the Watcom code: text files are opened "r" (CR/LF -> LF), read with
fscanf("%d") / fscanf("%d\\n") / fgets(buf, n); the binary ones "rb".  Mission/GENDAT*/BERTHA/MISC.Z
values are big-endian (Amiga origin); JS.CFG and the save files are little-endian (DOS native).
"""
import csv
import os
import struct
import sys

# --------------------------------------------------------------------------------------------
# text stream emulating the Watcom stdio calls the game uses


class TextStream:
    def __init__(self, raw):
        self.s = raw.replace(b'\r\n', b'\n').decode('latin1')
        self.p = 0

    def eof(self):
        return self.p >= len(self.s)

    def _skipws(self):
        while self.p < len(self.s) and self.s[self.p] in ' \t\n\r\f\v':
            self.p += 1

    def scanf_d(self, eat_trailing_ws=False):
        """fscanf("%d") (or "%d\\n" when eat_trailing_ws)."""
        self._skipws()
        q = self.p
        if q < len(self.s) and self.s[q] in '+-':
            q += 1
        while q < len(self.s) and self.s[q].isdigit():
            q += 1
        txt = self.s[self.p:q]
        self.p = q
        if eat_trailing_ws:
            self._skipws()
        return int(txt) if txt not in ('', '+', '-') else 0

    def scanf_s(self):
        self._skipws()
        q = self.p
        while q < len(self.s) and self.s[q] not in ' \t\n\r\f\v':
            q += 1
        txt = self.s[self.p:q]
        self.p = q
        return txt

    def fgets(self, n):
        """fgets(buf, n): up to n-1 chars, stops after '\\n' (kept)."""
        q = self.p
        lim = self.p + n - 1
        while q < len(self.s) and q < lim:
            q += 1
            if self.s[q - 1] == '\n':
                break
        txt = self.s[self.p:q]
        self.p = q
        return txt


def chop(s):
    """the game's `buf[strlen(buf)-1] = 0` after fgets"""
    return s[:-1] if s else s


def be16(b, o):
    return struct.unpack_from('>H', b, o)[0]


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


# --------------------------------------------------------------------------------------------
# mission records (DATA/M0..M3)

MISSION_REC = 0x1c2
PARAM_NAMES = [
    'p00_land_obj',        # nonzero: "return and land" objective; /100==1 final-mission flag; ==2 -> lives=0
    'p01_recon_col',       # recon photo: fly over column p01 (+-1) ...
    'p02_recon_row',       # ... below row p02 (y >= p02*16)
    'p03_target_x0',       # target column range start | 2001..4999: steal-aircraft col+2000 | >=5000: tile id+5000
    'p04_target_x1',       # target column range end   | (>=5000 form) number of consecutive tile ids
    'p05_air_kills',       # enemy aircraft to shoot down (decremented per kill)
    'p06_convoy_kills',    # convoy vehicles to destroy; >1000: escort mode, n=p06%1000 must survive
    'p07_targets_req',     # number of destroyed target tiles required (also Bertha damage threshold)
    'p08_enemy_air',       # enemy aircraft: (ENEMIES record-1)*100 + count
    'p09_convoy_count',    # convoy vehicle count; >=1000: convoy keeps shuttling (flag) count=p09%1000
    'p10_convoy_col',      # convoy start column; >=1000: n=p10/1000 ground units at p03 column
    'p11_enemy_spawn_col',  # enemy aircraft spawn column (+-600px random)
    'p12_enemy_wake_col',  # enemy aircraft stay passive until player passes this column / within 2000px
    'p13_convoy_kind',     # hi byte: truck<'a'+hi>.spx bank & behaviour (1 shuttle, 2 water); lo: hit-point multiplier
    'p14_pickup_col',      # pickup/agent object column (radar marker), 0 = none
    'p15_convoy_types_a',  # 4 nibbles: vehicle type (GENDAT2 row) of convoy vehicles 0..3 (LSB first)
    'p16_convoy_types_b',  # 4 nibbles: vehicle types 4..7
    'p17_convoy_row',      # convoy ground row (<0x46: row -> row*16-1; else pixel y)
    'p18_convoy_spacing',  # pixel distance between convoy vehicles
    'p19_bertha',          # Bertha overlay: letter index*1000 + column (DATA/BERTHA<'A'+n>)
    'p20_ceiling_row',     # flying above this row speeds up enemy-bomber raids; target column for tile 2/0xa0 check
    'p21_target_marker',   # nonzero: guided weapons/arrow aim at the middle of p03..p04
    'p22_target_flag',     # ==1: special hit rule on p03..p04 targets (FUN_00018bc8 / FUN_000438f2)
    'p23_convoy_flag',     # convoy terrain/turn flag (Convoy_Update)
    'p24_pickup_sprite',   # pickup sprite id (%500); 500..999: carried by convoy vehicle p14-1; >1000: dropped when convoy stops; 0xca/0xac agent
    'p25_weather',         # 0: 5% random fog, 1: night (no parallax, night palette), 2: fog
    'p26_enemy_base_col',  # enemy airfield column (bombers/scramble spawn x = col*16-0x40); 0 = none (<2000)
    'p27_enemy_base_row',  # enemy airfield row (>63: pixels, >>4)
    'p28_bertha_row',      # Bertha overlay top row
    'p29_misc',            # /1000 -> 0x90148 (random event chance), %1000 -> 0x8deb0/0x8deb4
]
AERO_NOTES = ('M3 (Aerolimits) reinterprets: p03 landing pad column (+-5, tiles 0x3e/0x3f stamped on rows p07/p08), '
              'p04,p05 crate x,y (tiles; p05=0 -> falls from sky), p06 -> 0x9091c, p24 -> crate sprite, '
              'p15..p22 = 4 gate (x,y) tile pairs, then zeroed')


def parse_mission(rec):
    d = {}
    d['briefing'] = rec[0:320].decode('latin1').rstrip(' \0')
    d['map'] = rec[320:340].decode('latin1').rstrip(' \0')
    d['tileset'] = rec[340:360].decode('latin1').rstrip(' \0')
    d['params'] = list(struct.unpack('>30H', rec[360:420]))
    d['asc'] = rec[420:440].decode('latin1').rstrip(' \0')
    d['default_weapons'] = list(struct.unpack('>4H', rec[440:448]))
    d['pad'] = rec[448:450]
    return d


def load_missions(path):
    raw = open(path, 'rb').read()
    return [parse_mission(raw[i:i + MISSION_REC]) for i in range(0, len(raw) - MISSION_REC + 1, MISSION_REC)]


# --------------------------------------------------------------------------------------------
# WEAPONS.DAT / JETS.N / HUDTEXT / GENDATAD / SARCASM / L1L2 / ENEMIES

WEAPON_FIELDS = ['fire_kind', 'arm_delay', 'motor', 'flight_kind', 'detonate_eol', 'w5_flag',
                 'blast_a', 'blast_b', 'thrust', 'icon_sprite', 'per_rack', 'weight',
                 'rack_mult', 'desc', 'stock', 'resupply10']


def load_weapons(path):
    ts = TextStream(open(path, 'rb').read())
    count = ts.scanf_d(True)
    out = []
    for i in range(count + 2):  # loop is `i <= count + 1`
        if ts.eof():
            break
        w = {'index': i, 'name': chop(ts.fgets(0x28))}
        kinds = [ts.scanf_d() for _ in range(6)]
        w.update(zip(WEAPON_FIELDS[0:6], kinds))
        vals = [ts.scanf_d() for _ in range(6)]
        w.update(zip(WEAPON_FIELDS[6:12], vals))
        w['rack_mult'] = ts.scanf_d(True)
        w['desc'] = chop(ts.fgets(0xa0))
        w['stock'] = ts.scanf_d()
        w['resupply10'] = ts.scanf_d(True)
        out.append(w)
    return count, out


def load_jets(path):
    ts = TextStream(open(path, 'rb').read())
    count = ts.scanf_d(True)
    return count, [chop(ts.fgets(0x50)) for _ in range(count + 1)]


def load_lines(path, n=None, size=0x50, strip=True):
    ts = TextStream(open(path, 'rb').read())
    out = []
    while not ts.eof() and (n is None or len(out) < n):
        s = ts.fgets(size)
        out.append(chop(s) if strip else s)
    return out


ENEMY_FIELDS = ['skill', 'gun_damage', 'missiles', 'bombs', 'bomb_weapon', 'bomb_ref16', 'spx_letter']


def load_enemies(path):
    raw = open(path, 'rb').read()
    return [dict(zip(ENEMY_FIELDS, raw[i:i + 7])) for i in range(0, len(raw) - 6, 7)]


# --------------------------------------------------------------------------------------------
# MISC (60 x 0xdc text records) and MISC.Z (60 x 300 bytes, 121 BE16 stats)

MISC_KEY = b'Mixamatosis is Fun !'


def load_misc(path):
    raw = open(path, 'rb').read()
    out = []
    for i in range(len(raw) // 0xdc):
        r = raw[i * 0xdc:(i + 1) * 0xdc]
        code = bytes(((r[0xa0 + j] - 1) & 0xff) ^ MISC_KEY[j] for j in range(20))
        out.append({'name': r[0:20].decode('latin1').rstrip(),
                    'abk': r[20:40].decode('latin1').rstrip(),
                    'desc': r[40:160].decode('latin1').rstrip(),
                    'code': code.decode('latin1').rstrip(' \0'),
                    'tail': r[0xb4:].decode('latin1').rstrip()})
    return out


# MISC.Z word index -> (destination global set in PlaneSelect_Screen 0x245a9, name, scale)
PLANE_STAT = {
    62: ('0x90060', 'ps62', ''), 63: ('0x90258', 'ps63', ''), 64: ('0x90148', 'event_chance', ''),
    65: ('0x90838', 'ps65', ''), 66: ('0x90358', 'ps66', ''), 67: ('0x9008c', 'ps67', ''),
    68: ('0x90508', 'gun_a', ''), 69: ('0x905a4', 'gun_b', ''), 70: ('0x90560', 'gun_c', ''),
    71: ('0x90564', 'gun_d', ''), 72: ('0x90850', 'gun_power', ''), 73: ('0x90188', 'throttle_down', ''),
    74: ('0x90450', 'ps74', ''), 75: ('0x8ff9c', 'ps75', ''), 76: ('0x8fee0', 'landing_flag', ''),
    77: ('0x9045c', 'plane_class', ''), 78: ('0x908d0', 'rotate_flag', ''), 79: ('0x906b0', 'gun_weapon', '0xff=none'),
    80: ('0x90090', 'perf_a', '/100'), 81: ('0x90780', 'perf_b', '/100'), 82: ('0x903b0', 'ps82', ''),
    83: ('0x90674', 'perf_c', '/100 (0 -> hover flag 0x90594)'),
    84: ('0x907d8', 'base_a', ''), 85: ('0x907dc', 'base_b', ''), 86: ('0x907a0', 'perf_d', '/100'),
    87: ('0x90954', 'ps87', '>10 -> -1'), 88: ('0x90168', 'rotate_rate', ''), 89: ('0x905dc', 'ps89', ''),
    90: ('0x8ff14', 'ps90', ''), 91: ('0x904cc', 'is_heli', ''), 92: ('0x90610', 'ps92', ''),
    93: ('0x903d4', 'ps93', ''), 94: ('0x90558/0x901f8', 'gun_mod', '%10 / /10'),
    95: ('0x90a34', 'ps95', ''), 96: ('0x90958', 'afterburner', ''), 97: ('0x9090c', 'ps97', ''),
    98: ('0x90a48', 'ps98', ''), 99: ('0x90a4c', 'ps99', ''), 100: ('0x8ff0c', 'ps100', ''),
    101: ('0x8fc10', 'racks_left', ''), 102: ('0x8fc14', 'racks_right', ''),
    103: ('0x90500', 'max_load', ''), 104: ('0x8fbc0', 'max_load_left', ''), 105: ('0x8fbc4', 'max_load_right', ''),
    106: ('0x90388', 'max_weight', ''), 107: ('0x90788', 'pod_weight', ''), 108: ('0x91834', 'armour', '+ bonus 0x909e0'),
    109: ('0x901dc', 'eject_dx', 'signed'), 110: ('0x901e0', 'eject_dy', 'signed'), 111: ('0x906d0', 'engine_kind', ''),
    112: ('0x903c0', 'ammo', ''), 113: ('0x8ff20', 'ps113', ''), 114: ('0x90644', 'ps114', ''),
    115: ('0x9006c', 'ps115', ''), 116: ('0x8ffe4', 'ps116', ''), 117: ('0x8ffe8', 'ps117', ''),
    118: ('0x90130', 'ps118', ''), 119: ('0x90844', 'ps119', ''), 120: ('0x90070', 'ps120', ''),
}


def load_miscz(path):
    raw = open(path, 'rb').read()
    out = []
    for i in range(len(raw) // 300):
        w = [be16(raw, i * 300 + 2 * k) for k in range(121)]
        out.append([s16(v) if k > 0x4f else v for k, v in enumerate(w)])
    return out


# --------------------------------------------------------------------------------------------
# GENDATA / GENDAT2 (binary BE16) and GENDAT3 (text)


def load_gendata(path):
    raw = open(path, 'rb').read()
    w = [be16(raw, i) for i in range(0, len(raw) - 1, 2)]
    p = 0
    rows, row = [], []
    while w[p] != 0xffff:
        if w[p] == 0x23:
            rows.append(row)
            row = []
        else:
            row.append(w[p])
        p += 1
    if row:
        rows.append(row)
    p += 1
    mat = w[p:p + 9]; p += 9
    obj_campaign = w[p:p + 26]; p += 26
    obj_aero = w[p:p + 26]; p += 26
    proj_sprite = w[p:p + 16]; p += 16
    misc6 = w[p:p + 6]; p += 6
    return {'colour_rows': rows, 'mat3x3': mat, 'param_is_bonus_campaign': obj_campaign,
            'param_is_bonus_aero': obj_aero, 'proj_sprite_base': proj_sprite,
            'ignored6': misc6, 'trailing': w[p:]}


def load_gendat2(path):
    raw = open(path, 'rb').read()
    w = [be16(raw, i) for i in range(0, len(raw) - 1, 2)]
    veh = [tuple(w[i * 3:i * 3 + 3]) for i in range(16)]
    p = 48
    skipped = w[p]
    p += 1
    t8 = w[p:p + 8]; p += 8
    t10 = w[p:p + 10]; p += 10
    t16 = w[p:p + 16]; p += 16
    t7 = w[p:p + 7]; p += 7
    return {'vehicles': veh, 'skipped': skipped, 'name_entry8': t8, 'bonus_sprites10': t10,
            'table16': t16, 'radar_scale7': t7}


GENDAT3_LAYOUT = [  # (count, [destination arrays]) -- pairs are interleaved per index
    (5, ['0x8d8c0', '0x8d8d4']), (18, ['0x8d818', '0x8d860']), (6, ['0x8d8a8']),
    ('lines', 4), (4, ['0x8d9f8 (aero rounds)']), (27, ['0x90b48[1..27]']), (4, ['0x8da88']),
    (3, ['0x90cd8', '0x90be4']), (6, ['0x90cf8[1..6]']), (5, ['0x90ce4']),
    (5, ['0x90cc4', '0x90c84']), (8, ['0x8deb8', '0x8e310']), (7, ['0x8ded8', '0x8df00']),
    ('music', 7),
]


def load_gendat3(path):
    ts = TextStream(open(path, 'rb').read())
    out = []
    for item in GENDAT3_LAYOUT:
        if item[0] == 'lines':
            out.append(('paths', [chop(ts.fgets(0x50)) for _ in range(item[1])]))
        elif item[0] == 'music':
            mus = []
            for _ in range(item[1]):
                t = chop(ts.fgets(0x50)); a = chop(ts.fgets(0x50)); n = ts.scanf_d(True)
                mus.append((t, a, n))
            out.append(('music', mus))
        else:
            n, dst = item
            vals = [[ts.scanf_d(True) for _ in dst] for _ in range(n)]
            out.append((' / '.join(dst), vals))
    return out


def load_bertha(path):
    raw = open(path, 'rb').read()
    w, h = be16(raw, 0), be16(raw, 2)
    return w, h, [list(raw[4 + y * w:4 + (y + 1) * w]) for y in range(h)]


def parse_asc(path):
    ts = TextStream(open(path, 'rb').read())
    pic = ts.scanf_s()
    x, y, z = ts.scanf_d(), ts.scanf_d(), ts.scanf_d()
    text = ts.s[ts.p:].lstrip('\n')
    return {'picture': pic, 'pax': pic[:-4] + '.pax' if len(pic) > 4 else pic,
            'text_x': x, 'text_y': y, 'third': z, 'text': text}


# --------------------------------------------------------------------------------------------
# JS.CFG and saves

CFG_WORDS = ['cd_music', 'sfx', 'key_E', 'key_Enter', 'key_A', 'key_U', 'key_L', 'key_D', 'key_P', 'key_Tab',
             'key_B', 'key_Esc', 'key_Up', 'key_Down', 'key_Left', 'key_Right', 'key_LShift', 'key_RShift',
             'joy_on', 'joy_cal0', 'joy_cal1', 'joy_cal2', 'joy_cal3', 'detail', 'key_fire', 'key_Alt',
             'key_Ctrl', 'key_KPstar', 'key_Backspace', 'sb_irq']


def parse_cfg(raw):
    raw = raw[:0x48].ljust(0x48, b'\0')
    w = struct.unpack('<36H', raw)
    return list(zip(CFG_WORDS + ['unused%d' % i for i in range(6)], w))


SAVE_BYTES = ['lives', 'mission', 'kills', 'bonus_autoeject', 'bonus_extinguisher', 'bonus_ammo',
              'flag_90078', 'bonus_ecm', 'bonus_armour', 'bonus_firepower', 'bonus_9_flag']
SAVE_DWORDS = ['next_extra_threshold', 'next_bonus_score', 'bonus_score_step', 'score']


def parse_save(raw):
    d = dict(zip(SAVE_BYTES, raw[0:11]))
    d.update(zip(SAVE_DWORDS, struct.unpack_from('<4i', raw, 11)))
    d['plane_used'] = list(struct.unpack_from('<71h', raw, 27))
    d['weapon_stock'] = list(struct.unpack_from('<71h', raw, 27 + 142))
    return d


# --------------------------------------------------------------------------------------------


def dump_all(game, out):
    data = os.path.join(game, 'DATA')
    os.makedirs(out, exist_ok=True)

    def W(name):
        return open(os.path.join(out, name), 'w', encoding='utf-8', newline='')

    # missions
    for m in ('M0', 'M1', 'M2', 'M3'):
        recs = load_missions(os.path.join(data, m))
        with W('missions_%s.csv' % m) as f:
            cw = csv.writer(f)
            cw.writerow(['index', 'map', 'tileset', 'asc'] + PARAM_NAMES + ['w_def1', 'w_def2', 'w_fb1', 'w_fb2'])
            for i, r in enumerate(recs):
                cw.writerow([i, r['map'], r['tileset'], r['asc']] + r['params'] + r['default_weapons'])
        with W('missions_%s.txt' % m) as f:
            if m == 'M3':
                f.write('NOTE: ' + AERO_NOTES + '\n\n')
            cur_map = ''
            for i, r in enumerate(recs):
                cur_map = r['map'] or cur_map
                f.write('=== %s #%d  map=%r (in effect %r) tileset=%r asc=%r weapons=%s\n'
                        % (m, i, r['map'], cur_map, r['tileset'], r['asc'], r['default_weapons']))
                f.write('  ' + r['briefing'] + '\n')
                nz = ['%s=%d' % (PARAM_NAMES[k], v) for k, v in enumerate(r['params']) if v]
                f.write('  ' + (', '.join(nz) or '(no params)') + '\n\n')

    count, weapons = load_weapons(os.path.join(data, 'WEAPONS.DAT'))
    with W('weapons.csv') as f:
        cw = csv.writer(f)
        cw.writerow(['index', 'name'] + WEAPON_FIELDS)
        for w in weapons:
            cw.writerow([w['index'], w['name']] + [w[k] for k in WEAPON_FIELDS])

    jcount, jets = load_jets(os.path.join(data, 'JETS.N'))
    misc = load_misc(os.path.join(data, 'MISC'))
    miscz = load_miscz(os.path.join(data, 'MISC.Z'))
    l1l2 = open(os.path.join(data, 'L1L2'), 'rb').read()
    with W('planes.csv') as f:
        cw = csv.writer(f)
        hdr = ['index', 'jets_n_name', 'misc_name', 'abk', 'code', 'l1l2_limit', 'desc']
        hdr += ['frame%02d' % k for k in range(62)]
        hdr += ['%d_%s' % (k, PLANE_STAT[k][1]) for k in range(62, 121)]
        cw.writerow(hdr)
        for i in range(max(len(jets), len(misc))):
            mi = misc[i] if i < len(misc) else {}
            row = [i, jets[i] if i < len(jets) else '', mi.get('name', ''), mi.get('abk', ''),
                   mi.get('code', ''), l1l2[i] if i < len(l1l2) else '', mi.get('desc', '')]
            row += miscz[i] if i < len(miscz) else []
            cw.writerow(row)

    with W('enemies.csv') as f:
        cw = csv.writer(f)
        cw.writerow(['record', 'p08_value'] + ENEMY_FIELDS)
        for i, e in enumerate(load_enemies(os.path.join(data, 'ENEMIES'))):
            cw.writerow([i, '%d..%d' % ((i - 1) * 100, (i - 1) * 100 + 99) if i else '-'] + [e[k] for k in ENEMY_FIELDS])

    with W('hudtext.txt') as f:
        for i, s in enumerate(load_lines(os.path.join(data, 'HUDTEXT.DAT'))):
            f.write('%3d 0x%05x %s\n' % (i, 0x8a428 + i * 0x50, s))
    with W('gendatad.txt') as f:
        for i, s in enumerate(load_lines(os.path.join(data, 'GENDATAD.DAX'), 16, 0x28, strip=False)):
            f.write('%2d %r\n' % (i, s))
    with W('sarcasm.txt') as f:
        for i, s in enumerate(load_lines(os.path.join(data, 'SARCASM'), 28, 0x50, strip=False)):
            f.write('%2d %r\n' % (i, s))
    with W('gendata.txt') as f:
        for k, v in load_gendata(os.path.join(data, 'GENDATA.DAX')).items():
            f.write('%s: %s\n' % (k, [hex(x) for x in v] if k == 'colour_rows' and False else v))
    with W('gendat2.txt') as f:
        for k, v in load_gendat2(os.path.join(data, 'GENDAT2.DAX')).items():
            f.write('%s: %s\n' % (k, v))
    with W('gendat3.txt') as f:
        for k, v in load_gendat3(os.path.join(data, 'GENDAT3.DAX')):
            f.write('%s: %s\n' % (k, v))
    with W('bertha.txt') as f:
        for n in sorted(x for x in os.listdir(data) if x.upper().startswith('BERTHA')):
            w, h, rows = load_bertha(os.path.join(data, n))
            f.write('%s %dx%d\n' % (n, w, h))
            for r in rows:
                f.write('  ' + ' '.join('%02x' % t for t in r) + '\n')
    with W('stories.txt') as f:
        for n in sorted(x for x in os.listdir(data) if x.upper().endswith('.ASC')):
            a = parse_asc(os.path.join(data, n))
            f.write('=== %s picture=%s -> %s x=%d y=%d third=%d\n%s\n' % (
                n, a['picture'], a['pax'], a['text_x'], a['text_y'], a['third'], a['text']))
    with W('l1l2.txt') as f:
        f.write(' '.join(str(b) for b in l1l2) + '\n')
    cfgp = os.path.join(game, 'JS.CFG')
    if os.path.exists(cfgp):
        with W('js_cfg.txt') as f:
            for k, v in parse_cfg(open(cfgp, 'rb').read()):
                f.write('%-14s %d (0x%x)\n' % (k, v, v))
    print('wrote', out)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    if argv[1] == 'all':
        game = argv[2] if len(argv) > 2 else 'Game'
        out = argv[3] if len(argv) > 3 else os.path.join('work', 'data')
        dump_all(game, out)
    elif argv[1] == 'save':
        for k, v in parse_save(open(argv[2], 'rb').read()).items():
            print(k, v)
    elif argv[1] == 'mission':
        recs = load_missions(argv[2])
        r = recs[int(argv[3])]
        for k in ('map', 'tileset', 'asc', 'default_weapons', 'briefing'):
            print(k, repr(r[k]))
        for n, v in zip(PARAM_NAMES, r['params']):
            print('  %-22s %d' % (n, v))
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
