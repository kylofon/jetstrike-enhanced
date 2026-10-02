# Intro (INTRO.EXE): scene state machine, for an in-process skippable port

Binary: `work/INTRO.bin` (flat LE image, base 0x10000, file offset = addr-0x10000), decompile
`port/decomp/intro.c`, names `port/intro_symbols.csv`; symbols for this spec: `port/spec/intro_symbols.csv`
(all INTRO.EXE addresses). File formats, sound and CD details: `port/formats/sound_intro.md` §2 (this spec
supersedes its timeline where they differ; differences are marked **[corr]**).
Confidence: **verified** (decompile checked against capstone where it matters), **likely**, **guess**.

Original launch: `JS.BAT` = `cd intro / intro / cd .. / js_cdrom`. INTRO.EXE is a separate DOS/4GW program
with its own copies of the mode-X, SB/GUS and MSCDEX code. Most functions are a 13-byte `__CHK` stub that
falls into the body at +0xd; addresses below name the stub, bodies are listed in the CSV.

---------------------------------------------------------------------------------------------------

## 1. Port design

| Original | Port |
|---|---|
| separate EXE run before the game | `Intro_Run()` called by the port's `main` before `Game_Run` (after SDL/video/audio init, before `Kbd_Install`-equivalent state is used by the game). Returns when skipped. Optional command-line switch to skip. |
| own mode X 320x240, 2 pages (0, 0x5a00), CRTC start + pel pan, 1 frame per vertical retrace | two 384x240 8-bit pages (96 bytes x 4 planes = 384 px virtual width); present 320x240 starting at x = `scroll` (0..15) of the shown page; **one logic frame per retrace = 59.94 Hz** (not the game's 20 Hz). |
| own palette (192 colours embedded) | load from the image bytes (§3) — the port must ship them (extract from INTRO.EXE at build time, or a small binary blob). |
| INT 9 ISR sets `g_KeyReleased[sc]=1` on **release** | on SDL key-up translate to set-1 scancode and set the flag; only Esc (1) and 'D' (0x20) are used by the live code. **Port: also accept any key / mouse / pad button (PORT note)**; original accepts only Esc/D released. |
| SB mixer polled 4x per frame (`Mixer_Update`) / GUS | the game's SDL audio mixer; play the INTRO.SAM slices as in `sound_intro.md`. |
| CD track 2 via MSCDEX | play Game/MUSIC/TRACK02 (if present); stop on exit. |
| exit: text mode, restore INT 9, free | stop sound/CD, keep the window, hand over to the game. |
| `rand()` (Watcom LCG) never seeded in the intro (no `srand` call) | use a **separate** LCG instance seeded with 1 so the intro is deterministic and does not disturb the game's RNG (the game seeds with `srand(time)` at MainMenu anyway). |

---------------------------------------------------------------------------------------------------

## 2. Data

```c
uint8  *g_RawBuf;        /* 0x671ec INTRO.RAW 0x36320 bytes (97 sprite blocks)          */
uint8  *g_BigFont;       /* 0x671fc BIGFNT2.RAW (0xc80) + BIGFNT1.RAW (0x9d0)            */
uint8  *g_SmallFontBuf;  /* 0x671f8 SMALLFNT.RAW 0xc90, never drawn                      */
uint8  *g_TilBuf;        /* 0x671e8 INTRO.TIL 0x18000 = 384 tiles x 256 B (4 planes x 64) */
uint8  *g_Char1Buf;      /* 0x67040 CHAR1.RAW 0x33f8, never drawn (it is offs[-1] slot)  */
uint8  *g_AosetBuf;      /* 0x671f0 malloc(0x12c00), only for the dead menu              */
uint8  *g_FrameBuf[8];   /* 0x67020.. FRAME1..8.RAW chunky 320 x H[k]                    */
int32   g_CityPoints[405][3]; /* 0x60e98 CITY.PTT {x, y, colour}, read 0x12fc bytes       */
int32   g_SpriteSize[97][2];  /* 0x60b50 {w, h} of INTRO.RAW sprite n at [n-1]           */
int32   g_SpriteOffs[98];     /* 0x67044 offs[0] = 0, offs[k] = start of block k+1 + 4   */
uint8  *g_TilePtrs[384];      /* 0x66218 g_TilBuf + i*0x100                              */
int32   g_TileMap[16][24];    /* 0x66818 tile index per 16x16 cell (initially i)         */
int32   g_KeyReleased[128];   /* 0x66e18                                                  */
uint8   g_Palette[0x300];     /* 0x65c18 master (first 0x240 from 0x6036c, rest 0)       */
uint8   g_PaletteWork[0x300]; /* 0x65f18 faded copy uploaded by Pal_Set                  */
int32   g_Scene;              /* 0x67018 1..7                                            */
int32   g_CreditIndex;        /* 0x60368 0..10                                           */
int32   g_LogoShown;          /* 0x60701                                                 */
int32   g_FrameOverlayActive; /* 0x60705                                                 */
```
FRAMEk heights {21,40,86,151,180,203,217,240}, buffers malloc'ed {0x1a40,0x3200,0x6b80,0xbcc0,0xe100,0xfdc0,
**0x10f18**, 0x12c00}; FRAME7 is read with **0x10f40** bytes (= 320*217) → 40-byte heap overflow in the
original (**Q1**; port: allocate the file size).

Sprite table build (verified): `pos = 0; offs[0] = 0;` for k = 1..97: `w = BE16(raw+pos)*16; h =
BE16(raw+pos+2); size[k-1] = {w,h}; raw[pos+4 .. pos+w*h-1] &= 0x1f; raw[pos..pos+3] = 0; pos += w*h;
offs[k] = pos + 4;`. Sprite n is drawn from `raw + offs[n-1]` (code uses `(&0x67040)[n]` = offs[n-1]),
w x h bytes, chunky, 0 transparent. So sprite 1 is drawn from `raw+0` (4 zero bytes, then its data shifted
by 4 pixels) while sprite n ≥ 2 starts at its block+4 and its last 4 pixels come from the next (zeroed)
header (**Q2**, keep: invisible differences except a 4-px shift of sprite 1).

Palette (verified): bytes 0x6036c[0..0x5f] (colours 0..31) `<<= 2`; [0x60..0x23f] (32..191) `>>= 2`
(unsigned); copied to `g_Palette[0..0x23f]`; colours 192..255 black.

Credit strings `g_CreditStrings` 0x6072d (11 ptrs), widths `g_CreditWidths` 0x6075d (11 x {w1, w2}):
```
0 "PC PROGRAMMING\TEAM HOI GAMES"                    150 150
1 "PUBLISHED BY\RASPUTIN SOFTWARE\COPYRIGHT 1994"   128 184   (3rd line reuses w2 → off-centre, Q3)
2 "ORIGINAL DESIGN\SHADOW SOFTWARE"                  153 167
3 "DESIGN\AARON FOTHERGILL"                           66 161
4 "CODE\MARTIJN PIETERSE"                             44 161
5 "ADDITIONAL CODE\PETER SCHAAP"                     157 132
6 "GRAPHICS SOUND AND MUSIC\ADAM FOTHERGILL"         256 161
7 "INTRO BACKGROUND GRAPHICS\METIN SEVEN"            256 117
8 "ADDITIONAL SOUND EFFECTS\DIGITAL DOMAIN"          256 142
9 "MUSIC RECORDED AT\THE CUPBOARD BARNSTAPLE"        183 253
10 "PRESS ANY KEY\TO PLAY JETSTRIKE"                 143 183
```
Glyph set 0x607b5 → "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!?".

---------------------------------------------------------------------------------------------------

## 3. Drawing primitives (INTRO.EXE copies; verified)

- `Video_FlipScroll(x, showPage1)` 0x205c4: CRTC start = (showPage1 ? 0x5a00 : 0) + x/4; **wait for the end
  of the current retrace, then for the start of the next**; then attribute 0x33 pel pan = (x&3)*2.
  Port: present page `showPage1` from pixel column x, rows 0..239; then wait for the next 59.94 Hz tick.
- `Tile_DrawBand(map, ptrs, band, plane, pageOfs)` 0x20614: band b (0..7) covers page rows b*32..b*32+31;
  48 tiles (24 per row, 2 rows; band 7 = 24 tiles, 1 row = rows 224..239) from `map + b*48`; tile t's
  plane p bytes `ptrs[t][p*0x40 + r*4 + c]` (r 0..15, c 0..3) go to page byte (r*96 + tileCol*4 + c) of that
  plane. In chunky terms: page pixel (tileCol*16 + c*4 + p, b*32 + tileRow*16 + r) = tile[p*64 + r*4 + c].
  Opaque.
- `Tile_ScrollMapLeft(map)` 0x20ba3: each of the 16 rows of 24 entries rotates left by one.
- `Video_PlotUnder(x, y, col, pageOfs)` 0x200b8: writes `col` at (x, y) only if the current pixel is 0 or
  0x20..0x24.
- `Spr_BlitClip(src, x, y, w, h, 0, 0, pageOfs)` 0x100ce: draws only if `x + w > 0 && x < 0x150 && y >= 0`;
  `x < 0`: draw columns -x..w-1 at x=0; `x + w >= 0x160`: draw columns 0..(0x160-x)-1; else whole.
  No bottom clip (h never exceeds 240-y in the live code). Pixel 0 transparent. Draw area is 0..351 px of
  the 384-px page (scroll adds up to 15).
- `Spr_Blit` 0x202c0: chunky → planar, 0 transparent, source row stride w + skip + pad.
- `Pal_Set(pal, first, last, waitVsync)` 0x20254; `Pal_Fade(dir)` 0x149a6: dir 0: k = 1..25 `work[i] =
  pal[i]*k/25` (int), `Pal_Set(work,0,255,1)` (1 retrace each, 25 frames); dir 1: k = 25..0 (26 frames).
- `Intro_DrawCredits(scroll, pageOfs)` 0x13ff1: `x = scroll + (320 - w1)/2; y = (g_LogoShown ||
  g_FrameOverlayActive || g_Scene == 7) ? 0x96 : 100;` for each char: `'\\'` → `x = scroll + (320 - w2)/2;
  y += 12`; space → `x += 11`; else glyph k (index in the glyph set; not found → k = 38, past the end),
  `Spr_Blit(g_BigFont + k*0x90 + (k < 17 ? 0x10 : 0x20), x, y, 16, 9, 0, 0, pageOfs)`, then `x += (ch == 'I')
  ? 7 : 11; if (ch == 'W') x += 1;`.

---------------------------------------------------------------------------------------------------

## 4. Intro_Main 0x107c8 (stub `main` 0x107bb) — scene state machine (verified)

### 4.1 Init (in order)
1. `Intro_SoundInit()`; `g_KeyReleased[0..127] = 0`; palette conversion (§2); `g_PaletteWork = 0`.
2. malloc all buffers; any of RAW/BIGFONT/SMALLFONT/TIL/CHAR1 NULL → `printf("Not enough memory!")`, return 0.
3. fread all files (§2), build tile pointers and the sprite table.
4. hook INT 9; `Video_SetModeX()` (clears VRAM, shows page 0); `g_Palette` ← source; `g_PaletteWork = 0`;
   `Pal_Set(work,0,255,0)`.
5. State: `g_Scene = 1; xA = -96 (0xffffffa0); yA = 100; xB = -120; yB = 0x90; sprA = 1; sprB = 1;
   ovlStep = 1; bobA = 0; bobB = 0; wait = 0; g_LogoShown = 0; sample0Played = 0;` map identity.
6. Draw page 0 (pageOfs 0): bands 0..6 x planes 0..3 (band 7 not drawn here); city: every point `y -= 2`
   (once, permanent); plot points with x < 0x170 at `(0 + x, 0x9e + y)` with `Video_PlotUnder`.
7. `Pal_Fade(0)` (25 retraces), `Intro_PlaySample(6)` (loop bed), `CD_InitAndPlayTrack2()` (MSCDEX: prints
   "MSCDEX not installed" / "...version 2.10 or better required" and continues silently on failure).

Frame-local state (all int32, initial 0 unless stated): `scroll` (local_fc), `pageOfs` (local_124, draw page
0 or 0x5a00), counters `c3` (100), `c10` (104), `cCred` (108), `cStep` (10c), `cOvl` (110), `credOn` = 1 (local_c),
`exit` = 0, per-scene vars listed in §4.3. Smoke particles: arrays `PX[100] PY[100] PS[100] PA[100]` (in
local_798) and count `np` (local_d8).

### 4.2 Per-frame order (one iteration per retrace)
```c
while (!exit) {
  Video_FlipScroll(scroll, pageOfs == 0);         /* show the OTHER page (drawn last frame), wait retrace */
  if (SB && sam) Mixer_Update();
  c3++; c10++; cCred++; cStep++;
  if (c3 == 3) { c3 = 0; if (scroll < 15) scroll++; else { Tile_ScrollMapLeft(g_TileMap); scroll = 0; } }
  if (c10 == 10) { c10 = 0; for all 405 points x--; for all: if (x < 0) x = 0x17f; }
  for (band = 0; band < 8; band++) for (plane = 0; plane < 4; plane++) Tile_DrawBand(g_TileMap, g_TilePtrs, band, plane, pageOfs);
  if (SB && sam) Mixer_Update();
  for all points: if (x < 0x170) Video_PlotUnder(scroll + x, 0x9e + y, colour, pageOfs);
  if (g_LogoShown && !g_FrameOverlayActive) DrawLogo();
  if (g_FrameOverlayActive) Overlay();            /* §4.4 */
  if (SB && sam) Mixer_Update();
  Scene();                                        /* §4.3 */
  if (SB && sam) Mixer_Update();
  Credits();                                      /* §4.5 */
  pageOfs = (pageOfs == 0) ? 0x5a00 : 0;
  if (Intro_ReadKey() == -1) exit = 1;            /* Esc or 'D' released (flags consumed) */
}
Intro_SoundShutdown(); CD_Stop(); Pal_Fade(1); return 1;     /* then Intro_ExitWaitKey / Intro_Cleanup (§5) */
```
`DrawLogo()` = `Spr_BlitClip(raw + offs[65], (320 - w66)/2 + scroll, (240 - h66)/2 - 12, w66, h66, 0, 0, pageOfs)`
(sprite 66, the JETSTRIKE logo). All scene coordinates are drawn at `scroll + x`.
The very first loop iteration shows page 1 (still black) for one retrace (**Q4**, invisible: the palette is
already up — a 1-frame black flash; port may skip).

### 4.3 Scenes (sprite `n` = INTRO.RAW sprite n; Draw(n,x,y) = Spr_BlitClip(raw+offs[n-1], scroll+x, y, w[n], h[n]))

`step` below means "this frame advances the animation": the scene code is `if (cStep < K && wait == 0)
{ draw only } else { cStep = 0; advance; draw }`, and `cStep` was already incremented at the top of the
frame, so K = 1 → every frame, K = 2 → every 2nd frame, K = 3 → every 3rd frame. Once `wait > 0` every
frame is a step.

**Scene 1 — two jets fly in** (every frame; RNG: 6 rand() per frame in this order):
```c
r1 = rand(); r2 = rand(); if (xA < (r2%10)*(r1%22)/10 + 100) xA += 2; else if (!sample0Played) { Intro_PlaySample(0); sample0Played = 1; }
r1 = rand(); r2 = rand(); if (xB < (r2%10)*(r1%22)/10 + 0x56) xB += 2;
yA -= bobA; yB -= bobB;
if (yA == 100) bobA = 0;  if (yB == 0x90) bobB = 0;  if (yA == 0x61) bobA = -1;  if (yB == 0x8d) bobB = -1;
if (rand() % 22 == 1 && yA == 100) bobA = 1;   if (rand() % 22 == 1 && yB == 0x90) bobB = 1;
Draw(1, xA, yA); Draw(1, xB, yB);
if (xA > 99 && xB > 0x55) wait++;
if (wait > 150) { g_Scene = 2; vxA = 1; vyA = 1; wait = 0; vxB = 1; vyB = 1; 0x5c = 0; cStep = 0; }
```
(bob: y goes 100 → 97 at -1/frame... sign: `y -= bob`, bob = 1 moves up until y == 97 then bob = -1 back
down to 100, then 0.) Comparisons signed.

**Scene 2 — pull up and away** (K = 3): first frame `Intro_PlaySample(1)` (flag local_dc).
step: `xA -= vxA; yA -= vyA; xB -= vxB; yB -= vyB; sprA = min(sprA+1, 21); if (sprA > 4) sprB = min(sprB+1, 21);
if (sprA > 4) { vxA++; vxB++; } if (vyA < 5) vyA++; if (sprA > 4 && vyB < 5) vyB++;`
then `if (sprA < 20) { Draw(sprA, xA, yA); Draw(sprB, xB, yB); } else { wait++; if (wait > 50) { sprA = 23;
xA = -96; yA = 100; vyA = 0; g_Scene = 3; 0x5c = 0; wait = 0; } cStep = 0; }`.
Non-step frames draw both. **[corr]** x *decreases* (jets move left, i.e. away into the scrolling
background). The 50 frames of `wait` run at 1 per frame (step every frame once wait > 0).

**Scene 3 — single jet banks towards the viewer** (K = 1): first frame `Intro_PlaySample(2)`.
step: `xA += 3; if (sprA > 30 && xA > 80) yA--; if (xA % 5 == 1 && xA > 0) sprA = min(sprA+1, 34);`
(C remainder, signed) `if (xA > 300 && !s3) { s3 = 1; Intro_PlaySample(3); }`
`if (xA < 0x141) Draw(sprA, xA, yA); else { wait++; if (wait > 80) { g_Scene = 4; cStep = 0; wait = 0; xA = -96; yA = 100; sprA = 1; } }`.

**Scene 4 — fast pass** (K = 1): `if (!s4 && xA > 200) { Intro_PlaySample(4); s4 = 1; }` (checked before
the step) step: `xA += 4; if (xA < 0x141) Draw(1, xA, 100); else { wait++; if (wait > 30) { g_Scene = 5;
wait = 0; sprA = 0x33; xA = 0; yA = 100; np = 0; clear PX/PY/PS/PA[0..99]; } }`.

**Scene 5 — small jet with smoke trail** (K = 1): first frame `Intro_PlaySample(5)` (silent: no case for 5).
step:
```c
xA += 4;
if (xA > 100 && !emit) { emit = 1; ey = yA + 6; smokeSpr = 0; ex = xA; }
if (emit) for (k = 0; k < 4; k++) { ex += 4; if (np < 60 && ex < 320) { PX[np] = ex; PY[np] = ey; PS[np] = smokeSpr + 0x44; PA[np] = 0; np++; } }
off = (xA > 320);                                    /* local_d0, stays set into scene 6 */
if (xA < 0x191) { Draw(0x33, xA, yA); if (emit) UpdateSmoke(); }
else { wait++; if (wait > 50) { g_Scene = 6; wait = 0; xA = 0; yA = 0xbf; vx6 = 16; vyA = 0; sprA = 0x23; tx = 0x18; ty = 0x20; np = 0; emit = 0; } }
```
`UpdateSmoke()` (also used by scene 6; verified):
```c
for (j = np - 1; j > 0; j--) {          /* particle 0 is never drawn or updated (Q5) */
   Draw(PS[j], PX[j], PY[j]);
   PX[j]--; PY[j]--; PA[j] += off*3 + 1;
   if (rand() % 3 == 1 && PS[j] < 0x46) PS[j]++;           /* sprites 68..70 (0x44..0x46) */
   if (0x50 - np < PA[j]) { swap(PX[j],PX[np-1]); swap(PY..); swap(PS..); swap(PA..); np--; }
}
```
Non-step frames: draw the jet, and if `emit`, draw particles `np-1 .. 1` without updating. (The swap uses
the *current* np, so a just-swapped particle is not re-examined this frame.)

**Scene 6 — jet climbs out of the city** (K = 2): step:
```c
nx = xA + vx6; vx6 = max(vx6 - 1, 0);
ny = yA + vyA; vyA -= (ny < 0x96) ? 2 : 1;               /* [corr] accelerates upward faster above y 150 */
spr = sprA + 1;
if (spr == 0x32) {   /* → scene 7 */
   wait = 0; ovlStep = 1; g_LogoShown = 0; cCred = 0; g_Scene = 7; g_FrameOverlayActive = 1; cOvl = 0;
   l78 = xB(stale); l84 = 4; l88 = 2; l8c = 2; l90 = 0; l94 = 0; l98 = 0;   /* dead state, never drawn */
   xA = -64; yA = 0x50; l9c = 0x19; l7c = ny; l74 = nx; sprA = spr; yB = ny;
} else { sprA = spr; yA = ny; xA = nx; Draw(spr, nx, ny); }
dx = (xA + tx) - ex; dy = (yA + ty) - ey;                 /* tx = 0x18, ty = 0x20; ex/ey stale from scene 5 at first */
smokeSpr = min((sprA - 0x22)/3, 3);
x0 = ex; y0 = ey;
for (k = 1; k < 5; k++) { ex = x0 + (dx*k)/4; ey = y0 + (dy*k)/4;          /* C division, toward 0 */
   if (np < 60 && ex < 320) { PX[np]=ex; PY[np]=ey; PS[np]=smokeSpr+0x44; PA[np]=0; np++; } }
UpdateSmoke();                                            /* with off = value left by scene 5 (1) → age +4 */
```
Non-step frames: `Draw(sprA, xA, yA)` and draw particles np-1..1. On the transition step the smoke is
aimed at (-64+24, 80+32) (Q6, harmless: scene 7 never draws particles).

**Scene 7 — jet crosses with the logo explosion** (K = 2): step:
```c
/* dead bookkeeping (kept for RNG-free fidelity; no drawing): */
if (l74) { l94 = min(l94+1, 12); l74 += l84; l7c += l8c; if (l74 < -0x20) l74 = 0; }
if (l78) { l78 += l88; l88++; l80 += l90; l90++; l98 = min(l98+1, 12); if (l78 > 0x140) l78 = 0; }
xA += 3;
if (rand() % 3 == 1) { if (sprA < 0x32) sprA = 0x32; else if (sprA < 0x41) sprA++; }    /* sprites 50..65 */
if (wait == 0) Draw(sprA, xA, 0x50);
if (xA > 0x140 && ++wait > 50) { g_Scene = 1; wait = 0; xA = -96; yA = 100; xB = -120; yB = 0x90; sprA = 1; sprB = 1; bobA = 0; bobB = 0; }
```
Non-step frames draw the jet. Loop back to scene 1: the play-once flags (sample 0..5) are **not** reset, so
samples play only on the first cycle (and sound is shut down by the overlay anyway); `g_LogoShown` stays 1.

### 4.4 FRAME overlay (only after scene 6 → 7) (verified)
```c
if (cOvl < 3) cOvl++; else { cOvl = 0; if (++ovlStep > 9) g_FrameOverlayActive = 0; }
switch (ovlStep) {        /* x = scroll, chunky 320 wide, 0 transparent */
 case 1: blit FRAME1 at y 0x6f (h 21);  case 2: FRAME2 y 0x67 (40);  case 3: FRAME3 y 0x4c (86);
 case 4: FRAME4 y 0x2f (151);           case 5: FRAME5 y 0x18 (180);
 case 6: DrawLogo(); FRAME6 y 0x0c (203);  case 7: DrawLogo(); FRAME7 y 6 (217);  case 8: DrawLogo(); FRAME8 y 0 (240);
 default: DrawLogo(); g_LogoShown = 1; Intro_SoundShutdown();   /* only reached on the frame ovlStep becomes 10 */
}
```
Each step lasts 4 frames (cOvl 0..3). Note the switch runs after the increment, so on the frame where
`ovlStep` becomes 10 the default case runs once (logo + sound shutdown) and the overlay flag is already 0.
`Intro_SoundShutdown` stops all intro samples for good (later `Intro_PlaySample` calls are silent); the
CD music continues.

### 4.5 Credits (verified) **[corr]**
```c
if (!credOn) { if (cCred > 0x28) { if (++g_CreditIndex > 10) g_CreditIndex = 0; cCred = 0; credOn = 1; } }
else if (cCred < 200) { Intro_DrawCredits(scroll, pageOfs); cCred++; }
else { cCred = 0; credOn = 0; }
```
`cCred` is also incremented at the top of every frame, so a message is visible for **100 frames** (counter
+2 per frame: 0..199) and the gap is 41 frames (counter +1 per frame until > 40). The first message (index
0) starts at frame 1. Entering scene 7 resets `cCred = 0` (restarts the current message's timer).

### 4.6 Input — Intro_ReadKey 0x142c1 (verified)
Mouse branch only if `0x671f4` (never set). Then priority: `g_KeyReleased[1]` (Esc) → clear, -1;
`[0x20]` ('D') → clear, -1; `[0x48]` Up → 1; `[0x50]` Down → 2; `[0x4d]` Right → 4; `[0x4b]` Left → 3;
`[0x1c]` Enter → 5 (each clears its flag); else 0. Only -1 matters to the live loop; the others are consumed
(one per frame) and ignored. Kbd_ISR 0x10010: `g_KeyReleased[sc & 0x7f] = (sc & 0x80) ? 1 : 0` (set on
break, cleared on make).

---------------------------------------------------------------------------------------------------

## 5. Exit (verified)
After the loop: `Intro_SoundShutdown(); CD_Stop(); Pal_Fade(1)` (26 retraces). Then the code continues in
the fragment Ghidra calls `Intro_ExitWaitKey` 0x13475: `if (credOn) Intro_Cleanup()`; else (Esc during the
41-frame gap) it falls into a leftover menu loop (PLAYERS/GAMES/ABORT buttons, mouse, MAINMENU/BETWEEN/AOSET/
OPTIONS/PLANESEL .RAW screens, `Menu_*`, `Mouse_*`) whose only reachable exit is another Esc/'D' →
`Intro_Cleanup`. `Intro_Cleanup` 0x13f03: `Pal_Fade(1)` (again), text mode, sound shutdown, `CD_Stop`, restore
INT 9, free buffers, return 0. **Port: on skip just stop sound/CD, fade out (26 frames) and return** (Q7:
drop the dead menu).

---------------------------------------------------------------------------------------------------

## 6. Bugs and quirks

| # | Behaviour | Recommendation |
|---|---|---|
| Q1 | FRAME7 read overflows its buffer by 40 bytes | port: allocate the file size |
| Q2 | sprite 1 drawn from block+0 (4-px shift), others lose 4 trailing pixels | keep (faithful look) |
| Q3 | 3-line credit uses width 2 for line 3 | keep |
| Q4 | first frame shows the black page 1 | skip or keep (1 frame) |
| Q5 | smoke particle 0 never drawn/updated | keep |
| Q6 | stale emitter/aging state across scenes 5→6 | keep |
| Q7 | Esc in the credit gap → dead menu, needs a second Esc | port: exit immediately |
| Q8 | only Esc / 'D' (on release) skip, despite "PRESS ANY KEY" | port: any key/button (PORT note) |
| Q9 | `Intro_PlaySample(5)` silent; samples play only on the first cycle | keep |

## 7. Open questions
1. Whether the port should run the intro on every start (original: yes, via JS.BAT) — suggest a config flag.
2. Exact `Spr_Blit` handling of `skip/pad` when clipping (used only via Spr_BlitClip): verified for the
   argument values above; byte-exact planar behaviour belongs to the video spec of INTRO (not covered here).
3. `Mixer_Update` timing (4 polls per frame) only matters for the SB buffer refill; the SDL port ignores it.
