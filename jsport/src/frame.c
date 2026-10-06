/* The mission frame loop: port/spec/game_flow.md §8 (Game_Run 0x1cc2f..0x20db3), in the exact order of the
 * original's 93 steps (GF step numbers in the comments), with the flight model of player.md §3 inlined as in
 * the original; every subsystem is called at its original place;
 * the Rand calls the original makes *in Game_Run itself* (arguments of empty stubs, emitters, the wreck loop)
 * are all kept here. Float expressions follow the disassembly: each (float) is a rounding point. */
#include "mission.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "level.h"
#include "platform.h"
#include "sound.h"
#include "video.h"
#include "host.h"

#define g_CrashTimer g_AlienAbduct

static void swap_views(void)
{
    SwapInt(&g_CamX, &g_ViewX);
    SwapInt(&g_CamY, &g_ViewY);
    SwapInt(&g_PrevCamX, &DS32(0x8FFA4));
    SwapInt(&g_PrevCamY, &DS32(0x8FF50));
}

static int fbits(float f)
{
    int i;
    memcpy(&i, &f, sizeof i);
    return i;
}

/* Empty stubs of the original that only return a value (kept as values, their Rand arguments are kept at the
 * call sites): 0x11105 Stub_FrameD returns 0, 0x110e1 Stub_FrameC returns its first argument, 0x110bc
 * Stub_FrameB / 0x11653 Stub_FrameH / 0x112e1 Stub_FrameG return 0, 0x11678 Stub_FrameI returns 1. */
#define STUB_FRAME_D() (DS32(0x900FC) = 0)

/* 0x1adc9 FUN_0001adc9: fog palette refresh (GF step 65) */
void Fog_Refresh(void)
{
    DS32(0x90300) = DS32(0x90300) < 2 ? DS32(0x90300) : 1;
    if (g_FogActive != 0 && DS32(0x90300) == 1) {
        g_NightLevel = Clamp((g_CamY + 0x96) / 0x43 + 1, 0, 0xf);
        Pal_NightAltitude();
    }
}

static void player_sprite(void)                  /* GF step 24, player.md §9.1 */
{
    if (g_DeathTimer < 0x10) {
        if (IsOnScreen(g_CamX, g_CamY, g_PlayerWX, g_PlayerWY)) {
            if (g_AirframeType == 0) {
                DS32(0x8FF24) = 0;
                if ((g_BurnerFrames != 0 && (g_SpeedBits < 0x40800000 || g_Throttle < 8))
                    || DS32(0x90664) == 1 || DS32(0x90238) == 1)
                    DS32(0x8FF24) = 0x16;
                int g = g_GearDown - g_FixedGear;
                int id = g < 0 ? g_PlayerDirSprite[g_DirHalf * 2] : g_PlayerDirSprite[g_DirHalf * 2 + g];
                g_PlayerSpriteId = id + DS32(0x8FF24);
                Sprite_Queue(g_PlayerWX - g_CamX, g_PlayerWY - g_CamY, g_PlayerSpriteId);
            } else {
                DS32(0x9016C) = g_DirHalf;
                if ((g_Ctrl[7] != 0 || g_Ctrl[10] != 0 || (g_Ctrl[2] != 0 && g_Ctrl[5] != 0)) && g_DirHalf == 3
                    && g_PlayerVX == 0 && g_OnGround == 0 && g_IsHeli == 1)
                    g_DirHalf = 7;
                int g = g_GearDown - g_FixedGear;
                DS32(0x9339C) = Player_DrawHeli(g_PlayerWX - g_CamX, g_PlayerWY - g_CamY, g_IsHeli, DS32(0x907A4), g_DirHalf,
                                                g < 0 ? 0 : g, g_FrameParity);
                g_DirHalf = DS32(0x9016C);
            }
            if (g_HasAfterburner == 1 && g_Throttle > 6) Player_AfterburnerFlame();
            if (g_DirHalf == 0 || g_DirHalf == 0x10) Player_DrawReverseThrust();
        }
    } else {
        Debris_Update();
    }
}

static void lightning(void)                      /* GF step 48, video.md §7.1 */
{
    int flash = 0;
    if (g_NightMission == 0 || g_LightningCooldown != 0) {
        if (g_KeyDown[0x3d] != 0 && g_LightningCooldown == 0) flash = 1;      /* F3 */
    } else if (Rand(1000) == 1) {
        flash = 1;
    } else if (g_KeyDown[0x3d] != 0 && g_LightningCooldown == 0) {
        flash = 1;
    }
    if (!flash) return;
    if (g_DetailParallax != 0)
        for (DS32(0x90470) = 0; DS32(0x90470) < 0x30; DS32(0x90470)++) {}
    g_LightningCooldown = 0x32;
    Sfx_Play(0x12, 0x157c, 0x3f, g_CamX + g_PlayerScrX);
    g_BoltX = Rand(200 + VIEW_EXTRA_COLS) + 0x2c;          /* ENH: view: anywhere across the view */
    g_BoltY = g_BackPage / VRAM_ROWB;
    for (;;) {
        if (Rand(0x28) + 0x3c <= g_BoltY) break;      /* page B (row 322): no bolt (video.md Q6) */
        int y = g_BoltY + 0x14;
        DS32(0x8FFB4) = y + Rand(0x29);
        int a = Rand(0x14);
        int x = g_BoltX + a;
        int b = Rand(0x14);
        DS32(0x8FFAC) = Clamp(x - b, 0, 0x140 + VIEW_EXTRA_COLS);   /* ENH: view */
        Video_DrawLine((u32)g_BoltX, (u32)g_BoltY, (u32)DS32(0x8FFAC), (u32)DS32(0x8FFB4), 0xff);
        g_BoltX = DS32(0x8FFAC);
        g_BoltY = DS32(0x8FFB4);
    }
    if (g_DetailParallax == 0) {
        Pal_SetColor12(0x40, (u32)g_FlashTable[g_FlashCounter]);
        g_FlashCounter = 0x15;
    } else {
        for (DS32(0x90470) = 0xc4; DS32(0x90470) < 0xd4; DS32(0x90470)++) Pal_SetColor(DS32(0x90470), 0x3f, 0x3f, 0x3f);
        g_FlashCounter = 0x65;
    }
}

static void ground_probes(void)                  /* GF step 63 */
{
    DS32(0x905E8) = g_ScrollFineY;
    if (g_CamY > 0x33f) DS32(0x905E8) = 0x10;
    g_TrigCol = Div16(g_CamX + g_PlayerScrX) + 1;
    if (g_MapWidth <= g_TrigCol) g_TrigCol -= g_MapWidth;
    DS32(0x909AC) = g_CamY + g_PlayerScrY + g_GearHeight * g_GearDown;
    s32 *row = &DS32(0x8FF1C);
    *row = Div16(DS32(0x909AC)) + 1 < 0x40 ? Div16(DS32(0x909AC)) + 1 : 0x3f;
    if (g_TrigCol < 0 || g_MapWidth <= g_TrigCol || *row < 0xb || g_Crashed != 0) {
        DS32(0x90000) = 0; DS32(0x90004) = 0; DS32(0x90008) = 0;
        g_GroundAttr = 0; g_HitAttr = 0; *row = 0;
        return;
    }
    DS32(0x9028C) = g_EjectState == 0 ? 1 : 1;  /* Stub_FrameI(scrX+fineX-4, scrY+fineY+2) returns 1 */
    if (g_OnGround != 0) {
        g_DeckAttr = Map_GetTileAttr(g_TrigCol, *row + 1 < 0x40 ? *row + 1 : 0x3f, 3);
        DS32(0x9005C) = Map_GetTileAttr(g_TrigCol - 2 < 0 ? 0 : g_TrigCol - 2, *row, 0);
    }
    DS32(0x90000) = Map_GetTileAttr(g_TrigCol, *row - 1 < 0 ? 0 : *row - 1, 0);
    DS32(0x90004) = Map_GetTileAttr(g_TrigCol, *row, 0) * DS32(0x9028C);
    DS32(0x90008) = Map_GetTileAttr(g_TrigCol, *row + 1 < 0x40 ? *row + 1 : 0x3f, 0);
    DS32(0x90A60) = DS32(0x909AC) & 0xf;
    if (DS32(0x90A60) > 0xd) g_GroundAttr = DS32(0x90008);
    if (DS32(0x90A60) < 3) {
        g_HitAttr = DS32(0x90000);
        g_GroundAttr = DS32(0x90004);
    } else {
        g_HitAttr = DS32(0x90004);
    }
}

static void flight_velocity(void)                /* GF step 67, player.md §3.1 */
{
    int d = g_DirHalf;
    if (g_Crashed == 0) {
        if (g_IsHeli == 0) {
            float t = (float)g_DirVX[d] * g_Speed;
            g_VXError = (s32)(t - (float)g_PlayerVX);
            if (g_SpeedBits < 0x40800000 || g_Throttle < 9)
                g_PlayerVX += Clamp(g_VXError, g_WingAuthority * -2 + g_Overloaded, g_WingAuthority * 2 - g_Overloaded);
            else
                g_PlayerVX += Clamp(g_VXError, g_WingAuthority * -2 + g_Overloaded + g_BurnerFrames,
                                    (g_WingAuthority * 2 - g_Overloaded) - g_BurnerFrames);
            g_PlayerVX = Clamp(g_PlayerVX, -0x10, 0x10);
            g_LiftVY = (s32)((float)g_DirLift[d] * g_Speed);
            if (g_Speed < g_CruiseSpeed && g_LiftVY < 0)
                g_LiftVY = (s32)(((float)g_LiftVY * g_Speed) / g_CruiseSpeed);
            g_TargetVY = 0;
            if (g_OnGround == 0 && g_Speed < g_CruiseSpeed) g_TargetVY = 1;
            g_TargetVY = Clamp(g_TargetVY + g_StallSink + g_LiftVY, -0x10, 0x10);
            if (g_SpeedBits < 0x40800000 || g_Throttle < 8)
                g_PlayerVY += Clamp(g_TargetVY - g_PlayerVY, g_Overloaded - g_WingAuthority, g_MaxSinkAccel);
            else
                g_PlayerVY += Clamp(g_TargetVY - g_PlayerVY, (g_Overloaded - g_WingAuthority) + g_BurnerFrames, g_MaxSinkAccel);
        } else {
            DS32(0x90784) = g_HeliVX;
            g_PlayerVX += Sign(g_HeliVX - g_PlayerVX);
            g_PlayerVY = -0x10;
        }
    } else {
        g_HeliLift = g_HeliLift - 2 < 0 ? 0 : g_HeliLift - 2;
        if (Rand(4) == 1) g_PlayerVX -= Sign(g_PlayerVX);
        if (g_IsHeli == 0) g_PlayerVY = g_PlayerVY + 2 < 0x10 ? g_PlayerVY + 2 : 0x10;
        else g_PlayerVY = g_PlayerVY - 2 < -0x10 ? -0x10 : g_PlayerVY - 2;
    }
}

static void flight_move(void)                    /* GF step 68, player.md §3.2 */
{
    g_CamX += g_PlayerVX;
    if (abs(g_PlayerVX) == 0x10 && g_SpeedBits > 0x40a00000)
        g_PlayerScrX = Clamp(g_PlayerScrX - Sign(g_PlayerVX) * 4, 0x20 + VIEW_EXTRA_COLS / 2, 0x120 + VIEW_EXTRA_COLS / 2);
    else if (g_TaxiStopTimer == 0)
        g_PlayerScrX -= Sign(g_PlayerScrX - (0xa0 + VIEW_EXTRA_COLS / 2)) * 4;      /* ENH: view */
    if (g_IsHeli == 0) {
        if (g_OnGround == 0 || g_PlayerVY < 0) {
            if (g_CamY < CAM_Y_MAX) {          /* ENH: view */
                g_CamY += Clamp(g_PlayerVY, -0x10, 0x10);
            } else {
                g_Scratch690 = g_PlayerVY;
                if (abs(g_PlayerVY) > 2)
                    g_Scratch690 = (s32)((((double)(abs(g_PlayerVY) - 2) * 60.0) / (double)g_PlayerScrY + 2.0)
                                         * (double)Sign(g_PlayerVY));
                g_PlayerScrY += Clamp(g_Scratch690, -0xe, 0xe);
            }
        }
    } else {
        int v = ((g_HeliLift - 5) * g_PlayerVY) / 10;
        if (g_OnGround == 0 || v < 0) {
            if (g_CamY < CAM_Y_MAX) g_CamY += Clamp(v, -0x10, 0x10);      /* ENH: view */
            else g_PlayerScrY += Clamp(v, -0x10, 0x10);
        }
    }
}

static void flight_ground_contact(void)          /* GF step 69, player.md §3.3 */
{
    int d = g_DirHalf;
    int px = g_CamX + g_PlayerScrX;
    if (g_TowX > 0 && (g_TowX < px - 0x50 || g_OnGround == 0) && g_TowFrame > -0x11) g_TowX = px - 0x60;   /* Q1 */
    int k = (d + g_IsHeli * 0x10) * 2;
    if (g_PlayerDirSprite[k] == g_PlayerDirSprite[k + 1] || (g_Dir16Y[d / 2] > 0 && g_IsHeli != 1)) g_WheelsOk = 0;
    else g_WheelsOk = 1;
    if (g_IsHeli == 1) g_WheelsOk = abs(g_HeliVX) < 0xd ? 1 : 2;
    g_OnGround = 0;
    int py = g_CamY + g_PlayerScrY;
    /* PY <= 0x3e1 - BaseYOff (player.md Correction 1: the spec had '>') */
    g_OverRunway = (g_BaseStartX < px && px < g_BaseEndX && py <= 0x3e1 - g_BaseYOff) ? -1 : 0;
    if (0x81 - g_LandAttrLow <= g_GroundAttr && g_GroundAttr <= g_Seaplane + 0x81
        && (g_GearDown == 1 || (g_Seaplane == 1 && g_GroundAttr == 0x82))
        && (g_StallSink == 0 || (g_StallSink < 5 && py - 0x11 < g_StallTopY))
        && g_WheelsOk == 1
        && (g_PlayerVY < 1 || g_SpeedBits < 0x40400000 || g_IsHeli == 1)
        && g_Crashed == 0)
        g_OnGround = 1;
    if (g_OnGround == 1 && g_StallSink > 0) {
        g_StallSink = 0;
        Player_GearCollapseRoll();
        g_StallSink = 0;
    }
    if (0x7f < g_GroundAttr && g_GroundAttr < 0x8c && g_OnGround == 0 && (g_GearDown == 0 || g_FixedGear == 1)
        && g_StallSink == 0 && (g_OverRunway == 0 || g_GroundAttr == 0x81) && g_WheelsOk == 1
        && (g_PlayerVY < 1 || g_SpeedBits < 0x40400000 || g_IsHeli == 1) && g_Crashed == 0) {
        g_OnGround = 8;
        g_Speed = (float)((double)g_Speed + -0.02);
        if (g_SpeedBits < 0) g_Speed = 0;
        if (Rand(3) == 1) {
            g_DamageHits = 1;
            Player_DamageSystems();
        }
    }
    if ((0x3df < py || (0x7e < g_HitAttr && g_OverRunway == 0)) && g_Crashed == 0 && g_OnGround == 0) {
        g_Crashed = 1;
        g_DamageHits = 4;
        Hud_LampBlinkB();
        Hud_LampBlinkA();
        Player_DamageSystems();
        for (g_LoopI = 0; g_LoopI < 5; g_LoopI++) {
            int life = Rand(1);
            int y = ((Rand(0x10) + g_CamY + g_PlayerScrY) << 8) - 0x800;
            int x = ((Rand(0x10) + g_CamX + g_PlayerScrX) << 8) - 0x800;
            Particle_Spawn(x, y, 0, 0, 0, 0x1f4, life);
        }
        Explosion_Damage(g_CamX + g_PlayerScrX, g_CamY + g_PlayerScrY, g_PlayerVX, 0, g_CrashBlast, g_CrashBlast);
    }
    if (g_OnGround == 1) Mission_CheckComplete();
    else DS32(0x907C0) = 0;
    if (g_TaxiStopTimer > 0) {
        int old = g_PlayerScrX;
        g_Speed = (float)((double)g_Speed + -0.5);
        if (g_SpeedBits < 0) g_Speed = 0;
        g_PlayerScrX--;
        if (--g_TaxiStopTimer == 0 || (g_SpeedBits == 0 && abs(old - (0x95 + VIEW_EXTRA_COLS / 2)) < 0x10)) {
            g_PlayerScrX = 0x94 + VIEW_EXTRA_COLS / 2;                   /* ENH: view */
            g_Speed = 0;
            g_TaxiStopTimer = 0;
        }
    }
}

static void flight_speed(void)                   /* GF step 70, player.md §3.4/3.5 */
{
    int d = g_DirHalf;
    int px = g_CamX + g_PlayerScrX;
    if (g_Crashed == 1 && g_CrashAttr == 0) {
        g_CrashAttr = g_GroundAttr;
        g_CrashTileAttr = Map_GetTileAttr(Clamp(Div16(px + g_ScrollFineX), 0, g_MapWidth - 1), 0x3f, 0);
        g_CrashDir = d / 4;
    }
    if (g_OnGround > 0 && (d > 0x1d || d == 0x11 || d == 0x12) && g_IsHeli == 0 && g_SpeedBits < 0x40800000) {
        int r = (d == 0x11 || d == 0x12);
        g_Dir = r * 0x20 + g_ParkAttitude * -2 + (1 - r) * 4 * g_ParkAttitude;
        g_DirHalf = g_Dir / 2;
        g_LoopI = (s32)(Byte_Get(g_MapVal, Div16(px) + 0x400) & 0xff) * 0x10;
        g_PlayerScrY = g_OnGround == 1 ? (g_LoopI - g_GearHeight) - g_CamY : g_LoopI - g_CamY;
    }
    if (g_OnGround != 0 && g_SpeedBits == 0 && g_ReverseThrust > 0) g_ReverseThrust = 1;
    g_GroundParked = 0;
    if (g_OnGround == 1 && g_SpeedBits < 0x40000000 && g_IsHeli == 0) {
        g_Dir = (g_DirHalf < 5 || g_DirHalf > 0x1d) ? 0 : 0x20;
        g_Dir += g_ParkAttitude * -2;
        if (g_DirHalf < 4) g_Dir += g_ParkAttitude * 4;
        g_GroundParked = 1;
        g_StallSink = 0;
    }
    if (g_EjectState != 0 && Rand(1) != 0 && --g_Throttle < 0) g_Throttle = 0;
    d = g_DirHalf;
    if (g_IsHeli == 0) {
        g_Fuel = (g_Fuel - g_Throttle) + g_FuelLeaks * -100;
        if (g_Fuel < 0) g_Fuel = 0;
        if (g_IsGlider == 1) {
            Player_GliderUpdate();
        } else {
            if (g_DeckAttr < 0xfd)
                g_EffThrottle = (g_Throttle + g_EngineFire * -2) - g_OnGround * (g_ParkAttitude * -3 + 7);
            else
                g_EffThrottle = (g_Throttle + g_EngineFire * -2) - g_OnGround * ((g_ParkAttitude + 1) * -3 + 7);
            if (g_EffThrottle < 0) g_EffThrottle = 0;
            g_SpeedTmp = (float)g_EffThrottle * g_Thrust;
            g_SpeedTmp = (float)((double)g_SpeedTmp / ((double)(float)g_OnGround + 1.0));
            g_SpeedTmp = (float)((double)g_SpeedTmp * ((double)(g_CamY + 0x800) / 2700.0));
            g_SpeedTmp = (float)((double)g_SpeedTmp - ((double)g_OnGround * 0.001) * (double)g_WingAuthority);
            if (g_ReverseThrust == 2)
                g_SpeedTmp = (float)((double)g_SpeedTmp - (double)(((float)g_BrakeDrag + g_Drag) + 5.0f) * 0.0005);
            else
                g_SpeedTmp = (float)((double)g_SpeedTmp - (double)((float)g_BrakeDrag + g_Drag) * 0.0005);
            if (g_OnGround == 0) g_SpeedTmp = (float)((double)g_SpeedTmp + (double)g_DirLift[d] * 0.15);
            g_SpeedTmp = (float)((double)g_SpeedTmp - (0.002 - (double)(float)g_GearDown * 0.002));   /* Q18 */
            g_Speed = g_Speed + g_SpeedTmp;
            if (g_DirLift[d] < 1) g_SpeedTmp = g_MaxSpeedTab[g_Throttle];
            else g_SpeedTmp = g_MaxSpeedTab[g_Throttle] + 6.0f;
            if (fbits(g_SpeedTmp) > 0x40c00000) g_SpeedTmp = 6.0f;
            g_SpeedTmp2 = (float)g_RocketBoost + g_Speed;
            if (g_SpeedTmp > g_SpeedTmp2) g_SpeedTmp = g_SpeedTmp2;
            if (fbits(g_SpeedTmp) < 0) g_Speed = 0;
            else g_Speed = g_SpeedTmp;
            if (g_CatapultTimer > 0) {
                g_Speed = g_Speed + 3.0f;
                if (g_SpeedBits > 0x40c00000) g_Speed = 3.0f;                 /* Q2 */
                g_CatapultTimer--;
            }
        }
    } else {
        g_StallTopY = 0;
        int burn = (g_FuelLeaks * 100 + g_Throttle < 8) ? 8 : g_Throttle + g_FuelLeaks * 100;
        g_Fuel = (g_Fuel - burn < 0) ? 0 : g_Fuel - burn;
        g_StallSink = 0;
        g_Speed = (float)Clamp((s32)((double)(abs(g_HeliVX) * 6) * 0.0625), 0, 6);
        if (g_Ctrl[8] == 0 && g_Ctrl[9] == 0) g_HeliVX -= Sign(g_HeliVX) * g_FrameParity;
    }
    /* engine out (player.md §3.4) */
    if (g_Fuel == 0 || g_DamageFlags[1] != 0 || g_DamageFlags[11] != 0) {
        g_EngineWarnCol += Sign(g_EngineWarnDir) * 4;
        int v = g_EngineWarnCol < 0x21 ? 0x20 : g_EngineWarnCol;
        v = v < 0x3f ? v : 0x3f;
        g_EngineWarnCol = v;
        if (v == 0x20) g_EngineWarnDir = 1;
        if (v == 0x3f) g_EngineWarnDir = -1;
        Pal_SetColor(0x20, v & 0xff, 0, 0);
        g_Throttle = 0;
        g_HeliLift -= 3;
        if (g_HeliLift < 4) g_HeliLift = 4;
        if (g_FrameCounter50 == 0 && g_IsGlider == 0) Sfx_Play(9, 10000, 0x3f, g_CamX + g_PlayerScrX);
        if (g_IsHeli == 1) {
            g_Speed = (float)((double)g_Speed + -1.0);
            if (g_SpeedBits > 0x40c00000) g_Speed = 6.0f;
            if (g_SpeedBits < 0) g_Speed = 0;
        }
    }
    /* stall (player.md §3.5) */
    if (g_OnGround == 0) {
        g_PlaneFlown[g_PlaneSel - 1] = 1;
        DS32(0x902B0) = 1;
        g_CatapultCount = 0;
        g_ReverseThrust = Sign(g_ReverseThrust);
        int stall = (g_Speed < g_StallSpeed * 6.0f);
        if (!stall) {
            int sv = Sign(g_PlayerVX);
            int se = Sign(g_VXError);
            stall = (se == -sv && abs(g_VXError - g_PlayerVX) > 0xf);
        }
        if (stall && g_IsHeli == 0) {
            if (g_CamY + g_PlayerScrY < g_StallTopY) g_StallTopY = g_CamY + g_PlayerScrY;
            g_StallSink++;
            if (g_StallLimit / 16 < g_StallSink) g_StallSink = g_StallLimit / 16;
            if (g_StallLimit / 16 == g_StallSink && ++g_StallLimit > 0xc0) g_StallLimit = 0xc0;
        }
    }
    if ((double)g_Speed < (double)g_CruiseSpeed * 0.5 && g_OnGround == 0) {
        g_Speed = (float)((double)g_Speed + -0.01);
        if (g_SpeedBits < 0) g_Speed = 0;
    }
    if (!(g_Speed < g_StallSpeed * 6.0f)) {
        int se = Sign(g_VXError);
        int sv = Sign(g_PlayerVX);
        if ((se == sv || abs(g_VXError - g_PlayerVX) <= 0xf) && g_IsHeli == 0) {
            if (--g_StallSink < 0) g_StallSink = 0;
            g_StallLimit -= 0x10;
            if (g_StallLimit < 0x40) g_StallLimit = 0x40;
            g_StallTopY = 9999;
        }
    }
}

static void controls(void)                       /* GF step 80, player.md §4 */
{
    if (g_Crashed != 0 || g_EjectState != 0 || g_EngineFire >= 3) return;
    g_PrevDir = g_Dir;
    g_LastAction = g_KeyCode;
    g_KeyCode = Input_GetKeyCode_Stub();
    if (g_DamageFlags[1] == 0 && g_DamageFlags[11] == 0)
        for (DS32(0x90470) = 2; DS32(0x90470) < 0xc; DS32(0x90470)++)
            if (g_KeyDown[DS32(0x90470)] != 0) g_Throttle = DS32(0x90470) - 2;   /* keys 1..9, 0 */
    if (g_DamageFlags[1] != 0 && g_Throttle > 0) g_Throttle--;
    if (DS32(0x909DC) == 1 && g_IsHeli == 0) {
        if (g_Ctrl[2] == 0) {
            g_AimFrame = 0;
            g_Dir = (g_Dir + 0x40) % 0x40;
            DS32(0x90620) += (g_EngineTickRate * g_Throttle) / 2;
        }
        if (g_Ctrl[0] != 0 && g_Ctrl[1] == 0 && g_Ctrl[2] == 0) Player_ThrottleDown();
        if (g_Ctrl[1] != 0 && g_Ctrl[0] == 0 && g_Ctrl[2] == 0) Player_ThrottleUp();
    } else {
        if (g_Ctrl[0] != 0 && g_Ctrl[1] == 0 && g_Ctrl[2] == 0) Player_ThrottleDown();
        if (g_Ctrl[1] != 0 && g_Ctrl[0] == 0 && g_Ctrl[2] == 0) Player_ThrottleUp();
        if (g_Ctrl[8] != 0 && g_Ctrl[2] == 0 && (g_IsHeli == 1 || DS32(0x90468) == 1)) Player_PitchDown();
        if (g_Ctrl[9] != 0 && g_Ctrl[2] == 0 && (g_IsHeli == 1 || DS32(0x90468) == 1)) Player_PitchUp();
        if (g_Ctrl[7] == 0 && g_Ctrl[10] == 0 && g_IsHeli == 1 && g_Ctrl[2] == 0) {
            if (g_SpeedBits < 0x40800000) g_HeliLift += Sign(4 - g_HeliLift);
            else g_HeliLift = 5;
        }
        if (g_Ctrl[7] != 0 && g_GroundParked == 0 && g_Ctrl[2] == 0) Player_RotateA();
        if (g_Ctrl[10] != 0 && g_GroundParked == 0 && g_Ctrl[2] == 0) Player_RotateB();
        g_UpHeld = g_Ctrl[7];
        g_DownHeld = g_Ctrl[10];
    }
    if (g_OnGround == 1
        && (g_PlayerDirSprite[(g_Dir / 2) * 2] == g_PlayerDirSprite[(g_Dir / 2) * 2 + 1] || g_Dir16Y[g_Dir / 4] > 0)
        && g_IsHeli == 0)
        g_Dir = g_PrevDir;
    if (DS32(0x909A0) != 0 && g_IsHeli == 0) Player_PullUp();
    if (g_PrevDir != g_Dir && g_SpeedBits > 0x40a00000 && g_IsHeli == 0) g_WingVapour = 1;
}

static void auto_eject(void)                     /* GF step 79 */
{
    if ((g_EngineFire > 3 || g_Crashed > 0) && g_EjectState == 0 && g_AutoEjectOn == 1) {
        Hud_PushMessage(DSTR(0x8110D));                              /* "Autoejecting" */
        g_NextBonusScore += g_BonusScoreStep;
        if (g_NextBonusScore < g_Score[g_AeroPlayer] + 1) g_NextBonusScore = DS32A(0x8E334)[g_AeroPlayer];   /* sic */
        g_NextBonusScore += 1000;
        if (g_NextBonusScore < g_MissionBonus) g_NextBonusScore = g_MissionBonus;
        strcpy(DSTR(0x84A48), DSTR(0x8111A));                        /* "Next Bonus at " */
        itoa_js(g_NextBonusScore, DSTR(0x85548));
        strcat(DSTR(0x84A48), DSTR(0x85548));
        Hud_PushMessage(DSTR(0x84A48));
        g_EjectState = 1;
        g_TowState = -0x11;
        g_PendingWarnSfx = 0;
        g_EjectScrX = g_PlayerScrX;
        g_EjectScrY = g_PlayerScrY - 8;
        g_EjectCamX = g_CamX;
        g_EjectCamY = g_CamY;
        g_ChuteVY = -0x10;
        DS32(0x8FEF4) = 0x10;
        if (--g_PracticeEjects < 0) g_PracticeEjects = 0;
        int x = g_CamX + g_PlayerScrX;
        g_PilotX = Sign(g_PilotX) * x;
        g_AutoEjectOn = 0;
    }
}

static int loop_condition(void)                  /* game_flow.md §8.1 */
{
    return (g_DeathTimer < 0x20 || (DS32(0x90344) > 0 && g_DeathTimer < 0x50) || g_PlayerScrY < 0x9d + VIEW_EXTRA_ROWS || g_EjectState > 0)
        && g_EjectState < 99 && g_MissionResult < 2 && g_MissionActive == 1
        && (g_OnGround != 8 || DS32(0x9041C) > 7 || g_SpeedBits != 0 || g_EjectState != 0);
}

/* PORT (developer aid): JS_TRACE=<n> prints the player state every n frames to stdout. */
static void trace(void)
{
    static int every = -1, n = 0;
    if (every < 0) {
        const char *e = getenv("JS_TRACE");
        every = e ? atoi(e) : 0;
        if (every > 0)
            printf("base %d..%d yoff %d W %d stall %.2f cruise %.2f top %d thrust %.3f turn %d\n", g_BaseStartX, g_BaseEndX,
                   g_BaseYOff, g_MapWidth, (double)g_StallSpeed, (double)g_CruiseSpeed, g_TopSpeed, (double)g_Thrust, g_TurnRate);
    }
    if (every <= 0 || ++n % every != 0) return;
    printf("t%.2f ", host_script_seconds());
    printf("f%d cam %d,%d scr %d,%d spd %.3f dir %d thr %d vx %d vy %d gnd %d gear %d stall %d crash %d eject %d fuel %d res %d\n",
           n, g_CamX, g_CamY, g_PlayerScrX, g_PlayerScrY, (double)g_Speed, g_Dir, g_Throttle, g_PlayerVX, g_PlayerVY,
           g_OnGround, g_GearDown, g_StallSink, g_Crashed, g_EjectState, g_Fuel, g_MissionResult);
    printf("    air %d (%d,%d dm %d) gnd %d msl %d shell %d convoy %d tv %d flares %d bonus %d score %d kills %d armour %d "
           "fire %d lock %d cv0 %d,%d st %d tanker %d,%d f%d hose %d\n",
           g_EnemyAirCount, g_EnemyAirX[0], g_EnemyAirY[0], DS32A(0x92638)[0], g_EnemyGroundCount, DS32(0x906FC), DS32(0x8FFA0),
           DS32(0x905AC), DS32(0x900B4), DS32(0x90310), DS32(0x90890), g_Score[g_AeroPlayer], g_Kills, g_Armour,
           g_EngineFire, DS32(0x9042C), DS32A(0x91F74)[0], DS32A(0x91F98)[0], DS32A(0x91F2C)[0], DS32(0x8FF64), DS32(0x8FF6C),
           DS32(0x8FFC8), DS32(0x8FFC0));
    fflush(stdout);
}

/* Game_Run 0x1cc2f..0x20db3: the frame loop of one attempt. */
void Mission_Run(void)
{
    while (loop_condition()) {
        trace();
        /* 1 */ Video_FlipPage();
        /* 2 */ if (g_ViewX > 0 || g_ViewY > 0) swap_views();
        s32 oldCamX = g_CamX;
        /* 3 */ g_TargetMarkX = 0; g_TargetMarkY = 0;
        /* 4 */ if (g_EjectState != 0) {
            DS32(0x909B0) = g_CamX;
            g_CamX = g_EjectCamX;
            DS32(0x909B4) = g_CamY;
            g_CamY = g_EjectCamY;
            if (oldCamX / 32 == g_EjectCamX / 32) g_EjectCamX += 2;
        }
        /* 5 */ if (g_MapWidthPx < g_CamX) { g_CamX &= 0xf; g_ViewTarget = -1; }
        if (g_CamX < 1) { g_CamX = g_MapWidthPx - 0x10 + (g_CamX & 0xf); g_ViewTarget = -1; }
        /* 6 */ g_CamY = Clamp(g_CamY, -0x7d0, CAM_Y_MAX);                /* ENH: view */
        DS32(0x9039C) = g_CamY;
        if (DS32(0x9039C) > CAM_Y_MAX) DS32(0x9039C) = CAM_Y_MAX;
        if (DS32(0x9039C) < 0) DS32(0x9039C) = DS32(0x9039C) % 0x40;
        /* 7 */ DS32(0x904F0) = Div16(g_CamX);
        DS32(0x904F4) = Div16(DS32(0x9039C));                          /* 0x13810, same as Div16 */
        DS32(0x904C4) = (g_CamY + 0x800) / 16;
        /* 8 */ DS32(0x90604) = g_ScrollFineX;
        g_ScrollFineX = g_CamX & 0xf;
        g_ScrollFineY = g_CamY & 0xf;
        /* 9 */ DS32(0x907A4) = g_CamX - g_PrevCamX;
        DS32(0x907A8) = g_CamY - g_PrevCamY;
        DS32(0x9083C) = DS32(0x907A4);
        DS32(0x90840) = DS32(0x907A8);
        if (abs(DS32(0x907A4)) > 0x10 || abs(DS32(0x907A8)) > 0x10) {
            DS32(0x907A4) = DS32(0x907BC);
            DS32(0x907A8) = DS32(0x9076C);
        }
        /* 10 */ if (g_FogActive != 0) {
            g_NightLevel = Clamp((g_CamY + 0x96) / 0x43 + 1, 0, 0xf);
            if (g_NightLevel != g_NightLevelShown) Pal_NightAltitude();
        }
        /* 11 */ if (g_LightningCooldown != 0) g_LightningCooldown--;
        /* 12 */ if (DS32(0x90318) > 0) DS32(0x90318)--;            /* Frame_DecCounter318 0x1676a */
        /* 13 */ Level_DrawBackground(DS32(0x904F0) + 1, DS32(0x904F4) + 1);
        /* 14 */ if ((g_TrigCamCol != DS32(0x904F0) || g_TrigCamRow != DS32(0x904C4) || g_TrigCamCol == -1) && g_EjectState == 0) {
            if (abs(DS32(0x904F0) - g_TrigCamCol) > 0x10) g_TrigCamCol = DS32(0x904F0) - DS32(0x907BC);
            g_TrigCol = imod_js(Div16(g_ScrollFineX + g_CamX + g_PlayerScrX), g_MapWidth - 1, "Game_Run MP2");
            g_Scratch690 = (s32)(Byte_Get(g_MapMp2, g_TrigCol) & 0xff);
            DS32(0x90668) = 0; DS32(0x9010C) = 0; DS32(0x90268) = 0; g_RadarJammed = 0;
            if (g_Scratch690 != 0 && g_Crashed == 0) Map_TriggerColumn();
        }
        /* 15 */ DS32(0x907BC) = DS32(0x907A4);
        DS32(0x9076C) = DS32(0x907A8);
        g_PrevCamX = g_CamX;
        g_PrevCamY = g_CamY;
        g_TrigCamCol = DS32(0x904F0);
        g_TrigCamRow = DS32(0x904C4);
        if (g_EjectState != 0) { g_CamX = DS32(0x909B0); g_CamY = DS32(0x909B4); }
        /* 16 */ Sprite_ClearQueue();
        /* 17 */ DS32(0x90034) = g_GroundAttr; g_GroundAttr = 0; g_HitAttr = 0;
        /* 18 */ {
            s32 vx = g_ViewX, vy = g_ViewY;
            if (vx == 0 && vy == 0) { vx = g_CamX; vy = g_CamY; }
            g_PlayerWY = vy + g_PlayerScrY;
            g_PlayerWX = vx + g_PlayerScrX;
            g_PlayerScrYSave = g_PlayerScrY;
            g_PlayerScrXSave = g_PlayerScrX;
            if (g_EjectState > 0) {
                g_PlayerScrX = g_EjectScrX; g_PlayerScrY = g_EjectScrY;
                g_CamX = g_EjectCamX; g_CamY = g_EjectCamY;
            }
        }
        /* 19 */ if (DS32(0x8FFF8) > 0) DS32(0x8FFF8)--;
        /* 20 */ /* if (!g_FogActive) Video_NightStub(): empty */
        /* 21 */ if (DS32(0x901E4) != 0) BaseRadar_Update();
        /* 22 */ if (g_MarkerX != 0 && IsOnScreen(g_CamX, g_CamY, g_MarkerX, g_MarkerY))
            Sprite_Queue(g_MarkerX - g_CamX, g_MarkerY + (0xf - g_CamY), 0x1e8);
        /* 23 */ if (DS32(0x8FFC0) > 0 && g_ViewTarget == -1 && g_TankerType != 0 && g_DirHalf == 0)
            Sprite_Queue(g_PlayerScrX + g_ProbeDX, g_PlayerScrY + g_ProbeDY, 0xa9);
        /* 24 */ player_sprite();
        /* 25 */ /* Stub_FrameA (empty) */
        if (g_NightMission != 0) Runway_Update();
        /* Stub_FrameB returns 0: its branch (Rand(0), Sfx, damage) is dead */
        /* 26 */ DS32(0x9042C) = g_LockTarget; g_LockTarget = -1;
        /* 27 */ if (g_TankerType != 0) Tanker_Update();
        /* 28 */ if (g_TowX != 0 || DS32(0x90310) != 0 || g_MP_PickupCol != 0 || DS32(0x906DC) != 0 || DS32(0x90158) != 0
                     || DS32(0x90998) != 0 || DS32(0x90528) != 0)
            SupportAircraft_Update();
        /* 29 */ if (DS32(0x90608) != 0) Flamer_Update();
        if (DS32(0x906FC) != 0) EnemyMissiles_Update();
        if (g_EnemyGroundCount != 0) EnemyGround_Update();
        /* 30 */ if (g_EnemyAirCount == 0) {
            DS32(0x906A4) += Rand(2);
            if (Div16(g_CamY + g_PlayerScrY) < (s32)g_MP_CeilingRow) DS32(0x906A4)++;
            if (DS32(0x906A4) > 300 && (g_GameMode & 1) == 0) {
                DS32(0x90A5C) = Rand(2) + 1;
                EnemyBomber_Spawn();
                DS32(0x906A4) = 0;
            }
        } else {
            EnemyAir_Update();
        }
        /* 31 */ if (g_LauncherCol != 0) Building_Update();
        if (DS32(0x905AC) != 0) Convoy_Update();
        if (DS32(0x900B4) > 0) TargetVehicles_Update();
        /* 32 */ if (g_BaseStartX - 0x140 < g_CamX && g_CamX < g_BaseEndX + 0x260 && 0x330 - g_BaseYOff < g_CamY)
            AirbaseCrew_Update();
        /* 33 */ if (g_FrameParity == 0) Weapons_FrameDispensers();
        /* 34 */ if (g_AlienAbduct > 0) Alien_Update();
        /* 35 */ { Rand(9); Rand(2); }           /* arguments of the empty stub 0x110e1 (returns 0x900fc), Q20 */
        /* 36 */ Particles_Update(g_CamX, g_CamY);
        /* 37 */ if (DS32(0x90890) != 0) Bonus_Update();
        if (DS32(0x90918) > 0) Pickup_Update();
        if (DS32(0x90498) != 0) BaseRepair_Update();
        /* 38 */ if (g_OnGround == 1 && DS32(0x8FF08) == 0 && DS32(0x90414) == 0) Runway_SetEndTargets();
        /* 39 */ if (g_BaseStartX - 0x140 < g_CamX && g_CamX < g_BaseEndX + 0x260 && (g_OnGround == 1 || DS32(0x8FF08) == 1)) {
            Airbase_Update();
        }
        /* 40 */ if (DS32(0x904AC) == 1 && DS32(0x90498) == 0) {
            DS32(0x90498) = 1;
            DS32(0x90488) = g_BaseEndX;
            DS32(0x904AC) = 0;
        }
        if (g_MP_TargetMarker != 0) {
            g_TargetMarkX = ((s32)(g_MP_TargetX1 - g_MP_TargetX0) / 2 + g_MP_TargetX0) * 0x10;
            g_TargetMarkY = 0x3e0;
        }
        if (DS32(0x9010C) == 0) {                /* MP2 object 0x84 */
            if (DS32(0x900E8) > 0) { Map_SetTile(DS32(0x900E8), DS32(0x900EC), (u8)DS32(0x900E4)); DS32(0x900E8) = 0; }
        } else {
            SAM_Draw();
            SAM_Fire();
            g_TargetMarkX = DS32(0x9010C) << 4;
            g_TargetMarkY = DS32(0x90110) << 4;
        }
        if (DS32(0x90268) == 0 || g_EjectState != 0) {                         /* MP2 object 0x85 */
            if (DS32(0x90230) > 0) { Map_SetTile(DS32(0x90230), DS32(0x90234), (u8)DS32(0x9022C)); DS32(0x90230) = 0; }
        } else {
            DS32(0x90758) = 1;
            Gun_Draw();
            g_TargetMarkX = DS32(0x90268) << 4;
            g_TargetMarkY = DS32(0x9026C) << 4;
            if (g_Crashed == 0 && DS32(0x8FFA0) < 10 && (g_GameMode & 1) == 0) Gun_Fire();
        }
        if (DS32(0x90668) == 0 || g_EjectState != 0) {                         /* MP2 object 0x83 */
            if (DS32(0x9063C) > 0) { Map_SetTile(DS32(0x9063C), DS32(0x90640), (u8)DS32(0x90630)); DS32(0x9063C) = 0; }
        } else {
            DS32(0x90758) = 0;
            Flak_Draw();
            g_TargetMarkX = DS32(0x90668) << 4;
            g_TargetMarkY = DS32(0x90614) << 4;
            if (DS32(0x90660) == 0 && g_Crashed == 0) Flak_Fire();
        }
        /* 41 */ if (g_MarkerX != 0 && IsOnScreen(g_CamX, g_CamY, g_MarkerX + 0x29, g_MarkerY))
            Sprite_Queue(g_MarkerX + (0x29 - g_CamX), g_MarkerY + (0xf - g_CamY), 0x1e9);
        if (g_CrateX != 0) Crate_Update();
        if (DS32(0x90754) != 0) EnemyPilots_Update();
        if (g_AeroGateX[g_AeroGateIdx] != 0) Waypoint_Update();
        if (DS32(0x902E0) != 0) Commandos_Update();
        /* 42 */ Hud_DrawTargetArrow(g_CamX, g_CamY, g_TargetMarkX, g_TargetMarkY);
        /* 43 */ if (DS32(0x90660) != 0) DS32(0x90660)--;
        if (g_ProjCount != 0 && g_Crashed == 0 && g_EjectState == 0) Projectiles_Update();
        /* 44 */ g_FrameParity = 1 - g_FrameParity;
        if (DS32(0x901D8) != 0) DS32(0x901D8)--;
        g_FrameCounter50 = Wrap(g_FrameCounter50 + 1, 0, 0x31);
        /* 45 */ if (g_EjectState != 0) Player_EjectUpdate();
        if (g_EngineFire != 0) Player_EngineFire();
        if (DS32(0x9037C) != 0) {                /* B52 target set (weapons' g_MarkerX): start the B52 */
            if (DS32(0x90998) == 0) {
                DS32(0x90998) = 1; DS32(0x90970) = 0; DS32(0x9095C) = 0; DS32(0x90978) = 8;
                DS32(0x90984) = 0x3d34; DS32(0x90988) = -0x800;
            }
            Rand(9);
            Rand(5);
            STUB_FRAME_D();
        }
        if (DS32(0x90398) != 0) {                /* drop zone: start Fat Albert */
            if (DS32(0x90528) == 0) {
                DS32(0x90528) = 1; DS32(0x9050C) = 0; DS32(0x90548) = 0; DS32(0x9051C) = 0; DS32(0x904D8) = 0;
                DS32(0x90510) = 8; DS32(0x90518) = 0; DS32(0x904C8) = g_BaseEndX; DS32(0x904C0) = 0x3cb - g_BaseYOff;
                DS32(0x904B4) = 4;
            }
            Rand(9);
            Rand(5);
            STUB_FRAME_D();
        }
        if (g_TankerType == 0 && g_EnemyAirCount < 1 && g_MP_EnemyBaseCol == 0) {   /* the tanker */
            g_TankerType = 1;
            DS32(0x8FFBC) = 0; DS32(0x8FFF4) = 0; DS32(0x8FF60) = 0; DS32(0x8FF5C) = 8; DS32(0x8FFC8) = 0;
            DS32(0x8FF64) = g_BaseEndX; DS32(0x8FF6C) = 0x3cb - g_BaseYOff; DS32(0x8FF58) = 4;
        }
        if (DS32(0x905B0) != 0) {                /* g_GunFiring: smoke puff */
            DS32(0x905B0) = 0;
            Rand(3);
            STUB_FRAME_D();
        }
        if (g_WingVapour != 0 && g_EngineFire == 0 && g_Crashed == 0) {
            Rand(3);
            STUB_FRAME_D();
            g_WingVapour = 0;
        }
        if (g_RocketBoost != 0) Particle_Spawn(g_PlayerWX << 8, g_PlayerWY << 8, 0, -0x80, 0, 0x10, 0x18);
        if (DS32(0x907B0) > 0 && g_CamX < g_MapWidthPx - 0x140 && 0x140 < g_CamX && g_CamY + g_PlayerScrY < 0x3b6
            && g_OnGround == 0 && g_IsHeli == 0) {
            Rand(3);                             /* argument of the empty stub 0x1112a (Stub_FrameE) */
        }
        if (g_OnGround == 1) {
            if (DS32(0x90034) == 0x82)
                for (g_LoopI = 0; g_LoopI < 4; g_LoopI++) { Rand(3); Rand(3); Rand(8); STUB_FRAME_D(); }   /* splash */
            if (DS32(0x90414) == 0) {
                if (DS32(0x90034) == 0x81 && g_IsHeli == 0) {
                    DS32(0x90088) = 1;
                    Sfx_Play(7, 0x2ee0, 0x30, g_CamX + g_PlayerScrX);       /* touchdown */
                }
                for (g_LoopI = 0; g_LoopI < 6; g_LoopI++) { Rand(3); Rand(8); Rand(8); STUB_FRAME_D(); }   /* dust */
            }
        }
        if (g_CatapultTimer > 0)
            for (g_LoopI = 0; g_LoopI < 6; g_LoopI++) { Rand(3); Rand(8); Rand(8); STUB_FRAME_D(); }
        DS32(0x90414) = g_OnGround;
        /* 46 */ if (g_Crashed != 0 || g_EngineFire > 3) {         /* burning wreck: Rand order exact (Q20) */
            g_LoopI = 1;
            for (;;) {
                int lim;
                int c = g_PlaneClass * -8 + 10;
                if (c < Rand(7) + 4) lim = c;
                else lim = Rand(7) + 4;
                if (lim < g_LoopI) break;
                Rand(5); Rand(0x10); Rand(0x10); Rand(0x10); Rand(0x10);   /* arguments of Stub_FrameF 0x11012 */
                g_LoopI++;
            }
        }
        /* 47 */ if (g_EjectState != 0) {
            g_CamX = g_PlayerWX - g_PlayerScrXSave;
            g_PlayerScrX = g_PlayerScrXSave;
            g_CamY = g_PlayerWY - g_PlayerScrYSave;
            if (g_CamY > CAM_Y_MAX) g_CamY = CAM_Y_MAX;                 /* ENH: view */
            g_PlayerScrY = g_PlayerScrYSave;
        }
        /* 48 */ lightning();
        /* 49a */ if (g_GunTrigger != 0 && g_GearDown == g_FixedGear && g_Crashed == 0 && g_EjectState == 0 && g_OnGround == 0)
            Player_Weapons();
        /* 49b */ /* Stub_FrameL / Stub_FrameK / Stub_FrameG: empty */
        /* 50 */ Sprite_DrawQueue();
        /* 51 */ if (DS32(0x8FFA0) != 0) EnemyShells_Update();
        Bullets_Update(g_CamX, g_CamY);
        /* Stub_FrameJ: empty */
        /* 52 */ if (g_HudMsgCount != 0) Hud_DrawMessages();
        /* 53 */ if (g_AgentDropPending != 0 && Div16(g_CamX + g_PlayerScrX) == (s32)g_MP_TargetX0 + 4) AgentDrop_Release();
        /* 54 */ if (g_SecExpCount != 0) { Map_SecondaryExplosions(); g_SecExpCount = 0; }
        /* 55 */ if (g_MP_ReconCol != 0 && (s32)g_MP_ReconCol - 1 <= g_TrigCol && g_TrigCol <= (s32)g_MP_ReconCol + 1
                     && (s32)g_MP_ReconRow * 0x10 <= g_CamY + g_PlayerScrY) {
            g_MP_ReconCol = 0;
            g_MP_ReconRow = 0;
            Hud_PushMessage(g_GameMode == 3 ? DSTR(0x810FD) : HUDTEXT(0));  /* "Mind Your Head!" / RECON PHOTOS TAKEN */
            Recon_PhotoCheck();
        }
        /* 56 */ /* Stub_FrameH returns 0: the branch with Rand(9) is dead */
        /* 57 */ if (DS32(0x902EC) != 0 && g_FrameParity == 0) {
            DS32(0x902EC)--;
            Sfx_Play(0xe, 0x2328, 0x3f, g_CamX + g_PlayerScrX);
        }
        if (DS32(0x8FFF8) == 4) Sfx_Play(4, 0x61a8, 0x3f, g_CamX + g_PlayerScrX);
        if (DS32(0x901C8) != 0) BaseHit_Losses();
        /* 58 */ g_Overloaded = (g_WingAuthority < 2 || g_LoadWeight <= g_MaxLoadWeight) ? 0 : 1;
        /* 59 */ /* if (0x90af4 && g_ProjCount < 8) Stub_FrameM(): empty */
        /* 60 */ if (g_ViewX > 0 || g_ViewY > 0) swap_views();
        /* 61 */ if (DS32(0x9068C) > 0) {
            DS32(0x9068C) -= 0x888;
            if (DS32(0x9068C) < 0) DS32(0x9068C) = 0;
            Pal_SetColor12(0x1a, (u32)DS32(0x9068C));
        }
        /* 62 */ DS32(0x90620) += (g_EngineTickRate * g_Throttle) / 2;
        if (g_NavLightBlink != 0) DS32(0x90238) = 1 - DS32(0x90238);
        if (DS32(0x90620) > 0x13) {
            DS32(0x90664) = 1 - DS32(0x90664);
            DS32(0x90620) = 0;
            if (DS32(0x90664) == 1) Sfx_LaunchThump();
        }
        /* 63 */ ground_probes();
        /* 64 */ if (g_FrameParity == 1 && g_Crashed == 0) Hud_UpdateRadar();
        /* 65 */ if (DS32(0x9046C) == 1 && g_MissionActive == 1) {
            DS32(0x90300) = 1;
            Fog_Refresh();
            DS32(0x9046C) = 0;
            Sfx_MissionEvent();
        }
        /* 66 */ if (DS32(0x8FF34) != 0 && --DS32(0x8FF34) == 0 && DS32(0x90190) == 1)
            Sfx_Play(0x17, 0x1004, 0x3f, g_CamX + g_PlayerScrX);
        /* 67 */ flight_velocity();
        /* 68 */ flight_move();
        /* 69 */ flight_ground_contact();
        /* 70 */ flight_speed();
        /* 71 */ if (g_OnGround == 8) Player_Ditching();
        /* 72 */ if (g_AutoThrottle != 0 && g_DamageFlags[1] == 0) Player_AutoThrottle();
        /* 73 */ if (g_StallSink > 0) {
            if (DS32(0x909A0) != 0) Player_FullPower();
            if (g_StallNoseDrop != 0) {
                if (g_DirHalf < 8 || g_DirHalf > 0x18) g_Dir--;
                if (g_DirHalf > 7 && g_DirHalf < 0x18) g_Dir++;          /* can reach 64 (Q3) */
                if (g_Dir < 0) g_Dir = 0x3f;
            }
        }
        /* 74 */ if (g_RocketBoost != 0 && --g_RocketBoostTimer == 0) g_RocketBoost = 0;
        /* 75 */ if (g_NextBonusScore <= g_Score[g_AeroPlayer] && DS32(0x90890) == 0 && g_OnGround == 0) {
            DS32(0x909A8) = g_CamX + g_PlayerScrX;
            DS32(0x909AC) = g_CamY - 300;
            Bonus_Spawn();
        }
        /* 76 */ if (g_TowX == 0 || g_TowState == -0x11) Player_Update();
        /* 77 */ if (g_GunTrigger != 0 && g_TowState != 0 && g_OnGround == 0 && g_TowState != -0x11) g_TowState = -0x11;
        /* 78 */ if (g_GearDown - g_FixedGear == 1 && g_IsHeli == 0 && g_DamageFlags[2] == 0) Player_TakeoffAssist();
        /* 79 */ auto_eject();
        /* 80 */ controls();
        /* 81 */ Engine_SoundUpdate();
        /* 82 */ if (g_CamY < 0) {
            if (Rand(400000) == 0x144fd && g_AlienAbduct == 0) g_AlienAbduct = 0;   /* sic: no-op (Q21) */
        }
        /* 83 */ if (g_MissionResult == 1 && g_GameMode == 0 && DS32(0x90918) == 0 && g_Mission == DS32A(0x90E8C)[DS32(0x9084C)]) {
            DS32(0x90918) = 2;
            DS32(0x9092C) = (g_BaseEndX - g_BaseStartX) / 2 + g_BaseStartX;
            DS32(0x90930) = 0x3e0;
            Hud_PushMessage(DSTR(0x8BF08));                          /* "PRIZE BALLOON LAUNCHED !" */
        }
        if (g_GameMode == 3 && DS32(0x8FFF0) / 2 < DS32(0x8FFEC) && DS32(0x90918) == 0 && DS32(0x90910) > 0) {
            DS32(0x90918) = 1;
            DS32(0x9092C) = DS32(0x90910);
            DS32(0x90930) = 0x3e0;
            DS32(0x90910) = 0;
        }
        /* 84 */ g_DirHalf = g_Dir / 2;
        g_FireHeldGearDown = 0;
        g_FireLatch = 0;
        /* 85 */ if (g_EjectState == 0 && g_Crashed == 0) {
            if (g_FireOrConfirm != 0 && g_GearDown - g_FixedGear == 1 && g_IsHeli == 0) g_FireHeldGearDown = 1;
            if (g_FireLeftReq != 0 && g_GearDown == g_FixedGear && DS32(0x90624) == 0 && g_DamageFlags[3] == 0
                && g_RackRounds[0] > 0 && g_OnGround == 0 && g_Ctrl[2] == 0) {
                DS32(0x8FF00) = 1; g_RackSel = 0; Weapon_Fire();
                DS32(0x8FF00) = 0; g_RackSel = 0; Weapon_Fire();
            }
            if (g_FireRightReq != 0 && g_GearDown == g_FixedGear && DS32(0x90624) == 0 && g_DamageFlags[4] == 0
                && g_RackRounds[1] > 0 && g_OnGround == 0 && g_Ctrl[2] == 0) {
                DS32(0x8FF00) = 1; g_RackSel = 1; Weapon_Fire();
                DS32(0x8FF00) = 0; g_RackSel = 1; Weapon_Fire();
            }
            if (g_FireLeftReq == 0 && g_FireRightReq == 0) DS32(0x905E0) = 0;
            if (g_FireLeftReq != 0) g_FireLeftReq = 0;
            if (g_FireRightReq != 0) g_FireRightReq = 0;
            DS32(0x90624) = 0;
            DS32(0x9067C) = 0;
            if (g_FireOrConfirm != 0) DS32(0x9067C) = 1;
        }
        /* 86 */ DS32(0x905B4) -= Sign(DS32(0x905B4));
        {   /* ENH: view: the box moves down by half the extra rows at the top (the plane keeps its place in the
             * height) and by all of them at the bottom (the ground line at the lowest camera) */
            s32 top = 0x50 + VIEW_EXTRA_ROWS / 2, bot = 0xa0 + VIEW_EXTRA_ROWS;
            if (g_PlayerScrY < top) { g_CamY += g_PlayerScrY - top; g_PlayerScrY = top; }
            if (g_PlayerScrY > bot) { g_CamY += g_PlayerScrY - bot; g_PlayerScrY = bot; }
        }
        /* 87 */ if (DS32(0x90114) > 0 && Rand(100) == 1 && DS32(0x90158) == 0) {
            DS32(0x90158) = 1; DS32(0x900CC) = 0; DS32(0x900D0) = DS32(0x90114); DS32(0x900C8) = 0;
            DS32(0x900BC) = -1; DS32(0x900D4) = 0x3df;
        }
        /* 88 */ if (g_GameMode == 0 && DS32(0x906DC) == 0 && DS32(0x90728) == 0) {
            DS32(0x90760) += DS32(0x8FFEC) - DS32(0x9071C);
            DS32(0x9071C) = DS32(0x8FFEC);
            if (DS32(0x90760) > 20000) {
                DS32(0x906DC) = 1; DS32(0x9073C) = 1; DS32(0x90740) = -0x578; DS32(0x90728) = 0; DS32(0x90750) = 0;
                DS32(0x9072C) = 0x14; DS32(0x90734) = 8; DS32(0x90730) = 0;
                if (g_EnemyAirCount == 0) {
                    DS32(0x90A5C) = 2;
                    EnemyBomber_Spawn();
                }
            }
        }
        /* 89 */ if (g_GameMode == 3) {
            if (DS32(0x8FFD0) == 0) DS32(0x8FFEC) = 0;
            if (g_MissionResult != 1) DS32(0x903D0) = DS32(0x8FFEC);
            DS32(0x8FFEC) = DS32(0x903D0);
            if (DS32(0x8FFF0) > 0 && DS32(0x8FFF0) - 0x226 <= DS32(0x903D0)) {
                g_PlaneCand = (DS32(0x8FFF0) - DS32(0x903D0)) / 0x32;
                if (g_PlaneCand < 0xb && g_PlaneCand != DS32(0x903DC)) {
                    /* Stub_AeroCountdown: empty */
                    DS32(0x903DC) = g_PlaneCand;
                }
            }
        }
        /* 90 */ g_BerthaDelay -= Sign(g_BerthaDelay);
        g_StrafeCount -= Sign(g_StrafeCount);
        if (g_FrameCounter50 == 1) DS32(0x90288) = DS32(0x8FFEC);
        /* 91 */ if (g_MP_Bertha != 0 && Rand(0x65) == 1 && g_BerthaDelay < 1) Bertha_Update();
        /* 92 */ if (g_BerthaShellTimer != 0 && ++g_BerthaShellTimer == 0x14) {
            g_BerthaShellTimer = 0;
            Explosion_Damage(g_BerthaShellX, 0x3ff - g_BaseYOff, 0, 0, 2000, 2000);
        }
        /* 93 */ if (DS32(0x8FEB4) != 0) Hud_WeaponMessage();
        if (DS32(0x909B8) != 0) Hud_PlaneMessage();
        if (g_AgentDropPending != 0) AgentDrop_Update();
    }
}
