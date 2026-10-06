/* Player subsystem: port/spec/player.md (§2.2 tables, §3.6-3.12 helpers, §4 controls, §5 Player_Update, §7
 * ejection and damage, §9.1 sprite helpers). The flight model itself (§3.1-3.5) is inlined in the frame loop
 * (frame.c), as in the original. Float code follows the disassembly: every (float) cast is a rounding point. */
#include "mission.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "level.h"
#include "platform.h"
#include "sound.h"
#include "video.h"

static double dbits(uint64_t u)
{
    double d;
    memcpy(&d, &u, sizeof d);
    return d;
}

/* ================================================================ flight tables (§2.2) */

/* Inlined in Mission_Setup 0x21c... and Player_Update 0x2e17e / 0x2e8c8: the plane tables. intAngles16 = 1 is
 * the hover/agile switch variant whose 16-entry loop truncates the angle to an int first (player.md Q9). */
void Player_BuildPlaneTables(int intAngles16)
{
    for (g_LoopI = 0; g_LoopI < 0x10; g_LoopI++) {
        double a;
        if (intAngles16) {
            DS32(0x90248) = (s32)((double)g_LoopI * dbits(0x3fd921fb4d12d84aull));   /* __FDI4: truncation */
            a = (double)DS32(0x90248);
        } else {
            DF32(0x90208) = (float)((double)g_LoopI * dbits(0x3fd921fb4d12d84aull));
            a = (double)DF32(0x90208);
        }
        g_Dir16X[g_LoopI] = (s32)(-cos(a) * 8.0);
        g_Dir16Y[g_LoopI] = (s32)(-sin(a) * 3.0);
    }
    for (g_LoopI = 0; g_LoopI < 0x20; g_LoopI++) {
        DF32(0x90208) = (float)((double)g_LoopI * dbits(0x3fc921fb4d12d84aull));
        double a = (double)DF32(0x90208);
        g_DirGunX[g_LoopI] = (s32)(-(cos(a) * 16.0 + 0.5));
        g_DirVX[g_LoopI] = (s32)(-cos(a) * 8.0);
        g_DirGunY[g_LoopI] = (s32)(-sin(a) * 16.0);
        g_DirLift[g_LoopI] = (s32)(-sin(a) * dbits(0x4014cccccccccccdull));     /* 5.2 */
    }
    DS32(0x8E190) = 0x10;                       /* g_DirGunX[16] */
}

void Player_BuildHeliTables(void)
{
    for (g_LoopI = 0; g_LoopI < 7; g_LoopI++) {
        DF32(0x90208) = (float)((double)g_LoopI * dbits(0x3ff0c1523361e585ull));
        g_Dir16X[g_LoopI] = (s32)(-cos((double)DF32(0x90208)) * 8.0);
        g_Dir16Y[g_LoopI] = -3;
    }
}

/* 0x863c8[t] = (float)((double)(stat82 * t) / 10.8 + 1.0) */
void Player_BuildSpeedCaps(void)
{
    for (g_LoopI = 0; g_LoopI < 10; g_LoopI++)
        g_MaxSpeedTab[g_LoopI] = (float)((double)(g_TopSpeed * g_LoopI) / dbits(0x402599999999999aull) + 1.0);
}

/* ================================================================ controls (§4) */

/* 0x323b9 Player_RotateA (Up) */
void Player_RotateA(void)
{
    if (g_IsHeli == 0) {
        g_Dir -= g_TurnRate;
        g_Dir = (g_Dir + 0x40) % 0x40;
        DS32(0x90620) += (g_EngineTickRate * g_Throttle) / 2;
    } else if (g_Fuel > 0 && g_DamageFlags[0] == 0 && g_DamageFlags[11] == 0) {
        g_HeliLift += abs(g_HeliVX) / 5 + 1;
        if (g_HeliLift > 10) g_HeliLift = 10;
    }
}

/* 0x32476 Player_RotateB (Down) */
void Player_RotateB(void)
{
    if (g_IsHeli == 0) {
        g_Dir += g_TurnRate;
        g_Dir = (g_Dir + 0x40) % 0x40;
        DS32(0x90620) += (g_EngineTickRate * g_Throttle) / 2;
    } else if (g_HeliLift > 0) {
        g_HeliLift--;
    }
}

/* 0x32585 Player_ThrottleUp */
void Player_ThrottleUp(void)
{
    if (g_IsHeli == 0) {
        g_ReverseThrust = Sign(g_ReverseThrust);
        if (g_DamageFlags[1] == 0 && g_DamageFlags[11] == 0 && g_Fuel > 0) {
            g_Throttle += 2;
            if (g_Throttle > 9) g_Throttle = 9;
            DS32(0x8FFD0) = 1;
        }
    } else {
        g_Dir = Clamp(g_Dir + 2, 0, 13);
        if (g_Dir == 13 && g_Throttle < 9) g_Throttle++;
    }
}

/* 0x326d3 Player_ThrottleDown */
void Player_ThrottleDown(void)
{
    if (g_IsHeli == 0) {
        if (g_DamageFlags[1] == 0 && g_DamageFlags[11] == 0 && (g_Throttle -= 2) < 0) g_Throttle = 0;
        if (g_Throttle == 0 && g_OnGround == 1) {
            if (g_ReverseThrust == 0) g_BrakeDrag = g_BrakeStrength * 3 * (g_Fuel > 0) + 10;
            else if (g_SpeedBits > 0) g_ReverseThrust = 2;
        }
    } else {
        g_Dir = Clamp(g_Dir - 2, 0, 13);
        if (g_Dir == 0 && g_Throttle < 9) g_Throttle++;
    }
}

/* 0x324ed Player_PitchUp (Right) */
void Player_PitchUp(void)
{
    if (g_IsHeli == 0) {
        Player_ThrottleUp();
    } else {
        g_HeliVX += 2;
        g_HeliVX = Clamp(g_HeliVX, -g_HeliMaxLeft[g_Dir / 2], g_HeliMaxRight[g_Dir / 2]);
        if (g_HeliVX == 0 && g_Throttle > 4) g_Throttle--;
    }
}

/* 0x32635 Player_PitchDown (Left): clears the brakes first (Q7) */
void Player_PitchDown(void)
{
    g_BrakeDrag = 0;
    if (g_IsHeli == 0) {
        Player_ThrottleDown();
    } else {
        g_HeliVX = Clamp(g_HeliVX - 2, -g_HeliMaxLeft[g_Dir / 2], g_HeliMaxRight[g_Dir / 2]);
        if (g_HeliVX == 0 && g_Throttle > 4) g_Throttle--;
    }
}

/* 0x16620 Player_PullUp (training "backseat driver") */
void Player_PullUp(void)
{
    if (g_CamY + g_PlayerScrY > 0) {
        DS32(0x90598) = Sign(g_Dir16X[g_Dir / 4]);
        DS32(0x9059C) = Sign(g_Dir16Y[g_Dir / 4]);
        if (DS32(0x901BC) != 0) {
            Hud_PushMessage(HUDTEXT(15));                            /* "PULL UP STUPID" */
            g_Throttle = 9;
            if (g_Dir / 4 < 4 || g_Dir / 4 > 12) g_Dir += g_TurnRate;
            if (g_Dir / 4 > 4 && g_Dir / 4 < 13) g_Dir -= g_TurnRate;
        }
        g_Dir = (g_Dir + 0x40) % 0x40;
    }
}

/* 0x15cb3 Player_FullPower */
void Player_FullPower(void)
{
    Hud_PushMessage(DSTR(0x8A748));                                  /* "FULL POWER STUPID !" */
    g_Throttle = 9;
}

/* 0x1693b Player_TakeoffAssist (§3.7) */
void Player_TakeoffAssist(void)
{
    g_SpeedDelta = g_Speed - g_PrevSpeed;
    g_PrevSpeed = g_Speed;
    g_RotateSpeed = g_CruiseSpeed - g_StallSpeed;
    g_RotateSpeed = (float)((double)g_RotateSpeed / 1.5 + (double)g_StallSpeed);
    if (g_FireHeldGearDown != 0 && g_EjectState == 0 && g_Fuel > 0) {
        if (g_WheelsOk == 1 && (double)g_SpeedDelta < 0.02 && g_Speed < g_RotateSpeed + -1.0f
            && g_DamageFlags[1] == 0 && g_DamageFlags[11] == 0) {
            g_Throttle = g_Throttle + 1 < 9 ? g_Throttle + 1 : 9;
        }
        if (g_WheelsOk == 1 && (double)g_SpeedDelta > -0.02 && g_Speed < g_RotateSpeed
            && g_DamageFlags[1] == 0 && g_DamageFlags[11] == 0 && g_Throttle > 0)
            g_Throttle--;
        if (g_OnGround == 1 && g_Throttle > 0) g_Throttle--;
        if (g_OnGround == 0) {
            if ((g_Dir < 0x11 && g_Dir > 4) || (g_Dir > 0x1c && g_Dir < 0x1b)) g_Dir -= g_TurnRate;
            if ((g_Dir > 0x10 && g_Dir < 0x1d) || g_Dir > 0x2e) {
                g_Dir += g_TurnRate;
                g_Dir %= 0x40;
            }
        }
    }
}

/* 0x16d70 Player_GearCollapseRoll (§3.10) */
void Player_GearCollapseRoll(void)
{
    if (Rand(5) == 1) {
        g_GearDown = 0;
        g_DamageFlags[10] = 1;
        Hud_PushMessage(HUDTEXT(38));                                /* "UNDERCARRIAGE FAILED !" */
        g_DamageLampsDirty = 1;
    }
}

/* 0x15bb6 Player_Ditching (§3.8) */
void Player_Ditching(void)
{
    if ((double)g_Speed < 0.1) DS32(0x9041C)++;
    if (Rand(5) == 1 && g_EjectState == 0) Hud_PushMessage(HUDTEXT(9));   /* "OUCH !" */
    if (Rand(20) == 1 && (double)g_Speed >= (double)g_CruiseSpeed * 0.5) {
        g_PlayerScrY -= Rand(4);
        if (Rand(2) == 1) {
            int a = Rand(2);
            int b = Rand(2);
            g_DirHalf = g_DirHalf + a - b;
        }
    }
}

/* 0x152a0 Player_AutoThrottle (§3.9) */
void Player_AutoThrottle(void)
{
    if ((g_GearDown == 0 || g_Ctrl[4] == 0) && g_OnGround == 0 && g_Fuel > 0) {
        if ((double)g_Speed < (double)g_CruiseSpeed + 0.5 && g_Throttle < 9) {
            g_Throttle++;
            DS32(0x8FFD0) = 1;
        }
        if (g_Speed > g_CruiseSpeed + 1.0f && g_Throttle > 5) g_Throttle--;
    }
}

/* 0x403c1 Player_GliderUpdate (§3.6) */
void Player_GliderUpdate(void)
{
    double E = (double)(g_Speed / (float)(g_OnGround + 1))
             - ((double)g_OnGround * 0.001) * (double)g_WingAuthority
             - (double)(((float)g_BrakeDrag + g_Drag) + (float)(g_ReverseThrust == 2 ? 5 : 0)) * 0.002
             + ((double)g_DirLift[g_DirHalf] * dbits(0x3fb999999999999aull)) * (double)(g_OnGround == 0)
             + dbits(0xbf60624dd2f1a9fcull)
             - (double)g_GearDown * dbits(0x3f947ae147ae147bull)
             + (double)g_RocketBoost;
    double r = (E > 6.0) ? 6.0 : E;
    if (r < 0) r = 0.0;
    g_Speed = (float)r;

    int px = g_CamX + g_PlayerScrX, py = g_CamY + g_PlayerScrY;
    int col = px / 16;
    g_LoopI = (s32)(Byte_Get(g_MapVal, 0x400 + (g_MapWidth + col) % 1000) & 0xff);            /* Q6: % 1000 */
    g_LoopI = (s32)(Byte_Get(g_MapVal, 0x400 + (col + g_MapWidth - 1) % 1000) & 0xff);
    g_LoopI = (s32)(Byte_Get(g_MapVal, 0x400 + (col + g_MapWidth + 2) % 1000) & 0xff);
    int h = g_LoopI;
    /* Q5: the bounds are whatever the shared loop counters g_LoopJ / 0x90830 hold now (kept global). */
    if (abs(py / 16 - h) < 16 && g_LoopJ < h && h < g_LoopK && h < 0x3c && g_OnGround == 0
        && abs(g_DirVX[g_DirHalf]) > 2 && (g_TowState == 0 || g_TowState == -17) && g_OverRunway == 0) {
        int l = (abs(py) / 16 - h - 16) / 4;
        g_GliderLift = (l + g_GliderLift > 8) ? 8 : g_GliderLift + l;
    }
    if (Map_GetTileAttr(px / 16, h, 3) & 0x40) g_PlayerScrY -= 2;
    if (g_GliderLift < -3) g_PlayerScrY -= 4;
    else g_PlayerScrY += g_GliderLift;
    g_GliderLift -= Sign(g_GliderLift);
}

/* 0x16b82 Player_EngineFire (§3.12) */
void Player_EngineFire(void)
{
    int a = Rand(8);
    int b = Rand(8);
    g_AimFrame = a - b;
    if (Rand(1) != 0) DS32(0x900FC) = 0;
    int s = Sign(g_EjectState);
    if (Rand(200) == s * 0x50 + 1000) g_EngineFire++;       /* never true (Q15) */
    if (g_EngineFire == 3) {
        DS32(0x8FF80) = DS32(0x900AC) + 0x18;
        DS32(0x900AC) += DS32(0x900B0);
        if (DS32(0x900AC) == -2 || DS32(0x900AC) == 2) DS32(0x900B0) = -DS32(0x900B0);
        int e = DS32(0x8FF80) - g_DirHalf;
        int m = abs(e) < 5 ? abs(e) : 4;
        g_Dir += Sign(e) * m;
        g_Dir = (g_Dir + 0x40) & 0x3f;
        g_DirHalf = g_Dir / 2;
        if (g_DirHalf > 0x15 && g_DirHalf < 0x1b && g_Crashed == 0) {
            Sprite_Queue(g_PlayerWX - g_CamX, g_PlayerWY - g_CamY, ((g_DirHalf - 0x16) / 2) * 2 + 0x116 + g_FrameParity);
            DS32(0x900FC) = 0;
        }
    }
    if (g_EngineFire > 3 && g_DeathTimer == 0) {
        DS32(0x9049C) = 1;
        g_Throttle = 0;
        g_DeathTimer = 0xf;
        g_Crashed = 1;
        g_StallSink = 0x10;
    }
}

/* ================================================================ damage (§7.2) */

static void Damage_EngineFire(void)              /* 0x3f5c8 */
{
    g_EngineFire++;
    if (DS32(0x906A8) > 0) {                     /* fire extinguishers */
        g_EngineFire -= Rand(2) + 1;
        if (g_EngineFire < 0) g_EngineFire = 0;
        DS32(0x906A8)--;
    }
    if (g_EngineFire > 0 && g_OnGround == 0 && g_Crashed == 0) g_PendingWarnSfx = 0xb;
}

static void Damage_EngineFail(void)              /* 0x3f645 */
{
    g_Throttle = 0;
    if (g_OnGround == 0 && g_Crashed == 0) g_PendingWarnSfx = 0xc;
}

static void Damage_WingHoled(void)               /* 0x3f8c8 */
{
    if (g_WingAuthority == 1) {
        g_WingAuthority = 0;
        if (g_OnGround == 0 && g_Crashed == 0) g_PendingWarnSfx = 0xd;
    } else {
        g_WingAuthority = 1;
    }
}

/* 0x3f2fe Player_DamageSystems */
void Player_DamageSystems(void)
{
    g_ViewTarget = -1;
    DS32(0x8FFCC) = -1;
    g_DamageHits = Rand((g_PlaneClass + 1) * g_DamageHits) + 1 + g_PlaneClass;
    if (g_OnGround == 8) g_DamageHits = Rand(g_DamageHits) + 1;
    for (g_DmgLoop = 1; g_DmgLoop <= g_DamageHits; g_DmgLoop++) {
        if (--g_Armour < 0) {
            if (g_Armour == -1) Sfx_Play(0xf, 12000, 0x20, g_CamX + g_PlayerScrX);
            if (g_EjectState == 0) DS32(0x9034C)++;
            int m = (g_OnGround == 8 || g_PlaneClass == 10) ? 0 : 1;
            int s = (Rand(m * g_PlaneClass + 13) < 15) ? Rand(m * g_PlaneClass + 13) : 14;
            g_LastDamage = s;
            if (s == 13 && g_EngineKind == 2) g_LastDamage = 15;
            switch (g_LastDamage) {
            case 0: Damage_EngineFire(); break;
            case 1: Damage_EngineFail(); break;
            case 2: g_TurnRate = (g_TurnRate != 1); break;                  /* 0x3f688 CONTROL HIT */
            case 3: g_RackRounds[0] = 0; break;                             /* 0x3f6c2 */
            case 4: g_RackRounds[1] = 0; break;                             /* 0x3f6e7 */
            case 5: break;                                                  /* 0x3f70c */
            case 6: Hud_LampBlinkA(); break;
            case 7: Hud_LampBlinkB(); break;
            case 8:                                                         /* 0x3f7c9 */
                Hud_LampBlinkA();
                Hud_LampBlinkB();
                g_DamageFlags[5] = 1; g_DamageFlags[6] = 1; g_DamageFlags[7] = 1;
                break;
            case 9: g_FuelLeaks++; break;                                   /* 0x3f80c */
            case 10: if (g_FixedGear == 1) g_GearDown = 0; break;           /* 0x3f82d */
            case 11: if (Rand(10) == 1) g_Throttle = Rand(8); break;        /* 0x3f85b */
            case 12: if (Rand(10) == 1) g_GunAmmo = 0; break;               /* 0x3f894 */
            case 13: Damage_WingHoled(); break;
            case 14: g_EngineFire = 3; break;                               /* 0x3f920 */
            case 15: Damage_WingHoled(); break;
            }
            g_DamageFlags[g_LastDamage < 14 ? g_LastDamage : 13] = 1;
            g_DamageLampsDirty = 1;
            if (g_EjectState == 0) Hud_PushMessage(g_DamageMsgs + g_LastDamage * 0x50);
        }
    }
}

/* ================================================================ ejection (§7.1) */

/* 0x3b6e6 Eject_SetPilotX */
void Eject_SetPilotX(void)
{
    for (g_LoopI = 0; g_LoopI < 6; g_LoopI++) {}
    int x = g_CamX + g_PlayerScrX;
    g_PilotX = Sign(g_PilotX) * x;
}

/* 0x3c600 Player_EjectUpdate */
void Player_EjectUpdate(void)
{
    DS32(0x90960) = DS32A(0x90CF8)[g_EjectState];
    if (DS32(0x90960) == 0x35) {
        DS32(0x90960) = DS32A(0x90CE4)[DS32(0x90864) / 2] + 0x35;
        DS32(0x90864) += DS32(0x90860);
        if (DS32(0x90864) == 0 || DS32(0x90864) == 9) DS32(0x90860) = -DS32(0x90860);
    }
    if (g_ChuteVY > -1 && g_EjectState < 4) {
        DS32(0x90960) = DS32(0x90964) / 2 + 0x18e;
        DS32(0x90964) = Wrap(DS32(0x90964) + 1, 0, 7);
        if (DS32(0x90980) == 0) {
            Particle_Spawn((g_CamX + g_PlayerScrX) * 0x100, (g_CamY + g_PlayerScrY) * 0x100, 0x200, g_ChuteVY << 8,
                           0x80, 0x14, 0x16);
            DS32(0x90980) = 1;
        }
    }
    Sprite_Queue(g_PlayerScrX, g_PlayerScrY, DS32(0x90960));
    if (g_PilotX != 0) {
        Sprite_Queue(g_PilotX - g_EjectCamX, g_PlayerScrY, DS32(0x90960));
        g_MP_PickupCol = (u16)(g_PilotX / 16);
        if (g_MP_PickupCol == 0) g_MP_PickupCol = 1;
        g_PilotX -= (g_ChuteVY < 0) ? 3 : 1;
        if (g_PilotX < 1) g_PilotX = 1;
        g_PickupX = g_PilotX;
    }
    if (g_ChuteVY < 0) {
        int r = Rand(2);                         /* argument of the empty stub 0x11105 (Stub_FrameD) */
        (void)r;
        DS32(0x900FC) = 0;                       /* Stub_FrameD returns 0 */
    }
    g_ChuteSwing = Clamp(g_ChuteSwing + g_ChuteSwingDir, 0, 0x17);
    if (g_ChuteSwing > 0x16 || g_ChuteSwing < 1 || Rand(2) == 1) g_ChuteSwingDir = -g_ChuteSwingDir;
    DS32(0x8FFCC) = -1;
    g_EjectCamY += g_ChuteVY;
    g_ChuteVY += g_FrameParity;
    int cap = 16 - 12 * (g_EjectState == 7);
    if (g_ChuteVY > cap) g_ChuteVY = cap;
    if (g_ChuteVY < 4 && g_EjectState < 3) g_EjectState = 3 - g_EjectState;
    if (g_ChuteVY > 3 && g_CamY + g_PlayerScrY > 800) {
        g_EjectState++;
        int lim = 100 - 0x5f * (g_EjectSeat == -1);
        if (DS32(0x90974) + 7 <= lim) lim = DS32(0x90974) + 7;
        if (g_EjectState > lim) g_EjectState = lim;
        if (g_ChuteFail == 1) {
            Hud_PushMessage(DSTR(0x8AFB8));                          /* "HAD YOU WORRIED" */
            g_ChuteFail = 0;
        }
    }
    g_OverRunway = (g_BaseStartX < g_EjectScrX + g_EjectCamX && g_EjectScrX + g_EjectCamX < g_BaseEndX) ? 1 : 0;
    int row = (g_CamY + g_PlayerScrY) / 16;
    if (row < 0) row = 0;
    DS32(0x90228) = Map_GetTileAttr(Clamp((g_CamX + g_PlayerScrX) / 16 + 1, 0, g_MapWidth), row, 0);
    if (g_ChuteVY < 0 && DS32(0x90228) > 0x7e && g_OverRunway == 0) {
        Sprite_Queue(g_PlayerScrX, g_PlayerScrY, 0x4b);
        g_EjectState = 100;
        g_ChuteVY = 0;
    }
    if (g_EjectCamY > CAM_Y_MAX) {                                   /* ENH: view */
        int water = (Map_GetTileAttr((g_CamX + g_PlayerScrX) / 16, 0x3f, 0) == 0x82);
        DS32(0x8FF44) = water * 4;
        DS32(0x90244) = DS32(0x8FF44) + 0x9f + VIEW_EXTRA_ROWS;
        if (water) g_CrashAttr = 0x82;
        g_EjectCamY = CAM_Y_MAX;
        g_EjectScrY += g_ChuteVY;
        if (DS32(0x90244) <= g_EjectScrY && g_EjectState < 7 && DS32(0x8FF44) < g_EjectState) {
            Sprite_Queue(g_PlayerScrX, DS32(0x90244), 0x4b);
            g_EjectState = 100;
        }
        if (DS32(0x90244) <= g_EjectScrY && g_EjectState > 6) {
            if (g_EjectState < 0x18) g_EjectState = 0x18;
            if (DS32(0x90974) < 0x1b) DS32(0x90974) = 0x1b;
            g_ChuteVY = 4;
            g_EjectScrY = DS32(0x90244);
        }
        if (g_EjectState == 0x1d && DS32(0x90974) < 0x33) {
            g_EjectState = 0x1c;
            DS32(0x90974)++;
        }
        if (DS32(0x90974) > 0x1d) g_EjectState = 99;
        if (DS32(0x90244) <= g_EjectScrY && g_EjectState <= DS32(0x8FF44)) g_EjectState = 99;
        if (g_EjectState == 99 && g_PilotX > 0) {
            g_LoopI = Map_GetTileAttr(g_PickupX / 16, 0x3f, 0);
            g_PickupY = 0x3e1;
            if (g_LoopI == 0x82 && (g_MP_PickupSprite == 0xca || g_MP_PickupSprite == 0xac)) {
                DS32(0x90A44) = 0;
                g_MP_PickupSprite = 0xcd;
                g_PickupY = 0x3e5;
            }
            if (g_BaseStartX - 0x40 < g_PickupX && g_PickupX < g_BaseEndX + 0x40) {
                g_MP_PickupCol = 0;
                DS32(0x90A44) = 0;
                DS32(0x90A3C) = 0;
            }
            g_PilotX = 0;
        }
    }
    if (g_EjectSeat == -1 && g_EjectState < 99) g_EjectState = 2;
}

/* ================================================================ drawing (§9.1) */

/* 0x29b29 Player_DrawHeli: body + rotor from the .HD table (rotor offset always from record 0, Q12). */
int Player_DrawHeli(int x, int y, int heli, int dx, int f, int gear, int rotorAnim)
{
    const u8 *hd = g_HeliHD;
    if (heli == 1) {
        int kind = g_HeliFrameKind[f];
        int base = g_HeliFrameBase[f];
        f = base;
        if (kind != 1 && kind > 2) {
            if (kind < 4) {
                f = base + 1 + Sign(dx);
            } else if (kind == 5) {
                if (dx < 0) {
                    f = (-dx == g_HeliMaxLeft[g_Dir / 2]) ? base : base + 1;
                } else {
                    f = base + 2;
                    if (dx > 0) f = (dx == g_HeliMaxRight[g_Dir / 2]) ? base + 4 : base + 3;
                }
            }
        }
        if (gear == 1 && g_FixedGear == 0) f += 0x12;
    } else {
        if (gear - g_FixedGear == 1 && (f < 3 || f > 0x1d || (f > 0xd && f < 0x13))) f += 0x20;
        f += 0x24;
    }
    if (!hd) return f;                           /* PORT: no .HD loaded (not a heli/VTOL plane) */
    s32 r0, r2;
    memcpy(&r0, hd + f * 16, 4);
    memcpy(&r2, hd + f * 16 + 8, 4);
    s32 a, b;
    if (r0 < 1) { a = r2 & 0xffff; b = r0; } else { a = r0; b = r2; }
    a &= 0xffff;
    s32 r = (-b) & 0xffff;
    s16 ox = 0, oy = 0;                          /* PORT: uninitialised in the original when r == 0 (id 0 is not drawn) */
    if (r) {
        r += rotorAnim;
        memcpy(&ox, hd + 14, 2);
        memcpy(&oy, hd + 12, 2);
    }
    if (r0 < 0) {
        Sprite_Queue(x, y, a);
        Sprite_Queue(ox + x, oy + y, r);
    } else {
        Sprite_Queue(ox + x, oy + y, r);
        Sprite_Queue(x, y, a);
    }
    return f;
}

/* 0x156a2 Player_AfterburnerFlame */
void Player_AfterburnerFlame(void)
{
    int d = g_DirHalf;
    DS32(0x902AC) = 0;
    g_LoopJ = g_Throttle - 6;
    for (g_LoopI = 0; g_LoopI < 3; g_LoopI++) {
        DS32(0x90278) = (s32)(((double)(g_DirGunX[d] * g_FlameLength) + (double)(g_LoopJ * (g_DirGunX[d] * g_LoopI)) * dbits(0x3fe999999999999aull)) / 15.0);
        DS32(0x9027C) = (s32)(((double)(g_DirGunY[d] * g_FlameLength) + (double)(g_LoopJ * (g_DirGunY[d] * g_LoopI)) * dbits(0x3fe999999999999aull)) / 15.0);
        int bx = (g_PlayerWX - g_CamX) - DS32(0x90278), by = (g_PlayerWY - g_CamY) - DS32(0x9027C);
        if (g_TwinEngine == 0) {
            Sprite_Queue(bx, by, Rand(1) * 2 + (0xa4 - g_LoopI / 2));
        } else {
            Sprite_Queue(g_DirGunY[d] / 5 + bx, by, Rand(1) * 2 + 0xa4);
            Sprite_Queue(bx - g_DirGunY[d] / 5, by, Rand(1) * 2 + 0xa4);
        }
    }
    DS32(0x90920) = Wrap(DS32(0x90920) + 1, 0, 2);
}

/* 0x1554b Player_DrawReverseThrust */
void Player_DrawReverseThrust(void)
{
    if (g_ReverseThrust == 2 && g_HookDown == 0) {
        int r = (g_DirHalf / 2 == 8);
        DS32(0x90278) = r ? -g_ReverseExhaustX : g_ReverseExhaustX;
        DS32(0x902AC) = r;
        int y = g_PlayerWY - g_CamY;
        Sprite_Queue(g_PlayerWX - g_CamX + DS32(0x90278), Rand(2) + y - 1, r + 0xa1);
    }
    if (g_HookDX > 0 && g_HookDown == 1 && g_IsHeli == 0) {
        int r = (g_DirHalf / 2 == 8);
        DS32(0x90278) = r ? -g_HookDX : g_HookDX;
        DS32(0x902AC) = r;
        Sprite_Queue(g_PlayerWX - g_CamX + DS32(0x90278), g_PlayerWY - g_CamY + g_HookDY, r + 0xa9);
    }
}

/* 0x3b5c0 Debris_Spawn: wreck pieces in the projectile arrays */
void Debris_Spawn(void)
{
    g_ProjCount = Rand(6) + 4;
    for (g_LoopI = 0; g_LoopI < g_ProjCount; g_LoopI++) {
        int a = g_PlayerWX + Rand(8);
        g_ProjX[g_LoopI] = a - Rand(8);
        g_ProjY[g_LoopI] = g_PlayerWY;
        int v = g_PlayerVX + Rand(2);
        g_ProjVX[g_LoopI] = v - Rand(2);
        if (g_PlayerVX == 0) g_ProjVY[g_LoopI] = 0;
        else g_ProjVY[g_LoopI] = -Rand(abs(g_PlayerVX) * 2);
        g_ProjBlastA[g_LoopI] = Rand(6);
    }
}

/* 0x3b27c Debris_Update (GF step 24 once g_DeathTimer >= 16) */
void Debris_Update(void)
{
    if (DS32(0x90628) == 0) {
        Debris_Spawn();
        DS32(0x90628) = 1;
    }
    int n = g_ProjCount;
    if (g_ProjCount > 0) {
        while ((g_LoopI = n - 1) > -1) {
            s32 x = g_ProjX[g_LoopI], y = g_ProjY[g_LoopI], vx = g_ProjVX[g_LoopI], vy = g_ProjVY[g_LoopI];
            /* g_CurX/Y/VX/VY (0x9080c/0x90810/0x90934/0x90938): the original's working copies, stored below */
            if (IsOnScreen(g_CamX, g_CamY, x, y)) Sprite_Queue(x - g_CamX, y - g_CamY, g_ProjBlastA[g_LoopI] + 0x3e);
            if (g_ProjBlastA[g_LoopI] < 4) g_ProjBlastA[g_LoopI] = (g_ProjBlastA[g_LoopI] + 1) & 3;
            x += vx;
            y += vy;
            vy = vy + 1 < 0xc ? vy + 1 : 0xb;
            if (y > 1000) x = 0;
            g_ProjX[g_LoopI] = x;
            g_ProjY[g_LoopI] = y;
            g_ProjVX[g_LoopI] = vx;
            g_ProjVY[g_LoopI] = vy;
            DS32(0x9080C) = x;
            DS32(0x90810) = y;
            DS32(0x90934) = vx;
            DS32(0x90938) = vy;
            s32 b = g_ProjBlastA[g_LoopI] < 8 ? g_ProjBlastA[g_LoopI] : 7;
            g_ProjBlastA[g_LoopI] = b < 0 ? 0 : b;
            n = g_LoopI;
            if (g_ProjX[g_LoopI] == 0) {
                SwapInt(&g_ProjX[g_LoopI], &g_ProjX[g_ProjCount - 1]);
                SwapInt(&g_ProjY[g_LoopI], &g_ProjY[g_ProjCount - 1]);
                SwapInt(&g_ProjVX[g_LoopI], &g_ProjVX[g_ProjCount - 1]);
                SwapInt(&g_ProjVY[g_LoopI], &g_ProjVY[g_ProjCount - 1]);
                SwapInt(&g_ProjBlastA[g_LoopI], &g_ProjBlastA[g_ProjCount - 1]);
                g_ProjCount--;
                n = g_LoopI;
            }
        }
    }
}

/* ================================================================ Player_Update (§5) */

static void pause_wait(int wantDown)
{
    /* busy loops on g_KeyDown[0..0x7f] (PORT: pump events, game_flow.md Q24) */
    DS32(0x90470) = wantDown ? 0 : 0x100;
    while (wantDown ? DS32(0x90470) < 0x100 : DS32(0x90470) > 0xff) {
        for (DS32(0x90470) = 0; DS32(0x90470) < 0x80; DS32(0x90470)++)
            if (g_KeyDown[DS32(0x90470)] != 0) DS32(0x90470) = 0x100;
        if (wantDown ? DS32(0x90470) < 0x100 : DS32(0x90470) > 0xff) Platform_Spin();
    }
}

/* 0x2d34e Player_Update */
void Player_Update(void)
{
    char *msg = DSTR(0x84A48);
    DS32(0x90814) = g_Ctrl[0];
    DS32(0x90820) = g_Ctrl[2];
    Input_ReadControls();
    g_FireOrConfirm = g_Fire;
    if (g_NeedsTow > 0 && (g_Ctrl[1] != 0 || g_Ctrl[9] != 0) && g_OnGround == 1 && g_SpeedBits == 0 && g_TowX == 0) {
        g_DirHalf = 0;
        g_TowVY = 0;
        g_TowRope = 0;
        g_TowTime = 0;
        g_TowX = g_CamX + 0x140 + VIEW_EXTRA_COLS;                   /* ENH: view: at the right edge */
        g_TowY = g_CamY + g_PlayerScrY;
        g_TowFrame = 2;
        g_TowState = 0;
    }
    DS32(0x8FFA4) = g_ViewX;
    DS32(0x8FF50) = g_ViewY;
    g_ViewX = 0;
    g_ViewY = 0;
    g_ActionThisFrame = 0;
    if (g_KeyAutoThrottle != 0 && g_LastAction != 7) {
        int n = -g_AutoThrottle;
        g_AutoThrottle = n + 1;
        Hud_PushMessage(HUDTEXT(n + 0x16));
        g_LastAction = 7;
        g_ActionThisFrame = 1;
    }
    if (g_Ctrl[2] == 0 || g_Ctrl[8] == 0 || g_Ctrl[1] != 0) {
        DS32(0x9079C) = 0;
    } else if (g_RocketBoost == 0 && g_RocketCount > 0) {   /* dead: g_Ctrl[2] is always 0 (Q17) */
        Hud_PushMessage(DSTR(0x81268));                              /* "Rocket Boost !" */
        DS32(0x907B0) = 0;
        g_RocketCount--;
        g_RocketBoost = 10;
        g_RocketBoostTimer = 0x1e;
    } else if (++DS32(0x9079C) > 3) {
        DS32(0x907B0) = 1 - DS32(0x907B0);
    }
    if (g_KeyBriefing == 0) {
        DS32(0x90084) = 0;
    } else if (++DS32(0x90084) > 8) {
        Hud_DrawBriefing();
        DS32(0x8F150) = 0x3c;
    }
    if (g_KeyPause != 0) {
        g_LastAction = 3;
        g_ActionThisFrame = 1;
        g_PlaneCand = DS32(0x8FFEC);                                 /* 0x9002c: saved Aerolimits timer */
        for (g_LoopI = 0xf; g_LoopI > -1; g_LoopI--) {}
        Pal_Fade(0, 0x100, 2, 0x10);
        pause_wait(0);                                               /* until no key is down */
        pause_wait(1);                                               /* until a key is down */
        Pal_Fade(0, 0x100, 3, 0x10);
        DS32(0x8FFEC) = g_PlaneCand;
    }
    if (g_KeyAbort != 0 && g_OnGround == 0) {
        g_EngineFire = 3;
        g_Lives = -8;
    }
    if (g_KeyLook != 0) {
        if (DS32(0x8FFA4) == 0 && DS32(0x8FF50) == 0) Hud_PushMessage(DSTR(0x81278));   /* "Looking around" */
        g_ViewTarget = -1;
        g_ViewX = g_CamX + ((s32)g_KeyLeft - (s32)g_KeyRight) * -0x140;
        g_ViewY = g_CamY + ((s32)g_KeyUp - (s32)g_KeyDn) * -0xb0;
        g_KeyUp = 0;
        g_KeyDn = 0;
        g_KeyLeft = 0;
        g_KeyRight = 0;
    }
    if (g_KeyTarget != 0 && DS32(0x90814) == 0) {
        g_Scratch690 = 0;
        msg[0] = 0;
        for (g_LoopI = 0; g_Scratch690 == 0 && g_LoopI < 6; g_LoopI++) {
            if (g_ViewTarget == 200) {
                g_ViewTarget = -1;
                g_Scratch690 = 1;
                strcpy(msg, DSTR(0x81288));                          /* "Follow Aborted" */
            }
            if (g_ViewTarget == 0x96) {
                g_ViewTarget = 200;
                if (g_EnemyAirCount > 0) { g_Scratch690 = 1; strcpy(msg, DSTR(0x81297)); }
            }
            if (g_ViewTarget == 100) {
                g_ViewTarget = 0x96;
                if (DS32(0x902E0) > 0) { g_Scratch690 = 1; strcpy(msg, DSTR(0x812B0)); }
            }
            if (g_ViewTarget == 0x32) {
                g_ViewTarget = 100;
                if (DS32(0x90528) != 0) { g_Scratch690 = 1; strcpy(msg, DSTR(0x812C7)); }
            }
            if (g_ViewTarget > -1 && g_ViewTarget < 0x32) {
                g_ViewTarget = 0x32;
                strcpy(msg, DSTR(0x812DC));                          /* "Following B52" */
                g_Scratch690 = 1;
            }
            if (g_ViewTarget == -1) {
                g_ViewTarget = g_ProjCount;
                strcpy(msg, DSTR(0x812EA));                          /* "Ready to follow weapon" */
                g_Scratch690 = 1;
            }
        }
        Hud_PushMessage(msg);
    }
    if (g_KeyEject != 0 && g_EjectState == 0) {
        g_EjectState = 1;
        g_PendingWarnSfx = 0;
        g_EjectScrX = g_PlayerScrX;
        g_EjectScrY = g_PlayerScrY - 2;
        g_EjectCamX = g_CamX;
        g_EjectCamY = g_CamY;
        g_ChuteVY = 0;
        if (g_PlayerVY > 0) g_ChuteVY = g_PlayerVY;
        g_ChuteVY += g_EjectSeat * -8;
        if (g_ChuteVY < -0x10) g_ChuteVY = -0x10;
        if (g_ChuteVY > 0x10) g_ChuteVY = 0x10;
        DS32(0x8FFCC) = -1;
        g_ChuteSwing = Rand(0x17);
        g_ChuteSwingDir = 0x10;
        DS32(0x8FEF4) = 0x10;
        int x = g_CamX + g_PlayerScrX;
        g_PilotX = Sign(g_PilotX) * x;
        if (g_EjectSeat != 0) Eject_SetPilotX();
        g_TowState = -17;
        g_ChuteFail = 0;
        if (Rand(100) == 1) {
            Hud_PushMessage(DSTR(0x8AB58));                          /* "*** PARACHUTE FAILURE ***" */
            g_ChuteFail = 1;
        } else {
            Hud_PushMessage(DSTR(0x8ABA8));                          /* "YOU'RE OUT O' HERE !" */
        }
        if ((double)g_Speed < 0.1 && g_OnGround == 1) g_EjectState = 99;
    }
    /* Gear (U): on the ground only above 6 x stall speed, in the air always (player.md Correction 2). */
    if (g_KeyGear != 0 && (g_OnGround == 0 || g_Speed > g_StallSpeed * 6.0f)
        && g_LastAction != 4 && g_DamageFlags[10] == 0 && g_FixedGear == 0) {
        g_LastAction = 4;
        g_ActionThisFrame = 1;
        if (g_GearLatch == 0) {
            g_GearDown = 1 - g_GearDown;
            if (g_GearSilent == 0) Sfx_Play(0x13, 0x1194, 0x1e, g_CamX + g_PlayerScrX);
            Hud_PushMessage(HUDTEXT(g_GearDown + 0x23));
            if (g_HookDX != 0 && g_BaseIsCarrier == 1) {
                g_HookDown = g_GearDown;
                Hud_PushMessage(HUDTEXT(g_GearDown + 0x38));
            }
            g_GearLatch = 1;
        }
    } else {
        g_GearLatch = 0;
    }
    /* follow views (level.md §6.2). ENH: view: the target sits at (0xa0, 0x58), plus half the extra columns/rows */
#define VIEW_MID_X (0xa0 + VIEW_EXTRA_COLS / 2)
#define VIEW_MID_Y (0x58 + VIEW_EXTRA_ROWS / 2)
    if (g_ViewTarget > -1 && g_ViewTarget < g_ProjCount) {
        if (g_ProjKind[g_ProjCount] == 8) {                          /* Q8: the slot after the last one */
            g_ViewTarget = -1;
        } else {
            g_ViewX = g_ProjX[g_ViewTarget] - VIEW_MID_X;
            g_ViewY = g_ProjY[g_ViewTarget] - VIEW_MID_Y <= CAM_Y_MAX ? g_ProjY[g_ViewTarget] - VIEW_MID_Y : CAM_Y_MAX;
            DS32(0x903C8) = g_ViewY;
            DS32(0x903C4) = g_ViewX;
        }
    }
    if (g_ViewTarget == 0x32) {
        if (DS32(0x90998) == 0) g_ViewTarget = -1;
        else {
            g_ViewX = DS32(0x90984) - VIEW_MID_X;
            g_ViewY = DS32(0x90988) - VIEW_MID_Y;
            if (g_ViewY < -0x800) g_ViewY = -0x800;
            DS32(0x903C8) = g_ViewY;
            DS32(0x903C4) = g_ViewX;
        }
    }
    if (g_ViewTarget == 100) {
        if (DS32(0x90528) == 0) g_ViewTarget = -1;
        else {
            g_ViewX = DS32(0x90520) - VIEW_MID_X;
            g_ViewY = DS32(0x904C0) - VIEW_MID_Y;
            if (g_ViewY < -0x800) g_ViewY = -0x800;
            DS32(0x903C8) = g_ViewY;
            DS32(0x903C4) = g_ViewX;
        }
    }
    if (g_ViewTarget == 0x96) {
        if (DS32(0x902E0) == 0) g_ViewTarget = -1;
        else {
            g_ViewX = DS32(0x8DDC0) - VIEW_MID_X;
            g_ViewY = DS32(0x8DD98) - VIEW_MID_Y;
            if (g_ViewY > CAM_Y_MAX) g_ViewY = CAM_Y_MAX;
            if (g_ViewY < -0x800) g_ViewY = -0x800;
            DS32(0x903C8) = g_ViewY;
            DS32(0x903C4) = g_ViewX;
        }
    }
    if (g_ViewTarget == 200) {
        if (g_EnemyAirCount == 0) g_ViewTarget = -1;
        else {
            g_ViewX = g_EnemyAirX[0] - VIEW_MID_X;
            g_ViewY = g_EnemyAirY[0] - VIEW_MID_Y;
            if (g_ViewY > CAM_Y_MAX) g_ViewY = CAM_Y_MAX;
            if (g_ViewY < -0x800) g_ViewY = -0x800;
            DS32(0x903C8) = g_ViewY;
            DS32(0x903C4) = g_ViewX;
        }
    }
    if (g_ViewTarget < -1) {
        g_ViewX = DS32(0x903C4);
        g_ViewY = DS32(0x903C8);
        if (--g_ViewTarget < -0xf) g_ViewTarget = -1;
    }
    if (g_KeyDn == 0 || g_OnGround != 1 || g_SpeedBits != 0 || g_GameMode == 3) {
        DS32(0x90854) = 0;
        DS32(0x90140) = 0;
    } else {
        DS32(0x90854)++;
    }
    /* §5.6 hover/agile switch (VTOL) */
    if (g_KeyHover == 0 || g_AirframeType != 2 || g_LastAction == 5) {
        DS32(0x903BC) = 0;
    } else {
        g_LastAction = 5;
        g_ActionThisFrame = 1;
        if (DS32(0x903BC) == 0) {
            int d = g_DirHalf;
            if (g_IsHeli == 0) {
                if (d < 3 || d > 0x1d || (d > 0xd && d < 0x13)) {
                    Hud_PushMessage(DSTR(0x8A6A8));                  /* "HOVER MODE" */
                    g_IsHeli = 1;
                    g_Dir = (d < 0xe || d > 0x12) ? 0 : 0xd;
                    g_HeliVX = g_DirVX[d] * 2;
                    Player_BuildSpeedCaps();
                }
                g_HeliLift = 4;
            } else if (g_Dir / 2 == 0 || g_Dir / 2 == 6) {
                Hud_PushMessage(DSTR(0x8A798));                      /* "AGILE MODE" */
                if (g_Throttle < 4) g_Throttle = 4;
                g_Dir = (g_Dir / 2 == 6) << 5;
                g_IsHeli = 0;
                Player_BuildSpeedCaps();
                g_HeliLift = 0;
            }
            if (g_IsHeli == 0) Player_BuildPlaneTables(1);           /* Q9: integer angles */
            else Player_BuildHeliTables();
        }
        DS32(0x903BC) = 1;
    }
    /* §5.7 Up released on the ground */
    if (g_KeyUp == 0 && g_UpHeld != 0 && g_IsHeli == 0 && g_OnGround == 1 && (double)g_Speed < 0.1
        && g_PlayerVX == 0 && (g_IsHeli == 0 || g_FixedGear == 0) && g_LastAction != 6) {
        if (g_AirframeType == 2) {
            if (g_IsHeli == 0) {
                g_IsHeli = 1;
                Player_BuildSpeedCaps();
                DS32(0x902D0) = g_DirHalf / 2;
                g_Dir = 6;
                g_DirHalf = 3;
                g_HeliVX = 0;
                g_HeliLift = 0;
                g_Speed = 0;
            }
            /* the "back to plane" branch (g_IsHeli != 0) is unreachable here (Q20) */
            if (g_IsHeli == 0) Player_BuildPlaneTables(0);
            else Player_BuildHeliTables();
        } else if (g_DirHalf < 8) {
            g_Dir = g_ParkAttitude * -4 + 0x20;
        } else {
            g_Dir = g_ParkAttitude << 2;
        }
    }
    g_LastAction = g_LastAction * g_ActionThisFrame;
}
