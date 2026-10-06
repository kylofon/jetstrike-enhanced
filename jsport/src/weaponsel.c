/* WeaponSelect_Screen and helpers: port/spec/game_flow.md §7.2 (checked against the disassembly 0x29d90..). Called
 * by Airbase_Update (enemies/airbase step); until then frame.c opens it with F12 while parked (PORT debug). */
#include "mission.h"

#include <string.h>

#include "pic.h"
#include "platform.h"
#include "sound.h"
#include "video.h"

#define g_HoverZone DS32(0x8FEA8)               /* Zone_HitTest of the cursor cell */
#define g_ZoneGrid  DS32A(0x8DCA0)              /* [x*8 + y]: read when the cursor cell has no zone */

static const char *const where = "WeaponSelect_Screen";

/* §7.1 load formula without the stock cap (the screen's 'a/b/cap' chain, js.c 0x29e..). */
static s32 rack_capacity(int r, s32 per)
{
    s32 w = g_RackWeapon[r], wt = g_WeaponWeight[w];
    s32 a = idiv_js(g_RackMaxLoad[r], wt, where);
    s32 b = idiv_js(g_HardpointLoad * g_RackPoints[r], wt, where);
    s32 m = (a * per < per * b) ? a : b;
    m = per * m;
    s32 cap = g_WeaponRackMult[w] * g_RackPoints[r] * per;
    return cap < m ? per * g_WeaponRackMult[w] * g_RackPoints[r] : m;
}

static int confirm_key(void) { return g_Fire != 0 || g_KeyDown[0x39] != 0 || g_KeyDown[0x1c] != 0; }

/* 0x2ba3c WeaponSelect_DrawCounts: rack 1's weight divided by rack 0's per-rack count (Q10, kept). */
void WeaponSelect_DrawCounts(void)
{
    s32 per0 = g_WeaponPerRack[g_RackWeapon[0]], per1 = g_WeaponPerRack[g_RackWeapon[1]];
    Text_DrawNumber(0x6d, 0xba, idiv_js(g_RackRounds[0], per0, "WeaponSelect_DrawCounts"), 4);
    Text_DrawNumber(0x6d, 0xd9, idiv_js(g_RackRounds[1], per1, "WeaponSelect_DrawCounts"), 4);
    Text_DrawNumber(0x74, 200, idiv_js(g_WeaponWeight[g_RackWeapon[0]] * g_RackRounds[0], per0, "WeaponSelect_DrawCounts"), 5);
    Text_DrawNumber(0x74, 0xe7, idiv_js(g_WeaponWeight[g_RackWeapon[1]] * g_RackRounds[1], per0, "WeaponSelect_DrawCounts"), 5);
}

/* 0x2bb33 WeaponSelect_DrawInfo (weapon 0x8ff44): description, stock, "NOT LOADED". */
void WeaponSelect_DrawInfo(void)
{
    s32 i = DS32(0x8FF44);
    DS32(0x90434) = 0;
    strcpy(g_InfoText, g_WeaponDesc + i * 0xa0);
    strcat(g_InfoText, DSTR(0x81239));                               /* "  -  " */
    itoa_js(g_WeaponStock[i], g_InfoText + strlen(g_InfoText));
    strcat(g_InfoText, HUDTEXT(75));                                 /* " AVAILABLE" */
    if (g_RackRounds[g_RackSel] == 0 && g_WeaponStock[i] > 0) strcat(g_InfoText, HUDTEXT(76));   /* " ---NOT LOADED---" */
    SelectScreen_DrawInfoBox(0);                 /* PORT: no argument pushed in the original (Q11) */
}

static void hl_save(int x, int y, int id)        /* Video_CopyRect(0, x, y, x+W, y+H, 0, 0, 0x104) */
{
    Video_CopyRect(0, x, y, x + (int)Sprite_GetWidth(id), y + (int)Sprite_GetHeight(id), 0, 0, 0x104);
}

static void hl_restore(int x, int y, int id)     /* Video_CopyRect(0, 0, 0x104, W, 0x104+H, 0, x, y) */
{
    Video_CopyRect(0, 0, 0x104, (int)Sprite_GetWidth(id), (int)Sprite_GetHeight(id) + 0x104, 0, x, y);
}

/* 0x29d90 WeaponSelect_Screen */
void WeaponSelect_Screen(void)
{
    int hl = 0, first = 0;
    char name[11];
    if (g_CDEndgameLatch != 0) CD_Stop();
    DS32(0x90140) = 0;
    Sound_StopAll();
    DS32(0x904F8) = 0;
    Weapons_ReturnUnused();
    DS32(0x9034C) = 0;
    DS32(0x90300) = 1;
    Pal_Fade(0, 0x100, 0, 0x20);
    g_MissionActive = 1;
    SelectScreen_LoadBg(1);                                          /* wepch.pax */
    Video_SetStartAndPan(0, 0x14, 0);
    Video_SetSplitLine(400);
    DS32(0x906A0) = 8;
    DS32(0x908F0) = 1;
    DS32(0x9034C) = 0;
    g_MissionActive = 1;
    g_FuelBase = g_PlaneFuel;
    g_MaxLoadWeight = ((g_RackMaxLoad[0] + g_RackMaxLoad[1]) * 2) / 3;
    for (g_RackSel = 0; g_RackSel < 2; g_RackSel++) {
        s32 w = g_RackWeapon[g_RackSel];
        if (g_HardpointLoad < g_WeaponWeight[w]) {
            g_RackRounds[g_RackSel] = 0;
        } else {
            s32 per = g_WeaponPerRack[w];
            s32 m = rack_capacity(g_RackSel, per);
            s32 n = g_WeaponStock[w] * per < m ? g_WeaponStock[w] * per : m;
            DS32(0x8FF30) = n;
            g_RackRounds[g_RackSel] = n;                             /* stock not deducted yet */
        }
    }
    g_RackSel = 0;
    DS32(0x900C0) = 0x4b;
    g_InfoBoxW = 0xce;
    g_CursorX = 0;
    g_CursorY = 0;
    g_MenuDX = 0;
    g_MenuDY = 0;
    g_MenuDone = 0;
    while (g_MenuDone == 0) {
        DS32(0x901B4) = 0;
        if (first != 0) Pal_Fade(0x40, 0x80, 0, 0x10);
        if (first == 0) {
            Pal_Fade(0, 0x80, 1, 0x20);
            Pal_Upload(0xff, 0x100);
            first++;
        } else {
            g_PicFullLoad = 0;
            Pic_LoadPax(DSTR(0x81224), 0, 0);                        /* wepch.pax */
            g_PicFullLoad = 1;
        }
        Video_CopyRect(0, 0x10, 0xb5, 100, 200, 0, 0, 400);          /* save the rack boxes */
        Video_CopyRect(0, 0x10, 0xd5, 100, 0xe8, 0, 0xa0, 400);
        for (g_LoopI = 0; g_LoopI < 4; g_LoopI++)
            for (g_LoopJ = 0; g_LoopJ < 4; g_LoopJ++) {
                s32 i = g_SelPage * g_SelPageSize + g_LoopJ + g_LoopI * 4;
                if (g_WeaponCount < i || 0x3b < i || g_WeaponIcon[i] < 2) {
                    Text_DrawCenteredAt(0, g_LoopJ * 0x47 + 0x15, g_LoopI * 0x20 + 0x49, DSTR(0x8122E));
                } else {
                    if (g_WeaponStock[i] > 0) Sprite_DrawNow((u32)(g_LoopJ * 0x47 + 0x24), (u32)(g_LoopI * 0x20 + 0x3d), g_WeaponIcon[i]);
                    strncpy(name, g_WeaponNames + i * 0x28, 10);
                    name[10] = 0;
                    Text_DrawCenteredAt(0, g_LoopJ * 0x47 + 0x15, g_LoopI * 0x20 + 0x49 + (g_LoopI != 0), name);
                }
            }
        Pal_Fade(0x40, 0x80, 1, 0x10);
        Zone_Clear();
        for (g_LoopI = 0; g_LoopI < 4; g_LoopI++)
            for (g_LoopJ = 0; g_LoopJ < 4; g_LoopJ++) Zone_Add(g_LoopI * 4 + 1 + g_LoopJ, g_LoopJ, g_LoopI, g_LoopJ, g_LoopI);
        DS32(0x9043C) = 3;
        WeaponSelect_DrawCounts();
        /* stock preview of the rack being edited (display only; sign -1 here, +1 in the load: Q9) */
        DS32(0x8FED4) = g_WeaponPerRack[g_RackWeapon[g_RackSel]];
        s32 per = DS32(0x8FED4);
        s32 sgn = (g_RackWeapon[0] == g_RackWeapon[1]) ? -1 : 0;
        s32 st = idiv_js(g_RackRounds[1 - g_RackSel], per, where) * sgn + g_WeaponStock[g_RackWeapon[g_RackSel]];
        if (st < 0) st = 0;
        DS32(0x8FEFC) = st;
        s32 cap = rack_capacity(g_RackSel, per);
        DS32(0x8FF30) = st * per < cap ? st * per : cap;
        Zone_Add(0x17, 0, 4, 3, 4);                                  /* arrow */
        Zone_Add(0x19, 0, 5, 2, 5);                                  /* NEW PLANE */
        Zone_Add(0x18, 3, 5, 3, 5);                                  /* DONE */
        g_SelPageSize = 0x10;
        while (DS32(0x901B4) == 0) {
            g_HoverZone = Zone_HitTest(g_CursorX, g_CursorY);
            for (int guard = 0; g_HoverZone == 0 && guard < 64; guard++) {   /* PORT: guard (never 0 in practice) */
                g_CursorX = Wrap(g_CursorX + 1, 0, 3);
                g_HoverZone = g_ZoneGrid[g_CursorX * 8 + g_CursorY];
            }
            int cx = g_CursorX * 0x47 + 0x11;
            if (g_HoverZone > 0 && g_HoverZone < 0x15) {
                /* the save uses 0x90830 != 0 where the drawing uses g_CursorY != 0 (kept) */
                hl_save(cx, (g_CursorY * 0x21 + 0x35 - (g_LoopK != 0)) - (g_CursorY == 3), 0x1c5);
                hl = 1;
                Sprite_DrawNow((u32)cx, (u32)((g_CursorY * 0x21 + 0x35 - (g_CursorY != 0)) - (g_CursorY == 3)), 0x1c5);
            }
            if (g_HoverZone == 0x17) {
                g_CursorX = 3; g_CursorY = 4;
                hl_save(0xe2, 0xb7, 0x1cb);
                Sprite_DrawNow(0xe2, 0xb7, 0x1cb);
                hl = 2;
            }
            if (g_HoverZone == 0x19) {
                g_CursorX = 2; g_CursorY = 5;
                hl_save(0x9c, 0xe2, 0x1cb);
                Sprite_DrawNow(0x9c, 0xe2, 0x1cb);
                hl = 3;
            }
            if (g_HoverZone == 0x18) {
                g_CursorX = 3; g_CursorY = 5;
                hl_save(0xe2, 0xe2, 0x1cb);
                Sprite_DrawNow(0xe2, 0xe2, 0x1cb);
                hl = 4;
            }
            Video_CopyRect(0, 0, 400, 0x54, 0x1a3, 0, 0x10, 0xb5);  /* restore the rack boxes */
            if (g_RackRounds[0] > 0) Sprite_DrawNow(0x26, 0xbe, g_WeaponIcon[g_RackWeapon[0]]);
            Video_CopyRect(0, 0xa0, 400, 0xf4, 0x1a3, 0, 0x10, 0xd5);
            if (g_RackRounds[1] > 0) Sprite_DrawNow(0x26, 0xde, g_WeaponIcon[g_RackWeapon[1]]);
            Sprite_DrawNow(0x48, (u32)(g_RackSel * 0x20 + 0xbe), 0x9d);   /* rack pointer */
            Input_PollMenu();
            switch (hl) {
            case 1: hl_restore(g_CursorX * 0x47 + 0x11, (g_CursorY * 0x21 + 0x35 - (g_LoopK != 0)) - (g_CursorY == 3), 0x1c5); hl = 0; break;
            case 2: hl_restore(0xe2, 0xb7, 0x1cb); hl = 0; break;
            case 3: hl_restore(0x9c, 0xe2, 0x1cb); hl = 0; break;
            case 4: hl_restore(0xe2, 0xe2, 0x1cb); hl = 0; break;
            default: break;
            }
            g_CursorX = Clamp(g_CursorX + g_MenuDX, 0, 3);
            g_CursorY = Clamp(g_CursorY + g_MenuDY, 0, 5);
            if (g_SelPage == 3 && g_CursorY == 3) g_CursorY = g_MenuDY + 3;   /* last page has 3 rows */
            if (g_FireOrConfirm != 0 || g_Ctrl[0] != 0 || g_Ctrl[1] != 0 || confirm_key()) {
                g_ZoneHit = Zone_HitTest(g_CursorX, g_CursorY);
                for (int guard = 0; g_ZoneHit == 0 && guard < 64; guard++) {   /* PORT: the original loops forever */
                    Wrap(g_CursorX + 1, 0, 3);                       /* result discarded (sic) */
                    g_ZoneHit = g_ZoneGrid[g_CursorX * 8 + g_CursorY];
                }
                if (g_Ctrl[2] != 0) {                                /* dead: g_Ctrl[2] is always 0 */
                    g_CursorX = 3;
                    g_CursorY = DS32(0x9043C) + 1;
                    g_ZoneHit = 0;
                }
                if (g_ZoneHit > 0 && g_ZoneHit < 0x15 && confirm_key()) {
                    if (g_Ctrl[3] != 0) g_RackSel = 0;               /* dead */
                    if (g_Ctrl[5] != 0) g_RackSel = 1;               /* dead */
                    s32 i = g_SelPage * g_SelPageSize + g_ZoneHit;   /* 1-based */
                    if (g_WeaponIcon[i - 1] > 1) {
                        g_RackWeapon[g_RackSel] = g_WeaponCount < i - 1 ? g_WeaponCount : i - 1;
                        s32 w = g_RackWeapon[g_RackSel];
                        if (g_HardpointLoad < g_WeaponWeight[w]) {
                            g_RackRounds[g_RackSel] = 0;
                            WeaponSelect_DrawCounts();
                        } else {
                            DS32(0x8FED4) = g_WeaponPerRack[w];
                            s32 p = DS32(0x8FED4);
                            s32 same = (g_RackWeapon[0] == g_RackWeapon[1]);
                            s32 s2 = idiv_js(g_RackRounds[1 - g_RackSel], p, where) * same + g_WeaponStock[w];
                            if (s2 < 0) s2 = 0;
                            DS32(0x8FEFC) = s2;
                            s32 c2 = rack_capacity(g_RackSel, p);
                            DS32(0x8FF30) = s2 * p < c2 ? s2 * p : c2;
                            g_RackRounds[g_RackSel] = DS32(0x8FF30);
                        }
                        DS32(0x8FF44) = g_WeaponCount < i - 1 ? g_WeaponCount : i - 1;
                        WeaponSelect_DrawInfo();
                        g_RackSel = 1 - g_RackSel;
                        WeaponSelect_DrawCounts();
                    }
                }
                if (g_ZoneHit == 0x15 || g_ZoneHit == 0x16) {        /* dead: no such zones */
                    g_RackSel = g_ZoneHit - 0x15;
                    WeaponSelect_DrawCounts();
                }
                if (g_Ctrl[0] != 0) {                                /* previous page */
                    if (--g_SelPage < 0) g_SelPage = 3;
                    DS32(0x901B4) = 1;
                    while (g_Ctrl[0] != 0) Input_PollMenu();
                    g_CursorX = 3; g_CursorY = 4;
                }
                if (g_ZoneHit == 0x17 || g_Ctrl[1] != 0) {           /* next page */
                    g_SelPage++;
                    if (0x3b < g_SelPage * g_SelPageSize) g_SelPage = 0;
                    DS32(0x901B4) = 1;
                    while (g_Ctrl[1] != 0) Input_PollMenu();
                    g_CursorX = 3; g_CursorY = 4;
                }
                if (g_ZoneHit == 0x18 && confirm_key()) {            /* DONE */
                    g_MenuDone = 1;
                    DS32(0x901B4) = 1;
                }
                if (g_ZoneHit == 0x19 && confirm_key()) {            /* NEW PLANE */
                    g_MissionActive = 0;
                    g_MenuDone = 1;
                    DS32(0x901B4) = 1;
                }
                while (confirm_key()) Input_PollMenu();
            }
        }
    }
    Mission_ResetState();
    DS32(0x90300) = 0;
    Pal_Fade(0, 0x100, 0, 0x20);
    DS32(0x8FA18) = 0xb0;
    DS32(0x9046C) = 1;
}
