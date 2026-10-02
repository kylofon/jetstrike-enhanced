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

/* 0x38ba8 EnemyBomber_Spawn. TODO(enemies step): not ported yet; the briefing calls it for missions with
 * enemy aircraft (its Rand calls are therefore missing from the RNG sequence until then). */
void EnemyBomber_Spawn(void) {}

/* 0x3faab Enemy_SetupSpriteIds. TODO(enemies step): not ported yet. */
void Enemy_SetupSpriteIds(void) {}
