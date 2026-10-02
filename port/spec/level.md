# Level subsystem: map, tile attributes, terrain damage, camera, mission parameters and objectives

Target: `work/JS.bin` (JS_CDROM.EXE flat image, base 0x10000), decompile `port/decomp/js.c`. Companion specs:
`platform.md` (File_LoadWhole 0x12114, Rand 0x10d65, Clamp 0x10e11, Sign, Byte_Get 0x12f3e / Byte_Set 0x12f18),
`video.md` (Level_DrawBackground 0x13842, Tiles_Draw*, palette effects), `sound.md`. Game_Run 0x1ba0c,
Mission_Setup 0x212ac and Mission_LoadBriefing 0x2299a belong to **game_flow**; this spec describes only their
map/parameter parts. Player flight (Player_Update 0x2d34e, Player_LandingCheck 0x15957, Player_DeathAndLanding)
and enemy AI (EnemyAir_Update 0x36ae3, Convoy_Update, Building_Update 0x149ae, Turrets/Helis/Ships, EnemyBomber_Spawn
0x38ba8, Explosion_Damage 0x39b69) are other specs and are referenced by address only.

Confidence tags: **verified** (read in the capstone disassembly, `jsdis.py` style), **likely**, **guess**.
All arithmetic is 32-bit signed `int` unless noted; "Div16(v)" means the Watcom idiom
`(v + ((v>>31)&15)) >> 4` = C `v/16` truncating toward zero (NOT `v>>4` for negatives).
`Rand(n)` = `n==0 ? 0 : rand() % (n+1)` (platform spec). Every Rand call is listed in order.

Corrections to `port/formats/maps.md` / `data.md` found here are collected in §12.

---------------------------------------------------------------------------------------------------

## 1. Data structures

### 1.1 Map buffers (verified)

```c
/* 0x849D0 */ uint8_t *g_MapGrid;   /* malloc(0xFA04) by Mem_AllocMapBuffers 0x1aeaa: 4-byte header + W*64 tiles      */
              /* [0..1] BE16 width W, [2..3] BE16 height (=64, never read), [4 + y*W + x] tile id, y=0 top          */
              /* capacity 0xFA04 = 4 + 1000*64  ->  W <= 1000 (shipped: 1000, 600 for BONUS01/AOLIM01)             */
/* 0x8FE90 */ int32_t  g_MapWidth;  /* W in tiles                                                                    */
/* 0x849D8 */ uint8_t *g_MapVal;    /* malloc(0x13B8) on first map load, never freed; NOT zeroed                     */
              /* +0x000 attr table 0 (class), +0x100 table 1 (replacement tile), +0x200 table 2 (armour),         */
              /* +0x300 table 3 (destruction flags)  -- the 1024-byte .VAL file                                   */
              /* +0x400 + x : live ground row of column x (u8, 0..63; lowered by craters / Bertha stamp)          */
              /* +0xBD0 + x : original ground row of column x (u8; written only by Map_LoadMxp)                   */
              /* sizes: 0x400 + 0x7D0 + 0x7D0 = 0x13B8 (2000 columns reserved, only W used)                        */
/* 0x849EC */ uint8_t *g_MapMp2;    /* (was DAT_000849ec) .MP2 column trigger table, malloc(1000) in Mission_Setup,  */
              /* malloc(2000) in Mission_LoadBriefing, whichever runs first; file is 1000 bytes                    */
/* 0x900C4 */ int32_t  g_SkyTile;   /* (was DAT_000900c4) tile id used to erase things                               */
/* 0x8FFFC */ int32_t  g_MapVariant;/* NN of the .mp2/.mxp name                                                      */
/* 0x85148 */ char     g_MapName[]; /* current record map name (digits stripped)                                    */
/* 0x80074 */ char     g_MapLoaded[];/* (was DAT_00080074) name of the map whose grid is in memory ("" at start)     */
/* 0x84E48 */ char     g_TilesetPending[]; /* record tileset name with ".tlx"                                       */
/* 0x84B48 */ char     g_TilesetName[];    /* last loaded tileset; NOTE Parallax_Load rewrites its extension      */
/* 0x80058 */ uint8_t *g_TileData;  /* (was DAT_00080058) 64 KB tile pixels; g_TilePtrs 0x845b8[256] = base+i*256      */
/* 0x80050 */ uint8_t *g_Backdrop;  /* (was DAT_00080050) 0x280C8 malloc, 320x512 parallax bytes (video spec)          */
/* 0x84510 */ uint8_t *g_PackBuf;   /* 85000-byte LZW input buffer (platform)                                        */
/* 0x849F0 */ uint8_t *g_MapBuf;    /* 150000-byte buffer; Map_StampBertha loads data/berthaX into it                */
```

Read access: `tile(x,y) = g_MapGrid[4 + y*W + x]`, no bounds checks anywhere (x is not wrapped by the
accessors; callers do `% W` or Clamp themselves). `attr(x,y,t) = g_MapVal[(t&3)*256 + tile(x,y)]`.

### 1.2 Tile attribute tables (VAL), resolved meanings

**Table 0 = class** (all shipped VAL files, every tile used in a shipped map; counted with a script over
MXP x VAL):

| attr0 | meaning | code that tests it | conf |
|---|---|---|---|
| 0x00 | **air / sky** (the sky tile `tile(0,0)` has class 0 in every VAL). Also the class of some "destroyed" tiles: Bertha counts it as destroyed | ground scan (`!= attr0(sky)`), Bertha_Update, many `< 0x7f` tests | verified |
| 0x01 | only SEAMAP01 (2 tiles). Behaves as air-class (< 0x7f). No special code | - | likely |
| 0x02 | **rubble** (destroyed structure). Target columns count when attr0 2 or 0xA0 is found under the surface | Mission_CheckObjectives, Bertha_Update | verified |
| 0x05 | **marker**: when MP2-triggered, the position is stored in g_MarkerX/Y (0x9089c/0x908a0) and Game_Run draws sprite 0x1e9 there (smoke/flag) | FUN_0003e1a3 → `Map_TriggerColumn` | verified |
| 0x7F | **solid terrain** (ground, rock, water body). `> 0x7e` = solid in all collision tests | everywhere | verified |
| 0x80 | solid structure surface (buildings, hangars) | collision; convoy (0x80/0x7f) | likely |
| 0x81 | **runway** (landing strip; the player's base) | Mission_Setup, landing code | verified |
| 0x82 | **sea / carrier deck**; crash-into-water messages | Mission_Setup (carrier flag), landing, debrief | verified |
| 0x83 | MP2 object A: blanked to sky while triggered, a sprite object takes its place (FUN_0003dfaa / FUN_0003ec5b) | Map_TriggerColumn | verified (mechanism); object kind: AI spec |
| 0x84 | MP2 object B (FUN_0003e0b2 / FUN_0003ef72) | Map_TriggerColumn | same |
| 0x85 | MP2 object C (FUN_0003dea2 / FUN_0003e8b3, not in modes 1/3) | Map_TriggerColumn | same |
| 0x86 | **radar jammer**: triggers g_RadarJammed (0x9047c) → Hud_UpdateRadar shows "Jammed" | Map_TriggerColumn, Hud_UpdateRadar 0x2ec74 | verified |
| 0x87 | city maps only (7 MP2 entries). **No handler**: the trigger does nothing for it | - | verified (no code), meaning guess |
| 0x8C | **base radar dish anchor**: at mission start Game_Run searches columns runwayEnd/16 .. -10, rows above the runway, and puts an animated sprite (0x1e2..0x1e5) there (FUN_00016e38); it disappears when attr0 at (x/16+1, y/16+1) < 0x7f | Game_Run 0x1ca40.. | verified |
| 0xA0 | rubble/crater variant (e.g. JETMAP tiles 236/237/239) | Mission_CheckObjectives, Bertha_Update | verified |
| 0xC7 (199) | **special target**: hitting it (Map_CraterAt) sets g_SpecialHit 0x90768=1 and pushes the HUD string "Om" (0x813bc). 0x90768==1 at the next briefing / Mission_Setup forces fog | Map_CraterAt | verified (effect), purpose guess |

The form-(c) target spec (p03 >= 5000, §8.3) compares **attr0 classes** (not tile ids): shipped values
5131..5136 = classes 0x83..0x88.

**Table 1** = tile id that replaces this tile when it is destroyed. `t1[t] == t` marks terrain-like tiles
(crater chain / debris rules below). **Table 2** = armour: destroyed when `armour*10 <= damage`; score
`armour*(planeClass+1)` (planeClass = 0x9045c, MISC.Z word 77). In Map_DamageColumn `armour > 200` stops the
column collapse (bedrock). **Table 3** = destruction flags (bits tested only in Map_CraterAt and 0x403c1):

| bit / value | effect | conf |
|---|---|---|
| exactly 0xFF | "no effect": only replaced, no score, no particles (SEAMAP water) | verified |
| 0x01 | no debris particle (tested on the destroyed tile's row, see §4.1) | verified |
| 0x04 | **secondary explosion**: queues an Explosion_Damage(x*16, y*16, 0,0, 2000, 2000) for the same frame (fuel/ammo dumps) | verified |
| 0x10 | extra explosion particle (type 0x19) if fewer than 10 such particles (0x90320) | verified |
| 0x40 | **upward push**: in the hover routine 0x403c1 (`Player_HoverUpdate`), a tile with this bit at the ground row under the player raises the player 2 px/frame (trees / tall objects for VTOLs) | verified (code), purpose likely |
| 0x02, 0x08, 0x20, 0x80 | never tested | verified |

Values 0xFD/0xFE (SEAMAP) are therefore "0xFF minus a bit": they get every effect whose bit is set (0x04
secondary explosion, 0x10, 0x40), and 0xFE also gets debris. Probably meant as 0xFF; keep as is.

### 1.3 MP2 column trigger table (verified)

1000 bytes, byte per column (columns >= W unused, all zero in shipped files):
* `0` nothing; `1..0x7F` row of an object in this column; `0x80..0xFF` relative pointer `off = v - 0xA0`
  to column `c+off` whose byte holds the row (shipped data uses 0x80..0xC0, runs 192..161, row, 159..128 around
  each object = trigger window of ±32 columns). Shipped direct rows are all <= 63; the classes found there are
  0x83 (159), 0x84 (118), 0x85 (68), 0x86 (18), 0x87 (7), 0x7F (18, JUNGLE01 and NDMAP01), 0x05 (2), 0x00 (3), 0x80 (1).

### 1.4 Other state owned by this spec

```c
/* 0x901A8 */ int32_t g_BaseEndX;    /* px: right end of the runway (column*16)                                     */
/* 0x90180 */ int32_t g_BaseStartX;  /* px: left boundary of the runway (first non-runway column left of it, *16)   */
/* 0x901AC */ int32_t g_BaseYOff;    /* (63-runwayRow)*16: height of the runway above the map bottom               */
/* 0x905F0 */ int32_t g_RunwayFill;  /* tile id left of the runway's right end; used to repair runway holes         */
/* 0x90818 */ int32_t g_BaseIsCarrier;/* attr0(endCol,63)==0x82                                                    */
/* 0x903B4 */ int32_t g_MapWidthPx;  /* W*16, camera wrap limit                                                     */
/* 0x8FF18 */ int32_t g_TrigCol;     /* column being triggered (also used by Player for the player column)         */
/* 0x90690 */ int32_t g_Scratch690;  /* MP2 byte while triggering; ALSO a general scratch (Bertha letter/count,       */
              /*                       objective flag, menus). Map_ResetCounters zeroes it                         */
/* 0x8FEE4 */ int32_t g_TrigOff;     /* column offset of the triggered object (0 for direct rows)                   */
/* 0x8FEEC */ int32_t g_TrigRow;     /* row of the triggered object                                                 */
/* 0x905BC */ int32_t g_TrigClass;   /* attr0 of the triggered tile                                                 */
/* blanked tiles (restore records): (x, y, saved tile)                                                            */
/* 0x9063C,0x90640,0x90630  class 0x83 ;  0x900E8,0x900EC,0x900E4  class 0x84 ;  0x90230,0x90234,0x9022C  class 0x85 */
/* active objects: 0x90668/0x90614 (0x83 x col,row), 0x9010C/0x90110 (0x84), 0x90268/0x9026C (0x85)                */
/* 0x9047C g_RadarJammed; 0x9089C/0x908A0 g_MarkerX/Y (px, -16 each)                                              */
/* ground launcher: 0x902C0 col (nonzero = Building_Update runs), 0x902C4 row, 0x902BC kind, 0x902DC timer        */
/* Bertha: 0x908E8 col, 0x908EC row, 0x908E4 width, 0x908DC height, 0x908C8 start delay (600),                   */
/*         0x908D8 shell timer, 0x908F4 shell target x                                                            */
/* agent drop: 0x90280 g_AgentDropPending; crate/agent object 0x90948 x, 0x9094C y, 0x90940 sprite, 0x9091C vy     */
/* target marker for the HUD arrow / guided weapons: 0x90574 x px, 0x9052C y px (0 = none), cleared every frame    */
/* 0x80070 g_FogActive (video.md calls it g_NightPalActive, see §9); 0x92B18 g_FogSticky; 0x90768 g_SpecialHit     */
```

---------------------------------------------------------------------------------------------------

## 2. Loading (as used by Mission_LoadBriefing 0x2299a and Mission_Setup 0x212ac)

### 2.1 Order inside Mission_Setup (verified, map part only)

```
Sound_StopAll; CD_Stop; ...; if (0x903f4 != g_Mission) Mission_LoadBriefing();   // §2.3 may load the map first
if (plane select) { PlaneSelect_Screen(); g_TilesetName[0] = 0; }               // forces a tileset reload
... weapons ...; if (p09 && mode<3) { Enemy_SetupSpriteIds(); p09 = 0; }          // convoy, AI spec
if (strcmp(g_TilesetPending, g_TilesetName) && g_TilesetPending[0]) {
    Tileset_Load(g_TilesetPending);            // 0x25c13 -> Tileset_LoadTlx 0x133b3 (+ dead loop 0..511)
    strcpy(g_TilesetName, g_TilesetPending);
    0x906dc = 0x90728 = 0x90750 = 0x90114 = 0x90158 = 0x90760 = 0x9071c = 0; }
Parallax_Load(g_TilesetName);                  // 0x1351e, EVERY mission (Rand(1) when detail on and day)
if (strcmp(g_MapName, g_MapLoaded) == 0 || g_MapName[0] == 0) strcpy(g_MapName, g_MapLoaded);
else { *(uint32*)g_MapGrid = 0; strcpy(g_MapLoaded, g_MapName); 0x90410 = 0; }
if (g_MapGrid == NULL || *(uint32*)g_MapGrid == 0) { MapLoadAll(); FindRunway(); }    // §2.2, §2.4
Pal_Fade(0,0x100,0,0x20);
File_LoadWhole("map/", g_MapName+"1.val", &g_MapVal, 0);    // re-reads ONLY the 1024 attribute bytes
Map_ResetCounters();  Hud_DrawPanel();
... camera / player start (§6.1), runway repair (§2.4) ...
```

Consequences (verified): the grid, the live heights and all damage **persist across missions on the same
map** (a blank record map name keeps the map; retrying the same mission keeps it too). Only a map-name change
reloads the MXP. The VAL re-read restores tables 0..3 (never modified by the game) and leaves the height
tables untouched.

### 2.2 MapLoadAll (inline in Mission_Setup 0x21720..0x21825 and Mission_LoadBriefing 0x23adx; verified)

```c
if (!g_MapVal) g_MapVal = malloc(0x13b8);
if (!g_MapMp2) g_MapMp2 = malloc(1000);      /* 2000 in Mission_LoadBriefing */
File_LoadWhole("map/", g_MapName + "1.val", &g_MapVal, 0);
n = min(strlen(g_MapName), 6); name = g_MapName[:n] + ('0'+g_MapVariant/10) + ('0'+g_MapVariant%10);
File_LoadWhole("map/", name + ".mp2", &g_MapMp2, 0);
/* name + ".mxp" left in the shared path buffer 0x85048 */  Map_LoadMxp();
```
Mission_Setup additionally sets `0x904ac = 0` before; Mission_LoadBriefing sets `0x90158 = 0x9037c = 0x90998 =
0x90114 = 0` before and calls `Map_ResetCounters()` after. Mission_LoadBriefing does **not** update
g_MapLoaded, so a new map is loaded twice (briefing, then Mission_Setup). See §11 Q1 for what this does to the
Bertha stamp.

### 2.3 Map_LoadMxp @ 0x2422d — `void Map_LoadMxp(void)` (verified)

```c
0x902f8 = 1;
File_LoadWhole("map/", path /* 0x85048 */, &g_PackBuf, 0);
LZW_Unpack(g_PackBuf, g_MapGrid);
g_MapWidth = g_MapGrid[0]*256 + g_MapGrid[1];
uint8 sky = attr(0,0,0);
for (x = 0; x < W; x++)
    for (y = 0; y < 64; y++)
        if (attr(x,y,0) != sky) { g_MapVal[0xBD0+x] = y; g_MapVal[0x400+x] = y; break; }
for (i = 0; i < 64; i++) ;              /* empty loop, leaves 0x90ab8 = 64 */
g_SkyTile = tile(0,0);
```
Quirk: a column with no non-sky tile keeps whatever was in the height bytes (malloc garbage on first load,
the previous map's value later). No shipped map has such a column. PORT: zero-initialise g_MapVal.

### 2.4 Runway / carrier detection (Mission_Setup 0x2182a..0x219c6, verified)

```c
row = 63;  g_SkyTile = g_MapGrid[5];      /* = tile(1,0), overrides Map_LoadMxp's tile(0,0) (same in all maps) */
g_BaseStartX = g_BaseEndX = g_BaseIsCarrier = 0;
while (g_BaseStartX == 0) {                              /* scan rows bottom-up                           */
    for (x = W-1; x >= 0; x--)
        if (attr(x,row,0) == 0x81) {                     /* right-most runway tile of this row            */
            g_RunwayFill = tile(x-1,row);
            g_CamX = x*16 - 0xA0;  g_BaseStartX = x;  /* column for now */
            x = -1;                                      /* break                                         */
            g_BaseEndX = g_CamX + 0xA0;                  /* = x*16                                        */
            g_BaseYOff = (63-row)*16;
        }
    row--;
}
if (attr(Div16(g_BaseEndX), 63, 0) == 0x82) g_BaseIsCarrier = 1;
for (x = g_BaseStartX; x >= 0; x--)
    if (attr(x, 63 - Div16(g_BaseYOff), 0) != 0x81) { g_BaseStartX = x*16; x = -1; }
```
Quirks: no runway → endless loop reading outside the grid; a runway found in column 0 is ignored (loop
continues); a runway that reaches column 0 leaves g_BaseStartX as a column number. None happen with shipped
maps. Carrier test uses row 63 under the right end regardless of the runway row.

Runway repair, every Mission_Setup (0x21af5..0x21ba8, verified):
`for (x = Div16(g_BaseStartX)+1; x <= Div16(g_BaseEndX)-1; x++) if (attr(x, r,0) != 0x81) Map_SetTile(x, r, g_RunwayFill);`
with `r = 63 - Div16(g_BaseYOff)`. (Craters on the runway are filled again at every new sortie.)

### 2.5 Tileset_Load @ 0x25c13 / Tileset_LoadTlx @ 0x133b3 (verified)

`Tileset_Load(name)`: `Tileset_LoadTlx(name)`, then an empty loop i=0..511 setting 0x8fedc=i%20, 0x8fea0=i/20
(leftover; final values 0x8fedc = 511%20 = 11, 0x8fea0 = 25 are scratch).
`Tileset_LoadTlx(name)`: `File_LoadWhole("map/", name, &g_PackBuf, 0)`; `if (!g_TileData) g_TileData =
malloc(0x10000)` (NULL → FatalError("Memory Error while loading ", "the map tiles ", 2)); `LZW_Unpack(g_PackBuf,
g_TileData)`; path = "map/" + name with the last 4 chars replaced by ".pal"; `fopen(path,"rb")` (NULL →
FatalError(path, " not found ", 1)); `fread(g_Palette+0x180, 0xC0, 1)`; fclose; `g_TilePtrs[i] = g_TileData +
i*256`; finally the last 4 chars of `name` are rewritten to ".tlx". Pixel format: `port/formats/maps.md` §2.

### 2.6 Parallax_Load @ 0x1351e — `void Parallax_Load(char *name /* g_TilesetName */)` (verified)

```c
if (!g_DetailParallax) { for (c = 0xC4; c < 0xD4; c++) Pal_SetColor12(c, g_NightMission ? 0x00A : 0x4AF); return; }
if (!g_Backdrop && !(g_Backdrop = malloc(0x280C8))) FatalError("Error allocating memory. While t...", "", 2);
n = strlen(name); name[n-3]='d'; name[n-2]='x'; name[n-1]= g_NightMission ? '1' : '0';
File_LoadWhole("map/", name, &g_PackBuf, 0); LZW_Unpack(g_PackBuf, g_Backdrop);
for (i = 0; i < 0x28000; i++) g_Backdrop[i] -= 0x40;       /* (also copies byte to 0x907f8/0x90830: scratch) */
name[n-3]='p'; if (!g_NightMission) { name[n-2]='0'; name[n-1]='0'+Rand(1); } else { name[n-2]='1'; name[n-1]='0'; }
File_LoadWhole("map/", name, &g_PackBuf, 0);
for (i = 0; i < 48; i++) { g_Palette[0x240+i] = g_PackBuf[i*4] >> 2; g_BackdropPal[i] /*0x92ad4*/ = same; }
/* empty loop c = 0xC0..0xCF */
```
Quirk (verified): the extension of g_TilesetName is left as `.p00/.p01/.p10`, so the next Mission_Setup's
`strcmp(g_TilesetPending, g_TilesetName)` always differs and the TLX is **reloaded every mission** when detail is
on. Harmless; keep (it also keeps the RNG sequence: one Rand(1) per day mission with detail on).

---------------------------------------------------------------------------------------------------

## 3. Accessors

| Function | Signature | Body (verified) |
|---|---|---|
| Map_GetTileAttr @ 0x10cb6 | `int (int x, int y, uint t)` | `return g_MapVal[(t&3)*256 + g_MapGrid[4 + y*W + x]];` (zero-extended byte) |
| Map_SetTile @ 0x10d22 | `void (int x, int y, uint8 tile)` | `g_MapGrid[4 + y*W + x] = tile;` |
| Map_GetTile @ 0x116dd | `int (int x, int y)` | `return g_MapGrid[4 + y*W + x];` |
| Map_ResetCounters @ 0x24377 | `void (void)` | `g_Scratch690 = 0; 0x90638 = 0; 0x90634 = 0;` |
| Map_FindTile @ 0x40e8b | `int (int v, int x0, int y0, int x1, int y1, int mode)` | see below |
| Map_ScanAround @ 0x3cdec | `uint8 (int x)` | see below |

`Map_FindTile` (verified): rows outer, columns inner; returns the first matching **column** (unwrapped x) or -1.
```c
for (y = y0; y < y1; y++) for (x = x0; x < x1; x++) {
    v2 = mode == 0 ? Map_GetTile(x % W, y) : Map_GetTileAttr(x % W, y, mode-1);
    if (v2 == v) { found = x; x = x1+1; y = y1+1; } }
```
All shipped callers use `mode = 1` (attr table 0) and a one-column window `x1 = x0+1`.

`Map_ScanAround(x)` (verified; caller EnemyAir_Update 0x36ae3): minimum live ground row over columns
`x-10 .. x+9`: `c = i<0 ? i+W : (i > W ? i-W : i); m = min(m, g_MapVal[0x400+c])`, start m = 63. Quirk: `i == W`
is not wrapped (reads height byte W, which is outside the map: stale/garbage); PORT: keep the index, with the
zero-initialised buffer it reads 0 → **returns 0**. Recommendation: keep (faithful) but zero-init.

---------------------------------------------------------------------------------------------------

## 4. Terrain damage

### 4.1 Map_CraterAt @ 0x3b777 — `void Map_CraterAt(int px, int py, int dmg)` (verified)

Only caller: Explosion_Damage 0x39b69 (per impact point, px/py world pixels, dmg = blast value).
Globals: 0x90008 g_CrTile, 0x904e8, 0x90530, 0x90ac0, 0x904e4, 0x903f8 g_SpreadL, 0x90178 g_SpreadR,
0x8fec4 g_CrCol, 0x907ac, 0x90100 (smoke points), 0x902fc (secondary explosions), 0x90254 current player,
0x9045c planeClass, 0x90534, 0x901c8 (own-base damage counter), 0x907b8 (secondary explosion in progress).

```c
if (py <= 0) return;
x = Div16(Clamp(px + 8, 0, W*16 - 1));
y = Div16(min(py + 8, 0x3FF));                    /* row 0..63 */
g_CrTile = 0x904e8 = attr(x,y,1);                 /* replacement tile                                   */
0x90530 = attr(x,y,2);                            /* armour                                             */
if (attr(x,y,0) == 0xC7) { g_SpecialHit = 1; Hud_PushMessage("Om"); }
0x90ac0 = (y == 63) * attr(x, Clamp(y-1, 0, 63), 2);   /* armour of the tile ABOVE, only on the bottom row  */
if ((0x90530*10 <= dmg && 0x90ac0*10 <= dmg) || g_CrTile == g_SkyTile) {
    0x904e4 = attr(x,y,3);   dmg = 5000;           /* dmg is dead after this                            */
    if (0x904e4 == 0xFF) g_CrTile = attr(x,y,1);
    else {
        g_Score[p] += attr(x,y,2) * (planeClass + 1);
        if (g_CrTile != g_SkyTile) {
            cy = y > 63 ? 63 : y;  cy = cy < 1 ? 1 : cy;          /* rows used for the table-3 tests        */
            if (0x90100 < 6 && Rand(10) == 1 && g_GameMode < 3) { /* Rand only evaluated when 0x90100 < 6    */
                smokeX[0x90100] /*0x8ec48*/ = x*16 - 8;  smokeY /*0x8ec60*/ = y*16 - 8;
                smokeT /*0x8ec30*/ = Rand(20) + 4;
                if (g_MapVal[0x100 + g_CrTile] == g_CrTile && !(attr(x,cy,3) & 1))
                    Particle_Spawn(x*0x1000 - 0x800, y*0x1000 - (g_MapVal[g_CrTile] > 0x7e)*0x1000, 0,0,0,0x40,0x15);
                0x90100++;
            }
            if (0x907b8 == 1 && g_MapVal[0x100 + g_CrTile] == g_CrTile && !(attr(x,cy,3) & 1))
                Particle_Spawn(same args);
            if (attr(x,cy,3) & 4) { secX[0x902fc] /*0x8dfd0*/ = x<<4; secY /*0x8e050*/ = y<<4; secDmg /*0x8df50*/ = 2000; 0x902fc++; }
            if ((attr(x,cy,3) & 0x10) && 0x90320 < 10) Particle_Spawn(x<<12, y<<12, 0,0,0x10,0x20,0x19);
            if (g_BaseStartX <= x*16 && x*16 <= g_BaseEndX && 0x90534 == 0) {
                g_Score[p] = max(g_Score[p] - 1000, 0);  0x901c8++; }
        }
    }
    Map_SetTile(x, y, g_CrTile);  g_CrTile = g_SkyTile;  y--;
} else y = 0;
/* ---- column collapse ---- */
g_SpreadL = g_SpreadR = 0;  g_CrCol = x;
if (g_MapVal[0x400+x] <= y && x > 1 && x < W-1 && y > 12) {
    r = Map_DamageColumn(x, y, g_SkyTile, 0);   g_Score[p] += g_ColArmourSum /*0x933b0*/;
    h = g_MapVal[0x400+x];  0x904e8 = h;
    if (r < h) g_MapVal[0x400+x] = y;
    for (v = h; v < y && v > 0; v += Rand(2) + 1) Rand(2);     /* 2 Rand per iteration, result unused   */
    if (g_MapVal[0x400+x-1] <= y && tile(x-1,y) == g_SkyTile) g_SpreadL = y+1;
    if (g_MapVal[0x400+x+1] <= y && tile(x+1,y) == g_SkyTile) g_SpreadR = y+1;
    for (; g_SpreadL > 0 && x > 0; x--) {                       /* first iteration is column x itself    */
        r = Map_DamageColumn(x, g_SpreadL, g_SkyTile, 1);  g_Score[p] += g_ColArmourSum;
        h = g_MapVal[0x400+x];
        if (r < h) g_MapVal[0x400+x] = g_SpreadL;
        Rand(2);                                                 /* result unused                          */
        if (g_MapVal[0x400+x-1] > g_SpreadL || x < g_CrCol-3 ||
            attr(x-1, g_SpreadL, 1) != tile(x-1, g_SpreadL)) g_SpreadL = 0;
    }
    for (x = g_CrCol; g_SpreadR > 0; x++) {                     /* no upper bound on x                    */
        r = Map_DamageColumn(x, g_SpreadR, g_SkyTile, 1);  g_Score[p] += g_ColArmourSum;
        h = g_MapVal[0x400+x];
        if (r <= h) g_MapVal[0x400+x] = g_SpreadR;               /* <= here, < on the left: original      */
        Rand(2);
        if (g_MapVal[0x400+x+1] > g_SpreadR || x > g_CrCol+3 ||
            attr(x+1, g_SpreadR, 1) != tile(x+1, g_SpreadR)) g_SpreadR = 0;
    }
}
```
(`p` = 0x90254; the left-loop condition order is: height test, then `x < g_CrCol-3`, then the attr1/tile test;
when either of the first two holds the attr test is skipped — matters only for side effects, none here.)

RNG sequence per call (in order): [Rand(10) if 0x90100<6, then Rand(20) if it returned 1] ; column collapse:
2 per height-loop step; 1 per left-spread column; 1 per right-spread column.

**Uninitialised return value (verified by stack-frame arithmetic, important for the RNG-free state):** the
first, mode-0 Map_DamageColumn call returns its local `[ebp-0x10]` without having written it whenever the walk
stops because of the height test (§4.2). That stack slot is the one in which the previous **direct**
Map_GetTileAttr call made by Map_CraterAt stored its return value (Map_GetTileAttr keeps its result at
`[ebp-0x14]`, which is the same address; the calls in between — Map_SetTile, Byte_Get, Particle_Spawn — do not
touch it). So `r` for the first call =
* `attr(x,y,1)` when table 3 was 0xFF,
* `attr(x,y,2)` when the replacement tile was the sky tile,
* `attr(x,cy,3)` in the normal destroyed path (the last test, bit 0x10).
(The column-collapse block is not reached when nothing was destroyed, because then y = 0.) PORT: implement
exactly this rule ("last attribute read"); conf **likely** (assumes no interrupt handler writes this deep into
the stack between the calls; DOS/4GW runs IRQ handlers on its own stack).

### 4.2 Map_DamageColumn @ 0x3f195 — `int Map_DamageColumn(int x, int y, int tile, int mode)` (verified)

Walks up the column from row y, clearing it; `g_ColArmourSum` 0x933b0 = sum of the armour of the visited tiles
(added to the score by the caller through FUN_0003f2d8 0x3f2d8 = `return g_ColArmourSum`).
```c
g_ColArmourSum = 0;   int ret /* [ebp-0x14], uninit */, last /* [ebp-0x10], uninit */;
for (i = y; i > 0; i--, y--) {           /* i and y always equal                                            */
    a = attr(x,y,2);  g_ColArmourSum += a;
    if (a > 200) { last = attr(x,y,1); Map_SetTile(x,y,last); i = -1; ret = mode; }      /* bedrock: stop  */
    else if (mode) { last = attr(x,y,1); Map_SetTile(x,y,last); mode = 0; }              /* first tile only */
    else { Map_SetTile(x,y,tile);
           if (g_MapVal[0x400+x] > i) { i = -1; ret = last; } }                           /* above the ground */
}
return ret;
```
Quirks: with mode 0 and no bedrock, `ret = last` is uninitialised (§4.1 rule). If the loop never stops early
(height 0) or y <= 0, `ret` itself is uninitialised (`[ebp-0x14]`); not reachable from Map_CraterAt (y > 12,
and a column of height 0 does not occur). The returned value is compared with a **row**, although in mode 1 it
is a **tile id** (`last`) — an original bug; keep.

### 4.3 Secondary explosions (verified)

Game_Run after Hud_DrawMessages: `if (0x902fc) { FUN_00014abe(); 0x902fc = 0; }`;
FUN_00014abe = `for i < 0x902fc: { 0x907b8 = 1; Explosion_Damage(secX[i], secY[i], 0, 0, secDmg[i], secDmg[i]); }`.
No bound on 0x902fc (arrays of 32 ints at 0x8dfd0/0x8e050/0x8df50); chained explosions within one
Explosion_Damage can append more. PORT: bound the array (assert) — never exceeded in practice (likely).

---------------------------------------------------------------------------------------------------

## 5. MP2 column trigger

### 5.1 Callers (verified)

* Game_Run 0x1cfa6..0x1d088, every frame after Level_DrawBackground, when
  `(g_CamCol 0x904dc != col || g_CamRowK 0x904e0 != rowK || 0x904dc == -1) && 0x9099c == 0` where
  `col = Div16(g_CamX)` (0x904f0) and `rowK = Div16(g_CamY + 0x800)` (0x904c4). Before that:
  `if (abs(col - 0x904dc) > 16) 0x904dc = col - 0x907bc`. Then
  ```c
  g_TrigCol = Div16(g_CamX + g_PlayerScrX + g_ScrollFineX) % (W - 1);   /* fine scroll added twice: quirk */
  g_Scratch690 = g_MapMp2[g_TrigCol];
  0x90668 = 0x9010c = 0x90268 = 0; g_RadarJammed = 0;
  if (g_Scratch690 && 0x907fc == 0) Map_TriggerColumn();
  ```
  After the block (always): `0x907bc = 0x907a4; 0x9076c = 0x907a8; 0x90384 = g_CamX; 0x90374 = g_CamY;
  0x904dc = col; 0x904e0 = rowK`. So the trigger runs only on frames where the camera changed tile.
* `FUN_0003e66b` (weapon-camera path; callers Projectiles_Update 0x332fb/0x33308 and 0x413a0): resets
  0x90668/0x9010c/0x90268 (not g_RadarJammed), `g_TrigCol = Div16(g_CamX + g_PlayerScrX + g_ScrollFineX + 16) %
  (W-1)`, then the same read/call without the 0x907fc test.

### 5.2 Map_TriggerColumn @ 0x3e1a3 (was FUN_0003e1a3) — `void (void)` (verified)

```c
if (0x9063C > 0) { Map_SetTile(0x9063C, 0x90640, 0x90630); 0x9063C = 0; }   /* restore blanked tiles      */
if (0x900E8 > 0) { Map_SetTile(0x900E8, 0x900EC, 0x900E4); 0x900E8 = 0; }
if (0x90230 > 0) { Map_SetTile(0x90230, 0x90234, 0x9022C); 0x90230 = 0; }
v = g_Scratch690;  c = g_TrigCol;
if (v < 0x80) {                                     /* direct row                                           */
    g_TrigClass = attr(c, v, 0);  g_TrigOff = 0;  g_TrigRow = v;
    if (g_TrigClass < 0x80 && g_TrigClass != 5) {
        k = g_Mission/10 < 1 ? 1 : g_Mission/10;
        if (Rand(12) < k && 0x902C0 == 0 &&                          /* Rand always consumed here           */
            attr(c, min(v+1, 63), 0) > 0x7E) {                       /* solid tile below                    */
            0x902C0 = c; 0x902C4 = v;                                /* ground launcher -> Building_Update   */
            0x902BC = Rand((g_Mission > 30) + 1);  0x902DC = Rand(100) + 40;
        }
    }
} else {                                            /* relative pointer                                     */
    g_Scratch690 = v - 0xA0;                        /* off, -32..+95                                        */
    t = Clamp(c + off, 0, W-1);   r = g_MapMp2[t];  0x906B4 = r;
    g_TrigClass = attr(t, min(r, 63), 0);
    g_TrigOff = off;  g_TrigRow = r;                /* NOT clamped (used below)                             */
}
X = c + g_TrigOff;  Y = g_TrigRow;
switch-like (all independent ifs, in this order):
  0x83: 0x90668 = X; 0x90614 = Y; 0x90630 = tile(X,Y); Map_SetTile(X,Y,g_SkyTile); 0x9063C = X; 0x90640 = Y;
  0x84: 0x9010C = X; 0x90110 = Y; 0x900E4 = tile(X,Y); Map_SetTile(X,Y,g_SkyTile); 0x900E8 = X; 0x900EC = Y;
  0x85: 0x90268 = X; 0x9026C = Y; 0x9022C = tile(X,Y); Map_SetTile(X,Y,g_SkyTile); 0x90230 = X; 0x90234 = Y;
  0x86: g_RadarJammed = 1; 0x90574 = 0; 0x90454 = -1;
  0x05: g_MarkerX = X*16 - 16; g_MarkerY = Y*16 - 16;
```
RNG: direct rows with class < 0x80 and != 5: Rand(12), then (if it spawns) Rand(1 or 2), Rand(100).

Game_Run then (0x1d84x.., verified) for each object class: if its active column (0x9010c / 0x90268 / 0x90668)
is 0 (or, for 0x83/0x85, 0x9099c != 0) and a blanked record exists → restore the tile now; else run the
object's sprite routine (0x84: FUN_0003e0b2 + FUN_0003ef72; 0x85: FUN_0003dea2, then FUN_0003e8b3 if
0x907fc==0 && 0x8ffa0<10 && mode is 0 or 2; 0x83: FUN_0003dfaa, then FUN_0003ec5b if 0x90660==0 && 0x907fc==0)
and set the target marker 0x90574/0x9052c to the object (x*16, y*16). Those routines are AI spec.
Net effect: a map tile of class 0x83..0x85 is replaced by a live sprite object while the player is within
the MP2 window, and painted back when he leaves (or the trigger re-runs).

---------------------------------------------------------------------------------------------------

## 6. Camera

### 6.1 Model (verified unless noted)

* World position of the player = `(g_CamX + g_PlayerScrX, g_CamY + g_PlayerScrY)`; g_CamX/g_CamY
  (0x903a8/0x903ac) = top-left of the 320x175 view in world pixels. y grows downward; the map bottom is
  1024 px (row 63); g_CamY is clamped to **[-2000, 0x340]** every frame (0x1cde7), so the sky above the map is
  2000 px tall (no tiles; Level_DrawBackground rows < 2 / video spec).
* Mission start (Mission_Setup 0x21aa3..): `g_CamY = 0x340 - 0x90064` (0x90064: plane-dependent, player spec),
  `0x900a8 = g_CamY`, `g_CamX = g_BaseEndX - 0x140`, `g_PlayerScrX = 0xA0`,
  `g_PlayerScrY = 0x9F - 0x8ff0c - g_BaseYOff`, `0x90384 = g_CamX` (previous camX), `g_MapWidthPx = W*16`.
* Horizontal: Game_Run 0x1ed89 `g_CamX += g_PlayerVX` (0x90778, px/frame; the plane is kept near screen x
  0x28..0x120 by drifting g_PlayerScrX: `scrX -= Sign(scrX - 0x28)` normally, or `scrX = Clamp(scrX -
  Sign(vx), 0x20, 0x120)` at |vx| == 16 with high speed). Player spec owns these lines.
* Vertical: the camera scrolls instead of the player while `g_CamY < 0x340`
  (`g_CamY += Clamp(vy, -16, 16)`); at the bottom limit the player sprite moves instead. After the player
  update (0x20ac8..): `if (scrY < 0x50) { g_CamY += scrY - 0x50; scrY = 0x50; } if (scrY > 0xA0) { g_CamY += scrY -
  0xA0; scrY = 0xA0; }` — a dead band of screen rows 0x50..0xA0.
* **Horizontal wrap** (0x1cd9d, every frame, before drawing):
  ```c
  if (g_CamX > g_MapWidthPx) { g_CamX &= 15;                          g_ViewTarget = -1; }
  if (g_CamX < 1)            { g_CamX = g_MapWidthPx - 16 + (g_CamX & 15); g_ViewTarget = -1; }
  ```
  Equivalent to ±W*16 for overshoots below 16 px, except camX == 0 maps to W*16-16 (a 16 px jump; quirk,
  keep). Any wrap cancels a follow view. Column indices are wrapped with `% W` / `% (W-1)` by their users.
* Fine scroll: `g_ScrollFineX = g_CamX & 15`, `g_ScrollFineY = g_CamY & 15` (video spec; also stored in
  0x90604 the previous fineX). `0x907a4/0x907a8` = camera delta this frame (`g_CamX - 0x90384`, `g_CamY -
  0x90374`); if either |delta| > 16 (a wrap or view switch) the previous frame's delta (0x907bc/0x9076c) is used
  instead. Draw origin `Level_DrawBackground(Div16(g_CamX)+1, Div16(clampY)+1)` with `clampY = min(g_CamY,
  0x340)`, negative → `% 64` (0x9039c).
* Player_LandingCheck 0x15957 nudges `g_CamX ±= 4` toward `g_BaseStartX + 0x82 - scrX` (player spec).

### 6.2 Follow / alternative views (Player_Update 0x2d34e, Game_Run; verified)

`g_ViewX/g_ViewY` (0x8ff68/0x8ff70): when either is > 0 at the start of a frame (0x1ccc6) **and** again
before drawing the sprites (0x1e61e), Game_Run swaps (SwapInt) g_CamX<->g_ViewX, g_CamY<->g_ViewY,
0x90384<->0x8ffa4, 0x90374<->0x8ff50. So the world is simulated with the real camera and drawn with the view
camera. Player_Update clears them each frame (after saving to 0x8ffa4/0x8ff50) and recomputes:

| g_ViewTarget 0x8ff10 | view (x = obj - 0xA0, y = obj - 0x58, y clamped) | ends when |
|---|---|---|
| -1 | none | - |
| 0..49 | projectile slot i (0x92830[i], 0x92884[i]); y <= 0x340 | slot >= 0x908bc, or its kind 0x91384 == 8 |
| 50 (0x32) | B52 (0x90984/0x90988), y >= -0x800 | 0x90998 == 0 |
| 100 | Fat Albert (0x90520/0x904c0), y >= -0x800 | 0x90528 == 0 |
| 150 (0x96) | ground force (0x8ddc0/0x8dd98), y in [-0x800, 0x340] | 0x902e0 == 0 |
| 200 | enemy aircraft 0 (0x9264c/0x92660), same clamp | 0x90708 == 0 |
| < -1 | hold the last view (0x903c4/0x903c8), decrement; < -15 → -1 | |

Each successful case stores the view into 0x903c4/0x903c8. KP-* (key slot 0x8453e) = "Looking around":
`g_ViewX = g_CamX - 0x140*(Left - Right)`, `g_ViewY = g_CamY - 0xB0*(Up - Down)` (key slots 0x84552/0x84564,
0x84560/0x8454e, cleared after), sets g_ViewTarget = -1. Backspace (g_KeySlots 0x8453a, when 0x90814 == 0)
cycles -1 → "Ready to follow weapon" (target = next projectile slot 0x908bc) → B52 → Fat Albert → Ground Force
→ Enemy Aircraft → "Follow Aborted" in up to 6 passes, skipping absent objects (the check order inside one
pass is 200, 150, 100, 50, 0..49, -1). Quirk: "Follow Aborted" sets -1 and the same pass then reaches the -1
case, so the message is overwritten with "Ready to follow weapon" and following re-arms immediately. Keep.
E key (0x84548) with 0x9099c == 0 starts the ejection view: `0x9099c = 1`, the player screen position and
camera are frozen in 0x90270/0x90274 and 0x9024c/0x90250 (Game_Run restores them each frame while 0x9099c > 0;
0x9024c creeps +2 px when it shares a 32-px block with camX). Ejection itself: player spec.

`Camera_Update` 0x403c1 is **not** a camera routine: it is the VTOL/hover vertical physics (x87 soft-float,
writes 0x90094, 0x90580, g_PlayerScrY; uses table-3 bit 0x40). Renamed `Player_HoverUpdate`; the player spec
should own its float details.

---------------------------------------------------------------------------------------------------

## 7. Mission parameters: set-up per parameter

Parameter names/addresses as in `port/js_symbols.csv` (g_MissionParams 0x91648, word i at +2i, unsigned
16-bit after the byte swap; arithmetic below is on the zero-extended value). Order of Mission_LoadBriefing
(verified, map-related part):

1. Clear: 0x902c0, 0x90190..0x9019c, 0x9014c, 0x90144, 0x91140[0..39], 0x908d8, 0x908e8, 0x902e0, 0x8ffb8,
   0x900a4=1, **g_AgentDropPending=0**, crate 0x90948=0, 0x90a40=0, 0x90a44=0, 0x90a10=0, 0x90918=0.
2. Read the record, byte-swap the 30 params. `g_MissionBonus = (g_Mission+1)*1000` (32000 in mode 3).
3. **p29**: `0x90148 = p29/1000; p29 %= 1000` (0x90148 = chance flag of the random event FUN_00014923; plane
   select overwrites it with MISC.Z word 64). `0x905c0 = 0; 0x909d8 = 0; 0x90400 = p00/100`.
4. **p09**: `bVar = p09 > 999; if (bVar) p09 %= 1000;` `0x8ffe0 = bVar` (convoy turns round).
5. **p06 > 1000**: escort mode `0x905c0 = 1; 0x909d8 = min(p06 % 1000, p09); p06 = 0`.
6. **p29 != 0** (after step 3, so the remainder): `0x8deb0 = 0x8deb4 = p29` (no reader found; these words sit
   just before the table 0x8deb8 and would only be read with a negative index) — guess: dead.
7. **p10 > 999**: `n = p10/1000 → 0x90348` (EnemyGround count); for i < n: `x[i] 0x8d9d8 = i*64 + p03*8`,
   `y[i] 0x8d9e8 = g_MapVal[0x400 + Div16(x[i])]*16 - 32`, 0x8d9c8=3, 0x8d8f8=-4, 0x8d8e8=8, 0x8d9b8=4,
   0x8d7d8=100, 0x8d9a8=4, 0x8d998=0; then `p10 %= 1000`. (Uses the heights of the map currently in memory,
   which is the previous map when this record changes maps — quirk, no shipped record combines both.)
8. Default weapons (record +0x1b8). **p25**: `g_NightMission 0x9035c = (p25 == 1)`.
9. Mode < 3: **p26/p27**: `if (p26 && p26 < 2000) { g_EnemyBaseX 0x906f4 = p26; g_EnemyBaseRow 0x9070c = p27;
   if (p27 > 63) g_EnemyBaseRow >>= 4; 0x90410 = 0; }` (consumers: EnemyBomber_Spawn, Game_Run scramble: AI
   spec; spawn x = p26*16 - 0x40, y = row*16 - 3 per data.md). `if (p08) 0x90708 = 0`.
   Mode 3: **gates** `g_AeroGateX[i] = p(15+2i) << 4; g_AeroGateY[i] = p(16+2i) << 4` for i<4, then p15..p22 = 0;
   `if (g_AeroGateX[0] > 0) p09 = 1` (makes the course an objective); `0x90334 = 0` (current gate).
10. `0x905ac = 0; 0x90a30 = p24; 0x90a38 = (p24 == 0xCA || p24 == 0xAC)` (animated agent pickup).
11. **p08**: `g_EnemySetIndex = p08/100 + 1; if (p08 > 99 && mode < 3) p08 %= 100;` load DATA/ENEMIES if changed.
12. `if (p00 == 2) g_Lives = 0`. Map name digit stripping (twice), plane-stat restores.
13. Story screen, briefing page; **fog decision** (§9); F-key save.
14. Mission-start pass (0x23696..0x23f66), in this order:
    * steal/agent conversion: `if (2000 < p03 && p03 < 5000) { old = p03; p03 -= 2000; if (p04 < p03) p04 = old -
      2000; g_AgentDropPending = 1; }` (p03 == 2000 exactly stays form (a) with column 2000).
    * map load if the name changed (§2.2) + Map_ResetCounters.
    * **p08 / enemies**: mode < 3 and p08 != 0: `0x90a5c = min(p08, 2); EnemyBomber_Spawn()`.
      Mode 3 and p03 != 0: `0x90a5c = 0; 0x90708 = 0;` and, if !g_AgentDropPending, the **Aerolimits pad**
      (§8.6), then `p07 = p08 = 0`.
    * Mode 3: **crate**: `if (p04) { 0x90948 = p04<<4; 0x9094c = (int16)p05<<4 (p05 == 0: -0x400); 0x9091c = p06;
      0x90940 = p24; p24 = 0; }`; `if (p03) { p04 = p03 + 5 + 5*pending; p03 = p03 - 5 - 5*pending; }`
      (16-bit arithmetic).
      Modes 0-2: `g_TargetTilesInit 0x902f4 = 0; if (p03 > 4999) count form-(c) targets` (§8.3).
    * **p19**: `if (p19) Map_StampBertha()` (§8.5).
    * `0x9046c = 1; 0x9038c = 1;`
    * **p14**: `if (p14) { y = 63; while (attr(p14, y, 0) > 0x7E) y--; g_PickupY 0x909ec = y*16; }` (top of the
      ground; endless loop if the column is solid to row 0 — none). `if (p24 > 500) 0x90a40 = p14 - 1` (agent
      rides convoy vehicle p14-1).
15. Mission_Setup later: `0x909e8 = p14 << 4` (pickup x), and `if (p09 && mode<3) { Enemy_SetupSpriteIds(); p09 =
    0; }` — **p09 is zeroed before play** in modes 0-2, so p09 is an objective only in Aerolimits (gates);
    the convoy's own completion goes through p06 (AI spec).

Per-parameter summary of everything else consumed by this spec:

| param | where (this spec) | behaviour |
|---|---|---|
| p01/p02 recon | Game_Run 0x1e4a0 | `if (p01 && p01-1 <= g_TrigCol' <= p01+1 && p02*16 <= g_CamY + g_PlayerScrY)` → p01 = p02 = 0, HUD "RECON PHOTOS TAKEN" (mode 3: "Mind Your Head!"), FUN_000400da (photo flash). The column tested is whatever 0x8ff18 holds at that point: the MP2 trigger column of this frame (§5.1) if the camera changed tile, otherwise the player column `Div16(g_CamX+g_PlayerScrX)+1` (wrapped at W) written later in the previous frame (0x1e9xx). Keep this order. "Row >= p02" means **at or below** that altitude (fly low). verified |
| p03/p04/p07 | §8 | |
| p20 | Game_Run 0x1d62a | raid counter `0x906a4 += Rand(2)` per frame while no enemy aircraft is active (0x90708 == 0), +1 more while the player's row `Div16(g_CamY+scrY) < p20` (flying **above** the radar ceiling); `> 300 && (mode&1)==0` → `0x90a5c = Rand(2)+1; EnemyBomber_Spawn(); 0x906a4 = 0`. Also §8.4 (clears p20/p21 — treats p20 as a column). verified |
| p21 | Game_Run 0x1d822 | nonzero: marker x = `((p04 - p03)/2 + p03)*16`, y = 0x3e0 every frame (even when p03 == 0: values 2210/4200/2350 in M0 #33/#71/#104 then point at column 0..). Guided weapons/arrow (AI/weapons) also test p21 && p03. verified/guess (meaning of the large values) |
| p22 | 0x18bc8, 0x438f2 | `p03 && p22 == 1 && p03 < 5000`: enemy carrier/ship target handling (M0 #21 converted cruiser, #117 cargo ship). AI spec. likely |
| p23 | Convoy_Update | AI spec |
| p24 | §7 step 10/14, Crate_Update | pickup / crate sprite |
| p25 | §9 | 0 random fog, 1 night, 2 fog |
| p28 | §8.5 | Bertha top row (M0 #33 has p28 = 1000 with p19 = 0: unused) |

---------------------------------------------------------------------------------------------------

## 8. Objectives

### 8.1 Mission_CheckComplete @ 0x3f02e — `void (void)` (verified)

Called by Game_Run 0x1f422 once per frame **only while `0x90440 == 1`** (player on the ground); otherwise
Game_Run sets `0x907c0 = 0`, re-arming the objective check for the next landing.
```c
if (0x90094 == 0 && g_PlayerVX == 0 && g_BaseStartX < camX+scrX && camX+scrX < g_BaseEndX &&
    g_MissionResult == 0 && 0x907c0 == 0 && 0x907fc == 0) {
    Mission_CheckObjectives();                                  /* only when stopped on the runway       */
    if ((0x901b8 != 0 || 0x902cc > 0) && g_GameMode < 3) {      /* landed-with-cargo flags (player spec) */
        Mission_CompleteScreen(); 0x901b0 = 0x901b8 = 0x902cc = 0; } }
if (0x90834 == 0 && 0x901f0/2 == 0 && 0x9005c == 1) { 0x90200 = 4; 0x901f0 = 2; }
if (0x904fc == 1 && 0x90094 > 0 && 0x90994 == 0xFE && 0x901f0/2 < 4 && 0x904b8 == 0) { 0x904b8 = 7; 0x8ffd4 = 0; }
if (0x90094 == 0) Player_LandingCheck();
```
(the last three statements are player/carrier logic; listed for completeness). 0x90094 is a float stored as
int bits (vertical speed); `== 0` / `> 0` compare the raw bits (verified, integer compares).

### 8.2 Mission_CheckObjectives @ 0x44a5d — `void (void)` (verified)

Runs once per landing (sets 0x907c0 = 1; Game_Run clears it while airborne, §8.1).
```c
if (0x905c0) p06 = (int)0x909d8 < 1 ? 0 : 0x909d8;             /* escort: survivors needed         */
g_TargetsDone 0x90504 = 0;  g_AeroPadScore 0x903b8 = 0;
if (g_GameMode < 3) {
  if (p03 >= 5000) {                                             /* form (c), §8.3                   */
    0x90830 = 0;
    for (j = 0; j < p04; j++) for (x = 0; x < W; x++) {
        f = Map_FindTile(p03-5000+j, x, g_MapVal[0xBD0+x]-1, x+1, 64, 1);
        if (f > 1) { 0x90830++; x = f + 1; } }                  /* skips column x+1: quirk          */
    g_TargetsDone = g_TargetTilesInit - 0x90830;
    if (g_TargetTilesInit < p07) g_TargetsDone = p07;            /* too few targets: auto-done       */
  } else if (!g_AgentDropPending) {                              /* form (a)                          */
    for (x = p03; x <= p04; x++)                                 /* rubble class 2                   */
      if (Map_FindTile(2, x, orig[x]-1, x+1, 64, 1) > -1 || live[x] > orig[x]) g_TargetsDone++;
    for (x = p03; x <= p04; x++)                                 /* rubble class 0xA0                */
      if (Map_FindTile(0xA0, x, orig[x]-1, x+1, 64, 1) > -1 || live[x] > orig[x]) g_TargetsDone++;
  }
  if (p07 <= g_TargetsDone && !g_AgentDropPending) p03 = p04 = p07 = 0;
  if (p20) {
    if (Map_FindTile(2,    p20, 0, p20+1, 64, 1) > -1) p20 = p21 = 0;
    if (Map_FindTile(0xA0, p20, 0, p20+1, 64, 1) > -1) p20 = p21 = 0;   /* p20 is 0 now: scans column 0 */
  }
} else if (p03 && !g_AgentDropPending) {                         /* Aerolimits target pad             */
  for (i = 0; i < 11; i++) {
    f = Map_FindTile(2, p03+i, 0, p03+i+1, 64, 1);
    if (f == -1) f = Map_FindTile(0xA0, p03+i, 0, p03+i+1, 64, 1);
    if (f > -1 || live[i] > orig[p03+i]) {                      /* live[i]: should be live[p03+i]     */
        g_AeroPadScore += (4 - abs(i-5)) * 2500;  p03 = p04 = 0; } }   /* later i scan from column 0+i */
}
0x907c0 = 1;
any = 0; for (i = 0; i < 22; i++) if (param[i] && g_ParamNotObjective[i*2 + (g_GameMode == 3)] == 0) any = 1;
if (0x905c0) { 0x909d8 = p06; p06 = 0; }
if (!any) { g_MissionResult = 0x9025c + 1;  0x903d0 = 0x8ffec;  0x903d8 = g_BaseEndX;
            Hud_PushMessage(g_HudText[0x90a10*40 + 18]); }     /* 18 MISSION COMPLETED, 58 MISSION FAILED */
```
"live[x] > orig[x]" = the column's ground row went **down** (row number larger), i.e. the surface was lowered
by craters. Comparisons on the byte values are unsigned (`cmp bl, al; jbe`). With the shipped GENDATA the
objective params are p00..p07, p09, p14 (both modes).

Quirks (keep for faithfulness, PORT notes):
* Form (a) counts a lowered column **twice** (once per loop), and a column with both rubble classes twice.
  So p07 = number of "hits", not distinct columns.
* Form (c) start count (§8.3) does not skip columns, the check does (`x = f+1` then `x++`), and only counts
  f > 1: adjacent target columns make the "remaining" count lower than the initial one → targets look
  destroyed. M0 #30 has p04 = 0 (no classes) → initial 0 < p07 → the target objective completes at once.
* Aerolimits pad: `live[i]` instead of `live[p03+i]`; after the first hit p03 = 0 and the remaining iterations
  scan columns i; score can be negative (i = 0 or 10 gives -2500).
* p20 is a row elsewhere but a column here.

### 8.3 Form (c) initial count (Mission_LoadBriefing 0x23e26, verified)

`g_TargetTilesInit = 0; if (p03 > 4999) for (x = 0; x < W; x++) for (j = 0; j < p04; j++)
if (Map_FindTile(p03-5000+j, x, orig[x]-1, x+1, 64, 1) > -1) g_TargetTilesInit++;` — loops in the opposite
nesting to the check, every match counts (no `> 1` filter, no skip). The search starts one row above the
original surface: targets standing on the ground are included.

### 8.4 Target forms (summary, verified)

* (a) `1 <= p03 <= 2000`: column range p03..p04, done when `g_TargetsDone >= p07`.
* (b) `2000 < p03 < 5000`: **agent drop** (not "steal aircraft"; the briefings say "drop a secret agent... drop
  zone marked with smoke"), §8.7.
* (c) `p03 >= 5000`: attr0 classes `p03-5000 .. p03-5000+p04-1` anywhere (on/below the original surface).

### 8.5 Bertha (verified)

`Map_StampBertha @ 0x243d6 (void)`: `g_Scratch690 = p19/1000; name = "bertha" + ('A' + p19/1000);
File_LoadWhole("data/", name, &g_MapBuf, 0); g_BerthaCol 0x908e8 = p19 % 1000; g_BerthaRow 0x908ec = p28;
g_BerthaW 0x908e4 = BE16[0]; g_BerthaH 0x908dc = BE16[1]; g_BerthaDelay 0x908c8 = 600;` then for row j < H, col
i < W (k from 4): `Map_SetTile(col+i, row+j, data[k++]); if (row < live[col+i]) live[col+i] = row;` (heights
lowered to the stamp's top row for every stamped column, even where the stamp byte is sky).

Game_Run per frame: `0x908c8 -= Sign(0x908c8)` (0x20ccx), then
`if (p19 && Rand(101) == 1 && g_BerthaDelay < 1) Bertha_Update();` (Rand only when p19 != 0; ~1/102 per
frame after the 600-frame delay), then the shell timer:
`if (0x908d8 && ++0x908d8 == 20) { 0x908d8 = 0; Explosion_Damage(0x908f4, 0x3FF - g_BaseYOff, 0, 0, 2000, 2000); }`.

`Bertha_Update @ 0x1af0f (void)`:
```c
n = 0;
for (x = col; x < col+W; x++) for (y = row; y < row+H; y++) {
    a = attr(x,y,0);  0x90248 = a;  if (a == 2 || a == 0xA0 || a == 0) n++; }      /* destroyed/empty cells */
g_Scratch690 = n;
if (n < p07) {
    0x908f4 = Rand(g_BaseEndX - g_BaseStartX) + g_BaseStartX;     /* shell lands on the player's base   */
    Explosion_Damage((col+W)*16 + 16, row*16 - 16, 16, -16, 300, 300);   /* muzzle blast, right of the gun */
    0x908d8 = 1;
} else p19 = 0;
```
So the gun shells the **runway** (20 frames after each muzzle blast, 2000 damage) until p07 of its cells are
air/rubble. Note that empty (class 0) cells of the stamp count as destroyed from the start; shipped stamps
need p07 = 2..3 (30 for M0 #109).

### 8.6 Aerolimits target pad (Mission_LoadBriefing 0x23cdb, verified)

```c
for (i = p03-3; i <= p03+3; i++) Map_SetTile(p07, i, 0x3E);     /* x = p07, y = i  (!)                */
for (i = p03-5; i <= p03+5; i++) Map_SetTile(p08, i, 0x3F);
p07 = p08 = 0;
```
The shipped records (M3 #3/#12/#13: p03 = 31, p07 = 253, p08 = 69) therefore put vertical strips of the slope
tiles 0x3E/0x3F at columns 253 and 69, rows 26..36 (mid-air, class 0x7F = solid), while the scored pad is
columns p03-5..p03+5 = 26..36 at row 63 — where AOLIM01 already has the target-board tiles 67..74 on the
ground. The x/y order is almost certainly an original bug (with x = i, y = p07 the rows 253/69 would be out of
range, so the data cannot be "fixed" by swapping either). Recommendation: keep (faithful); the visible effect
is two floating zig-zag columns. The strips persist for the rest of the Aerolimits session (blank map name).

### 8.7 Agent drop (form b)

`StealMission_Update @ 0x15387` → **AgentDrop_Update** (runs every frame while g_AgentDropPending):
```c
0x90574 = ((p04 - p03)/2 + p03)*16 - 8;  0x9052c = 0x3E0;                   /* HUD marker: zone centre */
if (p03*16 - 0x148 < g_CamX && g_CamX < p03*16 + 0x1C0 && 0x90100 < 6 && Rand(4) == 1) {
    smokeX[0x90100] = ((p04 - p03)/2 + p04)*16 - 8;  smokeY = 0x3E0;      /* p04 + half: quirk          */
    smokeT = Rand(10) + 14;  0x90100++; }
```
(Rand(4) only evaluated when the camera is in range and 0x90100 < 6.)
Release: Game_Run 0x1e45b: `if (g_AgentDropPending && Div16(g_CamX + g_PlayerScrX) == p03 + 4)`
`StealMission_Taken @ 0x17be5` → **AgentDrop_Release**: `g_AgentDropPending = 0; crateX = g_CamX+scrX;
crateY = g_CamY+scrY; crateSprite = 0x30; crateVY = 7; HUD "AGENT DROPPED !"; 0x902d4 = 1;` (automatic when
passing over column p03+4, no key needed).
Fall: Crate_Update 0x4596f (this spec, shared with the Aerolimits crate), every frame while crateX != 0:
```c
0x90574 = crateX; 0x9052c = crateY;
if (IsOnScreen(g_CamX, g_CamY, crateX, crateY)) {
    Sprite_Queue(crateX - g_CamX, crateY - g_CamY, crateSprite);
    if (BoxOverlap(camX+scrX, camY+scrY, crateX, crateY, 16, 16) && p03 == 0 && g_GameMode == 3) {
        p04 = p05 = p06 = 0; crateX = 0; 0x900dc = 1; HUD "TARGET NABBED !"; 0x908f8 = -999; } }
crateY += crateVY;
if (p03) { crateSprite = 0x37 - crateVY; crateVY -= 0x905a0; if (crateVY < 2) crateVY = 2; }  /* parachute */
if (Div16(crateY) == 63) {
    if (p03 == 0 || crateSprite < 0x34) { 0x90a10 = 1; HUD "AGENT SPLATTED !"; if (mode == 3) 0x900d8 = 3; }
    else { HUD "AGENT LANDED"; 0x908f8 = -999;
           if (p03*16 < crateX || p04*16 < crateX)                  /* "right of the zone start": quirk */
               g_MissionBonus = max(g_MissionBonus - abs(Div16(crateX) - (p03 + (p04-p03)/2))*500, 0); }
    p03 = p04 = p05 = p06 = 0;  crateX = 0; }
```
0x90a10 = 1 makes Mission_CheckObjectives print "MISSION FAILED, NO POINTS !" instead of "MISSION COMPLETED".
0x905a0 is the parachute deceleration (player/AI global; guess). Quirk: `crateY` reaching row 63 is tested with
`==`; a crate falling faster than 16 px/frame past row 63 is never resolved (cannot happen: vy <= 7 here; the
Aerolimits crate's vy = p06 is small in the data).

### 8.8 Aerolimits gates — Waypoint_Update @ 0x45584 (verified)

Runs while `g_AeroGateX[0x90334] != 0`. Gate g = 0x90334, centre (gx, gy) = (g_AeroGateX[g], g_AeroGateY[g]):
marker = (gx, gy); left post at gx-0x40 (sprite 0x4c), right post gx+0x40 (sprite 0x4d), each drawn when
IsOnScreen; touching a post (BoxOverlap 16x16 with the player) sets `0x90570 = 1` (failed pass). A pass is
reset (0x90570 = 0) by BoxOverlap(player, (gx,gy), 0x50, 0x10). If BoxOverlap(player, (gx,gy), 0x30, 9) and
0x90570 == 0: HUD "GATE PASSED !", `g_AeroGateX[g] = 0; 0x900dc = 1; g++`; if the next gate x is 0:
`g = 0; p09 = 0; HUD "COURSE COMPLETE"`. (BoxOverlap = |dx| < w && |dy| < h with strict compares.)
Quirk: a gate only becomes passable after the post-touch flag is cleared by flying through its central
0x50x0x10 box, which the 0x30x9 test is inside — so touching a post and then flying through still passes.

---------------------------------------------------------------------------------------------------

## 9. Weather (p25), fog and night (verified)

* Night: `g_NightMission = (p25 == 1)` → Parallax_Load uses `.dx1/.p10` or dark sky 0x00A, Runway_Update
  (§9.1), Pal_CycleEffects lightning/flashes (video spec).
* Fog (Mission_LoadBriefing 0x237a6): `if ((Rand(20) == 1 || p25 == 2 || g_SpecialHit == 1) && !g_NightMission
  && g_GameMode < 3) g_FogActive = 1;` — **Rand(20) is always called** (1/21 chance). If set: draw "FOG
  WARNING !" (hudtext 82) on the briefing, `g_FogSticky 0x92b18 = 1; g_FogActive = 0`.
  Game_Run sets `g_FogActive = 0` before every Mission_Setup; Mission_Setup 0x2217e: `if ((g_FogSticky || p25 ==
  2 || g_SpecialHit == 1) && !g_NightMission && g_GameMode < 3) g_FogActive = 1;`. Game_Run then calls
  Pal_SaveNight when g_FogActive, recomputes the altitude palette (video "Pal_NightAltitude": colours blend
  toward 63 = white with height `Clamp((g_CamY+150)/67+1, 0, 15)`), and Pal_Restore after the debrief.
  **g_FogSticky is never cleared** (only written at 0x23801): after the first foggy briefing every later day
  mission of the session is foggy (no warning shown). Quirk; keep (recommendation), flag to the user.
* So 0x80070 is the **fog** switch, not night: video.md's `g_NightPalActive` / `Pal_NightAltitude` /
  `Pal_SaveNight` implement fog (whitening with altitude). Renamed here `g_FogActive`; video spec owns the
  palette functions (suggest renaming them Pal_FogAltitude / Pal_SaveFog).
* g_SpecialHit 0x90768: set by hitting a class-0xC7 tile; cleared at mission end when `0x90440 == 0 && 0x909c0 <
  0x40` (0x20ddd) and at program start (MainMenu 0x46b21).

### 9.1 Runway_Update @ 0x15194 — night runway light (verified)

```c
0x901a4 = 1 - 0x901a4;                                 /* blink phase, toggles every frame             */
if (0x901a4 == 0) {
    0x9017c += 32;                                     /* light runs left->right along the runway       */
    if (0x9017c > min(g_CamX + 0x160, g_BaseEndX - 32)) 0x9017c = max(g_BaseStartX + 8, g_CamX - 0x40); }
if (IsOnScreen(g_CamX, g_CamY, 0x9017c, 0x3DF - g_BaseYOff))
    Sprite_Queue(0x9017c - g_CamX, 0x3E0 - g_CamY - g_BaseYOff, 0x192 + 0x901a4);
```
0x9017c starts at `g_BaseStartX + 8` (Game_Run mission start).

---------------------------------------------------------------------------------------------------

## 10. Original bugs and quirks (summary, with recommendation)

| # | Where | Quirk | Recommendation |
|---|---|---|---|
| 1 | Map_DamageColumn / Map_CraterAt | uninitialised return value; deterministic "last attribute read" rule (§4.1) | keep, emulate the rule |
| 2 | Map_DamageColumn | returns a tile id (mode 1) compared with a row | keep |
| 3 | Map_CraterAt | left spread `<`, right `<=`; right spread has no width bound | keep. Near the right edge (crater column >= W-4) x can reach W..W+2, i.e. column x-W of the next row; harmless in the grid buffer except at the last row (PORT: allocate the grid with a few spare bytes) |
| 4 | Map_CraterAt | 2+1+1 Rand calls with unused results | keep (RNG sequence) |
| 5 | Mission_Setup | map/damage persist across missions on the same map; VAL reload does not restore heights | keep (design) |
| 6 | Mission_LoadBriefing | new map loaded twice; pad/Bertha/target counting happen on the first copy | see Q1 |
| 7 | Parallax_Load | tileset reloaded every mission (extension rewrite) | keep |
| 8 | MP2 trigger | fine scroll added to the column; only runs when the camera changes tile; `% (W-1)` | keep |
| 9 | Camera wrap | camX == 0 → W*16-16 | keep |
| 10 | Follow views | "Follow Aborted" immediately overwritten by "Ready to follow weapon" | keep |
| 11 | Objectives (a) | double counting of lowered columns | keep |
| 12 | Objectives (c) | column skip and `> 1` filter vs. initial count; M0 #30 auto-complete | keep |
| 13 | Aero pad | x/y swapped stamp; `live[i]` in scoring; negative edge scores | keep |
| 14 | p20 | row for the raid, column for the objective clear | keep |
| 15 | Agent drop | smoke at `(p04-p03)/2 + p04`; landing penalty test `x > p03*16 || x > p04*16` | keep |
| 16 | Fog | g_FogSticky never cleared | keep but mention to the user (gameplay-visible) |
| 17 | Runway detection | no runway → endless loop; runway at column 0 ignored | PORT: assert |
| 18 | Map_LoadMxp / g_MapVal | malloc not zeroed; empty columns keep stale height | PORT: zero-init |
| 19 | Map_ScanAround | index W not wrapped | keep with zeroed buffer |
| 20 | Mission_LoadBriefing | `0x90348 = 0` when the save prompt is shown (mission > 0, campaign, after a story) wipes p10's ground units; no shipped record has both | keep |

---------------------------------------------------------------------------------------------------

## 11. Open questions

1. **Bertha / pad stamps on a map change**: Mission_LoadBriefing stamps (and counts form-(c) targets) on the map
   it loads, then Mission_Setup reloads the MXP because g_MapLoaded was not updated — wiping the stamp and
   recomputing heights. All shipped Bertha records (M0 #40, 51, 82, 109, 133, 148) and Aero pad records have a
   blank map name, so the problem never shows; confirm in DOSBox that the Bertha is visible (expected: yes).
2. The values of the stack slot in §4.1 assume no IRQ handler writes into the game stack below esp between two
   calls. DOS/4GW normally switches stacks for hardware interrupts, but this is not verified.
3. Classes 0x83/0x84/0x85: which objects (SAM site, gun, radar?) — AI spec (FUN_0003dfaa / 0x3e0b2 / 0x3dea2).
4. Class 0x87 (city maps) has no handler; maybe an Amiga-only object.
5. p21 values 2210/4200/2350: probably meant as something else (a column?); the code only tests != 0.
6. 0x8deb0/0x8deb4 (p29 % 1000): no reader found.
7. 0x90064 (start altitude offset), 0x905a0 (parachute deceleration), 0x901b8/0x902cc (completion flags):
   player/AI specs.
8. Shell timer and Bertha delay are frame-based (~70 Hz/(1+g_VSyncWaits), platform spec), so their real-time
   length depends on the benchmark; keep frame counts.

## 12. Corrections for port/formats (not edited here)

* maps.md §4 table 0: **0x00 = air** (the sky tile's class in every VAL), **0x7F = solid** (`> 0x7e` = solid);
  the current text has it inverted. Add 0x86 radar jammer, 0x8C base radar dish, 0x87 no handler, 0x05 marker.
* maps.md §4 table 3: bit 2 = secondary explosion (Explosion_Damage 2000 at the tile), bit 6 (0x40) = hover
  push-up; 0x02/0x08/0x20 unused; 0xFD/0xFE are bit sets.
* maps.md §3: width limit is 1000 (grid buffer 0xFA04), not 2000. Mission_Setup's sky tile is `tile(1,0)`.
* maps.md §5: MP2 pointer bytes 0xC1..0xFF also work (off up to +95); row clamped to 63 only for the class
  read. "VAL reloaded every Mission_Setup (resets damage tables)" → it re-reads only the 1024 attribute bytes;
  heights and grid persist.
* data.md p03 (b): **agent drop**, release automatic at column p03+4, landing judged by Crate_Update.
  p03 (c): attr0 **classes**, not tile ids. p07 Aero: **column** of the 0x3E strip (rows p03±3); p08 Aero: column
  of the 0x3F strip. p09: zeroed in Mission_Setup for modes 0-2. p20: radar ceiling row (+ objective-clear
  quirk). p25: fog warning sticky. p29 % 1000 → 0x8deb0/4, no reader.
