/* HUD: port/spec/player.md §10 (panel, messages, radar, target arrow) and §9.2 (briefing overlay). The panel
 * (display.pax) lives at VRAM offset 0 and is shown below the split line; everything here draws in absolute
 * VRAM coordinates except the messages and the briefing overlay (on the back page). */
#include "mission.h"

#include <stdlib.h>
#include <string.h>

#include "level.h"
#include "platform.h"
#include "video.h"

static void draw_lamps(void)
{
    for (g_LoopI = 0; g_LoopI < 0xe; g_LoopI++)
        Sprite_DrawNowShift((u32)((g_LoopI % 7) * 8 + 0xc), (u32)((g_LoopI / 7) * 7 + 0x2c), g_DamageFlags[g_LoopI] + 0x8a);
}

/* 0x25c8b Hud_DrawPanel */
void Hud_DrawPanel(void)
{
    DS32(0x90394) = g_MapWidth << 4;
    Pic_LoadHudPanel(DSTR(0x81206));                                /* "display.pax" */
    if (g_RackRounds[0] > 0) Sprite_DrawNowShift(0x5e, 0x16, g_WeaponIcon[g_RackWeapon[0]]);
    if (g_RackRounds[1] > 0) Sprite_DrawNowShift(0x5e, 0x2a, g_WeaponIcon[g_RackWeapon[1]]);
    for (g_LoopI = 0; g_LoopI < g_Lives && g_LoopI < 10; g_LoopI++)
        Sprite_DrawNowShift(0x122, Sprite_GetHeight(0x1fb) * (u32)g_LoopI + 10, 0x1fb);
    draw_lamps();
    Video_SetLineColor(0x16);
    DS32(0x909A4) = 0;
    DS32(0x909AC) = 0x19;
    for (g_LoopI = 0; g_LoopI < g_MapWidth; g_LoopI++) {
        DS32(0x909A8) = idiv_js(g_LoopI * 0x3e, g_MapWidth, "Hud_DrawPanel");
        u32 h = Byte_Get(g_MapVal, g_LoopI + 0xbd0);
        DS32(0x90434) = (s32)((h & 0xff) * 0xd) >> 6;
        if (DS32(0x909A4) < DS32(0x909A8)) {
            Video_DrawLineColor((u32)(DS32(0x909A4) + 0xdc), (u32)(DS32(0x909AC) + 0x11), (u32)(DS32(0x909A4) + 0xdc), 0x1d);
            DS32(0x909AC) = 0x19;
            DS32(0x909A4) = DS32(0x909A8);
        }
        DS32(0x909AC) = DS32(0x909AC) < DS32(0x90434) ? DS32(0x909AC) : DS32(0x90434);
    }
    g_DamageLampsDirty = 0;
    if (g_GameMode != 3) {
        Text_DrawNumber(0x54, 0x20, g_RackRounds[0], 3);
        Text_DrawNumber(0x54, 0x32, g_RackRounds[1], 3);
        Text_DrawNumber(0x54, 0xb, g_GunAmmo, 4);
    }
    Hud_DrawRadarLine(0x3a, 0x28, g_BaseStartX / 16, g_BaseEndX / 16, 1);
    if (g_MP_PickupCol != 0 && g_MP_PickupSprite < 500) Hud_DrawRadarLine(3, 0x2d, g_MP_PickupCol, g_MP_PickupCol, 3);
    if (g_MP_ReconCol != 0) Hud_DrawRadarLine(3, 0x2d, g_MP_ReconCol, g_MP_ReconCol, 3);
    if (g_MP_TargetX0 != 0 && g_MP_TargetX0 < 5000 && g_MP_TargetX1 != 0)
        Hud_DrawRadarLine(3, 0x2d, g_MP_TargetX0, g_MissionParams[4 - (g_AgentDropPending != 0)], 3);
    if (g_MP_ConvoyCol != 0) Hud_DrawRadarLine(3, 0x2d, g_MP_ConvoyCol, g_MP_ConvoyCol, 3);
    DS32(0x90160) = -1;
    DS32(0x900F4) = -1;
    DS32(0x90414) = 1;
    g_OnGround = 1;                              /* sic (player.md §10.1) */
    DS32(0x902A8) = -1;
    DS32(0x905EC) = -1;
    DS32(0x902A4) = -1;
    DS32(0x908E0) = -999;
    if (DS32(0x902F8) == 1) {
        DS32(0x9040C) = idiv_js(g_BaseStartX * 0x3e0, DS32(0x90394), "Hud_DrawPanel");
        DS32(0x901D4) = idiv_js(g_BaseEndX * 0x3e0, DS32(0x90394), "Hud_DrawPanel");
        Video_FillRect(0, 0x40, 0x7e, 0x59, 0);
        DS32(0x902F8) = 0;
    }
    Video_CopyRect(0, 0xdc, 5, 0x11a, 0x1e, 0, 0, vl.save_row);       /* radar background (row 0x246) */
    Video_CopyRect(0, 0x95, 0x14, 0x9f, 0x37, 0, 0x40, vl.save_row);  /* altimeter background */
    if (g_GameMode > 0) {
        DS32(0x9040C) = 0;
        DS32(0x901D4) = 0x3e;
    }
    Pal_Fade(0, 0x100, 1, 0x20);
    Video_SetLineColor(1);
}

/* 0x26174 Hud_DrawRadarLine(dx0, y0, wx1, wx2, col): a bracket from the panel to the radar strip. */
void Hud_DrawRadarLine(int dx0, int y0, int wx1, int wx2, int col)
{
    Video_SetLineColor(col);
    int w16 = g_MapWidth << 4;
    int x1 = idiv_js(wx1 * 0x3e0, w16, "Hud_DrawRadarLine");
    int x2 = idiv_js(wx2 * 0x3e0, w16, "Hud_DrawRadarLine");
    dx0 += 0xdd;
    x1 += 0xdd;
    x2 += 0xdd;
    DS32(0x8FEC8) = (x1 + x2) / 2;
    s32 m = DS32(0x8FEC8);
    Video_DrawLineColor((u32)dx0, 0x31, (u32)dx0, (u32)y0);
    Video_DrawLineColor((u32)dx0, (u32)y0, (u32)m, (u32)y0);
    Video_DrawLineColor((u32)m, (u32)y0, (u32)m, 0x1e);
    Video_DrawLineColor((u32)x1, 0x1e, (u32)x2, 0x1e);
}

/* 0x267da Hud_DrawTargetArrow */
void Hud_DrawTargetArrow(int camX, int camY, int tx, int ty)
{
    if (abs(camX - tx) + abs(camY - ty) < 0x4b0 && tx != 0) {
        int x = tx - camX, y = ty - camY;
        if (x < 5) x = 5;
        if (x > 0x138) x = 0x138;
        if (y < 5) y = 5;
        if (y > 0xa4) y = 0xa4;
        Sprite_Queue(x, y, 0xa7);
    }
}

/* 0x26e03 Hud_PushMessage (no length check) */
void Hud_PushMessage(const char *s)
{
    if (g_HudMsgCount < 10) {
        char *dst = g_HudMsgs + g_HudMsgCount * 100;
        memmove(dst, s, strlen(s) + 1);
        g_HudMsgTime[g_HudMsgCount] = 0x28;
        g_HudMsgCount++;
    }
}

/* 0x26e5a Hud_DrawMessages: an expiring message shifts the rest up but the row is not decremented (Q19). */
void Hud_DrawMessages(void)
{
    char *buf = DSTR(0x85448);
    int row = 0, k0 = 0;
    for (g_LoopI = 0; g_LoopI < g_HudMsgCount; g_LoopI++) {
        strcpy(buf, g_HudMsgs + g_LoopI * 100);
        Text_DrawSmall(vl.view_w / 2 - Text_WidthSmall(buf) / 2 + g_ScrollFineX, row * 8 + 0x2e + g_ScrollFineY, buf, 1);   /* ENH: view */
        if (--g_HudMsgTime[g_LoopI] == 0) {
            for (int k = k0; k < g_HudMsgCount - 1; k++) {
                memmove(g_HudMsgs + k * 100, g_HudMsgs + (k + 1) * 100, strlen(g_HudMsgs + (k + 1) * 100) + 1);
                g_HudMsgTime[k] = g_HudMsgTime[k + 1];
            }
            g_HudMsgCount--;
            g_LoopI--;
            k0--;
        }
        k0++;
        row++;
    }
}

/* 0x26b27 Hud_DrawBriefing (B held > 8 frames) */
void Hud_DrawBriefing(void)
{
    char tmp[512];
    char *line = DSTR(0x85248);
    strcpy(tmp, g_BriefingText);
    if (DS32(0x905AC) > 0)
        for (g_LoopI = 0; g_LoopI < DS32(0x905AC); g_LoopI++)
            itoa_js(DS32A(0x8DAB8)[DS32A(0x918E0)[g_LoopI]], tmp + strlen(tmp));   /* debug leftover */
    DS32(0x90434) = 7;
    while (tmp[0] != 0) {
        DS32(0x8FF44) = Text_FitWidth(tmp, 0xa0);
        int n = DS32(0x8FF44);
        if (n < 1 || Text_WidthSmall(tmp) < 0xa0) {
            strcpy(line, tmp);
            tmp[0] = 0;
        } else {
            strncpy(line, tmp, (size_t)n + 1);
            line[n + 1] = 0;
            memmove(tmp, tmp + n + 1, strlen(tmp + n + 1) + 1);
        }
        Text_DrawSmall(vl.view_w / 2 - Text_WidthSmall(line) / 2 + g_ScrollFineX, DS32(0x90434) * 8 + 10 + g_ScrollFineY, line, 1);   /* ENH: view */
        DS32(0x90434)++;
    }
}

/* 0x3f727 Hud_LampBlinkA / 0x3f777 Hud_LampBlinkB: the lamp sprite drawn at its own hotspot + offset. */
void Hud_LampBlinkA(void)
{
    u32 y = (Sprite_GetY(0x8c) & 0xffff) + 5;
    u32 x = (Sprite_GetX(0x8c) & 0xffff) + 0x51;
    Sprite_DrawNowShift(x, y, 0x8c);
}

void Hud_LampBlinkB(void)
{
    u32 y = (Sprite_GetY(0xa8) & 0xffff) + 5;
    u32 x = (Sprite_GetX(0xa8) & 0xffff) + 0xdd;
    Sprite_DrawNowShift(x, y, 0xa8);
}

/* 0x26cc6 Hud_Nop: empty in the original (its callers' messages are never shown). */
void Hud_Nop(const char *s) { (void)s; }

/* 0x26ce1 Hud_PlaneMessage */
void Hud_PlaneMessage(void)
{
    Hud_Nop(HUDTEXT(g_AeroPlayer + 0x3d));
    DS32(0x909B8) = 0;
}

/* 0x26d24 Hud_WeaponMessage: builds "<name> LOADED" / "NO ... AMMO" per rack for Hud_Nop. */
void Hud_WeaponMessage(void)
{
    char *buf = DSTR(0x85448);
    DS32(0x8FEB4) = 0;
    strcpy(buf, g_WeaponNames + g_RackWeapon[0] * 0x28);
    strcat(buf, DSTR(0x8B968));
    if (g_RackRounds[0] == 0) strcpy(buf, DSTR(0x8B148));
    g_LastDamage = 0;
    Hud_Nop(buf);
    strcpy(buf, g_WeaponNames + g_RackWeapon[1] * 0x28);
    strcat(buf, DSTR(0x8B968));
    if (g_RackRounds[1] == 0) strcpy(buf, DSTR(0x8B148));
    g_LastDamage = 1;
    Hud_Nop(buf);
}

static void radar_put2(s32 x, s32 y, int scale, u8 col)
{
    DS32(0x90748) = idiv_js(x * scale, DS32(0x90394), "Hud_UpdateRadar");
    DS32(0x9074C) = ((y + 0x7e8) * 0x19) / 0xc08;
    Video_PutPixel((u32)(DS32(0x90748) + 0xdd), DS32(0x9074C) + 5, col);
    Video_PutPixel((u32)(DS32(0x90748) + 0xde), DS32(0x9074C) + 5, col);
}

/* 0x2ec74 Hud_UpdateRadar (odd frames) */
void Hud_UpdateRadar(void)
{
    if (g_DamageFlags[7] == 0 && g_RadarJammed == 0) {
        DS32(0x90480) = 0;
        Video_CopyRect(0, 0, vl.save_row, 0x3e, vl.save_row + 0x19, 0, 0xdc, 5);   /* clear the radar */
    } else if (DS32(0x90480) == 0 && g_DamageFlags[7] == 0) {
        strcpy(g_InfoText, DSTR(0x81304));                           /* "Jammed" */
        Text_DrawSmall(0xfd - Text_WidthSmall(g_InfoText) / 2, 0x10, g_InfoText, 0);
        DS32(0x90480) = 1;
    }
    DS32(0x90394) = g_MapWidth << 4;
    if (g_DamageFlags[5] == 0) {
        DS32(0x900B8) = (s32)(g_Speed * 8.0f);
        if (DS32(0x900B8) != DS32(0x900F4)) {
            for (int i = 0; i < DS32(0x900B8); i++) {
                for (int r = 0; r < 3; r++) {
                    if (i < 0x2e) Video_PutPixel((u32)(i + 0xc), r + 8, (u8)(i / 3 + 0x21));
                    else if (i == 0x2e) Video_PutPixel(0x3a, r + 8, 0x27);
                    else Video_PutPixel((u32)(i + 0xc), r + 8, 0x30);
                }
            }
            for (int i = DS32(0x900B8); i < 0x30; i++)
                for (int r = 0; r < 3; r++) Video_PutPixel((u32)(i + 0xc), r + 8, 0);
            DS32(0x900F4) = DS32(0x900B8);
        }
        if (abs(g_Fuel - DS32(0x905EC)) > 0x32) {
            Video_FillRect(0x17, 0x17, 0x2d, 0x1c, 0);
            Text_DrawNumber(0x17, 0x17, g_Fuel, 5);
            DS32(0x905EC) = g_Fuel;
        }
    } else if (Rand(10) == 1 && DS32(0x9068C) == 0) {
        DS32(0x9068C) = 0xfff;
    }
    if (g_DamageFlags[7] == 0 && g_RadarJammed == 0) {
        s32 rx = idiv_js(g_CamX * 0x3e, DS32(0x90394), "Hud_UpdateRadar");
        s32 ry = ((g_CamY + 0x800) * 0x19) / 0xc08;
        DS32(0x90174) = rx;
        DS32(0x90178) = ry;
        if (rx != DS32(0x90160) || ry != DS32(0x90164)) {
            s32 lo = rx - 8 < 0 ? 0 : rx - 8;
            DS32(0x9040C) = DS32(0x9040C) < lo ? DS32(0x9040C) : lo;
            s32 hi = rx + 8 < 0x3f ? rx + 8 : 0x3e;
            DS32(0x901D4) = hi < DS32(0x901D4) ? DS32(0x901D4) : hi;
            DS32(0x90160) = rx;
            DS32(0x90164) = ry;
        }
        Video_PutPixel((u32)(rx + 0xdd), ry + 5, 0x1f);
        s32 gx = g_AeroGateX[g_AeroGateIdx];
        if (g_CrateX > 0 || gx > 0) {
            s32 x = g_CrateX < gx ? gx : g_CrateX;
            DS32(0x908F8) = idiv_js(x * 0x3e, DS32(0x90394), "Hud_UpdateRadar");
            s32 y = g_CrateY;
            if (g_AeroGateY[g_AeroGateIdx] != 0) y = g_AeroGateY[g_AeroGateIdx];
            DS32(0x908FC) = ((y + 0x7e8) * 0x19) / 0xc08;
            if (DS32(0x908F8) == rx && DS32(0x908FC) == ry) DS32(0x908E0) = -1;
            if (DS32(0x908F8) > 0) Video_PutPixel((u32)(DS32(0x908F8) + 0xdd), DS32(0x908FC) + 5, 0x1f);
        }
        if (g_EnemyAirCount != 0)
            for (g_LoopI = 0; g_LoopI < g_EnemyAirCount; g_LoopI++) {
                DS32(0x909A8) = idiv_js(g_EnemyAirX[g_LoopI] * 0x3e, DS32(0x90394), "Hud_UpdateRadar");
                DS32(0x909AC) = ((g_EnemyAirY[g_LoopI] + 0x7e8) * 0x19) / 0xc08;
                if (DS32(0x909A8) > 0) Video_PutPixel((u32)(DS32(0x909A8) + 0xdd), DS32(0x909AC) + 5, 0x1e);
            }
        if (g_EnemyGroundCount != 0)
            for (g_LoopI = 0; g_LoopI < g_EnemyGroundCount; g_LoopI++) {
                DS32(0x909A8) = idiv_js(DS32A(0x8D9D8)[g_LoopI] * 0x3e, DS32(0x90394), "Hud_UpdateRadar");
                DS32(0x909AC) = ((DS32A(0x8D9E8)[g_LoopI] + 0x800) * 0x19) / 0xbd9;
                if (DS32(0x909A8) > 0) Video_PutPixel((u32)(DS32(0x909A8) + 0xdd), DS32(0x909AC) + 4, 0x1e);
            }
        if (DS32(0x906DC) != 0) radar_put2(DS32(0x9073C), DS32(0x90740), 0x3e, 0x1e);   /* campaign bomber */
        if (DS32(0x90998) != 0) radar_put2(DS32(0x90984), DS32(0x90988), 0x3f, 0x1f);   /* B52 (scale 0x3f, Q16) */
        if (DS32(0x90528) != 0) radar_put2(DS32(0x90520), DS32(0x904C0), 0x3e, 0x1f);   /* Fat Albert */
        if (g_TankerType != 0) radar_put2(DS32(0x8FF64), DS32(0x8FF6C), 0x3e, 0x1f);    /* tanker */
    }
    Pal_SetColor(0x1f, D32(0x8038C) & 0xff, D32(0x8038C) & 0xff, D32(0x8038C) & 0xff);
    DS32(0x8038C) += DS32(0x80390) * 3;
    if (DS32(0x8038C) > 0x3d || DS32(0x8038C) < 0x23) DS32(0x80390) = -DS32(0x80390);
    Pal_SetColor(0x1e, D32(0x80384) & 0xff, 0xc, 0xc);
    DS32(0x80384) += DS32(0x80390) * 3;
    if (DS32(0x80384) > 0x3d || DS32(0x80384) < 0x23) DS32(0x80388) = -DS32(0x80388);   /* sic (Q16) */
    Video_CopyRect(0, 0x40, vl.save_row, 0x4a, vl.save_row + 0x23, 0, 0x95, 0x14);    /* restore the altimeter */
    s32 py = g_CamY + g_PlayerScrY;
    if (py < 0) py = 0;
    s32 ay = (s32)((double)py / 29.1764);        /* 0x403d2d288ce703b0 */
    for (int x = 0x96; x < 0x9f; x++) Video_PutPixel((u32)x, ay + 0x14, 1);
    if (g_Score[g_AeroPlayer] != DS32(0x902A8)) {
        Video_FillRect(0xab, 0x28, 0xd4, 0x2d, 0xe);
        Text_DrawNumber(0xac, 0x28, g_Score[g_AeroPlayer], 8);
        DS32(0x902A8) = g_Score[g_AeroPlayer];
    }
    if (g_Kills != DS32(0x902A4)) {              /* kill tally, strict '<' (Q21) */
        DS32(0x90478) = g_Kills;
        DS32(0x90448) = 0;
        Video_FillRect(0xa6, 0x38, 0xd4, 0x3c, 0xe);
        for (DS32(0x9048C) = 6; DS32(0x90478) > 0 && DS32(0x90448) < 0x34 && DS32(0x9048C) > -1; DS32(0x9048C)--)
            for (; g_KillTallyValue[DS32(0x9048C)] < DS32(0x90478) && DS32(0x90448) < 0x34; DS32(0x90448) += 7) {
                Sprite_DrawNowShift((u32)(DS32(0x90448) + 0xa4), 0x38, DS32(0x9048C) + 0x94);
                DS32(0x90478) -= g_KillTallyValue[DS32(0x9048C)];
            }
        DS32(0x902A4) = g_Kills;
    }
    if (g_DamageLampsDirty != 0) {
        draw_lamps();
        g_DamageLampsDirty = 0;
    }
}
