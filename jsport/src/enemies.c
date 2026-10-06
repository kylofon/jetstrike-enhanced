/* Enemies and world objects (port/spec/enemies.md): enemy aircraft (EnemyBomber_Spawn, EnemyAir_Update and its
 * helpers), enemy missiles, ground gunships, enemy shells, ejected enemy pilots, the convoy, the MP2 emplacements
 * (gun / flak / SAM), the target-zone vehicles, the player's commandos, the emplacement under construction, the
 * airbase (vehicles, rearm -> WeaponSelect_Screen, crew, runway repair, base-hit losses), the bonus crate and its
 * twelve handlers, the prize balloon pickup, the alien abduction and Enemy_DropBomb.
 *
 * Every object array and every "current object" scratch global lives in the data-segment image at its original
 * address: the originals leak values between objects and updaters (enemies.md Q10), overflow their arrays (Q8) and
 * swap only some arrays on removal (Q15); all of that is reproduced by working on the image directly. The shared
 * loop index g_LoopI is used as the original does (callees read it). Rand() calls are in the original's order,
 * including the re-evaluated clamp macros (CLAMPR, enemies.md). */
#include "mission.h"

#include <stdlib.h>
#include <string.h>

#include "level.h"
#include "platform.h"
#include "sound.h"
#include "video.h"

#define PX (g_CamX + g_PlayerScrX)
#define PY (g_CamY + g_PlayerScrY)

static int min_i(int a, int b) { return a < b ? a : b; }
static int max_i(int a, int b) { return a > b ? a : b; }

/* ---- shared tables (enemies.md §1) */
#define T_Dir16X     DS32A(0x8FC30)             /* (int)(-cos*8): movement, read [dir] or [2*d] */
#define T_Dir16Y     DS32A(0x8FC70)
#define T_Dir16Xb    DS32A(0x8FE10)             /* (int)(-cos*8), (int)(-sin*3): facing tests (mission.h g_Dir16X) */
#define T_Dir16Yb    DS32A(0x8FE50)
#define HEADING(dx, dy) ((s32)(D16(0x92982 + ((dx) + 1) * 6 + (dy) * 2) / 2))

/* ---- scratch globals */
#define g_CurX       DS32(0x9080C)
#define g_CurY       DS32(0x90810)
#define g_ObjX       DS32(0x909A8)              /* current object x (EnemyAir, convoy; spawn / launch point) */
#define g_ObjY       DS32(0x909AC)
#define g_ObjDir     DS32(0x909FC)
#define g_ObjThr     DS32(0x909F0)
#define g_ObjSpd     DS32(0x909D4)
#define g_ObjHp      DS32(0x909CC)
#define g_ObjState   DS32(0x90990)
#define g_CurVX      DS32(0x90934)
#define g_CurVY      DS32(0x90938)
#define g_AimDirX    DS32(0x90A24)
#define g_AimDirY    DS32(0x90A28)
#define g_ViewCamX   DS32(0x90618)
#define g_ViewCamY   DS32(0x9061C)
#define g_TargetX    DS32(0x9001C)
#define g_TargetY    DS32(0x90020)
#define g_PXe        DS32(0x90038)
#define g_PYe        DS32(0x9004C)
#define g_PYg        DS32(0x90050)
#define g_AiLevel    DS32(0x90220)
#define g_FaceX      DS32(0x90598)              /* player facing for the lock reticle */
#define g_FaceY      DS32(0x9059C)
#define g_Reticle    DS32(0x903F0)
#define g_LockDist   DS32(0x90420)
#define g_LockDX     DS32(0x90424)
#define g_LockDY     DS32(0x90428)
#define g_HudRows    DS32(0x90308)
#define g_EnemyDist  DS32(0x90A00)
#define g_Loop724    DS32(0x90724)              /* second shared loop index */
#define g_Spr98c     g_LoopJ                    /* 0x9098c (g_ProjSprite) */

/* ---- enemy aircraft (§2.1) */
#define g_EnemyAirX     DS32A(0x9264C)
#define AY           DS32A(0x92660)
#define ADM          DS32A(0x92638)
#define ADIR         DS32A(0x8FBE0)
#define AHP          DS32A(0x9270C)
#define ATHR         DS32A(0x92548)
#define ASPD         DS32A(0x9281C)
#define ABURN        DS32A(0x8F0F0)
#define AWX          DS32A(0x8F118)
#define AWY          DS32A(0x8F128)
#define AMIS         DS32A(0x8FC00)
#define ABOMB        DS32A(0x8E3D8)
#define ASTATE       DS32A(0x8EC78)
#define AEJECT       DS32A(0x90B34)
#define g_EnemySkill    DS32(0x906E0)
#define g_EnemyGunDamage DS32(0x906D8)
#define g_EnemyMissiles DS32(0x906F8)
#define g_EnemyBombs    DS32(0x906B8)
#define g_EnemyBombWeapon DS32(0x906F0)
#define g_EnemyBombRef  DS32(0x906CC)
#define g_SpawnCount    DS32(0x90A5C)
#define g_WanderX       DS32(0x90A08)
#define g_WanderY       DS32(0x90A0C)
#define g_AirKilledTot  DS32(0x90410)
#define g_MP_EnemySpawnCol D16(0x9165E)         /* p11 */
#define g_MP_EnemyWakeCol  D16(0x91660)         /* p12 */
#define g_BonusType     DS32(0x90890)
#define g_BonusX        DS32(0x90894)
#define g_BonusY        DS32(0x90898)
#define g_BonusChute    DS32(0x9088C)

/* ---- enemy missiles (§3) */
#define g_EnemyMslCount DS32(0x906FC)
#define MX           DS32A(0x8FAB8)
#define MY           DS32A(0x8FAC8)
#define MDIR         DS32A(0x8FA58)
#define MLIFE        DS32A(0x8FA68)
#define g_MslDir     DS32(0x908AC)
#define g_MslLife    DS32(0x90704)

/* ---- shells (§5) */
#define g_ShellCount DS32(0x8FFA0)
#define SX           DS32A(0x8E388)
#define SY           DS32A(0x8E3B0)
#define SVX          DS32A(0x8DDE8)
#define SVY          DS32A(0x8DE10)
#define SLIFE        DS32A(0x8E350)
#define SSIZE        DS32A(0x8DF28)

/* ---- ground gunships (§4) */
#define GX           DS32A(0x8D9D8)
#define GY           DS32A(0x8D9E8)
#define GDIR         DS32A(0x8D9C8)
#define GVY          DS32A(0x8D8F8)
#define GMIS         DS32A(0x8D9B8)
#define GSHELL       DS32A(0x8D7D8)
#define GHP          DS32A(0x8D9A8)
#define GDM          DS32A(0x8D998)
#define G8E8         DS32A(0x8D8E8)

/* ---- pilots (§6) */
#define g_PilotCount DS32(0x90754)
#define EPX          DS32A(0x8DB50)
#define EPY          DS32A(0x8DB78)
#define EPST         DS32A(0x8DB28)
#define EPVY         DS32A(0x8D7F0)

/* ---- convoy (§7) */
#define g_ConvoyCount   DS32(0x905AC)
#define CVX          DS32A(0x91F74)
#define CVY          DS32A(0x91F98)
#define CVVX         DS32A(0x91F50)
#define CVSLOT       DS32A(0x918E0)
#define CVST         DS32A(0x91F2C)
#define CVSMK        DS32A(0x91904)
#define CVHP         DS32A(0x91928)
#define g_ConvoyTrain   DS32(0x8FFE0)
#define g_ConvoySprites DS32A(0x8DAB8)
#define g_MP_ConvoyKind D16(0x91662)            /* p13 */
#define g_MP_ConvoyRow  D16(0x9166A)            /* p17 */
#define g_MP_ConvoySpacing D16(0x9166C)         /* p18 */
#define g_MP_ConvoyFlag D16(0x91676)            /* p23 */
#define g_AgentVehicle  DS32(0x90A40)

/* ---- target vehicles (§9) */
#define g_TVCount    DS32(0x900B4)
#define TVX          DS32A(0x8E400)
#define TVY          DS32A(0x8E418)
#define TVST         DS32A(0x8E3E8)

/* ---- commandos (§10.2) */
#define g_CommandoCount DS32(0x902E0)
#define CMX          DS32A(0x8DDC0)
#define CMY          DS32A(0x8DD98)
#define CMVX         DS32A(0x8DD20)
#define CMANIM       DS32A(0x8DD70)
#define CMFALL       DS32A(0x8DD48)

/* ---- the B52 / Hercules / big bomber (player.md §6) */
#define g_B52Active  DS32(0x90998)
#define g_B52X       DS32(0x90984)
#define g_B52Y       DS32(0x90988)
#define g_HercActive DS32(0x90528)
#define g_HercX      DS32(0x90520)
#define g_HercY      DS32(0x904C0)

#define g_FlareCount DS32(0x90310)
#define g_FlareX     DS32A(0x8FAE0)
#define g_FlareY     DS32A(0x8FAE8)
#define g_RackSel_   DS32(0x90338)
#define g_LaunchX    DS32(0x90878)
#define g_LaunchY    DS32(0x9087C)
#define g_LaunchDirX DS32(0x90784)
#define g_LaunchDirY DS32(0x90790)

/* the player-projectile arrays (weapons.md §2.2) used by Enemy_DropBomb / Convoy_LaunchMissile */
#define P_KIND       DS32A(0x91384)
#define P_ARM        DS32A(0x912B0)
#define P_MOTOR      DS32A(0x914AC)
#define P_FLIGHT     DS32A(0x91330)
#define P_DETONATE   DS32A(0x9125C)
#define P_FLAGS      DS32A(0x913D8)
#define P_BLASTA     DS32A(0x92720)
#define P_BLASTB     DS32A(0x927C8)
#define P_TX         DS32A(0x92590)
#define P_TY         DS32A(0x925E4)
#define P_FRAME      DS32A(0x92774)
#define P_LIFE       DS32A(0x926B8)

static void swap_last(s32 *a, int i, int n) { SwapInt(&a[i], &a[n - 1]); }

/* 0x268be Tracer_Stub: empty (its arguments carry no Rand calls except where noted at the call sites). */

/* 0x3cd9e Heading_TurnToward(dir, dx, dy) = TurnTab[dir][Oct(dx, dy)] (tables of the image) */
static int Heading_TurnToward(int dir, int dx, int dy)
{
    return DS32A(0x80584)[dir * 8 + DS32A(0x80568)[dy * 4 + dx]];
}

/* 0x3cdec Map_ScanAround(x): lowest live ground row over columns x-10..x+9 (i == W not wrapped, level.md Q19) */
static int Map_ScanAround(int x)
{
    u32 m = 0x3f;
    for (DS32(0x90470) = x - 10; DS32(0x90470) < x + 10; DS32(0x90470)++) {
        int i = DS32(0x90470);
        int c = i < 0 ? i + g_MapWidth : (g_MapWidth < i ? i - g_MapWidth : i);
        u32 b = Byte_Get(g_MapVal, c + 0x400) & 0xff;
        if (b < m) m = b;
    }
    return (int)m;
}

/* 0x389b0 Sfx_RandomAmbient (enemies.md EnemyFire_Sfx): one-shot voice lines, Rand only while the latch is 0 */
static void Sfx_RandomAmbient(void)
{
    if (DS32(0x9018C) == 0 && Rand(0x14) == 1) {
        Sfx_Play(0x10, 4000, 0x3f, PX);
        DS32(0x9018C) = 1;
        g_SfxBusyTimer = 8;
    }
    if (DS32(0x9014C) == 0 && Rand(10) == 1) {
        Sfx_Play(0x1a, 4000, 0x3f, PX);
        DS32(0x9014C) = 1;
        g_SfxBusyTimer = 8;
    }
}

static void missile_add(int x, int y, int d, int life)
{
    int n = g_EnemyMslCount;
    MX[n] = x;
    MY[n] = y;
    MDIR[n] = d;
    MLIFE[n] = life;
    g_EnemyMslCount++;
}

/* the lock reticle (enemies.md §2.8): player facing (heliHalf: the heli test reads g_DirHalf/2, else g_Dir/2) */
void Lock_Facing(int heliHalf)
{
    g_FaceX = T_Dir16Xb[g_DirHalf / 2];
    g_FaceY = T_Dir16Yb[g_DirHalf / 2];
    if (g_IsHeli != 0) {
        g_FaceY = 3;
        if (g_DirHalf < 6) {
            g_FaceX = -0x10;
            if ((heliHalf ? g_DirHalf / 2 : g_Dir / 2) == 2) g_FaceX = 0;
        } else {
            g_FaceX = 0x10;
        }
    }
}

/* the reticle itself; the caller has set g_AimDirX/Y = Sign(object - player) and the facing */
void Lock_Draw(int dist, int ldx, int ldy, int lockid, int xmax, int ymin, int upgrade)
{
    if (Sign(g_FaceX) == g_AimDirX && Sign(g_FaceY) == g_AimDirY) {
        g_Reticle = 0;
        g_LockDist = dist;
        g_LockDX = ldx;
        g_LockDY = ldy;
        int t0 = g_WeaponThrust[g_RackWeapon[0]], t1 = g_WeaponThrust[g_RackWeapon[1]];
        int t = t1 < t0 ? t0 : t1;
        int rng = t * 0x20 < 0x4b0 ? 0x4b0 : t << 5;
        if (g_LockDist < rng) { g_Reticle = 0x9b; g_LockTarget = lockid; }
        if (upgrade && g_LockDist < 0x140) g_Reticle = 0x9c;
        if (g_Reticle > 0) {
            int y = min_i(g_PlayerScrY + g_LockDY, 0xa8 + VIEW_EXTRA_ROWS);     /* ENH: view: the screen edges */
            if (y < ymin) y = ymin;
            int x = min_i(g_PlayerScrX + g_LockDX, xmax + VIEW_EXTRA_COLS);
            if (x < 8) x = 8;
            Sprite_Queue(x, y, g_Reticle);
        }
    }
}

/* ====================================================================== Enemy_DropBomb (§2.9) */

/* 0x17944 Enemy_DropBomb: fires g_EnemyBombWeapon from (0x909a8, 0x909ac) through the player launch routines;
 * the jump table is indexed with k0+1 (Q5). */
void Enemy_DropBomb(void)
{
    DS32(0x8FEE8) = g_RackWeapon[0];
    DS32(0x8FEAC) = g_PlayerVX;
    g_LaunchX = g_ObjX;
    g_LaunchY = g_ObjY;
    g_PlayerVX = 0;
    g_LaunchDirX = DS32A(0x8FAF0)[g_ObjDir / 2];
    g_LaunchDirY = DS32A(0x8FB30)[(g_ObjDir / 2) * 2];
    DS32(0x9002C) = g_WeaponType[g_EnemyBombWeapon * 6] + 1;
    g_Scratch690 = 0;
    g_RackSel_ = 0;
    g_RackWeapon[0] = g_EnemyBombWeapon;
    switch (DS32(0x9002C)) {
    case 0: case 3: case 6: case 7: Weapon_LaunchBallistic(); break;
    case 1: Weapon_FireRocket(); break;
    case 2: Weapon_FireGuided(); break;
    case 4: Weapon_TakePhoto(); break;
    case 5: Weapon_DropTank(); break;
    case 8: Weapon_ArmJP233(); break;
    case 9: break;                               /* 0x1821c Stub_Kind9: empty */
    case 10: Weapon_DropCommando(); break;
    default: break;
    }
    int n = g_ProjCount, w = g_RackWeapon[g_RackSel_];
    P_KIND[n] = g_WeaponType[w * 6 + 0];
    P_ARM[n] = g_WeaponType[w * 6 + 1];
    P_MOTOR[n] = g_WeaponType[w * 6 + 2];
    P_FLIGHT[n] = g_WeaponType[w * 6 + 3];
    P_DETONATE[n] = g_WeaponType[w * 6 + 4];
    P_FLAGS[n] = g_WeaponType[w * 6 + 5];
    g_RackWeapon[g_RackSel_] = DS32(0x8FEE8);
    P_TX[n] = 0;
    P_TY[n] = 0;
    g_ProjCount = min_i(g_ProjCount + 1, 0x15);
    g_PlayerVX = DS32(0x8FEAC);
}

/* ====================================================================== enemy aircraft (§2) */

static void air_remove(int i)
{
    int n = g_EnemyAirCount;
    swap_last(g_EnemyAirX, i, n); swap_last(AY, i, n); swap_last(ADM, i, n); swap_last(ADIR, i, n);
    swap_last(AHP, i, n); swap_last(ATHR, i, n); swap_last(ASPD, i, n); swap_last(ABURN, i, n);
    swap_last(AMIS, i, n); swap_last(ABOMB, i, n); swap_last(ASTATE, i, n); swap_last(AEJECT, i, n);
    g_EnemyAirCount--;
}

/* p11*16 + Rand(600) - Rand(600) (one evaluation of the clamped expression) */
static int spawn_x(void)
{
    u32 c = g_MP_EnemySpawnCol;
    int a = Rand(600);
    int b = Rand(600);
    return (int)((c * 0x10 + (u32)a) - (u32)b);
}

/* CLAMPR(spawn_x(), lo, hi) as compiled (2..4 evaluations) */
static int spawn_x_clamped(int lo, int hi)
{
    int m = (hi < spawn_x()) ? hi : spawn_x();
    if (m < lo) return lo;
    return (hi < spawn_x()) ? hi : spawn_x();
}

/* 0x38ba8 EnemyBomber_Spawn (§2.2): 0x90a5c aircraft; the body runs once even for 0 (Q25) */
void EnemyBomber_Spawn(void)
{
    if (g_GameMode < 3) {
        g_LoopI = 0;
        do {
            int i = g_LoopI;
            if (g_MP_EnemyAir == 0) {
                if (g_EnemyBaseX < 1 || (g_Mission / 10) * 3 + 5 <= g_AirKilledTot || g_GameMode == 1) {
                    if (DS32(0x906DC) == 1 && DS32(0x90730) == 0) {
                        DS32(0x90730) = 1;
                        g_ObjX = DS32(0x9073C) + 0x40;
                        AY[i] = DS32(0x90740);
                        ASTATE[i] = 0;
                        if (i == 0) Hud_PushMessage(DSTR(0x813A4));           /* "Enemy Bomber Raiding!" */
                    } else {
                        g_ObjX = -999;
                    }
                } else {
                    g_ObjX = g_EnemyBaseX * 0x10 - 0x40;
                    AY[i] = g_EnemyBaseRow * 0x10 - 3;
                    ASTATE[i] = 0x81;
                    while (Map_GetTileAttr(Div16(g_ObjX), Div16(AY[i] + 0x12), 0) != 0x81 && Div16(AY[i]) < 0x3e)
                        AY[i] += 4;
                    AY[i] -= 0x14;
                    if (i == 0) Hud_PushMessage(HUDTEXT(54));                  /* ENEMY AIRCRAFT SCRAMBLING */
                }
            } else {
                g_ObjX = spawn_x_clamped(1, (g_MapWidth - 1) * 0x10);
                g_MP_EnemyAir = (u16)((int)g_MP_EnemyAir - 1 < 0 ? 0 : g_MP_EnemyAir - 1);
                AY[i] = -800;
                ASTATE[i] = 0;
            }
            ADM[i] = 0;
            g_EnemyAirX[i] = g_ObjX;
            ADIR[i] = 0;
            AHP[i] = 0;
            ATHR[i] = 4;
            ASPD[i] = 0;
            ABURN[i] = 0;
            AEJECT[i] = 0;
            AMIS[i] = g_EnemyMissiles;
            ABOMB[i] = g_EnemyBombs;
            g_LoopI++;
        } while (g_LoopI < g_SpawnCount);
        g_EnemyAirCount = g_SpawnCount;
        g_LoopI = g_SpawnCount;
        while (--g_LoopI > -1)
            if (g_EnemyAirX[g_LoopI] < 1) air_remove(g_LoopI);
    }
    g_SpawnCount = 0;
}

/* §2.3 part A: movement and drawing of every aircraft */
static void air_move_all(void)
{
    for (g_LoopI = 0; g_LoopI < g_EnemyAirCount; g_LoopI++) {
        int i = g_LoopI;
        if (g_EnemyAirX[i] <= -999) continue;
        g_ObjState = ASTATE[i];
        g_ObjX = g_EnemyAirX[i];
        g_ObjY = AY[i];
        g_ObjThr = ATHR[i];
        g_ObjHp = AHP[i];
        g_ObjDir = ADIR[i];
        g_ObjSpd = ASPD[i];
        if (IsOnScreen(g_CamX, g_CamY, g_ObjX, g_ObjY)) {
            if (ABURN[i] < 1) {
                int s = g_ObjDir + 0x5a + g_ObjState;
                Sprite_Queue(g_ObjX - g_CamX, g_ObjY - g_CamY, s < 0xdc ? s : 0xdb);
            } else {
                Sprite_Queue(g_ObjX - g_CamX, g_ObjY - g_CamY, Rand(2) + 0xc3);
                if (IsOnScreen(g_CamX, g_CamY, AWX[i], AWY[i])) {
                    Sprite_Queue(AWX[i] - g_CamX, AWY[i] - g_CamY, ABURN[i] + 0xc5);
                    ABURN[i] = Wrap(ABURN[i] + 1, 1, 4);
                }
            }
            if (ADM[i] > 0 && ADM[i] > 1) {
                DS32(0x8FF80) = DS32(0x900AC) / 2 + 0xc;
                DS32(0x900AC) += DS32(0x900B0);
                if (DS32(0x900AC) == -2 || DS32(0x900AC) == 2) DS32(0x900B0) = -DS32(0x900B0);
                g_ObjDir += Sign(DS32(0x8FF80) - g_ObjDir);
                if (10 < g_ObjDir && g_ObjDir < 0xe)
                    Sprite_Queue(g_ObjX - g_CamX, g_ObjY - g_CamY, (g_ObjDir - 0xc) * 2 + 0x116 + g_FrameParity);
            }
        }
        if (ADM[i] > 0) {
            if (ADM[i] > 1 && Rand(0x14) == 1 && AEJECT[i] == 0) {        /* pilot ejects: no bound check (Q26) */
                AEJECT[i] = 1;
                EPX[g_PilotCount] = g_ObjX;
                EPY[g_PilotCount] = g_ObjY;
                EPVY[g_PilotCount] = -8;
                EPST[g_PilotCount] = 1;
                g_PilotCount++;
            }
            if (Rand(100) > 0x62) ADM[i]++;
        }
        if (ABURN[i] != 0) AWY[i] += 8;
        g_ObjX += Clamp(T_Dir16X[g_ObjDir] * g_ObjSpd / 4, -0x10, 0x10);
        g_ObjY += Clamp(T_Dir16Y[g_ObjDir] * g_ObjSpd / 4 + ADM[i], -0x10, 0x10);
        g_ObjY = min_i(g_ObjY, 0x3f0);
        DS32(0x90394) = g_MapWidth * 0x10;
        if (DS32(0x90394) <= g_ObjX) g_ObjX -= g_MapWidth * 0x10;
        if (g_ObjX < 0) g_ObjX += DS32(0x90394);
        if (abs(PX + DS32(0x90394) - g_ObjX) < 0xa0 && g_CamX < 0xa0) g_ObjX += PX - DS32(0x90394);   /* Q2 */
        if (abs(PX - DS32(0x90394) + g_ObjX) < 0xa0 && DS32(0x90394) - 0xa0 < g_CamX) g_ObjX = PX + DS32(0x90394) - g_ObjX;
        g_EnemyAirX[i] = g_ObjX;
        ASTATE[i] = g_ObjState;
        AY[i] = g_ObjY;
        ASPD[i] = g_ObjSpd;
        ATHR[i] = g_ObjThr;
        AHP[i] = g_ObjHp;
        ADIR[i] = g_ObjDir;
    }
}

/* §2.5..2.7: the AI of aircraft g_LoopI (x > -999) */
static void air_ai(void)
{
#define I g_LoopI
    DS32(0x906C4) = Sign(T_Dir16Y[g_ObjDir]);
    DS32(0x90718) = Sign(T_Dir16X[g_ObjDir]);
    {
        int e = (T_Dir16Y[g_ObjDir] + g_ObjThr * 2) / 8 + g_ObjSpd - ADM[I];
        g_ObjSpd = Clamp(e, 0, 8);
    }
    g_AiLevel = 0;
    g_AimDirY = Sign(g_TargetY - g_ObjY);
    g_AimDirX = Sign(g_TargetX - g_ObjX);
    /* 1 take-off roll */
    if ((g_ObjSpd > 4 || g_ObjX < g_EnemyBaseX * 0x10 - 0x1e0) && g_ObjX < g_EnemyBaseX * 0x10 - 0x140 && ASTATE[I] > 0) {
        ASTATE[I] = 0;
        g_AimDirY = -1;
        g_AimDirX = 0;
        g_ObjDir = 1;
        g_ObjThr = 9;
        g_ObjSpd = max_i(g_ObjSpd, 5);
        g_AiLevel = 1;
    }
    /* 2 taxi */
    if (ASTATE[I] > 0) {
        g_AimDirY = 0;
        g_ObjDir = 0;
        g_AimDirX = -1;
        g_AiLevel = 1;
        g_ObjThr = 9;
    }
    /* 3 wander when far */
    if (g_AiLevel == 0 && (Rand(10) * DS32(0x90070) > 2 || Rand(10) * g_PlaneClass > 9)
        && (abs(g_ObjX - g_TargetX) > 800 || abs(g_ObjY - g_TargetY) > 600) && ABOMB[I] == 0) {
        g_AimDirX = g_WanderX;
        g_AimDirY = g_WanderY;
    }
    /* 4 */
    if (g_ObjY > 600 && ASTATE[I] == 0 && ABOMB[I] < 999) g_AimDirY = min_i(g_AimDirY, 0);
    /* 5 */
    if (g_MapWidth * 0x10 - 0x280 < g_ObjX) { g_AimDirX = -1; g_AiLevel = 1; }
    /* 6 terrain avoidance */
    if (g_ObjY > 200 && ASTATE[I] == 0 && ABOMB[I] < 999) {
        DS32(0x901BC) = Map_ScanAround(min_i(Div16(g_ObjX), g_MapWidth - 0xb));
        if (DS32(0x901BC) - 4 < Div16(g_ObjY)) {
            g_AimDirY = -1;
            g_AimDirX = 0;
            g_AiLevel = 2;
            g_ObjThr = 9;
        }
    }
    /* 7 evade when locked (wander X/Y swapped, sic) */
    if (g_LockTarget == I && Rand(100) < g_EnemySkill * 8 + 8 && ASTATE[I] == 0 && g_AiLevel == 0
        && abs(g_ObjX - g_TargetX) < 0xa0) {
        g_AimDirY = g_WanderX;
        g_AimDirX = g_WanderY;
    }
    /* 8 new wander direction */
    if (Rand(0x32) == 1) {
        g_WanderX = 1 - Rand(1) * 2;
        g_WanderY = 1 - Rand(1) * 2;
    }
    /* 9 too high above the camera (0x8fea0: Tileset_Load leftover) */
    if (g_ObjY < 0 && g_ObjY < g_CamY + DS32(0x8FEA0) - 0x80) {
        g_AimDirY = 1;
        g_AimDirX = 0;
        g_ObjSpd = 0;
        g_ObjThr = 0;
        g_AiLevel = 2;
    }
    /* 10 break-off when head-on and close */
    if ((-DS32(0x90718) == g_AimDirX || -DS32(0x906C4) == g_AimDirY) && g_ObjY < 700 && abs(g_ObjX - g_TargetX) < 200
        && abs(g_ObjY - g_TargetY) < 200 && Rand(10) < 4 && g_AiLevel == 0) {
        g_ObjThr = 0;
        g_AiLevel = 2;
    }
    /* 11 */
    if (g_AimDirY == -1 && (g_LockTarget != I || g_ObjY > 500) && g_AiLevel < 2) g_ObjThr = 9;
    /* 12 stall */
    if (g_ObjSpd < 3) { g_ObjY += 0x10; g_AimDirY = 1; g_ObjThr = 9; }
    /* 13 (unbounded) */
    if (g_ObjSpd < 5 && g_LockTarget != I) g_ObjThr++;
    /* 14 crippled or pilot gone: dive */
    if (ADM[I] > 1 || AEJECT[I] == 1) { g_AimDirY = 1; g_AimDirX = Rand(2) - 1; }
    /* 15 */
    if (g_AimDirX == 0 && g_AimDirY == 0) g_AimDirX = 1 - Rand(1) * 2;
    /* 16 turn (0x90770 is never written) */
    {
        int i = I;
        int r = Rand(0x10);
        if (ADM[i] + r < DS32(0x90770) + 10 || g_AiLevel > 0) g_ObjDir = Heading_TurnToward(g_ObjDir, g_AimDirX, g_AimDirY);
    }
    /* 17 jink */
    if (Rand(10) + DS32(0x90770) < 4 && g_ObjY < 500 && g_AiLevel == 0 && abs(g_ObjX - g_TargetX) < 600)
        g_ObjDir = (Rand(2) + g_ObjDir - 1) & 0xf;
    g_LockTarget = -1;
    /* 18 wake-up column p12 */
    if (Div16(g_PXe) < (s32)g_MP_EnemyWakeCol || abs(g_ObjX - g_PXe) < 2000) g_MP_EnemyWakeCol = 0;
    if (g_MP_EnemyWakeCol != 0) return;
    /* reticle §2.8 */
    g_AimDirX = Sign((g_ObjX - g_PXe) / 8);
    g_AimDirY = Sign((g_ObjY - g_PYg) / 8);
    Lock_Facing(0);
    Lock_Draw(abs(g_ObjX - g_PXe) + abs(g_ObjY - g_PYg), g_ObjX - g_CamX - g_PlayerScrX, g_ObjY - g_CamY - g_PlayerScrY, I,
              0x134, 8, 1);
    /* §2.6 bombing */
    if (g_BaseStartX < g_ObjX && g_ObjX < g_BaseEndX && g_EnemyBombRef * 0x10 - 0x30 <= g_ObjX && ABOMB[I] > 0
        && g_ProjCount < 0x15 && ABOMB[I] < 999 && ADM[I] < 2) {
        Enemy_DropBomb();
        ABOMB[I]--;
    }
    /* missiles at the B52 */
    if (g_B52Active != 0 && abs(g_B52X - g_ObjX) < 600 && abs(g_B52Y - g_ObjY) < 600 && AMIS[I] > 0 && ADM[I] < 2
        && Rand(6) > 4 - g_EnemySkill) {
        AMIS[I]--;
        missile_add(g_ObjX, g_ObjY, g_ObjDir / 2, 0x1e);
    }
    /* §2.7 guns and missiles at the player */
    if (-g_AimDirX == Sign(T_Dir16Xb[g_ObjDir]) && -g_AimDirY == Sign(T_Dir16Yb[g_ObjDir])) {
        g_EnemyDist = abs(g_ObjX - g_PXe) + abs(g_ObjY - g_PYg);
        if (g_EnemyDist < 0x140 && Rand(4) < g_EnemySkill + 1 && ADM[I] < 2) {
            Sfx_RandomAmbient();
            /* tracer: arguments of the empty Tracer_Stub; the divisions fault when the aircraft is level or
             * vertically aligned with the player (Q4): the port stops there as the original does (idiv_js). */
            (void)idiv_js((g_PYg - g_ObjY) << 8, abs(g_PYg - g_ObjY), "EnemyAir_Update (tracer, Q4)");
            (void)idiv_js((g_PXe - g_ObjX) << 8, abs(g_PXe - g_ObjX), "EnemyAir_Update (tracer, Q4)");
            Rand(1);
            Rand(1);
            if (Rand(10) < g_EnemySkill + 1) {
                Explosion_Damage(g_PXe, g_PYg, 0, 0, 10, 10);
                Sfx_Play(Rand(1) * 0x17 + 4, 7000, 0x3f, PX);
                g_DamageHits = g_EnemyGunDamage + 1;
                Player_DamageSystems();
            }
        }
        if (g_EnemyDist < 0x280 && g_EnemyDist >= 0x140 && g_EnemyMslCount < 4 && AMIS[I] > 0 && ADM[I] < 2
            && Rand(6) > 4 - g_EnemySkill) {
            Sfx_RandomAmbient();
            AMIS[I]--;
            missile_add(g_ObjX, g_ObjY, g_ObjDir / 2, 0x1e);
            Flare_Release();
        }
    }
#undef I
}

/* 0x36ae3 EnemyAir_Update (§2.3..2.7) */
void EnemyAir_Update(void)
{
    air_move_all();
    int k = min_i(g_FrameParity, g_EnemyAirCount - 1);
    if (k < 0) k = 0;
    g_LoopI = k;
    if (g_EnemyAirX[k] < -998) {
        air_remove(k);
    } else {
        g_PXe = PX;
        g_PYe = PY;
        g_TargetY = min_i(PY, 800);
        g_TargetX = g_PXe;
        if (ABOMB[k] > 0) { g_TargetX = (g_BaseEndX - g_BaseStartX) / 2 + g_BaseStartX; g_TargetY = 0; }
        if (g_B52Active != 0) { g_TargetX = g_B52X; g_TargetY = g_B52Y; }
        if (g_HercActive != 0) { g_TargetX = g_HercX; g_TargetY = g_HercY; }
        g_PYg = g_PYe;
        if (g_ViewTarget == 200) {
            g_TargetX = g_ViewX; g_TargetY = g_ViewY;
            g_PXe = g_ViewX; g_PYg = g_ViewY;
        }
        g_ObjX = g_EnemyAirX[k];
        g_ObjY = AY[k];
        g_ObjDir = ADIR[k];
        g_ObjSpd = ASPD[k];
        g_ObjThr = ATHR[k];
        g_ObjHp = AHP[k];
        DS32(0x90048) = g_PYe;
        if (ABOMB[k] > 0 && g_BaseStartX < g_ObjX && g_ObjX < g_BaseEndX) {
            g_TargetY = g_EnemyBombRef << 4;
            DS32(0x90048) = g_TargetY;
        }
        DS32(0x90688) = Map_GetTileAttr(Div16(g_ObjX) % g_MapWidth, Clamp(Div16((ASTATE[k] == 0x81) * 0x14 + g_ObjY), 0, 0x3f), 0);
        if ((DS32(0x90688) > 0x7e && DS32(0x90688) != 0x81) || ADM[g_LoopI] > 5 || g_ObjY > 999) {
            if (ASTATE[g_LoopI] != 0) g_EnemyBaseX = 0;                  /* crashed while parked: base closed */
            Explosion_Damage(g_ObjX, g_ObjY, 0, T_Dir16Y[g_ObjDir], (ABOMB[g_LoopI] == 999) * 1000 + 1000, 2000);
            ADM[g_LoopI]++;
            if (ADM[g_LoopI] > 5 || Div16(g_ObjY) > 0x3e) {             /* destroyed */
                Explosion_Damage(g_ObjX, g_ObjY, 0, 0x10, 1000, 2000);
                if (AHP[g_LoopI] < 4) {                                  /* always true */
                    g_AirKilledTot++;
                    g_Kills++;
                    DS32(0x900DC) = 1;
                    g_MP_AirKills = (u16)((int)g_MP_AirKills - 1 < 0 ? 0 : g_MP_AirKills - 1);
                    g_Score[g_AeroPlayer] += (g_PlaneClass + 1) * 1000;
                    if ((g_BonusType == 0 && Rand(2) == 1) || g_ExtraAircraftScore < g_Score[g_AeroPlayer]) Bonus_Spawn();
                }
                if (g_MP_EnemyAir == 0 && g_MP_AirKills == 0) {
                    g_SpawnCount--;
                    g_ObjX = -999;
                    ABURN[g_LoopI] = 0;
                } else {                                                 /* respawn (Q3: not abomb/aeject/astate) */
                    g_MP_EnemyAir = (u16)((int)g_MP_EnemyAir - 1 < 0 ? 0 : g_MP_EnemyAir - 1);
                    g_ObjSpd = 6;
                    g_ObjX = spawn_x_clamped(200, (g_MapWidth - 1) * 0x10 - 200);
                    g_ObjY = -800;
                    g_ObjDir = 0;
                    ADM[g_LoopI] = 0;
                    g_ObjThr = 4;
                    AMIS[g_LoopI] = 4;
                    ABURN[g_LoopI] = 0;
                }
            }
        }
        if (-999 < g_ObjX) air_ai();
        g_EnemyAirX[g_LoopI] = g_ObjX;
        AY[g_LoopI] = g_ObjY;
        ASPD[g_LoopI] = g_ObjSpd;
        ATHR[g_LoopI] = g_ObjThr;
        AHP[g_LoopI] = g_ObjHp;
        ADIR[g_LoopI] = g_ObjDir;
    }
    if (g_LockTarget > -1) DS32(0x8FFF8) += 4;                           /* lock tone */
}

/* ====================================================================== enemy missiles (§3) */

/* 0x19fda EnemyMissiles_Update (was Helis_Update) */
void EnemyMissiles_Update(void)
{
    g_ViewCamX = g_CamX;
    g_ViewCamY = g_CamY;
    g_Loop724 = g_EnemyMslCount;
    if (g_ViewX > 0 || g_ViewY > 0) { g_ViewCamX = g_ViewX; g_ViewCamY = g_ViewY; }
    while (--g_Loop724 > -1) {
        int j = g_Loop724;
        g_CurX = MX[j];
        g_CurY = MY[j];
        g_MslDir = MDIR[j];
        g_MslLife = MLIFE[j];
        if (IsOnScreen(g_CamX, g_CamY, g_CurX, g_CurY)) Sprite_Queue(g_CurX - g_CamX, g_CurY - g_CamY, g_MslDir + 0x6a);
        if (BoxOverlap(g_CurX, g_CurY, g_HercX, g_HercY, 0x40, 0x20)) {
            Explosion_Damage(g_HercX, g_HercY, 0, 0, 10, 10);
            int r = Rand(4);
            DS32(0x90510) = DS32(0x90510) - r - 3;
            Hud_PushMessage(DSTR(0x81059));                              /* "Hercules Hit!" */
            if (DS32(0x90510) < 1) DS32(0x9050C) = 1;
            g_CurX = -999;
        }
        if (BoxOverlap(g_CurX, g_CurY, g_B52X, g_B52Y, 0x40, 0x20)) {
            Explosion_Damage(g_B52X, g_B52Y, 0, 0, 10, 10);
            int r = Rand(4);
            DS32(0x90978) = DS32(0x90978) - r - 3;
            Hud_PushMessage(DSTR(0x81068));                              /* "B52 Hit!" */
            if (DS32(0x90978) < 1) DS32(0x90970) = 1;
            g_CurX = -999;
        }
        if (BoxOverlap(g_CurX, g_CurY, g_ViewCamX + g_PlayerScrX, g_ViewCamY + g_PlayerScrY, 0x20, 0x20)) {
            Explosion_Damage(PX, PY, 0, 0, 10, 10);
            g_DamageHits = 4;
            Player_DamageSystems();
            g_CurX = -999;
        }
        if (g_CurX > -1 && g_EjectState == 0) {
            if (g_B52Active == 0 || abs(g_B52X - g_CurX) < 0x259 || abs(g_B52Y - g_CurY) > 599) {   /* Q9 */
                if (g_FlareCount == 0 || Rand(1) != 0) {
                    if (Rand(3) != 0 && g_MslLife < 0x14) {
                        g_AimDirX = Sign(g_ViewCamX + g_PlayerScrX - g_CurX);
                        g_AimDirY = Sign(g_ViewCamY + g_PlayerScrY - g_CurY);
                        if (g_MslLife < 0) { g_AimDirX = 0; g_AimDirY = 1; }
                    }
                } else {                                                 /* decoyed by the first flare */
                    g_AimDirX = Sign(g_FlareX[0] - g_CurX);
                    g_AimDirY = Sign(g_FlareY[0] - g_CurY);
                }
            } else {
                g_AimDirX = Sign(g_B52X - g_CurX);
                g_AimDirY = Sign(g_B52Y - g_CurY);
                if (g_MslLife < 0) { g_AimDirX = 0; g_AimDirY = 1; }
            }
            if (g_HercActive != 0 && abs(g_HercX - g_CurX) < 600 && abs(g_HercY - g_CurY) < 600) {
                g_AimDirX = Sign(g_HercX - g_CurX);
                g_AimDirY = Sign(g_HercY - g_CurY);
                if (g_MslLife < 0) { g_AimDirX = 0; g_AimDirY = 1; }
            }
            int r = Clamp(Div16(g_CurY + 0x10), 0, 0x3f);
            int c = Clamp(Div16(g_CurX + 0x10), 0, g_MapWidth - 1);
            DS32(0x908B0) = Map_GetTileAttr(c, r, 0);
            if (DS32(0x908B0) > 0x7e || g_MslLife < 2) {
                Explosion_Damage(g_CurX + 0x10, g_CurY + 0x10, 0, 0, 0x32, 0x32);
                g_CurX = -999;
            }
            if (g_MslLife < 0x14) {
                g_AimFrame = HEADING(g_AimDirX, g_AimDirY);
                g_MslDir = (g_MslDir + 8 + Sign(g_AimFrame - g_MslDir)) % 8;
            }
            g_CurVX = Sign(T_Dir16X[g_MslDir * 2]) * 0x18;
            g_CurVY = Sign(T_Dir16Y[g_MslDir * 2]) * 0x18;
        }
        g_CurX += g_CurVX;                                               /* stale when the AI was skipped (Q10) */
        g_CurY += g_CurVY;
        g_MslLife--;
        MX[j] = g_CurX;
        MY[j] = g_CurY;
        MDIR[j] = g_MslDir;
        MLIFE[j] = g_MslLife;
        if (MX[j] < 0) {
            int n = g_EnemyMslCount;
            swap_last(MX, j, n); swap_last(MY, j, n); swap_last(MLIFE, j, n); swap_last(MDIR, j, n);
            g_EnemyMslCount--;
        }
    }
}

/* ====================================================================== ground gunships (§4) */

/* 0x35e20 EnemyGround_Update */
void EnemyGround_Update(void)
{
    g_TargetX = PX;
    int ty = PY;
    if (g_ConvoyCount > 0 && g_EscortMode == 1) { g_TargetX = CVX[0]; ty = CVY[0]; }
    g_TargetY = ty - 0x40;
    g_LoopI = g_EnemyGroundCount;
    while (--g_LoopI > -1) {
        int i = g_LoopI;
        if (IsOnScreen(g_CamX, g_CamY, GX[i], GY[i])) {
            Sprite_Queue(GX[i] - g_CamX, GY[i] - g_CamY, GDIR[i] * 2 + 0x1a4 + g_FrameParity);
            if (GDM[i] > 0) {
                int life = Rand(4) + 0xc;
                Particle_Spawn(GX[i] << 8, GY[i] << 8, 0, 0, 0, life, 0x18);   /* anim 24 is empty (Q12) */
            }
        }
        if (GDM[i] < 5) {
            GX[i] = Clamp(GVY[i] + GDIR[i] * 2 - 6, 0x280, g_BaseStartX - 0x140);   /* sic: vy instead of x (Q1) */
            int h = (int)(Byte_Get(g_MapVal, Div16(GX[i]) + 0x400) & 0xff) * 0x10;
            DS32(0x9028C) = h - 0x10;
            GY[i] = Clamp(GY[i] + GVY[i], h - 0x50 - g_LoopI * 0x20, DS32(0x9028C));
            if (g_TargetY < 1) {
                if (Rand(10) == 1) GVY[g_LoopI] = Rand(2) * 4 - 4;
            } else {
                GVY[i] = Clamp(GVY[i] + Sign(g_TargetY - GY[i] - g_LoopI * 0x20), -4, 4);
            }
            if (Rand(1) != 0) GDIR[i] = Clamp(GDIR[i] + Sign(g_TargetX - GX[i] - g_LoopI * 0x20), 0, 6);
            g_Spr98c = (GDIR[i] < 3) - (GDIR[i] > 3);                   /* facing */
            g_AimDirX = Sign(GX[i] - g_CamX - g_PlayerScrX);
            g_AimDirY = Sign(GY[i] - g_CamY - g_PlayerScrY);
            if (-g_Spr98c == g_AimDirX && abs(GX[i] - g_CamX - g_PlayerScrX) < 0x80 && GY[i] < PY && PY < GY[i] + 0x50
                && GSHELL[i] > 0 && g_ShellCount < 10 && Rand(5) == 1) {
                int n = g_ShellCount;
                SX[n] = g_Spr98c * 8 + GX[i];
                SY[n] = GY[i];
                SLIFE[n] = Rand(10) + 10;
                SVX[n] = (PX - GX[i] - 8) / 6;
                SVY[n] = (PY - GY[i]) / 6;
                SSIZE[n] = 1;
                g_ShellCount++;
                Sfx_Play(1, 14000, 0xc, GX[i]);
                DS32(0x90074) = 0;
                Sfx_Play(3, 0x1194, 0x18, GX[i]);
                DS32(0x90080) = 0;
                GSHELL[i]--;
            }
            if (-g_Spr98c == g_AimDirX && abs(GX[i] - g_CamX - g_PlayerScrX) < 0x140 && abs(GY[i] - g_CamY - g_PlayerScrY) < 0x80
                && g_EnemyMslCount < 10 && GMIS[i] > 0 && Rand(0x32) < g_Mission / 3 + 1) {
                GMIS[i]--;
                missile_add(GX[i], GY[i], (3 < g_Spr98c) * -4, 3);       /* Q8: up to 10 entries, Q11: life 3 */
            }
            g_EnemyDist = abs(GY[i] - g_CamY - g_PlayerScrY) * 2;        /* sic (dy twice) */
            Lock_Facing(1);
            {
                int ldx = GX[i] - g_CamX - g_PlayerScrX, ldy = GY[i] - g_CamY - g_PlayerScrY;
                Lock_Draw(abs(ldx) + abs(ldy), ldx, ldy, g_LoopI + 3, 0x138, g_HudRows * -0x23 + 8, 1);
            }
        } else {
            Explosion_Damage(GX[i], GY[i], 0, 0, 2000, 2000);
            int n = g_EnemyGroundCount;
            swap_last(GX, i, n); swap_last(GY, i, n); swap_last(GDIR, i, n); swap_last(GVY, i, n);
            swap_last(GHP, i, n); swap_last(GDM, i, n); swap_last(GMIS, i, n); swap_last(G8E8, i, n);
            swap_last(GSHELL, i, n);
            g_EnemyGroundCount--;
        }
    }
}

/* ====================================================================== enemy shells (§5) */

/* 0x19cc8 EnemyShells_Update (was Targets_Update) */
void EnemyShells_Update(void)
{
    g_Loop724 = g_ShellCount - 1;
    g_ViewCamX = g_CamX;
    g_ViewCamY = g_CamY;
    if (g_ViewX != 0 || g_ViewY != 0) { g_ViewCamX = g_ViewX; g_ViewCamY = g_ViewY; }
    do {
        int j = g_Loop724;
        g_CurX = SX[j] - g_CamX;
        g_CurY = SY[j] - g_CamY;
        /* Q13: absolute VRAM (the HUD panel page); the colour argument is not pushed in the original (a byte of
         * the caller's saved EBP). PORT: a fixed colour. */
        if (g_CurX > -1 && g_CurX < 0x140 && (u32)g_CurY < 0x80000000u && g_CurY < 0xb0)
            Video_PutPixel((u32)g_CurX, g_CurY, 0xff);
        /* Q14: x compared with itself, y with camera X + screen y */
        if (BoxOverlap(g_ViewCamX + g_CurX, g_ViewCamY + g_CurY, g_ViewCamX + g_CurX, g_ViewCamX + g_PlayerScrY,
                       SSIZE[j] * 0x18 + 8, SSIZE[j] * 0x18 + 8)) {
            Explosion_Damage(g_ViewCamX + g_PlayerScrX, g_ViewCamY + g_PlayerScrY, 0, 0, 5, 5);
            g_DamageHits = SSIZE[j] + 1;
            Player_DamageSystems();
            SLIFE[j] = -999;
        }
        SX[j] += SVX[j];
        SY[j] += SVY[j];
        SLIFE[j]--;
        if (g_FrameParity == 0) SVY[j]++;
        g_Spr98c = g_ShellCount - 1;
        if (SLIFE[j] < 1) {                                              /* Q15: ssize not swapped */
            SwapInt(&SX[j], &SX[g_Spr98c]);
            SwapInt(&SY[j], &SY[g_Spr98c]);
            SwapInt(&SVX[j], &SVX[g_Spr98c]);
            SwapInt(&SVY[j], &SVY[g_Spr98c]);
            SwapInt(&SLIFE[j], &SLIFE[g_Spr98c]);
            g_ShellCount--;
        }
        g_Loop724--;
    } while (g_Loop724 > -1);
}

/* ====================================================================== ejected enemy pilots (§6) */

/* 0x35a33 EnemyPilots_Update (was Mines_Update) */
void EnemyPilots_Update(void)
{
    DS32(0x90760) = Wrap(DS32(0x90760) + 1, 0, 7);
    DS32(0x9075C) += DS32(0x90744);
    g_LoopI = g_PilotCount;
    if (DS32(0x9075C) == 0 || DS32(0x9075C) == 4) DS32(0x90744) = -DS32(0x90744);
    while (--g_LoopI > -1) {
        int i = g_LoopI;
        if (IsOnScreen(g_CamX, g_CamY, EPX[i], EPY[i])) {
            DS32(0x90960) = DS32A(0x90CF8)[EPST[i]];
            if (DS32(0x90960) == 0x35) DS32(0x90960) = DS32A(0x90CE4)[DS32(0x9075C)] + 0x35;
            if (EPST[i] < 5) DS32(0x90960) = DS32(0x90760) / 2 + 0x18e;
            Sprite_Queue(EPX[i] - g_CamX, EPY[i] - g_CamY, DS32(0x90960));
            if (DS32(0x9019C) == 0 && Rand(0x32) == 1) {                 /* scream, once per mission */
                Sfx_Play(0x16, 4000, 0x3f, PX);
                DS32(0x9019C) = 1;
                g_SfxBusyTimer = 8;
            }
        }
        EPY[i] += EPVY[i];
        EPVY[i] = min_i(EPVY[i] + g_FrameParity, (4 < EPST[i]) * 0xc + 0x10);
        if (EPST[i] < 7) {
            EPST[i]++;
            if (EPY[i] < 700 && EPST[i] == 5) EPST[i] = 1;
        }
        if (EPY[i] > 0x3e0) {
            EPY[i] = 0x3e0;
            int s = min_i(EPST[i] + 1, 0x1c);
            EPST[i] = s < 0x18 ? 0x18 : s;
            if (EPST[i] == 0x1c) {                                       /* Q15: epvy not swapped */
                int n = g_PilotCount;
                swap_last(EPX, i, n); swap_last(EPY, i, n); swap_last(EPST, i, n);
                g_PilotCount--;
            }
        }
    }
}

/* ====================================================================== convoy (§7) */

/* 0x3ea8f Convoy_FireShell (g_LoopI = vehicle); the size is not written (stale slot) */
static void Convoy_FireShell(void)
{
    int i = g_LoopI;
    if (CVY[i] - 0x78 < PY && Rand(0x1e) < g_Mission && Rand(6) == 1 && g_ShellCount < 10) {
        int n = g_ShellCount;
        SX[n] = CVX[i];
        SY[n] = CVY[i];
        SLIFE[n] = Rand(10) + 10;
        SVX[n] = Sign(PX - CVX[i]) << 4;
        int v = (PY - CVY[i]) / 4;
        SVY[n] = (v < 0 ? v : -1) - Rand(4);
        g_ShellCount++;
    }
}

/* 0x17684 Convoy_LaunchMissile (g_LoopI = vehicle): a player-array projectile (Projectile_Climb) */
static void Convoy_LaunchMissile(void)
{
    int n = g_ProjCount;
    P_KIND[n] = 2;
    P_ARM[n] = 0x1e;
    P_MOTOR[n] = 1;
    P_FLIGHT[n] = 9;
    P_DETONATE[n] = 0;
    P_FLAGS[n] = 0;
    P_BLASTA[n] = 2000;
    P_BLASTB[n] = 2000;
    P_TX[n] = 0;
    P_TY[n] = 0;
    g_ProjX[n] = CVX[g_LoopI];
    g_ProjY[n] = CVY[g_LoopI] - 8;
    P_FRAME[n] = 2;
    P_LIFE[n] = 1000;
    g_ProjCount = min_i(g_ProjCount + 1, 0x15);
}

/* 0x3b743 Convoy_DeathStub: an empty counting loop (its arguments are ignored) */
static void Convoy_DeathStub(void)
{
    DS32(0x8FEA4) = 0;
    do DS32(0x8FEA4)++; while (DS32(0x8FEA4) < 5);
}

/* 0x3d105 Convoy_Update (§7.3) */
void Convoy_Update(void)
{
    int kind = (int)g_MP_ConvoyKind >> 8;
    DS32(0x9042C) = g_TargetMarkX;
    if (DS32(0x90568) == 0 || DS32(0x9056C) > 2 || DS32(0x9056C) < 0) { DS32(0x90568) = 1; DS32(0x9056C) = 0; }
    if (kind == 2) {
        DS32(0x9056C) = 0;
    } else {
        DS32(0x9056C) += DS32(0x90568);
        if (DS32(0x9056C) == 0 || DS32(0x9056C) == 2) DS32(0x90568) = -DS32(0x90568);
    }
    DS32(0x905A8) = 1 - DS32(0x905A8);
    g_LoopI = g_ConvoyCount;
    while (--g_LoopI > -1) {
        g_ObjX = CVX[g_LoopI];
        g_ObjY = CVY[g_LoopI];
        DS32(0x909BC) = CVVX[g_LoopI];
        DS32(0x909F4) = CVSLOT[g_LoopI];
        if (CVST[g_LoopI] == 0) { g_TargetMarkX = g_ObjX + DS32(0x909BC); g_TargetMarkY = g_ObjY; }
        g_ObjDir = DS32(0x909BC);
        if (IsOnScreen(g_CamX, g_CamY, g_ObjX, g_ObjY)) {
            int flip = (g_ObjDir < 0 && g_ConvoyTrain == 0 && kind != 2);
            g_Spr98c = g_ConvoySprites[DS32(0x909F4) + DS32(0x9056C) + flip * 3];
            if (g_EscortMode != 0) {
                int y = g_ObjY - g_CamY;
                Sprite_Queue(g_ObjX - g_CamX, y - (s32)Sprite_GetHeight(g_Spr98c), g_FrameParity + 0x1ec);
            }
            {
                int y = g_ObjY - g_CamY;
                int h = (s32)Sprite_GetHeight(g_Spr98c);
                int sy = (u16)Sprite_GetY(g_Spr98c);
                Sprite_Queue(g_ObjX - g_CamX, sy + (y - h), g_Spr98c);
            }
            if (g_MP_ConvoyKind != 2) {
                int row = g_MP_ConvoyRow;
                int yy = g_ObjY < row ? row : g_ObjY;
                if (Video_ReadPixel(g_ObjX - g_CamX + g_ScrollFineX, yy - g_CamY + 1 + g_ScrollFineY, g_BackPage) == 0)
                    g_ObjY = min_i(g_ObjY + 1, row + 4);                 /* road below gone: sink */
                else if (Video_ReadPixel(g_ObjX - g_CamX + g_ScrollFineX, g_ObjY - g_CamY + g_ScrollFineY, g_BackPage) > 0)
                    g_ObjY = max_i(g_ObjY - 1, row);                     /* inside ground: climb */
            }
            if (CVST[g_LoopI] > 0) {
                DS32(0x909BC) = 0;
                int r = Rand(1);
                Sprite_Queue(g_ObjX - g_CamX, CVSMK[g_LoopI] / 2 + (g_ObjY - g_CamY), r + 0x9e);
            }
        }
        if ((g_LoopI & 1) == DS32(0x905A8)) {                            /* half rate: logic + write-back */
            if (CVST[g_LoopI] > 0) {
                if (Rand(0x14) > 0x12) CVST[g_LoopI]++;
                if (CVST[g_LoopI] > 3) {                                 /* destroyed */
                    if (1000 < g_MP_PickupSprite && g_MP_ConvoyKills == 0) {
                        g_MP_PickupSprite = (u16)(g_MP_PickupSprite % 500);
                        g_PickupX = g_ObjX;
                        g_MP_PickupCol = (u16)Div16(g_ObjX);
                        g_PickupY = g_ObjY;
                    }
                    Explosion_Damage(g_ObjX + 0x10, g_ObjY + 0x10, 0, 0, 1000, 100);
                    Convoy_DeathStub();
                    if (g_EscortMode == 0) {
                        g_MP_ConvoyKills--;
                        if ((s16)g_MP_ConvoyKills < 1) { g_MP_ConvoyKills = 0; g_MP_ConvoyCount = 0; g_MP_ConvoyCol = 0; }
                        g_Score[g_AeroPlayer] += 0xfa;
                    } else {
                        g_Score[g_AeroPlayer] -= 1000;
                        Hud_PushMessage(DSTR(0x81408));                  /* "Friendly Vehicle Destroyed!" */
                        if (g_ConvoyCount - 1 < g_EscortNeeded) {
                            g_EscortNeeded = 0;
                            Hud_PushMessage(DSTR(0x81425));              /* "Mission Failed!" */
                            g_MissionBonus = 0;
                        }
                    }
                    g_ObjX = -999;
                }
            }
            if (g_ObjX > 0) {
                g_ObjX += DS32(0x909BC);
                g_ObjX = (g_MapWidth * 0x10 + g_ObjX) % (g_MapWidth << 4);
                int r = min_i(Div16(g_ObjY) + (int)g_MP_ConvoyFlag, 0x3f);
                DS32(0x90578) = Map_GetTileAttr(Div16(g_ObjX + Sign(g_ObjDir) * 0x10) + 1, r, 0);
                DS32(0x9058C) = Map_GetTileAttr(Div16(g_ObjX) + 1, Div16(g_ObjY) + 1, 0);
                if (g_EscortMode == 1)                                   /* Q19: last gun-ray hit x */
                    g_ObjDir = abs(g_ObjDir) * Sign((int)g_MP_TargetX0 - Div16(DS32(0x90538)));
                if (DS32(0x90578) == 0x80 || DS32(0x90578) == 0x7f || (DS32(0x9058C) < 0x82 && g_MP_ConvoyFlag == 1)) {
                    g_ObjDir = -g_ObjDir;
                    if (kind == 1 || g_ConvoyTrain == 1)
                        for (DS32(0x90248) = 0; DS32(0x90248) < 8; DS32(0x90248)++) CVVX[DS32(0x90248)] = g_ObjDir;
                }
                if (g_ShellCount < 10 && CVHP[g_LoopI] > 0 && (g_GameMode & 1) == 0 && g_EscortMode == 0) Convoy_FireShell();
                if (g_ProjCount < 10 && CVHP[g_LoopI] > 2 && Rand(0x32) == 1 && g_BaseStartX - 0x280 < g_ObjX
                    && g_ObjX < g_BaseEndX + 0x280 && g_EscortMode == 0)
                    Convoy_LaunchMissile();
                if ((int)g_MP_TargetX0 - 2 <= Div16(g_ObjX) && Div16(g_ObjX) <= (int)g_MP_TargetX0 + 2) {   /* arrived */
                    g_EscortNeeded--;
                    Hud_PushMessage(DSTR(0x81436));                      /* "Vehicle arrived" */
                    g_ObjX = -999;
                    g_Score[g_AeroPlayer] += 2000;
                }
                if (DS32(0x9058C) < 0x7f && kind != 2) {                 /* falling (reuses cvst) */
                    int i = g_LoopI;
                    g_ObjY += CVST[i];
                    CVST[g_LoopI] = Rand(1) + CVST[i];
                }
                g_ObjY = min_i(g_ObjY, 0x3f0);
            }
            CVX[g_LoopI] = g_ObjX;
            CVY[g_LoopI] = g_ObjY;
            CVVX[g_LoopI] = g_ObjDir;
            if (CVX[g_LoopI] == -999) {
                int n = g_ConvoyCount, i = g_LoopI;
                if (g_ConvoyTrain == 1) g_ConvoyTrain = 2;
                if (i == g_AgentVehicle) g_AgentVehicle = n - 1;
                else if (n - 1 == g_AgentVehicle) g_AgentVehicle = i;
                SwapInt(&CVX[n - 1], &CVX[i]);
                SwapInt(&CVY[n - 1], &CVY[i]);
                SwapInt(&CVSLOT[n - 1], &CVSLOT[i]);
                SwapInt(&CVVX[n - 1], &CVVX[i]);
                SwapInt(&CVST[n - 1], &CVST[i]);
                SwapInt(&CVSMK[n - 1], &CVSMK[i]);
                SwapInt(&CVHP[n - 1], &CVHP[i]);
                g_ConvoyCount--;
            }
        }
    }
    if ((kind == 1 || g_ConvoyTrain > 0) && g_ConvoyCount > 1)
        for (g_LoopI = 1; g_LoopI < g_ConvoyCount; g_LoopI++) {
            CVX[g_LoopI] = CVX[0] + (int)g_MP_ConvoySpacing * g_LoopI;
            if (g_ConvoyTrain == 2) CVST[g_LoopI] = 5;
        }
    if (g_ConvoyTrain == 2) CVST[0] = 5;
}

/* ====================================================================== MP2 emplacements (§8) */

static int aim_frame(void)
{
    int dv = Clamp(Div16(DS32(0x905FC) + 0xf - g_CamY - g_PlayerScrY), 1, 100);
    return Clamp((PX - DS32(0x905E4) - 8) / dv + 2, 0, 4);
}

/* 0x3dea2 Gun_Draw (class 0x85) */
void Gun_Draw(void)
{
    DS32(0x905E4) = DS32(0x90268) << 4;
    DS32(0x905FC) = DS32(0x9026C) << 4;
    if (IsOnScreen(g_CamX, g_CamY, DS32(0x905E4), DS32(0x905FC))) {
        DS32(0x902A0) = aim_frame();
        Sprite_Queue(DS32(0x905E4) - g_CamX - 0x10, DS32(0x905FC) - g_CamY, DS32(0x90290) * 5 + DS32(0x902A0) + 0x1b2);
    }
    DS32(0x90290) = 0;
}

/* 0x3dfaa Flak_Draw (class 0x83) */
void Flak_Draw(void)
{
    DS32(0x905E4) = DS32(0x90668) << 4;
    DS32(0x905FC) = DS32(0x90614) << 4;
    if (IsOnScreen(g_CamX, g_CamY, DS32(0x905E4), DS32(0x905FC))) {
        DS32(0x906AC) = aim_frame();
        Sprite_Queue(DS32(0x905E4) - g_CamX - 0x10, DS32(0x905FC) - g_CamY, DS32(0x9064C) * 5 + DS32(0x906AC) + 0x1b2);
    }
    DS32(0x9064C) = 0;
}

/* 0x3e0b2 SAM_Draw (class 0x84) */
void SAM_Draw(void)
{
    DS32(0x905E4) = DS32(0x9010C) << 4;
    DS32(0x905FC) = DS32(0x90110) << 4;
    if (IsOnScreen(g_CamX, g_CamY, DS32(0x905E4), DS32(0x905FC))) {
        DS32(0x90108) = aim_frame();
        Sprite_Queue(DS32(0x905E4) - g_CamX - 0x10, DS32(0x905FC) - g_CamY, DS32(0x90108) + 0x1bc);
    }
}

/* 0x3e8b3 Gun_Fire (class 0x85): the height test reads the flak row 0x90614 (enemies.md Correction 1) */
void Gun_Fire(void)
{
    if (DS32(0x90614) * 0x10 - 0x78 < PY && Rand(1) != 0 && g_ShellCount < 10) {
        int n = g_ShellCount;
        SX[n] = g_TargetMarkX;
        SY[n] = g_TargetMarkY - 0x10;
        SLIFE[n] = Rand(10) + 10;
        SVX[n] = Sign(PX - g_TargetMarkX) << 5;
        int v = (PY - g_TargetMarkY) / 6;
        SVY[n] = (v < 0 ? v : -1) - Rand(4);
        SSIZE[n] = 1;
        g_ShellCount++;
        DS32(0x90074) = 0;
        Sfx_Play(1, 14000, 0xc, g_TargetMarkX);
        DS32(0x90080) = 0;
        Sfx_Play(3, 0x1194, 0x18, g_TargetMarkX);
    }
}

/* 0x3ec5b Flak_Fire (class 0x83): airbursts near the player */
void Flak_Fire(void)
{
    DS32(0x9062C) = abs(DS32(0x90668) * 0x10 - g_PlayerScrX - g_CamX) + abs(DS32(0x90614) * 0x10 - g_PlayerScrY - g_CamY);
    if (DS32(0x9062C) < 0x280 && Rand(2) == 1) {
        int px = PX;
        int a = Rand(100);
        int b = Rand(100);
        DS32(0x90680) = px + a - b;
        DS32(0x90684) = Rand(0x8c) + g_CamY;
        DS32(0x90694) = Clamp(Div16(DS32(0x90680)), 0, g_MapWidth - 1);
        DS32(0x9069C) = Clamp(Div16(DS32(0x90684)), 0, 0x3c);
        int f = DS32(0x9028C) / 200;
        int r = Rand(2000);
        int id = Rand(1) * 0x17 + 4;
        Sfx_Play(id, r + (6000 - f), 0x3f, DS32(0x90680));
        DS32(0x9064C) = 1;
        g_ViewCamX = g_CamX;
        g_ViewCamY = g_CamY;
        if (g_ViewX != 0 || g_ViewY != 0) { g_ViewCamX = g_ViewX; g_ViewCamY = g_ViewY; }
        if (Map_GetTileAttr(DS32(0x90694), DS32(0x9069C), 0) < 0x7f) {
            if (BoxOverlap(DS32(0x90680), DS32(0x90684), g_ViewCamX + g_PlayerScrX, g_ViewCamY + g_PlayerScrY, 8, 8)) {
                g_DamageHits = 2;
                Player_DamageSystems();
            }
            Explosion_Damage(DS32(0x90680), DS32(0x90684), 0, 0, 10, 10);
            DS32(0x90660) = 10;
        }
    }
}

/* 0x3ef72 SAM_Fire (class 0x84) */
void SAM_Fire(void)
{
    if (Rand(100) > 0x50 && g_EnemyMslCount < 4 && abs(PY - DS32(0x90110) * 0x10) > 0xa0)
        missile_add(DS32(0x9010C) << 4, DS32(0x90110) * 0x10 - 0x40, 2, 0x1e);
}

/* ====================================================================== target-zone vehicles (§9) */

static int tv_x(void)
{
    u32 c = g_MP_TargetX0;
    int a = Rand(0x1e0);
    int b = Rand(0x1e0);
    return (int)(((c % 1000) * 0x10 + (u32)a) - (u32)b);
}

/* 0x4424d TargetVehicle_Spawn (was Enemy_InitPositions) */
void TargetVehicle_Spawn(void)
{
    int hi = g_MapWidth * 0x10 - 0x1e0;
    int m = (hi < tv_x()) ? hi : tv_x();
    int x;
    if (m < 0x1e0) x = 0x1e0;
    else x = (hi < tv_x()) ? hi : tv_x();
    int n = g_TVCount;
    TVX[n] = x - 8;
    TVY[n] = (int)(Byte_Get(g_MapVal, Div16(TVX[n]) + 0x401) & 0xff) << 4;
    TVST[n] = Rand(1);
    if (Map_GetTileAttr(Div16(TVX[n]), Div16(TVY[n]) + 1, 0) != 0x82) g_TVCount++;
}

/* 0x447f3 TV_LaunchSAM */
static void TV_LaunchSAM(void)
{
    int i = g_LoopI;
    if (abs(PX - TVX[i]) + abs(PY - TVY[i]) > 0x5a && g_EnemyMslCount < 4) {
        int n = g_EnemyMslCount;
        MX[n] = TVX[i];
        MY[n] = TVY[i] - 0x20;
        MDIR[n] = Rand(2) + 1;
        MLIFE[n] = 0xf;
        g_EnemyMslCount++;
    }
}

/* 0x448d7 TV_FireShell (size not written) */
static void TV_FireShell(void)
{
    int i = g_LoopI;
    if (g_ShellCount < 10) {
        int n = g_ShellCount;
        SX[n] = TVX[i];
        SY[n] = TVY[i];
        SLIFE[n] = Rand(10) + 10;
        SVX[n] = Sign(PX - TVX[i]) << 4;
        int v = (PY - TVY[i]) / 4;
        SVY[n] = (v < 0 ? v : -1) - Rand(4);
        g_ShellCount++;
    }
}

/* 0x44478 TargetVehicles_Update (was Ships_Update): Q6 (removal decrements 0x900e0), Q7 (count reset < mission 7) */
void TargetVehicles_Update(void)
{
    g_LoopI = g_TVCount;
    for (;;) {
        g_LoopI--;
        if (g_LoopI < 0) {
            if (g_Mission < 7) g_TVCount = 0;
            return;
        }
        int i = g_LoopI;
        if (IsOnScreen(g_CamX, g_CamY, TVX[i], TVY[i])) {
            Sprite_Queue(TVX[i] - g_CamX, TVY[i] - g_CamY, Rand(10) / 10 + 0xd9);
            /* PORT: the page argument is not pushed in the original (a stack word); the back page is used. */
            g_Spr98c = Video_ReadPixel(TVX[i] - DS32(0x904F0) * 0x10 + 0x10, TVY[i] - DS32(0x904F4) * 0x10 + 0xc, g_BackPage);
            g_LoopK = Map_GetTileAttr(Div16(TVX[i]) + 1, Div16(TVY[i]) + 1, 0);
            if (g_Spr98c > 0 && g_LoopK > 0x7e) TVY[i]--;
            if (g_Spr98c == 0 || g_LoopK < 0x7f) TVY[i] = min_i(TVY[i] + 1, 0x3e0);
            if (Rand(1000) > 0x3e3) {
                if (TVST[i] == 0) { TV_LaunchSAM(); TV_FireShell(); }
                else if (TVST[i] == 1) TV_FireShell();
            }
        }
        if (TVST[i] > 1) {
            TVY[i] = (TVST[i] - 8) * 2 + TVY[i];
            int x = TVX[i];
            int a = Rand(2);
            int b = Rand(2);
            TVX[g_LoopI] = x + a - b;
            TVST[i] = min_i(TVST[i] + 1, 0x10);
            if (TVY[i] > 0x3e0) TVX[i] = 0;
        }
        if (TVX[i] < 1) {
            swap_last(TVX, i, g_TVCount);
            swap_last(TVY, i, g_TVCount);
            DS32(0x900E0)--;
        }
    }
}

/* ====================================================================== commandos, building (§10) */

/* 0x14b4a Commandos_Update (was Flak_Update) */
void Commandos_Update(void)
{
    g_LoopI = g_CommandoCount;
    while (--g_LoopI > -1) {
        int i = g_LoopI;
        g_Spr98c = 1;
        g_DamageHits = 0;
        if (IsOnScreen(g_CamX, g_CamY, CMX[i], CMY[i])) {
            if ((CMVX[i] > 0) + 0x1d7 < 0x1de) Sprite_Queue(CMX[i] - g_CamX, CMY[i] - g_CamY, CMANIM[i] + 0x1da);
            else Sprite_Queue(CMX[i] - g_CamX, CMY[i] - g_CamY, 0x1dd);
            CMANIM[i] = Wrap(CMANIM[i] + 1, 0, 2);
            g_Spr98c = Video_ReadPixel(CMX[i] - DS32(0x904F0) * 0x10, CMY[i] - DS32(0x904F4) * 0x10 + 2, g_BackPage);
            g_DamageHits = Video_ReadPixel(CMX[i] - DS32(0x904F0) * 0x10, CMY[i] - DS32(0x904F4) * 0x10, g_BackPage);
        }
        if (Div16(CMY[i] + 0x11) < 0x40) g_LoopK = Map_GetTileAttr(Div16(CMX[i]), Div16(CMY[i] + 0x11), 0);
        else g_LoopK = Map_GetTileAttr(Div16(CMX[i]), 0x3f, 0);
        g_Scratch690 = 1;
        if (g_Spr98c > 0 && g_LoopK > 0x7e && g_DamageHits > 0) CMY[i]--;
        if (g_Spr98c > 0 && g_LoopK == 0x82) {
            if (CMVX[i] < 1) CMVX[i] = -CMVX[i];
            else CMX[i] = -999;
        }
        if (g_Spr98c == 0 || g_LoopK < 0x7f) {
            CMY[i] = min_i(CMY[i] + 1, 0x3ef);
            CMFALL[i]++;
            if (CMFALL[i] > 2) CMANIM[i] = 99;
        } else {
            CMFALL[i] = 0;
            int r = min_i(Div16(CMY[i]) + 1, 0x3f);
            if ((Map_GetTileAttr(Div16(CMX[i]), r, 3) & 0x20) == 0) {
                CMX[i] += CMVX[i] * g_Scratch690;
            } else {
                Explosion_Damage(CMX[i], CMY[i] + 0x10, 0, 0, 2000, 2000);
                CMX[i] = -999;
            }
        }
        if (g_TargetMarkX - 0xc < CMX[i] && CMX[i] < g_TargetMarkX + 0xc) {
            Explosion_Damage(CMX[i], CMY[i] + 0x10, 0, 0, 2000, 2000);
            CMX[i] = -999;
        }
        if (CMX[i] < 1) {                                                /* Q15: anim / fall not swapped */
            int n = g_CommandoCount;
            swap_last(CMX, i, n); swap_last(CMY, i, n); swap_last(CMVX, i, n);
            g_CommandoCount--;
        }
    }
}

/* 0x149ae Building_Update: emplacement under construction */
void Building_Update(void)
{
    if (IsOnScreen(g_CamX, g_CamY, g_LauncherCol << 4, DS32(0x902C4) << 4))
        Sprite_Queue(g_LauncherCol * 0x10 - g_CamX, DS32(0x902C4) * 0x10 - g_CamY + 0xf, 0x1e7);
    if (--DS32(0x902DC) == 0) {
        DS32(0x90658) = 0;
        for (g_LoopI = 0; g_LoopI < 0x100; g_LoopI++)
            if ((Byte_Get(g_MapVal, g_LoopI) & 0xff) == (u32)DS32(0x902BC) + 0x83u) { DS32(0x90658) = g_LoopI; g_LoopI = 999; }
        Map_SetTile(g_LauncherCol, DS32(0x902C4), (u8)DS32(0x90658));
        g_LauncherCol = 0;
    }
}

/* ====================================================================== airbase (§11) */

#define GROUND_Y (0x3df - g_BaseYOff)

/* 0x2c8a2 FireEngine_Spray */
static void FireEngine_Spray(void)
{
    g_Scratch690 = 0;
    if (DS32(0x905D4) <= PX + 0x3c && PX - 0x24 <= DS32(0x905D4) && g_CamY <= GROUND_Y && GROUND_Y <= g_CamY + 0xb0) {
        g_Scratch690 = 1;
        for (g_LoopI = 0; g_LoopI < 6; g_LoopI++)
            if (Rand(0x14) == 10) g_EngineFire = max_i(g_EngineFire - 1, 0);
    }
    int vy = -Rand(0x100);
    Particle_Spawn((DS32(0x905D4) - 0xc) * 0x100, (0x3d6 - g_BaseYOff) * 0x100, g_Scratch690 * 0x400 - 0x800, vy, 0x20, 0x10, 0x17);
}

/* the rearm / repair (also Bonus_Repair without the gear line) */
static void restore_plane(void)
{
    for (g_LoopI = 0; g_LoopI < 0xe; g_LoopI++) g_DamageFlags[g_LoopI] = 0;
    g_FuelLeaks = 0;
    g_TurnRate = DS32(0x917E4);
    g_MaxSinkAccel = DS32(0x917D4);
    g_WingAuthority = DS32(0x917D8);
    g_DamageLampsDirty = 1;
    g_Armour = DS32(0x91834) + g_ArmourBonus;
    g_FixedGear = DS32(0x917E8);
}

/* 0x2bc0b Airbase_Update: fuel truck, mission-complete jeep, fire engine, rearm truck (-> WeaponSelect_Screen).
 * g_EngineFire is the spec's g_DamageLevel 0x900d8; g_Speed is compared as bits. */
void Airbase_Update(void)
{
    DS32(0x8FF08) = 0;
    /* fuel truck 0x901fc */
    if (IsOnScreen(g_CamX, g_CamY, DS32(0x901FC), GROUND_Y)) {
        Sprite_Queue(DS32(0x901FC) - g_CamX, (0x3df - g_CamY) - g_BaseYOff, 0x72);
        DS32(0x8FF08) = 1;
    }
    if (g_EngineFire == 0 && g_SpeedBits == 0 && g_Fuel < g_FuelBase) {
        int hi = min_i(g_CamX + 0x14a, g_BaseEndX - 0x20);
        int lo = max_i(g_CamX - 0x40, g_BaseStartX);
        int d = PX - DS32(0x901FC);
        int step = abs(d) < 5 ? abs(d) : 4;
        DS32(0x901FC) = Clamp(Sign(d) * step + DS32(0x901FC), lo, hi);
    } else {
        DS32(0x901FC) = min_i(DS32(0x901FC) + 4, g_BaseEndX - 0x20);
    }
    if (DS32(0x901FC) <= g_PlayerScrX + g_CamX + 0x10 && g_PlayerScrX + g_CamX <= DS32(0x901FC) && g_SpeedBits == 0 && g_OnGround == 1) {
        DS32(0x901FC) = g_PlayerScrX + g_CamX + 0x10;
        g_Fuel = min_i(g_Fuel + 1000, g_FuelBase + DS32(0x90648) * 1000);   /* incl. fuel pods */
    }
    /* mission-complete jeep 0x903d8 */
    if (g_MissionResult != 0) {
        if (IsOnScreen(g_CamX, g_CamY, DS32(0x903D8), GROUND_Y)) {
            Sprite_Queue(DS32(0x903D8) - g_CamX, GROUND_Y - g_CamY, (PX - DS32(0x903D8) < 1 ? 0 : 0x8000) + 0x79);
            DS32(0x8FF08) = 1;
        }
        if (g_SpeedBits < 0x3f800000) {
            int hi = min_i(g_CamX + 0x14a, g_BaseEndX - 0x20);
            int lo = max_i(g_CamX - 0x40, g_BaseStartX);
            DS32(0x903D8) = Clamp(Sign(PX - DS32(0x903D8)) * 5 + DS32(0x903D8), lo, hi);
            if (DS32(0x903D8) <= g_PlayerScrX + g_CamX + 6 && g_PlayerScrX + g_CamX - 6 <= DS32(0x903D8) && g_EjectState == 0
                && g_OnGround == 1)
                g_MissionResult = 2;
        }
    }
    /* fire engine 0x905d4 */
    if (g_EngineFire > 0 && DS32(0x90600) == 0) { DS32(0x90600) = 1; DS32(0x905D4) = g_BaseEndX; }
    if (IsOnScreen(g_CamX, g_CamY, DS32(0x905D4), GROUND_Y)) {
        Sprite_Queue(DS32(0x905D4) - g_CamX, GROUND_Y - g_CamY, 0x73);
        Sprite_Queue(DS32(0x905D4) - g_CamX + 0x20, GROUND_Y - g_CamY, 0x74);
        DS32(0x8FF08) = 1;
    }
    if (g_EngineFire < 1 || 0x3fffffff < g_SpeedBits) {
        DS32(0x905D4) = min_i(DS32(0x905D4) + 4, g_BaseEndX - 0x30);
    } else {
        int hi = min_i(g_CamX + 0x14a, g_BaseEndX - 0x30);
        DS32(0x905D4) = Clamp(Sign(PX - DS32(0x905D4) + 0x3c) * 6 + DS32(0x905D4), g_BaseStartX, hi);
    }
    if (DS32(0x905D4) <= g_PlayerScrX + g_CamX + 0x20) DS32(0x905D4) += 4;
    if (g_OnGround > 0 && g_EngineFire > 0 && g_SpeedBits < 0x3f800000 && IsOnScreen(g_CamX, g_CamY, DS32(0x905D4), GROUND_Y))
        FireEngine_Spray();
    /* rearm truck 0x9082c */
    if (IsOnScreen(g_CamX, g_CamY, DS32(0x9082C), GROUND_Y)) {
        Sprite_Queue(DS32(0x9082C) - g_CamX, GROUND_Y - g_CamY, 0x76);
        Sprite_Queue(DS32(0x9082C) - g_CamX + 0x10, GROUND_Y - g_CamY, 0x77);
        Sprite_Queue(DS32(0x9082C) - g_CamX + 0x20, GROUND_Y - g_CamY, 0x78);
        DS32(0x8FF08) = 1;
    }
    if (g_EngineFire == 0 && g_SpeedBits == 0 && g_CatapultCount == 0 && DS32(0x90854) > 8) {
        int hi = min_i(g_CamX + 0x14a, g_BaseEndX - 0x30);
        int lo = max_i(g_CamX - 0x154, g_BaseStartX);
        int d = PX - DS32(0x9082C);
        int step = abs(d) < 5 ? abs(d) : 4;
        DS32(0x9082C) = Clamp(Sign(d) * step + DS32(0x9082C), lo, hi);
        if (DS32(0x908F0) == 0 && PX - 0x20 <= DS32(0x9082C) && DS32(0x9082C) <= PX + 1 && g_SpeedBits == 0) {
            restore_plane();                                             /* rearm + repair */
            g_GearDown = DS32(0x917EC);
            if (DS32(0x907B4) == 1) DS32(0x907B4) = 0;
            Sfx_Play(0x18, 4000, 0x3f, PX);
            WeaponSelect_Screen();
            if (DS32(0x90240) == 0) DS32(0x907B4) = 1;
            g_EngineSfxRequest = 1;
            DS32(0x90854) = 0;
            if (g_CatapultCount == 0) g_CatapultCount = -1;
            g_StallLimit = 0x40;
            g_StallTopY = g_CamY;
            for (g_LoopI = 0; g_LoopI < 0x10; g_LoopI++) {}
            if (g_MissionActive != 0) Hud_DrawPanel();
            DS32(0x8FFCC) = -1;
            if (g_MissionActive == 1) DS32(0x9046C) = 1;
            if (g_FogActive != 0) Pal_SaveNight();
        }
    } else {
        DS32(0x908F0) = 0;
        DS32(0x9082C) = min_i(DS32(0x9082C) + 2, g_BaseEndX - 0x40);
    }
}

/* 0x2c55b AirbaseCrew_Update (was Balloons_Update): five crew; entry 4 is never initialised (Q20) */
void AirbaseCrew_Update(void)
{
    s32 *crx = DS32A(0x90C04), *cry = DS32A(0x90C18), *crs = DS32A(0x90BF0), *crvx = DS32A(0x90BD0);
    for (g_LoopI = 0; g_LoopI < 5; g_LoopI++) {
        int i = g_LoopI;
        if (!IsOnScreen(g_CamX, g_CamY, crx[i], cry[i])) {
            if (crs[i] > 0x1d3) {                                        /* revive */
                crs[i] = Rand(1) * 2 + 0x1d0;
                crvx[i] = (crs[i] == 0x1d2) * 2 - 1;
            }
        } else {
            Sprite_Queue(crx[i] - g_CamX, cry[i] - g_CamY, g_FrameParity * (crs[i] < 0x1d4) + crs[i]);
        }
        crx[i] = Clamp(crx[i] + crvx[i], g_BaseStartX, g_BaseEndX);
        if (crx[i] < g_BaseStartX + 0x10 || g_BaseEndX - 0x10 < crx[i])
            crvx[i] = abs(crvx[i]) * (1 - (g_BaseEndX - 0x10 < crx[i]) * 2);
        if (crs[i] > 0x1d3) {                                            /* squashed */
            crs[i] = min_i(crs[i] + 1, 0x1d6);
            crvx[i] = (crs[i] == 0x1d6) * -4 + 4;
        }
        if (abs(crx[i] - g_CamX - g_PlayerScrX) < 0x20 && abs(cry[i] - g_CamY - g_PlayerScrY) < 0x20 && crs[i] < 0x1d4
            && 0x40000000 < g_SpeedBits)
            crs[i] = 0x1d4;                                              /* run over */
    }
}

/* 0x3dc4e BaseRepair_Update: runway repair truck */
void BaseRepair_Update(void)
{
    if (IsOnScreen(g_CamX, g_CamY, DS32(0x90488), GROUND_Y))
        Sprite_Queue(DS32(0x90488) - g_CamX, (0x3df - g_CamY) - g_BaseYOff, 0x75);
    DS32(0x90488) -= 4;
    if (g_BaseStartX < DS32(0x90488) && DS32(0x90488) <= g_BaseEndX) {
        DS32(0x8FEF0) = Div16(DS32(0x90488)) + 1;
        DS32(0x8FEF8) = 0x3f - Div16(g_BaseYOff);
        if (Map_GetTileAttr(DS32(0x8FEF0), DS32(0x8FEF8), 0) != 0x81) Map_SetTile(DS32(0x8FEF0), DS32(0x8FEF8), (u8)g_RunwayFill);
    } else {
        DS32(0x90498) = 0;
    }
}

/* 0x3dd5e BaseHit_Losses: hangar / weapon-store losses. `s` is an uninitialised local in the original when the
 * stock is <= 1 (Q21): PORT, it keeps the previous value (starts 0). */
void BaseHit_Losses(void)
{
    static s32 s;
    if ((g_GameMode & 1) == 0) {
        for (g_LoopI = 1; g_LoopI <= DS32(0x901C8); g_LoopI++) {
            g_LastDamage = -1;
            if (Rand(1) == 0) {
                g_Spr98c = Rand(DS32(0x90350) - 1);
                g_PlaneUsed[g_Spr98c]++;
                g_LastDamage = 0x11;                                     /* AIRCRAFT IN HANGAR HIT ! */
            } else {
                int w = Rand(g_WeaponCount - 1);
                if (g_WeaponStock[w] > 1) s = g_WeaponStock[w];
                g_Spr98c = w;
                int r = Rand(s);
                s = g_WeaponStock[w] - r;
                if (s < 0) s = 0;
                g_WeaponStock[g_Spr98c] = s;
                g_LastDamage = 0x10;                                     /* WEAPON STORES HIT ! */
            }
        }
        if (g_LastDamage != -1) Hud_PushMessage(HUDTEXT(g_LastDamage));
        DS32(0x901C8) = 0;
    }
}

/* ====================================================================== bonus crate, pickup (§14.3/14.4) */

/* 0x16f13 Bonus_Spawn: thresholds on g_Score[0] (Q23) */
void Bonus_Spawn(void)
{
    if (g_Score[0] < g_ExtraAircraftScore) {
        if (g_BonusType == 0) {
            g_BonusType = Rand(10) + 1;
            g_BonusX = g_ObjX;
            g_BonusY = g_ObjY;
        }
        if (g_NextBonusScore <= g_Score[0]) {
            g_NextBonusScore = max_i(g_NextBonusScore + g_BonusScoreStep, g_Score[0] + 1000);
            g_BonusScoreStep = max_i(g_BonusScoreStep + 1000, g_MissionBonus);
            char buf[100];
            memcpy(buf, DSTR(0x81000), 16);                              /* "next bonus at  " */
            buf[16] = 0;
            itoa_js(g_NextBonusScore, buf + strlen(buf));
            Hud_PushMessage(buf);
        }
    } else {
        g_BonusType = 0xb;
        g_BonusX = g_ObjX;
        g_BonusY = g_ObjY;
        do g_ExtraAircraftScore <<= 1; while (g_ExtraAircraftScore < g_Score[g_AeroPlayer]);
    }
}

/* the twelve handlers of Bonus_Award (0x17353..0x17663) */
static void Bonus_FireExtinguisher(void)
{
    if (g_EngineFire < 1) DS32(0x906A8)++;
    else { int r = Rand(1); g_EngineFire = g_EngineFire - r - 1; }
}
static void Bonus_AmmoPod(void) { DS32(0x909E4) = min_i(DS32(0x909E4) + 1, 4); g_GunAmmo += 100; }
static void Bonus_FuelPod(void) { DS32(0x90648) = min_i(DS32(0x90648) + 1, 4); g_Fuel += 1000; }
static void Bonus_Repair(void)
{
    restore_plane();
    if (DS32(0x917E8) == 1) g_GearDown = 1;
}
static void Bonus_AutoEject(void) { g_RocketCount += 4; Hud_PushMessage(DSTR(0x8102C)); }   /* "Green and Left to trigger" */
static void Bonus_ECM(void) { DS32(0x90710) = 1; }
static void Bonus_Armour(void) { g_ArmourBonus = min_i(g_ArmourBonus + 1, 4); g_Armour = max_i(g_Armour + 1, 0); }
static void Bonus_Saver(void) { DS32(0x90888) = min_i(DS32(0x90888) + 1, 4); }
static void Bonus_FirePower(void) { g_GunPowerUp = min_i(g_GunPowerUp + 1, 3); }
static void Bonus_FirePower2(void) { g_GunPowerUp2 = 1; }
static void Bonus_ExtraLife(void) { g_Lives++; }
static void Bonus_ExtraEject(void) { g_PracticeEjects++; }

/* 0x17214 Bonus_Award: the message is HUDTEXT[type + 43], the previous handler's name (Q24) */
static void Bonus_Award(void)
{
    switch (g_BonusType) {
    case 0: Bonus_FireExtinguisher(); break;
    case 1: Bonus_AmmoPod(); break;
    case 2: Bonus_FuelPod(); break;
    case 3: Bonus_Repair(); break;
    case 4: Bonus_AutoEject(); break;
    case 5: Bonus_ECM(); break;
    case 6: Bonus_Armour(); break;
    case 7: Bonus_Saver(); break;
    case 8: Bonus_FirePower(); break;
    case 9: Bonus_FirePower2(); break;
    case 10: Bonus_ExtraLife(); break;
    case 11: Bonus_ExtraEject(); break;
    default: break;
    }
    g_LastDamage = g_BonusType + 0x2b;
    if (g_BonusType == 10) { g_LastDamage = 0; Hud_PushMessage(DSTR(0x81010)); }   /* "Extra Aircraft" */
    if (g_BonusType == 11) { g_LastDamage = 0; Hud_PushMessage(DSTR(0x81020)); }   /* "Auto Eject" */
    if (g_LastDamage != 0) Hud_PushMessage(HUDTEXT(g_LastDamage));
    g_BonusType = 0;
}

/* 0x17072 Bonus_Update: crate on its parachute */
void Bonus_Update(void)
{
    if (IsOnScreen(g_CamX, g_CamY, g_BonusX, min_i(g_BonusY, 0x3de))) {
        if (g_BonusY < 0x3e0) Sprite_Queue(g_BonusX - g_CamX, g_BonusY - g_CamY, g_BonusChute + 0x31);
        Sprite_Queue(g_BonusX - g_CamX, min_i(g_BonusY, 0x3de) - g_CamY, DS32A(0x9255C)[g_BonusType]);
        if (BoxOverlap(g_BonusX, min_i(g_BonusY, 0x3de), PX, PY, 0x20, 0x20)) Bonus_Award();
    }
    g_BonusChute = min_i(g_BonusChute + 1, 4);
    g_BonusY = (g_BonusY + 4) - (g_BonusChute == 4);
    if (0x51c < g_BonusY) g_BonusType = 0;
}

/* 0x15492 PrizePlane_Award: messages to Hud_Nop (not shown), next bonus-plane mission */
static void PrizePlane_Award(void)
{
    char buf[256];
    g_LastDamage = 0;
    strcpy(buf, DSTR(0x8B648));
    itoa_js(DS32(0x9084C) + 1, DSTR(0x85548));
    strcat(buf, DSTR(0x85548));
    Hud_Nop(buf);
    g_LastDamage = 1;
    Hud_Nop(DSTR(0x80FF8));                                              /* "rommel " */
    DS32(0x8F150) = (DS32(0x90AF4) + 1) * 0x5a;
    DS32(0x8F154) = (DS32(0x90AF4) + 1) * 0x5a;
    DS32(0x9084C)++;
}

/* 0x45859 Pickup_Update: prize balloon (2) / Aerolimits pickup (1), rising 1 px per frame */
void Pickup_Update(void)
{
    if (g_GameMode == 0) { g_TargetMarkX = DS32(0x9092C); g_TargetMarkY = DS32(0x90930); }
    if (IsOnScreen(g_CamX, g_CamY, DS32(0x9092C), DS32(0x90930))) {
        Sprite_Queue(DS32(0x9092C) - g_CamX, DS32(0x90930) - g_CamY, DS32(0x90918) + 0xe0);
        if (BoxOverlap(PX, PY, DS32(0x9092C), DS32(0x90930), 0x20, 0x20)) {
            g_Score[g_AeroPlayer] += DS32(0x90918) * 10000;
            DS32(0x8FFEC) = 0;
            if (DS32(0x90918) == 2) PrizePlane_Award();
            DS32(0x90918) = -1;
        }
    }
    DS32(0x90930)--;
    if (DS32(0x90930) < -2000) DS32(0x90918) = -1;
}

/* ====================================================================== alien abduction */

/* 0x164d2 FUN_000164d2: the UFO beam (GF step 34 while g_AlienAbduct > 0; never started, game_flow Q21) */
void Alien_Update(void)
{
    int dx = max_i(g_AlienAbduct - 0x50, 0);
    int dy = max_i(g_AlienAbduct * -8 + 0x1f8, 0);
    Sprite_Queue(g_PlayerScrX + dx * 0x10, g_PlayerScrY - dy, 0x4f);
    if (g_AlienAbduct > 0x3f) {
        if (g_AlienAbduct == 0x50) {
            Hud_PushMessage(DSTR(0x8A7E8));
            Hud_PushMessage(DSTR(0x8A838));
            Hud_PushMessage(DSTR(0x8A888));
        }
        if (g_AlienAbduct == 0x40) {
            g_EjectState = 1;
            g_PendingWarnSfx = 0;
            g_EjectScrX = g_PlayerScrX;
            g_EjectScrY = g_PlayerScrY - 8;
            g_EjectCamX = g_CamX;
            g_EjectCamY = g_CamY;
            g_ChuteVY = -0x10;
            DS32(0x8FEF4) = 0x10;
            g_EngineFire = 3;
        }
    }
    g_AlienAbduct = min_i(g_AlienAbduct + 1, 100);
}
