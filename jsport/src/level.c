/* Level subsystem (subset): port/spec/level.md (map loading, tile accessors), enemies.md (Enemy_LoadSpx). */
#include "level.h"

#include <stdlib.h>
#include <string.h>

#include "files.h"
#include "game.h"
#include "lzw.h"
#include "platform.h"
#include "video.h"

u8 *g_MapBuf;
u8 *g_MapGrid;
u8 *g_MapVal;
u8 *g_MapMp2;

#define MAPBUF_BYTES  150000
#define MAPGRID_BYTES 0xfa04
#define g_Scratch690  DS32(0x90690)

/* 0x1aeaa Mem_AllocMapBuffers (no NULL checks in the original). */
void Mem_AllocMapBuffers(void)
{
    g_MapBuf = malloc(MAPBUF_BYTES);
    g_MapGrid = malloc(MAPGRID_BYTES);
    if (!g_MapBuf || !g_MapGrid) FatalError("Mem_AllocMapBuffers", ": out of memory", 2);   /* PORT */
    memset(g_MapGrid, 0, 4);
    File_SetBufferCapacity((uintptr_t)g_MapBuf, MAPBUF_BYTES);
}

/* PORT: the grid / attribute reads of the original are unchecked; outside the buffers they read 0 here
 * and writes are dropped (they would have hit other heap blocks). */
static s32 grid_idx(int x, int y) { return x + 4 + y * g_MapWidth; }
static bool grid_ok(s32 i) { return g_MapGrid && i >= 0 && i < MAPGRID_BYTES; }

/* 0x10cb6 Map_GetTileAttr(x, y, t): g_MapVal[(t&3)*256 + tile] */
int Map_GetTileAttr(int x, int y, u32 t)
{
    s32 i = grid_idx(x, y);
    u8 tile = grid_ok(i) ? g_MapGrid[i] : 0;
    u32 a = tile + (t & 3) * 0x100;
    return (g_MapVal && a < 0x13b8) ? g_MapVal[a] : 0;
}

/* 0x10d22 Map_SetTile */
void Map_SetTile(int x, int y, u8 tile)
{
    s32 i = grid_idx(x, y);
    if (grid_ok(i)) g_MapGrid[i] = tile;
}

/* 0x116dd Map_GetTile */
int Map_GetTile(int x, int y)
{
    s32 i = grid_idx(x, y);
    return grid_ok(i) ? g_MapGrid[i] : 0;
}

/* 0x12f3e Byte_Get / 0x12f18 Byte_Set (the low byte; the callers mask it). */
u32 Byte_Get(const u8 *p, int i) { return p ? p[i] : 0; }
void Byte_Set(u8 *p, int i, u8 v) { if (p) p[i] = v; }

/* 0x40e8b Map_FindTile(v, x0, y0, x1, y1, mode): first column whose tile (mode 0) or attribute (table
 * mode-1) equals v; -1 if none. */
int Map_FindTile(int v, int x0, int y0, int x1, int y1, int mode)
{
    int found = -1;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            int t = mode == 0 ? Map_GetTile(imod_js(x, g_MapWidth, "Map_FindTile"), y)
                              : Map_GetTileAttr(imod_js(x, g_MapWidth, "Map_FindTile"), y, (u32)(mode - 1));
            if (t == v) { found = x; x = x1 + 1; y = y1 + 1; }
        }
    return found;
}

/* 0x2422d Map_LoadMxp: map/<0x85048> (LZW) into the grid; per column the first row whose attribute
 * differs from tile (0,0)'s is stored at g_MapVal[0xbd0 + x] and [0x400 + x]. */
void Map_LoadMxp(void)
{
    DS32(0x902F8) = 1;
    int packed = File_LoadWhole(DSTR(0x81132), DSTR(0x85048), (void **)&g_PackBuf, 0);
    if (LZW_PackedSize(g_PackBuf) > MAPGRID_BYTES)
        FatalError(DSTR(0x85048), ": map larger than its grid buffer (the original would overwrite memory)", 6);
    LZW_Unpack(g_PackBuf, (u32)packed, g_MapGrid);
    g_MapWidth = g_MapGrid[0] * 0x100 + g_MapGrid[1];
    u8 sky = (u8)Map_GetTileAttr(0, 0, 0);
    for (int x = 0; x < g_MapWidth; x++)
        for (int y = 0; y < 0x40; y++)
            if ((u32)Map_GetTileAttr(x, y, 0) != sky) {
                Byte_Set(g_MapVal, x + 0xbd0, (u8)y);
                Byte_Set(g_MapVal, x + 0x400, (u8)y);
                y = 0x40;
            }
    for (g_LoopI = 0; g_LoopI < 0x40; g_LoopI++) {}
    g_SkyTile = Map_GetTile(0, 0);
}

/* 0x24377 Map_ResetCounters */
void Map_ResetCounters(void)
{
    DS32(0x90690) = 0;
    DS32(0x90638) = 0;
    DS32(0x90634) = 0;
}

/* 0x243d6 Map_StampBertha: data/bertha<A+p19/1000> stamped at (p19 % 1000, p28); lowers the height rows. */
void Map_StampBertha(void)
{
    DS32(0x90690) = g_MP_Bertha / 1000;
    char name[9];
    memcpy(name, DSTR(0x811C4), 7);             /* "bertha" + NUL */
    name[6] = (char)(DS32(0x90690) + 'A');
    name[7] = 0;                                 /* PORT: an uninitialised stack byte in the original */
    name[8] = 0;
    File_LoadWhole(DSTR(0x81141), name, (void **)&g_MapBuf, 0);
    const u8 *buf = g_MapBuf;
    DS32(0x908E8) = g_MP_Bertha % 1000;          /* g_BerthaCol */
    DS32(0x908EC) = D16(0x91680);                /* g_BerthaRow = p28 */
    DS32(0x908E4) = (buf[0] << 8 | buf[1]) & 0xffff;   /* g_BerthaW: BE16 */
    DS32(0x908C8) = 600;                         /* g_BerthaDelay */
    DS32(0x908DC) = (buf[2] << 8 | buf[3]) & 0xffff;   /* g_BerthaH */
    DS32(0x9028C) = 4;
    for (g_LoopJ = 0; g_LoopJ < DS32(0x908DC); g_LoopJ++)
        for (g_LoopI = 0; g_LoopI < DS32(0x908E4); g_LoopI++) {
            Map_SetTile(DS32(0x908E8) + g_LoopI, DS32(0x908EC) + g_LoopJ, buf[DS32(0x9028C)]);
            u32 h = Byte_Get(g_MapVal, g_LoopI + 0x400 + DS32(0x908E8));
            if (DS32(0x908EC) < (s32)(h & 0xff)) Byte_Set(g_MapVal, g_LoopI + 0x400 + DS32(0x908E8), (u8)DS32(0x908EC));
            DS32(0x9028C)++;
        }
}

/* 0x3f991 Enemy_LoadSpx: plane/enemy<a+letter>.spx into g_MapBuf; entries copied over sprite slots
 * 0x5a.., 0xdb, 0xb2+k. The entry number passed is the 0-based loop value, so slots 0x5a and 0x5b both get
 * the first entry (ReplaceFromBank counts from 1) - kept. */
void Enemy_LoadSpx(void)
{
    char name[16];
    strcpy(name, DSTR(0x81448));                 /* "enemya.spx" */
    name[5] = (char)(g_EnemySpxLetter + 'a');
    int packed = File_LoadWhole(DSTR(0x81453), name, (void **)&g_PackBuf, 0);
    if (LZW_PackedSize(g_PackBuf) > MAPBUF_BYTES)
        FatalError(name, ": bank larger than g_MapBuf (the original would overwrite memory)", 6);
    LZW_Unpack(g_PackBuf, (u32)packed, g_MapBuf);
    for (DS32(0x90830) = 0; DS32(0x90830) < 0x10; DS32(0x90830)++) {
        g_LoopJ = DS32(0x90830) + 0x5a;
        Enemy_ReplaceSprite();
    }
    DS32(0x90830) = 0x10;
    g_LoopJ = 0xdb;
    Enemy_ReplaceSprite();
    for (DS32(0x90830) = 0x11; DS32(0x90830) < 0x17; DS32(0x90830)++) {
        g_LoopJ = DS32(0x90830) + 0xb2;
        Enemy_ReplaceSprite();
    }
}

/* 0x3fa76 Enemy_ReplaceSprite */
void Enemy_ReplaceSprite(void) { Sprites_ReplaceFromBank(g_LoopJ, DS32(0x90830), g_MapBuf); }

/* 0x3ff4d Truck_LoadSpx: "plane/truck" + ('a' + p13 hi byte) + digits + ".spx"; the two-digit name is built as
 * '0'+(t/10)%10, '0'+t/10, i.e. always "11" for t >= 10 (enemies.md Q18, kept: TRUCKA11 does not exist -> fatal
 * load error as in the original). Copies 0x90434 frames over the sprite slots 0x8dab8[g_LoopJ + f]. */
static void Truck_LoadSpx(void)
{
    char *name = DSTR(0x85B48);
    memcpy(name, DSTR(0x8145C), 6);              /* "truck" */
    name[5] = (char)((D16(0x91662) >> 8) + 'a');
    s32 t = DS32A(0x918E0)[g_LoopI];
    if (t / 10 == 0) {
        name[6] = (char)(t % 10 + '0');
        name[7] = 0;
    } else {
        name[7] = (char)(t / 10 + '0');
        name[6] = (char)((t / 10) % 10 + '0');
        name[8] = 0;
    }
    strcat(name, DSTR(0x81462));                 /* ".spx" */
    int packed = File_LoadWhole(DSTR(0x81467), name, (void **)&g_PackBuf, 0);   /* "plane/" */
    if (LZW_PackedSize(g_PackBuf) > MAPBUF_BYTES)
        FatalError(name, ": bank larger than g_MapBuf (the original would overwrite memory)", 6);
    LZW_Unpack(g_PackBuf, (u32)packed, g_MapBuf);
    for (DS32(0x90830) = 0; DS32(0x90830) < DS32(0x90434); DS32(0x90830)++) {
        DS32(0x90248) = DS32A(0x8DAB8)[g_LoopJ + DS32(0x90830)];
        Sprites_ReplaceFromBank(DS32(0x90248), DS32(0x90830) + 1, g_MapBuf);
    }
}

/* 0x3faab Enemy_SetupSpriteIds: convoy set-up (Mission_Setup when p09 != 0), enemies.md §7.2 (Q16, Q17) */
void Enemy_SetupSpriteIds(void)
{
    static const s32 ids[24] = { 0x7a, 0x7b, 0x7c, 0x90, 0x91, 0x92, 0x93, 0xc0, 0xc1, 0xc2, 0xe3, 0xe4, 0xe5, 0xe6,
                                 0xe7, 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0x1ee, 0x1ef, 0x1f0 };
    s32 *spr = DS32A(0x8DAB8), *loaded = DS32A(0x8DB18), *slot = DS32A(0x918E0), *base = DS32A(0x8DA88);
    for (int k = 0; k < 24; k++) spr[k] = ids[k];
    DS32(0x905AC) = D16(0x9165A);                /* g_ConvoyCount = p09 */
    DS32(0x9057C) = D16(0x91662) & 0xff;
    DS32(0x902B8) = 0;
    DS32(0x8FF4C) = 0;
    for (g_LoopI = 0; g_LoopI < 4; g_LoopI++) loaded[g_LoopI] = -999;
    for (g_LoopI = 0; g_LoopI < DS32(0x905AC); g_LoopI++) {
        int i = g_LoopI;
        DS32A(0x91904)[i] = 0;
        DS32A(0x91F74)[i] = (s32)((u32)D16(0x9166C) * (u32)i + (u32)D16(0x9165C) * 0x10 + 0x60);
        DS32A(0x91F98)[i] = D16(0x9166A);
        DS32A(0x91F2C)[i] = 0;
        DS32A(0x91F50)[i] = 2;
        if ((D16(0x91662) >> 8) == 2) { DS32A(0x91F50)[i] = 0; DS32(0x8FF4C) = 3; }
        if (DS32A(0x91F98)[i] < 0x46) DS32A(0x91F98)[i] = DS32A(0x91F98)[i] * 0x10 - 1;
        DS32(0x903E4) = D16A(0x91666)[i / 4];    /* p15, p16, then p17 / p18 (Q16) */
        slot[i] = (DS32(0x903E4) >> ((i % 4) * 4)) & 0xf;
        DS32A(0x91928)[i] = DS32(0x9057C) * DS32A(0x8E1D0)[slot[i]];
        if (DS32(0x902B8) < 4) {
            g_Scratch690 = DS32(0x902B8);
            if (DS32(0x902B8) > 0)
                for (g_LoopJ = 0; g_LoopJ < DS32(0x902B8); g_LoopJ++)
                    if (loaded[g_LoopJ] == slot[g_LoopI]) { g_Scratch690 = g_LoopJ; g_LoopJ = 99; }
            if (g_Scratch690 == DS32(0x902B8)) {
                loaded[g_Scratch690] = slot[g_LoopI];
                DS32(0x902B8)++;
                if (slot[g_LoopI] == 0xf && DS32(0x8FF4C) == 3) {
                    spr[base[g_Scratch690]] = D16(0x91678);              /* parked pickup sprite = p24 */
                } else {
                    g_LoopJ = base[g_Scratch690];
                    DS32(0x90434) = (DS32(0x8FF4C) == 3) * -5 + 6;
                    Truck_LoadSpx();
                }
            }
        } else {
            /* Q17: `for (j = 0; ntypes < j; j++)` never runs, g_Scratch690 keeps the previous slot */
            for (g_LoopJ = 0; DS32(0x902B8) < g_LoopJ; g_LoopJ++)
                if (loaded[g_LoopJ] == slot[g_LoopI]) { g_Scratch690 = g_LoopJ; g_LoopJ = 99; }
        }
        slot[g_LoopI] = base[g_Scratch690];
    }
}
