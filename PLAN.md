# JetStrike — SDL3 port plan

Same approach as TestDrive1987 / TestDrive2 / TestDrive3 / Street Rod / Aces of the Pacific: first a
**faithful C port** here (reverse-engineer → document → port function by function against the
original data, read at runtime), then, if wanted, a separate **JetStrike Enhanced** repo built on it.

JetStrike (1994, Shadow Software / Rasputin Software, CD-ROM edition) is a side-scrolling 2D
arcade flight game (Amiga original, 1993). **Big difference from the earlier ports:** this is a
**32-bit protected-mode program** (Watcom C, DOS/4GW, flat model, LE executables), not 16-bit
real-mode MSC/EXEPACK. The 16-bit tooling (`unexepack.py`, `x86dis.py`, segment/DGROUP model,
`mem.h` segment:offset memory) does not carry over; the workflow, specs layout, host layer, launcher
and release process do.

## What we have (recon, 2026-10-02)

Source copy: `Jetstrik/` (untouched, git-ignored): `JETSTR/` installed game, `CD/` CloneCD image
(`.img/.cue/.ccd/.sub`, 384 MB), `Jetstrike (1994).exo` (eXoDOS marker, empty).
Working copy: `Game/` = `JETSTR/` (git-ignored; the port reads it at runtime).

| File | Size | Notes |
|---|---|---|
| `JS_CDROM.EXE` | 445 141 | main game. MZ stub + **LE** at 0x28B8, Watcom C 1988-92, 5 objects (code 0x3F794 @ 0x10000, data obj 5 0x14750 @ 0x80000), entry 1:0x385D0. Needs MSCDEX ("Wrong CD" check) |
| `INTRO/INTRO.EXE` | 77 134 | separate intro program (LE, 6 objects); data `INTRO/*.RAW, *.SPR, INTRO.TIL, INTRO.SAM` (digitised sound), `CITY.PTT` |
| `CONFIG.EXE` | 49 566 | setup (LE): sound fx on/off, SB IRQ, joystick calibration, key map → `JS.CFG` (60 bytes: scancodes + options) |
| `JS.BAT` | | `cd intro / intro / cd .. / js_cdrom` |
| `DOS4GW.EXE` | 265 420 | extender — not needed by the port |
| `GFX/*.PAX + .PAL` | 26 pairs | full-screen pictures (title, menus, plane/weapon choice, mission briefings, end-game, scores) |
| `PLANE/*.SPX` (115), `*.HD` (8) | 574 K | sprite sets: player planes, enemies, helicopters (`.HD` = heli data?) |
| `MAP/` | 109 files | levels: `*.MP2/.MXP` (21 maps), tile sets `*.TLX/.P00/.P01/.P10/.DX0/.DX1/.PAL` (8 sets), `*.VAL` |
| `DATA/` | 46 files | missions `*.ASC` (training, combat1-12, city, jungle, bonus, aolim…), `WEAPONS.DAT`, `HUDTEXT.DAT`, `ENEMIES`, `GENDAT*.DAX`, `JETS.N`, `BERTHA*`, `M0-M3`, `L1L2`, `MISC(.Z)`, `SARCASM`, `JETSPRIT.PAL`, `JETSTRIK.SPX` |
| `MISC/JETSOUND.AAF` | 297 594 | sound-effect bank (digitised, Sound Blaster); also looks for `jetsoundext.aaf` |
| `MISC/BIGFNT.RAW`, `SMALLFNT.RAW` | | fonts |
| CD tracks 2-15 | | **Red Book audio** — the music (14 tracks, ~31 min). Track 1 is the data track |

Video: VGA mode X 320×240×256 (unchained, 384-pixel virtual width; see `port/RE_GUIDE.md`).
Sound: Sound Blaster or Gravis UltraSound digitised effects (auto-detected), CD audio music via MSCDEX. Input: keyboard
(remappable, `JS.CFG`), joystick.

## Phases

### 0. Repository — done (2026-10-02)
- `C:\Coding\JetStrike`, git `master`, private remote https://github.com/kylofon/jetstrike-sdl3.
- `Game/` original data (ignored); `Jetstrik/` the untouched copy with the CD image (ignored).
- `_tools` → junction to `TestDrive1987/_tools` (Ghidra 12.1.3, JDK 21).
- Carried over from Street Rod: `gen_symbols.py`, `merge_symbols.py`, Ghidra `ApplySymbols`,
  `DecompileAll`, `postprocess.py`. Not carried over: the 16-bit tools.

### 1. Executable map — done (2026-10-02, see `port/RE_GUIDE.md`)
Result: LE loader + indexer + Ghidra pipeline (`tools/regen.sh`); JS 741 functions decompiled, 599 named
(275 Watcom runtime / asm, ~290 game, 43 globals); INTRO 110 and CONFIG 99 runtime names by masked
byte match. Findings: `main` 0x146f4 → `Game_Run` 0x1ba0c; **mode X 320×240** (384-pixel virtual width,
double-buffered, CRTC scrolling, split-screen HUD at line 175); sound = **Sound Blaster or Gravis
UltraSound** (obj 4 is a GUS driver), 4-channel software mixer at 50 Hz on SB; CD audio = random track
of {2-6, 14, 15} per mission, endgame N → track N+7; obj 2 is an LZW unpacker used by PAX/SPX/MXP/TLX/DX0;
no mission script interpreter (M0-M3 records drive fixed code; `.ASC` = story text).
Original sub-plan:
- `tools/lefile.py`: LE/LX parser — objects, page map, fixups → flat image + relocation list;
  dump each object to `work/`.
- Ghidra: needs an LE loader (Ghidra has none built in) — either the `ghidra-lx-loader` extension
  or our own converter to a flat ELF/raw binary with the fixups applied (decide after a test).
- Watcom runtime identification (FLIRT-like signature match against the Watcom 9.x/10.0 clib —
  build a matcher like `tdmatch.py` from a Watcom-compiled reference), register calling convention
  (`EAX, EDX, EBX, ECX`) set in Ghidra so the decompile is readable.
- `DecompileAll` → `port/decomp/`; `port/RE_GUIDE.md` (main loop, state machine, file loaders,
  interrupt handlers, DPMI / VGA / SB / MSCDEX calls), `port/symbols.csv`.
- Same for `INTRO.EXE` (small; may be ported as its own module or folded into the main program).

### 2. File formats — done (2026-10-02, `FORMATS.md`, `port/formats/`)
Every file in `Game/` is decoded (`tools/jsunpack.py`, `jsgfx.py`, `jsmap.py`, `jsdata.py`, `jssound.py`,
`jsintro.py`); CD tracks ripped by `tools/cdrip.py` to `Game/MUSIC/`. The CD's data track holds the same
files as `Game/`. No mission script: missions are 30 parameters driving fixed code. Open: some VAL
attribute values, MP2 details, p20-p23/p29, ~30 plane-stat fields (phase 3).
Original sub-plan:
1. `PAX`/`PAL` pictures → PNG (likely compressed 256-colour screens + 768-byte VGA palette).
2. `SPX` sprite sets and `JETSPRIT.PAL` → contact sheets.
3. `MAP/` tile sets and maps (`TLX`, `P00/P01/P10`, `DX0/DX1`, `MP2/MXP`, `VAL`) → full level PNGs.
4. Mission scripts `DATA/*.ASC`, `ENEMIES`, `WEAPONS.DAT`, `HUDTEXT.DAT`, `GENDAT*.DAX`, the rest of
   `DATA/` (whatever `MISC.Z` compression is).
5. `JETSOUND.AAF` → WAVs; `INTRO.SAM`.
6. Intro assets (`*.RAW`, `*.SPR`, `INTRO.TIL`, `CITY.PTT`).
7. `JS.CFG`, save games / high scores (whatever the game writes).
8. CD audio: `tools/cdrip.py` extracts tracks 2-15 from the `.img` (raw 2352-byte sectors, offsets
   from the `.cue`) → WAV, then OGG/FLAC into `Game/MUSIC/` for the port. Port reads them at
   runtime; not redistributed.

### 3. Specs (`port/spec/`) — done (2026-10-02)
Nine specs with symbol tables: `platform`, `video`, `sound`, `game_flow`, `intro`, `level`, `player`,
`weapons`, `enemies`; names merged (`tools/merge_symbols.py`, hand-resolved conflicts in
`port/symbols_overrides.csv`): JS 1297 names, INTRO 209. Logic runs at 19.98 Hz (3 retraces of 59.94 Hz).
Bugs and quirks: `port/QUIRKS.md` (all kept in this port; fixes go to Enhanced).
Original sub-plan:
Split by call tree once the map is known; expected:
- `platform` — startup, config file, timer/IRQ, keyboard (remap), joystick, file I/O, memory
- `video` — mode setup, palette (fades), blitters, sprite drawing, scrolling, fonts
- `sound` — SB driver, effect mixing/priorities; CD-audio track selection (which track where)
- `game_flow` — intro, title, menus (plane choice, weapon choice), mission briefings, scores,
  end game, the CD check (dropped)
- `level` — map/tile scroller, mission script interpreter, enemy spawning
- `play` — player plane physics, weapons, enemies AI, collisions, damage, HUD, carrier/landing

### 4. `jsport/` skeleton
CMake + SDL3 adapted from `srport`/`td3port`: host (timer, retrace, scancodes, mouse, gamepad,
audio mixer for SB samples **plus a music stream for the ripped CD tracks**), 8-bit
indexed framebuffer + palette, generated `symbols.h`, `PORTING.md`, headless test path
(`SDL_VIDEO_DRIVER=dummy`, snapshot dir, scripted keys). Because the original is flat 32-bit
C, the memory model is far simpler: plain C structs/pointers instead of `mem.h` seg:off.
Placeholder `game_main` shows `JETLOGO.PAX`.

### 5. Port, subsystem by subsystem
platform/video → title & menus (visible milestone) → intro → level scroller + sprites → player
plane and weapons → enemies / mission scripts → HUD, scores, end game → sound effects → CD music.
Each step checked against DOSBox captures of the original (`DOSBOX/`, ignored; mount the `.cue`
for CD audio) and headless snapshots.

### 6. Release, then (optionally) JetStrike Enhanced
Like TD2/TD3/Street Rod: zip + wxWidgets launcher, `RELEASE_NOTES.md`, `SHA256SUMS.txt`; make the
GitHub repo public at the first release. Enhanced ideas later (widescreen, higher resolution,
remastered sprites).

## Decisions (2026-10-02, provisional — the user can overrule)

1. **Target executable**: `JS_CDROM.EXE` (the CD version) with its data read at runtime.
2. **CD check / MSCDEX**: dropped, with a `/* PORT: */` note.
3. **Music**: CD tracks ripped once by a tool into `Game/MUSIC/trackNN.*`; the game plays without
   them (silent music) if missing.
4. **Intro**: ported as part of the same program (skippable), not as a second executable.
5. **Config**: `CONFIG.EXE` not ported; its options (sound on/off, keys, joystick) go into the
   launcher; `JS.CFG` is still read/written in the original format.

6. **Sound Blaster rate**: 19920 Hz by default (the intended rate); a setting offers 3906 Hz, the rate the
   original's bug actually programs (`port/spec/sound.md`).

Confirmed by the user (2026-10-02). No floppy/AdLib version for now.

## Open questions
- Meaning of the 30 mission parameters and of `.MP2`; fine detail of ~60 small AI helpers (phase 3).
