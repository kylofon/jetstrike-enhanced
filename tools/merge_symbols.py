"""Merge the symbol tables of one executable into port/<exe>_symbols.csv and report conflicts.

Inputs (header address,name,module,confidence,note; address = flat hex as loaded at 0x10000):
  port/symbols_{rt,game,data}[_<exe>].csv, port/spec/*_symbols.csv (intro_symbols.csv to INTRO, the rest to JS)
Globals are rows with module "global" or a name starting with g_.
Also writes work/<exe>_symbols_ghidra.txt for tools/ghidra/ApplySymbols.java.

    python tools/merge_symbols.py js|intro|config
"""
import csv, glob, os, re, sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
exe = sys.argv[1] if len(sys.argv) > 1 else 'js'
sfx = '' if exe == 'js' else '_' + exe
paths = [os.path.join(ROOT, 'port', f'symbols_{k}{sfx}.csv') for k in ('rt', 'game', 'data')]
specs = sorted(glob.glob(os.path.join(ROOT, 'port', 'spec', '*_symbols.csv')))
if exe == 'js':
    paths += [p for p in specs if os.path.basename(p) != 'intro_symbols.csv']
elif exe == 'intro':
    paths += [p for p in specs if os.path.basename(p) == 'intro_symbols.csv']

rows = defaultdict(list)
for path in paths:
    if not os.path.exists(path):
        continue
    src = os.path.basename(path)
    with open(path, newline='', encoding='utf-8') as fh:
        for r in csv.DictReader(fh):
            try:
                a = int(r['address'].strip(), 16)
            except (ValueError, AttributeError):
                continue
            name = re.sub(r'\W', '_', (r.get('name') or '').strip())
            if name:
                rows[a].append((src, name, r))

out, conflicts = [], []
for a in sorted(rows):
    names = {n for _, n, _ in rows[a]}
    if len(names) > 1:
        conflicts.append(f'{a:08x}: ' + ', '.join(f'{n} ({s})' for s, n, _ in rows[a]))
    src, name, r = rows[a][-1]  # spec files (last) win over the first-pass tables
    out.append(dict(address=f'{a:08x}', name=name, module=r.get('module', ''),
                    confidence=r.get('confidence', ''), note=r.get('note', ''), source=src))

with open(os.path.join(ROOT, 'port', f'{exe}_symbols.csv'), 'w', newline='', encoding='utf-8') as fh:
    w = csv.DictWriter(fh, fieldnames=['address', 'name', 'module', 'confidence', 'note', 'source'])
    w.writeheader()
    w.writerows(out)
os.makedirs(os.path.join(ROOT, 'work'), exist_ok=True)
with open(os.path.join(ROOT, 'work', f'{exe}_symbols_ghidra.txt'), 'w', newline='\n') as fh:
    for r in out:
        kind = 'global' if r['module'] == 'global' or r['name'].startswith('g_') else 'func'
        fh.write(f"{kind} {r['address']} {r['name']}\n")
print(f'{exe}: {len(out)} symbols, {len(conflicts)} conflicts')
for c in conflicts:
    print('  ' + c)
