# JetStrike file formats

Index; the details (offsets, types, loader and consumer addresses) are in `port/formats/`.
Each format has a decoder in `tools/` that writes to `work/` (ignored).

| Files | Doc | Decoder | Summary |
|---|---|---|---|
| packed files (PAX, SPX, TLX, MXP, DX0/DX1) | below | `tools/jsunpack.py` | u32 LE size + MSB-first LZW, 9-12-bit codes (literal port of LE object 2, 0x50000) |
| `GFX/*.PAX` + `.PAL` | `port/formats/gfx.md` | `tools/jsgfx.py` | linear 8-bit, 320 wide, height = size/320; PAL: 64 colours, R/B swapped, >>2 |
| `*.SPX`, `DATA/JETSPRIT.PAL` | `port/formats/gfx.md` | `tools/jsgfx.py` | run of `hotX, hotY, w/4, h` + 4 planes; +0x40 at load, 0x40 transparent; sprite colours 64-127 |
| `PLANE/*.HD` | `port/formats/gfx.md` | | helicopter collision boxes, 100 × 16 bytes BE |
| `MISC/*FNT.RAW` | `port/formats/gfx.md` | `tools/jsgfx.py` | 1 byte/pixel fonts, 16 bytes/row; HUD digits are a 3×5 table in the exe |
| `MAP/*.TLX`, `.PAL`, `.MXP`, `.VAL`, `.MP2`, `.DX0/.DX1`, `.P00/.P01/.P10` | `port/formats/maps.md` | `tools/jsmap.py` | 256 planar 16×16 tiles (colours 128-191); map BE w × 64 rows; 4 × 256-byte tile attribute tables; per-column trigger table; 320×512 parallax backdrop (colours 192-207) |
| `DATA/M0-M3` | `port/formats/data.md` | `tools/jsdata.py` | 450-byte mission records: briefing, map, tileset, 30 BE params, story screen, default weapons |
| `DATA/*.ASC`, `WEAPONS.DAT`, `HUDTEXT.DAT`, `ENEMIES`, `GENDAT*.DAX`, `JETS.N`, `MISC`, `MISC.Z`, `L1L2`, `BERTHA*`, `SARCASM` | `port/formats/data.md` | `tools/jsdata.py` | text (fscanf/fgets) and big-endian Amiga tables |
| `JS.CFG`, `js_save.00N` | `port/formats/data.md` | `tools/jsdata.py` | 30 LE words; 311-byte LE save |
| `MISC/JETSOUND.AAF` | `port/formats/sound_intro.md` | `tools/jssound.py` | AMOS sample bank, played as 35 raw slices from a table in the exe |
| `INTRO/*` | `port/formats/sound_intro.md` | `tools/jsintro.py` | intro sprites, planar tiles, city lights, explosion frames, samples; timeline |
| CD tracks 2-15 | `port/formats/sound_intro.md` | `tools/cdrip.py` | ripped to `Game/MUSIC/TRACKNN.WAV`; intro 2, missions random {2-6, 14, 15}, end game N → N+7 |

DOS file names: the game builds long names (`Trainingicons.tlx`); the port truncates to 8.3 and
opens case-insensitively.

## Original quirks the port keeps (decide per item in phase 3 specs)
- Sound Blaster effect slices start 148 bytes before the real sample (table lengths include the AMOS
  header, slicing starts at 0); mixer adds without clipping; only two pitch steps on SB (the engine jumps an octave at frequency 10000); see `port/spec/sound.md` for the SB rate bug.
- `WEAPONS.DAT` loads count+1 records; F10 save writes `js_save.00:`.
- Intro exits only on Esc or D despite "PRESS ANY KEY".
