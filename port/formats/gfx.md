# JetStrike graphics formats (PAX/PAL, SPX, HD, fonts)

Decoder/viewer: `tools/jsgfx.py` (`python tools/jsgfx.py all` writes `work/gfx/*.png`,
`work/spx/<NAME>.png` contact sheets (+ `work/spx/JETSTRIK/NNN.png` per-frame), `work/fonts/`).
All little-endian unless stated. "LZW" means the packed format of `tools/jsunpack.py`
(`u32 unpacked_size` + 9..12-bit MSB-first code stream), unpacked by `LZW_Unpack` 0x50000.

VRAM model used below: mode X, 4 planes, 96 bytes (384 px) per row, page = 240 rows = 0x5A00 bytes.

## 1. Palette layout (g_Palette 0x82ed8, 6-bit VGA values)

| indices | source | loaded by |
|---|---|---|
| 0..63 | picture `.PAL` (title/menu pictures) or `DISPLAY.PAL` (in-game HUD) | Pic_LoadPax 0x11a1a / Pic_LoadHudPanel 0x130c8 |
| 64..95 | `DATA/JETSPRIT.PAL` | Sprites_LoadSpx 0x11306 (2nd call) |
| 96..127 | same 32 colours copied again (target of Sprite_BlitShift, +0x20) | Sprites_LoadSpx |
| 128..191 | tileset `.PAL` | tileset loader (not this doc) |
| 254, 255 | text colours (255 set to 63,63,63 by Fonts_Load 0x114da) | |

## 2. PAX pictures + PAL  (GFX/*.PAX)

**There is no planar or special layout: every PAX is a linear 8-bpp image, 320 pixels wide.**
`height = unpacked_size / 320` (integer division). Pixel values are 0..63 only.

| unpacked size | rows | files |
|---|---|---|
| 81920 | 256 | AEROLIM, AOSCORES, AOSET, ENDGAME0-6, GAMEDONE, JETLOGO, MISCON, PLANECH, WEPCH |
| 64000 | 200 | BONUSPIC, JUNGLEPI, PRACTCRT, TRAINCRT |
| 70656 | 220 (+256 unused tail bytes) | CARRIER, CITYPIC, COMBATCO, ICEPIC, ROCKJET, TRAINING |
| 21120 | 66 | DISPLAY (HUD) |

The 70656-byte files "failed" only because 70656 is not a multiple of 320; the game computes
`rows = size/320 = 220` and ignores the trailing 256 bytes. Their `.PAL` files are 192/195 bytes
(64 colours), and since only colours 0..63 are used they look wrong with an unswapped palette.

### Pic_LoadPax(name, page, applyPal) @ 0x11a1a
1. `File_LoadWhole("gfx/", name)`, `LZW_Unpack` into buffer `DAT_84508`.
2. If `DAT_8004b == 1` (normal case): black palette 0..63 first, clear 0x5A00 bytes of all 4 planes at `0xA0000 + page*0x5A00`.
3. `Video_BlitLinearToPlanar(buf, x=0, y=page*240+20, width=320, height=size/320)` @ 0x1063a.
   Linear source, destination plane p gets source bytes x%4==p (it walks the source with stride 4 once per plane).
   So the picture appears at row 20 of the page (centred 200 lines in 240); 256-row pictures run past the page end.
4. If `DAT_8004b == 1`: load `name` with extension replaced by `.pal`; for i in 0..63 swap bytes `[3i]` and `[3i+2]`
   (**R and B are swapped in the file**), then `g_Palette[0..0xBF] = byte >> 2` (file is 8-bit, only the first
   0xC0 bytes are used even for 768-byte .PAL files). If `applyPal`, `Pal_Upload(0, 0x40)`.

C port: `rgb[i] = { pal[3i+2]>>2, pal[3i+1]>>2, pal[3i]>>2 }` for i < 64.

### Pic_LoadHudPanel(name) @ 0x130c8  (GFX/DISPLAY.PAX)
Clears all of VRAM (0x10000 bytes, all planes), unpacks DISPLAY.PAX into a 0x5280 buffer, blits it with
`Video_BlitLinearToPlanar(buf, 0, 0, 320, 66)` at VRAM offset 0 (rows 0..65), loads DISPLAY.PAL the same way
(swap R/B, >>2 into colours 0..63), then `Video_SetSplitLine(0xAF)` and CRTC start address = 0x1EC0
(regs 0x0C=0x1E, 0x0D=0xC0, i.e. row 82). The line-compare split makes rows 176.. of the screen show VRAM
from offset 0, i.e. the 66-row HUD panel at the bottom (175+66 = 241 lines).

## 3. SPX sprite banks  (DATA/JETSTRIK.SPX, PLANE/*.SPX)

File = LZW. Unpacked bank = concatenation of entries until the end of data (no count, no directory):

| off | type | field |
|---|---|---|
| 0 | u16 | hotX: pixels from left edge to the anchor |
| 2 | u16 | hotY: pixels from top edge to the anchor |
| 4 | u16 | w4 = width / 4 (width is always a multiple of 4) |
| 6 | u16 | h = height |
| 8 | u8[4][h][w4] | 4 planes, each `h` rows of `w4` bytes; plane p, row y, column c is pixel `(4c+p, y)` |

Entry size = `8 + w4*4*h`. Plane 0 is the leftmost pixel column of each 4-pixel group.
No RLE. All files parse exactly to the end (e.g. JETSTRIK: 529 entries, 422728 bytes;
fighter planes 22 or 44 frames of 48x48; helicopters/props 39/15/50; TRUCK* 1..6 frames).

**Colour / transparency.** At load time every pixel byte gets `+ *(u8*)0x80008` = **0x40** added
(Sprites_LoadSpx for JETSTRIK.SPX, Sprites_ReplaceFromBank 0x12f67 for the plane banks; the 8-byte header is
copied unchanged). The blitters skip pixels equal to `'@'` (0x40), so **raw value 0 = transparent**, raw 1..31
-> palette 65..95 (JETSPRIT.PAL). Observed raw maxima: JETSTRIK 30, plane banks <= 15.

**Sprite table.** `g_SpriteTab` pointers at 0x83a58 + (id-1)*4 point into the (offset-added) bank; ids 1..529
are JETSTRIK.SPX entries in order. Many slots (1-50, 81-105 etc.) are empty-looking 48x48 placeholders that
the per-mission plane/enemy/truck loaders overwrite:
- `Plane_LoadSpx` 0x26473: LZW `plane/<name>.spx` into `DAT_849f0`; its caller (the plane setup function just
  before it, which also loads the .HD) copies entry k into slot k for k = 1..`Sprites_CountInBank`.
- `Enemy_LoadSpx` 0x3f991: `plane/enemy<'a'+DAT_906e4>.spx`; entry i (0-based) 0..15 -> slot 0x5a+i,
  entry 16 -> slot 0xdb, entries 17..22 -> slot 0xb2+i.
- `Truck_LoadSpx` 0x3ff4d: `plane/truck<'a'+type><n>.spx` (n = 0..14 decimal); entry k -> slot
  `DAT_8dab8[DAT_9098c + k]` (slot ids from a table), k < `DAT_90434`.
- `Sprites_ReplaceFromBank(slot, n, bank)` 0x12f67 walks to the n-th entry (1-based) and copies header+pixels
  (+0x40) over the existing slot memory (the slot must be at least as large; plane frames are all 48x48).

**Blitters** (draw into `0xA0000 + page`, 96-byte rows, no clipping):
- `Sprite_Blit(x, y, spr, page)` 0x10128: left = x - hotX, top = y - hotY; returns without drawing if
  `x < hotX || y < hotY` (unsigned). Plane order starts at plane `left & 3` and the first plane written is the
  file's plane 0. Skips 0x40.
- `Sprite_BlitShift` 0x101c8: identical but writes `pixel + 0x20` (palette 96..127 bank; at load it is a copy of
  64..95, so it can be retinted independently, e.g. flash).
- `Sprite_BlitMirror` 0x1026c: horizontal flip; left = x - (w - hotX); reads each row backwards. No hotspot guard.
- `Sprite_DrawQueue` 0x1155e: queue entries with a flag draw at x+0x180 (384, horizontal wrap copy); `id|0x8000` = mirrored.

## 4. PLANE/*.HD  (helicopter hit data)
1600 bytes = 400 big-endian s32 (loader byte-swaps every dword) = 100 records of 16 bytes, read into
`DAT_84a34` by the plane-setup code at ~0x26390 when `plane/<name>.hd` exists (only APACHE, EH-101, HARRY,
HUEY, HUGHES50, LYNX). Indexed by sprite frame (`frame*16`) in `Player_Collide` 0x29b29 - collision boxes,
not graphics. Exact field meaning belongs to the collision doc.

## 5. Fonts (MISC/*.RAW, uncompressed, loaded whole by Fonts_Load 0x114da)
Both files are byte-per-pixel bitmaps 16 bytes wide, row-major, pointer at 0x80009 (small) / 0x8000d (big).
Lower-case input is upper-cased; `\n` = new line (+8 small, +12 big). Advance = rightmost non-zero column of
the glyph (`Font_GlyphWidth`) + 2 (small) / + 0 (big); unknown chars incl. space advance +2 (small) / +4 (big).

**SMALLFNT.RAW** (3296 B), charset at 0x80bf0: `ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!?(),.-` (index i).
Non-zero byte = pixel, drawn with colour 255 (or 254 if `DAT_80070`). `Font_DrawSmallGlyph` 0x12343, `Text_DrawSmall` 0x124e0:
| char | address | rows | y offset |
|---|---|---|---|
| A..P (i<16) | i*0x50 | 5 | 0 |
| Q (i=16) | i*0x50 | 6 (descender) | 0 |
| others (i>=17) | i*0x50 + 0x10 | 5 | 0 |
| `,` | i*0x50 + 0x10 | 3 | +3 |
| `-` | i*0x50 - 0x60 | 1 | +2 |
| `.` | i*0x50 - 0x10 | 1 | +4 |

**BIGFNT.RAW** (5698 B), charset at 0x80c1c: `ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!?`. Glyph i at
i*0x90 (9 rows of 16) for i<16, Q (i=16) 10 rows, i>16 at i*0x90+0x10, 9 rows. Byte 1 -> colour 255,
byte 5 -> colour 0 (drop shadow), 0 transparent (`Font_DrawBigGlyph` 0x123fa, `Text_DrawBig` 0x126e9).
Other byte values (7, 10, 13) occur only in unused padding.

**HUD digits** (no file): `Text_DrawDigit` 0x12831 uses a built-in 3x5 table at 0x80019, 5 bytes per digit 0..9,
bit 2 = left column, bit 1 = middle, bit 0 = right, colour 255; `Text_DrawNumber` 0x128f1 draws right-aligned, 4 px per digit.
