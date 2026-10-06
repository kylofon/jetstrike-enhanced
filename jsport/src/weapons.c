/* Weapons (port/spec/weapons.md): the rack trigger routines (Weapon_Fire and the launch routines of every
 * fire kind), the gun (Player_Weapons: flamer, tracers, terrain ray, hit rolls, heat, ammo, gun pods), the
 * player projectiles (Projectiles_Update 0x415e4 with its flight helpers 0x413a0..0x43d05), bullets, flares,
 * explosions (Explosion_Damage 0x39b69, Explosion_Terrain and the debris emitters), the sprite particles
 * (enemies.md §14.1), the flamer chain (enemies.md §10.1) and the JP233 / Porcupine dispensers.
 *
 * Every global lives in the data-segment image at its original address (the flight routines communicate
 * through "current projectile" globals whose values leak between projectiles and frames, weapons.md §2.2).
 * The order of the Rand() calls is the original's, including the re-evaluated clamp macros (CLAMP_REROLL). */
#include "mission.h"

#include <stdlib.h>
#include <string.h>

#include "level.h"
#include "platform.h"
#include "sound.h"
#include "video.h"

#define PX (g_CamX + g_PlayerScrX)
#define PY (g_CamY + g_PlayerScrY)
#define DIR32 g_DirHalf                         /* 0x901f0 */

/* ---- WEAPONS.DAT (game.h g_WeaponType = int[71][6]) */
#define K(w, k)            g_WeaponType[(w) * 6 + (k)]
#define g_FireRack         g_RackSel            /* 0x90338 */
#define g_RackCount        g_RackRounds         /* 0x8fcb0 */
#define W_CUR              g_RackWeapon[g_FireRack]

/* ---- projectiles (weapons.md §2.2): separate int32[21] arrays */
#define g_ProjFrame        DS32A(0x92774)
#define g_ProjBlastB       DS32A(0x927C8)
#define g_ProjLife         DS32A(0x926B8)
#define g_ProjArm          DS32A(0x912B0)
#define g_ProjMotor        DS32A(0x914AC)
#define g_ProjFlight       DS32A(0x91330)
#define g_ProjDetonate     DS32A(0x9125C)
#define g_ProjFlags        DS32A(0x913D8)
#define g_ProjTargetX      DS32A(0x92590)
#define g_ProjTargetY      DS32A(0x925E4)
#define g_ProjLock         DS32A(0x91CA0)
#define g_ProjSpriteBase   DS32A(0x92674)
#define g_ProjSprite       DS32(0x9098C)

/* launch staging */
#define g_LaunchX          DS32(0x90878)
#define g_LaunchY          DS32(0x9087C)
#define g_LaunchDirX       DS32(0x90784)
#define g_LaunchDirY       DS32(0x90790)        /* = g_TargetVY (shared) */
#define g_LaunchSfx        DS32(0x907C4)
#define g_LaunchDirTabX    DS32A(0x8FAF0)
#define g_LaunchDirTabY    DS32A(0x8FB30)
#define g_FirePairFirst    DS32(0x8FF00)
#define g_FireKindP1       DS32(0x9002C)
#define g_FireFail         g_Scratch690         /* 0x90690 */
#define g_DropTankHold     DS32(0x905E0)
#define g_HudNumOff        DS32(0x8FA38)
#define g_BigBombSfxDone   DS32(0x90198)
#define g_ExplSfxHold      g_SfxBusyTimer       /* 0x900f0 */
#define g_PhotoCol         DS32A(0x91304)
#define g_PhotoBlocked     DS32(0x9067C)
#define g_JP233Armed       DS32(0x901F4)
#define g_PorcupineArmed   DS32(0x901EC)
#define g_FuelBase         DS32(0x90378)
#define g_PodFuel          DS32(0x90788)
#define g_PlaneFuel        DS32(0x90388)
#define g_LockDist         DS32(0x90420)
#define g_PlayerVY_        DS32(0x9077C)

/* current projectile (Projectiles_Update and the flight routines) */
#define g_CurX             DS32(0x9080C)
#define g_CurY             DS32(0x90810)
#define g_CurVX            DS32(0x90934)
#define g_CurVY            DS32(0x90938)
#define g_CurFrame         DS32(0x9086C)
#define g_CurArm           DS32(0x90848)
#define g_CurKind          DS32(0x90870)
#define g_CurFlight        DS32(0x908C4)
#define g_CurMotor         DS32(0x90880)
#define g_CurFlags         DS32(0x90874)
#define g_CurDetonate      DS32(0x908D4)
#define g_CurSpeed         DS32(0x908A8)
#define g_CurTileAttr      DS32(0x908B0)
#define g_ProjStepX        DS32(0x90914)
#define g_ProjStepY        DS32(0x908CC)
#define g_AimX             DS32(0x9054C)
#define g_AimY             DS32(0x9055C)
#define g_AimDirX          DS32(0x90A24)
#define g_AimDirY          DS32(0x90A28)
#define g_AimLocked        DS32(0x907CC)
#define g_PingPongStep     DS32(0x90134)
#define g_PingPong         DS32(0x90138)
#define g_ProjLoopJ        DS32(0x90724)        /* shared enemy loop index */
#define g_MP_TargetFlag    D16(0x91674)         /* p22 */
#define g_Never8c0         DS32(0x908C0)        /* never written (0) */

/* direction tables (weapons.md §2.3) */
#define W_Dir16X           DS32A(0x8FC30)       /* read as [2*f] */
#define W_Dir16Y           DS32A(0x8FC70)
#define HEADING(dx, dy)    ((s32)(D16(0x92982 + ((dx) + 1) * 6 + (dy) * 2) / 2))

/* enemy arrays (enemies.md; damage is applied here as in the original) */
#define g_AirX             DS32A(0x9264C)
#define g_AirY             DS32A(0x92660)
#define g_AirHp            DS32A(0x9270C)
#define g_AirDmg           DS32A(0x92638)
#define g_AirBurn          DS32A(0x8F0F0)
#define g_AirWreckX        DS32A(0x8F118)
#define g_AirWreckY        DS32A(0x8F128)
#define g_GndX             DS32A(0x8D9D8)
#define g_GndY             DS32A(0x8D9E8)
#define g_GndHp            DS32A(0x8D9A8)
#define g_GndDmg           DS32A(0x8D998)
#define g_BigActive        DS32(0x906DC)
#define g_BigX             DS32(0x9073C)
#define g_BigY             DS32(0x90740)
#define g_BigHp            DS32(0x9072C)
#define g_BigKilled        DS32(0x90728)

/* gun (weapons.md §2.7) */
#define g_GunFlameMode     DS32(0x906B0)
#define g_GunSpreadA       DS32(0x90508)
#define g_GunSpreadB       DS32(0x905A4)
#define g_GunSpreadA2      DS32(0x90560)
#define g_GunSpreadB2      DS32(0x90564)
#define g_GunHitBonus      DS32(0x90850)
#define g_GunPower         DS32(0x90558)
#define g_GunType          DS32(0x901F8)
#define g_GunHeat          DS32(0x905B4)
#define g_GunFiring        DS32(0x905B0)
#define g_GunPodFiring     DS32(0x90584)
#define g_GunRange         DS32(0x90494)
#define g_GunRayHit        DS32(0x90534)
#define g_GunDisabled      DS32(0x8FA50)
#define g_BulletVX         DS32(0x93394)
#define g_BulletVY         DS32(0x93398)
#define g_GunSide          DS32(0x90248)
#define g_GunDX            DS32(0x90598)
#define g_GunDY            DS32(0x9059C)

/* flamer chain (enemies.md §10.1) */
#define g_FlameActive      DS32(0x90608)
#define g_FlameX           DS32A(0x9142C)
#define g_FlameY           DS32A(0x91458)
#define g_FlameF           DS32A(0x91500)
#define g_FlameVX          DS32(0x905F4)
#define g_FlameVY          DS32(0x905F8)
#define g_Flame62c         DS32(0x9062C)

/* bullets, particles */
#define g_Bullets          DS32A(0x92B20)       /* [32][4]: x8, y8, vx, vy */
#define g_BulletCount      DS32(0x93224)
#define g_Particles        DS32A(0x92D20)       /* [40][8] */
#define g_ParticleCount    DS32(0x93228)
#define g_PartAnim         ((s16 *)DSEG(0x8017C))

/* flares */
#define g_FlaresPending    DS32(0x906A0)
#define g_FlareCount       DS32(0x90310)
#define g_FlareLife        DS32A(0x8FAD8)
#define g_FlareX           DS32A(0x8FAE0)
#define g_FlareY           DS32A(0x8FAE8)
#define g_FlareVX          DS32A(0x8FBB0)
#define g_FlareVY          DS32A(0x8FBB8)
#define g_EnemyMslCount    DS32(0x906FC)
#define g_EnemyMslX        DS32A(0x8FAB8)
#define g_EnemyMslY        DS32A(0x8FAC8)
#define g_EnemyMslMode     DS32A(0x8FA58)

/* explosions */
#define g_ExplX            DS32(0x933A8)
#define g_ExplY            DS32(0x933AC)
#define g_ExplPower        DS32(0x933A0)
#define g_ExplPowerB       DS32(0x933A4)
#define g_ExplSfxPower     DS32(0x90210)
#define g_ExplCraters      DS32(0x9029C)
#define g_ExplX0           DS32(0x9093C)
#define g_ExplY0           DS32(0x90950)
#define g_ExplI            DS32(0x90868)
#define g_ExplCX           DS32(0x8FEC4)
#define g_ExplCY           DS32(0x8FECC)
#define g_ExplSolid        DS32(0x90294)
#define g_ExplMode         DS32(0x8FEA4)        /* debris mode / scratch (= g_DmgLoop) */
#define g_ExplDX8          DS32(0x90278)
#define g_ExplSpread       DS32(0x9027C)
#define g_ExplWater        DS32(0x8FF44)
#define g_ExplVX           DS32(0x90924)        /* never written (Q15) */
#define g_ExplVY           DS32(0x90928)
#define g_BaseHit          DS32(0x904AC)
#define g_ConvoyCountRT    DS32(0x905AC)        /* convoy vehicles alive (enemies.md §7.1) */
#define g_B52TargetX       DS32(0x9037C)        /* "marker" bomb -20 */
#define g_B52TargetY       DS32(0x90380)
#define g_DropZoneX        DS32(0x90398)        /* "drop zone" bomb -30 */
#define g_DropZoneY        DS32(0x903A0)

static void hud_rack_count(void)
{
    int r = g_FireRack;
    Video_FillRect(0x54, r * 0x12 + 0x20, 0x6a, r * 0x12 + 0x25, 0x1d);
    Text_DrawNumber(0x54, r * 0x12 + 0x20, g_RackCount[r], 3);
}

static int min_i(int a, int b) { return a < b ? a : b; }
static int max_i(int a, int b) { return a > b ? a : b; }

/* Mission target centre (weapons.md §5.8): (X1-X0)*8 + X0*16, unsigned arithmetic of the u16 params. */
static int mtc(void) { return (int)(((u32)g_MP_TargetX1 - (u32)g_MP_TargetX0) * 8u + (u32)g_MP_TargetX0 * 16u); }
static int mtcr(void)
{
    int a = Rand(0x20);
    int b = Rand(0x20);
    return mtc() + a - b;
}

/* ====================================================================== particles (enemies.md §14.1) */

/* 0x26520 Particles_Nop */
void Particles_Nop(void) {}

/* 0x2653b Particles_Clear */
void Particles_Clear(void) { g_ParticleCount = 0; }

/* 0x26560 Particle_Spawn(x8, y8, vx8, vy8, a5, life, anim) */
void Particle_Spawn(int x, int y, int vx, int vy, int a5, int a6, int a7)
{
    if (g_ParticleCount < 0x28) {
        s32 *p = &g_Particles[g_ParticleCount * 8];
        p[0] = x; p[1] = y; p[2] = vx; p[3] = vy; p[4] = a5; p[5] = a6; p[6] = a7; p[7] = 0;
        g_ParticleCount++;
    }
}

/* 0x2661e Particles_Update(camX, camY) */
void Particles_Update(int camX, int camY)
{
    for (int i = 0; i < g_ParticleCount; i++) {
        s32 *p = &g_Particles[i * 8];
        int dead = 0;
        int X = p[0] >> 8, Y = p[1] >> 8;
        if (IsOnScreen(camX, camY, X, Y)) {
            int f = p[7]++;
            if (f == 7) p[7] = 0;
            int s = g_PartAnim[p[6] * 8 + f];
            if (s == -1) { p[7] = 0; s = g_PartAnim[p[6] * 8]; }
            if (s == 0) dead = 1;
            else {
                p[0] += p[2];
                p[1] += p[3];
                Sprite_Queue(X - camX, Y - camY, s);
            }
        }
        if (p[5] < 1) dead = 1;
        p[5]--;
        if (dead) {
            g_ParticleCount--;
            memmove(p, &g_Particles[g_ParticleCount * 8], 32);
            i--;
        }
    }
}

/* ====================================================================== bullets (weapons.md §6) */

/* 0x2688f Bullets_Clear */
void Bullets_Clear(void)
{
    g_BulletCount = 0;
    DS32(0x93220) = 0;
}

/* 0x268d9 Bullet_Add */
void Bullet_Add(int x8, int y8, int vx, int vy)
{
    if (g_BulletCount < 0x20) {
        s32 *b = &g_Bullets[g_BulletCount * 4];
        b[0] = x8; b[1] = y8; b[2] = vx; b[3] = vy;
        g_BulletCount++;
    }
}

/* PORT: the height table read of a bullet left of column 0 would read the heap before g_MapVal; 0 there. */
static u32 mapval_safe(int i) { return (i >= 0 && i < 0x13b8) ? Byte_Get(g_MapVal, i) : 0; }

/* 0x2694b Bullets_Update(camX, camY): tracer pixels, colour 0xff */
void Bullets_Update(int camX, int camY)
{
    for (int i = 0; i < g_BulletCount; i++) {
        s32 *b = &g_Bullets[i * 4];
        b[0] += b[2] * 0x20;
        b[1] += b[3] * 0x20;
        if (b[3] != 0) b[3] += Sign(b[3]) * 6;
        int sx = (b[0] >> 8) - camX + 0x14;
        int sy = (b[1] >> 8) - camY + 0x14;
        /* ENH: view: the bullet lives while it is in the view (the gun reaches the right edge) */
        if (sx < 0 || sx > 0x140 + VIEW_EXTRA_COLS || sy < 0 || sy > 0xb0 + VIEW_EXTRA_ROWS || (int)(mapval_safe((b[0] >> 0xc) + 0xbd0) & 0xff) <= (b[1] >> 0xc)) {
            g_BulletCount--;
            memmove(b, &g_Bullets[g_BulletCount * 4], 16);
            i--;
        } else {
            Video_PutPixel((u32)(sx + g_ScrollFineX), sy + g_BackPage / VRAM_ROWB + g_ScrollFineY, 0xff);
        }
    }
}

/* ====================================================================== flares (weapons.md §7) */

/* 0x38a61 Flare_Release */
void Flare_Release(void)
{
    if (g_FlaresPending > 0 && g_FlareCount < 2) {
        g_FlaresPending--;
        int n = g_FlareCount;
        g_FlareX[n] = PX;
        g_FlareY[n] = PY;
        g_FlareLife[n] = Rand(0x14) + 10;
        g_FlareVX[n] = Clamp(g_PlayerVX, -4, 4);
        g_FlareVY[n] = Clamp(g_PlayerVY_, -4, 4);
        g_FlareCount++;
    }
}

/* Flare movement and decoy: the flare block of SupportAircraft_Update 0x2780b (0x28e6d-0x2919a). */
void SupportAircraft_Flares(void)
{
    if (g_FlareCount <= 0) return;
    for (g_LoopI = g_FlareCount - 1; g_LoopI >= 0; g_LoopI--) {
        int i = g_LoopI;
        if (IsOnScreen(g_CamX, g_CamY, g_FlareX[i], g_FlareY[i]))
            Sprite_Queue(g_FlareX[i] - g_CamX, g_FlareY[i] - g_CamY, 0xa0);
        g_FlareX[i] += g_FlareVX[i];
        g_FlareY[i] = min_i(g_FlareY[i] + g_FlareVY[i], 0x3f0);
        if (Rand(1) + g_FlareVY[i] < 9) g_FlareVY[i] = g_FlareVY[i] + Rand(1);
        else g_FlareVY[i] = 8;
        g_FlareLife[i]--;
        if (g_EnemyMslCount > 0)
            for (g_ProjLoopJ = 0; g_ProjLoopJ < g_EnemyMslCount; g_ProjLoopJ++)
                if (BoxOverlap(g_EnemyMslX[g_ProjLoopJ], g_EnemyMslY[g_ProjLoopJ], g_FlareX[i], g_FlareY[i], 0x40, 0x40))
                    g_EnemyMslMode[g_ProjLoopJ] = min_i(g_EnemyMslMode[g_ProjLoopJ], 1);
        if (g_FlareLife[i] < 1) {
            int l = g_FlareCount - 1;
            SwapInt(&g_FlareLife[i], &g_FlareLife[l]);
            SwapInt(&g_FlareY[i], &g_FlareY[l]);
            SwapInt(&g_FlareX[i], &g_FlareX[l]);
            SwapInt(&g_FlareVX[i], &g_FlareVX[l]);
            SwapInt(&g_FlareVY[i], &g_FlareVY[l]);
            g_FlareCount--;
        }
    }
}

/* 0x346d3 Flares_Add (k0 12) */
void Flares_Add(void)
{
    g_FlaresPending++;
    Flare_Release();
}

/* ====================================================================== launch routines (weapons.md §3) */

/* 0x34e56 Projectile_Init */
static void Projectile_Init(void)
{
    int n = g_ProjCount, w = W_CUR;
    g_ProjBlastA[n] = g_WeaponBlastA[w];
    g_ProjBlastB[n] = g_WeaponBlastB[w];
    g_ProjX[n] = g_LaunchX;
    g_ProjY[n] = g_LaunchY + 8;
    g_ProjLife[n] = g_WeaponThrust[w];
}

/* forward speed of a dropped store: Clamp(|vx/2| + thrust, 0, 12) (macro, no Rand) */
static int drop_speed(void)
{
    return Clamp(abs(g_PlayerVX / 2) + g_WeaponThrust[W_CUR], 0, 12);
}

/* 0x3529c Weapon_LaunchBallistic (k0 0, 3, 6, 7) */
void Weapon_LaunchBallistic(void)
{
    Projectile_Init();
    int n = g_ProjCount;
    g_ProjFrame[n] = 0;
    g_ProjVX[n] = drop_speed() * Sign(g_LaunchDirX);
    g_ProjVY[n] = Clamp(g_LaunchDirY, 0, 0x10);
    g_LaunchSfx = 1;
    int a = g_ProjBlastA[n];
    if (a == -0x28) {
        Hud_PushMessage(DSTR(0x81358));                              /* "Nuclear Psychopath Warning!" */
        g_FireLatch = 1;
    }
    g_LaunchSfx = (a != -0x28);
    if (g_ProjBlastA[n] == -10 && g_ViewTarget == -1) {
        Hud_PushMessage(DSTR(0x81375));                              /* "Following Camera Pod" */
        g_ViewTarget = g_ProjCount;
        g_LaunchSfx = 0;
    }
    if ((g_ProjBlastA[n] > 400 && Rand(10) == 1 && g_BigBombSfxDone == 0) || g_ProjBlastA[n] == -0x28) {
        Sfx_Play(0x15, 0x1004, 0x3f, PX);
        g_BigBombSfxDone = 1;
    }
}

/* 0x35532 Weapon_FireRocket (k0 1) */
void Weapon_FireRocket(void)
{
    int d = DIR32 / 2;
    if (d == 0 || d > 7 || g_IsHeli == 1) {
        Projectile_Init();
        int n = g_ProjCount;
        g_ProjVX[n] = Sign(g_LaunchDirX) << 5;
        g_ProjVY[n] = (Sign(g_LaunchDirY) << 5) < 0 ? 0 : Sign(g_LaunchDirY) << 5;
        g_ProjFrame[n] = DIR32 / 2 - 8;
        if (g_ProjFrame[n] < 0) g_ProjFrame[n] = 8;
        Sfx_Play(8, 4000, 0x3f, PX);
        if (g_IsHeli != 0) {
            if (DIR32 < 6) { g_ProjFrame[n] = 7; g_ProjVX[n] = -0x20; }
            else { g_ProjFrame[n] = 2; g_ProjVX[n] = 0x20; }
        }
    } else {
        g_FireFail = 1;
    }
    if (g_ProjBlastA[g_ProjCount] == -10 && g_ViewTarget == -1) {   /* stale slot after a refusal (Q22) */
        Hud_PushMessage(DSTR(0x8138B));                              /* "Following Camere Pod" (sic) */
        g_ViewTarget = g_ProjCount;
        g_LaunchSfx = 0;
    }
}

/* 0x356fe Weapon_FireGuided (k0 2) */
void Weapon_FireGuided(void)
{
    g_FireLatch = 1;
    if (K(W_CUR, 3) < 1) {
        if (g_LockTarget < 0) { g_FireFail = 1; return; }
        int m = max_i(g_WeaponThrust[g_RackWeapon[0]], g_WeaponThrust[g_RackWeapon[1]]);   /* both racks (quirk) */
        int range = (m * 0x20 < 0x280) ? 0x280 : m << 5;
        if (g_LockDist > range) { g_FireFail = 1; return; }
    }
    if (K(W_CUR, 3) == 0) g_ProjLock[g_ProjCount] = g_LockTarget + 1;
    Projectile_Init();
    int n = g_ProjCount;
    g_ProjVX[n] = Sign(g_LaunchDirX);
    Sfx_Play(8, 4000, 0x3f, PX);
    if (K(W_CUR, 1) == 0) {
        g_ProjVY[n] = Sign(g_LaunchDirY) < 0 ? 0 : Sign(g_LaunchDirY);
    } else {
        g_ProjVY[n] = 2;
        g_ProjVX[n] = Clamp(abs(g_LaunchDirX) + g_WeaponThrust[W_CUR], 0, 0x10) * Sign(g_LaunchDirX);
    }
    if (g_ProjVX[n] == 0 && g_ProjVY[n] == 0) g_ProjVX[n] = 1;
    g_ProjFrame[n] = DIR32 / 4;
}

/* 0x348db Weapon_TakePhoto (k0 4, camera pod) */
void Weapon_TakePhoto(void)
{
    g_FireLatch = 1;
    if (g_PhotoCount < 10 && g_PhotoBlocked == 0) {
        g_PhotoCol[g_PhotoCount] = Div16(PX);
        g_PhotoCount++;
    } else {
        g_FireFail = -1;
    }
}

/* 0x34f0d Weapon_DropTank (k0 5) */
void Weapon_DropTank(void)
{
    g_DropTankHold++;
    if (g_DropTankHold < 10) return;
    int other = (K(g_RackWeapon[1 - g_FireRack], 0) == 5);
    DS32(0x905D0) = g_PlaneFuel - other * g_PodFuel;
    int n = g_ProjCount;
    g_ProjX[n] = g_LaunchX;
    g_ProjY[n] = g_LaunchY + 8;
    g_ProjBlastA[n] = Clamp(g_Fuel - DS32(0x905D0), 0, 500);
    g_ProjBlastB[n] = 2000;
    g_ProjFrame[n] = 0;
    g_ProjLife[n] = 0;
    g_ProjVX[n] = drop_speed() * Sign(g_LaunchDirX);
    g_ProjVY[n] = Clamp(g_LaunchDirY, 0, 0x10);
    g_LaunchSfx = 1;
}

/* 0x351e9 Weapon_DropCommando (k0 10) */
void Weapon_DropCommando(void)
{
    g_FireLatch = 1;
    Projectile_Init();
    int n = g_ProjCount;
    g_ProjFrame[n] = 0;
    g_ProjVX[n] = 0;
    g_ProjVY[n] = Clamp(g_LaunchDirY, 0, 8);
    g_LaunchSfx = 1;
}

/* 0x34dd0 Weapon_ArmPorcupine (k0 11) */
void Weapon_ArmPorcupine(void)
{
    g_FireLatch = 1;
    if (g_PorcupineArmed == 0) { g_PorcupineArmed = g_FireRack + 1; g_ViewTarget = -1; }
}

/* 0x34e13 Weapon_ArmJP233 (k0 8) */
void Weapon_ArmJP233(void)
{
    g_FireLatch = 1;
    if (g_JP233Armed == 0) { g_JP233Armed = g_FireRack + 1; g_ViewTarget = -1; }
}

/* 0x3494e Weapon_DispenseJP233 / 0x34b94 Weapon_DispensePorcupine (weapons.md §3.11) */
static void dispense(s32 *armed, int porcupine)
{
    g_ViewTarget = -1;
    int d = DIR32 / 2;
    int r1 = Rand(4);
    int t = g_LaunchDirTabX[d];
    int r2 = Rand(4);
    int heli = g_IsHeli;
    g_LaunchDirX = t + r1 - r2;
    d = DIR32 / 2;
    g_LaunchDirY = Rand(2) + g_LaunchDirTabY[d * 2 + heli];
    if (g_EjectState == 0) { g_LaunchX = PX; g_LaunchY = PY; }
    if (g_RackCount[*armed - 1] == 0 || g_ProjCount == 0x15) { *armed = 0; return; }
    Sfx_Play(2, 20000, 0x1e, PX);
    g_RackCount[*armed - 1]--;
    if (!porcupine) g_FireKindP1 = 9;
    g_FireRack = *armed - 1;
    if (porcupine) Weapon_FireGuided(); else Weapon_LaunchBallistic();
    int n = g_ProjCount, w = g_RackWeapon[*armed - 1];
    g_ProjKind[n] = porcupine ? 2 : 7;
    g_ProjArm[n] = K(w, 1);
    g_ProjMotor[n] = K(w, 2);
    g_ProjFlight[n] = K(w, 3);
    g_ProjDetonate[n] = K(w, 4);
    g_ProjFlags[n] = K(w, 5);
    g_ProjTargetX[n] = 0;
    g_ProjTargetY[n] = 0;
    g_ProjCount = min_i(g_ProjCount + 1, 0x15);
}
void Weapon_DispenseJP233(void) { dispense(&g_JP233Armed, 0); }
void Weapon_DispensePorcupine(void) { dispense(&g_PorcupineArmed, 1); }

/* 0x177db AgentSmoke_Update (enemies.md §15) */
static void AgentSmoke_Update(void)
{
    for (g_LoopI = 0; g_LoopI < g_SmokeCount; g_LoopI++)
        if (IsOnScreen(g_CamX, g_CamY, g_SmokeX[g_LoopI], g_SmokeY[g_LoopI]) && Rand(1) != 0) g_SmokeT[g_LoopI]--;
    DS32(0x900F8)++;
    if (DS32(0x900F8) > 9) {
        for (g_LoopI = g_SmokeCount - 1; g_LoopI >= 0; g_LoopI--) {
            if (g_SmokeT[g_LoopI] < 1) {
                int l = g_SmokeCount - 1;
                SwapInt(&g_SmokeX[g_LoopI], &g_SmokeX[l]);
                SwapInt(&g_SmokeY[g_LoopI], &g_SmokeY[l]);
                SwapInt(&g_SmokeT[g_LoopI], &g_SmokeT[l]);
                g_SmokeCount--;
            }
        }
        DS32(0x900F8) = 0;
    }
}

/* 0x15ce6 Weapons_FrameDispensers (GF step 33, even frames) */
void Weapons_FrameDispensers(void)
{
    if (g_SmokeCount != 0) AgentSmoke_Update();
    if (g_JP233Armed != 0) Weapon_DispenseJP233();
    if (g_PorcupineArmed != 0) Weapon_DispensePorcupine();
}

/* 0x346f9 Weapon_FireGunPod (k0 9): the gun once with the pod's parameters (power not restored, Q5) */
void Weapon_FireGunPod(void)
{
    int r = g_FireRack;
    if (g_RackCount[r] > 0) {
        DS32(0x90018) = g_GunAmmo;
        g_GunAmmo = g_RackCount[r];
        DS32(0x90024) = g_Fuel;
        g_Fuel = g_GunAmmo * 200;
        g_RackCount[r]--;
        int w = g_RackWeapon[g_FireRack];
        DS32(0x90054) = g_GunFlameMode; g_GunFlameMode = K(w, 1);
        DS32(0x90044) = g_GunHitBonus;  g_GunHitBonus = K(w, 2);
        DS32(0x90040) = g_GunPower;     g_GunPower = K(w, 3);
        DS32(0x8FF84) = g_GunType;      g_GunType = K(w, 4);
        DS32(0x8FFA8) = g_GunSpreadA;   g_GunSpreadA = 2;
        DS32(0x9003C) = g_GunSpreadB;   g_GunSpreadB = 0;
        DS32(0x90010) = g_GunSpreadA2;  g_GunSpreadA2 = 0;
        DS32(0x90014) = g_GunSpreadB2;  g_GunSpreadB2 = 0;
        g_GunPodFiring = 1;
        Player_Weapons();
        g_GunPodFiring = 0;
        g_Fuel = DS32(0x90024);
        g_GunAmmo = DS32(0x90018);
        g_GunFlameMode = DS32(0x90054);
        g_GunHitBonus = DS32(0x90044);
        g_GunType = DS32(0x8FF84);
        g_GunSpreadA = DS32(0x8FFA8);
        g_GunSpreadB = DS32(0x9003C);
        g_GunSpreadA2 = DS32(0x90010);
        g_GunSpreadB2 = DS32(0x90014);
    }
    g_FireFail = -1;
}

/* 0x341e9 Weapon_Fire (GF step 85, twice per accepted press) */
void Weapon_Fire(void)
{
    if (g_ProjCount >= 0x15 || g_FireLatch != 0) return;
    g_LaunchX = PX;
    g_LaunchY = PY + g_FirePairFirst * 4;
    g_LaunchDirX = g_LaunchDirTabX[DIR32 / 2];
    g_LaunchSfx = 0;
    g_LaunchDirY = g_LaunchDirTabY[(DIR32 / 2) * 2 + g_IsHeli];
    g_FireKindP1 = K(W_CUR, 0) + 1;
    g_FireFail = 0;
    switch (K(W_CUR, 0)) {
    case 0: case 3: case 6: case 7: Weapon_LaunchBallistic(); break;
    case 1: Weapon_FireRocket(); break;
    case 2: Weapon_FireGuided(); break;
    case 4: Weapon_TakePhoto(); break;
    case 5: Weapon_DropTank(); break;
    case 8: Weapon_ArmJP233(); break;
    case 9: Weapon_FireGunPod(); break;
    case 10: Weapon_DropCommando(); break;
    case 11: Weapon_ArmPorcupine(); break;
    case 12: Flares_Add(); break;
    default: break;
    }
    if (g_FireFail != 0 || g_FireKindP1 <= 0) return;
    g_LoadWeight -= g_WeaponWeight[W_CUR];
    if (g_FireKindP1 != 0xc) {                   /* k0 != 11; flares / photos / JP233 still register (Q2) */
        int n = g_ProjCount, w = W_CUR;
        g_ProjKind[n] = K(w, 0);
        g_ProjArm[n] = K(w, 1);
        g_ProjMotor[n] = K(w, 2);
        g_ProjFlight[n] = K(w, 3);
        g_ProjDetonate[n] = K(w, 4);
        g_ProjFlags[n] = K(w, 5);
        if (g_LaunchSfx == 1) {
            Sfx_Play(5, 6000, 0x28, PX);
            g_ExplSfxHold = 10;
        }
    }
    if (g_FireKindP1 == 6) {                     /* drop tank: jettisoned on the 10th call */
        if (g_DropTankHold > 9) {
            g_RackCount[g_FireRack] = 0;
            int other = (K(g_RackWeapon[1 - g_FireRack], 0) == 5);
            g_FuelBase -= other * g_PodFuel;
            g_Fuel = g_Fuel < g_FuelBase ? g_Fuel : g_FuelBase;
            g_LoadWeight -= g_PodFuel / 4;
            g_ProjCount = min_i(g_ProjCount + 1, 0x15);
            if (g_HudNumOff == 0) hud_rack_count();
        }
        return;
    }
    g_RackCount[g_FireRack] -= g_FirePairFirst;
    if (g_GameMode == 3) { g_RackCount[0]--; g_RackCount[1]--; }   /* Aerolimits (Q3) */
    if (g_FireKindP1 != 0xc) {
        g_ProjTargetX[g_ProjCount] = 0;
        g_ProjTargetY[g_ProjCount] = 0;
        g_ProjCount = min_i(g_ProjCount + 1, 0x15);
    }
    if (g_HudNumOff == 0 && g_GameMode < 3) hud_rack_count();
}

/* ====================================================================== the gun (weapons.md §4) */

/* 0x32846 Player_Weapons */
void Player_Weapons(void)
{
    if (g_GunFlameMode != 0) {                   /* flamer (§4.1) */
        if (g_GunDisabled == 0 && g_Fuel > 0 && g_FlameActive == 0) {
            Sfx_Play(8, 3000, 0x3f, PX);
            g_Fuel -= 200;
            g_Flame62c = (u32)(DIR32 / 2 + g_GunFlameMode - 1) & 0xf;
            g_FlameX[0] = PX + g_Dir16X[g_Flame62c] * 8;
            g_FlameY[0] = PY + g_Dir16Y[g_Flame62c] * 0xc;
            g_FlameF[0] = 0;
            g_FlameVX = g_Dir16X[g_Flame62c];
            g_FlameVY = g_Dir16Y[g_Flame62c] << 2;
            g_FlameActive = 1;
        }
        return;
    }
    if (g_GunAmmo <= 0 || g_GunHeat >= 0x14 || g_GunDisabled != 0) return;
    g_GunFiring = 1;
    if (g_ViewTarget != 200) g_ViewTarget = -1;
    if (g_GunType == 0) {
        int p = g_GunPower + g_GunPowerUp;
        DS32(0x90588) = (p > 0);
        Sfx_Play(DS32(0x90588) * 0xf + 2, 20000 / (DS32(0x90588) + 1) - (p % 10) * 1000, 0x20, PX);
    } else {
        Sfx_Play(1, 14000, 0x10, 0);
        Sfx_Play(3, 0x1194, 0x20, 0);
    }
    if (g_HudNumOff == 0 && g_GunPodFiring == 0) {
        Video_FillRect(0x55, 10, 0x69, 0xf, 0x1d);
        Text_DrawNumber(0x55, 10, g_GunAmmo, 4);
    }
    g_BulletVX = 0;
    g_BulletVY = 0;
    if ((g_GunAmmo & 3) != 0 || DS32(0x90858) > 0 || g_GunSpreadA > 0) {          /* tracer */
        g_GunSide = (g_GunAmmo & 1) * -2 + 1;
        if (DS32(0x90858) == 0 && g_GunSpreadA == 0) g_GunSide = 1;
        g_GunDX = g_DirGunX[DIR32] / 2 + DS32(0x907A4) / 8;
        g_GunDY = g_DirGunY[DIR32] / 2 + DS32(0x907A8) / 8;
        if (g_IsHeli != 0) {
            DS32(0x90530) = DS32(0x9339C);
            g_GunDY = DS32A(0x8D860)[DS32(0x9339C)];
            g_GunDX = DS32A(0x8D818)[DS32(0x9339C)];
        }
        if (g_GunDY > 0) g_StrafeCount += 2;
        g_BulletVX = g_GunDX * 0x28;
        g_BulletVY = g_GunDY * 0x28;
        int by = (int)((u32)g_GunSide * (u32)(g_GunSpreadB + g_GunSpreadB2) * (u32)g_DirGunX[DIR32]) / 8;
        int r1 = Rand(6);
        int y = (r1 * g_GunType + PY + by) * 0x100;
        int x0 = (g_CamX - 0xc + g_PlayerScrX) - g_ScrollFineX;
        int r2 = Rand(6);
        int bx = (int)((u32)(g_GunSpreadA + g_GunSpreadA2) * (u32)g_DirGunY[DIR32] * (u32)g_GunSide) / 2;
        Bullet_Add((bx + x0 + r2 * g_GunType) * 0x100, y, g_BulletVX, g_BulletVY);
    }
    if (g_LockTarget == -1) {                    /* ray to the terrain (§4.3) */
        DS32(0x8FEF0) = PX;
        DS32(0x8FEF8) = PY;
        if (g_ViewTarget == 200) { DS32(0x8FEF0) = g_ViewX; DS32(0x8FEF8) = g_ViewY; }
        g_GunDX = g_DirVX[DIR32];
        g_GunDY = g_DirLift[DIR32];
        if (g_IsHeli != 0) {
            DS32(0x90530) = DS32(0x9339C);
            g_GunDX = DS32A(0x8D818)[DS32(0x9339C)];
            g_GunDY = DS32A(0x8D860)[DS32(0x9339C)];
        }
        g_BulletVX = (g_GunDX * g_GunRange) / 10;
        g_BulletVY = (g_GunDY * g_GunRange) / 3 + 0x1000;
        g_GunRayHit = Ray_Trace(DS32(0x8FEF0) - 0xc, DS32(0x8FEF8), g_BulletVX, g_BulletVY);
        if (g_GunRayHit > 0x7e) {
            DS32(0x90538) = Ray_HitX();
            DS32(0x9053C) = Ray_HitY();
            int p = (g_GunPower + g_GunPowerUp) * 6 + 6;
            Explosion_Damage(DS32(0x90538), DS32(0x9053C), 0, 0, p, p);
        }
        g_GunRayHit = 0;
    }
    for (g_Flame62c = 0; g_Flame62c < g_GunType / 2; g_Flame62c++) {          /* hit rolls */
        int bonus = (g_GunHitBonus != 0 || g_GunPowerUp2 != 0);
#define DMGN (g_GunPower + 1 + g_GunPowerUp + g_GunHitBonus + g_GunPowerUp2)
        /* ENH: view: kept, player-relative */
        if (g_LockTarget == 2 && g_LockDist < 0x141 && Rand(8) < bonus * 4 + 2) {
            g_BigHp -= Rand(DMGN);
            int xx = g_BigX + Rand(0x20);
            xx -= Rand(0x20);
            Explosion_Damage(xx, g_BigY, 0, 0, 5, 5);
            if (g_BigHp < 0) {
                g_BigKilled = 1;
                for (g_GunSide = 1; g_GunSide < 5; g_GunSide++)
                    Explosion_Damage(g_GunSide * 0x10 + g_BigX - 0x20, g_BigY, 0, 0, 2000, 2000);
            }
        }
        if (g_LockTarget > 2 && g_LockDist < 0x141 && Rand(0xd) < bonus * 4 + 2) {
            int t = g_LockTarget;
            DS32A(0x8D99C)[t] -= Rand(DMGN);
            if (DS32A(0x8D99C)[g_LockTarget] < 0) {
                t = g_LockTarget;
                DS32A(0x8D98C)[t] -= DS32A(0x8D99C)[t];
                DS32A(0x8D99C)[t] = 0;
                Explosion_Damage(DS32A(0x8D9CC)[t], DS32A(0x8D9DC)[t], 0, 0, 5, 5);
            }
        }
        if (g_LockTarget > -1 && g_LockDist < 0x141 && g_IsHeli == 0 && g_LockTarget < 2 && Rand(0xd) < bonus * 4 + 2) {
            int t = g_LockTarget;
            g_AirHp[t] -= Rand(DMGN);
            t = g_LockTarget;
            if (g_AirHp[t] < 0) {
                g_AirDmg[t] -= g_AirHp[t];
                g_AirHp[t] = 0;
                if (g_AirDmg[t] > 2 && g_AirBurn[t] == 0) { g_AirBurn[t] = 1; g_AirWreckX[t] = g_AirX[t]; g_AirWreckY[t] = g_AirY[t]; }
            }
            Explosion_Damage(g_AirX[t], g_AirY[t], 0, 0, 5, 5);
        }
#undef DMGN
    }
    if (g_GunPodFiring == 0) {                   /* heat, ammo, out-of-ammo voice (§4.4) */
        g_GunHeat += g_GunType * 2;
        if (g_GunHeat > 0x26) g_GunHeat = 0x28;
        g_GunAmmo = g_GunAmmo - g_GunType * 2 - 1;
        if (g_GunAmmo < 1 && g_EnemyAirCount > 0 && abs(g_AirX[0] - g_CamX - g_PlayerScrX) < 300
            && abs(g_AirY[0] - g_CamY - g_PlayerScrY) < 300 && g_PlaneClass > 0 && DS32(0x90144) == 0) {
            Sfx_Play(0x19, 8000, 0x3f, PX);
            DS32(0x90144) = 1;
        }
    }
}

/* 0x17c56 Flamer_Update (enemies.md §10.1; GF step 29 while g_FlameActive) */
void Flamer_Update(void)
{
    g_FireFail = 0;
    for (g_LoopI = 0; g_LoopI < g_FlameActive; g_LoopI++) {
        int i = g_LoopI;
        if (IsOnScreen(g_CamX, g_CamY, g_FlameX[i], g_FlameY[i]))
            Sprite_Queue(g_FlameX[i] - g_CamX, g_FlameY[i] - g_CamY, DS32A(0x8FC18)[g_FlameF[i]]);
        if (g_FlameX[i] > -1) {
            g_FireFail = 1;
            g_FlameX[i] += g_FlameVX * 2;
            g_FlameY[i] += g_FlameVY * 2;
        }
        g_FlameF[i]++;
        if (g_FlameF[i] > 5) { g_FlameX[i] = -1; g_FlameF[i] = 0; }
    }
    g_ProjSprite = g_FlameActive;
    int m = g_ProjSprite;
    g_FlameX[g_FlameActive] = g_FlameX[0] + g_FlameVX * g_FlameActive * 2;
    g_FlameY[m] = min_i(g_FlameVY * 2 * m + g_FlameY[0], 0x3e0);
    g_FlameF[m] = 0;
    if (g_FlameY[m] > -1) {
        DS32(0x9060C) = Clamp(Div16(g_FlameX[m]) + 1, 0, g_MapWidth - 1);
        DS32(0x90610) = Clamp(Div16(g_FlameY[m]) + 1, 0, 0x3f);
        g_TrigClass = Map_GetTileAttr(DS32(0x9060C), DS32(0x90610), 0);
        if (g_TrigClass > 0x7e) {
            DS32(0x907B8) = 1;
            Explosion_Damage(DS32(0x9060C) << 4, DS32(0x90610) << 4, 0, 0, g_GunPower * 0x14 + 10, g_GunPower * 0x14 + 10);
        }
    }
    if (g_EnemyAirCount > 0)
        for (g_LoopI = 0; g_LoopI < g_EnemyAirCount; g_LoopI++) {
            int j = g_LoopI;
            if (BoxOverlap(g_FlameX[g_ProjSprite], g_FlameY[g_ProjSprite], g_AirX[j], g_AirY[j], 0x20, 0x20)) {
                g_AirHp[j] -= Rand(4);
                if (g_AirHp[j] < 0) { g_AirDmg[j] -= g_AirHp[j]; g_AirHp[j] = 0; }
                if (g_AirDmg[j] > 2 && g_AirBurn[j] == 0) { g_AirBurn[j] = 1; g_AirWreckX[j] = g_AirX[j]; g_AirWreckY[j] = g_AirY[j]; }
                Explosion_Damage(g_AirX[j], g_AirY[j], 0, 0, 10, 10);
            }
        }
    g_FlameActive++;
    if (g_FireFail == 0 || g_FlameActive == 0xb) g_FlameActive = 0;
}

/* ====================================================================== projectiles (weapons.md §5) */

/* 0x413a0 Projectile_HitGround */
static void Projectile_HitGround(void)
{
    int i = g_LoopI;
    if (g_CurY > 0x3d4 && g_CurFlags == 2) {    /* penetrator */
        Explosion_Damage(g_CurX, min_i(g_CurY + 0x20, 0x3ef), g_CurVX, 0, g_ProjBlastA[i] * 5, g_ProjBlastB[i] * 5);
        g_CurX = -999;
    }
    if (g_CurX <= -999 || g_CurFlags == 2) return;
    if (g_CurTileAttr == 0x82 && g_CurKind == 3 && g_CurVY != 0) {       /* skip bomb */
        int t = g_CurVY - Rand(1);
        int v = (t < 0) ? 0 : g_CurVY - Rand(1);
        g_CurVY = -v;
        g_CurY = min_i(g_CurY, 99);                                       /* Q9 */
    } else if (g_CurKind == 10 && DS32(0x902E0) < 10) {                   /* commando lands */
        int n = DS32(0x902E0);
        DS32A(0x8DDC0)[n] = g_CurX;
        DS32A(0x8DD98)[n] = g_CurY;
        DS32A(0x8DD20)[n] = -2;
        DS32A(0x8DD70)[n] = 0;
        DS32(0x902E0)++;
        g_ProjStepX = 0;
        g_CurX = -999;
    } else {
        g_ExplSfxHold = 0;
        Explosion_Damage(g_CurX + 0x10, g_CurY + 0x10, g_CurVX, g_CurVY, g_ProjBlastA[i], g_ProjBlastB[i]);
        g_CurX = -999;
        g_ProjStepX = 0;
        g_ProjStepY = 0;
        Map_TriggerColumnAhead();
    }
}

/* 0x42509 Projectile_AimMissionTarget (k3 5/11/12) */
static void Projectile_AimMissionTarget(void)
{
    if (g_MP_TargetX0 != 0 && g_MP_TargetX1 != 0 && g_TargetMarkX < 1 && g_ProjTargetX[g_LoopI] == 0 && g_MP_TargetX0 < 5000) {
        g_ProjTargetX[g_LoopI] = mtcr();
        g_ProjTargetY[g_LoopI] = g_TargetMarkY;
    }
}

/* 0x42d0e Projectile_AimMarker (k3 2) */
static void Projectile_AimMarker(void)
{
    if (g_MP_TargetMarker != 0 && g_MP_TargetX0 != 0 && (int)((u32)g_MP_TargetX0 + g_Never8c0 * -8) <= g_CurX
        && g_CurX <= (int)(g_Never8c0 * 8 + (u32)g_MP_TargetX1) && g_MP_TargetX0 < 5000)
        g_AimX = mtcr();
}

/* shared steering into the heading table (Steer, SeaSkim) */
static void step_frame(void)
{
    g_AimFrame = HEADING(g_AimDirX, g_AimDirY);
    if (g_CurFrame < 3) g_CurFrame = 8;
    if (g_CurFrame == 3) g_CurFrame = 4;
    if (g_AimFrame == 0) g_AimFrame = 8;
    if (g_CurFrame < g_AimFrame) g_CurFrame++;
    if (g_AimFrame < g_CurFrame) g_CurFrame--;
    g_CurFrame &= 7;
}

/* 0x42ec9 Projectile_Steer */
static void Projectile_Steer(void)
{
    int i = g_LoopI;
    if (g_CurX <= -999 || g_ProjLife[i] <= 0) return;
    if (g_ProjTargetX[i] == 0) { g_ProjTargetX[i] = g_TargetMarkX - 8; g_ProjTargetY[i] = g_TargetMarkY; }
    else { g_AimX = g_ProjTargetX[i]; g_AimY = g_ProjTargetY[i]; }
    if (g_TargetMarkX == 0) { g_AimX = g_CurX; g_AimY = g_CurY; }
    g_AimLocked = 0;
    g_CurVX = Clamp(g_AimX - g_CurX, -g_CurSpeed, g_CurSpeed);
    g_CurVY = Clamp(g_AimY - g_CurY, -g_CurSpeed, g_CurSpeed);
    if (g_CurX < g_AimX + 0x50 && g_AimX - 0x50 < g_CurX && g_AimX > 0) {
        g_AimDirX = Sign(g_AimX - g_CurX);
        g_AimDirY = Sign(g_AimY - g_CurY) < 0 ? 0 : Sign(g_AimY - g_CurY);
        if (g_CurDetonate == 2 && g_CurVY > 4) g_CurFlight = 4;
        if (g_CurX < g_AimX + 0x14 && g_AimX - 0x14 < g_CurX && g_AimX > 0 && g_TargetMarkX > 0) { g_AimDirX = 0; g_AimDirY = 1; }
        g_AimLocked = 1;
    } else if (g_AimX > 0) {
        g_AimDirX = Sign(g_AimX - g_CurX);
        g_AimDirY = 0;
        g_CurVY = 0;
    }
    if (g_TargetMarkX == 0) {
        g_AimFrame = g_CurFrame;
        g_CurVX = Sign(g_Dir16X[g_CurFrame]) * g_CurSpeed;
        g_CurVY = Sign(g_Dir16Y[g_AimFrame]) * g_CurSpeed;
    }
    step_frame();
}

/* LGB steering block shared by 0x425c2 and 0x182cb; inRangeAimX: 0x425c2 tests g_AimX > 0, 0x182cb g_TargetMarkX > 0 */
static void lgb_steer(int useAimX)
{
    /* ENH: view: kept (target / aim windows, world) */
    if (g_MP_TargetX0 != 0 && (int)(g_MP_TargetX0 - 0x140) <= g_CurX && g_CurX <= (int)(g_MP_TargetX1 + 0x140) && g_MP_TargetX0 < 5000)
        g_AimX = mtcr();
    if (g_CurX < g_AimX + 0x140 && g_AimX - 0x140 < g_CurX && (useAimX ? g_AimX : g_TargetMarkX) > 0 && g_CurX > -999) {
        int s = max_i(g_CurSpeed, 6);
        int d = g_AimX - g_CurX - 8;
        g_CurVX = min_i(s, abs(d)) * Sign(d);
    }
}

/* 0x182cb Projectile_LgbSteer */
static void Projectile_LgbSteer(void)
{
    if (g_FogActive == 0) lgb_steer(0);
}

/* the original's clamp macro of an expression with Rands: re-evaluated at each use (weapons.md §5.8) */
static int reroll_expr(int base, int n) { int a = Rand(n); int b = Rand(n); return base + a - b; }
static int clamp_reroll(int base, int n)
{
    int m = (reroll_expr(base, n) < 0x11) ? reroll_expr(base, n) : 0x10;
    if (m < -0x10) return -0x10;
    return (reroll_expr(base, n) < 0x11) ? reroll_expr(base, n) : 0x10;
}

/* 0x425c2 Projectile_LgbCluster (k3 4/6/11) */
static void Projectile_LgbCluster(void)
{
    if (g_FogActive == 0 && g_CurFlight == 6) lgb_steer(1);       /* LGB: daytime only */
    if (g_CurFlight != 6 && g_CurFlight != 3 && g_CurVY > 4 && g_ProjCount < 0x15) {   /* cluster opens */
        g_ExplWater = g_CurFlags - 3;                                /* 0x8ff44: submunition weapon */
        int sw = g_ExplWater;
        for (g_ProjSprite = 0; g_ProjSprite < 6; g_ProjSprite++) {
            int n = g_ProjCount;
            g_ProjBlastA[n] = g_WeaponBlastA[sw];
            g_ProjBlastB[n] = g_WeaponBlastB[sw];
            g_ProjLife[n] = g_WeaponThrust[sw];
            int a = Rand(0x10);
            int t = g_CurX - a;
            g_ProjX[n] = t + Rand(0x10);
            a = Rand(0x10);
            t = g_CurY - a;
            g_ProjY[n] = t + Rand(0x10);
            g_ProjFrame[n] = 0;
            (void)clamp_reroll(g_CurVX, 8);                          /* BUG: `vy[n] == vx`, vx never set (Q10) */
            g_ProjKind[n] = K(sw, 0);
            g_ProjArm[n] = K(sw, 1);
            g_ProjMotor[n] = K(sw, 2);
            g_ProjFlight[n] = K(sw, 3);
            g_ProjDetonate[n] = K(sw, 4);
            g_ProjFlags[n] = K(sw, 5);
            g_ProjVY[n] = clamp_reroll(g_CurVY, 4);
            g_ProjCount = min_i(g_ProjCount + 1, 0x14);              /* Q11 */
        }
        g_CurKind = K(sw, 0);
        g_CurArm = K(sw, 1);
        g_CurMotor = K(sw, 2);
        g_CurFlight = K(sw, 3);
        g_CurDetonate = K(sw, 4);
        g_CurFlags = K(sw, 5);
        g_ProjBlastA[g_LoopI] = g_WeaponBlastA[sw];
        g_ProjBlastB[g_LoopI] = g_WeaponBlastB[sw];
        g_CurFrame = 0;
    }
}

/* 0x184cf Projectile_Alarm (k3 3) */
static void Projectile_Alarm(void)
{
    int i = g_LoopI;
    if (g_MP_TargetMarker != 0 && g_MP_TargetX0 != 0 && (int)((u32)g_MP_TargetX0 + g_Never8c0 * -8) <= g_CurX
        && g_CurX <= (int)(g_Never8c0 * 8 + (u32)g_MP_TargetX1) && g_MP_TargetX0 < 5000)
        g_AimX = mtcr();
    if (g_ProjTargetX[i] == 0) { g_ProjTargetX[i] = g_TargetMarkX; g_ProjTargetY[i] = g_TargetMarkY; }
    else { g_AimX = g_ProjTargetX[i]; g_AimY = g_ProjTargetY[i]; }
    if (g_ProjLife[i] < 1) {
        if (g_AimX == 0) { g_CurFrame = 6; g_CurVX = 0; g_CurVY = 1; }
        else { Projectile_LgbSteer(); g_CurFrame = Sign(g_CurVX) + 6; }
    } else {
        g_CurVX = 0;
        g_CurVY = -g_CurSpeed;
        g_CurFrame = 2;
    }
}

/* 0x42dc6 Projectile_Climb (k3 9, no shipped weapon) */
static void Projectile_Climb(void)
{
    if (g_CurFrame < 4) {
        int s = (0x20 - g_CurArm < 1) ? 1 : 0x20 - g_CurArm;
        g_CurSpeed = s;
        g_CurVX = 0;
        g_CurVY = -s;
        g_CurArm = (g_CurArm - 1 < 0) ? 0 : g_CurArm - 1;
        if (s > 0x1f) { g_CurFrame++; g_CurSpeed = 0x20; }
    } else {
        g_TargetMarkX = Rand(g_BaseEndX - g_BaseStartX) + g_BaseStartX;
        g_TargetMarkY = 1999;
        g_ProjTargetX[g_LoopI] = g_TargetMarkX;
        g_ProjTargetY[g_LoopI] = g_TargetMarkY;
    }
}

/* 0x4341a Projectile_HomeAir (k3 0, 12) */
static void Projectile_HomeAir(void)
{
    int i = g_LoopI;
    if (g_EnemyAirCount == 0 && g_BigActive == 0 && g_EnemyGroundCount == 0) return;
    if (g_EnemyAirCount != 0)
        for (g_ProjLoopJ = 0; g_ProjLoopJ < g_EnemyAirCount; g_ProjLoopJ++)
            if (BoxOverlap(g_CurX, g_CurY, g_AirX[g_ProjLoopJ], g_AirY[g_ProjLoopJ], 0x20, 0x20)) {
                Explosion_Damage(g_AirX[g_ProjLoopJ], g_AirY[g_ProjLoopJ], 0, 0, g_ProjBlastA[i], g_ProjBlastB[i]);
                g_CurX = -999;
            }
    if (g_BigActive != 0 && g_BigX - 0x40 < g_CurX && g_CurX < g_BigX + 0x40 && g_BigY - 0x20 < g_CurY && g_CurY < g_BigY + 0x20) {
        Explosion_Damage(g_CurX, g_CurY, 0, 0, g_ProjBlastA[i], g_ProjBlastB[i]);
        Particle_Spawn(g_CurX << 8, g_CurY << 8, 0, 0, 0, 0x20, 0);
        g_CurX = -999;
    }
    if (g_EnemyGroundCount != 0)
        for (g_ProjLoopJ = 0; g_ProjLoopJ < g_EnemyGroundCount; g_ProjLoopJ++)
            if (BoxOverlap(g_CurX, g_CurY, g_GndX[g_ProjLoopJ], g_GndY[g_ProjLoopJ], 0x20, 0x20)) {
                Explosion_Damage(g_GndX[g_ProjLoopJ], g_GndY[g_ProjLoopJ], 0, 0, g_ProjBlastA[i], g_ProjBlastB[i]);
                g_CurX = -999;
            }
    if (g_CurX > -999 && g_ProjKind[i] != 0) {
        g_ProjLoopJ = g_ProjLock[i] - 1;
        int t = g_ProjLoopJ;
        if (t == 1) {
            g_ProjLock[i] = (g_LockTarget < 0) ? DS32(0x90764) * 4 + 1 : g_LockTarget + 1;
        } else if (g_AirX[t] < 1 && t < 2) {                          /* target gone (t may be -1) */
            Explosion_Damage(g_CurX, g_CurY, 0, 0, g_ProjBlastA[i], g_ProjBlastB[i]);
            g_CurX = -999;
        } else {
            if (t < 3) { g_AimDirX = Sign(g_BigX - g_CurX); g_AimDirY = Sign(g_BigY - g_CurY); }
            else { g_AimDirX = Sign(DS32A(0x8D9CC)[t] - g_CurX); g_AimDirY = Sign(g_AirY[t] - g_CurY); }   /* sic (Q12) */
            g_AimFrame = HEADING(g_AimDirX, g_AimDirY);
            int d = g_AimFrame;
            if ((d < g_CurFrame && g_CurFrame - 4 < d) || (g_CurFrame < d && g_CurFrame + 4 < d)) g_CurFrame--;
            if ((g_CurFrame < d && d < g_CurFrame + 4) || (d < g_CurFrame && d < g_CurFrame - 4)) g_CurFrame++;
            g_CurFrame = (g_CurFrame + 8) % 8;
            g_CurVX = Sign(W_Dir16X[g_CurFrame * 2]) * g_CurSpeed;
            g_CurVY = Sign(W_Dir16Y[g_CurFrame * 2]) * g_CurSpeed;
        }
    }
}

/* 0x438f2 Projectile_SeaSkim (k3 7) */
static void Projectile_SeaSkim(void)
{
    int i = g_LoopI;
    if (g_MP_TargetX0 != 0 && g_MP_TargetFlag == 1 && g_MP_TargetX0 < 5000) g_AimX = mtcr();
    if (g_CurX <= -999 || g_ProjLife[i] <= 0) return;
    if (g_ProjTargetX[i] == 0) { g_ProjTargetX[i] = g_TargetMarkX; g_ProjTargetY[i] = 900; }
    else { g_AimX = g_ProjTargetX[i]; g_AimY = 900; }
    g_AimLocked = 0;
    if (g_CurX < g_AimX + 0x50 && g_AimX - 0x50 < g_CurX && g_AimX > 0) {
        g_AimDirX = Sign(g_AimX - g_CurX);
        g_AimDirY = (g_CurX < 0x3ac);                                 /* sic: x (Q17) */
        if (g_CurDetonate == 2 && g_CurVY > 4) g_CurFlight = 4;
        if (g_CurX < g_AimX + 0x14 && g_AimX - 0x14 < g_CurX && g_AimX > 0) { g_AimDirX = 0; g_AimDirY = 1; }
        g_AimLocked = 1;
        g_AimY = 0x3e2;
    } else if (g_AimX < 1) {
        g_AimDirX = Rand(2) - 1;
        g_AimDirY = (g_CurY < 0x3ac);
    } else {
        g_AimDirX = Sign(g_AimX - g_CurX);
        g_AimDirY = (g_CurY < 0x3ac);
    }
    step_frame();
    if (g_AimLocked == 0) {
        g_CurVX = Sign(W_Dir16X[g_CurFrame * 2]) * g_CurSpeed;
        g_CurVY = Sign(W_Dir16Y[g_CurFrame * 2]) * g_CurSpeed;
    } else {
        g_CurVX = min_i(g_CurSpeed, abs(g_AimX - g_CurX)) * Sign(g_AimX - g_CurX);
        g_CurVY = min_i(g_CurSpeed, abs(g_AimY - g_CurY)) * Sign(g_AimY - g_CurY);
    }
}

/* terrain-following altitude of the cruise missile (loop on the global 0x9098c) */
static void cruise_terrain(void)
{
    g_AimLocked = 0x3b;
    for (g_ProjSprite = 2; g_ProjSprite < 6; g_ProjSprite++) {
        int h = Clamp((int)(Byte_Get(g_MapVal, g_ProjSprite * Sign(g_AimX - g_CurX) + Div16(g_CurX) + 0x400) & 0xff) - 4, 0, 0x3f);
        if (h < g_AimLocked) g_AimLocked = h;
    }
}

/* 0x43d05 Projectile_Cruise (k3 13/14) */
static void Projectile_Cruise(void)
{
    int i = g_LoopI;
    if (g_MP_TargetX0 == 0 || g_MP_TargetX0 > 1999) {
        if (g_CurFlight == 0xe && g_MP_EnemyBaseCol != 0) {
            g_AimX = (int)g_MP_EnemyBaseCol * 0x10 - 0x40;
            cruise_terrain();
            g_AimY = g_AimLocked << 4;
            if (abs(g_AimX - g_CurX) < 0x3c) g_AimY = 0x3e2;
        } else {
            g_AimX = 8000;
            g_AimY = 0;
        }
    } else {
        g_AimX = mtc();
        cruise_terrain();
        g_AimY = g_AimLocked << 4;
        if (abs(g_AimX - g_CurX) < 0x3c) g_TargetMarkY = 0x3e2;     /* sic (Q18) */
    }
    if (g_CurX <= -999 || g_ProjLife[i] <= 0) return;
    if (g_CurY < g_AimX - 0x80) g_ProjLife[i]++;                       /* sic (Q13) */
    g_AimLocked = 0;
    g_ProjTargetX[i] = g_AimX;
    g_ProjTargetY[i] = g_AimY;
    if (g_CurX < g_AimX + 0x78 && g_AimX - 0x78 < g_CurX && g_TargetMarkX > 0) {
        g_AimLocked = 1;
        if (g_CurDetonate == 2 && g_CurVY > 4) g_CurFlight = 4;
        if (g_CurX < g_AimX + 0x14 && g_AimX - 0x14 < g_CurX && g_AimX > 0) g_AimY = 0x3e2;
    }
    g_CurVX = min_i(g_CurSpeed / 2, abs(g_AimX - g_CurX)) * Sign(g_AimX - g_CurX);
    g_CurVY = min_i(g_AimLocked * 0x12 + 4, abs(g_AimY - g_CurY)) * Sign(g_AimY - g_CurY);
    int c = Clamp(g_CurVY / 16, -2, 2);
    g_CurFrame = DS32(0x8D8C8 + c * 4 + (g_CurVX > 0) * 0x14) - g_ProjSpriteBase[g_CurKind];
}

/* 0x433c1 Projectile_Parachute (k0 7) */
static void Projectile_Parachute(void)
{
    if (g_CurVY > 8) g_CurFrame = 1;
    if (g_CurFrame == 1) g_CurVY = min_i(g_CurVY, 4);
}

/* 0x43207 Projectile_Commando (k0 10) */
static void Projectile_Commando(void)
{
    int i = g_LoopI;
    if (g_CurFrame < 10) {
        g_ProjVX[i] = 0;
        g_ProjVY[i] = 0;
        g_LaunchX = DS32(0x90520) + 0x34 + g_CurFrame * 2;            /* Q19 */
        g_LaunchY = DS32(0x904C0) + 9;
        g_ProjBlastA[i] = g_CurFrame % 3 + 0x1da;
        g_CurFrame++;
    } else {
        if (g_CurFrame < 0x11 && g_CurY > 0x80) g_CurFrame++;
        g_CurVY = g_ProjVY[i];
        g_ProjVY[i] = min_i(g_ProjVY[i] + 1, 0x10);
        if (g_CurFrame == 0x11) g_CurVY = 4;
        g_ProjBlastA[i] = DS32A(0x90CD0)[g_CurFrame];
        if (g_ProjBlastA[i] == 0x35) g_ProjBlastA[i] += DS32A(0x90CE4)[g_PingPong];
        if (g_CurFrame < 0xe) {
            g_ProjBlastA[i] = DS32(0x9013C) / 2 + 0x18e;
            DS32(0x9013C) = Wrap(DS32(0x9013C) + 1, 0, 7);
        }
    }
}

/* 0x415e4 Projectiles_Update (GF step 43) */
void Projectiles_Update(void)
{
    if (g_PingPongStep == 0) { g_PingPongStep = 1; g_PingPong = 0; }
    g_PingPong += g_PingPongStep;
    if (g_PingPong == 0 || g_PingPong == 4) g_PingPongStep = -g_PingPongStep;
    g_LoopI = g_ProjCount - 1;
    do {
        int i = g_LoopI;
        /* PORT: the original would index slot -1 here when called with no projectile (never happens) */
        g_CurX = g_ProjX[i];
        g_CurY = g_ProjY[i];
        g_CurVX = g_ProjVX[i];
        g_CurVY = g_ProjVY[i];
        g_CurFrame = g_ProjFrame[i];
        g_CurArm = g_ProjArm[i];
        g_CurKind = g_ProjKind[i];
        g_CurFlight = g_ProjFlight[i];
        g_CurMotor = g_ProjMotor[i];
        g_CurFlags = g_ProjFlags[i];
        g_CurDetonate = g_ProjDetonate[i];
        if (IsOnScreen(g_CamX, g_CamY, g_CurX, g_CurY)) {           /* draw (old position) */
            if (g_CurKind == 10) {
                if (g_ProjBlastA[i] == 0) g_ProjBlastA[i] = 0x18e;
                Sprite_Queue(g_CurX - g_CamX, g_CurY - g_CamY, g_ProjBlastA[i]);
            } else if (g_ProjBlastA[i] == -0x32) {
                Sprite_Queue(g_CurX - g_CamX, g_CurY - g_CamY, Rand(1) + 0x204);   /* water bomb */
            } else {
                g_ProjSprite = g_ProjSpriteBase[g_CurKind] + g_CurFrame;
                if (g_ProjSprite < 0x32) g_ProjSprite = 0x70;
                Sprite_Queue(g_CurX - g_CamX, g_CurY - g_CamY, g_ProjSprite);
            }
        }
        g_CurSpeed = 0;
        if (g_CurArm == 0 || g_CurFlight == 9) {
            if (g_CurMotor == 1) {
                if (IsOnScreen(g_CamX, g_CamY, g_CurX, g_CurY) && DS32(0x90AF4) == 1)
                    Rand(2);                       /* 0x900fc = Smoke_Stub(..., 0x900fc, Rand(2)): returns 0x900fc (Q20) */
                int m = max_i(abs(g_CurVX), abs(g_CurVY));
                g_CurSpeed = (m * 2 < 0x31) ? m * 2 : 0x30;
            }
            if (g_CurMotor == 2) g_CurSpeed = 0x20;
            if (g_CurFlight == 0) Projectile_HomeAir();
            if (g_CurFlight == 2) { Projectile_AimMarker(); if (g_MP_TargetMarker != 0) Projectile_Steer(); }
            if (g_CurFlight == 3) Projectile_Alarm();
            if (g_CurFlight == 4 || g_CurFlight == 6) Projectile_LgbCluster();
            if (g_CurFlight == 5) { Projectile_AimMissionTarget(); Projectile_Steer(); }
            if (g_CurFlight == 7) Projectile_SeaSkim();
            if (g_CurFlight == 8) {                                    /* brake and drop */
                g_CurFrame = Sign(g_CurVX) + 6;
                int t = abs(g_CurVX) < 2 ? abs(g_CurVX) : 2;
                g_CurVX -= Sign(g_CurVX) * t;
                g_CurVY = g_CurSpeed;
            }
            if (g_CurFlight == 9) { Projectile_Climb(); if (g_CurFrame > 3) Projectile_Steer(); }
            if (g_CurFlight == 10) {                                   /* proximity fuse */
                int row = Clamp(Div16(g_CurY) + 3, 0xc, 0x3f);
                g_CurTileAttr = Map_GetTileAttr(Clamp(Div16(g_CurX), 0, g_MapWidth - 1), row, 0);
                if (g_CurTileAttr > 0x7e) { g_CurDetonate = 1; g_ProjLife[g_LoopI] = 0; g_CurArm = 0; }
            }
            if (g_CurFlight == 0xb) { Projectile_AimMissionTarget(); Projectile_Steer(); Projectile_LgbCluster(); }
            if (g_CurFlight == 0xc) {
                if (g_LockTarget < 0 || (g_EnemyGroundCount < 1 && g_EnemyAirCount < 1 && g_BigActive < 1)) {
                    Projectile_AimMissionTarget();
                    Projectile_Steer();
                } else {
                    Projectile_HomeAir();
                }
            }
            if (g_CurFlight == 0xd || g_CurFlight == 0xe) Projectile_Cruise();
        } else {
            g_CurArm--;
        }
        if (g_CurKind == 0 || g_CurKind == 5) g_CurFrame = g_CurVY / 8 + (g_CurVX > 0) * 3;
        if (g_CurKind == 3) { g_CurFrame++; if (g_CurFrame > 3 || g_CurFrame == 0) g_CurFrame = 0; }
        if (g_CurKind == 7) Projectile_Parachute();
        if (g_CurKind == 10) Projectile_Commando();
        g_ProjStepX = g_CurVX;
        g_ProjStepY = g_CurVY;
        g_CurTileAttr = 0;
        if (g_CurX > -999) {
            while (g_ProjStepX != 0 || g_ProjStepY != 0) {
                g_CurX += Clamp(g_ProjStepX, -0x10, 0x10);
                g_CurY += Clamp(g_ProjStepY, -0x10, 0x10);
                g_CurTileAttr = 0;
                int col = Clamp(Div16(g_CurX + 0x10), 0, g_MapWidth - 1);
                if ((int)(Byte_Get(g_MapVal, col + 0x400) & 0xff) <= Div16(g_CurY + 0x10)) {
                    g_CurTileAttr = Map_GetTileAttr(Clamp(Div16(g_CurX + 0x10), 0, g_MapWidth - 1), Clamp(Div16(g_CurY + 0x10), 0, 0x3f), 0);
                    if (g_CurTileAttr > 0x7e) Projectile_HitGround();
                }
                g_ProjStepX -= Sign(g_ProjStepX) * min_i(abs(g_ProjStepX), 0x10);
                g_ProjStepY -= Sign(g_ProjStepY) * min_i(abs(g_ProjStepY), 0x10);
            }
        }
        i = g_LoopI;
        if (g_CurArm < 1) g_ProjLife[i]--;
        if (g_ProjLife[i] < 1 || (g_CurArm > 0 && g_CurFlight != 9)) {        /* gravity */
            g_CurVY = min_i(g_CurVY + 1, 0x10);
            if (g_CurArm < 1) g_ProjLife[i] = 0;
        }
        if (g_CurDetonate == 1 && g_ProjLife[i] < 1 && g_CurArm < 1) {        /* timed fuse */
            g_ExplSfxHold = 0;
            Explosion_Damage(g_CurX, g_CurY, g_CurVX, g_CurVY, g_ProjBlastA[i], g_ProjBlastB[i]);
            g_CurX = -999;
        }
        if (g_CurY > 0x3d4 && g_CurTileAttr > 0x7e && g_CurFlags == 2) {      /* penetrator */
            Explosion_Damage(g_CurX, min_i(g_CurY + 0x20, 0x3ef), g_CurVX, 0, g_ProjBlastA[i] * 5, g_ProjBlastB[i] * 5);
            g_CurX = -999;
        }
        if (g_CurTileAttr > 0x7e && g_CurX > -999) {
            if (g_CurTileAttr == 0x82 && g_CurKind == 3 && g_CurVY != 0) {    /* skip bomb on water */
                if (g_CurVY > 0) {
                    int t = g_CurVY - Rand(1);
                    int v = (t < 0) ? 0 : g_CurVY - Rand(1);
                    g_CurVY = -v;
                    g_CurY = min_i(g_CurY, 999);
                }
            } else {
                g_ExplSfxHold = 0;
                Explosion_Damage(g_CurX, g_CurY, g_CurVX, g_CurVY, g_ProjBlastA[i], g_ProjBlastB[i]);
                g_CurX = -999;
                g_CurY = -999;
            }
        }
        if (g_CurY > 1000) {
            g_ExplSfxHold = 0;
            Explosion_Damage(g_CurX, g_CurY, g_CurVX, 0, g_ProjBlastA[i], g_ProjBlastB[i]);
            g_CurX = -999;
        }
        g_ProjFlags[i] = g_CurFlags;
        g_ProjDetonate[i] = g_CurDetonate;
        g_ProjKind[i] = g_CurKind;
        g_ProjMotor[i] = g_CurMotor;
        g_ProjArm[i] = g_CurArm;
        g_ProjFlight[i] = g_CurFlight;
        g_ProjFrame[i] = g_CurFrame;
        g_ProjX[i] = g_CurX;
        g_ProjY[i] = g_CurY;
        g_ProjVX[i] = g_CurVX;
        g_ProjVY[i] = g_CurVY;
        if (g_ProjX[i] == -999) {                  /* remove: swap with the last slot */
            int l = g_ProjCount - 1;
            SwapInt(&g_ProjX[i], &g_ProjX[l]);
            SwapInt(&g_ProjY[i], &g_ProjY[l]);
            SwapInt(&g_ProjVX[i], &g_ProjVX[l]);
            SwapInt(&g_ProjVY[i], &g_ProjVY[l]);
            SwapInt(&g_ProjBlastA[i], &g_ProjBlastA[l]);
            SwapInt(&g_ProjBlastB[i], &g_ProjBlastB[l]);
            SwapInt(&g_ProjLife[i], &g_ProjLife[l]);
            SwapInt(&g_ProjFrame[i], &g_ProjFrame[l]);
            SwapInt(&g_ProjKind[i], &g_ProjKind[l]);
            SwapInt(&g_ProjFlags[i], &g_ProjFlags[l]);
            SwapInt(&g_ProjTargetX[i], &g_ProjTargetX[l]);
            SwapInt(&g_ProjTargetY[i], &g_ProjTargetY[l]);
            SwapInt(&g_ProjArm[i], &g_ProjArm[l]);
            SwapInt(&g_ProjMotor[i], &g_ProjMotor[l]);
            SwapInt(&g_ProjFlight[i], &g_ProjFlags[l]);                       /* BUG: k3 <-> k5 (Q7) */
            SwapInt(&g_ProjDetonate[i], &g_ProjDetonate[l]);
            g_ProjCount--;
        }
        g_LoopI--;
    } while (g_LoopI > -1);
}

/* ====================================================================== explosions (weapons.md §8) */

/* 0x3b261 Explosion_Nuke: empty (Q20) */
static void Explosion_Nuke(void) {}

/* 0x3adf9 Explosion_Particles (water) */
static void Explosion_Particles(void)
{
    if (g_ExplPower < 0x1e) {
        Particle_Spawn(g_ExplX << 8, (g_ExplY - 0x10) * 0x100, 0, g_ExplVY << 7, 0, 0x10, 0x10);
    } else {
        int y = Div16(g_ExplY) * 0x10 + 1;
        Particle_Spawn(g_ExplX << 8, (y < 0x3e2 ? y : 0x3e1) << 8, 0, 0, 0, 0x40, 0xc);
    }
    DS32(0x90114) = g_ExplX;
    if (Rand(0x14) == 1) Particle_Spawn(g_ExplX << 8, (g_ExplY - 0x10) * 0x100, 0x200, -0x200, 0x40, 0x20, 0x13);
    if (Rand(100) == 1) Particle_Spawn(g_ExplX << 8, (g_ExplY - 0x10) * 0x100, 0, 0, 0, 0x20, 0x14);
}

static int debris_vx(void)
{
    int base = g_ExplDX8 + g_ExplVX / 4;
    base += Rand(4);
    base -= Rand(4);
    return Clamp(base, -0x10, 0x10);
}

/* 0x3af32 Explosion_DebrisSmall (power < 30) */
static void Explosion_DebrisSmall(void)
{
    int vx = debris_vx();
    Particle_Spawn(g_ExplX << 8, (g_ExplY - 0x10) * 0x100, vx << 8, g_ExplVY << 8, 0x20, 0x10, 2);
}

/* 0x3afc0 Explosion_DebrisGround */
static void Explosion_DebrisGround(void)
{
    int vy0 = (g_ExplVY - g_ExplSpread) * 0x55;
    int vx = debris_vx();
    Particle_Spawn(g_ExplX << 8, (g_ExplY - 0x10) * 0x100, vx * 0x55, vy0, 0x80, 0x10, 0);
    g_DamageHits = Rand(1);
    for (g_LoopK = 0; g_LoopK < g_DamageHits; g_LoopK++) {
        DS32(0x901C0) = Rand(4);
        g_ExplMode = DS32A(0x90CC4)[DS32(0x901C0)];
        g_GunSide = 0x100 / (DS32A(0x90C84)[DS32(0x901C0)] + g_LoopK + 3);   /* PORT: same divide as the exe */
        int anim = g_ExplMode;
        int a5 = g_GunSide / 4;
        int vy = (g_ExplVY - Rand(2)) * g_GunSide;
        int vx2 = debris_vx() * g_GunSide;
        int y = (g_ExplY - 0x10 - Rand(0x10)) << 8;
        Particle_Spawn(g_ExplX << 8, y, vx2, vy, a5, 0x10, anim);
    }
}

/* 0x3b18d Explosion_DebrisSolid (building tile) */
static void Explosion_DebrisSolid(void)
{
    Particle_Spawn(g_ExplX << 8, (g_ExplY - 0x18) * 0x100, 0, 0, 0, 0x10, 3);
    g_DamageHits = Rand(2);
    for (g_LoopK = 0; g_LoopK <= g_DamageHits; g_LoopK++) {
        g_ExplMode = Rand(6);
        Particle_Spawn(g_ExplX << 8, (g_ExplY - 0x20) * 0x100, DS32A(0x8DED8)[g_ExplMode] << 9,
                       DS32A(0x8DF00)[g_ExplMode] << 9, 0, 0x10, g_ExplMode + 4);
    }
}

/* 0x3a99f Explosion_Terrain */
static void Explosion_Terrain(void)
{
    int centre = (g_ExplX == g_ExplX0);
    int r = Rand((g_ExplPower > 500) * 2 + 3);
    int solid = 0;
    if (Sign(r / 3) * centre != 0) {
        int row = Div16(g_ExplY) < 1 ? 1 : Div16(g_ExplY);
        if (Map_GetTileAttr(Div16(g_ExplX), row, 3) != 0) solid = 1;
    }
    g_ExplSolid = solid;
    int small = (g_ExplPower < 0x1e);
    if (small == 1) g_ExplSolid = 0;
    g_ExplMode = small + g_ExplSolid * 2;
    g_ExplDX8 = (g_ExplX - g_ExplX0) / 8;
    g_ExplSpread = abs(g_ExplDX8) + 8;
    int row = Div16(g_ExplY) < 1 ? 1 : Div16(g_ExplY);
    g_ExplWater = (Map_GetTileAttr(Div16(g_ExplX), row, 0) == 0x82);
    if (g_ExplWater != 0) g_ExplMode = 3;
    if (g_ExplVY > -1) {                         /* sticks once negative (Q15) */
#define KK (g_ExplPower / ((g_ExplI > 4) * -3 + 8))
        int A = abs(g_ExplVX) / 4;
        int m = (A - Rand(KK) <= 0) ? A - Rand(KK) : 0;
        if (m < -0x10) g_ExplVY = -0x10;
        else g_ExplVY = (A - Rand(KK) <= 0) ? A - Rand(KK) : 0;
#undef KK
    }
    switch (g_ExplMode) {
    case 0: Explosion_DebrisGround(); break;
    case 1: Explosion_DebrisSmall(); break;
    case 2: Explosion_DebrisSolid(); break;
    case 3: Explosion_Particles(); break;
    default: break;
    }
    DS32(0x907B8) = 0;
}

/* a structure at tile (c, r) destroyed by the blast or by losing its support (Explosion_Damage (5)-(7)) */
static void structure_hit(s32 *c, s32 r, int pts)
{
    int R = g_ExplCraters;
    if (*c > 0 && ((*c * 0x10 - R * 0xc <= g_ExplCX && g_ExplCX <= R * 0xc + *c * 0x10 && r * 0x10 - R * 0x10 <= g_ExplCY
                    && g_ExplCY <= R * 0x10 + r * 0x10 && g_ExplPower > 0xf)
                   || Map_GetTileAttr(*c, r + 1, 0) < 0x7f)) {
        if (g_SecExpCount < 0x20) {              /* PORT: 32-entry arrays, bounded as in Map_CraterAt */
            g_SecExpX[g_SecExpCount] = *c << 4;
            g_SecExpY[g_SecExpCount] = r << 4;
            g_SecExpDmg[g_SecExpCount] = 500;
            g_SecExpCount++;
        }
        *c = 0;
        g_Score[g_AeroPlayer] += pts;
    }
}

/* 0x39b69 Explosion_Damage(x, y, vx, vy, power, powerB): vx / vy unused */
void Explosion_Damage(int x, int y, int vx, int vy, int a5, int a6)
{
    (void)vx; (void)vy;
    g_ExplX = x;
    g_ExplY = y;
    g_ExplPower = a5;
    g_ExplPowerB = a6;
    if (a5 == -0x28) { Explosion_Nuke(); return; }
    if (a5 == -0x14) { g_B52TargetX = x; g_B52TargetY = y; }
    if (a5 == -0x1e) { g_DropZoneX = x; g_DropZoneY = y; }
    if (g_NightMission > 0 && a6 > 99) g_FlashCounter = min_i((a6 / 1000) * 10 + 1, 0x15);
    g_ExplSfxPower = min_i(a5, 4000);
    if (g_ExplSfxPower > 99) {
        if (g_ExplSfxHold < 5) Sfx_Play(Rand(1) * 0x17 + 4, 8000 - g_ExplSfxPower / 200, 0x20, x);
        if (g_ExplSfxPower > 499 && g_ExplSfxHold < 5) {
            Sfx_Play(6, 12000 - g_ExplSfxPower / 10, 0x20, g_ExplX);
            g_ExplSfxHold = 2;
        }
    }
    g_ExplCraters = min_i(g_ExplPower / 500 + g_ExplPower / 0x96 + 1, 5);
    if (Map_GetTileAttr(Div16(g_ExplX), Div16(g_ExplY), 0) == 0x82) g_ExplCraters = 1;
    g_ExplX0 = g_ExplX;
    g_ExplY0 = g_ExplY;
    g_ExplX = g_ExplX + (g_ExplCraters / 2) * -0x10 + 0x10;
    g_ExplI = 1;
    do {
        g_ExplX = Clamp(g_ExplX + 0x10, 0, g_MapWidth * 0x10 - 1);
        if (g_ExplY < 0) {
            g_ExplY = g_ExplY0 - (Rand(2) * 8 - 0x40);
            if (g_ExplX == g_ExplX0) g_ExplY = g_ExplY0;
        } else {
            g_ExplY = Clamp(g_ExplY0 - (Rand(2) * 8 - 8), 0, 0x3f0);
            if (g_ExplX == g_ExplX0) g_ExplY = g_ExplY0;
            Map_CraterAt(g_ExplX, g_ExplY, g_ExplPower);
            if (g_BaseStartX <= g_ExplX && g_ExplX < g_BaseEndX && g_BaseYOff + 0x3e0 <= g_ExplY) g_BaseHit = 1;
        }
        if (g_CamX - 0x40 < g_ExplX && g_ExplX < g_CamX + 0x160 + VIEW_EXTRA_COLS && g_CamY - 0x40 < g_ExplY
            && g_ExplY < g_CamY + 0x100 + VIEW_EXTRA_ROWS)                 /* ENH: view */
            Explosion_Terrain();
        g_ExplI++;
    } while (g_ExplI < g_ExplCraters);
    g_ExplCX = Clamp(g_ExplX0, 0, g_MapWidth * 0x10 - 1);
    g_ExplCY = min_i(g_ExplY0, 0x3f0);
    int R = g_ExplCraters;
    /* (1) target-zone vehicles (0x900e0 is never set, enemies.md Q6) */
    if (DS32(0x900E0) > 0)
        for (g_ExplI = 0; g_ExplI < DS32(0x900E0); g_ExplI++) {
            int j = g_ExplI;
            if (DS32A(0x8E400)[j] - R * 0xc - 4 < g_ExplCX && g_ExplCX < R * 0xc + DS32A(0x8E400)[j] + 4
                && g_ExplCY - R * 0x10 - 0x10 <= DS32A(0x8E418)[j] && DS32A(0x8E3E8)[j] < 2) {
                DS32A(0x8E3E8)[j] = 4;
                g_Score[g_AeroPlayer] += 100;
            }
        }
    /* (2) enemy aircraft */
    if (g_EnemyAirCount > 0)
        for (g_ExplI = 0; g_ExplI < g_EnemyAirCount; g_ExplI++) {
            int j = g_ExplI;
            if (g_AirX[j] - 0x20 < g_ExplCX && g_ExplCX < g_AirX[j] + 0x20 && g_AirY[j] - 0x20 < g_ExplCY && g_ExplCY < g_AirY[j] + 0x20) {
                DS32(0x8FFCC) = -1;
                int hp = g_AirHp[j];
                g_AirHp[j] = hp - 1 - Rand(g_ExplPower / 10);
                if (g_AirHp[j] < 0) { g_AirDmg[j] -= g_AirHp[j]; g_AirHp[j] = 0; }
                if (g_AirDmg[j] > 2 && g_AirBurn[j] == 0) { g_AirBurn[j] = 1; g_AirWreckX[j] = g_AirX[j]; g_AirWreckY[j] = g_AirY[j]; }
            }
        }
    /* (3) ground gunships */
    if (g_EnemyGroundCount > 0)
        for (g_ExplI = 0; g_ExplI < g_EnemyGroundCount; g_ExplI++) {
            int j = g_ExplI;
            if (g_GndX[j] - 0x20 < g_ExplCX && g_ExplCX < g_GndX[j] + 0x20 && g_GndY[j] - 0x20 < g_ExplCY && g_ExplCY < g_GndY[j] + 0x20) {
                int hp = g_GndHp[j];
                int e = (g_GunRayHit < 1 || PY <= g_GndY[j]) ? 0 : 1;
                g_GndHp[j] = hp - 1 - Rand(g_ExplPower / 10 + e * 2);
                if (g_GndHp[j] < 0) { g_GndDmg[j] -= g_GndHp[j]; g_GndHp[j] = 0; }
            }
        }
    /* (4) big target (Q16: y bound from the x coordinate) */
    if (g_BigActive > 0 && g_BigX - 0x50 < g_ExplCX && g_ExplCX < g_BigX + 0x50 && g_BigX - 0x20 < g_ExplCY && g_ExplCY < g_BigY + 0x20) {
        int hp = g_BigHp - 1;
        g_BigHp = hp - Rand(g_ExplPower / 10);
        if (g_BigHp < 0) g_BigKilled = 1;
    }
    /* (5)-(7) the three MP2 structures */
    structure_hit(&DS32(0x90230), DS32(0x90234), 3000);
    structure_hit(&DS32(0x9063C), DS32(0x90640), 1000);
    structure_hit(&DS32(0x900E8), DS32(0x900EC), 2000);
    /* (8) convoy vehicles */
    if (g_ConvoyCountRT > 0)
        for (g_ExplI = 0; g_ExplI < g_ConvoyCountRT; g_ExplI++) {
            int j = g_ExplI;
            if (DS32A(0x91F74)[j] - R * 0xc < g_ExplCX && g_ExplCX <= R * 0xc + DS32A(0x91F74)[j]
                && g_ExplCY - R * 0x10 <= DS32A(0x91F98)[j] && DS32A(0x91F98)[j] <= R * 0x10 + g_ExplCY && DS32A(0x91F2C)[j] < 4) {
                DS32A(0x91928)[j] -= Rand(g_ExplPower / 300 + min_i(g_ExplPower / 0x32, 6) + 2);
                if (DS32A(0x91928)[j] < 0) {
                    DS32A(0x91F2C)[j] -= DS32A(0x91928)[j];
                    DS32A(0x91928)[j] = 0;
                    if (g_GunRayHit > 0 && g_StrafeCount > 8 && DS32(0x90194) == 0 && Rand(5) == 1) {
                        DS32(0x90194) = 1;
                        Sfx_Play(0x14, 4000, 0x20, PX);
                        g_ExplSfxHold = 0xf;
                    }
                }
            }
        }
}
