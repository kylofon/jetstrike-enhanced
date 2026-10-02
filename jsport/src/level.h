#pragma once
/* Level subsystem (port/spec/level.md): the parts the briefing needs (map buffers and loading, tile
 * accessors, Bertha stamp) and the enemy sprite loader (enemies.md). The scroller itself comes with the
 * mission frame loop. */
#include "dseg.h"

extern u8 *g_MapBuf;             /* 0x849F0: malloc(150000), scratch for banks / gendat / bertha */
extern u8 *g_MapGrid;            /* 0x849D0: malloc(0xfa04): BE16 width, then rows of 64 */
extern u8 *g_MapVal;             /* 0x849D8: malloc(0x13b8): tile attribute tables + height rows */
extern u8 *g_MapMp2;             /* 0x849EC */
#define g_SkyTile        DS32(0x900C4)

void Mem_AllocMapBuffers(void);                          /* 0x1aeaa */
int  Map_GetTileAttr(int x, int y, u32 t);               /* 0x10cb6 */
void Map_SetTile(int x, int y, u8 tile);                 /* 0x10d22 */
int  Map_GetTile(int x, int y);                          /* 0x116dd */
u32  Byte_Get(const u8 *p, int i);                       /* 0x12f3e */
void Byte_Set(u8 *p, int i, u8 v);                       /* 0x12f18 */
int  Map_FindTile(int v, int x0, int y0, int x1, int y1, int mode);   /* 0x40e8b */
void Map_LoadMxp(void);                                  /* 0x2422d (file name in 0x85048) */
void Map_ResetCounters(void);                            /* 0x24377 */
void Map_StampBertha(void);                              /* 0x243d6 */
void Enemy_LoadSpx(void);                                /* 0x3f991 */
void Enemy_ReplaceSprite(void);                          /* 0x3fa76 */
void EnemyBomber_Spawn(void);                            /* 0x38ba8 (TODO: enemies step) */
void Enemy_SetupSpriteIds(void);                         /* 0x3faab (TODO: enemies step) */
