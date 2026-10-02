# JetStrike (CD-ROM, 1994): RE guide draft for the game code

Source: `work/JS.bin` (flat image, base 0x10000), Ghidra output in `port/decomp/js.c`. Names are listed in `port/symbols_game.csv`.
Credits string: "conversion by Team Hoi Games in 1994. Martijn Pieterse / Peter Schaap" (Amiga port).

## 1. Memory map

| Range | Object | Contents |
|---|---|---|
| 0x10000-0x107ff | obj1 | low-level VGA mode X asm-style routines (mode set, blits, CRTC) |
| 0x10760 | obj1 | keyboard ISR (a code label, not a Ghidra function) |
| 0x10800-0x485d0 | obj1 | game C code (Watcom, stack calling convention) |
| 0x310ba / 0x31a31 | obj1 | timer ISR (SB mixer) / SB IRQ ISR (code labels) |
| 0x485d0-0x50000 | obj1 | Watcom C runtime (libc, x87 helpers 0x49284..0x4a220, int386x 0x4a2d2, memset 0x4a2b0) |
| 0x50000-0x5023f | obj2 | LZW decompressor (`LZW_Unpack(src,dst)`), used for PAX/SPX/MXP/TLX/DX0 |
| 0x60000- | obj3 | BSS. 0x60000-0x63000 is the LZW dictionary, 0x63000+ holds LZW state. Also an alternate sprite table at 0x63a54 |
| 0x70000-0x704a5 | obj4 | Gravis UltraSound driver (asm) |
| 0x80000-0x94750 | obj5 | data, strings 0x80c40-0x81700, globals 0x8xxxx-0x93xxx |

Key global blocks: `g_Palette` 0x82ed8 (768 B), `g_SpriteTab` 0x83a54, `g_KeyDown` 0x83958, `g_Config` 0x936b4 (JS.CFG image), the map grid pointer at 0x849d0, and the per-object arrays at 0x8d000-0x92a00. Most game state is stored in plain globals; there are no structs.

## 2. Screen mode: Mode X, 320x240, verdict with evidence

`Video_SetModeX` (0x10010):
- INT 10h with mode 13h, then a palette clear.
- Sequencer 0x0604 (chain-4 off) and map mask 0x0F02. GC 0x4005/0x0506.
- CRTC: 0x14 = 0 (underline off), 0x17 = 0xE3 (byte mode), misc output 0x3C2 = 0xE3 (25 MHz, 480-line sync). 0x06 = 0x0D, 0x07 = 0x3E, 0x10 = 0xEA, 0x11 = 0x2A, 0x12 = 0xDF, 0x15 = 0xE7, 0x16 = 0x06. These are the standard 320x240 "Mode X" timings: 480 scanlines, double-scanned, giving 240 lines.
- Offset 0x13 = 0x30, so 96 bytes per row = **384-pixel virtual width** for horizontal scrolling. All code uses `y*0x60 + x>>2`.
- It clears 64 KB at 0xA0000. Planar writes go through 0x3C4 map mask (`'\x11' << (x&3)`, rotating).

Page and scroll handling:
- `Video_SetStartAndPan` (0x106b0) writes CRTC 0x0C/0x0D, waits for vsync, then sets attribute register 0x33 (pel pan, `(x&3)*2`).
- `Video_FlipPage` (0x13a68) double-buffers between `g_BackPage` 0x18c0 and 0x78c0 (+0x600 when displayed). Each page is 384x256.
- `Video_SetSplitLine` (0x105ef) programs the line compare (0x18, overflow bits in 0x07/0x09). In-game it is set to 0xAF (175 lines of playfield). The HUD panel (`display.pax`) sits at VRAM offset 0 and shows below the split line. At title/menu screens it is set to 400 (off).
- Full-screen pictures are 320x200 pages at 0x5a00 bytes per 240-line page, drawn at row 20 (`Pic_LoadPax`).
- Palette: 3C8/3C9 through `Pal_Upload`. Fades in `Pal_Fade` (0x13b01). Colour cycling runs in `Pal_CycleEffects` from the flip.

Palette layout: 0-63 picture/panel (PAX .pal, 64 colours), 64-95 sprites (jetsprit.pal), 96-127 shifted sprite bank (+0x20, `Sprite_BlitShift`), 128-191 tileset (.pal), 0x240/3 = 192+ parallax (p00/p01), 0xFE/0xFF text.

**Port implication:** use a 384x256 8-bit back buffer per page plus a 320x65 HUD strip. Present 320x175 from (scrollX&3 pan, start) above the HUD for a 320x240 output.

## 3. Call tree

```
main (0x146f4)
 ├ malloc check 2.5MB, buffers 85000/82000 (pack + picture)
 ├ Kbd_Install (INT 9 -> 0x10760)
 ├ Sprites_LoadSpx("jetstrike.spx"), Zone_Clear, Sprite_ClearQueue
 ├ fopen js.cfg -> fread 0x48 B into g_Config; joystick calib -> Joystick_Poll
 ├ Video_SetModeX, Pal_Upload, Video_BenchmarkSpeed
 ├ Game_Run (0x1ba0c)
 ├ Sound_Shutdown, Kbd_Restore, Video_SetTextMode, credits printf
Game_Run
 ├ init: gendat3.dax (text/consts, "Negative G"...), Mem_AllocMapBuffers, gendat2.dax (tables),
 │       jetstrike.spx again, data/misc, data/misc.z
 └ loop forever:
     ├ MainMenu (0x469e7)  [first call: CD_Check -> "Wrong CD"/"MSCDEX" fatal; Sound_Init; load
     │     jets.n, weapons.dat, hudtext.dat, l1l2, gendatad.dax, gendata.dax]
     │     menu = miscon.pax with zones: 5 load game (SaveGame_LoadMenu), 4 quit,
     │     3 Aerolimits options (AeroOptions_Menu, aoset.pax), 1/2 training/practice, >5 campaign start
     │     -> g_GameMode 0 campaign / 1 training / 2 practice / 3 Aerolimits (2 players)
     ├ quit -> Pal_Fade, return
     └ while lives>0 && mission<200 && !abort:            (mission loop)
         ├ Video_SetSplitLine(400); Mission_Setup (0x212ac)
         │    ├ Sound_StopAll, CD_Stop
         │    ├ Mission_LoadBriefing (0x2299a): data/M<mode> record; Story_ShowAsc(.asc); jetlogo.pax
         │    │     briefing text; "Press f1 to f10 to save" -> SaveGame_Write; WaitKey_Press
         │    ├ PlaneSelect_Screen (planech.pax) -> WeaponSelect_Screen (wepch.pax)
         │    ├ Tileset_Load(.tlx/.pal), Parallax_Load(.dx0/.p00), map .val/.mp2/.mxp (Map_LoadMxp)
         │    └ Hud_DrawPanel (display.pax, Pic_LoadHudPanel)
         ├ spawn setup, Enemy_InitPositions, CD_PlayTrack(random 2..6,14,15)
         ├ per-frame loop (while mission running):
         │    Video_FlipPage; Player_Update (0x2d34e, reads Input_ReadControls)
         │    object updaters (each guarded by a count global):
         │      Player_Ejection, Player_DeathAndLanding, Turrets_Update, Helis_Update,
         │      EnemyGround_Update, EnemyAir_Update / EnemyBomber_Spawn, Building_Update,
         │      Convoy_Update, Ships_Update, Balloons_Update, Particles_Update, Bonus_Update,
         │      Pickup_Update, Airbase_Update, Crate_Update, Mines_Update, Waypoint_Update,
         │      Flak_Update, Hud_DrawTargetArrow, Projectiles_UpdateGuided, CheatKeys...
         │    Player_Weapons, Weapon_Fire*, Engine_SoundUpdate, Explosion_Damage
         │    Player_DamageSystems (random failures), Mission_CheckComplete
         │    Level_DrawBackground (0x13842) -> Sprite_DrawQueue -> Bullets_Update
         │    -> Hud_DrawMessages -> Hud_UpdateRadar
         ├ end: CD_Stop, last frame, Sound_StopAll; mission++ on success (result==2)
         ├ Mission_Debrief (0x2c9cb, sarcasm); Pal_Restore (night)
         ├ Aerolimits: alternate player, pick next random mission
         └ EndGame_Screen (0x301b2) when lives<1 or game finished (lives=-10):
              campaign: endgame<N>.pax + text, CD_PlayTrack(N+7); mode 1/2: credits; mode 3: AeroScores
```

## 4. Subsystems and their functions

- **Platform/input**: Kbd_ISR, Kbd_Install/Restore, Timer_Install/Restore, Input_ReadControls (0x3ceae), Input_PollMenu, Joystick_*, Key_Up/Down/Left/Right, Input_GetFKey, FatalError, Rand (0x10d65, `rand()%(n+1)`), Clamp, Wrap, Sign.
- **Video**: Video_*, Pal_*, Sprite_Blit/BlitShift/BlitMirror, Sprite_Queue (0x10f11) and Sprite_DrawQueue (0x1155e) (per-frame draw list, sprite ids 1..0x212, `id|0x8000` = mirrored, x wraps at 384), Text_DrawSmall/Big/Number, Font_*.
- **Level/scroller**: The map is `width` columns by 64 rows of 16x16 tiles (1024 px tall). The camera is g_CamX/g_CamY. `Level_DrawBackground` redraws the whole 24x16-tile window into the back page every frame (no incremental scroll). The fine scroll comes from CRTC start + pel pan. Tile accessors are Map_GetTile/Map_GetTileAttr/Map_SetTile; attributes come from the .val table. Map_CraterAt / Map_DamageColumn handle terrain damage.
- **Player**: Player_Update (flight model, camera follow modes), Player_Throttle/Pitch/Rotate*, Player_DeathAndLanding, Player_Ejection, Player_DamageSystems, Engine_*.
- **Weapons**: weapons.dat tables at 0x8f168 (type), 0x921f4/0x91cf4/0x920d8/0x92310/0x91fbc/0x9242c/0x91e10 per weapon; Weapon_Fire*, Projectile_Init, Projectiles_Update(_b), Projectiles_UpdateGuided, Bullets_*, Flares_*.
- **Enemies/AI**: EnemyAir_Update, EnemyGround_Update, EnemyBomber_Spawn, Turrets_Update, Helis_Update, Convoy_Update, Ships_Update, Flak_Update, plus about 20 small target helpers in 0x18237-0x19c6f and 0x425c2-0x43d05 (not individually named yet; they share globals 0x9080c/0x90810 = current object x/y and 0x92590.. = object arrays).
- **Collision**: BoxOverlap (0x1172a), IsOnScreen (0x10eb0), Explosion_Damage (0x39b69), and tile attribute checks (`Map_GetTileAttr < 0x7f` = solid?).
- **HUD**: Hud_DrawPanel, Hud_PushMessage/DrawMessages (text from hudtext.dat / gendat3), Hud_UpdateRadar, Hud_DrawRadarLine, WeaponSelect_DrawCounts.
- **Mission "script"**: There is no bytecode interpreter. A mission is a fixed 450-byte record in data/M0..M3 (see below). The record's 30 big-endian parameters (g_MissionParams 0x91648.., e.g. 0x9164e/0x91650 target x-range, 0x91656/0x91658 counts, 0x91662 vehicle type, 0x9167a/0x9167c flags) select which hard-coded object updaters spawn and how they behave. The .ASC files are only story screens (line 1 = picture/.abk name, then text).

## 5. File loader table

| File | Loader | Notes |
|---|---|---|
| JS.CFG | main 0x146f4 | fread 0x48 into 0x936b4. Words: +0 CD music, +2 sfx, +4..+0x22 16 key scancodes (E, Enter, A, U, L, D, P, Tab, B, Esc, Up, Down, Left, Right, LShift, RShift), +0x24 joy on, +0x26..+0x2c joy calibration, +0x2e detail/parallax, +0x30 fire (Space), +0x32 Alt, +0x34 Ctrl, +0x36 KP*, +0x38 Backspace, +0x3a SB IRQ |
| GFX/*.PAX + .PAL | Pic_LoadPax(name,page,applyPal) 0x11a1a | LZW 320x200 linear, .pal = 64 RGB (8-bit, >>2, first 64 entries R/B swapped via SwapByte) |
| GFX/DISPLAY.PAX | Pic_LoadHudPanel 0x130c8 | HUD panel into VRAM top |
| DATA/JETSTRIK.SPX | Sprites_LoadSpx 0x11306 | LZW. The bank is a sequence of [x,y,w/4,h] 8-byte headers + 4 planes of w*h. Pixel += color offset (0x80008) |
| DATA/JETSPRIT.PAL | Sprites_LoadSpx (second call) | 32 colours -> palette 64..95 |
| PLANE/*.SPX | Plane_LoadSpx 0x26473, Enemy_LoadSpx 0x3f991, Truck_LoadSpx 0x3ff4d | banks copied into sprite slots via Sprites_ReplaceFromBank |
| MAP/<n>.TLX + .PAL | Tileset_LoadTlx 0x133b3 | 256 tiles x 64 B per plane-interleaved 16x16 |
| MAP/<n>.DX0/.DX1, .P00/.P01/.P10 | Parallax_Load 0x1351e | 0x28000 backdrop (LZW). Palette 48 colours at 192. Only with detail on |
| MAP/<n>1.VAL | Mission_Setup / Mission_LoadBriefing | tile attribute table into 0x849d8 (0x13b8 B) |
| MAP/<n><NN>.MP2 | same | into 0x849ec (1000/2000 B). Use unclear (object placement?) |
| MAP/<n><NN>.MXP | Map_LoadMxp 0x2422d | LZW map grid: BE16 width, then rows |
| DATA/M0..M3 | Mission_LoadBriefing 0x2299a | record = 320 briefing + 20 map + 20 tileset + 60 params + 20 asc + 10 |
| DATA/*.ASC | Story_ShowAsc 0x23f6b | story picture + text |
| DATA/WEAPONS.DAT | MainMenu 0x469e7 | text: count, then per weapon name(40), 6 ints, 6 ints, desc(160), 2 ints |
| DATA/HUDTEXT.DAT | MainMenu | 0x50-byte lines -> 0x8a428 |
| DATA/JETS.N | MainMenu | count + 40-char plane names; "Alien Superfighter" marked 9999 |
| DATA/ENEMIES | Mission_LoadBriefing | 7-byte record per enemy set |
| DATA/L1L2 | MainMenu | 60 bytes |
| DATA/GENDATAD.DAX | MainMenu | 16 x 40-char damage messages |
| DATA/GENDATA.DAX | MainMenu | BE16 tables (letter map, sprite ids, sin/cos init) |
| DATA/GENDAT2.DAX, GENDAT3.DAX | Game_Run | BE16 tables / text constants |
| DATA/MISC, MISC.Z | Game_Run | 0xdc-byte records / per-plane sprite offsets (300 B per plane) |
| DATA/SARCASM | Sarcasm_Load | 28 x 80 |
| DATA/BERTHA? | Map_StampBertha | map overlay (BE w,h + bytes) |
| MISC/SMALLFNT.RAW, BIGFNT.RAW | Fonts_Load | glyph bitmaps 16x5 / 16x9 |
| MISC/JETSOUND.AAF | Sound_Init | raw signed 8-bit samples, 0x48a7a B (0x32dea for a 256K GUS) |
| JS_SAVE.000-009 | SaveGame_Write/LoadMenu | |

## 6. Interrupts and hardware

- INT 9 keyboard: 0x10760. It reads 0x60, toggles 0x61 bit 7, EOI, and sets `g_KeyDown[scan&0x7f] = !(scan&0x80)` (0xE0 prefix ignored). Then it copies the JS.CFG-mapped keys into word slots 0x8453a-0x84564. The Alt/Ctrl slots are edge-triggered.
- INT 8 timer: installed only with Sound Blaster (Timer_Install, called from Sound_Init), at ~50 Hz (PIT divisor 0x5d37). The ISR 0x310ba is a 4-channel software mixer writing into the DMA buffer. **The game loop is paced by vsync, not by the timer** (Video_FlipPage waits g_VSyncWaits extra retraces, set by the benchmark).
- SB: base autodetected 0x210-0x280 (DSP reset, 0xAA). IRQ comes from JS.CFG. DMA channel 1 auto-init (mode 0x59, page port 0x83), 8-bit mono. The IRQ ISR 0x31a31 just acks.
- GUS: obj4 driver (GF1 regs base+0x103/0x104/0x105/0x107), 14 voices, samples uploaded to DRAM.
- Joystick: port 0x201 poll loop, calibration from JS.CFG.
- No mouse.

## 7. CD audio (MSCDEX via DPMI 0x300 -> INT 2Fh)

- CD_Check: 1500h install check, then 150Ch version > 2.09, then IOCTL input 0Ah (disc info). The disc **must have 15 tracks**, otherwise "Wrong CD. Consider buying the original!" (fatal). Track 1 is data.
- Track usage:
  - In mission: `t = rand(13)+2`, re-rolled while `t<2 || (7<=t<=13)`, so a random track from {2,3,4,5,6,14,15}. Started at the start of every mission, stopped at the end.
  - End game: `CD_PlayTrack(endgameIndex + 7)` (tracks 7.., together with endgame<N>.pax).
  - No CD music in menus or briefings; CD_Stop is called there. INTRO.EXE may use other tracks (not checked).
- CD_PlayTrack uses the cached track start table (CD_ReadTrackTable, MSF -> frames).

## 8. Open questions

1. The exact meaning of the 30 mission parameters and the .MP2 contents. The next pass should trace reads of 0x91648-0x91684 and of 0x849ec.
2. About 60 small AI/target functions in 0x18237-0x19c6f and 0x40f5a-0x43d05 are only coarsely named. Several Ghidra functions are duplicated at +0xd; these are not mis-detections: the first address is the `push n; call __CHK` prologue and some callers enter directly at +0xd, past the stack check, so Ghidra sees a second entry. Use the first address (the port has one function).
   Old note: functions duplicated at +0xd (15d2b/15d38, 160f4/16101, 167b6/167c3, 332fb/33308, 40f5a/40f67, 42b70/42b7d, 1b124/1b131, 19ab5/19ac2), which suggests mis-detected prologues. Use the first address.
3. Units and physics of Player_Update (0x2d34e): direction is 0..63 (DAT_00090200), speed 0x8ffd4 (0..9), and it heavily uses x87 helpers.
4. The sprite colour offset 0x80008 and the shift bank (+0x20): check whether the shifted bank is the hit-flash or the night palette.
5. The SPX header field order (x, y, w/4, h) is inferred from the blitters: w is in 4-pixel units and the planes are stored sequentially.
6. The layout of the JS.CFG trailing words and the meaning of keys E/A/U/L/D/B: compare against CONFIG.EXE (work/CONFIG.bin).
7. Frame rate: probably 70 Hz/(1+g_VSyncWaits). Verify against a DOSBox capture.

## Regenerating

```
sh tools/regen.sh            # JS INTRO CONFIG: lefile.py -> leindex.py -> merge_symbols.py -> Ghidra
```
- `tools/lefile.py` LE loader: objects at their bases, fixups applied, flat `work/<EXE>.bin` + `.json` + `_fixups.csv`.
- `tools/leindex.py` function starts from the `push imm32; call __CHK` prologues and data-object code pointers.
- `tools/ghidra/SetupLE.java` (pre): RWX block, `__watcall` prototype available, entry point, `__CHK` declared
  `__stdcall` (it pops its size argument) before analysis, otherwise every function's stack tracking breaks.
- `tools/ghidra/ApplySymbols.java`, `DecompileAll32.java` (everything else gets `__cdecl`: the game is built
  with Watcom's stack convention, `-3s`, caller pops).
- Symbols: `port/symbols_rt*.csv` (C runtime, asm objects), `port/symbols_game.csv` (game code), later
  `port/spec/*_symbols.csv`; merged into `port/<exe>_symbols.csv`.
