/* Game flow: port/spec/game_flow.md (§2 Game_Run, §3 menus, §4 Mission_Setup, §5 briefing, §6 plane
 * select, §7 weapons, §8.5/9 end of mission, §10 end game). The in-mission frame loop (§8) is in frame.c. */
#include "game.h"
#include "mission.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "files.h"
#include "host.h"
#include "level.h"
#include "lzw.h"
#include "pic.h"
#include "platform.h"
#include "sound.h"
#include "video.h"

u8 *g_MiscData;
u8 *g_MiscZData;
u8 *g_HeliHD;

#define WEP_TYPE(w)  g_WeaponType[(w) * 6]

static void strip_last(char *s)
{
    /* s[strlen(s) - 1] = 0, as the original (writes s[-1] for an empty string: inside the image). */
    size_t n = strlen(s);
    s[(ptrdiff_t)n - 1] = 0;
}

/* Watcom fgets at end of file leaves the buffer as it was. */
static void fgets_js(char *buf, int n, FILE *f)
{
    if (!fgets(buf, n, f)) { /* buffer unchanged */ }
}

/* fscanf of one int into an image int (the result is not checked by the original). */
static void scan_int(FILE *f, const char *fmt, s32 *dst)
{
    int v;
    if (fscanf(f, fmt, &v) == 1) *dst = v;
}

/* The original writes a 4-byte extension (+ NUL) over the last 4 characters of a name ("x.Abk" -> "x.spx");
 * a name shorter than 4 characters is written before its start, as the original does (inside the image). */
static void replace_ext(char *s, const char *ext)
{
    size_t n = strlen(s);
    memcpy(s + (ptrdiff_t)n - 4, ext, strlen(ext) + 1);
}

static u16 be16(const u8 *p) { return (u16)(p[0] << 8 | p[1]); }

/* ================================================================ zones (§3.3) */

#define g_Zones DS32A(0x83358)                  /* int[0x140], records {id, x1, y1, x2, y2} */

/* 0x11e64 Zone_Clear */
void Zone_Clear(void)
{
    for (int i = 0; i < 0x140; i++) g_Zones[i] = -1;
}

/* 0x11d02 Zone_Add: first int == -1 (scanned int by int), records packed. */
void Zone_Add(int id, int x1, int y1, int x2, int y2)
{
    int i = 0;
    while (g_Zones[i] != -1 || g_Zones[i] == id) i++;
    if (i > 0x140) FatalError(DSTR(0x80CD8), DSTR(0x80CCD), 7);   /* "zonemap", " overflow!" */
    g_Zones[i] = id;
    g_Zones[i + 1] = x1;
    g_Zones[i + 2] = y1;
    g_Zones[i + 3] = x2;
    g_Zones[i + 4] = y2;
}

/* 0x11dbc Zone_HitTest */
int Zone_HitTest(int x, int y)
{
    for (int i = 0; g_Zones[i] != -1; i += 5)
        if (g_Zones[i + 1] <= x && x <= g_Zones[i + 3] && g_Zones[i + 2] <= y && y <= g_Zones[i + 4])
            return g_Zones[i];
    return 0;
}

/* ================================================================ Game_Run (§2) */

/* 0x1ba0c Game_Run */
void Game_Run(void)
{
    char tmp[100];
    /* ---- §2.1 one-time initialisation */
    g_MapWidth = 0;
    g_SarcasmLines[0] = 0;
    for (DS32(0x90470) = 0; DS32(0x90470) < 0x1f; DS32(0x90470)++) D8(0x849B8 + DS32(0x90470)) = 0;
    static const s32 t908[18] = { 0, 0, -1, 1, -1, 1, 0, -1, -1, -1, -1, 1, 1, 1, 1, 1, 1, 0 };
    static const s32 t950[18] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, -1, -1, 1, 1, 0, 1, -1, 1 };
    for (int i = 0; i < 18; i++) { DS32A(0x8D908)[i] = t908[i]; DS32A(0x8D950)[i] = t950[i]; }
    DS32(0x8D7E8) = 4; DS32(0x8D7EC) = 8;
    DS32(0x8DA88) = 0; DS32(0x8DA8C) = 6; DS32(0x8DA90) = 0xc; DS32(0x8DA94) = 0x12;
    DS32(0x90CD8) = 0x1c; DS32(0x90BE4) = 0x17; DS32(0x90CDC) = 0x1d; DS32(0x90BE8) = 0x14;
    DS32(0x90CE0) = 0x1d; DS32(0x90BEC) = 0x10;
    DS32(0x90CFC) = 0x18c; DS32(0x90D00) = 0x18d; DS32(0x90D04) = 0x1c6; DS32(0x90D08) = 0x1c7;
    DS32(0x90D0C) = 0x1c8; DS32(0x90D10) = 0x1c9;
    DS32(0x90CE4) = 0x197; DS32(0x90CE8) = 0x198; DS32(0x90CEC) = 0; DS32(0x90CF0) = 0x199; DS32(0x90CF4) = 0x19a;
    for (g_LoopI = 7; g_LoopI < 0x65; g_LoopI++) DS32A(0x90CF8)[g_LoopI] = g_LoopI + 0x2e;
    DS32(0x90CC4) = 0; DS32(0x90C84) = 0; DS32(0x90CC8) = 1; DS32(0x90C88) = 2; DS32(0x90CCC) = 0xb;
    DS32(0x90C8C) = 2; DS32(0x90CD0) = 0xe; DS32(0x90C90) = 2; DS32(0x90CD4) = 0xf; DS32(0x90C94) = 0;
    for (g_LoopI = 0; g_LoopI < 0xb; g_LoopI++) DS32A(0x90C2C)[g_LoopI] = g_LoopI;

    FILE *f = Platform_Fopen(DSTR(0x81092), DSTR(0x81090));       /* "data/gendat3.dax", "r" */
    if (!f) FatalError(DSTR(0x810AF), DSTR(0x810A3), 1);
    const char *fmt = DSTR(0x810BB);                              /* "%d\n" */
    for (g_LoopI = 0; g_LoopI < 5; g_LoopI++) {
        scan_int(f, fmt, &DS32A(0x8D8C0)[g_LoopI]);
        scan_int(f, fmt, &DS32A(0x8D8D4)[g_LoopI]);
    }
    for (g_LoopI = 0; g_LoopI < 0x12; g_LoopI++) {
        scan_int(f, fmt, &DS32A(0x8D818)[g_LoopI]);
        scan_int(f, fmt, &DS32A(0x8D860)[g_LoopI]);
    }
    for (g_LoopI = 0; g_LoopI < 6; g_LoopI++) scan_int(f, fmt, &DS32A(0x8D8A8)[g_LoopI]);
    for (g_LoopI = 0; g_LoopI < 4; g_LoopI++) fgets_js(tmp, 0x50, f);
    for (g_LoopI = 0; g_LoopI < 4; g_LoopI++) scan_int(f, fmt, &g_AeroRoundsOptions[g_LoopI]);
    for (g_LoopI = 1; g_LoopI < 0x1c; g_LoopI++) scan_int(f, fmt, &DS32A(0x90B48)[g_LoopI]);
    DS32(0x8D7E8) = 4; DS32(0x8D7EC) = 8;
    for (g_LoopI = 0; g_LoopI < 4; g_LoopI++) scan_int(f, fmt, &DS32A(0x8DA88)[g_LoopI]);
    for (g_LoopI = 0; g_LoopI < 3; g_LoopI++) {
        scan_int(f, fmt, &DS32A(0x90CD8)[g_LoopI]);
        scan_int(f, fmt, &DS32A(0x90BE4)[g_LoopI]);
    }
    for (g_LoopI = 1; g_LoopI < 7; g_LoopI++) scan_int(f, fmt, &DS32A(0x90CF8)[g_LoopI]);
    for (g_LoopI = 0; g_LoopI < 5; g_LoopI++) scan_int(f, fmt, &DS32A(0x90CE4)[g_LoopI]);
    for (g_LoopI = 7; g_LoopI < 0x65; g_LoopI++) DS32A(0x90CF8)[g_LoopI] = g_LoopI + 0x2e < 0x4b ? g_LoopI + 0x2e : 0x4a;
    for (g_LoopI = 0; g_LoopI < 5; g_LoopI++) {
        scan_int(f, fmt, &DS32A(0x90CC4)[g_LoopI]);
        scan_int(f, fmt, &DS32A(0x90C84)[g_LoopI]);
    }
    for (g_LoopI = 0; g_LoopI < 8; g_LoopI++) {
        scan_int(f, fmt, &DS32A(0x8DEB8)[g_LoopI]);              /* g_HeliMaxLeft */
        scan_int(f, fmt, &DS32A(0x8E310)[g_LoopI]);              /* g_HeliMaxRight */
    }
    for (g_LoopI = 0; g_LoopI < 7; g_LoopI++) {
        scan_int(f, fmt, &DS32A(0x8DED8)[g_LoopI]);
        scan_int(f, fmt, &DS32A(0x8DF00)[g_LoopI]);
    }
    for (g_LoopI = 0; g_LoopI < 7; g_LoopI++) {
        DS32A(0x90AFC)[g_LoopI] = 0;
        fgets_js(DSTR(0x86148) + g_LoopI * 0x50, 0x50, f);       /* g_MusicTitles */
        fgets_js(DSTR(0x86170) + g_LoopI * 0x50, 0x50, f);
        scan_int(f, fmt, &DS32A(0x90B18)[g_LoopI]);              /* g_MusicTracks */
    }
    strcpy(DSTR(0x86378), DSTR(0x810BF));                         /* "Negative G" */
    strcpy(DSTR(0x863A0), DSTR(0x810CA));                         /* "Adam F" */
    DS32(0x90B34) = DS32(0x90B30) + 1;
    fclose(f);
    DS32(0x90308) = 0; DS32(0x90364) = 6; DS32(0x90524) = 1; DS32(0x9000C) = 0xe; DS32(0x90A2C) = 0x14f;
    Mem_AllocMapBuffers();
    File_LoadWhole(DSTR(0x810DD), DSTR(0x810D1), (void **)&g_MapBuf, 0x4e20);   /* data/gendat2.dax */
    const u8 *p = g_MapBuf;
    for (g_LoopI = 0; g_LoopI < 0x10; g_LoopI++) {
        DS32A(0x8DE38)[g_LoopI * 2] = be16(p);                   /* g_VehicleSprites */
        DS32A(0x8DE3C)[g_LoopI * 2] = be16(p + 2);
        DS32A(0x8E1D0)[g_LoopI] = be16(p + 4);                   /* g_VehicleHP */
        p += 6;
    }
    for (g_LoopI = 0; p += 2, g_LoopI < 8; g_LoopI++) DS32A(0x90E8C)[g_LoopI] = be16(p);   /* sic: skips a word first */
    for (g_LoopI = 0; g_LoopI < 10; g_LoopI++) { DS32A(0x9255C)[g_LoopI] = be16(p); p += 2; }   /* g_BonusSprites */
    for (g_LoopI = 0; g_LoopI < 0x10; g_LoopI++) { DS32A(0x8FA78)[g_LoopI] = be16(p); p += 2; }
    for (g_LoopI = 0; g_LoopI < 7; g_LoopI++) { DS32A(0x90EB0)[g_LoopI] = be16(p); p += 2; }    /* g_KillTallyValue */
    DS32(0x92560) = 0xd0; DS32(0x92578) = 0xd6; DS32(0x92564) = 200; DS32(0x9257C) = 0xd7;
    DS32(0x92568) = 0xd2; DS32(0x92580) = 0xe0; DS32(0x9256C) = 0xd3; DS32(0x92584) = 0xdc;
    DS32(0x92570) = 0xd4; DS32(0x92588) = 0x4e; DS32(0x92574) = 0xd5; DS32(0x9258C) = 0x1fc;
    Sprites_LoadSpx(DSTR(0x810E3));                               /* 2nd call: jetsprit.pal */
    File_LoadWhole(DSTR(0x810DD), DSTR(0x810F1), (void **)&g_MiscData, -1);    /* data/misc */
    File_LoadWhole(DSTR(0x810DD), DSTR(0x810F6), (void **)&g_MiscZData, -1);   /* data/misc.z */
    DS32(0x90AF4) = 1; DS32(0x9025C) = 0; DS32(0x906E8) = -1; DS32(0x9081C) = 0x69; DS32(0x90468) = 1;
    g_EnemySpxLoaded = -1;

    /* ---- §2.2 outer loop */
    for (;;) {
        if (DS32(0x907B4) == 1) DS32(0x907B4) = 0;
        if (g_AbortFlag != 2) {
            g_MenuRepeat = 1;
            while (g_MenuRepeat > 0) MainMenu();
        }
        if (g_QuitGame) {
            Pal_Fade(0, 0x100, 0, 0x20);
            return;
        }
        g_AbortFlag = 0;
        if (g_GameMode != 1) g_TrainingMode = 0;
        DS32(0x90A08) = -1; DS32(0x90A0C) = -1; DS32(0x9037C) = 0; DS32(0x90998) = 0; DS32(0x90398) = 0;
        DS32(0x90528) = 0;
        /* ---- §2.3 attempt loop */
        while (g_Lives > 0 && g_Mission < 200 && g_AbortFlag == 0) {
            DS32(0x8FF94) = 0; DS32(0x8FF8C) = 0; DS32(0x8FF90) = 0; DS32(0x8FF78) = 0; DS32(0x8FF74) = 0;
            g_FogActive = 0;
            if (DS32(0x907B4) == 1) DS32(0x907B4) = 0;
            Video_SetSplitLine(400);
            Mission_Setup();
            DS32(0x8FEF4) = 0;
            if (g_FogActive) Pal_SaveNight();
            DS32(0x900B4) = 0; DS32(0x8FF68) = 0; DS32(0x8FF70) = 0; DS32(0x90854) = 0; DS32(0x900A4) = 1;
            DS32(0x907C8) = -1;
            for (g_LoopI = 0; g_MP_TargetX0 != 0 && DS32(0x900B4) < 6 && g_GameMode == 0 && g_LoopI < 10; g_LoopI++)
                TargetVehicle_Spawn();
            DS32(0x8FFEC) = 0; DS32(0x90288) = 0; DS32(0x902B0) = 0; DS32(0x909A0) = 0; DS32(0x905EC) = -1;
            DS32(0x903FC) = 0; DS32(0x90360) = 0; DS32(0x8FFF0) = 0;
            if (g_Mission < 0x1f) g_AutoEjectOn = 1;
            if (g_GameMode > 0) {
                if (g_GameMode == 1 || (g_GameMode == 2 && g_PracticeEjects > 0)) { g_AutoEjectOn = 1; g_Lives = 7; }
                if (g_GameMode == 3) { DS32(0x8FFF0) = 0xc80; DS32(0x909B8) = 1; }
            }
            Particles_Clear();
            Particles_Nop();
            DS32(0x90318) = 0;
            Bullets_Clear();
            DS32(0x9064C) = 0; DS32(0x90100) = 0; DS32(0x907B0) = 0; DS32(0x9041C) = 0; DS32(0x8FFD0) = 0;
            DS32(0x8FFF8) = 0; DS32(0x90328) = 0;
            DS32(0x8FEB0) = Sign(DS32(0x8FEB0));
            DS32(0x9017C) = g_BaseStartX + 8; DS32(0x8FF10) = -1; DS32(0x900B0) = 1; DS32(0x900AC) = 0;
            DS32(0x90980) = 0; DS32(0x90864) = 0; DS32(0x90860) = 1;
            DS32(0x901E4) = 0;
            for (g_LoopI = g_BaseEndX / 16; g_BaseEndX / 16 - 10 <= g_LoopI; g_LoopI--)
                for (g_LoopJ = 0; g_LoopJ > -5; g_LoopJ--)
                    if (Map_GetTileAttr(g_LoopI, 0x3f - DS32(0x901AC) / 16 + g_LoopJ, 0) == 0x8c) {
                        DS32(0x901E4) = g_LoopI * 16 - 16;
                        DS32(0x901E8) = g_LoopJ * 16 - DS32(0x901AC) + 0x3ef;
                        DS32(0x901D0) = 0x1e2; DS32(0x901CC) = 1;
                        g_LoopI = 0; g_LoopJ = -8;
                    }
            DS32(0x90590) = g_BaseStartX + 16 + Rand(g_BaseEndX - g_BaseStartX - 0x80);
            for (g_LoopI = 0; g_LoopI < 4; g_LoopI++) {
                s32 base = g_BaseStartX + 16;
                DS32A(0x90C04)[g_LoopI] = base + Rand(g_BaseEndX - g_BaseStartX - 0x80);
                DS32A(0x90C18)[g_LoopI] = 0x3df - DS32(0x901AC);
                DS32A(0x90BF0)[g_LoopI] = 0x1d0;
                DS32A(0x90BD0)[g_LoopI] = -1;
                if (Rand(1) != 0) { DS32A(0x90BF0)[g_LoopI] = 0x1d2; DS32A(0x90BD0)[g_LoopI] = 1; }
            }
            DS32(0x9089C) = 0; DS32(0x90744) = 0; DS32(0x9075C) = 0;
            DS32(0x92B04) = 0;
            while (DS32(0x92B04) < 2 || (6 < DS32(0x92B04) && DS32(0x92B04) < 0xe)) DS32(0x92B04) = Rand(13) + 2;
            CD_PlayTrack(DS32(0x92B04));

            Mission_Run();                        /* the frame loop (§8), frame.c */

            /* §8.5 */
            if (g_MissionActive == 1) g_AbortFlag = 1;
            if (g_OnGround == 0 && g_AlienAbduct < 0x40) {
                g_SpecialHit = 0; DS32(0x901B8) = 0; DS32(0x902CC) = 0;
                if (g_PlaneClass == 0) g_Lives--;
                if (g_GameMode == 0 && g_PlaneLimit[g_PlaneSel - 1] < 200
                    && (g_PlaneUsed[g_PlaneSel - 1] += 3) >= g_PlaneLimit[g_PlaneSel - 1]
                    && g_AlienPlaneIdx + 1 != g_PlaneSel)
                    g_PlaneSel = 0;
                if (DS32(0x90888) == 0) {
                    g_RocketCount = 0; DS32(0x906A8) = 0; DS32(0x909E4) = 0; DS32(0x90648) = 0; DS32(0x90710) = 0;
                    g_ArmourBonus = 0; g_MissionBonus = 0; g_GunPowerUp = 0; g_GunPowerUp2 = 0;
                } else {
                    DS32(0x90888)--;
                }
            }
        }
        /* §9.1 */
        if (g_CDMusicOn) CD_Stop();
        g_HudMsgCount = 0;
        Level_DrawBackground(DS32(0x904F0) + 1, DS32(0x904F4) + 1);
        Sprite_DrawQueue();
        Video_FlipPage();
        Sound_StopAll();
        if (g_MissionResult == 2 && g_GameMode < 3 && g_Lives > -8) {
            for (g_LoopI = 0; g_LoopI < 0x3c; g_LoopI++)
                if (g_GameMode == 0 && g_PlaneLimit[g_LoopI] < 200 && g_PlaneFlown[g_LoopI] == 1) g_PlaneUsed[g_LoopI]++;
            if (g_PlaneLimit[g_PlaneSel - 1] <= g_PlaneUsed[g_PlaneSel - 1]) g_PlaneSel = 0;
            g_Mission++;
            DS32(0x9038C) = 0;
        }
        if ((g_GameMode > 0 && ((g_Mission == 10 && g_GameMode != 3) || g_AeroRound == g_AeroRoundsOptions[g_AeroRoundsSel]))
            || g_Mission > 0x95 || (g_FinalMission == 1 && g_MissionResult == 2))
            g_Lives = -10;
        if (g_Lives > -999) Mission_Debrief();
        if (g_FogActive) Pal_Restore();
        /* §9.3 Aerolimits rotation */
        if (g_GameMode == 3 && g_Lives > -8) {
            g_AbortFlag = 0;
            if (g_Lives > 1) g_Lives = 7;
            g_AeroPlayer = Wrap(g_AeroPlayer + 1, 0, g_AeroPlayers - 1);
            s32 b = g_BaseStartX - 500;
            DS32(0x90910) = b + Rand(0x4b0);
            g_BriefedMission = -1;
            DS32(0x9038C) = 0;
            if (g_AeroPlayer == 0) {
                while (!Bit_Test(g_Mission, g_AeroMissionMask) && g_AeroMissionMask != 0) g_Mission = Rand(0x14);
                Bit_Clear(g_Mission, &g_AeroMissionMask);
                if (++g_AeroRound == g_AeroRoundsOptions[g_AeroRoundsSel]) g_Lives = -10;
            }
        }
        /* §10 */
        if (g_AbortFlag == 1 || (g_Lives < 1 && g_Lives > -999)) EndGame_Screen();
        else g_AbortFlag = 2;
        DS32(0x90470) = 1;
        WaitKey_Release();
        Pal_Fade(0, 0x100, 0, 0x20);
        Video_FillRect(0, 0, 0x140, 0xf0, 0);
        for (DS32(0x90470) = 0; DS32(0x90470) < 0xff; DS32(0x90470)++)
            if (DS32(0x90470) < 0x40 && 0x60 < DS32(0x90470)) Pal_SetColor(DS32(0x90470), 0, 0, 0);   /* dead (Q17) */
        if (g_AbortFlag == 1 || g_Lives < 0) {
            DS32(0x9038C) = 0;
            if (g_GameMode != 0) g_MenuDone = 2;
            g_BaseStartX = 0;
            g_BaseEndX = 0;
            g_AbortFlag = 0;
            if (g_MenuDone != 2) g_Lives = 7;
        }
    }
}

/* ================================================================ main menu (§3) */

#define g_MenuColX DS32A(0x90BC4)               /* {4, 0x4a, 0x25} */
#define g_MenuRowY DS32A(0x8DA98)               /* 8 rows */

/* 0x47ec2 MainMenu_Draw */
void MainMenu_Draw(void)
{
    Pic_LoadPax(DSTR(0x81679), 0, 0);                             /* miscon.pax */
    Pal_Fade(0, 0x40, 1, 0x20);
    Zone_Clear();
    g_MenuChoice = 0xb;
    g_MenuDone = 0;
    DS32(0x8FEF0) = 0x10;
    DS32(0x8FEF8) = 0xb7;
    for (g_LoopI = 0; g_LoopI < 5; g_LoopI++) {
        DS32(0x8FEDC) = (g_LoopI % 2) * 0x45;
        DS32(0x8FEA0) = (g_LoopI / 2) * 0x13;
        Zone_Add(g_LoopI + 1, DS32(0x8FEDC) + 5, DS32(0x8FEA0) + 0x34, DS32(0x8FEDC) + 0x48, DS32(0x8FEA0) + 0x40);
    }
    Zone_Add(5, 5, 0x46, 0x8e, 0x59);
    for (g_LoopI = 0; g_LoopI < 10; g_LoopI++) {
        DS32(0x8FEDC) = (g_LoopI % 2) * 0x45;
        DS32(0x8FEA0) = (g_LoopI / 2) * 0x13;
        Zone_Add(g_LoopI + 6, DS32(0x8FEDC) + 5, DS32(0x8FEA0) + 0x79, DS32(0x8FEDC) + 0x48, DS32(0x8FEA0) + 0x85);
    }
    g_CursorX = 0;
    g_CursorY = 0;
    g_MenuDX = 0;
    g_MenuDY = 0;
    g_MenuColX[0] = 4; g_MenuColX[1] = 0x4a; g_MenuColX[2] = 0x25;
    static const s32 rows[8] = { 0x34, 0x47, 0x59, 0x79, 0x8c, 0x9f, 0xb2, 0xc5 };
    for (int i = 0; i < 8; i++) g_MenuRowY[i] = rows[i];
}

static u32 menu_seed(void)
{
    /* PORT (developer aid): JS_SEED=<n> replaces time(NULL) for reproducible runs. */
    const char *s = getenv("JS_SEED");
    return (s && *s) ? (u32)strtoul(s, NULL, 0) : (u32)time(NULL);
}

/* 0x469e7 MainMenu */
void MainMenu(void)
{
    /* PORT: CD_Check (MSCDEX, "Wrong CD") is dropped (PLAN.md decision 2). */
    if (g_CDCheckDone == 0) g_CDCheckDone = 1;
    g_SfxOn = g_Config.sfx;
    if (g_Config.sfx != 0 && g_SoundInitDone == 0) {
        Sound_Init();
        if (g_SoundDevice == 0) FatalError(DSTR(0x8158C), DSTR(0x8152F), 3);
        g_SoundInitDone = 1;
    }
    g_CDMusicOn = g_Config.cd_music;
    srand_js(menu_seed());
    DS32(0x90804) = 0;
    for (g_LoopI = 0; g_LoopI < 8; g_LoopI++) g_AeroUsedRacks[g_LoopI] = 0;
    g_SpecialHit = 0;
    g_AeroRound = 0;
    for (g_LoopI = 0; g_LoopI < 5; g_LoopI++) g_AeroGateX[g_LoopI] = 0;
    g_ExtraAircraftScore = 250000;
    g_NextBonusScore = 10000;
    g_BonusScoreStep = 12000;
    for (g_LoopI = 0; g_LoopI < 0x47; g_LoopI++) g_PlaneUsed[g_LoopI] = 0;
    DS32(0x906EC) = 0;
    g_Score[0] = 0;
    g_Score[1] = 0;
    g_Kills = 0;
    DS32(0x90890) = 0;
    if (g_TrainingPicked == 0) {
        for (int i = 0; i < 0x100; i++)
            if (i < 0x40 || 0x7f < i) Pal_SetColor(i, 0, 0, 0);
        Pal_UploadAll();
    }
    DS32(0x90494) = 0x10000;
    Video_ShowPage(0);
    DS32(0x909F8) = -1;
    if (DS32(0x849E0) == 0 && g_TrainingPicked == 0) {
        Fonts_Load();
        DS32(0x849E0) = DS32(0x849C0);
        DS32(0x849C0) = 0;
    }
    FILE *f = Platform_Fopen(DSTR(0x815AF), DSTR(0x815AD));       /* data/jets.n, "r" */
    if (!f) FatalError(DSTR(0x815AF), DSTR(0x815BB), 1);
    scan_int(f, DSTR(0x815C7), &g_PlaneCount);
    for (g_LoopI = 0; g_LoopI <= g_PlaneCount; g_LoopI++) {
        char *name = g_PlaneNames + g_LoopI * 0x28;
        fgets_js(name, 0x50, f);
        strip_last(name);
        if (strcmp(name, DSTR(0x815CB)) == 0) {                   /* "Alien Superfighter" */
            g_PlaneUsed[g_LoopI] = 9999;
            g_AlienPlaneIdx = g_LoopI;
        }
    }
    fclose(f);
    if (DS32(0x849CC) == 0) File_LoadStub(DSTR(0x815DE), 0, -1);
    DS32(0x8F814) = 0xde;
    DS32(0x8FA1C) = 0x40;
    f = Platform_Fopen(DSTR(0x815EE), DSTR(0x815AD));             /* data/weapons.dat */
    if (!f) FatalError(DSTR(0x8160A), DSTR(0x815FF), 1);
    scan_int(f, DSTR(0x815C7), &g_WeaponCount);
    const char *d = DSTR(0x81616), *dn = DSTR(0x815C7);           /* "%d", "%d\n" */
    for (g_LoopI = 0; g_LoopI <= g_WeaponCount + 1; g_LoopI++) {
        char *name = g_WeaponNames + g_LoopI * 0x28;
        fgets_js(name, 0x28, f);
        strip_last(name);
        for (g_LoopJ = 0; g_LoopJ < 6; g_LoopJ++) scan_int(f, d, &g_WeaponType[g_LoopJ + g_LoopI * 6]);
        scan_int(f, d, &g_WeaponBlastA[g_LoopI]);
        scan_int(f, d, &g_WeaponBlastB[g_LoopI]);
        scan_int(f, d, &g_WeaponThrust[g_LoopI]);
        scan_int(f, d, &g_WeaponIcon[g_LoopI]);
        scan_int(f, d, &g_WeaponPerRack[g_LoopI]);
        scan_int(f, d, &g_WeaponWeight[g_LoopI]);
        scan_int(f, dn, &g_WeaponRackMult[g_LoopI]);
        char *desc = g_WeaponDesc + g_LoopI * 0xa0;
        fgets_js(desc, 0xa0, f);
        strip_last(desc);
        scan_int(f, d, &g_WeaponStock[g_LoopI]);
        scan_int(f, dn, &g_WeaponResupply[g_LoopI]);
    }
    fclose(f);
    f = Platform_Fopen(DSTR(0x81619), DSTR(0x815AD));             /* data/hudtext.dat */
    if (!f) FatalError(DSTR(0x8162A), DSTR(0x815FF), 1);
    g_LoopI = 0;
    while (!feof(f)) {
        char *line = HUDTEXT(g_LoopI);
        fgets_js(line, 0x50, f);
        strip_last(line);
        g_LoopI++;
    }
    fclose(f);
    if (g_TrainingPicked == 0) {
        f = Platform_Fopen(DSTR(0x81639), DSTR(0x81636));         /* data/l1l2, "rb" */
        if (!f) FatalError(DSTR(0x81643), DSTR(0x815BB), 1);
        for (g_LoopI = 0; g_LoopI < 0x3c; g_LoopI++) {
            u8 b = 0;
            if (fread(&b, 1, 1, f) != 1) { /* not checked */ }
            g_PlaneLimit[g_LoopI] = b;
        }
        fclose(f);
        f = Platform_Fopen(DSTR(0x81648), DSTR(0x815AD));         /* data/gendatad.dax */
        if (!f) FatalError(DSTR(0x8165A), DSTR(0x815FF), 1);
        for (g_LoopI = 0; g_LoopI < 0x10; g_LoopI++) fgets_js(DSTR(0x85C48) + g_LoopI * 0x50, 0x28, f);
        fclose(f);
        File_LoadWhole(DSTR(0x81673), DSTR(0x81667), (void **)&g_MapBuf, 0);    /* data/gendata.dax */
        u8 *w = g_MapBuf;
        for (g_LoopI = 0; g_LoopI < 0x66; g_LoopI++) SwapByte(&w[g_LoopI * 2], &w[g_LoopI * 2 + 1]);
#define W16(q) ((u16)((q)[0] | (q)[1] << 8))
        const u8 *q = g_MapBuf;
        g_LoopI = 0;
        for (; W16(q) != 0xffff; q += 2) {
            DS32A(0x90FE8)[g_LoopI] = W16(q);                     /* g_NightFlashColours */
            if (W16(q) == 0x23) {
                DS32A(0x90FE8)[g_LoopI] = 0;
                g_LoopI = (g_LoopI / 10) * 10 + 9;
            }
            g_LoopI++;
        }
        q += 2;
        for (g_LoopI = 0; g_LoopI < 3; g_LoopI++)
            for (g_LoopJ = 0; g_LoopJ < 3; g_LoopJ++) { D16A(0x92980)[g_LoopI + g_LoopJ * 3] = W16(q); q += 2; }
        for (g_LoopI = 0; g_LoopI < 0x1a; g_LoopI++) { D16(0x8F088 + g_LoopI * 4) = W16(q); q += 2; }   /* g_ParamNotObjective */
        for (g_LoopI = 0; g_LoopI < 0x1a; g_LoopI++) { D16(0x8F08A + g_LoopI * 4) = W16(q); q += 2; }
        for (g_LoopI = 0; g_LoopI < 0x10; g_LoopI++) {
            float a = (float)((double)g_LoopI * 0.39269908125);   /* 0x3fd921fb4d12d84a */
            DF32(0x90208) = a;
            DS32A(0x8FC30)[g_LoopI] = (s32)(-cos((double)DF32(0x90208)) * 8.0);
            DS32A(0x8FC70)[g_LoopI] = (s32)(-sin((double)DF32(0x90208)) * 6.0);
            DS32A(0x8FAF0)[g_LoopI] = DS32A(0x8FC30)[g_LoopI] * 2;
            DS32A(0x8FB30)[g_LoopI * 2] = DS32A(0x8FC70)[g_LoopI] * 2;
            DS32A(0x8FB34)[g_LoopI * 2] = 3;
        }
        for (g_LoopI = 0; g_LoopI < 0x10; g_LoopI++) { DS32A(0x92674)[g_LoopI] = W16(q); q += 2; }   /* g_ProjSpriteBase */
        for (g_LoopI = 0; g_LoopI < 6; g_LoopI++) { DS32A(0x8FC18)[g_LoopI] = W16(q); q += 2; }
#undef W16
        static const s32 fc18[6] = { 0x36, 0x37, 0xaf, 0xb0, 0xb1, 0x45 };
        for (int i = 0; i < 6; i++) DS32A(0x8FC18)[i] = fc18[i];
        for (g_LoopI = 0; g_LoopI < 10; g_LoopI++)
            DF32(0x863C8 + g_LoopI * 4) = (float)((double)(DS32(0x903B0) * g_LoopI) / 10.8 + 1.0);   /* g_MaxSpeedTab */
        s32 *dir = DS32A(0x8FCB8), *dir2 = DS32A(0x8FCBC);       /* g_PlayerDirSprite pairs */
        for (g_LoopI = 0; g_LoopI < 9; g_LoopI++) {
            dir[g_LoopI * 2] = g_LoopI + 1;
            dir2[g_LoopI * 2] = g_LoopI + 1;
            if (g_LoopI < 8) {
                dir[(0x10 - g_LoopI) * 2] = Sprite_Mirror(g_LoopI + 1);
                dir2[(0x10 - g_LoopI) * 2] = Sprite_Mirror(g_LoopI + 1);
            }
        }
        for (g_LoopI = 0x11; g_LoopI < 0x19; g_LoopI++) {
            dir[g_LoopI * 2] = g_LoopI - 7;
            dir2[g_LoopI * 2] = g_LoopI - 7;
            if (g_LoopI < 0x18) {
                dir[(0x30 - g_LoopI) * 2] = Sprite_Mirror(g_LoopI - 7);
                dir2[(0x30 - g_LoopI) * 2] = Sprite_Mirror(g_LoopI - 7);
            }
        }
        for (g_LoopI = 0; g_LoopI < 10; g_LoopI++) {
            DS32A(0x8FDB8)[g_LoopI * 2] = g_LoopI + 0x2d;
            DS32A(0x8FDBC)[g_LoopI * 2] = g_LoopI + 0x2d;
        }
        DS32(0x8FDCC) = 0x21; DS32(0x8FDD4) = 0x22; DS32(0x8FDDC) = 0x23; DS32(0x8FDE4) = 0x24;
        DS32(0x8FDEC) = 0x25; DS32(0x8FDF4) = 0x26;
        DS32(0x8FCBC) = 0x12; DS32(0x8FCC4) = 0x13; DS32(0x8FCCC) = 0x14;
        DS32(0x8FD2C) = Sprite_Mirror(0x14);
        DS32(0x8FD34) = Sprite_Mirror(0x13);
        DS32(0x8FD3C) = Sprite_Mirror(0x12);
        DS32(0x8FD44) = Sprite_Mirror(0x15);
        DS32(0x8FD4C) = Sprite_Mirror(0x15);
        DS32(0x8FDB4) = 0x15;
        DS32(0x8FDAC) = 0x16;
        g_Lives = 7;
    }
    g_MissionResult = 0;
    g_AeroMissionMask = 0xffffe;
    DS32(0x8F810) = 0x30;
    DS32(0x8FA18) = 0xb0;
    DS32(0x90310) = 0;
    g_MapLoaded[0] = 0;
    g_TilesetName[0] = 0;
    DS32(0x902CC) = 0;
    DS32(0x901B8) = 0;
    g_EnemySetLoaded = -1;
    g_Mission = 0;
    g_BriefedMission = -1;
    g_MissionActive = 0;
    DS32(0x906A8) = 0;
    DS32(0x909E4) = 0;
    DS32(0x90648) = 0;
    g_AutoEjectOn = 0;
    DS32(0x90710) = 0;
    g_ArmourBonus = 0;
    DS32(0x90A5C) = 0;
    g_EnemyAirCount = 0;
    DS32(0x906A4) = 0;
    g_EnemyBaseX = 0;
    g_MenuRepeat = 0;
    if (g_TrainingPicked == 0) {
        g_StartMission = 0;
        DS32(0x9023C) = 0;
        MainMenu_Draw();
        g_CursorX = 0;
        g_CursorY = 0;
        DS32(0x907E8) = -1;                                       /* previous column */
        DS32(0x907F0) = 0;                                        /* previous row */
        while (g_MenuDone == 0) {
            Input_PollMenu();
            /* busy waits on ISR memory (Q24): g_Fire itself is not updated in here */
            do {
                while (g_Fire) Platform_Spin();
            } while ((g_KeyDown[0x39] || g_KeyDown[0x1c]) && (Platform_Spin(), 1));
            Input_PollMenu();
            while (g_Fire == 0 && g_KeyDown[0x39] == 0 && g_KeyDown[0x1c] == 0) {
                if (g_CursorX != DS32(0x907E8) || g_CursorY != DS32(0x907F0)) {
                    if (DS32(0x907E8) != -1) {
                        int pc = DS32(0x907F0) == 2 ? 2 : DS32(0x907E8);
                        Video_CopyRect(0, 0, 0xf0, 0x50, 0x102, 0, g_MenuColX[pc], g_MenuRowY[DS32(0x907F0)]);
                    }
                    int c = g_CursorY == 2 ? 2 : g_CursorX;
                    Video_CopyRect(0, g_MenuColX[c], g_MenuRowY[g_CursorY], g_MenuColX[c] + 0x50,
                                   g_MenuRowY[g_CursorY] + 0x12, 0, 0, 0xf0);
                    Sprite_DrawNow((u32)g_MenuColX[c], (u32)g_MenuRowY[g_CursorY], 0x1ca);
                    DS32(0x907E8) = g_CursorX;
                    DS32(0x907F0) = g_CursorY;
                    while (Input_AnyKey(1)) Platform_Spin();
                }
                Input_PollMenu();
                int nx = g_CursorX + g_MenuDX < 2 ? g_CursorX + g_MenuDX : 1;
                g_CursorX = nx < 0 ? 0 : nx;
                int ny = g_CursorY + g_MenuDY < 8 ? g_CursorY + g_MenuDY : 7;
                g_CursorY = ny < 0 ? 0 : ny;
            }
            int c = g_CursorY == 2 ? 2 : g_CursorX;
            g_ZoneHit = Zone_HitTest(g_MenuColX[c] + 8, g_MenuRowY[g_CursorY] + 4);
            if (g_ZoneHit == 5) { SaveGame_LoadMenu(); g_ZoneHit = 0; g_MenuChoice = 0xb; }
            if (g_ZoneHit == 4) { g_QuitGame = 1; g_MenuDone = 1; }
            if (g_ZoneHit == 3) { AeroOptions_Menu(); g_MenuChoice = 0xc; g_MenuDone = 1; }
            if (1 < g_ZoneHit && g_ZoneHit < 3) { g_MenuChoice = 0xc - g_ZoneHit; g_MenuDone = 1; }
            if (5 < g_ZoneHit) { g_MenuChoice = g_ZoneHit - 6; g_MenuDone = 1; }
            if (0 < g_ZoneHit && g_ZoneHit < 3) { g_MenuChoice = 0xc - g_ZoneHit; g_MenuDone = 1; }
        }
        if (g_MenuChoice != 0xc)
            Video_CopyRect(0, 0, 0xf0, 0x50, 0x102, 0, g_MenuColX[DS32(0x907E8)], g_MenuRowY[DS32(0x907F0)]);
        DS32(0x901B0) = 0;
        g_GameMode = 0;
        g_PracticeEjects = 0;
        if (g_MenuChoice == 0xc) g_GameMode = 3;
        if (g_MenuChoice == 10) { g_GameMode = 2; g_PracticeEjects = 3; }
        if (g_MenuChoice < 10) {
            g_GameMode = 1;
            g_StartMission = g_MenuChoice;
            g_TrainingPicked = 1;
            g_TrainingMode = 1;
        }
    }
    g_AutoEjectOn = 0;
    DS32(0x90170) = 0;
    if (g_TrainingPicked == 1) g_Mission = g_StartMission;
    g_TrainingPicked = 0;
    g_AeroPlayer = 0;
}

/* 0x48111 AeroOptions_Menu: Games / Players / Done (only g_Fire confirms). */
void AeroOptions_Menu(void)
{
    char text[32];
    Pal_Fade(0, 0x100, 0, 0x20);
    Pic_LoadPax(DSTR(0x81684), 0, 0);                             /* aoset.pax */
    Pic_LoadPax(DSTR(0x81684), 1, 0);
    g_MenuDone = 0;
    g_CursorY = 0;
    DS32(0x907F0) = -1;
    g_AeroPlayers = 2;
    g_AeroRoundsSel = 2;
    int changed = 0;
    Video_SetStartAndPan(0, 10, 0);
    Pal_Fade(0, 0x100, 1, 0x20);
    Text_DrawBig(0xa0 - Text_WidthBig(DSTR(0x8168E)) / 2, 0x7b, DSTR(0x8168E), 0);   /* "Games 10" */
    Text_DrawBig(0xa0 - Text_WidthBig(DSTR(0x81697)) / 2, 0x8f, DSTR(0x81697), 0);   /* "Players 2" */
    Text_DrawBig(0xa0 - Text_WidthBig(DSTR(0x816A1)) / 2, 0xb7, DSTR(0x816A1), 0);   /* "Done" */
    for (int x = 0; x < 0x140; x++)
        for (int y = 0x1e0; y < 0x1ef; y++) Video_PutPixel((u32)x, y, 0xf);
    while (g_MenuDone == 0) {
        if (g_CursorY != DS32(0x907F0) || changed) {
            for (int pass = 0; pass < 2; pass++) {
                s32 row = pass == 0 ? DS32(0x907F0) : g_CursorY;
                int y;
                if (row == 0) { y = 0x78; strcpy(text, DSTR(0x816A6)); itoa_js(g_AeroRoundsOptions[g_AeroRoundsSel], text + 6); }
                else if (row < 2) { y = 0x8c; strcpy(text, DSTR(0x816AD)); itoa_js(g_AeroPlayers, text + 8); }
                else if (row == 2) { y = 0xb4; strcpy(text, DSTR(0x816A1)); }
                else { y = 0x78; strcpy(text, DSTR(0x816A6)); itoa_js(g_AeroRoundsOptions[g_AeroRoundsSel], text + 6); }
                if (pass == 0) Video_CopyRect(1, 0, y, 0x140, y + 0xf, 0, 0, y);            /* un-highlight */
                else Video_CopyRect(0, 0, 0x1e0, 0x140, 0x1ef, 0, 0, y);                   /* highlight bar */
                Text_DrawBig(0xa0 - Text_WidthBig(text) / 2, y + 3, text, 0);
            }
            DS32(0x907F0) = g_CursorY;
        }
        Input_PollMenu();
        if (g_MenuDY == -1 && 0 < g_CursorY) g_CursorY--;
        if (g_MenuDY == 1 && g_CursorY < 2) g_CursorY++;
        changed = 0;
        if (g_Fire != 0) {
            DS32(0x907F0) = g_CursorY;
            if (g_CursorY == 1) { g_AeroPlayers = Wrap(g_AeroPlayers + 1, 1, 8); changed = 1; }
            if (g_CursorY == 0) { g_AeroRoundsSel = Wrap(g_AeroRoundsSel + 1, 0, 3); changed = 1; }
            if (g_CursorY == 2) g_MenuDone = 1;
        }
    }
}

/* ================================================================ save / load (§3.5) */

static void save_name(char *name, int slot)
{
    strcpy(name, DSTR(0x813C0));                                  /* "js_save.000" */
    name[10] = (char)(name[10] + slot);                           /* F10 -> "js_save.00:" (Q4) */
}

/* 0x3c0fb SaveGame_LoadMenu */
void SaveGame_LoadMenu(void)
{
    char name[16];
    g_Mission = 0;
    SaveGame_DrawLoadPrompt();
    int slot = Input_GetFKey();
    save_name(name, slot);
    /* PORT: ':' is not a valid file name character under DOS either: fopen fails as on the PC (no NTFS
     * stream syntax). */
    FILE *f = strchr(name, ':') ? NULL : Platform_Fopen(name, DSTR(0x813CC));
    if (!f) {
        Video_FillRect(0, 0, 0x140, 10, 0);
        Text_DrawSmall(0, 0, DSTR(0x813CF), 0);                   /* "Save file not found!" */
        return;
    }
    /* Low byte / word of each global only (the rest keeps its value, game_flow.md §3.5). */
    static const u32 bytes[11] = { 0x902F0, 0x903E0, 0x904A8, 0x90314, 0x906A8, 0x909E4, 0x90078, 0x90710,
                                   0x909E0, 0x90544, 0x90944 };
    for (int i = 0; i < 11; i++) if (fread(DSEG(bytes[i]), 1, 1, f) != 1) { /* not checked */ }
    static const u32 dwords[4] = { 0x90304, 0x902D8, 0x902E4, 0x8E330 };
    for (int i = 0; i < 4; i++) if (fread(DSEG(dwords[i]), 4, 1, f) != 1) { /* not checked */ }
    for (int i = 0; i < 0x47; i++) if (fread(&g_PlaneUsed[i], 2, 1, f) != 1) { /* not checked */ }
    for (int i = 0; i < 0x47; i++) if (fread(&g_WeaponStock[i], 2, 1, f) != 1) { /* not checked */ }
    g_StartMission = g_Mission;
    DS32(0x9023C) = 1;
    g_MenuDone = 1;
    g_MenuChoice = 0xb;
    /* no fclose (game_flow.md Q5, kept) */
}

/* 0x3c367 SaveGame_Write(slot) */
void SaveGame_Write(char slot)
{
    char name[16];
    save_name(name, slot);
    FILE *f = strchr(name, ':') ? NULL : Platform_Fopen(name, DSTR(0x813E4));   /* "wb" */
    if (!f) {
        /* PORT: the original fwrites through the NULL FILE* (F10 -> "js_save.00:", Q4) and stops with a
         * DOS/4GW exception; the port stops with a message instead of a host crash (QUIRKS.md). */
        FatalError(name, ": cannot be created (the original writes through a NULL FILE* here and stops with a DOS/4GW exception)", 1);
    }
    static const u32 bytes[11] = { 0x902F0, 0x903E0, 0x904A8, 0x90314, 0x906A8, 0x909E4, 0x90078, 0x90710,
                                   0x909E0, 0x90544, 0x90944 };
    for (int i = 0; i < 11; i++) fwrite(DSEG(bytes[i]), 1, 1, f);
    static const u32 dwords[4] = { 0x90304, 0x902D8, 0x902E4, 0x8E330 };
    for (int i = 0; i < 4; i++) fwrite(DSEG(dwords[i]), 4, 1, f);
    for (int i = 0; i < 0x47; i++) fwrite(&g_PlaneUsed[i], 2, 1, f);
    for (int i = 0; i < 0x47; i++) fwrite(&g_WeaponStock[i], 2, 1, f);
    fclose(f);
}

/* 0x3c56c SaveGame_DrawLoadPrompt */
void SaveGame_DrawLoadPrompt(void)
{
    Video_FillRect(0, 0, 0x140, 10, 0);
    Text_DrawSmall(0, 0, DSTR(0x813E7), 0);                       /* "Press F1 to F10 to load game..." */
}

/* ================================================================ mission set-up (§4, §7.3, §7.4) */

/* 0x22223 Weapons_ReturnUnused */
void Weapons_ReturnUnused(void)
{
    for (g_RackSel = 0; g_RackSel < 2; g_RackSel++)
        if (g_RackRounds[g_RackSel] > 0) {
            DS32(0x8FF44) = idiv_js(g_RackRounds[g_RackSel], g_WeaponPerRack[g_RackWeapon[g_RackSel]], "Weapons_ReturnUnused");
            g_WeaponStock[g_RackWeapon[g_RackSel]] += DS32(0x8FF44);
            g_RackRounds[g_RackSel] = 0;
        }
}

/* 0x222f0 Mission_ResetState: rack loads (§7.1), load weight, fuel, gun ammo. */
void Mission_ResetState(void)
{
    static const char *const where = "Mission_ResetState";
    if (g_CDMusicOn) CD_Stop();
    g_FuelBase = g_PlaneFuel;
    g_LoadWeight = 0;
    for (g_RackSel = 0; g_RackSel < 2; g_RackSel++) {
        s32 r = g_RackSel, w = g_RackWeapon[r];
        s32 per = g_WeaponPerRack[w], wt = g_WeaponWeight[w];
        if (g_HardpointLoad < wt) {
            g_RackRounds[r] = 0;
        } else {
            s32 a = idiv_js(g_RackMaxLoad[r], wt, where) * per;
            s32 b = per * idiv_js(g_HardpointLoad * g_RackPoints[r], wt, where);
            s32 m = a < b ? a : b;
            s32 cap = per * g_WeaponRackMult[w] * g_RackPoints[r];
            if (cap < m) m = cap;
            s32 n = g_WeaponStock[w] * per < m ? g_WeaponStock[w] * per : m;
            DS32(0x8FF30) = n;
            g_RackRounds[r] = n;
            if (WEP_TYPE(w) == 10) {
                g_RackRounds[r] = g_RackRounds[r] < DS32(0x90838) ? g_RackRounds[r] : DS32(0x90838);
                g_LoadWeight = wt * g_RackRounds[r];                  /* sic: '=' (Q12) */
            }
            g_LoadWeight += idiv_js(wt * g_RackRounds[r], per, where);
            g_WeaponStock[w] -= idiv_js(DS32(0x8FF30), per, where);
        }
        if (WEP_TYPE(w) == 5) {
            g_FuelBase += DS32(0x90788);
            g_LoadWeight += DS32(0x90788) / 4;
            g_RackRounds[r] = 1;
        }
        if (WEP_TYPE(w) == 4) {
            g_LoadWeight = g_LoadWeight - idiv_js(wt * g_RackRounds[r], per, where) + 100;
            g_RackRounds[r] = 10;
        }
    }
    g_MaxLoadWeight = (s32)((double)(g_RackMaxLoad[0] + g_RackMaxLoad[1]) / 1.5);
    g_Fuel = g_FuelBase + DS32(0x90648) * 1000;
    DS32(0x909C4) = g_GameMode < 3 ? DS32(0x903C0) + DS32(0x909E4) * 100 : 0;   /* g_GunAmmo */
}

/* 0x212ac Mission_Setup (game_flow.md §4, level.md §2) */
void Mission_Setup(void)
{
    Sound_StopAll();
    g_StoryShown = 0;
    if (g_CDMusicOn) CD_Stop();
    Weapons_ReturnUnused();
    if (g_GameMode == 3) g_PlaneSel = 0;
    if (g_PlaneSel == 0) g_MissionActive = 0;
    if (g_BriefedMission != g_Mission) {
        Mission_LoadBriefing();
        if (g_GameMode < 3) {
            g_RackWeapon[0] = g_MissionDefWeapons[0];
            g_RackWeapon[1] = g_MissionDefWeapons[1];
            DS32(0x8FEB4) = 1;
        }
    }
    if (g_MissionActive == 0) {
        PlaneSelect_Screen();
        DS32(0x90828) = -1;
        g_TilesetName[0] = 0;
        g_MissionActive = 1;
    }
    DS32(0x90654) = 0;                           /* g_FuelLeaks */
    DS32(0x907D8) = DS32(0x917D4);               /* g_MaxSinkAccel = s84 */
    DS32(0x907DC) = DS32(0x917D8);               /* g_WingAuthority = s85 */
    DS32(0x90670) = 1;                           /* g_DamageLampsDirty */
    g_Armour = DS32(0x91834) + g_ArmourBonus;    /* s108 */
    DS32(0x90168) = DS32(0x917E4);               /* g_TurnRate = s88 */
    DS32(0x905DC) = DS32(0x917E8);               /* g_FixedGear = s89 */
    DS32(0x8FF14) = DS32(0x917EC);               /* g_GearDown = s90 */
    DS32(0x9034C) = 0;
    DS32(0x906A0) = 8;                           /* g_FlaresPending */
    g_FuelBase = g_PlaneFuel;
    Mission_ResetState();
    if ((g_RackRounds[0] == 0 || g_RackRounds[1] != 0) && g_GameMode < 3) {   /* sic: "||" */
        if (g_RackRounds[0] == 0) g_RackWeapon[0] = g_MissionDefWeapons[2];
        if (g_RackRounds[1] == 0) g_RackWeapon[1] = g_MissionDefWeapons[3];
        Weapons_ReturnUnused();
        Mission_ResetState();
    }
    if (g_GameMode == 3) {
        if (g_MP_TargetX0 == 0 || 1999 < g_MP_TargetX0) {
            g_RackWeapon[0] = 0; g_RackWeapon[1] = 0; g_RackRounds[0] = 0; g_RackRounds[1] = 0;
        } else {
            g_RackWeapon[0] = g_MissionDefWeapons[3];
            g_RackRounds[0] = g_WeaponPerRack[g_MissionDefWeapons[3]];
            g_RackRounds[1] = g_WeaponPerRack[g_MissionDefWeapons[3]];
            g_RackWeapon[1] = g_MissionDefWeapons[3];
        }
    }
    if (g_MP_ConvoyCount != 0 && g_GameMode < 3) {
        Enemy_SetupSpriteIds();
        g_MP_ConvoyCount = 0;
    }
    if (strcmp(g_TilesetPending, g_TilesetName) != 0 && g_TilesetPending[0] != 0) {
        Tileset_Load(g_TilesetPending);
        strcpy(g_TilesetName, g_TilesetPending);
        DS32(0x906DC) = 0; DS32(0x90728) = 0; DS32(0x90750) = 0; DS32(0x90114) = 0; DS32(0x90158) = 0;
        DS32(0x90760) = 0; DS32(0x9071C) = 0;
    }
    Parallax_Load(g_TilesetName);
    if (strcmp(g_MapName, g_MapLoaded) == 0 || g_MapName[0] == 0) {
        strcpy(g_MapName, g_MapLoaded);
    } else {
        memset(g_MapGrid, 0, 4);
        strcpy(g_MapLoaded, g_MapName);
        DS32(0x90410) = 0;
    }
    if (g_MapGrid == NULL || (g_MapGrid[0] | g_MapGrid[1] | g_MapGrid[2] | g_MapGrid[3]) == 0) {
        DS32(0x904AC) = 0;
        if (g_MapVal == NULL) g_MapVal = calloc(1, 0x13b8);          /* PORT: zeroed (level.md Q18) */
        if (g_MapMp2 == NULL) g_MapMp2 = calloc(1, 1000);
        File_SetBufferCapacity((uintptr_t)g_MapVal, 0x13b8);
        strcpy(s_Tmp85048, g_MapName);
        strcat(s_Tmp85048, DSTR(0x8112C));                           /* "1.val" */
        File_LoadWhole(DSTR(0x81132), s_Tmp85048, (void **)&g_MapVal, 0);
        for (int ext = 0; ext < 2; ext++) {
            strcpy(s_Tmp85048, g_MapName);
            g_LoopI = (s32)strlen(s_Tmp85048);
            if (6 < g_LoopI) g_LoopI = 6;
            s_Tmp85048[g_LoopI] = (char)(g_MapVariant / 10 + '0');
            s_Tmp85048[g_LoopI + 1] = (char)(g_MapVariant % 10 + '0');
            s_Tmp85048[g_LoopI + 2] = 0;
            if (ext == 0) {
                strcat(s_Tmp85048, DSTR(0x81137));                   /* ".mp2" */
                File_LoadWhole(DSTR(0x81132), s_Tmp85048, (void **)&g_MapMp2, 0);
            } else {
                strcat(s_Tmp85048, DSTR(0x8113C));                   /* ".mxp" */
                Map_LoadMxp();
            }
        }
        /* runway / carrier search (level.md §2.4) */
        DS32(0x8FEA0) = 0x3f;
        g_SkyTile = (s32)(Byte_Get(g_MapGrid, 5) & 0xff);
        g_BaseStartX = 0;
        g_BaseEndX = 0;
        g_BaseIsCarrier = 0;
        while (g_BaseStartX == 0) {
            if (DS32(0x8FEA0) < -0x40)
                FatalError(g_MapName, ": no runway (attribute 0x81) in the map (the original loops forever)", 6);   /* PORT */
            for (g_LoopI = g_MapWidth - 1; g_LoopI > -1; g_LoopI--)
                if (Map_GetTileAttr(g_LoopI, DS32(0x8FEA0), 0) == 0x81) {
                    g_RunwayFill = Map_GetTile(g_LoopI - 1, DS32(0x8FEA0));
                    g_BaseEndX = g_LoopI * 0x10;
                    g_CamX = g_BaseEndX - 0xa0;
                    g_BaseStartX = g_LoopI;
                    g_LoopI = -1;
                    g_BaseYOff = (0x3f - DS32(0x8FEA0)) * 0x10;
                }
            DS32(0x8FEA0)--;
        }
        if (Map_GetTileAttr(Div16(g_BaseEndX), 0x3f, 0) == 0x82) g_BaseIsCarrier = 1;
        for (g_LoopI = g_BaseStartX; g_LoopI > -1; g_LoopI--)
            if (Map_GetTileAttr(g_LoopI, 0x3f - Div16(g_BaseYOff), 0) != 0x81) {
                g_BaseStartX = g_LoopI << 4;
                g_LoopI = -1;
            }
    }
    Pal_Fade(0, 0x100, 0, 0x20);
    strcpy(s_Tmp85048, g_MapName);
    strcat(s_Tmp85048, DSTR(0x8112C));                               /* "1.val": the attribute tables only */
    File_LoadWhole(DSTR(0x81132), s_Tmp85048, (void **)&g_MapVal, 0);
    Map_ResetCounters();
    Hud_DrawPanel();
    Player_BuildSpeedCaps();
    g_StallTopY = 0x340 - DS32(0x90064);
    g_StallLimit = 0x40;
    g_Throttle = 0;
    g_CamX = g_BaseEndX - 0x140;
    if (g_IsHeli != 0) g_Throttle = 9;
    g_CamY = g_StallTopY;
    for (g_LoopI = Div16(g_BaseStartX) + 1; g_LoopI <= Div16(g_BaseEndX) - 1; g_LoopI++)   /* re-pave the runway */
        if (Map_GetTileAttr(g_LoopI, 0x3f - Div16(g_BaseYOff), 0) != 0x81)
            Map_SetTile(g_LoopI, 0x3f - Div16(g_BaseYOff), (u8)g_RunwayFill);
    DS32(0x906A4) = 0;
    g_PlayerScrX = 0xa0;
    g_PlayerVY = 0;
    g_PlayerVX = 0;
    g_TargetVY = 0;
    g_LiftVY = 0;
    g_Speed = 0;
    g_Dir = 0;
    g_DirHalf = 0;
    g_GearDown = 1;
    g_PlayerScrY = (0x9f - g_GearHeight) - g_BaseYOff;
    g_StallSink = 0;
    g_DeathTimer = 0;
    g_Crashed = 0;
    g_EjectState = 0;
    g_ProjCount = 0;
    DS32(0x90344) = 0;
    g_OnGround = 1;
    g_EngineFire = 0;
    g_TrigCamCol = -1;
    DS32(0x90974) = 0;
    g_CrashAttr = 0;
    g_CrashDir = 0;
    if (g_IsHeli == 0) {
        Player_BuildPlaneTables(0);
        g_Dir = 0;
        g_DirHalf = 0;
    } else {
        Player_BuildHeliTables();
        g_Dir = 6;
        g_DirHalf = 3;
        g_HeliLift = 5;
        g_HeliVX = 0;
    }
    /* FUN_00015128: empty */
    for (g_LoopI = 0; g_LoopI < 0x10; g_LoopI++)
        for (g_LoopJ = 0; g_LoopJ < 0x20; g_LoopJ++) {}
    DS32(0x905D4) = g_BaseEndX;
    DS32(0x901FC) = g_BaseEndX;
    DS32(0x9082C) = g_BaseEndX;
    DS32(0x90668) = -1;
    DS32(0x90268) = 0;
    g_LockTarget = -1;
    DS32(0x90764) = 0;
    DS32(0x8FFCC) = -1;
    if (g_ReverseThrust != 0) g_ReverseThrust = 1;
    g_DamageLampsDirty = 1;
    for (g_LoopI = 0; g_LoopI < 0xe; g_LoopI++) g_DamageFlags[g_LoopI] = 0;
    g_FuelLeaks = 0;
    g_PrevCamX = g_CamX;
    DS32(0x90628) = 0;
    g_MissionActive = 1;
    if ((g_FogPending || g_MP_Weather == 2 || g_SpecialHit == 1) && g_NightMission == 0 && g_GameMode < 3)
        g_FogActive = 1;
    DS32(0x9049C) = 0;
    g_HookDown = 0;
    g_MapWidthPx = g_MapWidth << 4;
    DS32(0x904F8) = 0;
    g_ChuteFail = 0;
    DS32(0x90664) = 0;
    g_PickupX = (s32)g_MP_PickupCol << 4;
    DS32(0x8FED0) = 0;
    DS32(0x9046C) = 1;
}

/* ================================================================ briefing (§5) */

/* Word wrap of the briefing / end-game screens: n = Text_FitWidth; the line keeps chars 0..n. */
static int wrap_line(char *src, char *line, int maxw, bool width_check, int nul_at_n)
{
    DS32(0x8FF44) = Text_FitWidth(src, maxw);
    int n = DS32(0x8FF44);
    if (n < 1 || (width_check && Text_WidthSmall(src) < maxw)) {
        strcpy(line, src);
        src[0] = 0;
    } else {
        strncpy(line, src, (size_t)n + 1);
        line[n + (nul_at_n ? 0 : 1)] = 0;
        memmove(src, src + n + 1, strlen(src + n + 1) + 1);
    }
    return n;
}

/* 0x2299a Mission_LoadBriefing */
void Mission_LoadBriefing(void)
{
    char map[0x14], tileset[0x14], story[0x14];
    u8 defw[10];
    DS32(0x902C0) = 0; DS32(0x90190) = 0; DS32(0x90194) = 0; DS32(0x90198) = 0; DS32(0x9018C) = 0;
    DS32(0x9019C) = 0; DS32(0x9014C) = 0; DS32(0x90144) = 0;
    for (g_LoopI = 0; g_LoopI < 0x28; g_LoopI++) g_PlaneFlown[g_LoopI] = 0;
    DS32(0x908D8) = 0; DS32(0x908E8) = 0; DS32(0x902E0) = 0; DS32(0x8FFB8) = 0;
    DS32(0x900A4) = 1;
    g_AgentDropPending = 0;
    DS32(0x90948) = 0; DS32(0x90A40) = 0; DS32(0x90A44) = 0; DS32(0x90A10) = 0; DS32(0x90918) = 0;
    char *path = DSTR(0x90368);
    strcpy(path, DSTR(0x81141));                                  /* "data/" */
    path[5] = 'M';
    path[6] = (char)(g_GameMode + '0');
    path[7] = 0;
    FILE *f = Platform_Fopen(path, DSTR(0x81147));
    if (!f) FatalError(path, DSTR(0x8114A), 1);
    fseek(f, g_Mission * 0x1c2, SEEK_SET);
    memset(map, 0, sizeof map); memset(tileset, 0, sizeof tileset); memset(story, 0, sizeof story);
    memset(defw, 0, sizeof defw);
    if (fread(g_BriefingText, 0x140, 1, f) != 1) { /* not checked */ }
    D8(0x92AD1) = 0;
    if (fread(map, 0x14, 1, f) != 1) { /* not checked */ }
    if (fread(tileset, 0x14, 1, f) != 1) { /* not checked */ }
    if (fread(g_MissionParams, 0x3c, 1, f) != 1) { /* not checked */ }
    for (g_LoopI = 0; g_LoopI < 0x1e; g_LoopI++)
        g_MissionParams[g_LoopI] = (u16)((g_MissionParams[g_LoopI] >> 8) + g_MissionParams[g_LoopI] * 0x100);
    if (fread(story, 0x14, 1, f) != 1) { /* not checked */ }
    story[0x13] = 0;
    if (fread(defw, 10, 1, f) != 1) { /* not checked */ }
    fclose(f);
    map[0x13] = 0;                               /* PORT: the original's stack buffers are not terminated; */
    tileset[0x13] = 0;                           /* Str_TrimRight(..., 0x13) below terminates them anyway */
    strcpy(g_MapName, map);
    Str_TrimRight(g_MapName, 0x13);
    Str_TrimRight(tileset, 0x13);
    if (tileset[0] != 0) {
        strcpy(g_TilesetPending, tileset);
        Str_TrimRight(g_TilesetPending, 0x13);
        replace_ext(g_TilesetPending, DSTR(0x81156));             /* ".tlx" over the record's extension */
    }
    Str_TrimRight(story, 0x13);
    g_MissionBonus = (g_Mission + 1) * 1000;
    if (g_GameMode == 3) g_MissionBonus = 32000;
    DS32(0x90148) = g_MP_Misc29 / 1000;
    g_MP_Misc29 = g_MP_Misc29 % 1000;
    DS32(0x905C0) = 0;                           /* g_EscortMode */
    DS32(0x909D8) = 0;                           /* g_EscortNeeded */
    g_FinalMission = g_MissionParams[0] / 100;
    int train = 999 < g_MP_ConvoyCount;
    if (train) g_MP_ConvoyCount = g_MP_ConvoyCount % 1000;
    DS32(0x8FFE0) = train;                       /* g_ConvoyTrain */
    if (1000 < g_MP_ConvoyKills) {
        DS32(0x905C0) = 1;
        u32 k = (u32)g_MP_ConvoyKills % 1000;
        DS32(0x909D8) = (s32)(k < (u32)g_MP_ConvoyCount ? k : (u32)g_MP_ConvoyCount);
        g_MP_ConvoyKills = 0;
    }
    if (g_MP_Misc29 != 0) { DS32(0x8DEB0) = g_MP_Misc29; DS32(0x8DEB4) = g_MP_Misc29; }
    if (999 < g_MP_ConvoyCol) {
        g_EnemyGroundCount = g_MP_ConvoyCol / 1000;
        for (g_LoopI = 0; g_LoopI < g_EnemyGroundCount; g_LoopI++) {
            DS32A(0x8D9D8)[g_LoopI] = g_LoopI * 0x40 + g_MP_TargetX0 * 8;
            u32 h = Byte_Get(g_MapVal, DS32A(0x8D9D8)[g_LoopI] / 16 + 0x400);
            DS32A(0x8D9E8)[g_LoopI] = (s32)(h & 0xff) * 0x10 - 0x20;
            DS32A(0x8D9C8)[g_LoopI] = 3;
            DS32A(0x8D8F8)[g_LoopI] = -4;
            DS32A(0x8D8E8)[g_LoopI] = 8;
            DS32A(0x8D9B8)[g_LoopI] = 4;
            DS32A(0x8D7D8)[g_LoopI] = 100;
            DS32A(0x8D9A8)[g_LoopI] = 4;
            DS32A(0x8D998)[g_LoopI] = 0;
        }
        g_MP_ConvoyCol = g_MP_ConvoyCol % 1000;
    }
    for (g_LoopI = 0; g_LoopI < 4; g_LoopI++) g_MissionDefWeapons[g_LoopI] = defw[g_LoopI * 2] * 0x100 + defw[g_LoopI * 2 + 1];
    g_NightMission = (g_MP_Weather == 1);
    if (g_GameMode < 3) {
        if (g_MP_EnemyBaseCol != 0 && g_MP_EnemyBaseCol < 2000) {
            g_EnemyBaseX = g_MP_EnemyBaseCol;
            g_EnemyBaseRow = g_MP_EnemyBaseRow;
            if (0x3f < g_EnemyBaseRow) g_EnemyBaseRow = g_EnemyBaseRow >> 4;
            DS32(0x90410) = 0;
        }
        if (g_MP_EnemyAir != 0) g_EnemyAirCount = 0;
    } else {
        for (g_LoopI = 0; g_LoopI < 4; g_LoopI++) {
            g_AeroGateX[g_LoopI] = g_MP_ConvoyTypesA[g_LoopI * 2] << 4;
            g_AeroGateY[g_LoopI] = g_MP_ConvoyTypesB[g_LoopI * 2] << 4;
        }
        for (g_LoopI = 0xf; g_LoopI < 0x17; g_LoopI++) g_MissionParams[g_LoopI] = 0;
        if (0 < g_AeroGateX[0]) g_MP_ConvoyCount = 1;
        g_AeroGateIdx = 0;
    }
    DS32(0x905AC) = 0;                           /* g_ConvoyCount */
    DS32(0x90A30) = g_MP_PickupSprite;
    DS32(0x90A38) = (DS32(0x90A30) == 0xca || DS32(0x90A30) == 0xac);
    g_EnemySetIndex = g_MP_EnemyAir / 100 + 1;
    if (99 < g_MP_EnemyAir && g_GameMode < 3) g_MP_EnemyAir = g_MP_EnemyAir % 100;
    if (g_EnemySetIndex != g_EnemySetLoaded) {
        f = Platform_Fopen(DSTR(0x8115B), DSTR(0x81147));         /* data/enemies */
        if (!f) FatalError(DSTR(0x81168), DSTR(0x8114A), 1);
        u8 rec[7] = { 0 };
        fseek(f, g_EnemySetIndex * 7, SEEK_SET);
        if (fread(rec, 7, 1, f) != 1) { /* not checked */ }
        fclose(f);
        DS32(0x906E0) = rec[0];                  /* g_EnemySkill */
        DS32(0x906D8) = rec[1];                  /* g_EnemyGunDamage */
        DS32(0x906F8) = rec[2];                  /* g_EnemyMissiles */
        DS32(0x906B8) = rec[3];                  /* g_EnemyBombs */
        DS32(0x906F0) = rec[4];                  /* g_EnemyBombWeapon */
        DS32(0x906CC) = rec[5];                  /* g_EnemyBombRef */
        g_EnemySpxLetter = rec[6];
        if (g_EnemySpxLetter != g_EnemySpxLoaded) {
            Enemy_LoadSpx();
            g_EnemySpxLoaded = g_EnemySpxLetter;
        }
        g_EnemySetLoaded = g_EnemySetIndex;
    }
    if (g_MissionParams[0] == 2) g_Lives = 0;   /* sic */
    for (int k = 0; k < 2; k++) {
        size_t n = strlen(g_MapName);
        u8 c = (u8)g_MapName[(ptrdiff_t)n - 1];
        if (0x2f < c && c < 0x3a) {
            g_MapVariant = c - 0x30;
            g_MapName[(ptrdiff_t)n - 1] = 0;
        }
        if (k == 0) g_LoopI = (s32)n;
    }
    for (g_LoopI = 0; g_LoopI < 0xe; g_LoopI++) DS32A(0x8FA20)[g_LoopI] = 0;   /* g_DamageFlags */
    DS32(0x90654) = 0;
    DS32(0x90168) = DS32(0x917E4);
    DS32(0x907D8) = DS32(0x917D4);
    DS32(0x907DC) = DS32(0x917D8);
    DS32(0x90670) = 1;
    g_Armour = DS32(0x91834) + g_ArmourBonus;
    DS32(0x905DC) = DS32(0x917E8);
    DS32(0x8FF14) = DS32(0x917EC);
    Pal_Fade(0, 0x100, 0, 0x20);
    Video_SetStartAndPan(0, 0, 0);
    if (story[0] != 0 && g_AeroPlayer == 0) {
        strcpy(g_StoryName, story);
        Story_ShowAsc();
    }
    Pic_LoadPax(DSTR(0x81170), 0, 0);                             /* jetlogo.pax */
    Pal_Fade(0, 0x100, 1, 0x20);
    DS32(0x90300) = 1;
    g_HudMsgCount = 0;
    g_PickupY = 0x3e0;
    if (g_StoryShown != 0 && g_Mission != 0 && g_GameMode == 0) {
        g_LoopI = g_Mission;
        memcpy(g_InfoText, DSTR(0x8117C), 36);                    /* "Press f1 to f10 to save the game..." */
        Text_DrawSmall(0xa0 - Text_WidthSmall(g_InfoText) / 2, 0x8c, g_InfoText, 0);
        g_EnemyGroundCount = 0;                  /* sic (Q19) */
    }
    strcpy(g_InfoText, g_BriefingText);
    Str_TrimRight(g_InfoText, 0x13f);
    Str_TrimRight(g_BriefingText, 0x13f);
    DS32(0x90434) = 2;
    while (g_InfoText[0] != 0) {
        wrap_line(g_InfoText, s_Line85248, 0x130, false, 0);
        Text_DrawSmall(8, DS32(0x90434) * 8 + 0x19, s_Line85248, 0);
        DS32(0x90434)++;
    }
    g_FogActive = 0;
    DS32(0x90434)++;
    strcpy(s_Tmp85048, HUDTEXT(79));                              /* "MISSION BRIEFING: MISSION" */
    strcat(s_Tmp85048, DSTR(0x811A0));                            /* "   " */
    if (g_GameMode < 3) {
        itoa_js(g_Mission + 1, s_Tmp85048 + strlen(s_Tmp85048));
    } else {
        strcpy(s_Tmp85048, DSTR(0x811A4));                        /* "player " */
        itoa_js(g_AeroPlayer + 1, DSTR(0x8504F));
        strcat(s_Tmp85048, DSTR(0x811AC));                        /* "  -  " */
        strcat(s_Tmp85048, HUDTEXT(79));
        strcat(s_Tmp85048, DSTR(0x811B2));                        /* "  " */
        itoa_js(g_AeroRound + 1, s_Tmp85048 + strlen(s_Tmp85048));
    }
    Text_DrawSmall(0xa0 - Text_WidthSmall(s_Tmp85048) / 2, 0x1c, s_Tmp85048, 0);
    for (DS32(0x90470) = 0x9e - Text_WidthSmall(s_Tmp85048) / 2; DS32(0x90470) < Text_WidthSmall(s_Tmp85048) / 2 + 0xa2; DS32(0x90470)++)
        Video_PutPixel((u32)DS32(0x90470), 0x24, 2);
    strcpy(s_Tmp85048, HUDTEXT(80));                              /* "MISSION BONUS" */
    strcat(s_Tmp85048, DSTR(0x811B2));
    itoa_js(g_MissionBonus, s_Num85548);
    strcat(s_Tmp85048, s_Num85548);
    strcat(s_Tmp85048, DSTR(0x811B5));                            /* " " */
    strcat(s_Tmp85048, HUDTEXT(81));                              /* "POINTS" */
    Text_DrawSmall(0xa0 - Text_WidthSmall(s_Tmp85048) / 2, 100, s_Tmp85048, 0);
    DS32(0x90434)++;
    int r = Rand(0x14);
    if ((r == 1 || g_MP_Weather == 2 || g_SpecialHit == 1) && g_NightMission == 0 && g_GameMode < 3) g_FogActive = 1;
    if (g_FogActive != 0) {
        strcpy(s_Tmp85048, HUDTEXT(82));                          /* "FOG WARNING !" */
        g_FogPending = 1;
        g_FogActive = 0;
        Text_DrawSmall(0xa0 - Text_WidthSmall(s_Tmp85048) / 2, 0x78, s_Tmp85048, 0);
    }
    DS32(0x90470) = 5;
    WaitKey_Press();
    for (DS32(0x90470) = 0x3b; DS32(0x90470) < 0x45 && g_KeyDown[DS32(0x90470)] == 0; DS32(0x90470)++) {}
    if (DS32(0x90470) == 0x45) DS32(0x90470) = 0;
    else DS32(0x90470) -= 0x3a;
    if (g_StoryShown != 0 && g_Mission != 0 && g_GameMode == 0 && DS32(0x90470) != 0) SaveGame_Write((char)DS32(0x90470));
    if (g_MissionActive == 0) Pal_Fade(0, 0x100, 0, 0x20);
    u16 x0 = g_MP_TargetX0;
    for (g_LoopI = 0; g_LoopI < g_WeaponCount; g_LoopI++)
        if (g_WeaponStock[g_LoopI] < 999) {
            g_WeaponResupplyFrac[g_LoopI] += g_WeaponResupply[g_LoopI];
            g_WeaponStock[g_LoopI] += g_WeaponResupplyFrac[g_LoopI] / 10;
            g_WeaponResupplyFrac[g_LoopI] %= 10;
        }
    g_BriefedMission = g_Mission;
    g_MissionResult = 0;
    if (2000 < g_MP_TargetX0 && g_MP_TargetX0 < 5000) {
        g_MP_TargetX0 = (u16)(g_MP_TargetX0 - 2000);
        if (g_MP_TargetX1 < g_MP_TargetX0) g_MP_TargetX1 = (u16)(x0 - 0x7c8);
        g_AgentDropPending = 1;
    }
    if (strcmp(g_MapName, g_MapLoaded) != 0 && g_MapName[0] != 0) {
        DS32(0x90158) = 0; DS32(0x9037C) = 0; DS32(0x90998) = 0; DS32(0x90114) = 0;
        if (g_MapVal == NULL) g_MapVal = calloc(1, 0x13b8);
        if (g_MapMp2 == NULL) g_MapMp2 = calloc(1, 2000);
        File_SetBufferCapacity((uintptr_t)g_MapVal, 0x13b8);
        File_SetBufferCapacity((uintptr_t)g_MapMp2, 2000);
        strcpy(s_Tmp85048, g_MapName);
        strcat(s_Tmp85048, DSTR(0x8112C));                        /* "1.val" */
        File_LoadWhole(DSTR(0x81132), s_Tmp85048, (void **)&g_MapVal, 0);
        for (int ext = 0; ext < 2; ext++) {
            strcpy(s_Tmp85048, g_MapName);
            g_LoopI = (s32)strlen(s_Tmp85048);
            if (6 < g_LoopI) g_LoopI = 6;
            s_Tmp85048[g_LoopI] = (char)(g_MapVariant / 10 + '0');
            s_Tmp85048[g_LoopI + 1] = (char)(g_MapVariant % 10 + '0');
            s_Tmp85048[g_LoopI + 2] = 0;
            if (ext == 0) {
                strcat(s_Tmp85048, DSTR(0x81137));               /* ".mp2" */
                File_LoadWhole(DSTR(0x81132), s_Tmp85048, (void **)&g_MapMp2, 0);
            } else {
                strcat(s_Tmp85048, DSTR(0x8113C));               /* ".mxp" */
                Map_LoadMxp();
            }
        }
        Map_ResetCounters();
    }
    if ((g_MP_EnemyAir != 0 && g_GameMode < 3) || (g_MP_TargetX0 != 0 && g_GameMode == 3)) {
        if (g_GameMode < 3) {
            DS32(0x90A5C) = g_MP_EnemyAir < 3 ? g_MP_EnemyAir : 2;
            EnemyBomber_Spawn();
        } else {
            DS32(0x90A5C) = 0;
            g_EnemyAirCount = 0;
            if (g_AgentDropPending == 0) {
                for (g_LoopI = g_MP_TargetX0 - 3; g_LoopI <= g_MP_TargetX0 + 3; g_LoopI++) Map_SetTile(g_MP_TargetsReq, g_LoopI, 0x3e);
                for (g_LoopI = g_MP_TargetX0 - 5; g_LoopI <= g_MP_TargetX0 + 5; g_LoopI++) Map_SetTile(g_MP_EnemyAir, g_LoopI, 0x3f);
                g_MP_TargetsReq = 0;
                g_MP_EnemyAir = 0;
            }
        }
    }
    if (g_GameMode == 3) {
        if (g_MP_TargetX1 != 0) {
            DS32(0x90948) = g_MP_TargetX1 << 4;                  /* g_CrateX */
            DS32(0x9094C) = (s16)g_MP_AirKills << 4;             /* g_CrateY */
            if (g_MP_AirKills == 0) DS32(0x9094C) = -0x400;
            DS32(0x9091C) = g_MP_ConvoyKills;
            DS32(0x90940) = g_MP_PickupSprite;
            g_MP_PickupSprite = 0;
        }
        if (g_MP_TargetX0 != 0) {
            g_MP_TargetX1 = (u16)(g_MP_TargetX0 + 5 + (s16)g_AgentDropPending * 5);
            g_MP_TargetX0 = (u16)((g_MP_TargetX0 - 5) + (s16)g_AgentDropPending * -5);
        }
    } else {
        DS32(0x902F4) = 0;                       /* g_TargetTilesInit */
        if (4999 < g_MP_TargetX0)
            for (g_LoopI = 0; g_LoopI < g_MapWidth; g_LoopI++)
                for (g_LoopJ = 0; g_LoopJ < (s32)g_MP_TargetX1; g_LoopJ++) {
                    u32 h = Byte_Get(g_MapVal, g_LoopI + 0xbd0);
                    DS32(0x909A8) = Map_FindTile((g_MP_TargetX0 - 5000) + g_LoopJ, g_LoopI, (s32)(h & 0xff) - 1, g_LoopI + 1, 0x40, 1);
                    if (-1 < DS32(0x909A8)) DS32(0x902F4)++;
                }
    }
    if (g_MP_Bertha != 0) Map_StampBertha();
    DS32(0x9046C) = 1;
    DS32(0x9038C) = 1;
    if (g_MP_PickupCol != 0) {
        g_PickupY = 0x3f;
        while (0x7e < Map_GetTileAttr(g_MP_PickupCol, g_PickupY, 0)) g_PickupY--;
        g_PickupY <<= 4;
    }
    if (500 < g_MP_PickupSprite) DS32(0x90A40) = g_MP_PickupCol - 1;
}

/* 0x23f6b Story_ShowAsc: data/<asc>: picture name, x, y, (unused), then the text, on page 1. */
void Story_ShowAsc(void)
{
    char path[0x20];
    g_StoryShown = 1;
    strcpy(path, DSTR(0x81141));                                  /* "data/" */
    strcat(path, g_StoryName);
    FILE *f = Platform_Fopen(path, DSTR(0x811B7));                /* "r" */
    if (!f) FatalError(g_StoryName, DSTR(0x8114A), 1);
    if (fscanf(f, DSTR(0x811B9), g_StoryName) != 1) { /* not checked */ }
    replace_ext(g_StoryName, DSTR(0x811BC));                      /* "COMBATCOMBI.ABK" -> "COMBATCOMBI.pax" */
    Pic_LoadPax(g_StoryName, 1, 0);
    scan_int(f, DSTR(0x811C1), &DS32(0x8FEDC));
    scan_int(f, DSTR(0x811C1), &DS32(0x8FEA0));
    scan_int(f, DSTR(0x811C1), &DS32(0x90540));
    DS32(0x90434) = DS32(0x8FEA0) + 8;
    s_Tmp85B48[0] = 0;
    while (!feof(f)) {
        fgets_js(s_Tmp85B48 + strlen(s_Tmp85B48), 0x50, f);
        g_LoopI = (s32)strlen(s_Tmp85B48);
    }
    Text_DrawSmall(DS32(0x8FEDC), (g_GameMode == 3) * 0x28 + DS32(0x90434) + 0xf0, s_Tmp85B48, 0);
    fclose(f);
    Video_ShowPage(1);
    Video_SetStartAndPan(0, g_GameMode == 3 ? 0xfa : 0xf0, 0);
    if (g_GameMode == 3) Pal_SetColorNoUpload(0xff, 0, 0, 0);
    else Pal_SetColorNoUpload(0xff, 0x3f, 0x3f, 0x3f);
    Pal_Fade(0, 0x100, 1, 0x20);
    DS32(0x90470) = 1;
    g_WaitCountInit = -500;
    WaitKey_Release();
    Pal_Fade(0, 0x100, 0, 0x20);
    Video_FillRect(0, 0, 0x140, 0x1e0, 0);
    Video_ShowPage(0);
    if (g_GameMode == 3) Pal_SetColor(0xff, 0x3f, 0x3f, 0x3f);
}

/* ================================================================ plane select (§6) */

/* 0x257c3 SelectScreen_LoadBg */
void SelectScreen_LoadBg(int wep)
{
    g_PicFullLoad = 1;
    Pic_LoadPax(wep == 0 ? DSTR(0x811CB) : DSTR(0x811DE), 0, 0);  /* planech.pax / wepch.pax */
    DS32(0x8FA18) = DS32(0x90308) * 0x40 + 0x100;
    DS32(0x90300) = 0;
    DS32(0x8F810) = 0x30;
    g_SelPage = 0;
}

/* 0x25869 Text_DrawCenteredAt(page, x, y, s): centred on x + 0x1d. */
void Text_DrawCenteredAt(int page, int x, int y, const char *s)
{
    Text_DrawSmall((x + 0x1d) - Text_WidthSmall(s) / 2, y, s, page);
}

/* 0x258b6 SelectScreen_DrawInfoBox(right) */
void SelectScreen_DrawInfoBox(int right)
{
    int col = 0x53;
    DS32(0x90434) = 3;
    if (g_GameMode == 3) { right = 1; col = 0x5a; }
    if (right == 0) Video_FillRect(0x13, 0x17, 299, 0x33, 0);
    else Video_FillRect(0x53, 0x17, 0x127, 0x33, 0);
    if (g_GameMode == 3) {
        strcpy(s_Tmp85048, DSTR(0x811E8));                        /* "player 1" */
        s_Tmp85048[7] = (char)(s_Tmp85048[7] + g_AeroPlayer);
        Text_DrawBig(0x14, 0x22, s_Tmp85048, 0);
    }
    while (g_InfoText[0] != 0) {
        DS32(0x8FF44) = Text_FitWidth(g_InfoText, g_InfoBoxW - 6);
        int n = DS32(0x8FF44);
        if (n < 1 || Text_WidthSmall(g_InfoText) <= g_InfoBoxW - 6) {
            strcpy(s_Line85248, g_InfoText);
            g_InfoText[0] = 0;
        } else {
            strncpy(s_Line85248, g_InfoText, (size_t)n + 1);
            s_Line85248[n] = 0;
            memmove(g_InfoText, g_InfoText + n + 1, strlen(g_InfoText + n + 1) + 1);
        }
        Text_DrawSmall(right == 0 ? 0x13 : col, DS32(0x90434) * 6 + 7, s_Line85248, 0);
        DS32(0x90434)++;
    }
}

/* 0x25a5d SelectScreen_ShowPlaneName */
void SelectScreen_ShowPlaneName(void)
{
    if (0 < g_PlaneCand && g_PlaneCand < 0x3d) {
        strcpy(g_InfoText, g_PlaneNames + (g_PlaneCand - 1) * 0x28);
        if (g_PlaneUsed[g_PlaneCand - 1] == 9999) strcpy(g_InfoText, HUDTEXT(77));   /* "STOLEN AIRCRAFT" */
        SelectScreen_DrawInfoBox(0);
    }
}

/* 0x25add PlaneRecord_Load: the aircraft record (0xdc bytes of DATA/MISC). */
void PlaneRecord_Load(void)
{
    u8 rec[0xdc];
    for (int i = 0; i < 0xdc; i++) rec[i] = g_MiscData[g_PlaneSel * 0xdc + i - 0xdc];
    strncpy(DSTR(0x8C3D8), (char *)rec, 0x14);
    D8(0x8C3EC) = 0;
    strncpy(DSTR(0x8C458), (char *)rec + 0x14, 0x14);
    D8(0x8C46C) = 0;
    strncpy(g_PlaneDesc, (char *)rec + 0x28, 0x78);
    D8(0x8C550) = 0;
    memcpy(DSTR(0x85848), rec + 0xa0, strnlen((char *)rec + 0xa0, 0x14) < 0x14 ? strnlen((char *)rec + 0xa0, 0x14) + 1 : 0x14);   /* strncpy */
    memcpy(DSTR(0x85748), DSTR(0x811F1), 21);                     /* "Mixamatosis is Fun !" */
    D8(0x8C558) = 0;
    for (g_LoopI = 0; g_LoopI < 0x15; g_LoopI++) {
        DS32(0x90830) = (s32)D8(0x85848 + g_LoopI) - 1;
        D8(0x8C558 + g_LoopI) = (u8)((u8)DS32(0x90830) ^ D8(0x85748 + g_LoopI));
    }
    D8(0x8C56C) = 0;
}

/* 0x1adae: empty, called once after the first plane selection. */
void Stub_PlaneSelectOnce(void) {}

/* 0x264eb Plane_ReplaceSprite */
void Plane_ReplaceSprite(void) { Sprites_ReplaceFromBank(g_LoopJ, g_LoopI, g_MapBuf); }

/* 0x26473 Plane_LoadSpx: plane/<name>.spx into g_MapBuf (name in 0x85b48). */
void Plane_LoadSpx(void)
{
    replace_ext(s_Tmp85B48, DSTR(0x8121D));                       /* "Hawk.Abk" -> "Hawk.spx" */
    int packed = File_LoadWhole(DSTR(0x81212), s_Tmp85B48, (void **)&g_PackBuf, 0);
    DS32(0x90434) = (s32)LZW_PackedSize(g_PackBuf);
    if (DS32(0x90434) > 150000)
        FatalError(s_Tmp85B48, ": bank larger than g_MapBuf (the original would overwrite memory)", 6);
    LZW_Unpack(g_PackBuf, (u32)packed, g_MapBuf);
}

/* 0x26256 Plane_SetupSprites: plane stats from MISC.Z, plane/<name>.hd (helicopters), the plane bank over
 * sprite slots 1..n. */
void Plane_SetupSprites(void)
{
    const u8 *z = g_MiscZData + (DS32(0x90474) - 1) * 300;
    for (g_LoopI = 0; g_LoopI < 0x79; g_LoopI++) {
        u32 v = (u32)z[g_LoopI * 2] * 0x100 + z[g_LoopI * 2 + 1];
        if (0x7fff < v && 0x4f < g_LoopI) v -= 0x10000;
        g_PlaneStats[g_LoopI] = (s32)v;
    }
    strcpy(g_StoryName, DSTR(0x8C458));
    strcpy(s_Tmp85B48, g_StoryName);
    Str_TrimRight(s_Tmp85B48, 0x13);
    char path[0x60];
    strcpy(path, DSTR(0x81212));                                  /* "plane/" */
    strcat(path, s_Tmp85B48);
    replace_ext(path, DSTR(0x81219));                             /* ".hd" over ".Abk" */
    FILE *f = Platform_Fopen(path, DSTR(0x81147));
    if (f) {
        if (g_HeliHD == NULL) g_HeliHD = calloc(1, 0x4000);
        if (fread(g_HeliHD, 0x640, 1, f) != 1) { /* not checked */ }
        u8 *q = g_HeliHD;
        for (int i = 0; i < 0x640; i += 4) {
            SwapByte(q, q + 3);
            SwapByte(q + 1, q + 2);
            q += 4;
        }
        fclose(f);
    }
    Plane_LoadSpx();
    int n = Sprites_CountInBank(g_MapBuf, DS32(0x90434));
    for (DS32(0x90830) = 0; DS32(0x90830) < n; DS32(0x90830)++) {
        g_LoopI = DS32(0x90830) + 1;
        g_LoopJ = DS32(0x90830) + 1;
        Plane_ReplaceSprite();
    }
}

static int slot_x(void) { return ((g_CursorX - 0x24) / 0x47) * 0x47 + 0x13; }
static int slot_y(void) { return ((g_CursorY - 0x26) / 0x20) * 0x20 + 0x35; }

static float f32_div(s32 v, double d) { return (float)((double)v / d); }

/* 0x245a9 PlaneSelect_Screen */
int PlaneSelect_Screen(void)
{
    if (g_CDMusicOn) CD_Stop();
    DS32(0x907E4) = 0;
    D8(0x85948) = 0;
    SelectScreen_LoadBg(0);
    DS32(0x900C0) = 6;
    g_InfoBoxW = 0x114;
    if (g_GameMode == 3) { DS32(0x900C0) = 0x56; g_InfoBoxW = 0xc4; }
    DS32(0x90350)++;
    DS32(0x8FF20) = 0;                           /* g_ParkAttitude */
    DS32(0x90644) = 0;                           /* g_Seaplane */
    DS32(0x902B0) = 0;
    g_PlaneSel = 0;
    g_SelPageSize = 0x14;
    g_SelPage = g_SelPage < 2 ? g_SelPage : 1;
    g_CursorX = 0x24;
    g_CursorY = 0x26;
    Video_SetStartAndPan(0, 0, 0);
    Pal_Upload(0xff, 0x100);
    bool faded = false;
    while (g_PlaneSel == 0) {
        DS32(0x901B4) = 0;
        g_PicFullLoad = 0;
        g_FadeTargetColor = 6;
        Pal_Fade(0x40, 0x80, 0, 0x10);
        g_FadeTargetColor = 0;
        Pic_LoadPax(DSTR(0x811CB), 0, 0);
        if (!faded) { Pal_Fade(0, 0x80, 1, 0x20); faded = true; }
        Zone_Clear();
        g_PicFullLoad = 1;
        for (g_LoopI = 0; g_LoopI < 5; g_LoopI++)
            for (g_LoopJ = 0; g_LoopJ < 4; g_LoopJ++) {
                s32 r = g_LoopI, c = g_LoopJ;
                DS32(0x9028C) = c + r * 4 + g_SelPage * g_SelPageSize;
                s32 k = DS32(0x9028C);
                Zone_Add(r * 4 + 1 + c, c * 0x46 + 4, r * 0x20 + 0x21, c * 0x46 + 0x45, r * 0x20 + 0x3e);
                if (k < (g_PlaneCount < 0x3d ? g_PlaneCount : 0x3c)) {
                    if (g_PlaneUsed[k] < 9999 && ((g_GameMode & 1) == 0 || k < 4)) {
                        if (g_GameMode == 3) {
                            if (c == 3) {
                                if (g_AeroUsedRacks[g_AeroPlayer] == 0 && 0 < g_Mission) Sprite_DrawNow(0x100, (u32)(r * 0x20 + 0x39), 0x207);
                                else Sprite_DrawNow(0xf8, (u32)(r * 0x20 + 0x39), g_MissionDefWeapons[2] + DS32(0x90A2C) - 1);
                            } else {
                                Sprite_DrawNow((u32)(c * 0x47 + 0x23), (u32)(r * 0x20 + 0x39), g_MissionDefWeapons[r * 4 + c] + DS32(0x90A2C) - 1);
                            }
                        } else {
                            Sprite_DrawNow((u32)(c * 0x47 + 0x23), (u32)(r * 0x20 + 0x39), k + DS32(0x90A2C));
                        }
                        s32 left = g_PlaneLimit[k] - g_PlaneUsed[k];
                        if (g_PlaneLimit[k] == g_PlaneUsed[k] || left < 0) {
                            Text_DrawCenteredAt(0, c * 0x47 + 0x18, r * 0x20 + 0x49, HUDTEXT(72));   /* TRASHED */
                        } else if (left < 200) {
                            itoa_js(left, s_Tmp85B48);
                            Text_DrawCenteredAt(0, c * 0x47 + 0x18, r * 0x20 + 0x49, s_Tmp85B48);
                        } else {
                            Text_DrawCenteredAt(0, c * 0x47 + 0x18, r * 0x20 + 0x49, HUDTEXT(87));   /* LOTS ! */
                        }
                    } else if (9998 < g_PlaneUsed[k]) {
                        Text_DrawCenteredAt(0, c * 0x47 + 0x18, r * 0x20 + 0x49, DSTR(0x811B5));
                    }
                }
            }
        if (faded) {
            g_FadeTargetColor = 6;
            Pal_Fade(0x40, 0x80, 1, 0x10);
            g_FadeTargetColor = 0;
        }
        if ((g_GameMode & 1) == 0) Zone_Add(0x17, 0x90, 0xc0, 0x119, 0xcd);
        else { g_InfoText[0] = 0; SelectScreen_DrawInfoBox(0); }
        int hl = -1;
        while (DS32(0x901B4) == 0) {
            int z = Zone_HitTest(g_CursorX, g_CursorY);
            if (0 < z && Zone_HitTest(g_CursorX, g_CursorY) < 0x15) {
                Video_CopyRect(0, slot_x(), slot_y(), slot_x() + 0x42, slot_y() + 0x12, 0, 0, 0xf0);
                hl = 0;
                Sprite_DrawNow((u32)slot_x(), (u32)slot_y(), 0x1c5);
            }
            if (Zone_HitTest(g_CursorX, g_CursorY) == 0x17) {
                int h = (int)Sprite_GetHeight(0x1cb), w = (int)Sprite_GetWidth(0x1cb);
                Video_CopyRect(0, 0xe4, 0xd4, w + 0xe4, h + 0xd4, 0, 0, 0xf0);
                hl = 1;
                Sprite_DrawNow(0xe4, 0xd4, 0x1cb);
            }
            if (0 < g_ZoneHit && g_ZoneHit < 0x15) g_PlaneCand = g_ZoneHit + g_SelPage * g_SelPageSize;
            g_PlaneCand = Clamp(g_PlaneCand, 0, 0x3d);
            if (g_GameMode == 3) {
                s32 s = g_PlaneCand > 4 ? 4 : g_PlaneCand;
                g_PlaneCand = s;
                DS32(0x90170) = 0;
                g_AeroPlaneSlot = s;
                if (s == 4) {
                    if (strcmp(g_PlaneNames + (g_MissionDefWeapons[2] - 1) * 0x28, DSTR(0x811D7)) == 0   /* "Glider" */
                        || g_AeroUsedRacks[g_AeroPlayer] != 0 || g_Mission < 1) {
                        g_PlaneCand = g_MissionDefWeapons[2];
                        g_AeroPlaneSlot = 3;
                    } else {
                        g_PlaneCand = Rand(g_PlaneCount - 1) + 1;
                    }
                } else if (0 < s && s < 4) {
                    g_PlaneCand = g_MissionDefWeapons[s - 1];
                }
            }
            g_PlaneCand = Clamp(g_PlaneCand, 0, 0x3c);
            if (g_PlaneCand != DS32(0x902B0) && (g_GameMode != 1 || g_PlaneCand < 5)) {
                SelectScreen_ShowPlaneName();
                DS32(0x902B0) = g_PlaneCand;
            }
            Input_PollMenu();
            if (hl == 0) {
                Video_CopyRect(0, 0, 0xf0, 0x42, 0x102, 0, slot_x(), slot_y());
                hl = -1;
            } else if (hl == 1) {
                int h = (int)Sprite_GetHeight(0x1cb), w = (int)Sprite_GetWidth(0x1cb);
                Video_CopyRect(0, 0, 0xf0, w, h + 0xf0, 0, 0xe4, 0xd4);
                hl = -1;
            }
            if (Zone_HitTest(g_CursorX, g_CursorY) == 0x17) {
                if (g_MenuDY == -1) g_CursorY = 0xa6;
            } else {
                int nx = g_MenuDX * 0x48 + g_CursorX < 0xfd ? g_CursorX + g_MenuDX * 0x48 : 0xfc;
                g_CursorX = nx < 0x26 ? 0x26 : nx;
                g_MenuDY = g_CursorY + (g_GameMode != 3) * g_MenuDY * 0x20;   /* sic: g_MenuDY reused as the temp */
                g_CursorY = (g_GameMode & 1) == 0 ? 0x100 : 0x40;
                if (g_MenuDY < g_CursorY) g_CursorY = g_MenuDY;
                if (g_CursorY < 0x26) g_CursorY = 0x26;
            }
            if (0xbf < g_CursorY) {
                if ((g_GameMode & 1) == 0) { g_CursorY = 0xc0; g_CursorX = 0xfc; }
                else g_CursorY = 0x26;
            }
            g_ZoneHit = Zone_HitTest(g_CursorX, g_CursorY);
            g_FireOrConfirm = (g_Fire == 0 && g_KeyDown[0x39] == 0 && g_KeyDown[0x1c] == 0) ? 0 : 1;
            if (((g_ZoneHit == 0x17 && g_FireOrConfirm != 0) || g_Ctrl[1] != 0) && (g_GameMode & 1) == 0) {
                g_SelPage++;
                s32 m = g_PlaneCount - 1 < 0x3c ? g_PlaneCount - 1 : 0x3b;
                if (g_SelPage * g_SelPageSize - m != 0 && m <= g_SelPage * g_SelPageSize) g_SelPage = 0;
                DS32(0x901B4) = 1;
                while (g_Fire != 0 || g_KeyDown[0x39] != 0 || g_KeyDown[0x1c] != 0) Input_PollMenu();
            }
            if (g_Ctrl[0] != 0 && (g_GameMode & 1) == 0) {
                g_SelPage--;
                if (g_SelPage == -1) {
                    s32 m = g_PlaneCount - 1 < 0x3c ? g_PlaneCount - 1 : 0x3b;
                    g_SelPage = m / g_SelPageSize;
                }
                DS32(0x901B4) = 1;
                while (g_Ctrl[1] != 0) Input_PollMenu();   /* sic: waits on TurnR (Q7) */
            }
            if (0 < g_ZoneHit && g_ZoneHit < 0x15) g_PlaneCand = g_ZoneHit + g_SelPage * g_SelPageSize;
            if (g_GameMode == 3) {
                s32 s = g_PlaneCand < 4 ? g_PlaneCand : 3;
                g_PlaneCand = s;
                DS32(0x90170) = 0;
                g_AeroPlaneSlot = s;
                if (0 < s) g_PlaneCand = g_MissionDefWeapons[s - 1];
            }
            if (0x3f < g_AlienAbduct) {
                g_PlaneCand = g_AlienPlaneIdx + 1;
                DS32(0x90170) = 0;
                DS32(0x907E4) = 1;
            }
            if ((g_FireOrConfirm != 0 && 0 < g_PlaneCand
                 && (g_SelPage * g_SelPageSize + g_ZoneHit == g_PlaneCand || g_GameMode == 3))
                || 0x3f < g_AlienAbduct) {
                g_PlaneSel = g_PlaneCount + 1 < g_PlaneCand ? g_PlaneCount + 1 : g_PlaneCand;
                PlaneRecord_Load();
                if ((g_PlaneUsed[g_PlaneSel - 1] < g_PlaneLimit[g_PlaneSel - 1] && (g_GameMode != 1 || g_PlaneSel < 5))
                    || DS32(0x907E4) != 0) {
                    DS32(0x901B4) = 1;
                    if (0x3f < g_AlienAbduct) g_AlienAbduct = -1;
                    strcpy(g_InfoText, g_PlaneDesc);
                    SelectScreen_DrawInfoBox(0);
                } else {
                    strcpy(g_InfoText, g_PlaneUsed[g_PlaneSel - 1] == 9999 ? HUDTEXT(73) : HUDTEXT(74));
                    SelectScreen_DrawInfoBox(0);
                    g_PlaneSel = 0;
                }
            }
        }
    }
    if (DS32(0x9012C) == 0) {
        Stub_PlaneSelectOnce();
        DS32(0x9012C) = 1;
    }
    DS32(0x90474) = g_PlaneSel;
    DS32(0x90064) = 0;
    Plane_SetupSprites();
    DS32(0x909F8) = g_PlaneSel;
    DS32(0x90594) = 0;                                            /* g_IsGlider */
    s32 *st = g_PlaneStats;
    DS32(0x90060) = st[62]; DS32(0x90258) = st[63]; DS32(0x90238) = 0; DS32(0x90148) = st[64];
    DS32(0x90838) = st[65]; DS32(0x90358) = st[66]; DS32(0x9008C) = st[67]; DS32(0x90508) = st[68];
    DS32(0x905A4) = st[69]; DS32(0x90560) = st[70]; DS32(0x90564) = st[71]; DS32(0x90850) = st[72];
    DS32(0x90188) = st[73]; DS32(0x90450) = st[74]; DS32(0x8FF9C) = st[75]; DS32(0x8FEE0) = st[76];
    DS32(0x9045C) = st[77]; DS32(0x908D0) = st[78];
    DS32(0x906B0) = st[79];
    if (st[79] == 0xff) DS32(0x906B0) = 0;
    DF32(0x90090) = f32_div(st[80], 100.0);                       /* g_StallSpeed */
    DF32(0x90780) = f32_div(st[81], 100.0);                       /* g_Drag */
    DS32(0x903B0) = st[82];                                       /* g_TopSpeed */
    DF32(0x90674) = f32_div(st[83], 100.0);                       /* g_Thrust */
    if (D32(0x90674) == 0) DS32(0x90594) = 1;
    DS32(0x907D8) = st[84];
    DS32(0x907DC) = st[85];
    DF32(0x907A0) = f32_div(st[86], 100.0);                       /* g_CruiseSpeed */
    if (DS32(0x90594) != 0) {
        DF32(0x90780) = (float)((double)DF32(0x90780) / 5.0);
        DF32(0x90090) = DF32(0x90090) / 100.0f;
    }
    DS32(0x90954) = st[87];
    if (10 < st[87]) DS32(0x90954) = -1;
    DS32(0x90168) = st[88]; DS32(0x905DC) = st[89]; DS32(0x8FF14) = st[90]; g_IsHeli = st[91];
    DS32(0x90610) = st[92]; DS32(0x903D4) = st[93];
    DS32(0x90558) = st[94] % 10; DS32(0x901F8) = st[94] / 10;
    DS32(0x90A34) = st[95]; DS32(0x90958) = st[96]; DS32(0x9090C) = st[97]; DS32(0x90A48) = st[98];
    DS32(0x90A4C) = st[99]; DS32(0x8FF0C) = st[100];
    g_RackPoints[0] = st[101]; g_RackPoints[1] = st[102]; g_HardpointLoad = st[103];
    g_RackMaxLoad[0] = st[104]; g_RackMaxLoad[1] = st[105]; g_PlaneFuel = st[106]; DS32(0x90788) = st[107];
    g_Armour = st[108]; DS32(0x901DC) = st[109]; DS32(0x901E0) = st[110]; DS32(0x906D0) = st[111];
    DS32(0x903C0) = st[112]; DS32(0x8FF20) = st[113]; DS32(0x90644) = st[114]; DS32(0x9006C) = st[115];
    DS32(0x8FFE4) = st[116]; DS32(0x8FFE8) = st[117]; DS32(0x90130) = st[118]; DS32(0x90844) = st[119];
    DS32(0x90070) = st[120];
    int ret = st[111];
    if (st[111] != DS32(0x906E8)) {
        DS32(0x90724) = st[111];
        ret = st[111] / 10;
        if (st[111] % 10 == 3) DS32(0x90724) = st[111] - 3;
    }
    DS32(0x900A4) = 1;
    DS32(0x8FA18) = 0xb0;
    DS32(0x90150) = 1;
    DS32(0x90300) = 0;
    DS32(0x9046C) = 1;
    if (g_AlienAbduct == 0x40) g_AlienAbduct = -1;
    return ret;
}

/* ================================================================ end of a mission (§9, §10) */

/* 0x2d2b8 Sarcasm_Load: 28 lines, newline kept. */
void Sarcasm_Load(void)
{
    FILE *f = Platform_Fopen(DSTR(0x8124C), DSTR(0x8124A));       /* data/sarcasm, "r" */
    if (!f) FatalError(DSTR(0x8124C), DSTR(0x81259), 1);
    for (g_LoopI = 0; g_LoopI < 0x1c; g_LoopI++) fgets_js(g_SarcasmLines + g_LoopI * 0x50, 0x50, f);
    fclose(f);
}

#define SARC(n) (g_SarcasmLines + (n) * 0x50)

/* 0x2c9cb Mission_Debrief: text over the last frame, on the page being shown. */
void Mission_Debrief(void)
{
    g_BackPage = (g_BackPage == 0x18C0) ? 0x78C0 : 0x18C0;
    if (g_SarcasmLines[0] == 0) Sarcasm_Load();
    g_EndGameIndex = 3;
    g_AbortFlag = 1;
    if (DS32(0x9049C) == 1) g_EndGameIndex = 5;
    strcpy(g_DebriefText, SARC(Rand(3)));
    if (Rand(1) != 0 && (g_CrashDir == 0 || 7 < g_CrashDir)) {
        strcpy(g_DebriefText, SARC(4));
        g_EndGameIndex = 2;
    }
    if (g_CrashAttr == 0x82 || g_CrashTileAttr == 0x82) {
        strcpy(g_DebriefText, SARC(Rand(2) + 5));
        g_EndGameIndex = 1;
    }
    if (g_CrashAttr == 0x81) {
        g_EndGameIndex = 4;
        strcpy(g_DebriefText, SARC(Rand(3) + 8));
    }
    if (99 < g_EjectState) {
        g_EndGameIndex = 0;
        strcpy(g_DebriefText, SARC(12));
        if (Rand(1) != 0) strcpy(g_DebriefText, SARC(13));
    }
    if (g_EjectState == 99) {
        g_EndGameIndex = 6;
        g_AbortFlag = 0;
        strcpy(g_DebriefText, SARC(14));
        if (Rand(1) != 0 && 4 < DS32(0x9034C)) strcpy(g_DebriefText, SARC(Rand(1) + 0xf));
        if (Rand(1) != 0 && g_CrashAttr == 0x82) strcpy(g_DebriefText, SARC(17));
        if (Rand(1) != 0) strcpy(g_DebriefText, SARC(18));
    }
    if (1 < g_OnGround && g_EjectState == 0) {
        g_AbortFlag = 0;
        strcpy(g_DebriefText, SARC(Rand(1) + 0x13));
    }
    if (g_Lives == 0 && g_EjectState == 99) {
        g_AbortFlag = 0;
        g_EndGameIndex = 6;
        strcpy(g_DebriefText, SARC(21));
    }
    if (g_MissionResult == 2) {
        g_AbortFlag = 0;
        s32 base = DS32(0x90A10) * 3;
        strcpy(g_DebriefText, SARC(Rand(2) + base + 0x16));
    } else if (g_GameMode == 3) {
        strcpy(g_DebriefText, HUDTEXT(Rand(1) + 0x3a));
        g_AbortFlag = 0;
    }
    if (0x3f < g_AlienAbduct) {
        strcpy(g_DebriefText, HUDTEXT(60));                       /* "NO ONE WILL BELIEVE YOU !" */
        g_AbortFlag = 0;
        g_PlaneSel = 0;
    }
    if (g_DebriefText[(ptrdiff_t)strlen(g_DebriefText) - 1] != ' ') strcat(g_DebriefText, DSTR(0x81240));
    if (0 < g_EjectState) g_OnGround = 0;
    DS32(0x90354) = 0x28;
    memcpy(s_Tmp85048, DSTR(0x81240), 2);                         /* " " */
    strcat(s_Tmp85048, g_DebriefText);
    DS32(0x90434) = 0;
    while (s_Tmp85048[0] != 0) {
        DS32(0x8FF44) = Text_FitWidth(s_Tmp85048, 0xf0);
        int n = DS32(0x8FF44);
        if (n < 1 || Text_WidthSmall(s_Tmp85048) < 0xf0) {
            strcpy(s_Line85248, s_Tmp85048);
            s_Tmp85048[0] = 0;
        } else {
            strncpy(s_Line85248, s_Tmp85048, (size_t)n);         /* no terminator (Q13, kept) */
            memmove(s_Tmp85048, s_Tmp85048 + n, strlen(s_Tmp85048 + n) + 1);
        }
        Text_DrawSmall((0xa0 - Text_WidthSmall(s_Line85248) / 2) + g_ScrollFineX, DS32(0x90434) + 0x2c + g_ScrollFineY, s_Line85248, 1);
        DS32(0x90434) += 10;
    }
    if (g_EjectState == 99 && g_GameMode == 0) {
        strcpy(g_InfoText, HUDTEXT(98));                          /* "YOU HAVE" */
        itoa_js(g_Lives, s_Num85548);
        strcat(g_InfoText, s_Num85548);
        strcat(g_InfoText, HUDTEXT(99));                          /* " CRASHES LEFT" */
        Text_DrawSmall((0xa0 - Text_WidthSmall(g_InfoText) / 2) + g_ScrollFineX, DS32(0x90354) * 8 + 0x50 + g_ScrollFineY, g_InfoText, 1);
    }
    if (g_MissionResult == 2) {
        s_Line85248[0] = 0;
        if (g_MissionBonus < 1) {
            strcpy(g_InfoText, HUDTEXT(71));                      /* "NO BONUS" */
        } else {
            if (g_GameMode == 3) {
                g_MissionBonus -= DS32(0x903D0) * 10 + DS32(0x903B8);
                DS32(0x90904) = g_AeroPlaneSlot;
            } else {
                DS32(0x90904) = g_PlaneClass + 1;
            }
            strcpy(g_InfoText, HUDTEXT(69));                      /* "BONUS" */
            itoa_js(g_MissionBonus, s_Num85548);
            strcat(g_InfoText, s_Num85548);
            strcat(g_InfoText, DSTR(0x81242));                    /* " PTS" */
            g_Score[g_AeroPlayer] += g_MissionBonus * DS32(0x90904);
            if (1 < DS32(0x90904)) {
                strcpy(s_Line85248, DSTR(0x81247));               /* " x" */
                itoa_js(DS32(0x90904), s_Num85548);
                strcat(s_Line85248, s_Num85548);
                strcat(s_Line85248, HUDTEXT(70));                 /* " FOR BRAVERY" */
            }
        }
        Text_DrawSmall((g_ScrollFineX + 0xa0) - Text_WidthSmall(g_InfoText) / 2, DS32(0x90354) * 8 + 0x50 + g_ScrollFineY, g_InfoText, 1);
        if (s_Line85248[0] != 0)                                  /* sic: centred with line 1's width */
            Text_DrawSmall((g_ScrollFineX + 0xa0) - Text_WidthSmall(g_InfoText) / 2, DS32(0x90354) * 8 + 0x52 + g_ScrollFineY, s_Line85248, 1);
    }
    if (g_GameMode == 3) {
        for (int k = 0; k < 2; k++) {                             /* "PLAYER 1" twice (Q14, kept) */
            strcpy(s_Line85248, HUDTEXT(61));
            strcat(s_Line85248, HUDTEXT(63));
            itoa_js(g_Score[k], s_Num85548);
            strcat(s_Line85248, s_Num85548);
            Text_DrawSmall((g_ScrollFineX + 0xa0) - Text_WidthSmall(s_Line85248) / 2,
                           DS32(0x90354) * 8 + (k ? 0x72 : 0x66) + g_ScrollFineY, s_Line85248, 1);
        }
    }
}

/* 0x304e0 TrainingCredits_Screen */
void TrainingCredits_Screen(void)
{
    Pal_Fade(0, 0x100, 0, 0x20);
    if (g_GameMode == 1) strcpy(g_StoryName, DSTR(0x81326));      /* traincrt.pax */
    if (g_GameMode == 2) strcpy(g_StoryName, DSTR(0x81333));      /* practcrt.pax */
    Pic_LoadPax(g_StoryName, 0, 0);
    Video_SetSplitLine(400);
    Video_SetStartAndPan(0, 0, 0);
    Pal_Fade(0, 0x40, 1, 0x20);
}

/* 0x45352 AeroScores_Screen */
void AeroScores_Screen(void)
{
    char num[12];
    Pal_Fade(0, 0x100, 0, 0x20);
    Pic_LoadPax(DSTR(0x81474), 0, 0);                             /* aoscores.pax */
    Video_SetStartAndPan(0, 0, 0);
    Video_SetSplitLine(400);
    DS32(0x8FF44) = 0;
    DS32(0x90540) = 0;
    for (g_LoopI = 0; g_LoopI < g_AeroPlayers; g_LoopI++) {
        if (DS32(0x90540) < g_Score[g_LoopI]) { DS32(0x8FF44) = g_LoopI; DS32(0x90540) = g_Score[g_LoopI]; }
        strcpy(s_Tmp85048, DSTR(0x81481));                        /* "player " */
        itoa_js(g_LoopI + 1, s_Tmp85048 + strlen(s_Tmp85048));
        strcat(s_Tmp85048, DSTR(0x81489));                        /* " scored 00000000" */
        itoa_js(g_Score[g_LoopI], num);
        strcpy(s_Tmp85048 + (strlen(s_Tmp85048) - strlen(num)), num);
        Text_DrawBig(0xa0 - Text_WidthBig(s_Tmp85048) / 2, g_LoopI * 0xd + 0x56, s_Tmp85048, 0);
    }
    strcpy(s_Tmp85048, DSTR(0x81481));
    itoa_js(DS32(0x8FF44) + 1, s_Tmp85048 + strlen(s_Tmp85048));
    strcat(s_Tmp85048, DSTR(0x8149A));                            /* " wins !!" */
    Text_DrawBig(0xa0 - Text_WidthBig(s_Tmp85048) / 2, 0xbe, s_Tmp85048, 0);
    Pal_Fade(0, 0x100, 1, 0x20);
}

/* 0x301b2 EndGame_Screen */
void EndGame_Screen(void)
{
    DS32(0x90470) = 0;
    if (g_Lives == -10) {
        if (DS32(0x907B4) == 1) DS32(0x907B4) = 0;
        if (g_GameMode == 0) {
            Pal_Fade(0, 0x100, 0, 0x20);
            Pic_LoadPax(DSTR(0x81319), 0, 0);                     /* gamedone.pax */
            Video_SetStartAndPan(0, 0, 0);
            Video_SetSplitLine(400);
            Pal_Fade(0, 0x100, 1, 0x20);
        } else if (g_GameMode == 3) {
            AeroScores_Screen();
        } else {
            s32 x = g_BaseStartX + (g_BaseEndX - g_BaseStartX) / 2;
            /* the airbase (the credits picture replaces it immediately, Q18). PORT: guard for a run that
             * never loaded a map (the original divides by zero there). */
            if (g_MapWidth != 0) Level_DrawBackground(x / 16 - 9, 0x35);
            strcpy(s_Tmp85048, HUDTEXT(65));                      /* "FINAL SCORE" */
            itoa_js(g_Score[0], s_Tmp85048 + strlen(s_Tmp85048));
            Text_DrawSmall(0xa0 - Text_WidthSmall(s_Tmp85048) / 2, 0x78, s_Tmp85048, 0);
            TrainingCredits_Screen();
        }
    } else {
        if (DS32(0x907B4) == 1) DS32(0x907B4) = 0;
        char name[20];
        strcpy(name, DSTR(0x8130C));                              /* "endgame" */
        name[7] = (char)(g_EndGameIndex + '0');
        name[8] = 0;
        strcat(name, DSTR(0x81314));                              /* ".pax" */
        Pal_Fade(0, 0x100, 0, 0x20);
        Video_SetSplitLine(400);
        Video_SetStartAndPan(0, 0x14, 0);
        Pic_LoadPax(name, 0, 0);
        strcpy(s_Tmp85048, g_DebriefText);
        DS32(0x90434) = 0xc;
        while (s_Tmp85048[0] != 0) {
            wrap_line(s_Tmp85048, s_Line85248, 0xf0, true, 0);
            Text_DrawSmall((g_ScrollFineX + 0xa0) - Text_WidthSmall(s_Line85248) / 2, DS32(0x90434), s_Line85248, 0);
            DS32(0x90434) += 10;
        }
        if (g_CDMusicOn) {
            g_CDEndgameLatch = 1;
            CD_PlayTrack(g_EndGameIndex + 7);
        }
        Pal_Fade(0, 0x100, 1, 0x20);
    }
}
