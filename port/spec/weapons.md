# Weapons subsystem (JS.EXE: gun `0x32846`, launcher `0x341e9`–`0x35a32`, projectiles `0x413a0`–`0x4424d`, bullets `0x2688f`–`0x26ae7`, flares `0x346d3`/`0x38a61`, explosions `0x39b69`–`0x3b27b`)

Target: `work/JS.bin` (flat image, base 0x10000). Symbols: `port/spec/weapons_symbols.csv`. Companion specs:
**`port/spec/player.md`** (flight model, camera incl. `0x8ff10` follow modes, Player_DamageSystems,
death/landing/ejection, Player_Collide, plane stats from MISC.Z, HUD), the enemies/level specs (enemy
arrays, Map_CraterAt `0x3b777`, ray trace `0x4501e`), `game_flow` (WeaponSelect, Mission_Setup).

Confidence tags: **verified** (read instruction by instruction in the capstone disassembly), **likely**
(read from the Ghidra decompile, cross-checked at the points that matter), **guess**.

**No floating point.** Everything in this module is 32-bit signed integer code. All divisions are C
signed divisions (truncate toward 0; the `sar edx,31 / sub / sar` and `shl edx,n / sbb / sar` idioms are
`x/2` and `x/2^n` with truncation). `>>` is written only where the binary uses a plain `sar`
(arithmetic shift, rounds toward -inf). The only trig tables used are integer tables built at start-up
(§2.3).

**Calling convention.** Stack, args pushed right to left, caller pops. `Rand(n)` (0x10d65) =
`rand() % (n+1)`; `Clamp(v,lo,hi)` (0x10e11); `Sign(v)` (0x13a25) = -1/0/1; `Wrap(v,lo,hi)` (0x1178a);
`abs` (0x49fdf); `BoxOverlap(x1,y1,x2,y2,dx,dy)` (0x1172a); `IsOnScreen(camx,camy,x,y)` (0x10eb0);
`Sprite_Queue(x,y,id)` (0x10f11); `Sfx_Play(id,freq,vol,x)` (0x315bc); `Particle_Spawn(x<<8,y<<8,vx,vy,a5,a6,a7)`
(0x26560); `Map_GetTileAttr(col,row,layer)` (0x10cb6), attribute `> 0x7e` = solid, `0x82` = water;
`Byte_Get(ptr,idx)` (0x12f3e); ground height column table = `g_MapVal[0x400 + col]` (in tiles); `Hud_PushMessage(str)` (0x26e03);
`Text_DrawNumber(x,y,val,digits)` (0x128f1); `Video_FillRect(x1,y1,x2,y2,col)` (0x1334f).
**The order of `Rand()` calls is given exactly**; C short-circuit evaluation decides whether a `Rand` is
consumed, so conditions are written in source order.

**Coordinates.** World pixels; tile = 16 px; the map is `g_MapWidth` (0x8fe90) columns × 64 rows
(y 0..0x3ff). Player world position = `g_CamX + g_PlayerScrX`, `g_CamY + g_PlayerScrY` (abbreviated
`PX`, `PY` below). `DIR32` = `g_901f0` (0..31, = `g_90200/2`, player heading, see player.md);
`DIR16 = DIR32/2`. `HELI` = `g_904cc` (helicopter flag, MISC.Z w91).
The dead/removed position marker is **-999** (`0xfffffc19`).

---------------------------------------------------------------------------------------------------

## 1. Overview and call tree

```
Game_Run 0x1ba0c (per frame, in this order — only weapon-relevant calls)
 ├ 0x1c92a  Bullets_Clear                       (mission start)
 ├ 0x1d706  Weapons_FrameDispensers 0x15ce6     (JP233 / Porcupine rack dispensers, every frame)
 │            ├ g_JP233Armed     → Weapon_DispenseJP233 0x3494e → Weapon_LaunchBallistic
 │            └ g_PorcupineArmed → Weapon_DispensePorcupine 0x34b94 → Weapon_FireGuided
 ├ 0x1dad9  Projectiles_Update 0x415e4          (if g_ProjCount && !g_907fc && !g_9099c)
 │            ├ flight routines (k3): 0x4341a 0x42d0e 0x42ec9 0x184cf(→0x182cb) 0x425c2 0x42509
 │            │                        0x438f2 0x42dc6 0x43d05, inline k3=8/10
 │            ├ kind routines (k0): 0x433c1 (parachute), 0x43207 (commando)
 │            ├ Projectile_HitGround 0x413a0 → Explosion_Damage / commando landing / 0x3e66b
 │            └ Explosion_Damage 0x39b69 → Map_CraterAt 0x3b777, Explosion_Terrain 0x3a99f
 │                                               → 0x3afc0 / 0x3af32 / 0x3b18d / Explosion_Particles 0x3adf9
 ├ 0x1e3ae  Player_Weapons 0x32846 (the gun)    (if g_GunTrigger && …, §4)
 │            ├ flamer: spawn flame shot (updated by 0x17c56, §4.4)
 │            └ gun: Bullet_Add, ray trace 0x4501e, hit rolls → Explosion_Damage
 ├ 0x1e419  Bullets_Update(g_CamX, g_CamY)       (after Sprite_DrawQueue; pixels)
 └ 0x209ae.. trigger block (§3.1): Weapon_Fire 0x341e9 ×2 per rack
              └ dispatch on fire_kind k0 (§3.3) → Weapon_* → Projectile_Init 0x34e56
Player_DeathAndLanding 0x2780b (player.md) contains the per-frame flare update (§7.2)
Flare_Release 0x38a61 is also called when an enemy missile is launched (0x28e68, 0x38724).
Enemy bomber 0x17944 (enemies spec) reuses Weapon_LaunchBallistic / Weapon_FireRocket /
  Weapon_FireGuided / Weapon_TakePhoto / Weapon_DropTank / Weapon_ArmJP233 / Weapon_DropCommando
  through its own jump table (with 0x1821c for kind 9) after setting g_RackWeapon[g_FireRack] = g_EnemyBombWeapon.
```

**Unreferenced code (do not port, documented for orientation):** `0x332fb` (`Projectiles_UpdateOld`,
also entered at `+0xd` = `0x33308`; no caller, no pointer to it anywhere in the image — verified by a
full scan for `call/jmp/jcc rel32` and for the absolute address). It is an older single-projectile
updater with jump tables on k3 (14 entries → 0x1a7cb, 0x1821c, 0x18663, 0x184cf, 0x1968e, 0x19178,
0x182cb, 0x18bc8, 0x1844f, 0x18fca, 0x190aa, 0x19153, 0x18720, 0x18771) and on k0 (11 entries). `0x42b70`
(an ALARM variant) has no caller either. **The live projectile updater is `0x415e4`**, which processes
**all** projectiles (the existing name `Projectiles_UpdateGuided` was misleading → renamed
`Projectiles_Update`; the dead one → `Projectiles_UpdateOld`).

---------------------------------------------------------------------------------------------------

## 2. Data

### 2.1 WEAPONS.DAT tables (loaded by MainMenu, see port/formats/data.md)

Per weapon `w` (0..70): `g_WeaponType[w]` 0x8f168 + w*0x18 = 6 ints **k0..k5**:

| k | Offset | Name here | Meaning |
|---|---|---|---|
| k0 | +0x00 | fire_kind | Weapon_Fire dispatch (§3.3) and projectile "kind" (sprite base, frame logic) |
| k1 | +0x04 | arm_delay | projectile: frames before the flight routine runs (counts down). Gun pod: flame mode (§3.12) |
| k2 | +0x08 | motor | projectile: 0 = unpowered, 1 = powered (smoke + speed), 2 = fixed speed 32. Gun pod: power bonus. Guided launch: k1≠0 selects the boosted launch (§3.10) |
| k3 | +0x0c | flight | flight routine 0..14 (§5.4). Gun pod: gun power |
| k4 | +0x10 | detonate | 1 = explode when the life counter runs out (k3=… ; also 2 = "switch flight to 4" in 0x42ec9/0x438f2/0x43d05). Gun pod: gun type |
| k5 | +0x14 | flags | 2 = runway penetrator (blast ×5 below y 0x3d4); for cluster weapons (k3 4/11) = submunition weapon index + 3 |

Other per-weapon ints: `g_WeaponBlastA` 0x921f4 (damage/"power", Explosion_Damage arg 5; special
negative values below), `g_WeaponBlastB` 0x91cf4 (arg 6), `g_WeaponThrust` 0x920d8 (initial life
counter; for ballistic launches also the forward impulse), `g_WeaponWeight` 0x9242c (lb subtracted from
`g_LoadWeight` 0x8feb8 per Weapon_Fire call), `g_WeaponPerRack` 0x91fbc, `g_WeaponRackMult` 0x91e10,
`g_WeaponIcon` 0x92310.

Special `BlastA` values (checked in Weapon_LaunchBallistic, Weapon_FireRocket, Explosion_Damage, the
projectile sprite code): **-40** Tac Nuke ("Nuclear Psychopath Warning!", explosion handler is an
empty stub), **-10** camera bomb / rocket camera (camera follows it), **-20** Marker (sets
`g_MarkerX/Y`), **-30** Drop Zone (sets `g_DropZoneX/Y`), **-50** Water Bomb (sprite `0x204+Rand(1)`).

Shipped table (abridged; `k = [k0..k5]`, A/B/thr = BlastA/BlastB/Thrust):

| w | Name | k | A/B/thr | Routing |
|---|---|---|---|---|
| 0-5 | Stinger, Sidewinder, AMRAAM, Phoenix, Sky Flash, Sparrow | [2,k1,1,0,1,0] (k1 0/0/0/2/1/2) | 50..500, thr 12..200 | guided AAM, homing 0x4341a |
| 6,7,10,13 | 250/500/1000/2000lb Bmb | [0,0,0,1,0,0] | 250..2000, thr 0 | ballistic |
| 8,11 | 500lb Drag, 1000lb Drg | [0,0,0,1,0,0] | thr **-4** | ballistic, retarded (forward impulse -4) |
| 9,12,14 | LGBs | [0,0,0,6,0,0] | | laser guided (0x425c2) |
| 15 | B.BUSTER | [0,4,1,8,0,2] | 3000/2000/20 | brake-and-drop (k3=8), penetrator |
| 16,17,20,29 | Clustr, Drag C, HADES, FA BOMB | [0,0|4,0,4,0,64..67] | | cluster: 6 submunitions of weapon k5-3 (61..64) |
| 18,19 | JP233, TWIN JP233 | [8,0,0,1,0,2] | 2000/500/0 | arms the JP233 dispenser |
| 21 | Durandal | [2,4,1,8,0,2] | | guided launch, k3=8 |
| 22-25 | Lt Rkt Pod, A.G Rocket, Hydra Pod, HVY ROCKET | [1,0,1,1,0,0|1] | thr 8..10 | rockets |
| 26-28 | HellfireAT, TOW AT, AGM-65 | [2,0,1,5,0,1] | | AT missiles (0x42509 + 0x42ec9) |
| 30 | AGM-130 | [2,1,1,11,0,64] | | steer + cluster (k5 64 → Bomblet) |
| 31 | ALARM | [2,0,1,3,0,0] | | 0x184cf |
| 32 | HARM | [2,0,1,2,0,0] | | 0x42d0e (+0x42ec9 with target marker) |
| 33-35 | Sea Eagle, Sea Skua, Harpoon | [2,0,1,7,0,1] | | sea skimmer 0x438f2 |
| 36 | Drop Tank | [5,0,0,1,0,0] | 1/1/100 | Weapon_DropTank |
| 37 | Camera Pod | [4,0,0,0,0,0] | | Weapon_TakePhoto |
| 38 | Air Mines | [0,4,0,0,0,0] | 50/50/-4 | ballistic, homing flight 0 |
| 39 | Skip Bomb | [3,0,2,1,0,0] | | ballistic, bounces on water |
| 40 | Tac Nuke | [0,0,0,1,0,0] | **-40**/0/-4 | stock 0 |
| 41-47 | Browning, Aden, OKB, M61, AT Cannon, Flamer, Backflash | [9,…] | | gun pods (§3.12) |
| 48, 49 | Cam Bomb, Rket Cam | [7,…] / [1,…] | **-10** | camera follows |
| 50 | Porcupine | [11,0,1,5,0,29] | | arms the Porcupine dispenser |
| 51, 52 | Drop Zone, Marker | [0,0,0,1,0,0] | -30 / -20 | ballistic markers |
| 53 | Commando | [10,0,0,1,0,0] | 0/0/0 | Weapon_DropCommando |
| 54 | Combi | [2,0,1,12,0,0] | | flight 12 |
| 55, 56 | RP Cruise, Cruise | [2,4,1,13,0,66|0] | | cruise 0x43d05 |
| 57 | Flare | [12,…] | | Flares_Add |
| 58-60 | Mini Bomb, Hedgehog, Water Bomb | | Hedgehog k5=41 → Air Mines | |
| 61-64 (= 67-70) | Bomblet, Para Bmlet, RP Bomblet, FA Bomblet | [6|7,0,0,1|10,0,0|2] | | submunitions |

### 2.2 Player projectiles (structure of arrays, verified)

`g_ProjCount` 0x908bc = number of live projectiles (0..21; launches refuse at ≥ 21, the cluster
spawner clamps at 20). Each field is an `int32[21]` (stride 4); most arrays are 0x54 bytes apart.
Written as a C struct for clarity, **but the port must keep separate arrays** only if it wants to keep the
swap bug in §5.6 bit-exact (it is easy to emulate with a struct too).

```c
/* index i = 0..20; address = base + 4*i */
typedef struct {
    int32 blastA;     /* 0x92720 g_ProjBlastA  : Explosion_Damage arg5; k0==10: sprite id (0 -> 0x18e) */
    int32 frame;      /* 0x92774 g_ProjFrame   : sprite frame / heading 0..7 for homing kinds          */
    int32 blastB;     /* 0x927c8 g_ProjBlastB  : Explosion_Damage arg6                                  */
    int32 x;          /* 0x92830 g_ProjX       : world px, -999 = dead                                  */
    int32 y;          /* 0x92884 g_ProjY                                                                */
    int32 vx;         /* 0x928d8 g_ProjVX      : px / frame                                             */
    int32 vy;         /* 0x9292c g_ProjVY                                                               */
    int32 life;       /* 0x926b8 g_ProjLife    : thrust/fuel counter                                    */
    int32 k0_kind;    /* 0x91384 g_ProjKind                                                             */
    int32 k1_arm;     /* 0x912b0 g_ProjArm     : arm delay, counts down                                 */
    int32 k2_motor;   /* 0x914ac g_ProjMotor                                                            */
    int32 k3_flight;  /* 0x91330 g_ProjFlight                                                           */
    int32 k4_det;     /* 0x9125c g_ProjDetonate                                                         */
    int32 k5_flags;   /* 0x913d8 g_ProjFlags                                                            */
    int32 targetX;    /* 0x92590 g_ProjTargetX : 0 = none (set by flight routines)                     */
    int32 targetY;    /* 0x925e4 g_ProjTargetY                                                          */
    int32 lock;       /* 0x91ca0 g_ProjLock    : g_LockTarget+1 at launch (k3==0 only); NOT swapped    */
} Projectile;
```

Launch staging globals (set by Weapon_Fire, read by the launch routines): `g_LaunchX` 0x90878,
`g_LaunchY` 0x9087c, `g_LaunchDirX` 0x90784, `g_LaunchDirY` 0x90790, `g_LaunchSfx` 0x907c4.

"Current projectile" cache used by Projectiles_Update and every flight routine (the flight routines
read and write **these globals**, not the arrays, except where noted):
`g_LoopI` 0x90ab8 (index), `g_CurX` 0x9080c, `g_CurY` 0x90810, `g_CurVX` 0x90934, `g_CurVY` 0x90938,
`g_CurFrame` 0x9086c, `g_CurArm` 0x90848, `g_CurKind` 0x90870, `g_CurFlight` 0x908c4,
`g_CurMotor` 0x90880, `g_CurFlags` 0x90874, `g_CurDetonate` 0x908d4, `g_CurSpeed` 0x908a8,
`g_CurTileAttr` 0x908b0, `g_ProjStepX/Y` 0x90914/0x908cc, aim point `g_AimX/Y` 0x9054c/0x9055c
(persist across projectiles and frames!), `g_AimDirX/Y` 0x90a24/0x90a28, `g_AimFrame` 0x90774,
`g_AimLocked` 0x907cc. **PORT: keep these as file-scope globals with the same lifetime**; several
routines rely on values left over from the previous projectile or frame (§10).

### 2.3 Direction tables (built at start-up, verified with ftrace)

All `(int)` conversions truncate toward 0; `a = i*pi/8` for 16 entries (constant 0.392699075 as a float),
`a = i*pi/16` (0.1963495375) for 32 entries.

| Table | Built in | Value |
|---|---|---|
| `g_Dir16X` 0x8fc30[i] (stride **8**: used as `[2*f]`), `g_Dir16Y` 0x8fc70[i] | MainMenu 0x4747c | `(int)(-cos(a)*8.0)`, `(int)(-sin(a)*6.0)`, i=0..15 (written at `[i]`, int stride 4; read as `[f*2]`) |
| `g_LaunchDirTabX` 0x8faf0[i] | MainMenu | `2 * g_Dir16X[i]` |
| `g_LaunchDirTabY` 0x8fb30[2i], [2i+1] | MainMenu | `2 * g_Dir16Y[i]`, and **3** (helicopter column) |
| 0x8fe10[i], 0x8fe50[i] (16) | PlayerState_ResetA 0x15d2b | `(int)(-cos(a)*8.0)`, `(int)(-sin(a)*3.0)` |
| 0x8e150[i] (32) | PlayerState_ResetA | `(int)-(cos(a)*16.0 + 0.5)` |
| 0x8e0d0[i] (32) | PlayerState_ResetA | `(int)(-cos(a)*8.0)` |
| 0x8e290[i] (32) | PlayerState_ResetA | `(int)(-sin(a)*16.0)` |
| 0x8e210[i] (32) | PlayerState_ResetA | `(int)(-sin(a)*5.2)` |
| 0x92980 (uint16[9]) | GENDATA step 2 | 3×3 heading table: `frame = tab[(dx+1)*3 + dy + 1] / 2` (read as `*(uint16*)(0x92982 + (dx+1)*6 + dy*2)`) |

(`i*pi/8` heading 0 points **left** (cos 0 → -8), heading 8 points right, 1..7 are nose-up.)

### 2.4 Racks and loadout (owned by game_flow/player; used here)

`g_FireRack` 0x90338 (0/1: rack being fired), `g_RackWeapon[2]` 0x8fbc8/0x8fbcc (weapon index),
`g_RackCount[2]` 0x8fcb0/0x8fcb4 (rounds), `g_LoadWeight` 0x8feb8, `g_FirePairFirst` 0x8ff00 (1 on the
first of the two Weapon_Fire calls per press), `g_FireKindP1` 0x9002c (= k0+1 of the weapon being fired),
`g_FireFail` 0x90690 (0 = OK; ≠0 = the launch routine refused or already did the bookkeeping),
`g_FireLatch` 0x902d4 (set by routines that must fire only once per press; cleared each frame at
0x208f7).

### 2.5 Bullets (visual tracer pixels, verified)

```c
typedef struct { int32 x, y;     /* world px << 8 (8.8 fixed) */
                 int32 vx, vy; } Bullet;   /* added *32 per frame */
Bullet g_Bullets[32];            /* 0x92b20, stride 0x10 */
int32  g_BulletCount;            /* 0x93224 */
int32  g_Bullet93220;            /* 0x93220, cleared with the count, never read here */
```

### 2.6 Flares (verified)

`g_FlaresPending` 0x906a0 (set to **8** by Mission_Setup and WeaponSelect_Screen, +1 per Flare weapon
shot), `g_FlareCount` 0x90310 (0..2), per flare `[2]`: `g_FlareLife` 0x8fad8, `g_FlareX` 0x8fae0,
`g_FlareY` 0x8fae8, `g_FlareVX` 0x8fbb0, `g_FlareVY` 0x8fbb8.

### 2.7 Gun

From MISC.Z (player.md): `g_GunFlameMode` 0x906b0 (w79; 0 = normal gun, ≠0 = flamer heading offset;
0xff = "none"?), `g_GunSpreadA` 0x90508 (w68), `g_GunSpreadB` 0x905a4 (w69), `g_GunSpreadA2` 0x90560
(w70), `g_GunSpreadB2` 0x90564 (w71), `g_GunHitBonus` 0x90850 (w72), `g_GunPower` 0x90558 (w94 % 10),
`g_GunType` 0x901f8 (w94 / 10; number of hit rolls = type/2, spread jitter), `g_GunAmmo` 0x909c4
(w112 + ammo pods*100). Runtime: `g_GunHeat` 0x905b4 (0..40, fire only < 20, -sign per frame at
0x20abd), `g_GunPowerUp` 0x90544 and `g_GunPowerUp2` 0x90944 (bonus crates; reset at mission end),
`g_GunFiring` 0x905b0, `g_GunPodFiring` 0x90584, `g_LockTarget` 0x90454 and `g_LockDist` 0x90420
(set by Player_DeathAndLanding/EnemyAir_Update/EnemyGround_Update, see §4.3), `g_GunRange` 0x90494
(= 0x10000 in MainMenu), `g_StrafeCount` 0x90738, `g_GunTrigger` 0x90ca8.

---------------------------------------------------------------------------------------------------

## 3. Firing the racks

### 3.1 Trigger block in Game_Run (0x208d9–0x20abd, verified)

Executed once per frame:

```c
g_901f0 = g_90200 / 2;                 /* DIR32 */
g_90438 = 0;
g_FireLatch = 0;
if (g_9099c == 0 && g_907fc == 0) {    /* not dead / not in an end sequence (player.md) */
    if (g_905cc && g_8ff14 - g_905dc == 1 && !HELI) g_90438 = 1;
    for rack r in {0 (key g_FireKey1 int16 @0x8453c, disable g_8fa2c), 1 (key g_FireKey2 @0x84540, disable g_8fa30)}:
        if (key != 0 && g_8ff14 - g_905dc == 0 && g_90624 == 0 && disable == 0
            && g_RackCount[r] > 0 && g_90440 == 0 && g_90ca0 == 0) {
            g_FirePairFirst = 1; g_FireRack = r; Weapon_Fire();
            g_FirePairFirst = 0; g_FireRack = r; Weapon_Fire();
        }
    if (g_FireKey1 == 0 && g_FireKey2 == 0) g_DropTankHold = 0;     /* 0x905e0 */
    if (g_FireKey1) g_FireKey1 = 0;                                  /* keys are one-shot latches */
    if (g_FireKey2) g_FireKey2 = 0;
    g_90624 = 0;
    g_PhotoBlocked = (g_905cc != 0);                                 /* 0x9067c */
}
g_GunHeat -= Sign(g_GunHeat);                                        /* 0x20abd, always */
```

`g_8ff14 - g_905dc == 0` is a plane-state condition from MISC.Z w89/w90 (player.md; gear/landed
state). `g_90440` = 1 at mission start (on the runway / not launched yet). **Each accepted press calls
Weapon_Fire twice** (a launch "pair"); the second call is blocked only by `g_FireLatch` (set by routines
that fire once) or by `g_ProjCount >= 21`. The rack count is charged only on the first call
(§3.2), so ballistic weapons and rockets launch **two projectiles per round** (verified; quirk Q1).

The gun is fired by `Player_Weapons` at 0x1e3ae when
`g_GunTrigger (0x90ca8) && g_8ff14 - g_905dc == 0 && !g_907fc && !g_9099c && !g_90440` (verified).

### 3.2 Weapon_Fire 0x341e9 (`void Weapon_Fire(void)`, verified)

```c
if (g_ProjCount >= 21 || g_FireLatch != 0) return;
w = g_RackWeapon[g_FireRack];
g_LaunchX   = PX;
g_LaunchY   = PY + g_FirePairFirst * 4;
g_LaunchDirX = g_LaunchDirTabX[DIR32 / 2];
g_LaunchSfx  = 0;
g_LaunchDirY = g_LaunchDirTabY[(DIR32 / 2) * 2 + HELI];
g_FireKindP1 = g_WeaponType[w].k0 + 1;
g_FireFail   = 0;
switch (g_FireKindP1 - 1) {                       /* jump table 0x34219, 13 entries; >12 -> none */
  case 0: case 3: case 6: case 7: Weapon_LaunchBallistic(); break;   /* 0x3529c */
  case 1:  Weapon_FireRocket();      break;   /* 0x35532 */
  case 2:  Weapon_FireGuided();      break;   /* 0x356fe */
  case 4:  Weapon_TakePhoto();       break;   /* 0x348db */
  case 5:  Weapon_DropTank();        break;   /* 0x34f0d */
  case 8:  Weapon_ArmJP233();        break;   /* 0x34e13 */
  case 9:  Weapon_FireGunPod();      break;   /* 0x346f9 */
  case 10: Weapon_DropCommando();    break;   /* 0x351e9 */
  case 11: Weapon_ArmPorcupine();    break;   /* 0x34dd0 */
  case 12: Flares_Add();             break;   /* 0x346d3 */
}
if (g_FireFail != 0 || g_FireKindP1 <= 0) return;
g_LoadWeight -= g_WeaponWeight[w];
if (g_FireKindP1 != 12) {                        /* i.e. k0 != 11 (Porcupine arming) */
    n = g_ProjCount;                             /* copy k0..k5 into slot n (even if not registered) */
    g_ProjKind[n] = k0; g_ProjArm[n] = k1; g_ProjMotor[n] = k2;
    g_ProjFlight[n] = k3; g_ProjDetonate[n] = k4; g_ProjFlags[n] = k5;
    if (g_LaunchSfx == 1) { Sfx_Play(5, 6000, 0x28, PX); g_ExplSfxHold = 10; }   /* 0x900f0 */
}
if (g_FireKindP1 == 6) {                         /* drop tank */
    if (g_DropTankHold < 10) return;
    g_RackCount[g_FireRack] = 0;
    other = (g_WeaponType[g_RackWeapon[1 - g_FireRack]].k0 == 5) ? 1 : 0;
    g_90378 -= other * g_90788;                  /* fuel capacity -= pod capacity (MISC.Z w107) */
    g_905b8 = min(g_905b8, g_90378);             /* fuel */
    g_LoadWeight -= g_90788 / 4;
    g_ProjCount = min(g_ProjCount + 1, 21);
    if (g_8fa38 == 0) { Video_FillRect(0x54, r*18+0x20, 0x6a, r*18+0x25, 0x1d);
                        Text_DrawNumber(0x54, r*18+0x20, g_RackCount[r], 3); }   /* r = g_FireRack */
    return;
}
g_RackCount[g_FireRack] -= g_FirePairFirst;
if (g_GameMode == 3) { g_RackCount[0]--; g_RackCount[1]--; }      /* Aerolimits: both racks, every call */
if (g_FireKindP1 != 12) {
    g_ProjTargetX[g_ProjCount] = 0; g_ProjTargetY[g_ProjCount] = 0;
    g_ProjCount = min(g_ProjCount + 1, 21);
}
if (g_8fa38 == 0 && g_GameMode < 3) { /* same FillRect + DrawNumber as above */ }
```

The HUD count is drawn at x 0x54, y 0x20 + 18*rack (3 digits, colour 0x1d background).
`g_8fa38` = HUD-number suppression flag (game_flow). Quirks: Q2 (routines that do not create a
projectile still register one), Q3 (Aerolimits double charge).

### 3.3 Projectile_Init 0x34e56 (verified)

```c
w = g_RackWeapon[g_FireRack]; n = g_ProjCount;
g_ProjBlastA[n] = g_WeaponBlastA[w];  g_ProjBlastB[n] = g_WeaponBlastB[w];
g_ProjX[n] = g_LaunchX;               g_ProjY[n] = g_LaunchY + 8;
g_ProjLife[n] = g_WeaponThrust[w];
```

### 3.4 Weapon_LaunchBallistic 0x3529c (was Weapon_FireMissile; k0 = 0, 3, 6, 7; verified)

```c
Projectile_Init();  n = g_ProjCount;  w = g_RackWeapon[g_FireRack];
g_ProjFrame[n] = 0;
s = abs(g_90778 / 2) + g_WeaponThrust[w];         /* g_90778 = player x speed (player.md) */
s = Clamp(s, 0, 12);                               /* written as nested min/max; no Rand */
g_ProjVX[n] = s * Sign(g_LaunchDirX);
g_ProjVY[n] = Clamp(g_LaunchDirY, 0, 16);
g_LaunchSfx = 1;
if (g_ProjBlastA[n] == -40) { Hud_PushMessage("Nuclear Psychopath Warning!" @0x81358);
                              g_LaunchSfx = 0; g_FireLatch = 1; }
if (g_ProjBlastA[n] == -10 && g_8ff10 == -1) { Hud_PushMessage("Following Camera Pod" @0x81375);
                              g_8ff10 = g_ProjCount; g_LaunchSfx = 0; }  /* camera follows slot n */
if ((g_ProjBlastA[n] > 400 && Rand(10) == 1 && g_BigBombSfxDone == 0) || g_ProjBlastA[n] == -40) {
    Sfx_Play(0x15, 0x1004, 0x3f, PX);  g_BigBombSfxDone = 1;          /* 0x90198 */
}
```

Retarded bombs (thrust -4): `s = |vx_player/2| - 4`, clamped at 0, i.e. they leave with less forward
speed. Note the gravity/flight is in §5.

### 3.5 Weapon_FireRocket 0x35532 (was Weapon_FireCamera; k0 = 1; verified)

```c
d = DIR32 / 2;
if (d == 0 || d >= 8 || HELI == 1) {               /* nose-up headings 1..7 refuse unless helicopter */
    Projectile_Init();  n = g_ProjCount;
    g_ProjVX[n] = Sign(g_LaunchDirX) * 32;
    g_ProjVY[n] = max(Sign(g_LaunchDirY) * 32, 0);
    g_ProjFrame[n] = d - 8;  if (g_ProjFrame[n] < 0) g_ProjFrame[n] = 8;
    Sfx_Play(8, 4000, 0x3f, PX);
    if (HELI) { if (DIR32 < 6) { frame = 7; vx = -32; } else { frame = 2; vx = 32; } }
} else g_FireFail = 1;
n = g_ProjCount;                                    /* even after a refusal (stale slot) */
if (g_ProjBlastA[n] == -10 && g_8ff10 == -1) {
    Hud_PushMessage("Following Camere Pod" @0x8138b);  /* sic */
    g_8ff10 = g_ProjCount; g_LaunchSfx = 0;
}
```

### 3.6 Weapon_FireGuided 0x356fe (k0 = 2; verified)

```c
g_FireLatch = 1;
w = g_RackWeapon[g_FireRack];
if (k3(w) <= 0) {                                   /* air-to-air: needs a lock in range */
    if (g_LockTarget <= -1) { g_FireFail = 1; return; }
    m = max(g_WeaponThrust[g_RackWeapon[0]], g_WeaponThrust[g_RackWeapon[1]]);
    range = (m*32 < 0x280) ? 0x280 : m*32;
    if (g_LockDist > range) { g_FireFail = 1; return; }
}
if (k3(w) == 0) g_ProjLock[g_ProjCount] = g_LockTarget + 1;
Projectile_Init();  n = g_ProjCount;
g_ProjVX[n] = Sign(g_LaunchDirX);
Sfx_Play(8, 4000, 0x3f, PX);
if (k1(w) == 0) g_ProjVY[n] = max(Sign(g_LaunchDirY), 0);
else { g_ProjVY[n] = 2;
       g_ProjVX[n] = Clamp(abs(g_LaunchDirX) + g_WeaponThrust[w], 0, 16) * Sign(g_LaunchDirX); }
if (g_ProjVX[n] == 0 && g_ProjVY[n] == 0) g_ProjVX[n] = 1;
g_ProjFrame[n] = DIR32 / 4;
```

The range uses the **larger thrust of the two racks**, not the fired weapon (quirk, keep).

### 3.7 Weapon_TakePhoto 0x348db (was Weapon_DropMarker; Camera Pod k0 = 4; verified)

```c
g_FireLatch = 1;
if (g_PhotoCount < 10 && g_PhotoBlocked == 0) {          /* 0x902cc, 0x9067c */
    g_PhotoCol[g_PhotoCount] = PX / 16;  g_PhotoCount++;   /* 0x91304[10] */
} else g_FireFail = -1;
```

The recon objective test (game_flow, Mission_CheckObjectives) reads `g_PhotoCol`. `g_PhotoCount` is
reset when a life is lost (Game_Run 0x1ba0c). A successful photo still costs weight and registers a
phantom projectile (Q2).

### 3.8 Weapon_DropTank 0x34f0d (was Weapon_FireRepeat; k0 = 5; verified)

```c
if (++g_DropTankHold < 10) return;                          /* 0x905e0, +1 per Weapon_Fire call */
other = (g_WeaponType[g_RackWeapon[1 - g_FireRack]].k0 == 5) ? 1 : 0;
g_905d0 = g_90388 - other * g_90788;                        /* fuel capacity without this tank */
n = g_ProjCount;
g_ProjX[n] = g_LaunchX;  g_ProjY[n] = g_LaunchY + 8;
g_ProjBlastA[n] = Clamp(g_905b8 - g_905d0, 0, 500);         /* fuel still in the tank explodes */
g_ProjBlastB[n] = 2000;  g_ProjFrame[n] = 0;  g_ProjLife[n] = 0;
s = Clamp(abs(g_90778 / 2) + g_WeaponThrust[w], 0, 12);
g_ProjVX[n] = s * Sign(g_LaunchDirX);
g_ProjVY[n] = Clamp(g_LaunchDirY, 0, 16);
g_LaunchSfx = 1;
```

Weapon_Fire (§3.2) subtracts the weight on **every** call while the key is held (Q4) and finishes the
jettison when the hold counter reaches 10.

### 3.9 Weapon_DropCommando 0x351e9 (was Weapon_FireBomb; k0 = 10; verified)

```c
g_FireLatch = 1;  Projectile_Init();  n = g_ProjCount;
g_ProjFrame[n] = 0;  g_ProjVX[n] = 0;  g_ProjVY[n] = Clamp(g_LaunchDirY, 0, 8);
g_LaunchSfx = 1;
```

### 3.10 Arming the dispensers (verified)

* `Weapon_ArmPorcupine` 0x34dd0 (k0 = 11): `g_FireLatch = 1; if (g_PorcupineArmed == 0) { g_PorcupineArmed = g_FireRack + 1; g_8ff10 = -1; }` (0x901ec).
* `Weapon_ArmJP233` 0x34e13 (k0 = 8): same with `g_JP233Armed` 0x901f4.

`Weapons_FrameDispensers` 0x15ce6 (called every frame at 0x1d706): `if (g_90100) FUN_177db(); if (g_JP233Armed) Weapon_DispenseJP233(); if (g_PorcupineArmed) Weapon_DispensePorcupine();`
(0x177db is not a weapon routine; enemies/player spec.)

### 3.11 Weapon_DispenseJP233 0x3494e / Weapon_DispensePorcupine 0x34b94 (verified)

Identical except where noted (`A` = armed rack+1 = `g_JP233Armed` / `g_PorcupineArmed`):

```c
g_8ff10 = -1;
g_LaunchDirX = g_LaunchDirTabX[DIR32/2] + Rand(4) - Rand(4);                /* Rand #1, #2 */
g_LaunchDirY = Rand(2) + g_LaunchDirTabY[(DIR32/2)*2 + HELI];               /* Rand #3 */
if (g_9099c == 0) { g_LaunchX = PX; g_LaunchY = PY; }                      /* else last position */
if (g_RackCount[A-1] == 0 || g_ProjCount == 21) { A = 0 /* disarm */; return; }
Sfx_Play(2, 20000, 0x1e, PX);
g_RackCount[A-1]--;
[JP233 only] g_FireKindP1 = 9;
g_FireRack = A - 1;
JP233: Weapon_LaunchBallistic();   Porcupine: Weapon_FireGuided();
n = g_ProjCount;  w = g_RackWeapon[A-1];
g_ProjKind[n] = (JP233 ? 7 : 2);                                            /* parachute / guided */
g_ProjArm[n] = k1(w); g_ProjMotor[n] = k2(w); g_ProjFlight[n] = k3(w);
g_ProjDetonate[n] = k4(w); g_ProjFlags[n] = k5(w);
g_ProjTargetX[n] = 0; g_ProjTargetY[n] = 0;
g_ProjCount = min(g_ProjCount + 1, 21);
```

One submunition per frame until the rack is empty. No weight is subtracted. The Porcupine path ignores
`g_FireFail` from Weapon_FireGuided (k3 = 5 > 0, so no lock is needed anyway). A trailing
`cmp [0x8fa38],0` has no effect (dead HUD code). Note `g_FireLatch` set by FireGuided is cleared at
the next frame's trigger block, so it does not block the dispenser.

### 3.12 Weapon_FireGunPod 0x346f9 (was Weapon_FireFromStock; k0 = 9; verified)

Temporarily swaps the gun parameters with the pod's and calls the gun once:

```c
r = g_FireRack;
if (g_RackCount[r] > 0) {
    save ammo=g_GunAmmo; g_GunAmmo = g_RackCount[r];
    save fuel=g_905b8;   g_905b8 = g_GunAmmo * 200;      /* flamer "fuel" = rounds*200 */
    g_RackCount[r]--;
    save g_GunFlameMode; g_GunFlameMode = k1(w);
    save g_GunHitBonus;  g_GunHitBonus  = k2(w);
    save g_GunPower;     g_GunPower     = k3(w);
    save g_GunType (to g_8ff84); g_GunType = k4(w);
    save g_GunSpreadA;  g_GunSpreadA = 2;
    save g_GunSpreadB;  g_GunSpreadB = 0;  save g_GunSpreadA2; g_GunSpreadA2 = 0;
    save g_GunSpreadB2; g_GunSpreadB2 = 0;
    g_GunPodFiring = 1;  Player_Weapons();  g_GunPodFiring = 0;
    (a dead `cmp [0x8fa38]/[g_GameMode]` pair)
    restore all saved values (fuel, ammo, flame mode, bonus, type, spreads)
}
g_FireFail = -1;                                        /* no weight, no projectile */
```

Save slots: 0x90018 ammo, 0x90024 fuel, 0x90054 flame, 0x90044 bonus, 0x90040 power — **power is
saved but not restored** (verified: no write back to 0x90558; quirk Q5: firing a pod permanently
replaces the built-in gun's power with the pod's k3), 0x8ff84 type, 0x8ffa8/0x9003c/0x90010/0x90014 spreads.
Since the pod gun runs with `g_GunPodFiring = 1`, Player_Weapons does not touch heat or ammo; one rack
round per Weapon_Fire call (two per key press).

### 3.13 Flares_Add 0x346d3 (k0 = 12; verified)

`g_FlaresPending++; Flare_Release();`

---------------------------------------------------------------------------------------------------

## 4. The gun: Player_Weapons 0x32846 (verified)

### 4.1 Flamer branch (`g_GunFlameMode != 0`)

```c
if (g_8fa50 == 0 && g_905b8 > 0 && g_FlameActive == 0) {          /* 0x90608 */
    Sfx_Play(8, 3000, 0x3f, PX);
    g_905b8 -= 200;                                                  /* fuel */
    g_9062c = (DIR32/2 + g_GunFlameMode - 1) & 15;                   /* Backflash (9) fires backwards */
    g_FlameX = PX + g_8fe10[g_9062c] * 8;                            /* 0x9142c */
    g_FlameY = PY + g_8fe50[g_9062c] * 12;                           /* 0x91458 */
    g_91500 = 0;
    g_FlameVX = g_8fe10[g_9062c];   g_FlameVY = g_8fe50[g_9062c] * 4;   /* 0x905f4, 0x905f8 */
    g_FlameActive = 1;
}
return;
```

With a built-in flamer the gun burns aircraft **fuel** (200 per shot). `g_8fa50`: gun disabled
(damage, player.md).

### 4.2 Gun branch

```c
if (g_GunAmmo <= 0 || g_GunHeat >= 20 || g_8fa50 != 0) return;
g_GunFiring = 1;                                               /* 0x905b0 */
if (g_8ff10 != 200) g_8ff10 = -1;                              /* cancel camera follow (player.md) */
if (g_GunType != 0) { Sfx_Play(1, 0x36b0, 0x10, 0); Sfx_Play(3, 0x1194, 0x20, 0); }
else {
    p = g_GunPower + g_GunPowerUp;
    g_90588 = (p > 0);
    Sfx_Play(g_90588*15 + 2, 20000/(1 + g_90588) - (p % 10)*1000, 0x20, PX);
}
if (g_8fa38 == 0 && g_GunPodFiring == 0) {
    Video_FillRect(0x55, 0x0a, 0x69, 0x0f, 0x1d);  Text_DrawNumber(0x55, 0x0a, g_GunAmmo, 4);
}
g_BulletVX = 0; g_BulletVY = 0;                                 /* 0x93394, 0x93398 */
if ((g_GunAmmo & 3) != 0 || g_90858 > 0 || g_GunSpreadA > 0) {  /* tracer */
    g_90248 = 1 - 2*(g_GunAmmo & 1);                            /* alternate sides */
    if (g_90858 == 0 && g_GunSpreadA == 0) g_90248 = 1;
    g_90598 = g_8e150[DIR32]/2 + g_907a4/8;                     /* g_907a4/8: player velocity (player.md) */
    g_9059c = g_8e290[DIR32]/2 + g_907a8/8;
    if (HELI) { g_90530 = g_9339c; g_9059c = g_8d860[g_90530]; g_90598 = g_8d818[g_90530]; }
                                                                /* GENDAT3 muzzle table, g_9339c = heli gun dir */
    if (g_9059c > 0) g_StrafeCount += 2;                        /* firing downwards */
    g_BulletVX = g_90598 * 40;  g_BulletVY = g_9059c * 40;
    by = ((g_GunSpreadB + g_GunSpreadB2) * g_8e150[DIR32] * g_90248) / 8;
    y  = (Rand(6) * g_GunType + PY + by) << 8;                                  /* Rand #1 */
    x  = -12 + PX - g_ShakeX + Rand(6) * g_GunType                              /* Rand #2 */
         + ((g_GunSpreadA + g_GunSpreadA2) * (g_8e290[DIR32] * g_90248)) / 2;
    Bullet_Add(x << 8, y, g_BulletVX, g_BulletVY);
}
```

(`by` is computed before Rand #1: the multiplications are `(B+B2)*e150` then `*side` then `/8`; for x it is
`e290*side` then `*(A+A2)` then `/2`.)

### 4.3 Hits

Lock targets (`g_LockTarget`, set elsewhere, verified readers): **-1** none; **0..1** enemy aircraft
`i` (arrays at 0x9264c x, 0x92660 y, 0x9270c hp, 0x92638 damage, EnemyAir_Update); **2** the big target
(0x9073c x, 0x90740 y, hp 0x9072c, killed flag 0x90728; Player_DeathAndLanding); **≥3** ground enemy
`t-3` (arrays indexed `[t]` at 0x8d99c hp, 0x8d98c damage, 0x8d9cc x, 0x8d9dc y — i.e. 0x8d9a8 / 0x8d998 /
0x8d9d8 / 0x8d9e8 indexed by `t-3`; EnemyGround_Update). `g_LockDist` = |dx|+|dy| to it.

```c
if (g_LockTarget == -1) {                                          /* ray to terrain */
    g_8fef0 = PX; g_8fef8 = PY;
    if (g_8ff10 == 200) { g_8fef0 = g_8ff68; g_8fef8 = g_8ff70; }  /* remote view origin */
    g_90598 = g_8e0d0[DIR32];  g_9059c = g_8e210[DIR32];
    if (HELI) { g_90530 = g_9339c; g_90598 = g_8d818[g_90530]; g_9059c = g_8d860[g_90530]; }
    g_BulletVX = g_90598 * g_GunRange / 10;
    g_BulletVY = g_9059c * g_GunRange / 3 + 0x1000;
    g_GunRayHit = Ray_Trace(g_8fef0 - 12, g_8fef8, g_BulletVX, g_BulletVY);   /* 0x4501e -> tile attr */
    if (g_GunRayHit > 0x7e) {
        g_90538 = Ray_HitX();  g_9053c = Ray_HitY();                          /* 0x450f8, 0x4511e */
        p = (g_GunPower + g_GunPowerUp) * 6 + 6;
        Explosion_Damage(g_90538, g_9053c, 0, 0, p, p);
    }
    g_GunRayHit = 0;
}
for (g_9062c = 0; g_9062c < g_GunType / 2; g_9062c++) {
    bonus = (g_GunHitBonus != 0 || g_GunPowerUp2 != 0) ? 1 : 0;
    dmgN  = g_GunPower + 1 + g_GunPowerUp + g_GunHitBonus + g_GunPowerUp2;
    /* (a) big target */
    if (g_LockTarget == 2 && g_LockDist <= 320 && Rand(8) < bonus*4 + 2) {
        g_9072c -= Rand(dmgN);
        xx = g_9073c + Rand(32);  xx -= Rand(32);
        Explosion_Damage(xx, g_90740, 0, 0, 5, 5);
        if (g_9072c < 0) { g_90728 = 1;
            for (g_90248 = 1; g_90248 < 5; g_90248++)
                Explosion_Damage(g_9073c - 32 + g_90248*16, g_90740, 0, 0, 2000, 2000); }
    }
    /* (b) ground enemy */
    if (g_LockTarget > 2 && g_LockDist <= 320 && Rand(13) < bonus*4 + 2) {
        t = g_LockTarget;
        hp[t] -= Rand(dmgN);                                   /* 0x8d99c[t] */
        if (hp[t] < 0) { dmg[t] -= hp[t]; hp[t] = 0;            /* 0x8d98c[t] += overflow */
                         Explosion_Damage(gx[t], gy[t], 0, 0, 5, 5); }   /* 0x8d9cc/0x8d9dc */
    }
    /* (c) enemy aircraft */
    if (g_LockTarget > -1 && g_LockDist <= 320 && !HELI && g_LockTarget < 2
        && Rand(13) < bonus*4 + 2) {
        t = g_LockTarget;
        ahp[t] -= Rand(dmgN);                                  /* 0x9270c */
        if (ahp[t] < 0) { adm[t] -= ahp[t]; ahp[t] = 0;         /* 0x92638 */
            if (adm[t] > 2 && g_8f0f0[t] == 0) { g_8f0f0[t] = 1; g_8f118[t] = ax[t]; g_8f128[t] = ay[t]; } }
        Explosion_Damage(ax[t], ay[t], 0, 0, 5, 5);            /* on every hit */
    }
}
```

Rand order per iteration: (a) `Rand(8)` [only if the first two conditions hold], `Rand(dmgN)`,
`Rand(32)`, `Rand(32)`; (b) `Rand(13)`, `Rand(dmgN)`; (c) `Rand(13)`, `Rand(dmgN)`. Kills and kill
score are applied by the enemy update routines when their damage counters exceed the limits
(enemies spec), not here. `g_GunType < 2` means **no hit rolls at all** (only the terrain ray when nothing
is locked) — this is the case for every gun pod in the shipped WEAPONS.DAT (k4 0 or 1) (quirk Q6).

### 4.4 Heat, ammo, out-of-ammo voice

```c
if (g_GunPodFiring == 0) {
    g_GunHeat += g_GunType * 2;   if (g_GunHeat >= 39) g_GunHeat = 40;
    g_GunAmmo = g_GunAmmo - g_GunType*2 - 1;
    if (g_GunAmmo <= 0 && g_90708 > 0 && abs(g_9264c[0] - PX) < 300 && abs(g_92660[0] - PY) < 300
        && g_9045c > 0 && g_90144 == 0) {                 /* plane class > 0 only */
        Sfx_Play(0x19, 8000, 0x3f, PX);  g_90144 = 1;
    }
}
```

The flame shot spawned in §4.1 is moved, collided and damages (tile hit → `Explosion_Damage(col*16,
row*16, 0, 0, g_GunPower*20+10, same)`, enemy aircraft in a 0x20 box → `hp -= Rand(4)`,
`Explosion_Damage(ax, ay, 0, 0, 10, 10)`) by **0x17c56** (currently named `Turrets_Update`, misnamed;
the flamer updater; it clears `g_FlameActive` when `g_FireFail(0x90690)==0` or after 11 frames).
Not specified further here (likely; enemies/effects spec).

---------------------------------------------------------------------------------------------------

## 5. Projectiles_Update 0x415e4 (was Projectiles_UpdateGuided; verified)

Called once per frame (0x1dad9) when `g_ProjCount != 0 && g_907fc == 0 && g_9099c == 0`.

### 5.1 Frame prologue

```c
if (g_PingPongStep == 0) { g_PingPongStep = 1; g_PingPong = 0; }       /* 0x90134, 0x90138 */
g_PingPong += g_PingPongStep;
if (g_PingPong == 0 || g_PingPong == 4) g_PingPongStep = -g_PingPongStep;   /* 0..4 triangle */
for (g_LoopI = g_ProjCount - 1; g_LoopI >= 0; g_LoopI--) { ... 5.2 .. 5.6 ... }
```

### 5.2 Load and draw (uses the position before this frame's move)

```c
i = g_LoopI;
g_CurX = x[i]; g_CurY = y[i]; g_CurVX = vx[i]; g_CurVY = vy[i]; g_CurFrame = frame[i];
g_CurArm = k1[i]; g_CurKind = k0[i]; g_CurFlight = k3[i]; g_CurMotor = k2[i];
g_CurFlags = k5[i]; g_CurDetonate = k4[i];
if (IsOnScreen(g_CamX, g_CamY, g_CurX, g_CurY)) {
    if (g_CurKind != 10) {
        if (blastA[i] != -50) {
            g_ProjSprite = g_ProjSpriteBase[g_CurKind] + g_CurFrame;    /* 0x9098c, 0x92674 */
            if (g_ProjSprite < 50) g_ProjSprite = 0x70;
            Sprite_Queue(g_CurX - g_CamX, g_CurY - g_CamY, g_ProjSprite);
        } else Sprite_Queue(g_CurX - g_CamX, g_CurY - g_CamY, Rand(1) + 0x204);   /* water bomb */
    } else {
        if (blastA[i] == 0) blastA[i] = 0x18e;
        Sprite_Queue(g_CurX - g_CamX, g_CurY - g_CamY, blastA[i]);
    }
}
g_CurSpeed = 0;
```

### 5.3 Motor (only when armed: `g_CurArm == 0 || g_CurFlight == 9`; otherwise `g_CurArm--` and skip to 5.5)

```c
if (g_CurMotor == 1) {
    if (IsOnScreen(g_CamX, g_CamY, g_CurX, g_CurY) && g_90af4 == 1)
        g_900fc = Smoke_Stub(g_CurX + 16, g_CurY + 16, 0x45, g_900fc, Rand(2));
              /* 0x26b03 returns its 4th argument: smoke trails are disabled, but Rand(2) IS consumed */
    m = max(abs(g_CurVX), abs(g_CurVY));
    g_CurSpeed = (2*m > 48) ? 48 : 2*m;
}
if (g_CurMotor == 2) g_CurSpeed = 32;
```

### 5.4 Flight routines (sequential `if`s on g_CurFlight, in this order)

| k3 | Calls | Used by |
|---|---|---|
| 0 | `Projectile_HomeAir` 0x4341a | AAMs, Air Mines |
| 1 | — (ballistic) | bombs, rockets |
| 2 | `Projectile_AimMarker` 0x42d0e, then if `g_MP_TargetMarker` (0x91672, uint16) > 0: `Projectile_Steer` 0x42ec9 | HARM |
| 3 | `Projectile_Alarm` 0x184cf | ALARM |
| 4, 6 | `Projectile_LgbCluster` 0x425c2 | cluster bombs (4), LGBs (6) |
| 5 | `Projectile_AimMissionTarget` 0x42509, `Projectile_Steer` 0x42ec9 | AT missiles, Porcupine |
| 7 | `Projectile_SeaSkim` 0x438f2 | anti-ship |
| 8 | inline brake: `g_CurFrame = Sign(g_CurVX) + 6; t = (abs(g_CurVX) < 2) ? abs(g_CurVX) : 2; g_CurVX -= Sign(g_CurVX) * t; g_CurVY = g_CurSpeed;` | B.Buster, Durandal |
| 9 | `Projectile_Climb` 0x42dc6, then if `g_CurFrame > 3`: 0x42ec9 | (no shipped weapon) |
| 10 | inline: `g_CurTileAttr = Map_GetTileAttr(Clamp(g_CurX/16, 0, g_MapWidth-1), Clamp(g_CurY/16 + 3, 0, 63), 0); if (> 0x7e) { g_CurDetonate = 1; life[i] = 0; g_CurArm = 0; }` | FA Bomblet (proximity fuse 3 tiles) |
| 11 | 0x42509, 0x42ec9, 0x425c2 | AGM-130 |
| 12 | `if (g_LockTarget > -1 && (g_90348 > 0 || g_90708 > 0 || g_906dc > 0)) 0x4341a; else { 0x42509; 0x42ec9; }` | Combi |
| 13, 14 | `Projectile_Cruise` 0x43d05 | cruise missiles |

Each k3 test is a separate `if` in the order 0, 2, 3, 4|6, 5, 7, 8, 9, 10, 11, 12, 13|14, re-reading
`g_CurFlight` each time. Routines that switch to 4 (0x42ec9, 0x438f2, 0x43d05 when `k4 == 2 && vy > 4`)
are all tested after the `4|6` test, so the cluster opening happens on the next frame; but 0x42d0e/0x42ec9
called from k3 2 and the 12 → 0x42ec9 path behave the same. Keep the order exactly.

### 5.5 Frame by kind, movement, collision

```c
if (g_CurKind == 0 || g_CurKind == 5) g_CurFrame = g_CurVY/8 + (g_CurVX > 0 ? 3 : 0);
if (g_CurKind == 3) { g_CurFrame++; if (g_CurFrame >= 4 || g_CurFrame == 0) g_CurFrame = 0; }
if (g_CurKind == 7) Projectile_Parachute();      /* 0x433c1 */
if (g_CurKind == 10) Projectile_Commando();      /* 0x43207 */
g_ProjStepX = g_CurVX;  g_ProjStepY = g_CurVY;  g_CurTileAttr = 0;
if (g_CurX > -999) {
    while (g_ProjStepX != 0 || g_ProjStepY != 0) {           /* sub-steps of at most 16 px */
        g_CurX += Clamp(g_ProjStepX, -16, 16);
        g_CurY += Clamp(g_ProjStepY, -16, 16);
        g_CurTileAttr = 0;
        col = Clamp((g_CurX + 16) / 16, 0, g_MapWidth - 1);
        if (Byte_Get(g_MapVal, col + 0x400) <= (g_CurY + 16) / 16) {      /* at/below the ground line */
            g_CurTileAttr = Map_GetTileAttr(col, Clamp((g_CurY + 16)/16, 0, 63), 0);
            if (g_CurTileAttr > 0x7e) Projectile_HitGround();               /* 0x413a0 */
        }
        g_ProjStepX -= Sign(g_ProjStepX) * min(abs(g_ProjStepX), 16);
        g_ProjStepY -= Sign(g_ProjStepY) * min(abs(g_ProjStepY), 16);
    }
}
```

(The loop keeps stepping even after HitGround set `g_CurX = -999` when it does not zero the steps; the
explosion branch of HitGround zeroes both.)

### 5.6 Life, gravity, fuses, write-back, removal

```c
if (g_CurArm <= 0) life[i]--;
if (life[i] <= 0 || (g_CurArm > 0 && g_CurFlight != 9)) {     /* gravity */
    g_CurVY = min(g_CurVY + 1, 16);
    if (g_CurArm <= 0) life[i] = 0;
}
if (g_CurDetonate == 1 && life[i] <= 0 && g_CurArm <= 0) {   /* timed fuse */
    g_ExplSfxHold = 0;
    Explosion_Damage(g_CurX, g_CurY, g_CurVX, g_CurVY, blastA[i], blastB[i]);
    g_CurX = -999;
}
if (g_CurY > 0x3d4 && g_CurTileAttr > 0x7e && g_CurFlags == 2) {   /* penetrator */
    Explosion_Damage(g_CurX, min(g_CurY + 32, 0x3ef), g_CurVX, 0, blastA[i]*5, blastB[i]*5);
    g_CurX = -999;
}
if (g_CurTileAttr > 0x7e && g_CurX > -999) {
    if (g_CurTileAttr == 0x82 && g_CurKind == 3 && g_CurVY != 0) {   /* skip bomb on water */
        if (g_CurVY > 0) {
            t = g_CurVY - Rand(1);
            g_CurVY = (t < 0) ? 0 : g_CurVY - Rand(1);      /* second, independent Rand(1) */
            g_CurVY = -g_CurVY;
            g_CurY = min(g_CurY, 999);
        }
    } else {
        g_ExplSfxHold = 0;
        Explosion_Damage(g_CurX, g_CurY, g_CurVX, g_CurVY, blastA[i], blastB[i]);
        g_CurX = -999;  g_CurY = -999;
    }
}
if (g_CurY > 1000) {
    g_ExplSfxHold = 0;
    Explosion_Damage(g_CurX, g_CurY, g_CurVX, 0, blastA[i], blastB[i]);
    g_CurX = -999;
}
k5[i] = g_CurFlags; k4[i] = g_CurDetonate; k0[i] = g_CurKind; k2[i] = g_CurMotor;
k1[i] = g_CurArm;  k3[i] = g_CurFlight;  frame[i] = g_CurFrame;
x[i] = g_CurX; y[i] = g_CurY; vx[i] = g_CurVX; vy[i] = g_CurVY;
if (x[i] == -999) {                                  /* remove: swap with the last live slot */
    L = g_ProjCount - 1;
    SwapInt(&x[i],&x[L]); SwapInt(&y[i],&y[L]); SwapInt(&vx[i],&vx[L]); SwapInt(&vy[i],&vy[L]);
    SwapInt(&blastA[i],&blastA[L]); SwapInt(&blastB[i],&blastB[L]); SwapInt(&life[i],&life[L]);
    SwapInt(&frame[i],&frame[L]); SwapInt(&k0[i],&k0[L]); SwapInt(&k5[i],&k5[L]);
    SwapInt(&targetX[i],&targetX[L]); SwapInt(&targetY[i],&targetY[L]);
    SwapInt(&k1[i],&k1[L]); SwapInt(&k2[i],&k2[L]);
    SwapInt(&k3[i], &k5[L]);                       /* BUG: k3 of slot i swapped with k5 of slot L */
    SwapInt(&k4[i],&k4[L]);
    g_ProjCount--;
}
```

**Swap bug (Q7, verified at 0x424a0):** after the removal the projectile moved into slot `i` gets
`k3 = k5 of the removed projectile` (its own k3 is lost), and slot L (now dead) holds the other values.
`g_ProjLock` (0x91ca0) is never swapped either (Q8). Both change the behaviour of surviving
projectiles whenever a lower-index projectile dies first. Keep for faithfulness (PORT note: optional fix
`swap(k3[i],k3[L])` and add `lock` to the swap).

### 5.7 Projectile_HitGround 0x413a0 (verified)

```c
if (g_CurY > 0x3d4 && g_CurFlags == 2) {                         /* penetrator hits the ground */
    Explosion_Damage(g_CurX, min(g_CurY + 32, 0x3ef), g_CurVX, 0, blastA[i]*5, blastB[i]*5);
    g_CurX = -999;
}
if (g_CurX <= -999 || g_CurFlags == 2) return;
if (g_CurTileAttr == 0x82 && g_CurKind == 3 && g_CurVY != 0) {   /* skip bomb (any sign of vy) */
    t = g_CurVY - Rand(1);
    g_CurVY = (t < 0) ? 0 : g_CurVY - Rand(1);
    g_CurVY = -g_CurVY;
    g_CurY = min(g_CurY, 0x63);                                    /* 99 (!) — see Q9 */
    return;
}
if (g_CurKind == 10 && g_902e0 < 10) {                            /* commando lands: spawn a soldier */
    g_8ddc0[g_902e0] = g_CurX;  g_8dd98[g_902e0] = g_CurY;
    g_8dd20[g_902e0] = -2;      g_8dd70[g_902e0] = 0;  g_902e0++;  /* soldier arrays (enemies spec) */
    g_ProjStepX = 0;  g_CurX = -999;
    return;
}
g_ExplSfxHold = 0;
Explosion_Damage(g_CurX + 16, g_CurY + 16, g_CurVX, g_CurVY, blastA[i], blastB[i]);
g_CurX = -999;  g_ProjStepX = 0;  g_ProjStepY = 0;
Map_TriggerAtPlayer();                                             /* 0x3e66b (level spec); it writes g_FireFail(0x90690) */
```

Note `g_CurFlags == 2` projectiles that hit ground above y 0x3d4 are **not** stopped here (they keep
going until y > 0x3d4 or y > 1000).

### 5.8 Flight routines (likely, decompile checked against asm for every Rand/clamp/compare)

Common helper expression: **mission target centre** `MTC = (X1 - X0)*8 + X0*16` with
`X0 = g_MP_TargetX0` (0x9164e, uint16), `X1 = g_MP_TargetX1` (0x91650) — the midpoint of the target
column range in pixels; `MTCR = MTC + Rand(32) - Rand(32)` (first Rand added, second subtracted).
`g_90574`/`g_9052c` = designated target x/y set by Game_Run's target arrow code (0x1d860..) and
others (target arrow, `Hud_DrawTargetArrow(camx, camy, g_90574, g_9052c)`); 0 = none.
`g_908c0` is never written (always 0).

**Projectile_AimMarker 0x42d0e (k3 2):**
`if (g_MP_TargetMarker != 0 && X0 != 0 && X0 - g_908c0*8 <= g_CurX && g_CurX <= g_908c0*8 + X1 && X0 < 5000) g_AimX = MTCR;`

**Projectile_AimMissionTarget 0x42509 (k3 5/11/12):**
`if (X0 > 0 && X1 > 0 && g_90574 <= 0 && targetX[i] == 0 && X0 < 5000) { targetX[i] = MTCR; targetY[i] = g_9052c; }`

**Projectile_Steer 0x42ec9 (verified):**

```c
if (g_CurX > -999 && life[i] > 0) {
    if (targetX[i] == 0) { targetX[i] = g_90574 - 8; targetY[i] = g_9052c; }
    else { g_AimX = targetX[i]; g_AimY = targetY[i]; }
    if (g_90574 == 0) { g_AimX = g_CurX; g_AimY = g_CurY; }
    g_AimLocked = 0;
    g_CurVX = Clamp(g_AimX - g_CurX, -g_CurSpeed, g_CurSpeed);
    g_CurVY = Clamp(g_AimY - g_CurY, -g_CurSpeed, g_CurSpeed);
    if (g_CurX < g_AimX + 0x50 && g_AimX - 0x50 < g_CurX && g_AimX > 0) {
        g_AimDirX = Sign(g_AimX - g_CurX);
        g_AimDirY = max(Sign(g_AimY - g_CurY), 0);       /* Sign evaluated twice when >= 0 */
        if (g_CurDetonate == 2 && g_CurVY > 4) g_CurFlight = 4;
        if (g_CurX < g_AimX + 0x14 && g_AimX - 0x14 < g_CurX && g_AimX > 0 && g_90574 > 0)
            { g_AimDirX = 0; g_AimDirY = 1; }
        g_AimLocked = 1;
    } else if (g_AimX > 0) { g_AimDirX = Sign(g_AimX - g_CurX); g_AimDirY = 0; g_CurVY = 0; }
    if (g_90574 == 0) {
        g_AimFrame = g_CurFrame;
        g_CurVX = Sign(g_8fe10[g_CurFrame]) * g_CurSpeed;
        g_CurVY = Sign(g_8fe50[g_AimFrame]) * g_CurSpeed;
    }
    g_AimFrame = HeadingTab(g_AimDirX, g_AimDirY);        /* §2.3: tab[(dx+1)*3 + dy + 1]/2 */
    if (g_CurFrame < 3) g_CurFrame = 8;
    if (g_CurFrame == 3) g_CurFrame = 4;
    if (g_AimFrame == 0) g_AimFrame = 8;
    if (g_CurFrame < g_AimFrame) g_CurFrame++;
    if (g_AimFrame < g_CurFrame) g_CurFrame--;
    g_CurFrame &= 7;
}
```

**Projectile_LgbCluster 0x425c2 (k3 4/6/11; verified):**

```c
if (g_80070 == 0 && g_CurFlight == 6) {                     /* LGB: daytime only (g_80070 = night flag) */
    if (X0 != 0 && X0 - 320 <= g_CurX && g_CurX <= X1 + 320 && X0 < 5000) g_AimX = MTCR;
    if (g_CurX < g_AimX + 320 && g_AimX - 320 < g_CurX && g_AimX > 0 && g_CurX > -999) {
        s = max(g_CurSpeed, 6);  d = g_AimX - g_CurX - 8;
        g_CurVX = min(s, abs(d)) * Sign(d);
    }
}
if (g_CurFlight != 6 && g_CurFlight != 3 && g_CurVY > 4 && g_ProjCount < 21) {   /* cluster opens */
    sw = g_CurFlags - 3;                                     /* submunition weapon (uses g_8ff44) */
    for (g_ProjSprite = 0; g_ProjSprite < 6; g_ProjSprite++) {
        n = g_ProjCount;
        blastA[n] = g_WeaponBlastA[sw]; blastB[n] = g_WeaponBlastB[sw]; life[n] = g_WeaponThrust[sw];
        x[n] = g_CurX - Rand(16) + Rand(16);
        y[n] = g_CurY - Rand(16) + Rand(16);
        frame[n] = 0;
        vxc = CLAMP_REROLL(g_CurVX + Rand(8) - Rand(8), -16, 16);    /* see below; value DISCARDED */
        (void)(vy[n] == vxc);                                   /* BUG: `==` — vx[n] is never set */
        k0..k5[n] = g_WeaponType[sw].k0..k5;
        vy[n] = CLAMP_REROLL(g_CurVY + Rand(4) - Rand(4), -16, 16);
        g_ProjCount = min(g_ProjCount + 1, 20);
    }
    g_CurKind..g_CurFlags = g_WeaponType[sw].k0..k5;           /* the carrier becomes a submunition */
    blastA[i] = g_WeaponBlastA[sw];  blastB[i] = g_WeaponBlastB[sw];  g_CurFrame = 0;
}
```

`CLAMP_REROLL(E, -16, 16)` is the original's macro clamp that **re-evaluates E (and its Rands)** at each
use (verified at 0x42866..0x4290f and 0x429cd..0x42a76):

```c
a = E;                       /* Rands #1,#2 */
m = (a > 16) ? 16 : E;       /* if a <= 16: Rands #3,#4 */
if (m < -16) r = -16;
else { b = E;                /* Rands #5,#6 */
       r = (b > 16) ? 16 : E; } /* if b <= 16: Rands #7,#8 */
```

So each submunition consumes, in order: `Rand(16)` ×4 (x, y), then 4..8 × `Rand(8)` (vx, discarded),
then 4..8 × `Rand(4)` (vy). Bomblets therefore keep whatever `vx[n]` the slot held before (Q10).
The count clamp at 20 means that with ≥ 20 live projectiles further bomblets overwrite slot 20 (Q11).

**Projectile_Alarm 0x184cf (k3 3):**

```c
if (g_MP_TargetMarker != 0 && X0 != 0 && X0 - g_908c0*8 <= g_CurX && g_CurX <= g_908c0*8 + X1 && X0 < 5000)
    g_AimX = MTCR;
if (targetX[i] == 0) { targetX[i] = g_90574; targetY[i] = g_9052c; }
else { g_AimX = targetX[i]; g_AimY = targetY[i]; }
if (life[i] < 1) {
    if (g_AimX == 0) { g_CurFrame = 6; g_CurVX = 0; g_CurVY = 1; }
    else { Projectile_LgbSteer(); g_CurFrame = Sign(g_CurVX) + 6; }      /* 0x182cb */
} else { g_CurVX = 0; g_CurVY = -g_CurSpeed; g_CurFrame = 2; }        /* climbs while it has fuel */
```

`Projectile_LgbSteer 0x182cb` = the LGB block of 0x425c2 with the in-range test using `g_90574 > 0`
instead of `g_AimX > 0` (likely).

**Projectile_Climb 0x42dc6 (k3 9, no shipped weapon):**
`if (g_CurFrame < 4) { s = max(32 - g_CurArm, 1); g_CurSpeed = s; g_CurVX = 0; g_CurVY = -s; g_CurArm = max(g_CurArm - 1, 0); if (s > 31) { g_CurFrame++; g_CurSpeed = 32; } } else { g_90574 = Rand(g_901a8 - g_90180) + g_90180; g_9052c = 1999; targetX[i] = g_90574; targetY[i] = g_9052c; }`
(0x90180/0x901a8 = runway/base x range; it overwrites the global designated target.)

**Projectile_HomeAir 0x4341a (k3 0, and 12; verified):**

```c
if (g_90708 == 0 && g_906dc == 0 && g_90348 == 0) return;   /* no air enemies, no big target, no ground enemies */
for (j = 0; j < g_90708; j++)                                 /* g_90724 */
    if (BoxOverlap(g_CurX, g_CurY, ax[j], ay[j], 0x20, 0x20)) {
        Explosion_Damage(ax[j], ay[j], 0, 0, blastA[i], blastB[i]); g_CurX = -999; }
if (g_906dc != 0 && g_9073c - 0x40 < g_CurX && g_CurX < g_9073c + 0x40
    && g_90740 - 0x20 < g_CurY && g_CurY < g_90740 + 0x20) {
    Explosion_Damage(g_CurX, g_CurY, 0, 0, blastA[i], blastB[i]);
    Particle_Spawn(g_CurX << 8, g_CurY << 8, 0, 0, 0, 0x20, 0);
    g_CurX = -999;
}
for (j = 0; j < g_90348; j++)
    if (BoxOverlap(g_CurX, g_CurY, gx[j], gy[j], 0x20, 0x20)) {      /* 0x8d9d8/0x8d9e8 */
        Explosion_Damage(gx[j], gy[j], 0, 0, blastA[i], blastB[i]); g_CurX = -999; }
if (g_CurX > -999 && k0[i] != 0) {                     /* array k0, not g_CurKind */
    t = g_ProjLock[i] - 1;
    if (t == 1) { g_ProjLock[i] = (g_LockTarget > -1) ? g_LockTarget + 1 : g_90764*4 + 1; return; }
    if (ax[t] < 1 && t < 2) {                          /* target gone (x <= 0): self-destruct; t may be -1 */
        Explosion_Damage(g_CurX, g_CurY, 0, 0, blastA[i], blastB[i]); g_CurX = -999; return; }
    if (t > 2) { g_AimDirX = Sign(g_8d9cc[t] - g_CurX); g_AimDirY = Sign(g_92660[t] - g_CurY); }
                                                       /* sic: x from the ground table, y from the AIR table */
    else       { g_AimDirX = Sign(g_9073c - g_CurX);   g_AimDirY = Sign(g_90740 - g_CurY); }
                                                       /* t = 0 or 2 home on the BIG target */
    d = HeadingTab(g_AimDirX, g_AimDirY);  f = g_CurFrame;
    if ((d < f && f - 4 < d) || (d > f && f + 4 < d)) f--;
    if ((d > f && f + 4 > d) || (d < f && f - 4 > d)) f++;
    g_CurFrame = (f + 8) % 8;
    g_CurVX = Sign(g_Dir16X[2*g_CurFrame]) * g_CurSpeed;
    g_CurVY = Sign(g_Dir16Y[2*g_CurFrame]) * g_CurSpeed;
}
```

(the decrement test uses the old f, the increment test the already-decremented f.) Q12: the target
selection logic is inconsistent (t==1 re-acquires, t==0 homes on the big target, ground targets mix
tables). Keep.

**Projectile_SeaSkim 0x438f2 (k3 7):** as Projectile_Steer with: aim update
`if (X0 != 0 && g_MP_TargetFlag == 1 && X0 < 5000) g_AimX = MTCR;` (g_MP_TargetFlag = mission param,
level spec); `targetY` is the constant **900**; inside ±0x50: `g_AimDirY = (g_CurX < 0x3ac)` (**sic, x**,
verified at 0x43a38 — the other two branches use `g_CurY < 0x3ac`), the ±0x14 test has no `g_90574`
condition, and sets `g_AimY = 0x3e2`; when `g_AimX < 1`: `g_AimDirX = Rand(2) - 1`,
`g_AimDirY = (g_CurY < 0x3ac)`; else `g_AimDirX = Sign(g_AimX - g_CurX)`, `g_AimDirY = (g_CurY < 0x3ac)`.
Same frame stepping as Steer. Then: if `!g_AimLocked`: velocity from `g_Dir16X/Y[2*frame]` signs ×
speed; else `vx = min(speed, |dx|) * Sign(dx)`, `vy = min(speed, |dy|) * Sign(dy)` towards (g_AimX, g_AimY).

**Projectile_Cruise 0x43d05 (k3 13/14; verified for the parts marked):**

```c
if (X0 == 0 || X0 >= 2000) {
    if (g_CurFlight == 14 && g_MP_EnemyBaseCol != 0) {           /* 0x9167c */
        g_AimX = g_MP_EnemyBaseCol*16 - 64;  TERRAIN();  g_AimY = g_AimLocked << 4;
        if (abs(g_AimX - g_CurX) < 60) g_AimY = 0x3e2;
    } else { g_AimX = 8000; g_AimY = 0; }
} else {
    g_AimX = MTC;  TERRAIN();  g_AimY = g_AimLocked << 4;
    if (abs(g_AimX - g_CurX) < 60) g_9052c = 0x3e2;              /* sic: writes the designator y */
}
/* TERRAIN(): g_AimLocked = 59; for (k = 2; k < 6; k++) { h = Clamp(Byte_Get(g_MapVal,
      k*Sign(g_AimX - g_CurX) + g_CurX/16 + 0x400) - 4, 0, 63); if (h < g_AimLocked) g_AimLocked = h; }
   (the min is written as the macro and re-reads the byte; no Rand, so no difference) */
if (g_CurX > -999 && life[i] > 0) {
    if (g_AimX - 0x80 > g_CurY) life[i]++;                       /* sic: compares Y (verified 0x44029) */
    g_AimLocked = 0;  targetX[i] = g_AimX;  targetY[i] = g_AimY;
    if (g_CurX < g_AimX + 0x78 && g_AimX - 0x78 < g_CurX && g_90574 > 0) {
        g_AimLocked = 1;
        if (g_CurDetonate == 2 && g_CurVY > 4) g_CurFlight = 4;
        if (g_CurX < g_AimX + 0x14 && g_AimX - 0x14 < g_CurX && g_AimX > 0) g_AimY = 0x3e2;
    }
    g_CurVX = min(g_CurSpeed/2, abs(g_AimX - g_CurX)) * Sign(g_AimX - g_CurX);
    g_CurVY = min(g_AimLocked*18 + 4, abs(g_AimY - g_CurY)) * Sign(g_AimY - g_CurY);
    g_CurFrame = g_8d8c0or8d4[(g_CurVX > 0)][Clamp(g_CurVY/16, -2, 2) + 2] - g_ProjSpriteBase[g_CurKind];
                 /* *(int*)(0x8d8c8 + 4*clamp + 0x14*(vx>0)): GENDAT3 sprite pair table 0x8d8c0/0x8d8d4 */
}
```

Q13: the fuel refill compares the aim **x** with the projectile **y**: practically the missile never
runs out of fuel while its target is more than ~1100 px from the map's left edge.

**Projectile_Parachute 0x433c1 (k0 7):** `if (g_CurVY > 8) g_CurFrame = 1; if (g_CurFrame == 1) g_CurVY = min(g_CurVY, 4);`

**Projectile_Commando 0x43207 (k0 10; verified):**

```c
if (g_CurFrame <= 9) {                                        /* climbing out of the aircraft */
    vx[i] = 0; vy[i] = 0;                                     /* arrays only: overwritten by write-back */
    g_LaunchX = g_90520 + 0x34 + g_CurFrame*2;  g_LaunchY = g_904c0 + 9;   /* staging globals (sic) */
    blastA[i] = g_CurFrame % 3 + 0x1da;  g_CurFrame++;
} else {
    if (g_CurFrame < 0x11 && g_CurY > 0x80) g_CurFrame++;
    g_CurVY = vy[i];  vy[i] = min(vy[i] + 1, 16);
    if (g_CurFrame == 0x11) g_CurVY = 4;                      /* chute open */
    blastA[i] = g_90cd0[g_CurFrame];                          /* sprite table (GENDAT3 0x90cd0..) */
    if (blastA[i] == 0x35) blastA[i] += g_90ce4[g_PingPong];
    if (g_CurFrame < 0xe) { blastA[i] = g_9013c/2 + 0x18e; g_9013c = Wrap(g_9013c + 1, 0, 7); }
}
```

For the commando `blastA` is the sprite id (it never explodes with power: on landing it becomes a
soldier, §5.7).

---------------------------------------------------------------------------------------------------

## 6. Bullets (verified)

* **Bullets_Clear 0x2688f**: `g_BulletCount = 0; g_Bullet93220 = 0;`
* **Bullet_Add 0x268d9** `(int x8, int y8, int vx, int vy)`: if `g_BulletCount < 32` append and
  `g_BulletCount++` (silently dropped otherwise).
* **Bullets_Update 0x2694b** `(int camx, int camy)`:

```c
for (i = 0; i < g_BulletCount; i++) {
    b = &g_Bullets[i];
    b->x += b->vx << 5;  b->y += b->vy << 5;
    if (b->vy != 0) b->vy += Sign(b->vy) * 6;                 /* |vy| grows: tracers curve away */
    sx = (b->x >> 8) - camx + 20;   sy = (b->y >> 8) - camy + 20;
    if (sx < 0 || sx > 320 || sy < 0 || sy > 0xb0
        || (b->y >> 12) >= Byte_Get(g_MapVal, (b->x >> 12) + 0xbd0)) {    /* ground (table at +0xbd0) */
        g_BulletCount--; *b = g_Bullets[g_BulletCount]; i--;            /* rep movsd copy of the last */
        continue;
    }
    sy += g_BackPage / 0x60;                                   /* draw page row offset (video) */
    Video_PutPixel(sx + g_ShakeX, sy + g_ShakeY, 0xff);
}
```

(`>>` here are `sar`. Bullets never damage anything; the gun's damage is the hit roll of §4.3. The
ground test uses the table at `g_MapVal + 0xbd0`, not `+0x400` like projectiles — level spec.)

---------------------------------------------------------------------------------------------------

## 7. Flares

### 7.1 Flare_Release 0x38a61 (was Flares_Update; verified)

```c
if (g_FlaresPending > 0 && g_FlareCount < 2) {
    g_FlaresPending--;
    n = g_FlareCount;
    g_FlareX[n] = PX;  g_FlareY[n] = PY;
    g_FlareLife[n] = Rand(20) + 10;
    g_FlareVX[n] = Clamp(g_90778, -4, 4);  g_FlareVY[n] = Clamp(g_9077c, -4, 4);   /* player velocity */
    g_FlareCount++;
}
```

Callers: Flares_Add (Flare weapon), and the enemy-missile launch code (0x38724 in the enemy aircraft
code and 0x28e68 in Player_DeathAndLanding for the big target): **a pending flare is released
automatically whenever an enemy missile is launched**. Every mission starts with 8 pending flares.

### 7.2 Flare movement and decoy (inside Player_DeathAndLanding 0x28e6d–0x2919a; verified)

```c
for (i = g_FlareCount - 1; i >= 0; i--) {                    /* g_LoopI */
    if (IsOnScreen(g_CamX, g_CamY, g_FlareX[i], g_FlareY[i]))
        Sprite_Queue(g_FlareX[i] - g_CamX, g_FlareY[i] - g_CamY, 0xa0);
    g_FlareX[i] += g_FlareVX[i];
    g_FlareY[i] = min(g_FlareY[i] + g_FlareVY[i], 0x3f0);
    g_FlareVY[i] = (Rand(1) + g_FlareVY[i] > 8) ? 8 : g_FlareVY[i] + Rand(1);   /* 1 or 2 Rand(1) */
    g_FlareLife[i]--;
    for (j = 0; j < g_906fc; j++)                             /* enemy missiles (0x8fab8 x, 0x8fac8 y) */
        if (BoxOverlap(g_8fab8[j], g_8fac8[j], g_FlareX[i], g_FlareY[i], 0x40, 0x40))
            g_8fa58[j] = min(g_8fa58[j], 1);                  /* missile guidance mode -> 1 (decoyed) */
    if (g_FlareLife[i] <= 0) { swap all five arrays with [g_FlareCount-1]; g_FlareCount--; }
}
```

---------------------------------------------------------------------------------------------------

## 8. Explosions

### 8.1 Explosion_Damage 0x39b69 `(int x, int y, int vx, int vy, int power, int powerB)` (verified)

`vx`, `vy` (args 3, 4) are **not used**. Globals: `g_ExplX` 0x933a8, `g_ExplY` 0x933ac, `g_ExplPower`
0x933a0, `g_ExplPowerB` 0x933a4.

```c
g_ExplX = x; g_ExplY = y; g_ExplPower = power; g_ExplPowerB = powerB;
if (g_ExplPower == -40) { Explosion_Nuke(); return; }          /* 0x3b261: empty function */
if (g_ExplPower == -20) { g_MarkerX = g_ExplX; g_MarkerY = g_ExplY; }       /* 0x9037c, 0x90380 */
if (g_ExplPower == -30) { g_DropZoneX = g_ExplX; g_DropZoneY = g_ExplY; }   /* 0x90398, 0x903a0 */
if (g_NightMission > 0 && g_ExplPowerB > 99)
    g_9031c = min((g_ExplPowerB/1000)*10 + 1, 21);           /* night palette flash row (Pal_CycleEffects) */
g_ExplSfxPower = min(g_ExplPower, 4000);                       /* 0x90210 */
if (g_ExplSfxPower >= 100) {
    if (g_ExplSfxHold < 5) Sfx_Play(Rand(1)*0x17 + 4, 8000 - g_ExplSfxPower/200, 0x20, g_ExplX);
    if (g_ExplSfxPower >= 500 && g_ExplSfxHold < 5) {
        Sfx_Play(6, 12000 - g_ExplSfxPower/10, 0x20, g_ExplX);  g_ExplSfxHold = 2; }
}
g_ExplCraters = min(g_ExplPower/150 + g_ExplPower/500 + 1, 5);              /* 0x9029c */
if (Map_GetTileAttr(g_ExplX/16, g_ExplY/16, 0) == 0x82) g_ExplCraters = 1;  /* water */
g_ExplX0 = g_ExplX;  g_ExplY0 = g_ExplY;                                     /* 0x9093c, 0x90950 */
g_ExplX = g_ExplX - (g_ExplCraters/2)*16 + 16;
g_ExplI = 1;                                                                 /* 0x90868 */
do {
    g_ExplX = Clamp(g_ExplX + 16, 0, g_MapWidth*16 - 1);
    if (g_ExplY >= 0) {
        g_ExplY = Clamp(g_ExplY0 - (Rand(2)*8 - 8), 0, 0x3f0);
        if (g_ExplX == g_ExplX0) g_ExplY = g_ExplY0;
        Map_CraterAt(g_ExplX, g_ExplY, g_ExplPower);                         /* 0x3b777 level spec */
        if (g_ExplX >= g_90180 && g_ExplX < g_901a8 && 0x3e0 + g_901ac <= g_ExplY)
            g_BaseHit = 1;                                                    /* 0x904ac: own base bombed */
    } else {
        g_ExplY = g_ExplY0 - (Rand(2)*8 - 64);
        if (g_ExplX == g_ExplX0) g_ExplY = g_ExplY0;
    }
    if (g_CamX - 64 < g_ExplX && g_ExplX < g_CamX + 0x160 && g_CamY - 64 < g_ExplY && g_ExplY < g_CamY + 0x100)
        Explosion_Terrain();                                                  /* §8.2 */
    g_ExplI++;
} while (g_ExplI < g_ExplCraters);
```

So the effect runs `max(1, n-1)` times, the first at `x0 - (n/2)*16 + 32`, stepping +16 (quirk Q14:
with n = 1 the only crater is 32 px right of the impact; `x == x0` is reached only for n ≥ 4).

Damage pass, with `CX = Clamp(g_ExplX0, 0, g_MapWidth*16 - 1)` (0x8fec4), `CY = min(g_ExplY0, 0x3f0)`
(0x8fecc), `r = g_ExplCraters`:

```c
/* (1) vehicles/convoy (count g_900e0; x 0x8e400, y 0x8e418, state 0x8e3e8) */
for (j = 0; j < g_900e0; j++)
    if (vx[j] - r*12 - 4 < CX && vx[j] + r*12 + 4 > CX && CY - r*16 - 16 <= vy[j] && vstate[j] < 2) {
        vstate[j] = 4;  g_Score[g_90254] += 100; }
/* (2) enemy aircraft (count g_90708) */
for (j = 0; j < g_90708; j++)
    if (ax[j] - 32 < CX && ax[j] + 32 > CX && ay[j] - 32 < CY && ay[j] + 32 > CY) {
        g_8ffcc = -1;
        ahp[j] = ahp[j] - 1 - Rand(g_ExplPower/10);
        if (ahp[j] < 0) { adm[j] -= ahp[j]; ahp[j] = 0; }
        if (adm[j] > 2 && g_8f0f0[j] == 0) { g_8f0f0[j] = 1; g_8f118[j] = ax[j]; g_8f128[j] = ay[j]; }
    }
/* (3) ground enemies (count g_90348; x 0x8d9d8, y 0x8d9e8, hp 0x8d9a8, dmg 0x8d998) */
for (j = 0; j < g_90348; j++)
    if (gx[j] - 32 < CX && gx[j] + 32 > CX && gy[j] - 32 < CY && gy[j] + 32 > CY) {
        e = (g_GunRayHit > 0 && PY > gy[j]) ? 2 : 0;
        ghp[j] = ghp[j] - 1 - Rand(g_ExplPower/10 + e);
        if (ghp[j] < 0) { gdm[j] -= ghp[j]; ghp[j] = 0; }
    }
/* (4) big target */
if (g_906dc > 0 && g_9073c - 0x50 < CX && g_9073c + 0x50 > CX && g_9073c - 0x20 < CY && g_90740 + 0x20 > CY) {
    g_9072c = g_9072c - 1 - Rand(g_ExplPower/10);       /* sic: y low bound uses the x coordinate 0x9073c */
    if (g_9072c < 0) g_90728 = 1;
}
/* (5),(6),(7) three single structures at tile (c,r): (g_90230,g_90234) +3000, (g_9063c,g_90640) +1000,
   (g_900e8,g_900ec) +2000 points */
for each (c, r, pts):
    if (c > 0 && (( c*16 - R*12 <= CX && c*16 + R*12 >= CX && r*16 - R*16 <= CY && r*16 + R*16 >= CY
                    && g_ExplPower > 15)
                  || Map_GetTileAttr(c, r + 1, 0) < 0x7f)) {      /* in range, or its support is gone */
        g_8dfd0[g_902fc] = c*16;  g_8e050[g_902fc] = r*16;  g_8df50[g_902fc] = 500;  g_902fc++;   /* fire */
        c = 0;  g_Score[g_90254] += pts;
    }
/* (8) objects (count g_905ac; x 0x91f74, y 0x91f98, hp 0x91928, state 0x91f2c) */
for (j = 0; j < g_905ac; j++)
    if (ox[j] - r*12 < CX && ox[j] + r*12 >= CX && CY - r*16 <= oy[j] && CY + r*16 >= oy[j] && ostate[j] < 4) {
        ohp[j] -= Rand(min(g_ExplPower/50, 6) + g_ExplPower/300 + 2);
        if (ohp[j] < 0) { ostate[j] -= ohp[j]; ohp[j] = 0;
            if (g_GunRayHit > 0 && g_StrafeCount > 8 && g_90194 == 0 && Rand(5) == 1) {
                g_90194 = 1;  Sfx_Play(0x14, 4000, 0x20, PX);  g_ExplSfxHold = 15; } }
    }
```

`R` in (5)-(7) is `g_ExplCraters` too. `Rand(n)` with negative `n` (marker powers -20/-30 → n = -2/-3)
is `rand() % (n+1)`: `% -1` = 0, `% -2` ∈ {-1,0,1} (C99 truncation; Watcom identical). Scores go to
`g_Score[g_90254]` (0x8e330, current player in Aerolimits). The meaning of the arrays (convoy
vehicles, ground enemies, structures, objects) belongs to the enemies/level specs; this function only
applies the damage. `g_8ffcc = -1` is an enemy-AI flag (enemies spec).

### 8.2 Explosion_Terrain 0x3a99f (verified)

```c
centre = (g_ExplX == g_ExplX0);
big = (g_ExplPower > 500);
s = Sign(Rand(big*2 + 3) / 3) * centre;                                       /* Rand #1 always */
g_ExplSolid = (s != 0 && Map_GetTileAttr(g_ExplX/16, max(g_ExplY/16, 1), 3) != 0);   /* 0x90294 */
g_ExplDebrisMode = (g_ExplPower < 30);                                        /* 0x8fea4 */
if (g_ExplDebrisMode == 1) g_ExplSolid = 0;
g_ExplDebrisMode += g_ExplSolid * 2;
g_ExplDX8 = (g_ExplX - g_ExplX0) / 8;        g_ExplSpread = abs(g_ExplDX8) + 8;   /* 0x90278, 0x9027c */
g_8ff44 = (Map_GetTileAttr(g_ExplX/16, max(g_ExplY/16, 1), 0) == 0x82);
if (g_8ff44) g_ExplDebrisMode = 3;                                           /* water */
if (g_ExplVY >= 0) {                                                          /* 0x90928 */
    A = abs(g_ExplVX) / 4;  k = g_ExplPower / (8 - 3*(g_ExplI > 4));
    a = A - Rand(k);  m = (a > 0) ? 0 : A - Rand(k);
    if (m < -16) g_ExplVY = -16;
    else { b = A - Rand(k); g_ExplVY = (b > 0) ? 0 : A - Rand(k); }
}
switch (g_ExplDebrisMode) { case 0: Explosion_DebrisGround(); break;   /* 0x3afc0 */
                            case 1: Explosion_DebrisSmall();  break;   /* 0x3af32 */
                            case 2: Explosion_DebrisSolid();  break;   /* 0x3b18d */
                            case 3: Explosion_Particles();    break; } /* 0x3adf9 */
g_907b8 = 0;
```

`g_ExplVX` 0x90924 is **never written** anywhere in the image (verified by a scan of every
`mov [0x90924],…`), so it is 0; `g_ExplVY` 0x90928 is only written here: once it becomes negative it is
never recomputed (Q15). The debris launch speed is therefore decided by the first explosions of the
session. Probably `Explosion_Damage`'s vx/vy args were meant to be stored there. Keep.

### 8.3 Debris emitters (verified unless noted)

`Particle_Spawn(x8, y8, vx, vy, a5, a6, a7)` (0x26560; particle semantics in the effects spec).

* **Explosion_DebrisSmall 0x3af32** (power < 30):
  `vxc = Clamp(g_ExplDX8 + g_ExplVX/4 + Rand(4) - Rand(4), -16, 16);`
  `Particle_Spawn(g_ExplX<<8, (g_ExplY-16)<<8, vxc<<8, g_ExplVY<<8, 0x20, 0x10, 2);`
* **Explosion_DebrisGround 0x3afc0** (normal):
  `vxc = Clamp(g_ExplDX8 + g_ExplVX/4 + Rand(4) - Rand(4), -16, 16);`
  `Particle_Spawn(g_ExplX<<8, (g_ExplY-16)<<8, vxc*0x55, (g_ExplVY - g_ExplSpread)*0x55, 0x80, 0x10, 0);`
  `n = Rand(1); for (j = 0; j < n; j++) { k = Rand(4); g_8fea4 = g_90cc4[k]; s = 256 / (g_90c84[k] + j + 3);`
  `  vy = (g_ExplVY - Rand(2)) * s; vxc = Clamp(g_ExplDX8 + g_ExplVX/4 + Rand(4) - Rand(4), -16, 16) * s;`
  `  yy = (g_ExplY - 16 - Rand(16)) << 8; Particle_Spawn(g_ExplX<<8, yy, vxc, vy, s/4, 0x10, g_8fea4); }`
  (Rand order per j: 4, 2, 4, 4, 16. Tables 0x90cc4/0x90c84 from GENDAT3.)
* **Explosion_DebrisSolid 0x3b18d** (hit a building tile; likely):
  `Particle_Spawn(g_ExplX<<8, (g_ExplY-24)<<8, 0, 0, 0, 0x10, 3); n = Rand(2);`
  `for (j = 0; j <= n; j++) { t = Rand(6); Particle_Spawn(g_ExplX<<8, (g_ExplY-32)<<8, g_8ded8[t]<<9, g_8df00[t]<<9, 0, 0x10, t+4); }`
* **Explosion_Particles 0x3adf9** (water; likely):
  `if (g_ExplPower < 30) Particle_Spawn(g_ExplX<<8, (g_ExplY-16)<<8, 0, g_ExplVY<<7, 0, 0x10, 0x10);`
  `else Particle_Spawn(g_ExplX<<8, min((g_ExplY/16)*16 + 1, 0x3e1)<<8, 0, 0, 0, 0x40, 0xc);`
  `g_90114 = g_ExplX; if (Rand(20) == 1) Particle_Spawn(g_ExplX<<8, (g_ExplY-16)<<8, 0x200, -0x200, 0x40, 0x20, 0x13);`
  `if (Rand(100) == 1) Particle_Spawn(g_ExplX<<8, (g_ExplY-16)<<8, 0, 0, 0, 0x20, 0x14);`

### 8.4 Other callers of Explosion_Damage

About 60 call sites in enemy/flak/level code (0x14b3b … 0x43742) use the same function with their own
powers (e.g. collisions, enemy bombs, turret shells). They are specified with their modules.

---------------------------------------------------------------------------------------------------

## 9. Scoring and HUD summary

* Weapon effects score only in Explosion_Damage: +100 per vehicle destroyed, +3000/+1000/+2000 for the
  three structures. Aircraft/ground-enemy kills are scored by their own update code when the damage
  counters written here pass their thresholds (enemies spec).
* HUD numbers drawn here: rack counts (Weapon_Fire, x 0x54, y 0x20/0x32, 3 digits) and gun ammo
  (Player_Weapons, x 0x55 y 0x0a, 4 digits), both over a colour-0x1d rectangle, suppressed by `g_8fa38`
  (rack counts also by Aerolimits). The icons and the selection screen counts are WeaponSelect_DrawCounts
  0x2ba3c / Hud_DrawPanel 0x25c8b (game_flow / player.md).
* Messages: "Nuclear Psychopath Warning!" (0x81358), "Following Camera Pod" (0x81375), "Following Camere
  Pod" (0x8138b) via Hud_PushMessage.

---------------------------------------------------------------------------------------------------

## 10. Quirks and bugs (recommendation in brackets; the user decides)

* **Q1** Two Weapon_Fire calls per accepted press; only the first charges a round, both charge
  weight; ballistic/rocket weapons launch a pair (y and y+4). [keep]
* **Q2** Weapon_TakePhoto, Weapon_ArmJP233 and Flares_Add do not set `g_FireFail`, so Weapon_Fire
  copies k0..k5 into slot `g_ProjCount` and registers it as a live projectile with **stale** position,
  speed, blast and life from whatever last used that slot (a removed projectile: x = -999, removed
  again next frame, but it may re-fire its stale `y > 1000` explosion; a never-used slot: a projectile at
  (0,0) that falls and explodes with power 0). The test `g_FireKindP1 != 12` excludes Porcupine arming
  only (probably meant for the Flare kind 12 = `k0+1 == 13`). [keep; PORT note: guard with a flag if
  the user wants it fixed]
* **Q3** Aerolimits: both racks lose a round on **both** calls. [keep]
* **Q4** Drop tank: weight -100 per call while holding the keys. [keep]
* **Q5** Gun pod firing permanently overwrites `g_GunPower` with the pod's k3 (saved, never restored).
  [keep]
* **Q6** Gun pods have k4 (gun type) 0/1, so `type/2 = 0` hit rolls: pods only damage via the ray when
  nothing is locked. [keep]
* **Q7** Projectile removal swaps `k3[i]` with `k5[last]`. [keep, optional fix]
* **Q8** `g_ProjLock` is not swapped on removal. [keep]
* **Q9** Skip bomb hitting water inside the sub-step loop clamps `y` to **99** (`min(y,0x63)`) while
  the outer check uses 999 (`0x3e7`); the bomb jumps to the top of the map. [verify in DOSBox; keep]
* **Q10** Cluster submunitions: `vy[n] == vx` typo, their vx is stale. [keep]
* **Q11** Cluster spawner clamps the count at 20 (launches at 21). [keep]
* **Q12** Projectile_HomeAir target selection inconsistencies (§5.8). [keep]
* **Q13** Cruise missile refuel compares target x with own y. [keep]
* **Q14** Explosion crater placement offset (+32 px for small blasts). [keep]
* **Q15** `g_ExplVX` never written; `g_ExplVY` sticks once negative. [keep]
* **Q16** Explosion_Damage big-target box uses `0x9073c - 0x20 < CY` (x coordinate as y bound). [keep]
* **Q17** SeaSkim uses `g_CurX < 0x3ac` in one branch (should be y). [keep]
* **Q18** Cruise (k3 13, mission target valid) writes `g_9052c = 0x3e2` (the global designator y)
  instead of `g_AimY`. [keep]
* **Q19** Commando climb-out writes the launch staging globals and the vx/vy arrays, both without
  effect on the commando itself (write-back overwrites the arrays). [keep]
* **Q20** Tac Nuke explosion handler 0x3b261 is empty (stock 0 anyway); smoke trail 0x26b03 is a stub
  that still consumes `Rand(2)`. [keep]
* **Q21** `g_8ff10` (camera follows projectile slot n) is not updated when projectiles are swapped.
  [keep; player.md]
* **Q22** Weapon_FireRocket's camera check after a refused launch looks at a stale slot. [keep]

---------------------------------------------------------------------------------------------------

## 11. Open questions

1. `g_8fa38` (HUD number suppression), `g_8fa50` (gun disabled), `g_90858` (forces alternating tracers),
   `g_90624`, `g_90ca0`, `g_90af4`, `g_8ff14 - g_905dc` exact meanings — owned by player/game_flow.
2. Keyboard repeat: `g_FireKey1/2` are cleared every frame; how often the keyboard ISR sets them again
   while a key is held decides how fast the drop tank hold counter (10 calls = 5 frames) and repeat
   fire work (platform spec).
3. Ray trace 0x4501e / 0x450f8 / 0x4511e and Map_CraterAt 0x3b777 belong to the level spec.
4. The flamer shot updater 0x17c56 (named `Turrets_Update`) should be specified (with this module or
   enemies) — its name looks wrong.
5. Whether `Rand(n)` for n ≤ -2 (marker powers) behaves as described depends on the C library `%`;
   Watcom's `idiv` truncates, as assumed.
6. Q9 (skip bomb y=99) should be confirmed in DOSBox before anyone "fixes" it.

---------------------------------------------------------------------------------------------------

## Corrections (phase 5 step C, checked against the disassembly while porting)

1. §5.4 k3 = 10 (proximity fuse): the row is `Clamp(g_CurY/16 + 3, 0xc, 0x3f)` (lower bound **12**, not 0; 0x41a8b).
2. §8.1 the damage-pass loops use the global `g_ExplI` 0x90868 as their index (not a local `j`); the night flash
   global 0x9031c is `g_FlashCounter` (video.md).
3. §8.3 Explosion_DebrisGround / Explosion_DebrisSolid keep their loop state in globals: count `n` → 0x907f8
   (`g_DamageHits`), `j` → 0x90830, `k` → 0x901c0, `s` → 0x90248, the table value / `t` → 0x8fea4.
4. §5.8 Projectile_Cruise: the TERRAIN loop counter `k` is the global 0x9098c (`g_ProjSprite`); Flamer_Update also
   leaves its chain index there (enemies.md §10.1).
5. §5.1 the projectile loop is a `do { } while (--g_LoopI >= 0)`: the body runs once before the test (the caller's
   `g_ProjCount != 0` guard makes it equivalent).
6. §7.2 the decoy loop over the enemy missiles uses the global 0x90724 as its index; the flare block is guarded by
   `g_FlareCount > 0`.
7. Names only (no behaviour change): in §3.1 `g_90440` is `g_OnGround` and `g_90ca0` is `g_Ctrl[2]` (game_flow.md §8,
   frame.c step 85); `g_80070` (LGB daytime test) is `g_FogActive` / `g_NightPalActive`; `g_MP_TargetFlag` is p22
   (0x91674).
