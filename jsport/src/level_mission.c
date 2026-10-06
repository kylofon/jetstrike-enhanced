/* Level subsystem, mission part: port/spec/level.md (§2.5/2.6 tileset and parallax, §4 terrain damage, §5 MP2
 * trigger, §8 objectives and level objects, §9.1 runway light) and player.md §9.3 (Carrier_Update). */
#include "mission.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"
#include "level.h"
#include "lzw.h"
#include "platform.h"
#include "video.h"

/* 0x25c13 Tileset_Load: Tileset_LoadTlx + a leftover loop (leaves 0x8fedc = 11, 0x8fea0 = 25). */
void Tileset_Load(char *name)
{
    Tileset_LoadTlx(name);
    for (g_LoopI = 0; g_LoopI < 0x200; g_LoopI++) {
        DS32(0x8FEDC) = g_LoopI % 0x14;
        DS32(0x8FEA0) = g_LoopI / 0x14;
    }
}

/* 0x133b3 Tileset_LoadTlx: map/<name> (LZW) -> 64 KB of tiles, map/<name>.pal -> g_Palette 0x80..0xbf,
 * g_TilePtrs[i] = tiles + i*256; the name's extension is rewritten to ".tlx". */
void Tileset_LoadTlx(char *name)
{
    char path[0x108];
    int packed = File_LoadWhole(DSTR(0x80D86), name, (void **)&g_PackBuf, 0);
    if (g_TileData == NULL) g_TileData = calloc(1, 0x10000);
    if (g_TileData == NULL) FatalError(DSTR(0x80D9B), DSTR(0x80D8B), 2);
    if (LZW_PackedSize(g_PackBuf) > 0x10000)
        FatalError(name, ": tileset larger than its 64 KB buffer (the original would overwrite memory)", 6);
    LZW_Unpack(g_PackBuf, (u32)packed, g_TileData);
    strcpy(path, DSTR(0x80D86));                                     /* "map/" */
    strcat(path, name);
    size_t n = strlen(path);
    memcpy(path + n - 4, DSTR(0x80CC8), 5);                          /* ".pal" over the extension */
    FILE *f = Platform_Fopen(path, DSTR(0x80C77));
    if (!f) FatalError(path, DSTR(0x80C8C), 1);
    if (fread(DSEG(0x83058), 0xc0, 1, f) != 1) { /* not checked */ }
    fclose(f);
    for (int i = 0; i < 0x100; i++) g_TilePtrs[i] = g_TileData + i * 0x100;
    n = strlen(name);
    memcpy(name + n - 4, DSTR(0x80DB6), 5);                          /* ".tlx" */
}

/* 0x1351e Parallax_Load(g_TilesetName): with detail on, map/<name>.dx0|dx1 (LZW, 320x512, -0x40) into the
 * backdrop and map/<name>.p0N|p10 (Rand(1) picks the day sky) into colours 0xc0..0xcf; the name keeps the
 * .pNN extension (level.md §2.6 quirk: the TLX is reloaded every mission). Detail off: flat sky colours. */
void Parallax_Load(char *name)
{
    if (g_DetailParallax == 0) {
        for (int c = 0xc4; c < 0xd4; c++) Pal_SetColor12(c, g_NightMission == 0 ? 0x4af : 10);
        return;
    }
    if (g_ParallaxBuf == NULL && (g_ParallaxBuf = calloc(1, PARALLAX_BYTES)) == NULL)
        FatalError(DSTR(0x80DBD), DSTR(0x80DBB), 2);
    size_t n = strlen(name);
    name[n - 3] = 'd';
    name[n - 2] = 'x';
    name[n - 1] = g_NightMission == 0 ? '0' : '1';
    int packed = File_LoadWhole(DSTR(0x80D86), name, (void **)&g_PackBuf, 0);
    if (LZW_PackedSize(g_PackBuf) > PARALLAX_BYTES)
        FatalError(name, ": backdrop larger than its buffer (the original would overwrite memory)", 6);
    LZW_Unpack(g_PackBuf, (u32)packed, g_ParallaxBuf);
    for (int i = 0; i < 0x28000; i++) {
        g_DamageHits = g_ParallaxBuf[i];         /* scratch copies left by the original */
        g_LoopK = g_DamageHits;
        g_ParallaxBuf[i] = (u8)(g_ParallaxBuf[i] - 0x40);
    }
    n = strlen(name);
    name[n - 3] = 'p';
    if (g_NightMission == 0) {
        name[n - 2] = '0';
        name[n - 1] = (char)(Rand(1) + '0');
    } else {
        name[n - 2] = '1';
        name[n - 1] = '0';
    }
    File_LoadWhole(DSTR(0x80D86), name, (void **)&g_PackBuf, 0);
    int c = 0x240;
    for (DS32(0x90474) = 0; DS32(0x90474) < 0x30; DS32(0x90474)++) {
        g_Palette[c] = (u8)(g_PackBuf[DS32(0x90474) * 4] >> 2);
        g_ParallaxPal[DS32(0x90474)] = g_Palette[c];
        c++;
    }
}

/* 0x10eb0 IsOnScreen(camX, camY, x, y). ENH: view: the right and bottom edges move out with the extra columns and
 * playfield rows (decision 2; the camera stops higher, CAM_Y_MAX). */
int IsOnScreen(int camX, int camY, int x, int y)
{
    return (camX - 0x40 < x && x < camX + 0x140 + VIEW_EXTRA_COLS && camY < y && y < camY + 200 + VIEW_EXTRA_ROWS) ? 1 : 0;
}

/* 0x1172a BoxOverlap(x1, y1, x2, y2, w, h): |dx| < w && |dy| < h, strict */
int BoxOverlap(int x1, int y1, int x2, int y2, int w, int h)
{
    return (x1 - w < x2 && x2 < x1 + w && y1 - h < y2 && y2 < y1 + h) ? 1 : 0;
}

/* 0x3e1a3 Map_TriggerColumn (level.md §5.2) */
void Map_TriggerColumn(void)
{
    if (DS32(0x9063C) > 0) { Map_SetTile(DS32(0x9063C), DS32(0x90640), (u8)DS32(0x90630)); DS32(0x9063C) = 0; }
    if (DS32(0x900E8) > 0) { Map_SetTile(DS32(0x900E8), DS32(0x900EC), (u8)DS32(0x900E4)); DS32(0x900E8) = 0; }
    if (DS32(0x90230) > 0) { Map_SetTile(DS32(0x90230), DS32(0x90234), (u8)DS32(0x9022C)); DS32(0x90230) = 0; }
    if (g_Scratch690 < 0x80) {
        g_TrigClass = Map_GetTileAttr(g_TrigCol, g_Scratch690, 0);
        g_TrigOff = 0;
        g_TrigRow = g_Scratch690;
        if (g_TrigClass < 0x80 && g_TrigClass != 5) {
            int k = g_Mission / 10 < 1 ? 1 : g_Mission / 10;
            if (Rand(0xc) < k && g_LauncherCol == 0) {
                int r = g_Scratch690 + 1 < 0x40 ? g_Scratch690 + 1 : 0x3f;
                if (Map_GetTileAttr(g_TrigCol, r, 0) > 0x7e) {
                    g_LauncherCol = g_TrigCol;
                    DS32(0x902C4) = g_Scratch690;
                    DS32(0x902BC) = Rand((g_Mission > 0x1e) + 1);
                    DS32(0x902DC) = Rand(100) + 0x28;
                }
            }
        }
    } else {
        g_Scratch690 -= 0xa0;
        int t = g_TrigCol + g_Scratch690;        /* min(.., W-1), then max(.., 0) */
        if (t > g_MapWidth - 1) t = g_MapWidth - 1;
        if (t < 0) t = 0;
        DS32(0x906B4) = (s32)(Byte_Get(g_MapMp2, t) & 0xff);
        int r = DS32(0x906B4) > 0x3f ? 0x3f : DS32(0x906B4);
        g_TrigClass = Map_GetTileAttr(t, r, 0);
        g_TrigOff = g_Scratch690;
        g_TrigRow = DS32(0x906B4);               /* not clamped */
    }
    int X = g_TrigCol + g_TrigOff, Y = g_TrigRow;
    if (g_TrigClass == 0x83) {
        DS32(0x90668) = X; DS32(0x90614) = Y;
        DS32(0x90630) = Map_GetTile(X, Y);
        Map_SetTile(X, Y, (u8)g_SkyTile);
        DS32(0x9063C) = X; DS32(0x90640) = Y;
    }
    if (g_TrigClass == 0x84) {
        DS32(0x9010C) = X; DS32(0x90110) = Y;
        DS32(0x900E4) = Map_GetTile(X, Y);
        Map_SetTile(X, Y, (u8)g_SkyTile);
        DS32(0x900E8) = X; DS32(0x900EC) = Y;
    }
    if (g_TrigClass == 0x85) {
        DS32(0x90268) = X; DS32(0x9026C) = Y;
        DS32(0x9022C) = Map_GetTile(X, Y);
        Map_SetTile(X, Y, (u8)g_SkyTile);
        DS32(0x90230) = X; DS32(0x90234) = Y;
    }
    if (g_TrigClass == 0x86) {
        g_RadarJammed = 1;
        g_TargetMarkX = 0;
        g_LockTarget = -1;
    }
    if (g_TrigClass == 5) {
        g_MarkerX = X * 0x10 - 0x10;
        g_MarkerY = Y * 0x10 - 0x10;
    }
}

/* PORT: the uninitialised locals of Map_DamageColumn (level.md §4.1, "likely"). s_slot10 models the stack
 * word that is Map_DamageColumn's `last`: Map_CraterAt's direct Map_GetTileAttr calls and the previous
 * Map_DamageColumn call leave their value there. s_slot14 models its `ret` the same way. */
static s32 s_slot10, s_slot14;

static int crater_attr(int x, int y, u32 t)
{
    int a = Map_GetTileAttr(x, y, t);
    s_slot10 = a;
    return a;
}

/* 0x3f195 Map_DamageColumn(x, y, tile, mode): clears the column upwards from row y; armour > 200 stops it.
 * `uninit` (PORT) is the value of the uninitialised `last`. */
int Map_DamageColumn(int x, int y, int tile, int mode, int uninit)
{
    s32 last = uninit, ret = s_slot14;
    g_ColArmourSum = 0;
    for (int i = y; i > 0; i--) {
        int a = Map_GetTileAttr(x, y, 2);
        g_ColArmourSum += a;
        if (a < 0xc9) {
            if (mode == 0) {
                Map_SetTile(x, y, (u8)tile);
                if (i < (s32)(Byte_Get(g_MapVal, x + 0x400) & 0xff)) {
                    i = -1;
                    ret = last;
                }
            } else {
                last = Map_GetTileAttr(x, y, 1);
                Map_SetTile(x, y, (u8)last);
                mode = 0;
            }
        } else {
            last = Map_GetTileAttr(x, y, 1);
            Map_SetTile(x, y, (u8)last);
            i = -1;
            ret = mode;
        }
        y--;
    }
    s_slot10 = last;
    s_slot14 = ret;
    return ret;
}

static int no_debris_bit(int attr3) { return (attr3 & 1) == 0; }

/* 0x3b777 Map_CraterAt(px, py, dmg) (level.md §4.1). Only caller: Explosion_Damage (weapons step). */
void Map_CraterAt(int px, int py, int dmg)
{
    if (py <= 0) return;
    int x = Div16(Clamp(px + 8, 0, g_MapWidth * 0x10 - 1));
    int y = Div16(py + 8 < 0x400 ? py + 8 : 0x3ff);
    DS32(0x90008) = crater_attr(x, y, 1);
    DS32(0x904E8) = DS32(0x90008);
    DS32(0x90530) = crater_attr(x, y, 2);
    if (crater_attr(x, y, 0) == 199) {
        g_SpecialHit = 1;
        Hud_PushMessage(DSTR(0x813BC));                              /* "Om" */
    }
    int bottom = (y == 0x3f);
    DS32(0x90AC0) = bottom * crater_attr(x, Clamp(y - 1, 0, 0x3f), 2);
    if ((DS32(0x90530) * 10 <= dmg && DS32(0x90AC0) * 10 <= dmg) || DS32(0x90008) == g_SkyTile) {
        DS32(0x904E4) = crater_attr(x, y, 3);
        int p = g_AeroPlayer;
        if (DS32(0x904E4) == 0xff) {
            DS32(0x90008) = crater_attr(x, y, 1);
        } else {
            g_Score[p] += crater_attr(x, y, 2) * (g_PlaneClass + 1);
            if (DS32(0x90008) != g_SkyTile) {
                int cy = y > 0x3f ? 0x3f : y;
                cy = cy < 1 ? 1 : cy;
                if (g_SmokeCount < 6 && Rand(10) == 1 && g_GameMode < 3) {
                    g_SmokeX[g_SmokeCount] = x * 0x10 - 8;
                    g_SmokeY[g_SmokeCount] = y * 0x10 - 8;
                    g_SmokeT[g_SmokeCount] = Rand(0x14) + 4;
                    if ((s32)(Byte_Get(g_MapVal, DS32(0x90008) + 0x100) & 0xff) == DS32(0x90008)
                        && no_debris_bit(crater_attr(x, cy, 3)))
                        Particle_Spawn(x * 0x1000 - 0x800,
                                       y * 0x1000 - (int)((Byte_Get(g_MapVal, DS32(0x90008)) & 0xff) > 0x7e) * 0x1000,
                                       0, 0, 0, 0x40, 0x15);
                    g_SmokeCount++;
                }
                if (DS32(0x907B8) == 1 && (s32)(Byte_Get(g_MapVal, DS32(0x90008) + 0x100) & 0xff) == DS32(0x90008)
                    && no_debris_bit(crater_attr(x, cy, 3)))
                    Particle_Spawn(x * 0x1000 - 0x800,
                                   y * 0x1000 - (int)((Byte_Get(g_MapVal, DS32(0x90008)) & 0xff) > 0x7e) * 0x1000,
                                   0, 0, 0, 0x40, 0x15);
                if (crater_attr(x, cy, 3) & 4) {
                    if (g_SecExpCount < 0x20) {          /* PORT: 32-entry arrays, bounded (level.md §4.3) */
                        g_SecExpX[g_SecExpCount] = x << 4;
                        g_SecExpY[g_SecExpCount] = y << 4;
                        g_SecExpDmg[g_SecExpCount] = 2000;
                        g_SecExpCount++;
                    }
                }
                if ((crater_attr(x, cy, 3) & 0x10) && DS32(0x90320) < 10)
                    Particle_Spawn(x << 0xc, y << 0xc, 0, 0, 0x10, 0x20, 0x19);
                if (g_BaseStartX <= x * 0x10 && x * 0x10 <= g_BaseEndX && DS32(0x90534) == 0) {
                    g_Score[g_AeroPlayer] = g_Score[g_AeroPlayer] - 1000 < 0 ? 0 : g_Score[g_AeroPlayer] - 1000;
                    DS32(0x901C8)++;
                }
            }
        }
        Map_SetTile(x, y, (u8)DS32(0x90008));
        DS32(0x90008) = g_SkyTile;
        y--;
    } else {
        y = 0;
    }
    DS32(0x903F8) = 0;                           /* g_SpreadL */
    DS32(0x90178) = 0;                           /* g_SpreadR */
    DS32(0x8FEC4) = x;                           /* g_ExplCX */
    if ((s32)(Byte_Get(g_MapVal, x + 0x400) & 0xff) <= y && x > 1 && x < g_MapWidth - 1 && y > 0xc) {
        DS32(0x907AC) = Map_DamageColumn(x, y, g_SkyTile, 0, s_slot10);
        g_Score[g_AeroPlayer] += g_ColArmourSum;
        DS32(0x904E8) = (s32)(Byte_Get(g_MapVal, x + 0x400) & 0xff);
        if (DS32(0x907AC) < DS32(0x904E8)) Byte_Set(g_MapVal, x + 0x400, (u8)y);
        for (DS32(0x90530) = DS32(0x904E8); DS32(0x90530) < y && DS32(0x90530) > 0;) {
            Rand(2);
            int r = Rand(2);
            DS32(0x90530) += r + 1;
        }
        if ((s32)(Byte_Get(g_MapVal, x + 0x3ff) & 0xff) <= y && Map_GetTile(x - 1, y) == g_SkyTile) DS32(0x903F8) = y + 1;
        if ((s32)(Byte_Get(g_MapVal, x + 0x401) & 0xff) <= y && Map_GetTile(x + 1, y) == g_SkyTile) DS32(0x90178) = y + 1;
        for (; DS32(0x903F8) > 0 && x > 0; x--) {
            DS32(0x907AC) = Map_DamageColumn(x, DS32(0x903F8), g_SkyTile, 1, s_slot10);
            g_Score[g_AeroPlayer] += g_ColArmourSum;
            DS32(0x904E8) = (s32)(Byte_Get(g_MapVal, x + 0x400) & 0xff);
            if (DS32(0x907AC) < DS32(0x904E8)) Byte_Set(g_MapVal, x + 0x400, (u8)DS32(0x903F8));
            Rand(2);
            if (DS32(0x903F8) < (s32)(Byte_Get(g_MapVal, x + 0x3ff) & 0xff) || x < DS32(0x8FEC4) - 3
                || crater_attr(x - 1, DS32(0x903F8), 1) != Map_GetTile(x - 1, DS32(0x903F8)))
                DS32(0x903F8) = 0;
        }
        x = DS32(0x8FEC4);
        while (DS32(0x90178) > 0) {                 /* no upper bound on x (level.md Q3) */
            DS32(0x907AC) = Map_DamageColumn(x, DS32(0x90178), g_SkyTile, 1, s_slot10);
            g_Score[g_AeroPlayer] += g_ColArmourSum;
            DS32(0x904E8) = (s32)(Byte_Get(g_MapVal, x + 0x400) & 0xff);
            if (DS32(0x907AC) <= DS32(0x904E8)) Byte_Set(g_MapVal, x + 0x400, (u8)DS32(0x90178));
            Rand(2);
            if (DS32(0x90178) < (s32)(Byte_Get(g_MapVal, x + 0x401) & 0xff) || DS32(0x8FEC4) + 3 < x
                || crater_attr(x + 1, DS32(0x90178), 1) != Map_GetTile(x + 1, DS32(0x90178)))
                DS32(0x90178) = 0;
            x++;
        }
    }
}

/* 0x14abe Map_SecondaryExplosions */
void Map_SecondaryExplosions(void)
{
    for (g_LoopI = 0; g_LoopI < g_SecExpCount && g_LoopI < 0x20; g_LoopI++) {   /* PORT: bounded (§4.3) */
        DS32(0x907B8) = 1;
        Explosion_Damage(g_SecExpX[g_LoopI], g_SecExpY[g_LoopI], 0, 0, g_SecExpDmg[g_LoopI], g_SecExpDmg[g_LoopI]);
    }
}

/* 0x3f02e Mission_CheckComplete (level.md §8.1): every frame while landed. g_Speed compared as raw bits. */
void Mission_CheckComplete(void)
{
    int px = g_CamX + g_PlayerScrX;
    if (g_SpeedBits == 0 && g_PlayerVX == 0 && g_BaseStartX < px && px < g_BaseEndX && g_MissionResult == 0
        && DS32(0x907C0) == 0 && g_Crashed == 0) {
        Mission_CheckObjectives();
        if ((DS32(0x901B8) != 0 || g_PhotoCount > 0) && g_GameMode < 3) {
            Mission_CompleteScreen();
            DS32(0x901B0) = 0;
            DS32(0x901B8) = 0;
            g_PhotoCount = 0;
        }
    }
    if (g_CatapultTimer == 0 && g_DirHalf / 2 == 0 && DS32(0x9005C) == 1) {
        g_Dir = 4;
        g_DirHalf = 2;
    }
    if (g_HookDown == 1 && g_SpeedBits > 0 && g_DeckAttr == 0xfe && g_DirHalf / 2 < 4 && g_TaxiStopTimer == 0) {
        g_TaxiStopTimer = 7;
        g_Throttle = 0;
    }
    if (g_SpeedBits == 0) Carrier_Update();
}

/* 0x44a5d Mission_CheckObjectives (level.md §8.2): once per landing. */
void Mission_CheckObjectives(void)
{
    if (g_EscortMode != 0) g_MP_ConvoyKills = (u16)(g_EscortNeeded < 1 ? 0 : g_EscortNeeded);
    g_TargetsDone = 0;
    g_AeroPadScore = 0;
    if (g_GameMode < 3) {
        if (g_MP_TargetX0 < 5000) {
            if (g_AgentDropPending == 0) {
                for (int v = 0; v < 2; v++)              /* rubble class 2, then 0xa0: lowered columns count twice */
                    for (g_LoopI = g_MP_TargetX0; g_LoopI <= (s32)g_MP_TargetX1; g_LoopI++) {
                        u32 orig = Byte_Get(g_MapVal, g_LoopI + 0xbd0) & 0xff;
                        DS32(0x909A8) = Map_FindTile(v == 0 ? 2 : 0xa0, g_LoopI, (s32)orig - 1, g_LoopI + 1, 0x40, 1);
                        if (DS32(0x909A8) > -1
                            || (Byte_Get(g_MapVal, g_LoopI + 0xbd0) & 0xff) < (Byte_Get(g_MapVal, g_LoopI + 0x400) & 0xff))
                            g_TargetsDone++;
                    }
            }
        } else {
            g_LoopK = 0;
            for (g_LoopJ = 0; g_LoopJ < (s32)g_MP_TargetX1; g_LoopJ++)
                for (g_LoopI = 0; g_LoopI < g_MapWidth; g_LoopI++) {
                    u32 orig = Byte_Get(g_MapVal, g_LoopI + 0xbd0) & 0xff;
                    DS32(0x909A8) = Map_FindTile((g_MP_TargetX0 - 5000) + g_LoopJ, g_LoopI, (s32)orig - 1, g_LoopI + 1, 0x40, 1);
                    if (DS32(0x909A8) > 1) {
                        g_LoopK++;
                        g_LoopI = DS32(0x909A8) + 1;     /* skips a column (quirk) */
                    }
                }
            g_TargetsDone = g_TargetTilesInit - g_LoopK;
            if (g_TargetTilesInit < (s32)g_MP_TargetsReq) g_TargetsDone = g_MP_TargetsReq;
        }
        if ((s32)g_MP_TargetsReq <= g_TargetsDone && g_AgentDropPending == 0) {
            g_MP_TargetX0 = 0;
            g_MP_TargetX1 = 0;
            g_MP_TargetsReq = 0;
        }
        if (g_MP_CeilingRow != 0) {
            DS32(0x909A8) = Map_FindTile(2, g_MP_CeilingRow, 0, g_MP_CeilingRow + 1, 0x40, 1);
            if (DS32(0x909A8) > -1) { g_MP_CeilingRow = 0; g_MP_TargetMarker = 0; }
            DS32(0x909A8) = Map_FindTile(0xa0, g_MP_CeilingRow, 0, g_MP_CeilingRow + 1, 0x40, 1);   /* column 0 if cleared */
            if (DS32(0x909A8) > -1) { g_MP_CeilingRow = 0; g_MP_TargetMarker = 0; }
        }
    } else if (g_MP_TargetX0 != 0 && g_AgentDropPending == 0) {
        for (g_LoopI = 0; g_LoopI < 0xb; g_LoopI++) {
            DS32(0x909A8) = Map_FindTile(2, g_MP_TargetX0 + g_LoopI, 0, g_MP_TargetX0 + g_LoopI + 1, 0x40, 1);
            if (DS32(0x909A8) == -1)
                DS32(0x909A8) = Map_FindTile(0xa0, g_MP_TargetX0 + g_LoopI, 0, g_MP_TargetX0 + g_LoopI + 1, 0x40, 1);
            if (DS32(0x909A8) > -1
                || (Byte_Get(g_MapVal, g_MP_TargetX0 + g_LoopI + 0xbd0) & 0xff) < (Byte_Get(g_MapVal, g_LoopI + 0x400) & 0xff)) {
                g_AeroPadScore += (4 - abs(g_LoopI - 5)) * 0x9c4;    /* live[i], not live[p03+i] (quirk) */
                g_MP_TargetX0 = 0;
                g_MP_TargetX1 = 0;
            }
        }
    }
    DS32(0x907C0) = 1;
    g_Scratch690 = 0;
    for (g_LoopI = 0; g_LoopI < 0x16; g_LoopI++)
        if (g_MissionParams[g_LoopI] != 0 && g_ParamNotObjective[g_LoopI * 2 + (g_GameMode == 3)] == 0) g_Scratch690 = 1;
    if (g_EscortMode != 0) {
        g_EscortNeeded = g_MP_ConvoyKills;
        g_MP_ConvoyKills = 0;
    }
    if (g_Scratch690 == 0) {
        g_MissionResult = DS32(0x9025C) + 1;
        DS32(0x903D0) = DS32(0x8FFEC);
        DS32(0x903D8) = g_BaseEndX;
        Hud_PushMessage(HUDTEXT(DS32(0x90A10) * 0x28 + 0x12));       /* MISSION COMPLETED / FAILED */
    }
}

/* 0x15957 Carrier_Update (player.md §9.3; from Mission_CheckComplete when stopped) */
void Carrier_Update(void)
{
    if (DS32(0x902B0) == 1 && g_MissionParams[0] != 0) {
        g_MissionParams[0] = 0;
        DS32(0x907C0) = 0;
    }
    int px = g_CamX + g_PlayerScrX;
    if (g_ParkAttitude * 4 == g_Dir && g_OnGround == 1 && g_CatapultTimer == 0 && g_BaseStartX + 0x7e < px
        && px < g_BaseStartX + 0x86 && g_DeckAttr > 0xfc && g_HookDX != 0 && g_Throttle == 0) {
        if (g_CatapultCount == 0) {
            g_CatapultCount = 10;
        } else {
            g_CatapultCount--;
            strcpy(DSTR(0x85448), DSTR(0x8A608));                    /* "STANDBY TO LAUNCH" */
            itoa_js(g_CatapultCount, DSTR(0x85548));
            strcpy(DSTR(0x85448) + strlen(DSTR(0x85448)), DSTR(0x85548));
            /* Stub_LaunchCountdown 0x26f9c is empty: the text is never shown (Q13) */
            if (g_CatapultCount == 0) {
                g_CatapultTimer = 10;
                g_Throttle = 9;
                DS32(0x8FFCC) = 1;
                g_EngineRev = 0x5a;
            }
        }
    } else {
        g_CatapultCount = 0;
    }
    if (g_DeckAttr > 0xfc && g_TaxiStopTimer == 0 && g_IsHeli == 0 && g_HookDX != 0 && g_Throttle > 0
        && g_ArmourBase + g_ArmourBonus == g_Armour) {
        if (g_HookDown == 1) {
            g_HookDown = 0;
            Hud_PushMessage(DSTR(0x8A658));                          /* "TAIL HOOK UP" */
        }
        if (g_BaseStartX + 0x82 < g_CamX + g_PlayerScrX) { g_Dir = 0; g_DirHalf = 0; g_CamX -= 4; }
        if (g_CamX + g_PlayerScrX < g_BaseStartX + 0x82) { g_Dir = 0; g_DirHalf = 0; g_CamX += 4; }
    }
}

/* 0x1af0f Bertha_Update (level.md §8.5) */
void Bertha_Update(void)
{
    g_Scratch690 = 0;
    for (g_LoopI = g_BerthaCol; g_LoopI < g_BerthaCol + g_BerthaW; g_LoopI++)
        for (g_LoopJ = g_BerthaRow; g_LoopJ < g_BerthaRow + g_BerthaH; g_LoopJ++) {
            DS32(0x90248) = Map_GetTileAttr(g_LoopI, g_LoopJ, 0);
            if (DS32(0x90248) == 2 || DS32(0x90248) == 0xa0 || DS32(0x90248) == 0) g_Scratch690++;
        }
    if (g_Scratch690 < (s32)g_MP_TargetsReq) {
        g_BerthaShellX = Rand(g_BaseEndX - g_BaseStartX) + g_BaseStartX;
        Explosion_Damage((g_BerthaCol + g_BerthaW) * 0x10 + 0x10, g_BerthaRow * 0x10 - 0x10, 0x10, -0x10, 300, 300);
        g_BerthaShellTimer = 1;
    } else {
        g_MP_Bertha = 0;
    }
}

/* 0x15387 AgentDrop_Update (level.md §8.7) */
void AgentDrop_Update(void)
{
    g_TargetMarkX = ((s32)(g_MP_TargetX1 - g_MP_TargetX0) / 2 + g_MP_TargetX0) * 0x10 - 8;
    g_TargetMarkY = 0x3e0;
    if ((s32)(g_MP_TargetX0 * 0x10 - 0x148) < g_CamX && g_CamX < (s32)(g_MP_TargetX0 * 0x10 + 0x1c0)
        && g_SmokeCount < 6 && Rand(4) == 1) {
        g_SmokeX[g_SmokeCount] = ((s32)(g_MP_TargetX1 - g_MP_TargetX0) / 2 + g_MP_TargetX1) * 0x10 - 8;   /* p04 + half (quirk) */
        g_SmokeY[g_SmokeCount] = 0x3e0;
        g_SmokeT[g_SmokeCount] = Rand(10) + 0xe;
        g_SmokeCount++;
    }
}

/* 0x17be5 AgentDrop_Release */
void AgentDrop_Release(void)
{
    g_AgentDropPending = 0;
    g_CrateX = g_CamX + g_PlayerScrX;
    g_CrateY = g_CamY + g_PlayerScrY;
    g_CrateSprite = 0x30;
    g_CrateVY = 7;
    Hud_PushMessage(DSTR(0x8B558));                                  /* "AGENT DROPPED !" */
    g_FireLatch = 1;
}

/* 0x4596f Crate_Update (agent drop / Aerolimits crate) */
void Crate_Update(void)
{
    g_TargetMarkX = g_CrateX;
    g_TargetMarkY = g_CrateY;
    if (IsOnScreen(g_CamX, g_CamY, g_CrateX, g_CrateY)) {
        Sprite_Queue(g_CrateX - g_CamX, g_CrateY - g_CamY, g_CrateSprite);
        if (BoxOverlap(g_CamX + g_PlayerScrX, g_CamY + g_PlayerScrY, g_CrateX, g_CrateY, 0x10, 0x10)
            && g_MP_TargetX0 == 0 && g_GameMode == 3) {
            g_MP_TargetX1 = 0;
            g_MP_AirKills = 0;
            g_MP_ConvoyKills = 0;
            g_CrateX = 0;
            DS32(0x900DC) = 1;
            Hud_PushMessage(DSTR(0x8A518));                          /* "TARGET NABBED !" */
            DS32(0x908F8) = -999;
        }
    }
    g_CrateY += g_CrateVY;
    if (g_MP_TargetX0 != 0) {
        g_CrateSprite = 0x37 - g_CrateVY;
        g_CrateVY -= g_FrameParity;
        if (g_CrateVY < 2) g_CrateVY = 2;
    }
    if (Div16(g_CrateY) == 0x3f) {
        if (g_MP_TargetX0 == 0 || g_CrateSprite < 0x34) {
            DS32(0x90A10) = 1;
            Hud_PushMessage(DSTR(0x8A568));                          /* "AGENT SPLATTED !" */
            if (g_GameMode == 3) g_EngineFire = 3;
        } else {
            Hud_PushMessage(DSTR(0x8A5B8));                          /* "AGENT LANDED" */
            DS32(0x908F8) = -999;
            if ((s32)(g_MP_TargetX0 * 0x10) < g_CrateX || (s32)(g_MP_TargetX1 * 0x10) < g_CrateX) {
                g_MissionBonus += abs(Div16(g_CrateX) - ((s32)g_MP_TargetX0 + (s32)(g_MP_TargetX1 - g_MP_TargetX0) / 2)) * -500;
                if (g_MissionBonus < 0) g_MissionBonus = 0;
            }
        }
        for (g_LoopI = 3; g_LoopI < 7; g_LoopI++) g_MissionParams[g_LoopI] = 0;
        g_CrateX = 0;
    }
}

/* 0x45584 Waypoint_Update (Aerolimits gates, level.md §8.8) */
void Waypoint_Update(void)
{
    int px = g_CamX + g_PlayerScrX, py = g_CamY + g_PlayerScrY;
    s32 gx = g_AeroGateX[g_AeroGateIdx], gy = g_AeroGateY[g_AeroGateIdx];
    g_TargetMarkX = gx;
    g_TargetMarkY = gy;
    if (IsOnScreen(g_CamX, g_CamY, gx - 0x40, gy)) {
        Sprite_Queue((gx - 0x40) - g_CamX, gy - g_CamY, 0x4c);
        if (BoxOverlap(px, py, gx - 0x40, gy, 0x10, 0x10)) g_AeroGatePostHit = 1;
    }
    if (IsOnScreen(g_CamX, g_CamY, gx + 0x40, gy)) {
        Sprite_Queue((gx + 0x40) - g_CamX, gy - g_CamY, 0x4d);
        if (BoxOverlap(px, py, gx + 0x40, gy, 0x10, 0x10)) g_AeroGatePostHit = 1;
    }
    if (g_AeroGatePostHit != 0 && BoxOverlap(px, py, gx, gy, 0x50, 0x10)) g_AeroGatePostHit = 0;
    if (BoxOverlap(px, py, gx, gy, 0x30, 9) && g_AeroGatePostHit == 0) {
        Hud_PushMessage(DSTR(0x8A478));                              /* "GATE PASSED !" */
        g_AeroGateX[g_AeroGateIdx] = 0;
        DS32(0x900DC) = 1;
        g_AeroGateIdx++;
        if (g_AeroGateX[g_AeroGateIdx] == 0) {
            g_AeroGateIdx = 0;
            g_MP_ConvoyCount = 0;
            Hud_PushMessage(DSTR(0x8A4C8));                          /* "COURSE COMPLETE" */
        }
    }
}

/* 0x15194 Runway_Update (night runway light, level.md §9.1) */
void Runway_Update(void)
{
    DS32(0x901A4) = 1 - DS32(0x901A4);
    if (DS32(0x901A4) == 0) {
        DS32(0x9017C) += 0x20;
        s32 hi = g_CamX + 0x160 < g_BaseEndX - 0x20 ? g_CamX + 0x160 : g_BaseEndX - 0x20;
        if (hi < DS32(0x9017C)) DS32(0x9017C) = g_BaseStartX + 8 < g_CamX - 0x40 ? g_CamX - 0x40 : g_BaseStartX + 8;
    }
    if (IsOnScreen(g_CamX, g_CamY, DS32(0x9017C), 0x3df - g_BaseYOff))
        Sprite_Queue(DS32(0x9017C) - g_CamX, (0x3e0 - g_CamY) - g_BaseYOff, DS32(0x901A4) + 0x192);
}

/* 0x16e38 BaseRadar_Update: the animated radar dish found at mission start (attr 0x8c). */
void BaseRadar_Update(void)
{
    if (IsOnScreen(g_CamX, g_CamY, DS32(0x901E4), DS32(0x901E8))) {
        Sprite_Queue(DS32(0x901E4) - g_CamX, DS32(0x901E8) - g_CamY, DS32(0x901D0));
        DS32(0x901D0) += DS32(0x901CC);
        if (DS32(0x901D0) == 0x1e2 || DS32(0x901D0) == 0x1e5) DS32(0x901CC) = -DS32(0x901CC);
        if (Map_GetTileAttr(Div16(DS32(0x901E4)) + 1, Div16(DS32(0x901E8)) + 1, 0) < 0x7f) DS32(0x901E4) = 0;
    }
}

/* 0x16dcd Runway_SetEndTargets */
void Runway_SetEndTargets(void)
{
    DS32(0x8FF04) = (g_BaseEndX - 0x154 < g_CamX) ? g_BaseStartX : g_BaseEndX;
    DS32(0x903D8) = DS32(0x8FF04);
    DS32(0x901FC) = DS32(0x8FF04);
    DS32(0x9082C) = DS32(0x8FF04);
    DS32(0x905D4) = DS32(0x8FF04);
}

/* 0x400da FUN_000400da: a recon camera on either rack remembers the photo column. */
void Recon_PhotoCheck(void)
{
    if (g_WeaponType[g_RackWeapon[0] * 6] == 4 || g_WeaponType[g_RackWeapon[1] * 6] == 4) DS32(0x901B8) = g_TrigCol;
}

/* 0x3e66b Map_TriggerColumnAhead (level.md §5.1): the MP2 trigger for the column ahead of the player; called by
 * Projectile_HitGround after an impact. */
void Map_TriggerColumnAhead(void)
{
    DS32(0x90668) = 0;
    DS32(0x9010C) = 0;
    DS32(0x90268) = 0;
    g_TrigCol = imod_js(Div16(g_CamX + g_PlayerScrX + g_ScrollFineX + 0x10), g_MapWidth - 1, "Map_TriggerColumnAhead");
    g_Scratch690 = (s32)(Byte_Get(g_MapMp2, g_TrigCol) & 0xff);
    if (g_Scratch690 != 0) Map_TriggerColumn();
}

/* 0x4501e Ray_Trace(x, y, vx, vy): the gun's terrain ray (40 steps of 4.12 fixed point, +0x200 sag per step);
 * returns the attribute of the solid tile hit (> 0x7e), else 0. The hit point goes to 0x80784/0x80788. */
int Ray_Trace(int x, int y, int vx, int vy)
{
    x <<= 0xc;
    y <<= 0xc;
    vy >>= 1;
    vx >>= 1;
    int i, attr = 0;
    for (i = 0; i < 0x28; i++) {
        attr = Map_GetTileAttr(x >> 0x10, y >> 0x10, 0);
        if (attr < 0x7f) {
            x += vx;
            y += vy + 0x200;
            if (x < 0) x += g_MapWidth << 0x10;
            if ((g_MapWidth << 0x10) < x) x -= g_MapWidth << 0x10;
        } else {
            DS32(0x80784) = x >> 0xc;
            DS32(0x80788) = y >> 0xc;
            i = 99;
        }
    }
    return i < 99 ? 0 : attr;
}
int Ray_HitX(void) { return DS32(0x80784); }   /* 0x450f8 */
int Ray_HitY(void) { return DS32(0x80788); }   /* 0x4511e */

/* 0x145f9 Video_Fill4x4(x, y, tile): one overview cell (video.md) */
static void Video_Fill4x4(int x, int y, int tile)
{
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            Video_PutPixel((u32)(x + i), y + j, g_TilePtrs[tile & 0xff] ? g_TilePtrs[tile & 0xff][j * 4 + i] : 0);
}

/* 0x14677 Level_DrawOverviewMap(col, row): 80 x 44 tiles as 4x4 cells at absolute VRAM row 100 */
void Level_DrawOverviewMap(int col, int row)
{
    for (int c = 0; c < 0x50; c++)
        for (int r = 0; r < 0x2c; r++) Video_Fill4x4(c * 4, r * 4 + 100, Map_GetTile(col + c, row + r));
}

/* 0x4011f Mission_CompleteScreen: after landing with photos taken (camera pod) or the recon flag 0x901b8, one
 * overview page per photo column, with the MP2 objects of the 73 columns around it marked. */
void Mission_CompleteScreen(void)
{
    DS32(0x9028C) = 0;
    g_LoopK = 1;
    if (DS32(0x901B8) > 0) {
        g_Score[g_AeroPlayer] += (g_PlaneClass + 1) * g_MissionBonus;
    } else if (DS32(0x9028C) < g_PhotoCount) {
        DS32(0x901B8) = DS32A(0x91304)[DS32(0x9028C)];
        DS32(0x9028C)++;
    }
    Video_ClassicScreen(true);                  /* ENH: the overview is a 320x240 screen, pillarboxed in a wide view */
    while (DS32(0x901B8) > 0) {
        while (g_AnyInput != 0) { Input_ReadControls(); Platform_Spin(); }   /* PORT: pump events */
        Video_SetStartAndPan(0, 100, 0);
        Level_DrawOverviewMap(DS32(0x901B8) - 0x1f, 0x14);
        strcpy(DSTR(0x85048), DSTR(0x8B8C8));
        strcat(DSTR(0x85048), DSTR(0x81470));
        itoa_js(g_LoopK, DSTR(0x85048) + strlen(DSTR(0x85048)));
        Text_DrawSmall(0xa0 - Text_WidthSmall(DSTR(0x85048)) / 2, 0x77, DSTR(0x85048), 0);
        DS32(0x9002C) = 0;
        for (g_LoopI = 0; g_LoopI < 0x49; g_LoopI++) {
            int c = DS32(0x901B8) - 0x1c + g_LoopI;
            /* PORT: columns left of 0 would read the heap before the MP2 buffer; 0 (no object) there */
            g_DmgLoop = c < 0 ? 0 : (s32)(Byte_Get(g_MapMp2, c) & 0xff);
            if (g_DmgLoop > 0 && g_DmgLoop < 0x80) {
                Video_DrawLineColor((u32)(g_LoopI * 4 + 0x12), (u32)(DS32(0x9002C) * 8 + 0x84), (u32)(g_LoopI * 4 + 0x12),
                                    (u32)((g_DmgLoop - 0x12) * 4 + 100));
                Text_DrawSmall(g_LoopI * 4 + 8, DS32(0x9002C) * 8 + 0x7d, DSTR(0x8B918), 0);
                DS32(0x9002C) = 1 - DS32(0x9002C);
            }
        }
        g_LoopK++;
        DS32(0x90478) = 0;
        Input_ReadControls();
        while (g_AnyInput == 0) { Input_ReadControls(); Platform_Spin(); }
        while (g_AnyInput == 0) { Input_ReadControls(); Platform_Spin(); }
        if (DS32(0x9028C) < g_PhotoCount && DS32(0x90478) == 0) {
            DS32(0x901B8) = DS32A(0x91304)[DS32(0x9028C)];
            DS32(0x9028C)++;
        } else {
            DS32(0x901B8) = 0;
        }
    }
    Video_ClassicScreen(false);
}
