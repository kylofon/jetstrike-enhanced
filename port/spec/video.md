# Video subsystem (JS.EXE: mode X, pages, split HUD, palette, blitters, sprite queue, level renderer, text, primitives)

Target: `work/JS.bin` (flat, base 0x10000). Decompile: `port/decomp/js.c`. Symbols: `port/spec/video_symbols.csv`.
Related formats: `port/formats/gfx.md` (PAX/SPX/fonts), `port/formats/maps.md` (TLX, DX0/P00). This spec does not
repeat file layouts; it specifies what the code does with them.

Confidence tags: **verified** (read instruction by instruction in a capstone disassembly), **likely**, **guess**.
All the low-level routines 0x10010-0x10760 and 0x31604-0x31a30 are hand-written asm (no `__CHK`, `cld`, register
use). Ghidra's C for them is wrong in places (plane rotation, carry tricks); the descriptions below are from the
disassembly. All float code (Level_DrawBackground, Pal_Fade, Pal_CycleEffects, Pal_NightAltitude, Pal_SaveNight)
was read from the disassembly with the Watcom soft-float register convention:
`__I4D` int32 EAX -> double EDX:EAX, `__U4D` uint32 -> double, `__FDD` EDX:EAX / ECX:EBX, `__FDM` multiply,
`__FDA` add, `__FDS` EDX:EAX - ECX:EBX, `__FDI4` / `__FDU4` double -> int32/uint32 truncating toward zero,
`__FSFD` float->double, `__FDFS` double->float (round to nearest). Port: plain IEEE `double` in the same order.

## 0. Summary of the hardware model (what the port has to emulate)

* Mode X 320x240, 60 Hz (misc 0xE3 = 25.175 MHz clock, 480-line timing, vertical total 0x20D; double scan from
  mode 13h). **Not 70 Hz** (verified from the CRTC values; the RE_GUIDE "70 Hz" is wrong).
* VRAM = 4 planes x 64 KiB. Every routine addresses it as `byte offset = y*96 + (x>>2)`, plane `x&3`.
  96 bytes per row = **384-pixel virtual width**, 682 rows. Treat it as one linear 8-bit buffer:

  ```c
  uint8_t vram[0x40000];                  /* pixel index = byteoffset*4 + plane                 */
  #define VIDX(pageofs, x, y)  ((int32_t)(pageofs)*4 + (int32_t)(y)*384 + (int32_t)(x))
  ```
  Because the plane advance in all blitters is "rotate mask; on wrap from plane 3 to plane 0 add 1 to the byte
  offset", **pixel x+1 is always index+1**, also across the 384 boundary (it lands on the next row, column 0) and
  for negative x (previous row). The linear index model reproduces every wrap the original produces. Writes outside
  `0..0x3FFFF` did not reach VRAM on the PC (they went to 0xB0000.. or below 0xA0000): PORT drop them.
* VRAM map (byte offsets, rows of 96 bytes):

  | offset | rows | content |
  |---|---|---|
  | 0x0000-0x18BF | 0..65 | HUD panel `display.pax` 320x66 (Pic_LoadHudPanel). Rows 0..64 visible below the split |
  | 0x18C0 | 66.. | **page A** (g_BackPage value 0x18C0). Level renderer writes rows 66..273 (13 tile rows) |
  | 0x78C0 | 322.. | **page B** (0x78C0). Rows 322..529 |
  | 0xDA40 | 582..608 | off-screen save area for the radar background (Hud_DrawPanel copies, Hud_UpdateRadar restores) |
  | page*0x5A00 | page*240.. | menu/picture pages (Pic_LoadPax draws at row page*240+20) |

* Display (CRTC): start address S (byte offset), pel pan P (0..3 pixels), line compare L.
  In game: `S = g_BackPage(shown) + 0x600 + fineY*96 + (fineX>>2)`, `P = fineX & 3`, L = 0xAF -> playfield is
  **175 lines** taken from S, then **65 lines** taken from offset 0 with pan 0 (the attribute mode register is
  0x61: bit 5 "pixel panning mode" resets panning after the line-compare match). Menus: L = 400 (never matches),
  S = row 20 (or page*0x5A00), 240 lines.
* Palette: `g_Palette` 0x82ED8 (768 B, 6-bit) is a *shadow*; hardware DAC only changes on `Pal_Upload`,
  `Pal_Black`, `Pal_SetColor*` (upload variants) and Video_SetModeX. They diverge on purpose (Pal_Fade restores
  g_Palette but leaves the DAC dark). The port needs **two** palettes: `g_Palette[768]` and `dac[768]`.
* Time: every vsync wait is a frame boundary. Present the 320x240 image from (vram, S, P, L, dac) at each wait.

## 1. Data structures and globals

```c
/* 0x80008 */ uint8_t  g_SpriteColorOfs;   /* = 0x40, added to every SPX pixel at load, verified (image byte) */
/* 0x80009 */ uint8_t *g_SmallFont;        /* unaligned pointer (smallfnt.raw), Fonts_Load              */
/* 0x8000D */ uint8_t *g_BigFont;          /* unaligned pointer (bigfnt.raw)                             */
/* 0x80011 */ char    *g_SmallCharset;     /* -> 0x80BF0 "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!?(),.-"   */
/* 0x80015 */ char    *g_BigCharset;       /* -> 0x80C1C "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!?"        */
/* 0x80019 */ uint8_t  g_DigitFont[50];    /* 3x5 digits, 5 rows/digit: 7,5,5,5,7 | 2,2,2,2,2 | 7,1,7,4,7 |
                                              7,1,3,1,7 | 5,5,7,1,1 | 7,4,7,1,7 | 7,4,7,5,7 | 7,1,1,1,1 |
                                              7,5,7,5,7 | 7,5,7,1,7   (bit2 = left column)              */
/* 0x8004B */ uint8_t  g_PicFullLoad;      /* 1: Pic_LoadPax clears the page + loads the .pal (default 0 in image,
                                              set to 1 by the game; 0 = pixels only)                         */
/* 0x8004F */ uint8_t  g_FadeTargetColor;  /* 0 = fade to black, n = fade toward palette entry n (byte)      */
/* 0x80050 */ uint8_t *g_ParallaxBuf;      /* malloc(0x280C8): 320x512 backdrop (+200 slack bytes)           */
/* 0x80054 */ int32_t  g_VSyncWaits;       /* extra vsyncs per Video_FlipPage (0/1/2, benchmark)             */
/* 0x80058 */ uint8_t *g_TileData;         /* malloc(0x10000): 256 tiles x 256 B                              */
/* 0x8005C */ int32_t  g_BackPage;         /* 0x18C0 or 0x78C0: page being drawn                             */
/* 0x8006C */ uint32_t g_LastFadeType;     /* Pal_Fade does nothing if called with the same type twice       */
/* 0x80070 */ int32_t  g_NightPalActive;   /* night lighting on: small font colour 254, altitude palette     */
/* 0x80428 */ uint8_t  g_LineMaskL[4] = {0x0F,0x0E,0x0C,0x08};
/* 0x8042C */ uint8_t  g_LineMaskR[4] = {0x01,0x03,0x07,0x0F};
/* 0x82ED8 */ uint8_t  g_Palette[768];
/* 0x831D8 */ uint8_t  g_TileWindow[16][24];/* tile ids for the current frame (only rows 0..12 used)         */
/* 0x83A54 */ uint8_t *g_SpriteTab[];      /* 1-based: sprite id n at 0x83A54+4n; ids 1..0x212 valid        */
/* 0x84500 */ int32_t  g_SpriteQueueCount;
/* 0x84504 */ int32_t  g_LineColor;        /* Video_SetLineColor / Video_DrawLineColor                        */
/* 0x8452E */ int16_t  g_ShownPage;        /* Video_ShowPage                                                  */
/* 0x845B8 */ uint8_t *g_TilePtrs[256];    /* g_TileData + t*256                                              */
/* 0x9031C */ int32_t  g_FlashCounter;     /* night flash state (explosions / lightning), Pal_CycleEffects  */
/* 0x90650 */ int32_t  g_NightLevel;       /* 0..15 altitude light level                                      */
/* 0x9065C */ int32_t  g_NightLevelShown;  /* last applied level (-2 = force)                                 */
/* 0x905C4 */ int32_t  g_ScrollFineX;      /* = g_CamX & 15  (was g_ShakeX: it is the fine scroll, not shake) */
/* 0x905C8 */ int32_t  g_ScrollFineY;      /* = g_CamY & 15  (was g_ShakeY)                                   */
/* 0x90FE4 */ int32_t  g_FlashTable[];     /* GENDATA.DAX colour rows; entry c used at index c (rows of 10 from 0x90FE8) */
/* 0x92AD4 */ uint8_t  g_ParallaxPal[48];  /* copy of colours 192..207 as loaded                              */
/* 0x933B4 */ uint8_t  g_PalSaved[768];    /* Pal_SaveNight / Pal_Restore                                    */
/* 0x8EC88 */ int32_t  g_NightGreyTab[16][16]; /* written by Pal_SaveNight, never read (dead)               */
```

Sprite draw queue entry (0x81AD8, stride 20, **no bound check**; 256 entries reach g_Palette at 0x82ED8):

```c
struct SprQ {            /* verified */
  int32_t mirror;        /* +0x00  1 if id > 0x8000                                 */
  int32_t wrap;          /* +0x04  1 -> drawn at x+384                               */
  int32_t x;             /* +0x08  x + g_ScrollFineX  (page pixel column)            */
  int32_t y;             /* +0x0C  y + 16 + g_ScrollFineY (page pixel row)           */
  uint8_t *spr;          /* +0x10  sprite header pointer                             */
};
```

Sprite header (SPX entry, `gfx.md` §3): `u16 hotX, u16 hotY, u16 w4, u16 h`, then 4 planes of `h` rows x `w4`
bytes. The blitters read **only the low byte** of w4 and h (`mov ah,[esi]`; `mov al,[esi+2]`).

## 2. Palette layout 0-255 (in game)

| index | content | written by |
|---|---|---|
| 0..63 | HUD panel `display.pal` (title/menu pictures use the same range with their own .pal) | Pic_LoadHudPanel / Pic_LoadPax: `byte>>2`, R/B swapped |
| 0x1E, 0x1F | radar blips, re-set every frame by Hud_UpdateRadar (0x1F grey pulse from 0x8038C, 0x1E red from 0x80384) | Pal_SetColor |
| 64 (0x40) | sprite transparency key (never drawn by a sprite). Night + detail off: the flash table is played on it | Pal_SetColor12 (Pal_CycleEffects) |
| 64..95 | sprite colours (jetsprit.pal, 32 x 6-bit raw, no shift) | Sprites_LoadSpx (2nd call) |
| 64..79 | at night recomputed from g_PalSaved by altitude (Pal_NightAltitude) | |
| 96..127 | copy of 64..95: target of Sprite_BlitShift (+0x20). Used by the HUD icons (Sprite_DrawNowShift) so they are **not** affected by the night lighting of 64..79 | Sprites_LoadSpx |
| 128..191 | tileset .pal (raw 6-bit, 0xC0 bytes). Tiles use 128..143 only. 128 = sky (tile pixel 0x80 = "transparent" with detail on) | Tileset_LoadTlx |
| 128..143 | night: altitude lit | Pal_NightAltitude |
| 192..207 | parallax backdrop (P00/P01/P10, `u32[i]>>2`, i<48) | Parallax_Load |
| 196..211 | detail off: plain sky 12-bit 0x4AF (day) / 0x00A (night). Lightning (detail on) sets them to 63,63,63 | Parallax_Load, lightning |
| 192..207 | night + detail on: flash ramp (Pal_CycleEffects), altitude lit (Pal_NightAltitude) | |
| 208..253 | unused (208..211 can be left white after a lightning, see quirk Q7) | |
| 254 | night small-font colour (0x30,0,0 dark red) | Pal_SaveNight |
| 255 | text white (63,63,63) | Fonts_Load |

Parallax pixels are stored `file - 0x40` (byte arithmetic): file 1..15 -> 0xC1..0xCF. Sprite pixels are stored
`file + 0x40`: file 0 -> 0x40 (transparent), 1..31 -> 65..95.

## 3. Low-level asm routines (0x10010-0x10760)

### Video_SetModeX @ 0x10010 — `void Video_SetModeX(void)` (verified)
1. `INT 10h AX=0x0013`.
2. DAC: `out 3C8,0`, then 0x300 x `out 3C9,0` (whole DAC black; g_Palette untouched).
3. Seq 0x0604 (chain-4 off), 0x0F02 (all planes). GC 0x4005 (write mode 0, read mode 0, 256-colour shift), 0x0506.
4. CRTC 0x3013 (offset 0x30 = 96 B/row), 0x0014, 0xE317, misc 0x3C2=0xE3, 0x0616, 0xE715, 0xDF12, 0x2A11,
   0xEA10, 0x3E07, 0x0D06.
5. `in 3DA`; attr 0x30 <- 0x61 (index 0x10 with PAS; mode = graphics, PPM, 8-bit).
6. Clear 0x10000 bytes at 0xA0000 with map mask 0x0F = **all 256 KiB zero**.
Port: `memset(vram,0)`, `memset(dac,0)`, start=0, pan=0, split=off (CRTC 0x18 from mode 13h = 0x3FF/0x1FF -> never).

### Video_SetTextMode @ 0x100C2 (verified) `INT 10h` with AX as left by the caller (exit path). Port: no-op.

### Video_PutPixel @ 0x100D0 — `void Video_PutPixel(uint32 x, int32 y, uint8 col)` (verified)
`vram[VIDX(0, x, y)] = col`. Absolute VRAM coordinates (no page). `x>>2` is unsigned.

### Video_WaitVSync @ 0x10108 — `int Video_WaitVSync(void)` (verified)
Wait while 3DA bit 3 set, then wait until set (= start of the next vertical retrace). Returns 0.
Port: **present one frame** (see §10) and sleep to the next 1/59.94 s tick.

### Sprite_Blit @ 0x10128 — `void Sprite_Blit(uint32 x, uint32 y, SprHdr *s, int32 pageofs)` (verified)
```
dx = x - s->hotX; if (borrow) return;          /* unsigned: x < hotX -> nothing drawn   */
dy = y - s->hotY; if (borrow) return;
w4 = (u8)s->w4; h = (u8)s->h; src = (u8*)s + 8;
for p in 0..3:                                   /* source plane p */
  for r in 0..h-1: for c in 0..w4-1:
     v = *src++; if (v != 0x40) vram[VIDX(pageofs, dx + 4*c + p, dy + r)] = v;
```
(The hardware loop starts at map mask `0x11 << (dx&3)` and rotates; equivalent to the above.) No right/bottom
clipping: columns >= 384 continue on the next row.

### Sprite_BlitShift @ 0x101C8 (verified)
Identical to Sprite_Blit but stores `(u8)(v + 0x20)`.

### Sprite_BlitMirror @ 0x1026C — `void Sprite_BlitMirror(int32 x, int32 y, SprHdr *s, int32 pageofs)` (verified)
**No hotspot guard.** `left = x - (w4*4 - hotX)` (all 32-bit, `w4` here is the full u16 field), `top = y - hotY`
(signed; `sar` for x>>2). Source pixel (sx, r) goes to `(left + (4*w4-1-sx), top + r)`:
```
for p in 0..3: for r: for c: v = plane[3-p][r][w4-1-c]; if (v != 0x40) vram[VIDX(pageofs, left+4c+p, top+r)] = v;
```
Equivalent to a horizontal flip of the whole sprite. Negative `top` writes above the page base (Q3).

### Unused asm: 0x1032C, 0x1039C, 0x10739 (no callers, verified by a call scan)
0x1032C copies a rectangle VRAM -> memory with GC write mode 1 (latch read, not a real save on PC hardware);
0x1039C copies memory -> VRAM with write mode 1; 0x10739 is a fragment computing a pixel address. Do not port.

### Tiles_DrawColumns @ 0x10540 — `void Tiles_DrawColumns(u8 *ids, u8 **tileptrs, int pagesel, int plane)` (verified)
Draws **24 x 13 tiles** (0x138 = 312 ids read sequentially from `ids`) into
`base = 0x18C0 + pagesel*0xC00` (pagesel 0 -> 0x18C0, 8 -> 0x78C0), for the one plane `plane` selected by the
caller's map mask. Tile k (k = 0..311): `tx = k % 24`, `ty = k / 24`; for row r 0..15 copy the dword
`tileptrs[ids[k]] + plane*64 + r*4` to byte offset `base + (ty*16 + r)*96 + tx*4`.
Linear meaning: page pixel `(16*tx + 4*j + plane, 16*ty + r)` = `tile[plane*64 + r*4 + j]`, i.e.
**tile pixel (x,y) = tile[(x&3)*64 + y*4 + (x>>2)]**. Page columns 0..383, rows 0..207 fully overwritten.

### Tiles_DrawColumnsParallax @ 0x1040C + Tiles_ParallaxRow @ 0x104F8 (verified)
`void Tiles_DrawColumnsParallax(u8 *ids, u8 **tileptrs, int pagesel, int plane, u8 *par, int parofs, int unused)`
* 22 x 13 tiles (0x11E = 286): per tile row it reads 22 ids then **skips 2** (`ids += 2`), so window columns
  22, 23 are never drawn: page columns 352..383 keep stale data (invisible, Q5).
* `p = par + parofs + plane` (+ `(pagesel & 7)*32*320` = 0). For each destination byte (4 per tile row per plane):
  `v = tilebyte; if (v == 0x80) v = *p; store; p += 4` and `p += 0x134` after the 4th (= next backdrop row).
  After 16 rows `p -= 0x13F0` (net +16 columns); after 22 tiles `p += 0x1400 - 0x160` (next tile row).
* Linear meaning: page pixel (c, r), c 0..351, r 0..207: `t = tile pixel; out = (t == 0x80) ? par[parofs + r*320 + c] : t`.
  **No horizontal wrap** of the 320-wide backdrop: `parofs%320 + c >= 320` reads the next backdrop line (Q4).
* Arg 7 is negated and +0x140 on entry, never used.

### Video_SelectPlane @ 0x105D9 — `(int plane)`: map mask `1 << plane` (verified). Port: implicit.

### Video_SetSplitLine @ 0x105EF — `void Video_SetSplitLine(int16 rows)` (verified)
`lc = rows*2 - 1` (16-bit); CRTC 0x18 = lc&0xFF; reg 0x07 bit 4 = lc bit 8; reg 0x09 bit 6 = lc bit 9
(read-modify-write). Port: `split_rows = (lc < 480) ? (lc+1)/2 : 240` -> 0xAF gives 175, 400 gives "off".
(likely: with double scan the first HUD line is display line 175.)

### Video_BlitLinearToPlanar @ 0x1063A — `(u8 *src, uint32 x, int32 y, uint32 w, int32 h)` (verified)
For p in 0..3 (starting plane x&3, rotating with byte carry): for row in 0..h-1, for c in 0..(w>>2)-1:
dest byte `y*96 + (x>>2) + row*96 + c` (+1 after the plane wrap) <- `src[p + row*w' + 4c]` where the source
pointer simply advances by 4 per byte and is **not** re-aligned per row: source index = `p + 4*(row*(w>>2) + c)`.
For w multiple of 4 this is `src[row*w + 4c + p]`. Linear meaning: `vram[VIDX(0,x+i,y+row)] = src[row*w+i]`.
No page argument: callers pass absolute rows.

### Video_SetStartAndPan @ 0x106B0 — `void Video_SetStartAndPan(uint32 x, int32 y, int32 base)` (verified)
`start = y*96 + (x>>2) + base`; CRTC 0x0C = start>>8, 0x0D = start&0xFF; `cli`; wait until not in retrace, then
until retrace; `sti`; attr 0x33 (index 0x13|0x20) = `(x&3)*2`. Port: `crtc.start = start & 0xFFFF;
crtc.pan = x & 3;` then one vsync (present).

### Video_ReadPixel @ 0x106FA — `int Video_ReadPixel(int32 x, int32 y, int32 base)` (verified)
`v = vram[VIDX(base, x, y)]` (signed `sar` for x); returns `v - 0x80` if `0x80 <= v <= 0xC0`, else 0.
Used by game logic as a **collision probe on the drawn back page** (callers 0x14ce7, 0x14d2e, 0x29333, 0x293a5,
0x3d3c4, 0x3d431, 0x44577): returns a tile colour index 0..64 or 0 for sky/sprite/parallax. Therefore the
port's VRAM must be bit-exact at the moment of the call (it reads what the frame drew so far).

## 4. Pages, flip, split HUD

### Video_FlipPage @ 0x13A68 — `void Video_FlipPage(void)` (verified)
```
Video_SetStartAndPan(g_ScrollFineX, g_ScrollFineY, g_BackPage + 0x600);   /* 1 vsync */
for (i = 0; i < g_VSyncWaits; i++) Video_WaitVSync();
if (g_FlashCounter != 0) Pal_CycleEffects();
g_BackPage = (g_BackPage == 0x18C0) ? 0x78C0 : 0x18C0;
```
So a game frame lasts `1 + g_VSyncWaits` retraces at 59.94 Hz. The shown window starts at page row 16+fineY,
column fineX: **screen (sx, sy) shows page pixel (sx + fineX, sy + 16 + fineY)**.

### Video_ShowPage @ 0x12E92 — `(int p)`: CRTC start = p*0x5A00 (pan unchanged), g_ShownPage = p (verified).
### Video_GetPage @ 0x12EF0: returns g_ShownPage.

### Pic_LoadHudPanel @ 0x130C8 (files spec; video effects only) (verified)
Clears all VRAM, blits display.pax 320x66 at offset 0, loads 64 colours (R/B swap, >>2) into g_Palette 0..63
(**no upload**), `Video_SetSplitLine(0xAF)`, CRTC start = 0x1EC0 (= 0x18C0 + 0x600).

### Video_BenchmarkSpeed @ 0x143D3 (verified), Video_CountVSync @ 0x31A19, Tiles_ParallaxBench @ 0x3184C, Tiles_ClearColumns @ 0x3197C
```
n = 0; WaitVSync(); prev = CountVSync();
for k = 1..13: WaitVSync();
   repeat k times: 4 x (detail ? Tiles_ParallaxBench(0,0,8,0,0,0) : Tiles_ClearColumns(0,0,8,0));
   c = CountVSync(); if (prev < c) n++; prev = c;
g_VSyncWaits = n==0 ? 2 : n==1 ? 1 : 0;
```
CountVSync = number of `in 3DA` polls from the end of the current retrace to the start of the next one.
Tiles_ClearColumns copies 24x24 tiles worth of dwords from linear address 0 into page B (garbage, overwritten
later); Tiles_ParallaxBench is a copy of the parallax loop. A fast machine ends with n = 0 -> **2 extra waits =
20 fps**; a slow one runs uncapped. PORT: do not benchmark; set g_VSyncWaits from config, default 2 (see Q-open 1).
The bench writes into page B: the port may skip it (page B is fully redrawn before it is shown).

## 5. Sprite queue

### Sprite_ClearQueue @ 0x10E70: `g_SpriteQueueCount = 0` (verified). Called by Game_Run 0x1d0e1 each frame after Level_DrawBackground, and main.
### Sprite_Mirror @ 0x1206E: returns `id + 0x8000`.

### Sprite_Queue @ 0x10F11 — `void Sprite_Queue(int32 x, int32 y, int32 id)` (verified)
```
if (id == 0) return;
q = &g_SpriteQueue[g_SpriteQueueCount];
q->x = x + g_ScrollFineX;
q->y = y + 16 + g_ScrollFineY;
if (id < 0x8001) { q->mirror = 0; q->spr = g_SpriteTab[id]; }
else             { q->mirror = 1; q->spr = g_SpriteTab[id - 0x8000]; }   /* 0x63A54+id*4 wraps to 0x83A54+(id-0x8000)*4 */
hx = Sprite_GetX(id);            /* 0 for mirrored ids (id > 0x212) */
q->wrap = (q->x - (hx & 0xFFFF) < 0);
g_SpriteQueueCount++;            /* no limit */
```
x, y are screen coordinates (0,0 = top-left of the visible playfield).

### Sprite_DrawQueue @ 0x1155E — `void Sprite_DrawQueue(void)` (verified)
Iterates `i = count-1 .. 0` (**last queued is drawn first, so the first queued sprite ends on top**):
`xx = q->x + (q->wrap ? 0x180 : 0)`; `q->mirror ? Sprite_BlitMirror(xx, q->y, q->spr, g_BackPage)
: Sprite_Blit(xx, q->y, q->spr, g_BackPage)`. It does not reset the count.
The +384 trick: a sprite whose left edge is left of page column 0 is drawn at column `left+384` (invisible right
margin) and the part past column 383 continues at column 0 of the **next row**, i.e. it appears at the correct x
but **1 pixel lower** (Q2). Mirrored sprites only wrap when `x < 0` (hotspot ignored) — a mirrored sprite with
`0 <= x < 4*w4-hotX` has a negative left and is drawn through the previous-row wrap instead: correct x, **1 pixel
higher** for the clipped part (Q2).

### Sprite_DrawNow @ 0x1102D `(x, y, id)`: `Sprite_Blit(x, y, g_SpriteTab[id], 0)` (absolute VRAM, used by HUD/menus).
### Sprite_DrawNowShift @ 0x11066 `(x, y, id)`: `Sprite_BlitShift(x, y, g_SpriteTab[id], 0)`.
### Sprite_GetX/GetY/GetWidth/GetHeight @ 0x11847 / 0x117F8 / 0x11895 / 0x118E7 `(id)`
Return 0 if `id == 0 || id > 0x212`, else hotX / hotY / `w4*4` / h (16-bit fields). (verified)

## 6. Level renderer

### Level_DrawBackground @ 0x13842 — `void Level_DrawBackground(int32 col, int32 row)` (verified)
Called by Game_Run 0x1cf91 with `col = Div16(g_CamX)+1`, `row = Div16(camYclamped)+1` (also 0x20f07, 0x3045f).
```
grid = g_MapGrid + 4;
if (row < 2) row = 2;
for j 0..15: for i 0..23:
    g_TileWindow[j][i] = grid[g_MapWidth*(row + j - 1) + (col + i) % g_MapWidth];   /* C '%' (idiv): negative col gives negative index */
/* backdrop origin (only used with detail on, always computed) */
px = (int)((double)g_CamX / 4.166666666) % 320 - g_ScrollFineX;            /* const 0x4010AAAAAA9F36A3 */
py = (int)((double)(g_CamY + 0x7D8) / 9.25 - (double)g_ScrollFineY);        /* __FDD then __FDS then __FDI4 */
pagesel = (g_BackPage == 0x18C0) ? 0 : 8;
for plane 0..3:
    Video_SelectPlane(plane);
    if (g_DetailParallax /* word 0x84536 */)
         Tiles_DrawColumnsParallax(g_TileWindow, g_TilePtrs, pagesel, plane, g_ParallaxBuf, py*320 + px, px);
    else Tiles_DrawColumns(g_TileWindow, g_TilePtrs, pagesel, plane);
```
Truncations: `__FDI4` toward zero, then C `%` (sign of dividend). Only rows 0..12 of g_TileWindow are drawn.
Composition (world <-> screen): page (0,0) = world tile (col, row-1), so with camX, camY >= 16 the visible screen
(sx, sy) = world `(16*col + fineX + sx - 16 ... )` = **world (g_CamX + 16 + sx, g_CamY + 16 + sy)** (likely; it
depends on the caller's col/row rounding; for camY < 16 the row clamp shifts it).
Screen shake: there is none in this function; the former "g_ShakeX/Y" are the fine scroll. (A separate shake, if
any, would have to modify g_CamX/Y; not found.)
Backdrop rows read: `py .. py+207`; py max with camY <= 0x340 is 307, so rows up to 514 are read (2 past the
buffer end, inside the 200-byte slack only for the first 200 bytes; rows 512-514 are never on screen because the
visible rows are 16+fineY .. 190+fineY). Negative py/px (camY near -2000) read before the buffer (Q4).

### Detail off vs on
* Off: tiles verbatim, 0x80 pixels show colour 128 (tileset entry 0). Parallax_Load sets 196..211 to sky colour.
* On: 0x80 pixels show the backdrop at 0.24x horizontal / 0.108x vertical camera speed.

### Video_Fill4x4 @ 0x145F9 `(x, y, tile)` / Level_DrawOverviewMap @ 0x14677 `(col, row)` (verified, debug only, caller 0x401da)
Fill4x4: for i 0..3, j 0..3: `PutPixel(x+i, y+j, g_TilePtrs[tile][j*4 + i])` (= tile pixels (4i, j), plane 0 rows 0..3).
OverviewMap: for c 0..79, r 0..43: `Fill4x4(c*4, r*4 + 100, Map_GetTile(col + c, row + r))` (absolute VRAM rows).

## 7. Palette routines

* **Pal_SetColor @ 0x11145** `(idx, r, g, b)`: g_Palette[3idx..] = r,g,b; `Pal_Upload(idx, idx+1)`. (verified)
* **Pal_SetColorNoUpload @ 0x1119E**: same without upload.
* **Pal_SetColor12 @ 0x111E6** `(idx, rgb)`: r = ((rgb>>8)&15)<<2, g = ((rgb>>4)&15)<<2, b = (rgb&15)<<2; upload idx.
* **Pal_GetColor12 @ 0x1125C** `(idx)`: returns `(r*16 + g)*16 + b` of the **6-bit** values (not >>2; overlapping
  nibbles, Q8). Only caller: Pal_SaveNight (dead table).
* **Pal_Black @ 0x1195B** `(first, last)`: DAC entries [first,last) := 0 (g_Palette unchanged). Caller: Pic_LoadPax (0,0x40).
* **Pal_Upload @ 0x119B6** `(first, last)`: DAC[first..last) := g_Palette (6-bit). **Pal_UploadAll @ 0x120EA** = (0,256).

### Pal_Fade @ 0x13B01 — `void Pal_Fade(int first, int last, uint32 type, int n)` (verified)
```
if (type == g_LastFadeType) return;            /* !! repeated type is ignored */
g_LastFadeType = type;
saved = copy of g_Palette (768);
switch (type):
 0: for s = 0..n-1:            k = (1.0/n) * (double)(n - s);
 1: for s = 0..n-1:            k = (1.0/n) * (double)s;
 2: N=2n; for s = 0..N/2-1:    k = (1.0/N) * (double)(N - s);     /* 1.0 .. 0.5+  (half fade out) */
 3: N=2n; for s = N/2..N-1:    k = (1.0/N) * (double)s;           /* 0.5 .. 1-    (half fade in)  */
 each step:
   for i = 3*first .. 3*last-1:
     if (g_FadeTargetColor == 0) g_Palette[i] = (uint32)((double)(uint32)saved[i] * k);
     else { t = saved[3*g_FadeTargetColor + i%3];
            g_Palette[i] = (uint32)((double)(int32)(saved[i] - t) * k + (double)(uint32)t); }
   Pal_Upload(first, last); Video_WaitVSync();
g_Palette = saved;                               /* always restored */
if (type & 1) Pal_Upload(first, last);           /* fade-in ends at full colour */
```
`1.0/n` is computed first (`__FDD(1.0, n)`), then multiplied by the step count, then by the colour; conversion
`__FDU4` truncates. type > 3: no steps, but the palette copy/restore and the odd-type upload still happen.
Fade-out leaves the **DAC** black while g_Palette is intact (the next Pal_Upload/Pic_LoadPax shows the picture).
There are 31 callers; e.g. Hud_DrawPanel ends with `Pal_Fade(0,0x100,1,0x20)`; plane select uses
`g_FadeTargetColor=6; Pal_Fade(0x40,0x80,0,0x10)` (sprite colours fade toward picture colour 6).

### Pal_CycleEffects @ 0x45C09 — night flashes, called from Video_FlipPage when g_FlashCounter != 0 (verified)
```
if (g_NightMission == 0) return;
c = ++g_FlashCounter;
if (g_DetailParallax == 0) {
    Pal_SetColor12(0x40, g_FlashTable[c]);                 /* int at 0x90FE4 + 4c */
    if (g_FlashTable[c] == 0) g_FlashCounter = 0;          /* (+ an unreachable restore of 192..207) */
} else if (c <= 99) {
    g_FlashCounter = 0x65;                                 /* jump to the sky flash */
} else {
    for i 0..15, comp in R,G,B: p = g_ParallaxPal[3i+comp];
      if (c <= 104) v = (int)((double)(63 - p) * 0.25 * (double)(c - 100) + (double)p);
      else          v = (int)(63.0 - (double)(63 - p) * 0.25 * (double)(c - 104));
      (R, G, B computed in that order, then Pal_SetColor(0xC0 + i, R, G, B): 16 uploads)
    if (g_FlashCounter == 0x6C) g_FlashCounter = 0;        /* checked twice, same effect */
}
```
Sequence after a trigger with 0x65: frames c = 102..108: 192..207 ramp to white (c=104) and back to the
backdrop colours (c=108), then stop. Triggers: Explosion_Damage (0x39b69 area, js.c line ~20953):
night && dmg > 99 -> `g_FlashCounter = min((dmg/1000)*10 + 1, 21)` (detail off: plays table row dmg/1000 on
colour 64; detail on: becomes the sky flash); lightning (§7.1).

### 7.1 Lightning (inside Game_Run, 0x1e1a0-0x1e370) (verified for the call order)
Condition each frame: `(g_NightMission && cooldown(0x8FFB0)==0 && Rand(1000)==1) || (g_KeyDown[0x3D] /*F3, 0x83995*/ && cooldown==0)`.
```
cooldown = 50;                                            /* decremented once per frame elsewhere */
Sfx_Play(0x12, 0x157C, 0x3F, g_CamX + g_PlayerScrX);     /* 0x315bc */
bx = Rand(200) + 44;  by = g_BackPage / 96;               /* by = 66 or 322: absolute VRAM row of the page top */
loop { r = Rand(40); if (r + 60 <= by) break;
       ny = by + 20 + Rand(41);
       a = Rand(20); b = Rand(20); nx = Clamp(bx + a - b, 0, 320);
       Video_DrawLine(bx, by, nx, ny, 0xFF); bx = nx; by = ny; }
if (!g_DetailParallax) { Pal_SetColor12(0x40, g_FlashTable[g_FlashCounter]); g_FlashCounter = 0x15; }
else { for i 0xC4..0xD3: Pal_SetColor(i, 63, 63, 63); g_FlashCounter = 0x65; }
```
Rand(n) = 0x10d65 (`rand() % (n+1)`). Because `by` starts at the page's absolute row, on page B (322) the loop
exits at once: **the bolt is only drawn when the back page is A** (Q6). The bolt is drawn in VRAM rows 66..~170,
i.e. page rows 0..~104 (top of the playfield). By code address the frame order in Game_Run is
Level_DrawBackground (0x1cf91) -> Sprite_ClearQueue (0x1d0e1) -> ... -> lightning (0x1e1b8..) -> Sprite_DrawQueue
(0x1e3fa), so the bolt lies on the background under the sprites (likely; one loop body).

### Pal_NightAltitude @ 0x460BA (was FUN_000460ba) (verified)
Callers: Game_Run 0x1cf61 when `g_NightPalActive && g_NightLevel != g_NightLevelShown` with
`g_NightLevel = Clamp((g_CamY + 150)/67 + 1, 0, 15)`; 0x1ae40 (same formula).
```
g_NightLevelShown = g_NightLevel;
for i 0..15:
  for comp: p = g_PalSaved[0x0C0 + 3i + comp]; v = (int)(p + (double)(63-p) * (1.0/21) * (double)g_NightLevel)  -> Pal_SetColor(0x40+i)
  for comp: p = g_PalSaved[0x180 + 3i + comp]; same with 1/21 and g_NightLevel                                   -> Pal_SetColor(0x80+i)
  L2 = Clamp((g_CamY + 150)/33 + 1, 0, 31);   /* recomputed per i, C idiv */
  for comp: p = g_PalSaved[0x240 + 3i + comp]; v = (int)(p + (double)(63-p) * (1.0/31) * (double)L2)          -> Pal_SetColor(0xC0+i)
```
Constants: 1/21 = 0x3FA8618618618619, 1/31 = 0x3FA0842108421084. Evaluation: `t = (double)(63-p) * C; t = t *
(double)level; v = __FDI4((double)p + t)` (add is `__FDA(p, t)`; commutative). 48 uploads per call.

### Pal_SaveNight @ 0x46617 (verified)
Called after Mission_Setup when g_NightPalActive (0x1c7d9) and at 0x2c510.
1. `g_PalSaved = g_Palette` (768 B).
2. `Pal_SetColor(0xFE, 0x30, 0, 0)`.
3. Builds g_NightGreyTab[j][i] (j 1..15, i 0..15): `f = (i==7) ? 0.0f : 0.3f; if (j<4) f = (float)((double)f + (double)(5-j)*0.2);
   d = 1.0 + (double)(15-j)/12.0 + (double)f; c = i + (int)(0.5 + (double)(13-i)/d); tab = c*0x111`; row 0 =
   `Pal_GetColor12(i) & 0xFFFF`. **No reader of 0x8EC88 exists** (call/xref scan): port optional.
4. `g_NightLevelShown = -2` (forces Pal_NightAltitude next frame).

### Pal_Restore @ 0x4699E: `g_Palette = g_PalSaved` (no upload). Caller 0x21051 (after the mission).

## 8. Text and digits

Font data layout: `gfx.md` §5. Glyph lookup = linear search of the charset string for the upper-cased byte
(`0x61..0x7A` -> -0x20); "not found" includes space and `\n` handling below.

### Font_DrawSmallGlyph @ 0x12343 `int (u8 *g, int x, int y, int rows)` (verified)
For r < rows, c < 16: if `g[r*16+c] != 0`: `maxc = max(maxc, c)`; `PutPixel(x+c, y+r, g_NightPalActive ? 0xFE : 0xFF)`.
Returns maxc (0 if empty). Absolute VRAM coordinates.

### Font_DrawBigGlyph @ 0x123FA `int (u8 *g, int x, int y, int w /*16*/, int rows)` (verified)
Byte 1 -> PutPixel 0xFF; byte 5 -> PutPixel 0 (shadow); any non-zero byte updates maxc. Returns maxc.

### Text_DrawSmall @ 0x124E0 — `void Text_DrawSmall(int x, int y, u8 *s, int onPage)` (verified)
```
y += (onPage * g_BackPage) / 96;  cx = x;
for each byte ch (upper-cased):
  if ch == '\n': y += 8; cx = x; continue;
  i = index in small charset; if not found: cx += 2; continue;      /* space = 2 px */
  if ch < ',' (0x2C):  glyph(g+i*0x50 (+0x10 if i>=17), cx, y, 5)   /* '!' '(' ')' -> i>=17 path */
  elif ch == ',': glyph(g + i*0x50 + 0x10, cx, y+3, 3)
  elif ch == '-': glyph(g + i*0x50 - 0x60, cx, y+2, 1)
  elif ch == '.': glyph(g + i*0x50 - 0x10, cx, y+4, 1)
  else: if i < 17: rows = (ch=='Q') ? 6 : 5 (only for ch <= 'Q'); glyph(g + i*0x50, ...)
        else glyph(g + i*0x50 + 0x10, cx, y, 5)
  cx += returned maxc + 2;
```
### Text_DrawBig @ 0x126E9 `(x, y, s, onPage)` (verified)
Same page adjust; `\n`: y += 12; not found: cx += 4; i<16: glyph(g+i*0x90, 9 rows); i==16 ('Q'): 10 rows;
i>16: g+i*0x90+0x10, 9 rows; `cx += maxc` (**no gap**).
### Font_GlyphWidth @ 0x12970 `(g, rows)`: max c over 16 columns with g != 0.
### Text_WidthSmall @ 0x12BED `(s)`: sum over chars of (glyph maxc as in DrawSmall) + 2; unknown chars and `\n` = 2.
### Text_WidthBig @ 0x12D8D `(s)`: unknown + 4; glyph maxc otherwise (no gap).
### Text_FitWidth @ 0x129EE `int (char *s, int maxw)` (verified)
Accumulates widths like Text_WidthSmall (`i` = index of the next char) until `s[i]==0 || w >= maxw`, then
`while (i > 0 && s[i] != ' ' && s[i] != 0) i--`; returns `w < maxw ? 0 : i` (0 also when no space found).
### Text_DrawCenteredAt @ 0x25869 `(unused, x, y, s)`: `Text_DrawSmall(x + 29 - Text_WidthSmall(s)/2, y, s, 0)` (likely).

### Text_DrawDigit @ 0x12831 `(x, y, d)` (verified)
For r 0..4: `b = g_DigitFont[d*5 + r]`; bit 2 -> PutPixel(x, y+r, 0xFF); bit 1 -> (x+1); bit 0 -> (x+2). No background.
### Text_DrawNumber @ 0x128F1 `(x, y, val, n)` (verified)
`px = x + 4n; repeat n: Text_DrawDigit(px, y, (u8)(val % 10)); val /= 10; px -= 4;` Leading zeros drawn;
leftmost digit at x+4. Negative val -> index `(u8)(negative remainder)` reads past the table (Q9).

## 9. Primitives

### Video_SetLineColor @ 0x143B0 `(c)`: g_LineColor = c. ### Video_DrawLineColor @ 0x13041 `(x1,y1,x2,y2)`: `Video_DrawLine(x1,y1,x2,y2,g_LineColor)`.

### Video_FillRect @ 0x1334F `(x1, y1, x2, y2, col)`: PutPixel for y1..y2, x1..x2 inclusive (absolute VRAM).

### Video_CopyRect @ 0x11BEE `(srcPage, x1, y1, x2, y2, dstPage, dx, dy)` (verified)
Latch copy (GC write mode 1, all planes). `rows = (u8)(y2 - y1)` (char counter; 0 -> 256 iterations? no: loop
`while (rows != 0)`, so 0 copies nothing). For each row k: bytes `x1>>2 .. x2>>2` (inclusive) at
`srcPage*0x5A00 + (y1+k)*96` -> `dstPage*0x5A00 + (dy+k)*96 + (dx>>2)`. Linear: copies 4-pixel groups.
Callers use page 0 and absolute rows (e.g. the radar save area at row 0x246).

### Video_DrawLine @ 0x31604 — `void Video_DrawLine(uint32 x1, uint32 y1, uint32 x2, uint32 y2, uint8 col)` (verified)
Absolute VRAM coordinates. Port as an exact DDA (all arithmetic uint32 unless stated):
```
if (x1 == x2) {                                   /* vertical */
   lo = min_signed(y1,y2); hi = max_signed(y1,y2);  plot (x2, lo..hi);               /* hi-lo+1 pixels */
} else if (y1 == y2) {                             /* horizontal */
   lo = min_signed(x1,x2); hi = max_signed(x1,x2);  plot (lo..hi, y1);
} else {
   if (y2 < y1 /*unsigned*/) swap so (xs,ys) = (x2,y2), (xe,ye) = (x1,y1); else (xs,ys)=(x1,y1),(xe,ye)=(x2,y2);
   dy = ye - ys;  acc = 0x800;  x = xs; y = ys;
   if (xe < xs) { dx = xs - xe;                    /* going left */
      if (dx >= dy) {   /* X-major left */
         step = (uint32)(((uint64)(dy-1) << 32 | (uint32)(-dy)) / dx);  /* 32-bit div */
         cnt = dx + 1;  loop { plot(x,y); if (--cnt16 == 0) break; acc += step (carry -> y++); x--; }
      } else {          /* Y-major left: 16-bit step, 32-bit accumulator never carries */
         step = (uint16)(((uint32)(dx & 0xFFFF) << 16) / (uint16)dy);
         cnt = dy;      loop { plot(x,y); if (--cnt16 == 0) break; acc += step (carry -> x--); y++; }
      }
   } else { dx = xe - xs;                          /* going right */
      if (dx >= dy) {   /* X-major right: 16-bit div, upper half of the step is 0xFFFF */
         q = (uint16)((((uint32)((dy-1) & 0xFFFF)) << 16 | ((-dy) & 0xFFFF)) / (uint16)dx);
         step = 0xFFFF0000u | q;
         cnt = dx + 1;  loop { plot(x,y); if (--cnt16 == 0) break; acc += step (carry -> y++); x++; }
      } else {          /* Y-major right */
         step = (uint16)(((uint32)(dx & 0xFFFF) << 16) / (uint16)dy);
         cnt = dy + 1;  loop { plot(x,y); if (--cnt16 == 0) break; acc += step (carry -> x++); y++; }
      }
   }
}
```
`carry` = unsigned 32-bit overflow of `acc + step` (acc keeps the wrapped sum). Counters are 16-bit (`dec cx`).
Consequences (Q1): steep lines (|dy| > |dx|) are drawn **vertical** at xs (the accumulator never carries);
left steep lines have one pixel less; right shallow lines use the odd `0xFFFF0000|q` step (simulate literally).
Callers: Video_DrawLineColor (radar: Hud_DrawRadarLine, Hud_DrawPanel profile, 0x40300) and the lightning.

### Hud_DrawRadarLine @ 0x26174 — `(int dx0, int y0, int wx1, int wx2, int col)` (likely: args from the decompile)
```
Video_SetLineColor(col); xa = dx0 + 0xDD;
x1 = wx1*0x3E0 / (g_MapWidth<<4) + 0xDD; x2 = wx2*0x3E0 / (g_MapWidth<<4) + 0xDD;  mid = (x1+x2)/2;
DrawLineColor(xa,0x31, xa,y0); DrawLineColor(xa,y0, mid,y0); DrawLineColor(mid,y0, mid,0x1E); DrawLineColor(x1,0x1E, x2,0x1E);
```
(HUD rows, absolute VRAM: the HUD occupies rows 0..65.) Radar logic itself (Hud_DrawPanel 0x25c8b,
Hud_UpdateRadar 0x2ec74, Hud_DrawMessages 0x26e5a, Hud_DrawTargetArrow 0x267da) belongs to the HUD spec; they only
use the primitives above (PutPixel, FillRect, CopyRect, Text_DrawSmall with onPage=1 for messages,
Text_DrawNumber, Sprite_DrawNowShift, Pal_SetColor on 0x1E/0x1F).

## 10. Port design: indexed framebuffer reproducing the 320x240 window

```c
typedef struct { uint16_t start; uint8_t pan; uint16_t split_rows; } Crtc;   /* split_rows 240 = off */
uint8_t vram[0x40000]; uint8_t g_Palette[768]; uint8_t dac[768]; Crtc crtc;

static inline void vput(int32_t i, uint8_t c) { if ((uint32_t)i < 0x40000) vram[i] = c; }
static inline uint8_t vget(int32_t i) { return (uint32_t)i < 0x40000 ? vram[i] : 0; }

void Video_Present(uint8_t out[240][320]) {          /* called from every WaitVSync/SetStartAndPan wait */
  for (int r = 0; r < 240; r++) {
    int32_t base = (r < crtc.split_rows) ? crtc.start*4 + r*384 + crtc.pan
                                         : (r - crtc.split_rows)*384;     /* HUD: offset 0, no pan */
    for (int c = 0; c < 320; c++) out[r][c] = vram[(base + c) & 0x3FFFF];  /* CRTC address wraps at 64K */
  }
  /* RGB = dac[3*i+k] << 2 | dac[3*i+k] >> 4 */
}
```
* All draw routines take absolute pixel coordinates or (pageofs, x, y) and use `VIDX`; there is no clipping
  except Sprite_Blit's hotspot guard. Implement blitters exactly as the pixel loops in §3 (no SDL clipping).
* Video_WaitVSync = `Video_Present` + wait for the 59.94 Hz tick; Video_SetStartAndPan sets crtc then does one
  WaitVSync. Pal_Fade's per-step wait and the in-game `1 + g_VSyncWaits` cadence follow automatically.
* Keep the per-frame order of the game loop (Game_Run): update (incl. lightning lines, Video_ReadPixel probes on the
  back page) -> Level_DrawBackground -> ... -> Sprite_DrawQueue -> Bullets -> Hud_DrawMessages -> Hud_UpdateRadar
  -> Video_FlipPage. The HUD rows of VRAM persist across frames (never cleared in game).
* Window size 320x240, aspect 4:3 (square pixels for 320x240).

## 11. Quirks / original bugs (recommendation in brackets)

* Q1 Video_DrawLine: steep lines are vertical, left-steep lines miss the last pixel, odd step for right-shallow
  lines. Lightning segments (|dx| <= 19 < dy) are therefore vertical strokes. [keep]
* Q2 Sprite +384 wrap: the wrapped part of a sprite at the left edge appears 1 px lower (unmirrored) or 1 px
  higher (mirrored, via negative offset). [keep; free with the linear model]
* Q3 Sprite_BlitMirror has no top guard: a mirrored sprite reaching above page row 0 writes into the rows above the
  page: on page A that is the **HUD panel (VRAM rows <= 65)**, which is never redrawn in game -> permanent HUD
  corruption; on page B rows 316..321 (harmless). [keep for faithfulness; PORT note option to clip at page row 0]
* Q4 Parallax reads are linear (no wrap at x=320: the right part shows the next backdrop line, 1 px lower) and can
  run past the 0x28000 buffer at extreme camY (invisible rows) or before it for camY near -2000. [keep the
  mapping; PORT: allocate a zeroed margin before/after the buffer instead of reading heap garbage]
* Q5 With detail on, page columns 352..383 and rows 208+ are never cleared (stale sprites, invisible). [keep]
* Q6 Lightning starts at the absolute VRAM row of the page (66 or 322): no bolt is drawn on frames whose back page
  is B; on page A it is drawn in page rows 0..~104. [keep]
* Q7 Lightning (detail on) whitens 196..211 but the flash ramp restores only 192..207: 208..211 stay white
  (unused colours). Detail-off sky colours are written to 196..211 while the parallax range is 192..207. [keep]
* Q8 Pal_GetColor12 packs 6-bit components into nibbles (overlap); only feeds a dead table. [keep/ignore]
* Q9 Text_DrawNumber with a negative value indexes outside g_DigitFont. [keep; callers pass >= 0]
* Q10 Pal_Fade ignores a call whose type equals the previous one (e.g. two fade-outs in a row: the 2nd is skipped).
  [keep, it is relied upon]
* Q11 g_SpriteQueue has no limit; entry 256 overwrites g_Palette. [PORT: assert/limit at 256]
* Q12 Sprite_Queue for id == 0x8000 is treated as unmirrored with a bogus pointer. [never happens]
* Q13 RE_GUIDE says 70 Hz; the mode is 60 Hz (59.94). [fix the guide]

## 12. Open questions

1. g_VSyncWaits on the reference machine: the benchmark gives 2 (20 fps) on any fast PC/emulator without jitter,
   1 (30 fps) or 0 (60 fps) on slow ones. Which did the game ship tuned for? (DOSBox capture.) Default 2 proposed.
2. Exact first HUD line with line compare 349 and double scan (175 vs 176 playfield lines) — verify on a capture.
3. Where colour 64 (0x40) is visible: Pal_CycleEffects/lightning animate it (night, detail off) and
   Pal_NightAltitude sets 64..79, but no sprite/tile pixel uses 0x40. Possibly a tileset or HUD pixel at night.
4. Confirm (game-loop spec) that 0x1cf91 .. 0x1e3fa are one loop iteration, i.e. the bolt is drawn after the
   background of the same back page and stays visible for one frame.
5. Hud_DrawRadarLine argument meaning (wx1/wx2 world x vs map columns) — HUD spec.
