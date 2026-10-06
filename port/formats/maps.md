# Game/MAP formats (tilesets, maps, backdrops)

Tool: `tools/jsmap.py all` → `work/maps/` (`<MAP>.png` plain level render, `<MAP>_detail.png`
approximate detail-on composite, `tiles_<SET>.png` 16x16 sheet, `parallax_<SET>_<DX>_<P>.png`).

All LZW files (`.TLX .MXP .DX0 .DX1`) = `u32 LE unpacked size` + stream, decoded by
`LZW_Unpack` 0x50000 (`tools/jsunpack.py: unpack`). Loaded with `File_LoadWhole(dir="map/", name, &buf)`.

## 1. Naming (DATA/M0..M3 record, Mission_LoadBriefing 0x2299a)

Record 0x1c2 bytes: +0x000 briefing (0x140), **+0x140 map name (20, space padded)**,
**+0x154 tileset name (20, space padded)**, +0x168 params (0x3c BE16), +0x1a4 asc (20), +0x1b8 (10).
Blank names = keep the previous map/tileset.

* Tileset: name is trimmed, last 4 chars (".abk") overwritten with ".tlx" → `g_TilesetName`
  0x84e48 (e.g. `Jeticons.tlx`). DOS 8.3 truncation does the rest (`Trainingicons.tlx` → `TRAINING.TLX`,
  `Jungleicons` → `JUNGLEIC`). Loaded only when it differs from the current one (Mission_Setup 0x212ac).
* Map: trimmed name; trailing digits (up to 2, Mission_LoadBriefing ~0x22bxx) are stripped into the
  level number `DAT_0008fffc` (`Jetmap3` → base `Jetmap`, n=3). Then:
  * VAL = `map/` + base + `"1.val"` (`Jetmap1.val`; `Junglemap1.val` → `JUNGLEMA.VAL`)
  * MP2 = `map/` + base[:6] + `%02d`(n) + `.mp2`; MXP likewise `.mxp` (`JETMAP03.MXP`).
  * Map is (re)loaded only if the name changed (`DAT_00080074` = last map name).

| Record map | VAL | MXP/MP2 | tileset |
|---|---|---|---|
| Jetmap1..4 | JETMAP1 | JETMAP01..04 | JETICONS |
| Rockmap1/2 | ROCKMAP1 | ROCKMA01/02 | ROCKICON |
| Junglemap1..3 | JUNGLEMA | JUNGLE01..03 | JUNGLEIC |
| Seamap1..3 | SEAMAP1 | SEAMAP01..03 | SEAICONS |
| Citymap1/2 | CITYMAP1 | CITYMA01/02 | CITYICON |
| Icemap1/3 | ICEMAP1 | ICEMAP01/03 | ICEICONS |
| Ndmap1 | NDMAP1 | NDMAP01 | ICEICONS |
| Bonus1 | BONUS1 | BONUS01 | AEROICON |
| Training1 (M1) | TRAINING | TRAINI01 | TRAINING |
| Practise1 (M2) | PRACTISE | PRACTI01 | ROCKICON |
| Aolim1 (M3) | AOLIM1 | AOLIM01 | AEROICON |

## 2. Tileset `.TLX` + `.PAL` (Tileset_LoadTlx 0x133b3, via Tileset_Load 0x25c13)

* `.TLX` LZW → 0x10000 bytes into a 64 KB malloc (`DAT_00080058`) = 256 tiles × 256 bytes.
  `g_TilePtrs[i]` (0x845b8, 256 dwords) = base + i*0x100.
* **Tile layout is mode-X planar**: `tile[plane*64 + row*4 + col]`, plane 0..3, row 0..15, col 0..3;
  pixel (x,y) = `tile[(x&3)*64 + y*4 + (x>>2)]`. Each plane block is 16 dwords, one per row,
  copied directly with the map mask set to that plane.
* Pixel values are absolute VGA indices 0x80..0x8F (only 16 colours actually used).
  **0x80 = transparent** when detail is on (backdrop shows through); when detail is off it is drawn
  as colour 128 (= `.PAL` entry 0, the flat sky colour).
* `.PAL`: 768 bytes but only the first **0xC0 bytes** (64 colours, 6-bit VGA RGB, max 63) are read
  (`fread(0x83058, 0xc0)`, i.e. `g_Palette`+0x180 → colours 128..191).

## 3. Map `.MXP` (Map_LoadMxp 0x2422d)

LZW → `g_MapGrid` (0x849d0):

| off | type | |
|---|---|---|
| 0 | u16 **BE** | width W in tiles (1000; 600 for BONUS01/AOLIM01). Max 2000 (see VAL buffer) |
| 2 | u16 BE | height = 64 (code hardcodes 64 rows / 0x3f) |
| 4 | u8[W*64] | tile ids, row-major: `grid[4 + y*W + x]`, y=0 top. 1024 px tall |

Accessors: `Map_GetTile` 0x116dd, `Map_SetTile` 0x10d22, `Map_GetTileAttr(x,y,t)` 0x10cb6 =
`VAL[t*256 + tile]`. After load: `DAT_000900c4` = tile(0,0) = the "empty sky" tile used to erase
destroyed stuff; for each column the first row whose attr0 differs from attr0 of tile(0,0) (= ground
height) is stored at `g_MapVal+0x400+x` (live, modified by craters) and `+0xbd0+x` (original, used
by the HUD radar strip in Hud_DrawPanel and target counting).

Mission_Setup scans from row 63 upward for the right-most column whose attr0 == 0x81 (runway): that
sets the base/landing strip (`DAT_000901a8` = x px, start camera `g_CamX = x-0xa0`); attr0 0x82 at
row 63 under it sets `DAT_00090818` (carrier/sea base).

## 4. Tile attributes `.VAL` (1024 bytes, 4 tables × 256, indexed by tile id)

Loaded into `g_MapVal` (0x849d8, malloc 0x13b8: 0x400 VAL + 0x7d0 live heights + 0x7d0 orig heights).
Reloaded on every Mission_Setup (resets damage tables, not the grid).

| table | meaning (from Map_CraterAt 0x3b777, Mission_Setup, FUN_0003e1a3) |
|---|---|
| 0 (+0x000) | **collision/class**. 0x7F = air/passable (≥0x7f treated as non-solid: `attr > 0x7e`). <0x7F = solid. Specials: 0x80 (ground/structure surface), 0x81 runway (landing), 0x82 sea/carrier base, 0x83/0x84/0x85 objects temporarily blanked to sky when MP2-triggered (replaced by a sprite), 0x86 trigger flag, 0x05 marker position, 0xC7 (199) mission-critical target (HUD message on hit), 0x00/0x02/0xA0/0x8C seen |
| 1 (+0x100) | **destroyed replacement tile id** (Map_SetTile on destruction). `t1[t]==t` = indestructible/terrain (crater chain, debris particles) |
| 2 (+0x200) | **armour/value**: tile is destroyed when `t2*10 <= damage`; score += `t2*(difficulty+1)`. 0xFF = effectively indestructible |
| 3 (+0x300) | **destruction flags**: 0xFF = no effect (only replaced); bit0 = no debris particle; bit2 = leaves a burning point (fire, timer 2000); bit4 = extra explosion particle (type 0x19). Other bits (0x20, 0x02) unknown |

## 5. `.MP2` (1000 bytes, one byte per map column; buffer `DAT_000849ec`)

Proximity trigger table read each frame for the player's column
`c = ((camX+playerScrX+shakeX[+16]) >> 4) % (W-1)` (main loop ~0x1d03c, also 0x3e6xx), handled by
FUN_0003e1a3:
* `0` – nothing.
* `1..0x7F` – a **ground object row** y in this column (tile (c,y)). If attr0 < 0x80 and ≠5 and the
  tile above is air, a ground launcher may be spawned at (c,y) (chance ∝ mission/10 out of 12).
  attr0 0x83/0x84/0x85: tile is swapped to the sky tile while near (restored next trigger), 0x86 sets
  a flag, 5 stores a position (`DAT_0009089c/a0`).
* `0x80..0xC0` – **relative pointer**: `off = v - 0xA0` (−32..+32) → the column `c+off` whose MP2 byte
  holds the row. Files have runs `192,191,…,161, <row>, 159,…,128` around each object.
The overview/debug map (0x3fxxx) also draws the 1..0x7f entries as markers.

## 6. Parallax backdrop `.DX0/.DX1` + `.P00/.P01/.P10` (Parallax_Load 0x1351e)

Only when `g_DetailParallax` (0x84536, JS.CFG +0x2e) ≠ 0. The extension of the tileset name is
replaced: `.dx0` normally, `.dx1` when `DAT_0009035c` (= mission param word at 0x9167a == 1: **night**
mission; also enables Runway_Update).
* DX LZW → 0x28000 bytes = **320 × 512 linear, 8-bit** (malloc 0x280c8 at `DAT_00080050`). Each byte
  has 0x40 subtracted after load: file values 1..15 → indices 0xC1..0xCF.
* Palette: `.p0` + `'0'+Rand(1)` (P00 or P01 randomly, day) or `.p10` (night). 196/192 bytes =
  LE u32 per component, 8-bit value; colours 192..207 (16 RGB triples) take
  `byte[i*4] >> 2` for i=0..47. Trailing 4 bytes unused. Also copied to `DAT_00092ad4`.
* Detail off: colours 0xC4..0xD3 set to 12-bit 0x4AF (day) / 0x00A (night) via Pal_SetColor12.

### Drawing (Level_DrawBackground 0x13842 → Tiles_DrawColumns 0x10540 / Tiles_DrawColumnsParallax 0x1040c)
* Window: 24×16 tile ids copied to 0x831d8 from columns `(col+i) % W`, rows `row-1+j` (row ≥ 2).
* Per plane p (map mask), into the back page (`0xA0000 + 0x18c0` or `0x78c0`, row stride 96 bytes =
  384 px virtual width):
  * detail off: 24 × 13 tiles, copy plane block verbatim.
  * detail on: **22 × 13 tiles** (last 2 window columns skipped); each tile byte 0x80 is replaced by
    backdrop byte. Backdrop origin:
    `bx = (int)(g_CamX / 4.1666667) % 320 - g_ShakeX`,
    `by = (int)((g_CamY + 0x7d8) / 9.25 - g_ShakeY)`;
    screen pixel (sx,sy) reads `dx[(by+sy)*320 + bx + sx]` **linearly with no horizontal wrap**
    (x ≥ 320 spills into the next line; the art tolerates this). So the backdrop scrolls at 0.24× the
    camera horizontally and 0.108× vertically.
* Fine scroll via CRTC start + pel pan (Video_FlipPage 0x13a68).

The `_detail.png` composite in jsmap uses one camera per 320×176 cell, so cell seams are expected.
