/* The support aircraft (port/spec/player.md §6, §9.4; enemies.md §13): SupportAircraft_Update 0x2780b (glider tow
 * plane, Fat Albert, B52, "Nessie" ship, the big enemy bomber with its lock-on cue and missiles, the player's flares,
 * the ground pickup / agent rescue) and Tanker_Update 0x27008 (air refuelling). Globals live in the data-segment
 * image at their original addresses; Rand() calls in the original order (arguments are evaluated right to left,
 * so a Rand in the last argument of Particle_Spawn runs before the call). */
#include "mission.h"

#include <stdlib.h>

#include "level.h"
#include "platform.h"
#include "sound.h"
#include "video.h"

#define PX (g_CamX + g_PlayerScrX)
#define PY (g_CamY + g_PlayerScrY)

static int min_i(int a, int b) { return a < b ? a : b; }
static int max_i(int a, int b) { return a > b ? a : b; }

#define g_ObjX       DS32(0x909A8)
#define g_ObjY       DS32(0x909AC)
#define g_ObjDir     DS32(0x909FC)
#define g_EnemyBombWeapon DS32(0x906F0)
#define g_AimDirX    DS32(0x90A24)
#define g_AimDirY    DS32(0x90A28)
#define g_EnemyMslCount DS32(0x906FC)
#define g_HudRows    DS32(0x90308)
#define g_PropDX     DS32A(0x90CD8)
#define g_PropDY     DS32A(0x90BE4)

/* tow plane */
#define g_TowFrameT  g_TowFrame
/* Fat Albert (Hercules) */
#define g_HercActive DS32(0x90528)
#define g_HercX      DS32(0x90520)
#define g_HercY      DS32(0x904C0)
#define g_HercHit    DS32(0x904B0)
#define g_HercDoor   DS32(0x904B4)
#define g_HercFrame  DS32(0x90518)
#define g_HercDrop   DS32(0x904D8)
#define g_HercDown   DS32(0x9050C)
#define g_HercVY     DS32(0x904BC)
#define g_HercTimer  DS32(0x9051C)
#define g_HercWreck  DS32(0x90548)
#define g_DropZoneX  DS32(0x90398)
/* B52 */
#define g_B52Active  DS32(0x90998)
#define g_B52X       DS32(0x90984)
#define g_B52Y       DS32(0x90988)
#define g_B52Hit     DS32(0x90970)
#define g_B52Hp      DS32(0x90978)
#define g_B52Wreck   DS32(0x9095C)
#define g_B52Phase   DS32(0x9096C)
#define g_B52TargetX DS32(0x9037C)
/* ship */
#define g_ShipActive DS32(0x90158)
#define g_ShipX      DS32(0x900D0)
#define g_ShipY      DS32(0x900D4)
#define g_ShipFrame  DS32(0x900C8)
#define g_ShipDir    DS32(0x900BC)
#define g_ShipTurn   DS32(0x900CC)
/* big bomber */
#define g_BigActive  DS32(0x906DC)
#define g_BigX       DS32(0x9073C)
#define g_BigY       DS32(0x90740)
#define g_BigKilled  DS32(0x90728)
#define g_BigWreck   DS32(0x90714)
#define g_BigHitDone DS32(0x90750)
#define g_BigMissiles DS32(0x90734)
/* pickup */
#define g_PickupWalk DS32(0x8FF30)
#define g_Winch      DS32(0x8FEB0)
#define g_WinchLen   DS32(0x8FED0)
#define g_HasWinch   DS32(0x8FEE0)
#define g_PickAnim   DS32(0x90A3C)
#define g_PickWalker DS32(0x90A38)
#define g_PickTarget DS32(0x90A44)
#define g_AgentVehicle DS32(0x90A40)

void Enemy_DropBomb(void);                       /* 0x17944 (enemies.c) */

/* the wreck of a shot-down transport: two sprites and two smoke particles, offset counter `ofs` */
static void wreck(int X, int Y, int sprA, int sprB, int ofsRead, s32 *ofsInc)
{
    Sprite_Queue(X - g_CamX - 0x20, Y - g_CamY, sprA);
    int r = Rand(1);
    Particle_Spawn((X - 0x20) * 0x100, (Y - 0x10) * 0x100, 0, 0, 0, 0x20, r);
    Sprite_Queue(X - g_CamX + ofsRead, (Y - g_CamY) - Div16(ofsRead), sprB);
    r = Rand(1);
    Particle_Spawn((X + ofsRead) * 0x100, (Y - Div16(ofsRead)) * 0x100 - 0x1000, 0, 0, 0, 0x20, r);
    (*ofsInc)++;
}

static void tow_plane(void)
{
    if (!IsOnScreen(g_CamX, g_CamY, g_TowX, g_TowY)) {
        if (g_TowState == -0x11) g_TowX = 0;
    } else {
        Sprite_Queue(g_TowX - g_CamX, g_TowY - g_CamY, g_TowFrame + 0x1de);
        if (-0x11 < g_TowState && (g_TowX < PX - 0x50 || g_OnGround == 0)) {
            for (g_LoopI = 0; g_LoopI < 6; g_LoopI++) {                  /* rope sag -1,0,1,2,2,1 */
                g_LoopJ = ((g_LoopI - (g_LoopI == 5) * 2) - (3 < g_LoopI)) - 1;
                int y = g_LoopJ + (g_TowY - g_CamY);
                int len = (PX - g_TowX < g_TowRope) ? PX - g_TowX : g_TowRope;
                double f = (double)len / 96.0;
                int x = (s32)((double)(g_TowX - g_CamX) + (double)(g_LoopI << 4) * f);
                Sprite_Queue(x, y, 0x1e1);
            }
            if (g_TowRope < 0x60) {                                      /* hook on the ground */
                int y = (0x3df - g_BaseYOff) - g_CamY;
                int len = (PX - g_TowX < g_TowRope) ? PX - g_TowX : g_TowRope;
                double f = (double)(len * 0x60) / 96.0;
                Sprite_Queue((s32)((double)(g_TowX - g_CamX) + f), y, 0x1d2 + g_FrameParity);
            }
        }
    }
    if (g_TowState < -0x10 || (PX - 0x5c <= g_TowX && -1 < g_TowState && g_OnGround != 0 && g_TowTime < 1 && g_TowRope != 0x60)) {
        if (0 < g_TowX) {
            if (g_OnGround == 1) {
                if (g_SpeedBits == 0) { g_TowX -= 2; g_TowFrame = 1; }
                g_TowState = 0;
                g_TowY = 0x3dc - g_BaseYOff;
            } else {
                g_TowY -= 0x10;
                g_TowX -= 6;
                g_TowFrame = min_i(g_TowFrame + 1, 2);
                if (g_TowY < -0x800) g_TowX = 0;
            }
        }
    } else {                                                             /* towing */
        g_Speed = (float)abs(g_TowState / 8);
        g_DirHalf = 0;
        g_Dir = 0;
        if (0x1e < g_TowTime) {
            g_PlayerScrY += g_TowVY;
            g_TowVY = g_TowVY < -7 ? -8 : g_TowVY - 1;
        }
        if (g_TowRope == 0x60) {
            g_TowTime++;
            g_TowState -= g_FrameParity;
            if (g_TowState < -0x10) g_TowState = -0x10;
        }
        g_TowX = PX - 0x60;
        g_TowRope = min_i(g_TowRope + 2, 0x60);
        g_TowY = PY;
        if (g_TowY < -400) g_TowState = -0x11;                           /* auto release high up */
        if (g_TowState < -2 && 0 < g_TowFrame) g_TowFrame--;
    }
}

static void fat_albert(void)
{
    if (IsOnScreen(g_CamX, g_CamY, g_HercX, g_HercY)) {
        if (g_HercHit == 0) {
            if (0 < g_HercDoor && g_HercFrame == 0) {
                Sprite_Queue(g_HercX - g_CamX - 0x24, g_HercY - g_CamY + 0xe + g_HercDoor, 0x199);
                Sprite_Queue(g_HercX - g_CamX - 0xd, g_HercY - g_CamY + 0xe + g_HercDoor, 0x19a);
            }
            if (g_HercDrop != 0) Sprite_Queue(g_HercX - g_CamX + 0xc, g_HercY - g_CamY + 0x11, 0x1a4 - g_HercDrop);
            Sprite_Queue(g_HercX - g_CamX, g_HercY - g_CamY, g_HercFrame + 0x131);
            /* propeller: y from Sprite_GetX (sic), x without the camera (sic) - player.md Correction */
            int id = g_HercFrame * 2 + 0x19b + g_FrameParity;
            int y = (g_HercY - g_CamY) - (u16)Sprite_GetX(g_HercFrame + 0x131) + g_PropDY[g_HercFrame];
            int x = g_PropDX[g_HercFrame] + (g_HercX - (u16)Sprite_GetX(g_HercFrame + 0x131));
            Sprite_Queue(x, y, id);
        } else {
            wreck(g_HercX, g_HercY, 0x134, 0x135, g_HercWreck, &g_HercWreck);
        }
    }
    g_HercX += g_HercDown * 4 - 8;
    g_HercY += g_HercDown * 0x10;
    if (g_HercX < g_DropZoneX + 0xa0 && g_HercDrop < 3 && 3000 < g_HercX) g_HercDrop++;
    if (g_DropZoneX - 0x2d0 < g_HercX && g_ProjCount < 0x15 && g_HercX < g_DropZoneX + 0x40 && g_HercDown == 0
        && (Rand(10) == 1 || g_ProjCount == 0)) {
        g_EnemyBombWeapon = 0x35;
        g_ObjX = g_HercX + 0x20;
        g_ObjY = g_HercY + 9;
        g_ObjDir = 0;
        Enemy_DropBomb();
    }
    if (500 < g_HercY) {
        g_HercTimer++;
        if (0x20 < g_HercTimer) g_HercFrame = min_i(g_HercFrame + 1, 2);
        if (0x10 < g_HercTimer && g_FrameParity == 0 && 0 < g_HercDoor) g_HercDoor--;
    }
    if (g_HercX < 3000) {
        g_HercFrame = min_i(g_HercFrame + 1, 2);
        g_HercDrop = max_i(g_HercDrop - 1, 0);
        g_DropZoneX = 0;
        if (g_HercY < -0x800) g_HercActive = 0;
    }
    if (g_HercFrame < 1) {
        g_HercVY = 0;
    } else {
        g_HercY += g_HercVY;
        int lim = g_HercFrame * -6;
        g_HercVY = (lim <= g_HercVY - 1) ? g_HercVY - 1 : lim;
        if (g_HercY < 0 && 3000 < g_HercX) g_HercFrame--;
    }
}

static void b52(void)
{
    if (IsOnScreen(g_CamX, g_CamY, g_B52X, g_B52Y)) {
        if (g_B52Hit == 0) {
            Sprite_Queue(g_B52X - g_CamX, g_B52Y - g_CamY, 0x138);
            if (Rand(1) != 0 && g_B52Hp < 5) {                           /* contrail (Rand order: life, a5, vy) */
                int life = Rand(4) + 0xc;
                int a5 = Rand(8) << 6;
                int vy = -Rand(4) << 6;
                Particle_Spawn((g_B52X + 0x20) << 8, (g_B52Y + 0x10) << 8, 0, vy, a5, life, 0x18);
            }
        } else {
            wreck(g_B52X, g_B52Y, 0x139, 0x198, g_B52Wreck, &g_B52Wreck);
        }
    }
    g_B52X += g_B52Hit * 6 - 0xc;
    g_B52Y += g_B52Hit * 0x10;
    if (g_B52TargetX < g_B52X && g_B52Y < -400) g_B52Y += (-600 < g_B52Active) * -4 + 8;   /* sic: the flag */
    if (0x100 < g_B52Y) {
        Explosion_Damage(g_B52X - 0x20, g_B52Y, 0, 0, 2000, 2000);
        Explosion_Damage(g_B52X - g_B52Wreck, g_B52Y, 0, 0, 2000, 2000);
        g_B52Active = 0;
        g_B52TargetX = 0;
    }
    g_B52Phase = Wrap(g_B52Phase + 1, 0, 3);
    if (g_B52TargetX - 0x2d0 < g_B52X && g_B52Phase == 0 && g_ProjCount < 0x15 && g_B52X < g_B52TargetX + 0x50 && g_B52Hit == 0) {
        g_EnemyBombWeapon = 0xd;                                         /* carpet bombing */
        g_ObjX = g_B52X;
        g_ObjY = g_B52Y;
        g_ObjDir = 0;
        Enemy_DropBomb();
    }
    if (g_B52X < g_B52TargetX - 0x140 && g_B52Hit == 0) g_B52Y += (g_B52X < 0x640) * -0x10 - 4;
    if (g_B52X < 0 || g_B52Y < -0x800) { g_B52Active = 0; g_B52TargetX = 0; }
}

static void ship(void)
{
    if (IsOnScreen(g_CamX, g_CamY, g_ShipX, g_ShipY)) {
        Sprite_Queue(g_ShipX - g_CamX, g_ShipY - g_CamY, g_ShipFrame + 0x123);
        if (g_ShipX - 0x20 <= PX && PX <= g_ShipX + 0x20 && g_ShipFrame < 4 && Rand(100) == 1) {   /* dives */
            g_ShipFrame = 4;
            g_ShipDir = 1;
        }
    }
    if (g_ShipFrame == 0 || g_ShipFrame == 3) {
        g_ShipX += g_ShipDir * 2;
        if (Map_GetTileAttr(Clamp(Div16(g_ShipX) + g_ShipDir, 0, g_MapWidth), 0x3f, 0) != 0x82) {
            g_ShipTurn = 1;
            g_ShipDir = -g_ShipDir;
        }
    }
    if (g_ShipTurn != 0 || 3 < g_ShipFrame) {
        g_ShipFrame += g_ShipDir;
        if (g_ShipFrame == 0 || g_ShipFrame == 3) { g_ShipFrame = Clamp(g_ShipFrame, 0, 3); g_ShipTurn = 0; }
        if (g_ShipFrame == 8) { g_ShipActive = 0; DS32(0x90120) = 0; }
    }
}

static void big_bomber(void)
{
    if (IsOnScreen(g_CamX, g_CamY, g_BigX, g_BigY)) {
        if (g_BigKilled == 0) Sprite_Queue(g_BigX - g_CamX, g_BigY - g_CamY, 0x12e);
        else wreck(g_BigX, g_BigY, 0x12f, 0x130, g_BigWreck, &g_BigWreck);
    }
    g_BigX += g_BigKilled * -4 + 8;
    g_BigY += g_BigKilled * 0x10;
    if (g_BaseStartX - 0x140 < g_BigX && g_ProjCount < 0x15 && g_FrameParity == 0 && g_BigX < g_BaseEndX && g_BigKilled == 0) {
        g_EnemyBombWeapon = 0xd;                                         /* stays 13 for the mission (Q22) */
        g_ObjX = g_BigX;
        g_ObjY = g_BigY;
        g_ObjDir = 0x10;
        Enemy_DropBomb();
    }
    if (g_BaseEndX < g_BigX) g_BigY += -2 + (g_BigY < -0x640) * 2 + (g_BigY < -0x708) * 4;
    if (g_BigKilled == 1 && g_BigHitDone == 0) {
        g_BigHitDone = 1;
        g_BigWreck = 0x20;
        for (DS32(0x90678) = 1; DS32(0x90678) < 5; DS32(0x90678)++)
            Explosion_Damage(DS32(0x90678) * 0x10 + g_BigX - 0x20, g_BigY + 0x10, 0, 0, 2000, 2000);
    }
    if (g_BigY < -2000 || g_MapWidth * 0x10 - 0x10 < g_BigX) g_BigActive = 0;
    if (0x100 < g_BigY) {
        Explosion_Damage(g_BigX - 0x20, g_BigY, 0, 0, 2000, 2000);
        Explosion_Damage(g_BigX + g_BigWreck, g_BigY, 0, 0, 2000, 2000);
        g_BigActive = 0;
    }
    /* lock-on cue (enemies.md §2.8, LOCKID 2, no 0x9c) */
    g_AimDirX = Sign(g_BigX - g_CamX - g_PlayerScrX);
    g_AimDirY = Sign(g_BigY - g_CamY - g_PlayerScrY);
    int ldx = g_BigX - g_CamX - g_PlayerScrX, ldy = g_BigY - g_CamY - g_PlayerScrY;
    DS32(0x90A00) = abs(ldy) + abs(ldx);
    Lock_Facing(1);
    Lock_Draw(abs(ldx) + abs(ldy), ldx, ldy, 2, 0x138, g_HudRows * -0x23 + 8, 0);
    /* its missiles at the player (player.md §6.5 "bomber flares") */
    if (DS32(0x90A00) < 0x280 && 199 < DS32(0x90A00) && g_EnemyMslCount < 4 && g_BigKilled == 0 && 0 < g_BigMissiles
        && Rand(6) > 3) {
        g_BigMissiles--;
        int n = g_EnemyMslCount;
        DS32A(0x8FAB8)[n] = g_BigX;
        DS32A(0x8FAC8)[n] = g_BigY;
        DS32A(0x8FA58)[n] = 4;
        DS32A(0x8FA68)[n] = 0x1e;
        g_EnemyMslCount++;
        Flare_Release();
    }
}

static void ground_pickup(void)
{
    g_PickupWalk = 1;
    if (IsOnScreen(g_CamX, g_CamY, g_PickupX, g_PickupY) && g_PilotX == 0) {
        if (g_Winch == 0 && g_HasWinch == 1) { g_Winch = 1; g_WinchLen = 0; }
        if (g_PickAnim < 3 || g_MP_PickupSprite == 0xcd || g_PickWalker == 0)
            Sprite_Queue(g_PickupX - g_CamX, g_PickupY - g_CamY, (u32)g_MP_PickupSprite % 500 + min_i(g_PickAnim, g_PickWalker * 3));
        else
            Sprite_Queue(g_PickupX - g_CamX, g_PickupY - g_CamY, g_PickAnim + 0xd6);
        int y = (g_PickupY - g_CamY) + 1 + g_ScrollFineY;
        if (y >= 0xc0 + VIEW_EXTRA_ROWS) y = 0xbf + VIEW_EXTRA_ROWS;     /* ENH: view */
        if (Video_ReadPixel(g_PickupX - g_CamX + g_ScrollFineX, y, g_BackPage) == 0) {
            if (Video_ReadPixel(g_PickupX - g_CamX + g_ScrollFineX, (g_PickupY - g_CamY) + g_ScrollFineY, g_BackPage) != 0) {
                g_PickupY--;
                g_PickupWalk = 0;
            }
        } else {
            g_PickupY = min_i(g_PickupY + 1, 1000);
            g_PickupWalk = 0;
        }
    }
    if (500 < g_MP_PickupSprite && g_MP_PickupSprite < 1000) {          /* rides convoy vehicle 0x90a40 */
        int v = g_AgentVehicle;
        g_PickupX = DS32A(0x91F74)[v];
        int flip = DS32A(0x91F50)[v] < 0;
        int h = (s32)Sprite_GetHeight(DS32A(0x8DE38)[flip + DS32A(0x918E0)[v] * 2]);
        g_PickupY = (DS32A(0x91F98)[v] - h) + 2;
        if (g_PickupX < 0) {
            g_MissionBonus = 0;
            g_MP_PickupCol = 0;
            g_MP_PickupSprite = 0;
            g_PickTarget = 0;
            DS32(0x90A10) = 1;
            Hud_PushMessage(DSTR(0x8B058));                              /* AGENT KILLED ! */
        }
    }
    if (g_Winch == 1 && (g_PickupX < g_CamX - 0x40 || g_CamX + 0x17c + VIEW_EXTRA_COLS < g_PickupX)) g_Winch = 3;   /* ENH: view */
    if (0 < g_Winch && g_IsHeli == 1) {
        Sprite_Queue(g_PlayerScrX, g_PlayerScrY + g_WinchLen, 0xab);
        if (g_Winch == 1) {
            int e = (g_PickupY - g_CamY) - g_PlayerScrY - g_WinchLen;
            int l = (Sign(e) + g_WinchLen < 0) ? 0 : g_WinchLen + Sign(e);
            g_WinchLen = min_i(l, 0x30);
            if (g_PickupY < PY + g_WinchLen) g_WinchLen = max_i((g_PickupY - g_CamY) - g_PlayerScrY, 4);
        } else {
            if (g_Winch == 2) {
                g_PickupX = PX;
                g_PickupY = PY + g_WinchLen;
                g_PickAnim = 0;
            }
            g_WinchLen--;
            if (g_WinchLen < 4) {
                if (g_Winch == 2 && g_EjectState == 0) {
                    Hud_PushMessage(DSTR(0x8B0A8));                      /* PICKUP COMPLETE ! */
                    g_PilotX = 1;
                    g_PickTarget = g_PickupX;
                    g_MP_PickupCol = 0;
                    g_MP_PickupSprite = (u16)(g_MP_PickupSprite % 500);
                }
                g_Winch = 0;
            }
        }
    }
    g_Scratch690 = 0;
    if ((g_OnGround == 1 && g_MP_PickupSprite < 500 && (g_SpeedBits < 0x3f000000 || g_IsHeli == 0)
         && (PX < g_BaseStartX || g_BaseEndX < PX))
        || (0 < g_WinchLen && g_PickupX - 8 <= PX && PX <= g_PickupX + 8 && g_PickupY <= PY + g_WinchLen))
        g_Scratch690 = 1;
    if (500 < g_MP_PickupSprite && g_SpeedBits < 0x3f000000 && g_PickupX - 0x10 < PX && PX < g_PickupX + 0x10
        && g_PickupY - 0x10 < PY)
        g_Scratch690 = 1;
    if (g_Scratch690 == 1) {
        if (PX - 0x10 < g_PickupX && g_PickupX < PX + 0x10) {
            Hud_PushMessage(DSTR(0x8B0F8));                              /* THANKS BUD ! */
            if (g_Winch == 1) {
                g_Winch = 2;
            } else if (g_EjectState == 0) {
                g_PilotX = 1;
                g_PickTarget = g_PickupX;
                g_MP_PickupCol = 0;
                g_MP_PickupSprite = (u16)(g_MP_PickupSprite % 500);
            }
        } else if (g_MP_PickupSprite < 500 && g_PickWalker == 1) {
            g_PickAnim = max_i(g_PickAnim, 3);
            g_PickupX += Sign(PX - g_PickupX) * g_PickupWalk;
            g_PickupWalk = 0;
        }
    } else if (g_PickTarget == g_PickupX) {
        g_PickAnim = min_i(g_PickAnim, 2);
    }
    if (g_PickWalker == 0 || g_MP_PickupSprite == 0xcd) g_PickAnim = 0;
    if (g_PickTarget != 0 && g_PickupX != g_PickTarget) {
        if (g_PickWalker == 1) g_PickupX += Sign(g_PickTarget - g_PickupX) * g_PickupWalk;
        g_MP_PickupCol = (u16)Div16(g_PickupX);
        g_MP_PickupSprite = (u16)(DS32(0x90A30) % 500);
        g_PickAnim = max_i(g_PickAnim, 3);
        if (g_PickupX == g_PickTarget) g_PickTarget = 0;
        g_LoopI = Map_GetTileAttr(Div16(g_PickupX), 0x3f, 0);
        if (g_LoopI == 0x82 && (g_MP_PickupSprite == 0xca || g_MP_PickupSprite == 0xac)) {   /* drowns */
            g_PickTarget = 0;
            g_MP_PickupSprite = 0xcd;
            g_PickAnim = 0;
        }
    }
    if (g_PickWalker == 1 || g_MP_PickupSprite == 0xcd) {
        if (g_PickAnim < 2) {
            g_PickAnim = 1 - g_PickAnim;
            if (Rand(5) == 1) g_PickAnim = 2;
        } else if (g_PickAnim < 3) {
            if (Rand(8) == 0) g_PickAnim = 0;
        } else {
            g_PickAnim = Clamp(7 - g_PickAnim, 3, 4);
        }
    }
}

/* 0x2780b SupportAircraft_Update (was Player_DeathAndLanding) */
void SupportAircraft_Update(void)
{
    if (g_TowX != 0) tow_plane();
    if (g_HercActive != 0) fat_albert();
    if (g_B52Active != 0) b52();
    if (g_ShipActive != 0) ship();
    if (g_BigActive != 0) big_bomber();
    SupportAircraft_Flares();                                            /* weapons.c (0x28e6d) */
    if (g_MP_PickupCol != 0 && g_MP_PickupSprite < 1000 && 0 < g_CamY + VIEW_EXTRA_ROWS) ground_pickup();   /* ENH: view (camera height) */
}

/* 0x27008 Tanker_Update (was Player_Ejection): tanker 0x8ff64/0x8ff6c, hit 0x8ffbc, frame 0x8ffc8, drogue 0x8ffc0,
 * timer 0x8ff60, vy 0x8ff54, door 0x8ff58. The wreck increments Fat Albert's counter 0x90548 (player.md Q14). */
void Tanker_Update(void)
{
#define TX   DS32(0x8FF64)
#define TY   DS32(0x8FF6C)
#define THIT DS32(0x8FFBC)
#define TFR  DS32(0x8FFC8)
#define HOSE DS32(0x8FFC0)
#define TTIM DS32(0x8FF60)
#define TVY  DS32(0x8FF54)
#define DOOR DS32(0x8FF58)
    if (!IsOnScreen(g_CamX, g_CamY, TX - (u16)Sprite_GetX(g_TankerType + 0x131), TY)) {
        int x = TX - g_CamX;                                             /* arrow at the screen edge */
        if (x > 0x134 + VIEW_EXTRA_COLS) x = 0x134 + VIEW_EXTRA_COLS;    /* ENH: view */
        if (x < 8) x = 8;
        int y = TY - g_CamY;
        if (y > 0xa8 + VIEW_EXTRA_ROWS) y = 0xa8 + VIEW_EXTRA_ROWS;
        if (y < 8) y = 8;
        Sprite_Queue(x, y, 0x1e6);
    } else if (THIT == 0) {
        if (0 < DOOR && TFR == 0) {
            int y = (TY - g_CamY) - (u16)Sprite_GetY(0x131) + 0x1f + DOOR;
            int x = (TX - g_CamX) - (u16)Sprite_GetX(0x131) + 9;
            Sprite_Queue(x, y, 0x199);
            y = (TY - g_CamY) - (u16)Sprite_GetY(0x131) + 0x1f + DOOR;
            x = (TX - g_CamX) - (u16)Sprite_GetX(0x131) + 0x20;
            Sprite_Queue(x, y, 0x19a);
        }
        Sprite_Queue(TX - g_CamX, TY - g_CamY, g_TankerType + 0x131);
        {
            int id = g_TankerType * 2 + 0x19b + g_FrameParity;
            int y = (TY - g_CamY) - (u16)Sprite_GetY(g_TankerType + 0x131) + g_PropDY[TFR];
            int x = g_PropDX[TFR] + ((TX - g_CamX) - (u16)Sprite_GetX(g_TankerType + 0x131));
            Sprite_Queue(x, y, id);
        }
        if (0 < HOSE) {
            for (g_LoopI = 0; g_LoopI < 5; g_LoopI++) Sprite_Queue(g_LoopI * HOSE + (TX - g_CamX), (TY - g_CamY) + 7, 0x136);
            Sprite_Queue(HOSE * 5 + (TX - g_CamX), (TY - g_CamY) + 7, 0x137);
        }
    } else {
        s32 ofs = DS32(0x8FFF4);
        wreck(TX, TY, 0x134, 0x135, ofs, &DS32(0x90548));                /* Q14 */
    }
    TX += THIT * 4 - 8;
    TY += ((0x4b0 < TX && TY < -500) ? 2 : 0) + THIT * 0x10;
    /* ENH: view: kept, player-relative */
    if (PX - 0x140 < TX && TX < PX + 0x140 && PY - 0xa0 < TY && TY < PY + 0xa0 && TFR == 0 && THIT == 0 && TY < 0
        && g_DirHalf == 0 && g_ProbeDX != 0) {                           /* refuelling */
        int ex = PX + g_ProbeDX - 0x60 - TX;
        TX += Clamp(Sign(ex) * abs(ex), -0x10, 4);
        int ey = PY + g_ProbeDY - 7 - TY;
        int st = abs(ey) > 4 ? 4 : abs(ey);
        TY += Sign(ey) * st;
        HOSE = min_i(HOSE + 1, 0x10);
        if (HOSE == 0x10 && TX + 0x5c < g_ProbeDX + PX && g_ProbeDX + PX < TX + 100 && TY + 3 < g_ProbeDY + PY
            && g_ProbeDY + PY < TY + 0xb)
            g_Fuel = min_i(g_Fuel + 100, g_FuelBase + DS32(0x90648) * 1000);
    } else if (0 < HOSE) {
        HOSE--;
    }
    if (-500 < TY) {
        TTIM++;
        if (0x40 < TTIM) TFR = min_i(TFR + 1, 2);
        if (0x10 < TTIM && g_FrameParity == 0 && 0 < DOOR) DOOR--;
    }
    if (TX < g_BaseStartX - 800) {
        TFR = min_i(TFR + 1, 2);
        if (TY < -0x800) { TY = -0x800; TX = g_MapWidth * 0x10 - 0x140; TFR = 0; }
    }
    if (TFR < 1) {
        TVY = 0;
    } else {
        HOSE = 0;
        TY += TVY;
        TVY--;
        if (TVY < TFR * -6) TVY = TFR * -6;
        if (TY < 0 && 3000 < TX) TFR--;
    }
#undef TX
#undef TY
#undef THIT
#undef TFR
#undef HOSE
#undef TTIM
#undef TVY
#undef DOOR
}
