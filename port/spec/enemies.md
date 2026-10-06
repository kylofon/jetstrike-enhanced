# Enemies and world objects (JS.EXE): enemy aircraft, ground units, missiles, shells, convoys, vehicles, MP2 emplacements, airbase vehicles, crew, pilots, commandos, flamer, particles, bonuses

Binary `work/JS.bin` (flat, base 0x10000), decompile `port/decomp/js.c`. Symbols: `port/spec/enemies_symbols.csv`.
Related specs (not repeated here): `platform.md` (Rand, Clamp, Sign, Wrap, BoxOverlap, IsOnScreen, SwapInt),
`video.md` (Sprite_Queue, Video_ReadPixel, Video_PutPixel), `sound.md` (Sfx_Play ids), `weapons.md` (player
projectiles SoA §2.2, Explosion_Damage §8.1 = **all damage to the objects below is applied there**, gun hit rolls
§4.3, flares §7, Projectiles_Update 0x415e4), `level.md` (MP2 trigger §5, mission params §7, Bertha §8.5,
agent drop / Crate_Update §8.7, Waypoint_Update §8.8, Runway_Update §9.1), `game_flow.md` §8.2 (frame-loop
order and the guards of every updater below; step numbers "GF n" refer to it).

Conventions. Confidence: **verified** (decompile checked against capstone where calls/args were garbled),
**likely**, **guess**. All globals are `int32` unless stated; mission params `pNN` are `uint16`
(`g_MissionParams` 0x91648 + 2*NN, zero-extended). `Rand(n)` = `rand()%(n+1)`. `Div16(v)` = C division by 16
(truncation toward 0; the compiler emits `sar/shl/sbb/sar`). `/8`, `/4`, `/6` likewise truncate toward 0.
`PX = g_CamX + g_PlayerScrX`, `PY = g_CamY + g_PlayerScrY` (player world position). `g_LoopI` 0x90ab8 is
the shared global loop index (its value leaks into callees, e.g. 0x17684, 0x3ea8f, 0x447f3 use it as "current
object"). `g_FrameParity` 0x905a0 toggles every frame (GF 44). "Remove i" always means: swap the listed arrays'
element i with element count-1 via `SwapInt`, then count-- (arrays not listed are NOT swapped — several
quirks come from that).

**Ghidra caveat.** Calls with stack arguments to `Clamp`, `Sign`, `abs`, `Rand`, `Particle_Spawn`,
`Sfx_Play`, `Map_GetTileAttr`, `Sprite_Queue` are frequently shown with wrong argument lists in js.c
(arguments pushed early for an outer call are attributed to an inner one). Every such call in this spec was
re-read in the disassembly; trust the spec over js.c.

**Macro double evaluation.** Watcom expanded the game's `MIN/MAX` macros, so a clamp of an expression that
contains `Rand()` evaluates the expression (and its Rand calls) **again in the taken branch**. This is the
exact RNG behaviour and must be reproduced; it is written as `CLAMPR(expr, lo, hi)` below:

```c
/* CLAMPR(E, lo, hi) as compiled (E re-evaluated, each evaluation consumes its Rand calls):          */
v = (E > hi) ? hi : E;          /* E evaluated once for the test, a 2nd time if it is the result     */
r = (v < lo) ? lo : ((E > hi) ? hi : E);   /* when v >= lo: E evaluated again for the test (+again)  */
```
i.e. 1st: `t1 = E; m = (t1 > hi) ? hi : E'` ; then `if (m < lo) r = lo; else { t3 = E''; r = (t3 > hi) ? hi : E'''; }`
where each `E` is a fresh evaluation. Number of evaluations 2..4.

---------------------------------------------------------------------------------------------------

## 0. Inventory, renames, call sites

| Address | Name (old → new) | Object | Guard in Game_Run (GF step) |
|---|---|---|---|
| 0x36ae3 | EnemyAir_Update | enemy aircraft (≤ 2) | GF 30: `0x90708 != 0` |
| 0x38ba8 | EnemyBomber_Spawn | spawns enemy aircraft | GF 30 raid counter, GF 88, Mission_LoadBriefing |
| 0x35e20 | EnemyGround_Update | ground gunships (≤ 4) | GF 29: `0x90348` |
| 0x19fda | Helis_Update → **EnemyMissiles_Update** | enemy homing missiles (≤ 10) | GF 29: `0x906fc` |
| 0x19cc8 | Targets_Update → **EnemyShells_Update** | enemy flak shells (≤ 10) | GF 51: `0x8ffa0` |
| 0x17c56 | Turrets_Update → **Flamer_Update** | player flamer shot chain (≤ 11) | GF 29: `0x90608` |
| 0x14b4a | Flak_Update → **Commandos_Update** | player's dropped soldiers (≤ 10) | GF 41: `0x902e0` |
| 0x149ae | Building_Update (kept) | emplacement under construction | GF 31: `0x902c0` |
| 0x3d105 | Convoy_Update | convoy vehicles (≤ 9) | GF 31: `0x905ac` |
| 0x44478 | Ships_Update → **TargetVehicles_Update** | vehicles in the target zone (≤ 6) | GF 31: `0x900b4 > 0` |
| 0x4424d | Enemy_InitPositions → **TargetVehicle_Spawn** | spawns one of them | attempt loop (GF §2.3) |
| 0x35a33 | Mines_Update → **EnemyPilots_Update** | ejected enemy pilots | GF 41: `0x90754` |
| 0x2c55b | Balloons_Update → **AirbaseCrew_Update** | 5 walking ground crew | GF 32 (camera near base) |
| 0x2bc0b | Airbase_Update | fuel truck, fire engine, rearm truck, jeep | GF 39 |
| 0x2661e | Particles_Update | 40 sprite particles | GF 36 (always) |
| 0x3b27c | Debris_Update | player-wreck debris | GF 24 (player dead) |
| 0x17072 | Bonus_Update | bonus crate on parachute | GF 37: `0x90890` |
| 0x45859 | Pickup_Update | prize balloon / Aerolimits pickup | GF 37: `0x90918 > 0` |
| 0x3dea2/0x3e8b3 | MP2 class 0x85 draw / fire | gun emplacement | GF 40 |
| 0x3dfaa/0x3ec5b | MP2 class 0x83 draw / fire | flak emplacement | GF 40 |
| 0x3e0b2/0x3ef72 | MP2 class 0x84 draw / fire | SAM site | GF 40 |
| 0x3dc4e | **BaseRepair_Update** | runway repair truck | GF 37: `0x90498` |
| 0x3dd5e | **BaseHit_Losses** | hangar / weapon stores losses | GF 57: `0x901c8` |
| inside Player_DeathAndLanding 0x2780b | (no own function) | big enemy bomber 0x906dc, "Nessie" 0x90158 | §13 |

Not covered here (owned elsewhere, referenced): Waypoint_Update 0x45584, Crate_Update 0x4596f,
AgentDrop_Update 0x15387, AgentDrop_Release 0x17be5, Bertha_Update 0x1af0f (level.md); Projectile_LgbSteer
0x182cb and Projectile_Alarm 0x184cf (weapons.md; live); B52 0x90984 / Hercules ("Fat Albert") 0x90520
friendly support aircraft and the flare update (Player_DeathAndLanding, player spec).

**Range 0x40f5a-0x43d05** (the ~13 "AI/target helpers"): apart from the dead 0x40f5a/0x40f67 (§15) they are
the live guided-projectile helpers owned by weapons.md (0x413a0 Projectile_HitGround, 0x42509 Projectile_AimMissionTarget,
0x425c2 Projectile_LgbCluster, 0x42b70/0x42b7d Projectile_AlarmOld wrapper/body, 0x42d0e Projectile_AimMarker,
0x42dc6 Projectile_Climb, 0x42ec9 Projectile_Steer, 0x43207 Projectile_Commando, 0x433c1 Projectile_Parachute,
0x4341a Projectile_HomeAir, 0x438f2 Projectile_SeaSkim, 0x43d05 Projectile_Cruise); not repeated here.
Turrets_Update 0x17c56 is resolved as Flamer_Update (player flamer chain, §10.1), consistent with weapons.md.

**Dead code** (no caller anywhere; verified by a full scan of call/jmp rel32 targets in 0x10000-0x485d0):
the old projectile updater 0x332fb/0x33308 (weapons.md §1) and its flight helpers in 0x18237-0x19c6f, plus
0x19ab5/0x19ac2, 0x40f5a/0x40f67, 0x3e705/0x3e712. They are named in §15 for orientation; **do not port**.

---------------------------------------------------------------------------------------------------

## 1. Shared tables and globals

```c
/* 0x8fc30 g_Dir16X[16], 0x8fc70 g_Dir16Y[16]: (int)(-cos(i*pi/8)*8), (int)(-sin(i*pi/8)*6) (weapons.md §2.3).
   Indexed [dir] (16 headings) by EnemyAir, [dir*2] (8 headings) by missiles / old helpers.                 */
/* 0x8fe10 g_Dir16Xb[16], 0x8fe50 g_Dir16Yb[16]: (int)(-cos*8), (int)(-sin*3) — "facing" tests             */
/* 0x92980 uint16[9] heading table: frame8 = *(uint16*)(0x92982 + (dx+1)*6 + dy*2) / 2   (dx,dy in -1..1)   */
/* 0x80554.. int32 octant table (verified dump): Oct(dx,dy) = *(int*)(0x80568 + (dy*4+dx)*4)               */
static const int Oct[3][3] = /* [dy+1][dx+1] */ { {1,2,3}, {0,2,4}, {7,6,5} };   /* (0,0) -> 2           */
/* 0x80584 int32 TurnTab[16][8] (verified dump): new 16-heading one step towards octant o                  */
static const int TurnTab[16][8] = {
 {0,1,1,1,1,1,15,15},{0,2,2,2,2,2,0,0},{1,2,3,3,3,3,1,1},{2,2,4,4,4,4,2,2},
 {3,3,4,5,5,5,5,3},{4,4,4,6,6,6,6,4},{5,5,5,6,7,7,7,5},{6,6,6,6,8,8,8,6},
 {7,7,7,7,8,9,9,7},{8,8,8,8,8,10,10,8},{9,9,9,9,9,10,11,9},{10,10,10,10,10,10,12,10},
 {13,13,13,11,11,11,12,13},{14,14,14,14,14,12,12,14},{15,15,15,15,15,15,13,14},{0,0,0,0,0,0,14,14} };
/* FUN_0003cd9e -> Heading_TurnToward(dir, dx, dy) = TurnTab[dir][Oct[dy+1][dx+1]]   (verified)            */
```

Common globals used as scratch by several updaters (keep global, values leak between objects):
`0x9080c/0x90810` cur x/y, `0x909a8/0x909ac` cur x/y (EnemyAir, Convoy; also read by Bonus_Spawn and
0x17944 as the spawn/launch position), `0x90934/0x90938` cur vx/vy, `0x90a24/0x90a28` steer dx/dy
(g_AimDirX/Y), `0x90774` aim frame, `0x9098c`, `0x90830`, `0x90690` (g_Scratch690), `0x90618/0x9061c`
"view camera" (= g_ViewX/Y 0x8ff68/0x8ff70 when either is nonzero (Helis: > 0), else g_CamX/Y).

ENEMIES record (DATA/ENEMIES, 7 bytes, loaded by Mission_LoadBriefing when `g_EnemySetIndex` 0x906d4 =
p08/100+1 changes; 10 records in the shipped file): byte 0 `g_EnemySkill` 0x906e0, 1 `g_EnemyGunDamage`
0x906d8, 2 `g_EnemyMissiles` 0x906f8, 3 `g_EnemyBombs` 0x906b8, 4 `g_EnemyBombWeapon` 0x906f0 (weapon index),
5 `g_EnemyBombRef` 0x906cc (bombing row; ×16 = px), 6 `g_EnemySpxLetter` 0x906e4 (plane/enemy<'a'+n>.spx).
Shipped records (skill,gun,mis,bombs,wpn,ref,letter): 0:(0,0,4,0,0,0,0) 1:(0,0,2,2,10,40,1) 2:(1,1,4,0,0,0,0)
3:(1,1,4,4,10,40,0) 4:(2,1,6,0,0,0,0) 5:(2,1,4,4,13,40,1) 6:(2,2,8,0,0,0,0) 7:(2,2,4,8,13,60,1)
8:(1,1,2,0,0,0,2) 9:(6,4,12,0,0,0,3). Uses: skill → EnemyAir evade/gun/missile rolls (§2.5); gun damage →
`0x907f8 = gunDamage+1` before Player_DamageSystems; missiles/bombs → per-aircraft stocks at spawn;
bombWeapon/bombRef → Enemy_DropBomb (§2.7) and the bombing altitude; letter → Enemy_LoadSpx (§12).

---------------------------------------------------------------------------------------------------

## 2. Enemy aircraft

### 2.1 Arrays (SoA, int32, index 0..1; max 2 entries in practice — EnemyBomber_Spawn count p08 % 100 is
clamped by Mission_LoadBriefing to ≤ 2: `0x90a5c = min(p08, 2)`) (verified)

```c
/* count g_EnemyAirCount 0x90708                                                                      */
int32 ax     [] @0x9264c;  /* world x px; < -998 = dead (removed by the AI pass)                      */
int32 ay     [] @0x92660;  /* world y px                                                              */
int32 adm    [] @0x92638;  /* damage 0..; >1 crippled (smokes, dives), >5 destroyed                   */
int32 adir   [] @0x8fbe0;  /* heading 0..15 (0 = left, 8 = right, 4 = up)                              */
int32 ahp    [] @0x9270c;  /* armour, starts 0, Explosion_Damage/gun subtract and clamp at 0          */
int32 athr   [] @0x92548;  /* throttle 0..9 (may exceed 9, see §2.5 step 13)                          */
int32 aspd   [] @0x9281c;  /* speed 0..8                                                              */
int32 aburn  [] @0x8f0f0;  /* 0 ok; 1..4 burning-wreck animation frame (set by Explosion_Damage/gun) */
int32 awx    [] @0x8f118;  /* wreck explosion x (frozen at the moment of burning)                     */
int32 awy    [] @0x8f128;  /* wreck y, +8 per frame while burning                                     */
int32 amis   [] @0x8fc00;  /* missiles left                                                           */
int32 abomb  [] @0x8e3d8;  /* bombs left (999 = special, see §2.6)                                    */
int32 astate [] @0x8ec78;  /* 0 flying, 0x81 parked on the enemy airbase                              */
int32 aeject [] @0x90b34;  /* 1 = pilot has ejected (never reset on respawn!)                         */
```
Per-aircraft scratch while updating: x 0x909a8, y 0x909ac, dir 0x909fc, thr 0x909f0, spd 0x909d4,
hp 0x909cc, state 0x90990. AI override level `0x90220` (0 free, 1 forced, 2 forced+no evasions).
Wander direction `0x90a08/0x90a0c` (±1, set to -1/-1 at mission start). Lock target `g_LockTarget` 0x90454
(-1 none, 0..1 aircraft, 2 big bomber, ≥3 ground unit+3), consumed by the player's gun/missiles (weapons.md).

### 2.2 EnemyBomber_Spawn @ 0x38ba8 — `void EnemyBomber_Spawn(void)` (verified)

Spawns `0x90a5c` aircraft (callers set it: GF 30 `Rand(2)+1`, GF 88 2, Mission_LoadBriefing `min(p08,2)`).
```c
if (g_GameMode < 3) {
  i = 0;
  do {                                                     /* do-while: runs once even if 0x90a5c == 0 */
    if (g_MP_EnemyAir /*p08*/ == 0) {
      if (g_EnemyBaseX < 1 || 0x90410 >= (g_Mission/10)*3 + 5 || g_GameMode == 1) {
        if (0x906dc == 1 && 0x90730 == 0) {                /* escort of the big bomber (§13.1)         */
          0x90730 = 1;  X = 0x9073c + 0x40;  ay[i] = 0x90740;  astate[i] = 0;
          if (i == 0) Hud_PushMessage("Enemy Bomber Raiding!");
        } else X = -999;                                   /* removed below                            */
      } else {                                             /* scramble from the enemy airbase           */
        X = g_EnemyBaseX*16 - 0x40;  ay[i] = g_EnemyBaseRow*16 - 3;  astate[i] = 0x81;
        while (Map_GetTileAttr(Div16(X), Div16(ay[i] + 0x12), 0) != 0x81 && Div16(ay[i]) < 0x3e)
            ay[i] += 4;                                    /* drop onto the runway (class 0x81)        */
        ay[i] -= 0x14;
        if (i == 0) Hud_PushMessage(HUDTEXT[54] /*0x8b508 "ENEMY AIRCRAFT SCRAMBLING"*/);
      }
    } else {                                               /* air spawn near p11                       */
      X = CLAMPR(p11*16 + Rand(600) - Rand(600), 1, (g_MapWidth-1)*16);   /* Rand order: +first, -second */
      p08 = max(p08 - 1, 0);  ay[i] = -800;  astate[i] = 0;
    }
    adm[i] = 0; ax[i] = X; adir[i] = 0; ahp[i] = 0; athr[i] = 4; aspd[i] = 0; aburn[i] = 0; aeject[i] = 0;
    amis[i] = g_EnemyMissiles; abomb[i] = g_EnemyBombs;
    i++;
  } while (i < 0x90a5c);
  g_EnemyAirCount = 0x90a5c;
  for (i = 0x90a5c - 1; i >= 0; i--)
    if (ax[i] < 1) remove i over {ax, ay, adm, adir, ahp, athr, aspd, aburn, amis, abomb, astate, aeject};
}
0x90a5c = 0;
```
X is written to the scratch 0x909a8 first (so 0x909a8 holds the last spawn x afterwards). Quirk: with
`0x90a5c == 0` the body still runs once (consumes Rand/decrements p08 in the air branch) and the count
becomes 0. Note the `p08 > 99` record index was already stripped by Mission_LoadBriefing (level.md §7).

### 2.3 EnemyAir_Update @ 0x36ae3 — part A: movement of every aircraft (verified)

```c
for (i = 0; i < g_EnemyAirCount; i++) {
  if (ax[i] <= -999) continue;
  load x,y,thr,hp,dir,spd,state from the arrays;
  if (IsOnScreen(g_CamX, g_CamY, x, y)) {
    if (aburn[i] < 1) Sprite_Queue(x - g_CamX, y - g_CamY, min(dir + 0x5a + state, 0xdb));  /* parked: 0xdb */
    else {
      Sprite_Queue(x - g_CamX, y - g_CamY, Rand(2) + 0xc3);                                /* fireball  */
      if (IsOnScreen(g_CamX, g_CamY, awx[i], awy[i])) {
        Sprite_Queue(awx[i] - g_CamX, awy[i] - g_CamY, aburn[i] + 0xc5);
        aburn[i] = Wrap(aburn[i] + 1, 1, 4); } }
    if (adm[i] > 1) {                                       /* crippled: spin animation                */
      0x8ff80 = 0x900ac/2 + 12;  0x900ac += 0x900b0;  if (0x900ac == -2 || 0x900ac == 2) 0x900b0 = -0x900b0;
      dir += Sign(0x8ff80 - dir);                           /* drifts towards 11..13                    */
      if (10 < dir && dir < 14) Sprite_Queue(x - g_CamX, y - g_CamY, (dir - 12)*2 + 0x116 + g_FrameParity);
    }
  }
  if (adm[i] > 0) {
    if (adm[i] > 1 && Rand(20) == 1 && aeject[i] == 0) {   /* pilot ejects -> §6 (no bound check!)     */
      aeject[i] = 1;  px[n] = x; py[n] = y; pvy[n] = -8; pst[n] = 1; n = ++0x90754; }
    if (Rand(100) > 0x62) adm[i]++;                          /* Rand(100) whenever adm > 0             */
  }
  if (aburn[i] != 0) awy[i] += 8;
  x += Clamp(g_Dir16X[dir]*spd/4, -16, 16);
  y += Clamp(g_Dir16Y[dir]*spd/4 + adm[i], -16, 16);        /* damage pulls it down                     */
  y = min(y, 0x3f0);
  0x90394 = g_MapWidth*16;                                  /* horizontal wrap                          */
  if (x >= 0x90394) x -= g_MapWidth*16;   if (x < 0) x += 0x90394;
  if (abs(PX + 0x90394 - x) < 0xa0 && g_CamX < 0xa0) x += PX - 0x90394;          /* sic, see Q2        */
  if (abs(PX - 0x90394 + x) < 0xa0 && 0x90394 - 0xa0 < g_CamX) x = PX + 0x90394 - x;
  store x, state, y, spd, thr, hp, dir;
}
```
Note the store happens even for the first (spin) frames; `adir` is modified by the spin code.

### 2.4 Part B: one aircraft per frame does AI (verified)

`k = Clamp(g_FrameParity, 0, g_EnemyAirCount - 1)` (written to g_LoopI): aircraft 0 on even frames, 1 on odd.
```c
if (ax[k] < -998) { remove k over the 12 arrays of §2.2; return-ish (skips to the lock tail) }
else {
  PXe = PX (0x90038); PYe = PY (0x9004c);
  Tx = PXe (0x9001c);  Ty = min(PYe, 800) (0x90020);                       /* target                  */
  if (abomb[k] > 0) { Tx = (g_BaseEndX - g_BaseStartX)/2 + g_BaseStartX; Ty = 0; }   /* raid the base   */
  if (0x90998 /*B52 active*/) { Tx = 0x90984; Ty = 0x90988; }
  if (0x90528 /*Hercules*/)   { Tx = 0x90520; Ty = 0x904c0; }
  PYg = PYe (0x90050);
  if (g_ViewTarget 0x8ff10 == 200) { Tx = PXe = g_ViewX; Ty = PYg = g_ViewY; }       /* (0x90038/0x90050) */
  load x,y,dir,spd,thr,hp of k;   0x90048 = PYe;
  if (abomb[k] > 0 && g_BaseStartX < x && x < g_BaseEndX) Ty = 0x90048 = g_EnemyBombRef*16;
  /* ground / damage check */
  a = Map_GetTileAttr(Div16(x) % g_MapWidth, Clamp(Div16(y + (astate[k]==0x81)*20), 0, 63), 0);  0x90688 = a;
  if ((a > 0x7e && a != 0x81) || adm[k] > 5 || y > 999) {
    if (astate[k] != 0) g_EnemyBaseX = 0;                     /* crashed while parked: base closed      */
    Explosion_Damage(x, y, 0, g_Dir16Y[dir], abomb[k]==999 ? 2000 : 1000, 2000);
    adm[k]++;
    if (adm[k] > 5 || Div16(y) > 0x3e) {                      /* destroyed                              */
      Explosion_Damage(x, y, 0, 16, 1000, 2000);
      if (ahp[k] < 4) {                                       /* always true: hp never rises above 0    */
        0x90410++; g_Kills++; 0x900dc = 1; p05 = max(p05 - 1, 0);
        g_Score[g_AeroPlayer] += (0x9045c /*plane class*/ + 1) * 1000;
        if ((0x90890 == 0 && Rand(2) == 1) || g_ExtraAircraftScore < g_Score[g_AeroPlayer]) Bonus_Spawn();
      }                                                       /* (Rand(2) only when no bonus is active) */
      if (p08 == 0 && p05 == 0) { 0x90a5c--; x = -999; aburn[k] = 0; }
      else {                                                  /* respawn from p11                       */
        p08 = max(p08 - 1, 0);  spd = 6;
        x = CLAMPR(p11*16 + Rand(600) - Rand(600), 200, (g_MapWidth-1)*16 - 200);
        y = -800; dir = 0; adm[k] = 0; thr = 4; amis[k] = 4; aburn[k] = 0;   /* NOT abomb/aeject/astate */
      }
    }
  }
  if (x > -999) { AI §2.5 }
  store x, y, spd, thr, hp, dir into k;
}
if (g_LockTarget > -1) 0x8fff8 += 4;                          /* lock-tone counter (sound/player)       */
```
Scores: each enemy aircraft destroyed by anything (including crashing into terrain on its own) is credited.

### 2.5 AI decision (aircraft k, in this exact order) (verified)

```c
sy0 = Sign(g_Dir16Y[dir]) (0x906c4);  sx0 = Sign(g_Dir16X[dir]) (0x90718);
spd = Clamp((g_Dir16Y[dir] + thr*2)/8 + spd - adm[k], 0, 8);   /* climbing slows, diving speeds up     */
lvl = 0;  dy = Sign(Ty - y);  dx = Sign(Tx - x);
/* 1 take-off roll */
if ((spd > 4 || x < g_EnemyBaseX*16 - 0x1e0) && x < g_EnemyBaseX*16 - 0x140 && astate[k] > 0) {
    astate[k] = 0; dy = -1; dx = 0; dir = 1; thr = 9; spd = max(spd, 5); lvl = 1; }
/* 2 taxi */
if (astate[k] > 0) { dy = 0; dir = 0; dx = -1; lvl = 1; thr = 9; }
/* 3 wander when far (Rand(10) #2 only if #1 fails) */
if (lvl == 0 && (Rand(10)*0x90070 > 2 || Rand(10)*0x9045c > 9)
    && (abs(x - Tx) > 800 || abs(y - Ty) > 600) && abomb[k] == 0) { dx = 0x90a08; dy = 0x90a0c; }
/* 4 */ if (y > 600 && astate[k] == 0 && abomb[k] < 999) dy = min(dy, 0);
/* 5 */ if (x > g_MapWidth*16 - 0x280) { dx = -1; lvl = 1; }
/* 6 terrain avoidance */
if (y > 200 && astate[k] == 0 && abomb[k] < 999) {
    0x901bc = h = Map_ScanAround(min(Div16(x), g_MapWidth - 11));   /* min surface row of cols c-10..c+9 */
    if (h - 4 < Div16(y)) { dy = -1; dx = 0; lvl = 2; thr = 9; } }
/* 7 evade when locked (Rand(100) only if locked on k) */
if (g_LockTarget == k && Rand(100) < g_EnemySkill*8 + 8 && astate[k] == 0 && lvl == 0 && abs(x - Tx) < 0xa0) {
    dy = 0x90a08; dx = 0x90a0c; }                                     /* sic: X/Y wander swapped        */
/* 8 new wander direction */
if (Rand(50) == 1) { 0x90a08 = 1 - 2*Rand(1); 0x90a0c = 1 - 2*Rand(1); }
/* 9 too high above the camera */
if (y < 0 && y < g_CamY + 0x8fea0 - 0x80) { dy = 1; dx = 0; spd = 0; thr = 0; lvl = 2; }
/* 10 break-off when head-on and close (Rand(10) last) */
if ((-sx0 == dx || -sy0 == dy) && y < 700 && abs(x - Tx) < 200 && abs(y - Ty) < 200 && Rand(10) < 4 && lvl == 0) {
    thr = 0; lvl = 2; }
/* 11 */ if (dy == -1 && (g_LockTarget != k || y > 500) && lvl < 2) thr = 9;
/* 12 stall */ if (spd < 3) { y += 16; dy = 1; thr = 9; }
/* 13 */ if (spd < 5 && g_LockTarget != k) thr++;                     /* unbounded                       */
/* 14 crippled or pilot gone: dive */
if (adm[k] > 1 || aeject[k] == 1) { dy = 1; dx = Rand(2) - 1; }
/* 15 */ if (dx == 0 && dy == 0) dx = 1 - 2*Rand(1);
/* 16 turn */ if (adm[k] + Rand(16) < 0x90770 + 10 || lvl > 0) dir = Heading_TurnToward(dir, dx, dy);
/* 17 jink */ if (Rand(10) + 0x90770 < 4 && y < 500 && lvl == 0 && abs(x - Tx) < 600) dir = (dir + Rand(2) - 1) & 15;
g_LockTarget = -1;
/* 18 wake-up column p12 */
if (Div16(PXe) < p12 || abs(x - PXe) < 2000) p12 = 0;
if (p12 == 0) { reticle §2.8 (lock index k); bombing §2.6; missiles at B52; guns/missiles at the player §2.7 }
```
`0x90770` is never written (0): steps 16/17 are `adm + Rand(16) < 10` and `Rand(10) < 4`. `0x90070` = plane
stat from the MISC record (+0x864 of PlaneRecord, game_flow §6), `0x9045c` = player plane class.
`0x8fea0` is a Tileset_Load leftover (25 after any tileset load; level.md §2.5) – so step 9 is
`y < g_CamY - 0x67`. Rand order in one AI pass: [3: R10 (,R10)] [7: R100] 8: R50 (,R1,R1) [10: R10]
[14: R2] [15: R1] 16: R16 17: R10 (,R2) then §2.6/2.7.

### 2.6 Bombing and missiles at the B52 (inside `p12 == 0`, after the reticle) (verified)

```c
if (g_BaseStartX < x && x < g_BaseEndX && g_EnemyBombRef*16 - 0x30 <= x && abomb[k] > 0
    && g_ProjCount < 21 && abomb[k] < 999 && adm[k] < 2) { Enemy_DropBomb(); abomb[k]--; }
if (0x90998 && abs(0x90984 - x) < 600 && abs(0x90988 - y) < 600 && amis[k] > 0 && adm[k] < 2
    && Rand(6) > 4 - g_EnemySkill) {
    amis[k]--;  EnemyMissile_Add(x, y, dir/2, 0x1e);              /* §3.1 */ }
```
`Enemy_DropBomb` launches from the scratch 0x909a8/0x909ac (= x,y of k, still loaded) with heading 0x909fc.

### 2.7 Guns and missiles at the player (verified with capstone)

```c
dx = Sign((x - PXe)/8);  dy = Sign((y - PYg)/8);   (computed for the reticle, §2.8; reused here)
if (Sign(g_Dir16Xb[dir]) == -dx && Sign(g_Dir16Yb[dir]) == -dy) {          /* nose points at the player */
  dist = abs(x - PXe) + abs(y - PYg);  0x90a00 = dist;
  if (dist < 0x140 && Rand(4) < g_EnemySkill + 1 && adm[k] < 2) {
    EnemyFire_Sfx();                                                        /* 0x389b0, §3.4             */
    /* tracer: arguments of an empty stub (0x268be); evaluated right to left — two Rand(1) calls,
       and two integer divisions that FAULT when the divisor is 0 (see Q4):                            */
    t1 = ((PYg - y) << 8) / abs(PYg - y);   t2 = ((PXe - x) << 8) / abs(PXe - x);
    r1 = Rand(1);   r2 = Rand(1);                                           /* order: y-offset, x-offset */
    if (Rand(10) < g_EnemySkill + 1) {
      Explosion_Damage(PXe, PYg, 0, 0, 10, 10);
      Sfx_Play(Rand(1)*0x17 + 4, 7000, 0x3f, PX);
      0x907f8 = g_EnemyGunDamage + 1;  Player_DamageSystems();
    }
  }
  if (dist < 0x280 && dist >= 0x140 && g_EnemyMslCount < 4 && amis[k] > 0 && adm[k] < 2
      && Rand(6) > 4 - g_EnemySkill) {
    EnemyFire_Sfx();  amis[k]--;  EnemyMissile_Add(x, y, dir/2, 0x1e);  Flare_Release();   /* 0x38a61 */ }
}
```
(`dir/2` with dir ≥ 0 is a plain shift.) `Flare_Release` = the player's automatic flare (weapons.md §7.1).

### 2.8 Lock reticle (shared pattern; EnemyAir, EnemyGround, big bomber) (verified)

```c
psx = g_Dir16Xb[g_DirHalf/2]; psy = g_Dir16Yb[g_DirHalf/2];            /* player facing, 0x90598/0x9059c */
if (g_IsHeli) { psy = 3; psx = (g_DirHalf < 6) ? ((g_Dir/2 == 2) ? 0 : -16) : 16; }  /* see note      */
if (Sign(psx) == dx && Sign(psy) == dy) {                               /* dx,dy = Sign(obj - player)    */
  0x903f0 = 0;  d = abs(ox - PXe) + abs(oy - PYg) (0x90420);
  ddx = ox - g_CamX - g_PlayerScrX (0x90424); ddy = oy - g_CamY - g_PlayerScrY (0x90428);
  rng = max(32 * max(g_WeaponThrust[g_RackWeapon[0]], g_WeaponThrust[g_RackWeapon[1]]), 0x4b0);
  if (d < rng)   { 0x903f0 = 0x9b; g_LockTarget = LOCKID; }
  if (d < 0x140)   0x903f0 = 0x9c;                                        /* (not for the big bomber)     */
  if (0x903f0 > 0) Sprite_Queue(Clamp(g_PlayerScrX + ddx, 8, XMAX), Clamp(g_PlayerScrY + ddy, YMIN, 0xa8), 0x903f0);
}
```
| user | dx,dy | LOCKID | XMAX | YMIN | heli test of g_Dir |
|---|---|---|---|---|---|
| EnemyAir | `Sign((ax-PXe)/8)`, `Sign((ay-PYg)/8)` | k | 0x134 | 8 | `g_Dir/2 == 2` (0x90200) |
| EnemyGround | `Sign(gx-g_CamX-scrX)`, `Sign(gy-g_CamY-scrY)` | i+3 | 0x138 | `8 - 0x23*0x90308` | `g_DirHalf/2 == 2` |
| big bomber §13.1 | same, with 0x9073c/0x90740 | 2 | 0x138 | `8 - 0x23*0x90308` | `g_DirHalf/2 == 2` |
The Clamp is a MIN/MAX macro pair (no Rand inside, so double evaluation is harmless). `g_DirHalf` =
0x901f0, `g_IsHeli` 0x904cc, `0x90308` = HUD panel offset flag (player/hud spec). The ground and bomber
variants have no `d < 0x140` 0x9c upgrade for the bomber (only 0x9b) — verified.

### 2.9 Enemy_DropBomb @ 0x17944 (was FUN_00017944) — `void (void)` (verified)

Fires `g_EnemyBombWeapon` through the player launch routines.
```c
0x8fee8 = g_RackWeapon[0];  0x8feac = 0x90778;                       /* save (0x90778 = player launch vx) */
g_LaunchX = 0x909a8; g_LaunchY = 0x909ac; 0x90778 = 0;
g_LaunchDirX = g_LaunchDirTabX[0x909fc/2];  g_LaunchDirY = g_LaunchDirTabY[(0x909fc/2)*2];
0x9002c = g_WeaponType[g_EnemyBombWeapon].k0 + 1;  g_Scratch690 = 0;  g_FireRack 0x90338 = 0;
g_RackWeapon[0] = g_EnemyBombWeapon;
switch (0x9002c) {            /* jump table 0x1795f: index = k0+1 (NOT k0 as in Weapon_Fire) — Q5   */
  case 0: case 3: case 6: case 7: Weapon_LaunchBallistic(); break;   case 1: Weapon_FireRocket(); break;
  case 2: Weapon_FireGuided(); break;   case 4: Weapon_TakePhoto(); break;   case 5: Weapon_DropTank(); break;
  case 8: Weapon_ArmJP233(); break;     case 9: /* 0x1821c empty */ break;  case 10: Weapon_DropCommando(); break;
}                              /* > 10: nothing                                                          */
n = g_ProjCount;  copy g_WeaponType[g_RackWeapon[0]].k0..k5 into ProjKind/Arm/Motor/Flight/Detonate/Flags[n];
g_RackWeapon[0] = 0x8fee8;  ProjTargetX[n] = ProjTargetY[n] = 0;
g_ProjCount = min(g_ProjCount + 1, 0x15);  0x90778 = 0x8feac;
```
Callers: EnemyAir §2.6, big bomber §13.1 (sets `g_EnemyBombWeapon = 13`, 0x909fc = 16 first), and the
Hercules/B52 blocks of Player_DeathAndLanding (friendly drops, player spec). The projectile is a normal
player projectile (it can damage enemies and the player's own base via Explosion_Damage).

---------------------------------------------------------------------------------------------------

## 3. Enemy missiles — EnemyMissiles_Update @ 0x19fda (was Helis_Update) (verified)

### 3.1 Arrays and spawners
```c
/* count g_EnemyMslCount 0x906fc (spawners test < 4 or < 10; arrays have room for 4: 0x8fab8..0x8fac8!) */
int32 mx  [] @0x8fab8;  int32 my [] @0x8fac8;  int32 mdir[] @0x8fa58 /* 0..7 */;  int32 mlife[] @0x8fa68;
```
`EnemyMissile_Add(x, y, d, life)` (inline everywhere): `mx[n]=x; my[n]=y; mdir[n]=d; mlife[n]=life; n++`.
Spawners: EnemyAir (§2.6/2.7, life 0x1e), EnemyGround (§4, life 3, limit < 10), SAM site 0x3ef72 (§8.3,
life 0x1e), target vehicle 0x447f3 (§9, life 0xf). Overflow quirk Q8: EnemyGround allows up to 9 → writes
past the 4-entry arrays (my[4..] overlaps mx/other arrays). Flares test these arrays (weapons.md §7.2).

### 3.2 Update
```c
VX = g_CamX, VY = g_CamY; if (g_ViewX > 0 || g_ViewY > 0) { VX = g_ViewX; VY = g_ViewY; }
for (j = g_EnemyMslCount - 1; j >= 0; j--) {
  x = mx[j]; y = my[j]; d = mdir[j]; life = mlife[j];          /* 0x9080c 0x90810 0x908ac 0x90704 */
  if (IsOnScreen(g_CamX, g_CamY, x, y)) Sprite_Queue(x - g_CamX, y - g_CamY, d + 0x6a);
  if (BoxOverlap(x, y, 0x90520, 0x904c0, 0x40, 0x20)) {        /* Hercules                        */
     Explosion_Damage(0x90520, 0x904c0, 0, 0, 10, 10); 0x90510 = 0x90510 - Rand(4) - 3;
     Hud_PushMessage("Hercules Hit!"); if (0x90510 < 1) 0x9050c = 1; x = -999; }
  if (BoxOverlap(x, y, 0x90984, 0x90988, 0x40, 0x20)) {        /* B52                              */
     Explosion_Damage(0x90984, 0x90988, 0, 0, 10, 10); 0x90978 = 0x90978 - Rand(4) - 3;
     Hud_PushMessage("B52 Hit!"); if (0x90978 < 1) 0x90970 = 1; x = -999; }
  if (BoxOverlap(x, y, VX + g_PlayerScrX, VY + g_PlayerScrY, 0x20, 0x20)) {
     Explosion_Damage(PX, PY, 0, 0, 10, 10); 0x907f8 = 4; Player_DamageSystems(); x = -999; }
  if (x > -1 && g_EjectState == 0) {
    if (0x90998 == 0 || abs(0x90984 - x) < 0x259 || abs(0x90988 - y) > 599) {     /* sic, Q9      */
      if (g_FlareCount == 0 || Rand(1) != 0) {
        if (Rand(3) != 0 && life < 20) {
          dx = Sign(VX + g_PlayerScrX - x); dy = Sign(VY + g_PlayerScrY - y);
          if (life < 0) { dx = 0; dy = 1; } }                  /* out of fuel: fall                */
      } else { dx = Sign(g_FlareX[0] - x); dy = Sign(g_FlareY[0] - y); }          /* decoyed       */
    } else { dx = Sign(0x90984 - x); dy = Sign(0x90988 - y); if (life < 0) { dx = 0; dy = 1; } }
    if (0x90528 && abs(0x90520 - x) < 600 && abs(0x904c0 - y) < 600) {
      dx = Sign(0x90520 - x); dy = Sign(0x904c0 - y); if (life < 0) { dx = 0; dy = 1; } }
    a = Map_GetTileAttr(Clamp(Div16(x + 16), 0, g_MapWidth - 1), Clamp(Div16(y + 16), 0, 63), 0);
    if (a > 0x7e || life < 2) { Explosion_Damage(x + 16, y + 16, 0, 0, 0x32, 0x32); x = -999; }
    if (life < 20) { f = heading_table(dx, dy) /*0x92980*/; d = (d + 8 + Sign(f - d)) % 8; }
    vx = Sign(g_Dir16X[d*2]) * 24;  vy = Sign(g_Dir16Y[d*2]) * 24;           /* 0x90934 / 0x90938    */
  }
  x += vx; y += vy;  life--;                    /* vx/vy are the GLOBALS: stale from the previous      */
  store x, y, d, life;                          /* missile when this one skipped the AI (Q10)          */
  if (mx[j] < 0) remove j over {mx, my, mlife, mdir};
}
```
`dx,dy` are the globals 0x90a24/0x90a28: when no branch assigns them they keep the previous values
(previous missile or another updater) — keep them global. Rand order per missile: Hercules Rand(4) [if hit],
B52 Rand(4) [if hit], `Rand(1)` [only with flares out], `Rand(3)`. Missiles with life ≥ 20 fly straight.
A ground-unit missile (life 3) explodes on its second update (`life < 2`) ~48 px from the launcher (Q11).

---------------------------------------------------------------------------------------------------

## 4. Ground gunships — EnemyGround_Update @ 0x35e20 (verified with capstone)

### 4.1 Arrays (index 0..3, count g_EnemyGroundCount 0x90348; set up by Mission_LoadBriefing for p10 > 999,
level.md §7 step 7)
```c
int32 gx[]@0x8d9d8, gy[]@0x8d9e8;  int32 gdir[]@0x8d9c8 /* 0..6, 3 = hover */;  int32 gvy[]@0x8d8f8;
int32 gmis[]@0x8d9b8 /* 4 */;  int32 gshell[]@0x8d7d8 /* 100 */;  int32 ghp[]@0x8d9a8 /* 4 */;
int32 gdm[]@0x8d998 /* damage, >4 = dead */;  int32 g8e8[]@0x8d8e8 /* 8, never read here */;
```
Damage: Explosion_Damage (3) and gun hits with g_LockTarget ≥ 3 (weapons.md).

### 4.2 Update
```c
Tx = PX; Ty0 = PY;  if (g_ConvoyCount 0x905ac > 0 && g_EscortMode 0x905c0 == 1) { Tx = cvx[0]; Ty0 = cvy[0]; }
Ty = Ty0 - 0x40;                                     /* 0x9001c / 0x90020                                 */
for (i = n - 1; i >= 0; i--) {
  if (IsOnScreen(g_CamX, g_CamY, gx[i], gy[i])) {
    Sprite_Queue(gx[i] - g_CamX, gy[i] - g_CamY, gdir[i]*2 + 0x1a4 + g_FrameParity);
    if (gdm[i] > 0) Particle_Spawn(gx[i] << 8, gy[i] << 8, 0, 0, 0, Rand(4) + 12, 0x18);   /* Q12      */
  }
  if (gdm[i] > 4) {                                    /* destroyed (no score)                           */
    Explosion_Damage(gx[i], gy[i], 0, 0, 2000, 2000);
    remove i over {gx, gy, gdir, gvy, ghp, gdm, gmis, g8e8, gshell}; continue; }
  gx[i] = Clamp(gvy[i] + gdir[i]*2 - 6, 0x280, g_BaseStartX - 0x140);         /* sic: Q1 (verified asm) */
  h = g_MapVal[0x400 + Div16(gx[i])] * 16;  0x9028c = h - 16;                 /* surface height (px)    */
  gy[i] = Clamp(gy[i] + gvy[i], h - 0x50 - 32*i, h - 16);                     /* stacked by index       */
  if (Ty < 1) { if (Rand(10) == 1) gvy[i] = Rand(2)*4 - 4; }
  else gvy[i] = Clamp(gvy[i] + Sign(Ty - gy[i] - 32*i), -4, 4);
  if (Rand(1) != 0) gdir[i] = Clamp(gdir[i] + Sign(Tx - gx[i] - 32*i), 0, 6);
  face = (gdir[i] < 3) - (gdir[i] > 3);                                        /* 0x9098c                */
  dx = Sign(gx[i] - g_CamX - g_PlayerScrX); dy = Sign(gy[i] - g_CamY - g_PlayerScrY);
  /* shell (Rand(5) last) */
  if (-face == dx && abs(gx[i] - PX) < 0x80 && gy[i] < PY && PY < gy[i] + 0x50 && gshell[i] > 0
      && g_ShellCount < 10 && Rand(5) == 1) {
    EnemyShell_Add(gx[i] + face*8, gy[i], Rand(10) + 10, (PX - gx[i] - 8)/6, (PY - gy[i])/6, 1);   /* §5 */
    Sfx_Play(1, 14000, 0xc, gx[i]); 0x90074 = 0; Sfx_Play(3, 0x1194, 0x18, gx[i]); 0x90080 = 0;
    gshell[i]--; }
  /* missile (Rand(50) last) */
  if (-face == dx && abs(gx[i] - PX) < 0x140 && abs(gy[i] - PY) < 0x80 && g_EnemyMslCount < 10
      && gmis[i] > 0 && Rand(50) < g_Mission/3 + 1) {
    gmis[i]--;  EnemyMissile_Add(gx[i], gy[i], (face > 3) * -4 /* always 0 */, 3); }
  0x90a00 = 2*abs(gy[i] - PY);                                                 /* sic (dy twice), unused */
  reticle §2.8 with LOCKID = i + 3;
}
```
`EnemyShell_Add` writes the shell, then calls the empty stub 0x268be (args computed, no Rand), count++.

---------------------------------------------------------------------------------------------------

## 5. Enemy shells — EnemyShells_Update @ 0x19cc8 (was Targets_Update) (verified with capstone)

```c
/* count g_ShellCount 0x8ffa0 (spawners require < 10)                                                    */
int32 sx[]@0x8e388, sy[]@0x8e3b0, svx[]@0x8dde8, svy[]@0x8de10, slife[]@0x8e350, ssize[]@0x8df28 /*0/1*/;
```
`EnemyShell_Add(x, y, life, vx, vy, size)` is inline in the spawners: EnemyGround (§4), gun emplacement
0x3e8b3 (§8.2), convoy 0x3ea8f (§7.3), target vehicle 0x448d7 (§9) — each sets the fields in the order
x, y, life (`Rand(10)+10`), vx, vy (`Rand(4)` subtracted last), size, then count++.

Update (only called when count > 0):
```c
VX = g_CamX, VY = g_CamY; if (g_ViewX != 0 || g_ViewY != 0) { VX = g_ViewX; VY = g_ViewY; }
for (j = count - 1; j >= 0; j--) {                    /* do-while: body runs at least once           */
  s_x = sx[j] - g_CamX;  s_y = sy[j] - g_CamY;          /* 0x9080c/0x90810                             */
  if (s_x >= 0 && s_x < 0x140 && s_y >= 0 && s_y < 0xb0) Video_PutPixel(s_x, s_y /*, colour: Q13*/);
  if (BoxOverlap(VX + s_x, VY + s_y, VX + s_x, VX + g_PlayerScrY, ssize[j]*24 + 8, ssize[j]*24 + 8)) {  /* Q14 */
     Explosion_Damage(VX + g_PlayerScrX, VY + g_PlayerScrY, 0, 0, 5, 5);
     0x907f8 = ssize[j] + 1;  Player_DamageSystems();  slife[j] = -999; }
  sx[j] += svx[j];  sy[j] += svy[j];  slife[j]--;
  if (g_FrameParity == 0) svy[j]++;                   /* gravity every 2nd frame                       */
  if (slife[j] < 1) remove j over {sx, sy, svx, svy, slife} (NOT ssize — Q15);
}
```
Quirks: Q13 the pixel goes through `Video_PutPixel`, which writes **absolute VRAM** (no page offset) and whose
3rd argument (colour) is not pushed — it reads the caller's saved EBP low byte (a stack address byte, constant
during a run). VRAM offset 0.. is the HUD panel page (video.md), so shells appear (if at all) as dots in the
HUD panel rows, not on the playfield. Q14 the hit test compares the shell x with itself and the shell y with
`VX + g_PlayerScrY` (camera **X** + screen y): the player is hit when `|(VY + s_y) - (VX + scrY)| < size*24+8`,
independent of x. Recommendation: keep both for faithfulness (PORT note: Q13 render in a fixed colour on the
HUD strip; Q14 behaviour must stay to keep RNG/damage identical).

---------------------------------------------------------------------------------------------------

## 6. Ejected enemy pilots — EnemyPilots_Update @ 0x35a33 (was Mines_Update) (verified)

```c
/* count 0x90754 (no bound check in the spawner, arrays have 10 entries)                               */
int32 epx[]@0x8db50, epy[]@0x8db78, epst[]@0x8db28 /* state */, epvy[]@0x8d7f0;
```
Spawned by EnemyAir §2.3 (`vy = -8, state = 1`).
```c
0x90760 = Wrap(0x90760 + 1, 0, 7);                    /* tumble frame                                   */
0x9075c += 0x90744; if (0x9075c == 0 || 0x9075c == 4) 0x90744 = -0x90744;   /* 0x90744 = 0 at mission start: never moves */
for (i = n - 1; i >= 0; i--) {
  if (IsOnScreen(g_CamX, g_CamY, epx[i], epy[i])) {
    s = 0x90cf8[epst[i]];                             /* GENDAT3 table, [i] = min(i+0x2e, 0x4a) for i >= 7 */
    if (s == 0x35) s = 0x90ce4[0x9075c] + 0x35;
    if (epst[i] < 5) s = 0x90760/2 + 0x18e;
    Sprite_Queue(epx[i] - g_CamX, epy[i] - g_CamY, s);
    if (0x9019c == 0 && Rand(50) == 1) { Sfx_Play(0x16, 4000, 0x3f, PX); 0x9019c = 1; 0x900f0 = 8; }  /* scream, once per mission */
  }
  epy[i] += epvy[i];
  epvy[i] = min(epvy[i] + g_FrameParity, epst[i] > 4 ? 28 : 16);
  if (epst[i] < 7) { epst[i]++; if (epy[i] < 700 && epst[i] == 5) epst[i] = 1; }   /* tumble while high */
  if (epy[i] > 0x3e0) { epy[i] = 0x3e0; epst[i] = Clamp(epst[i] + 1, 24, 28) /*min then max*/;
    if (epst[i] == 28) remove i over {epx, epy, epst} (NOT epvy); }
}
```
Pilots are harmless decoration (no collision); states 5..7 fall at up to 28 px/frame ("chute" sprites from
the table), 24..28 = landed animation.

---------------------------------------------------------------------------------------------------

## 7. Convoy

### 7.1 Arrays (index 0..8, count g_ConvoyCount 0x905ac) (verified)
```c
int32 cvx[]@0x91f74, cvy[]@0x91f98, cvvx[]@0x91f50 /* px per half-rate step */;
int32 cvslot[]@0x918e0 /* sprite slot base into 0x8dab8 */;  int32 cvst[]@0x91f2c /* 0 ok, >0 hit/falling */;
int32 cvsmk[]@0x91904 /* smoke y offset*2, always 0 */;  int32 cvhp[]@0x91928;
```
Mode globals: `g_EscortMode` 0x905c0 (p06 > 1000: friendly convoy to escort), `g_EscortNeeded` 0x909d8,
`g_ConvoyTrain` 0x8ffe0 (p09 ≥ 1000: 1 = train, 2 = one car lost → everything explodes), kind `p13`
(hi byte = truck letter/behaviour: 1 formation, 2 parked pickups; lo byte = hp multiplier),
`0x90a40` agent vehicle index (p24 > 500).

### 7.2 Enemy_SetupSpriteIds @ 0x3faab — convoy set-up (Mission_Setup when p09 != 0) (verified)
```c
0x8dab8[0..23] = {0x7a,0x7b,0x7c, 0x90,0x91,0x92,0x93, 0xc0,0xc1,0xc2, 0xe3..0xed, 0x1ee,0x1ef,0x1f0};
g_ConvoyCount = p09;  0x9057c = p13 & 0xff;  ntypes 0x902b8 = 0;  0x8ff4c = 0;  loaded[0..3] 0x8db18 = -999;
for (i = 0; i < g_ConvoyCount; i++) {
  cvsmk[i] = 0;  cvx[i] = p18*i + p10*16 + 0x60;  cvy[i] = p17;  cvst[i] = 0;  cvvx[i] = 2;
  if ((p13 >> 8) == 2) { cvvx[i] = 0; 0x8ff4c = 3; }
  if (cvy[i] < 0x46) cvy[i] = cvy[i]*16 - 1;            /* row given instead of pixels               */
  t = (p15_p16_word[i/4] >> ((i%4)*4)) & 15;            /* i >= 8 reads p17, i >= 12 p18 (Q16)        */
  cvslot[i] = t (temporarily);  cvhp[i] = 0x9057c * g_VehicleHP[t];
  if (ntypes < 4) {
    s = ntypes; for (j = 0; j < ntypes; j++) if (loaded[j] == t) { s = j; break; }
    if (s == ntypes) { loaded[s] = t; ntypes++;
      if (t == 15 && 0x8ff4c == 3) 0x8dab8[0x8da88[s]] = p24;              /* parked pickup sprite     */
      else { 0x9098c = 0x8da88[s]; 0x90434 = (0x8ff4c == 3) ? 1 : 6; Truck_LoadSpx(); } }
    g_Scratch690 = s;
  } else { /* loop `for (j = 0; ntypes < j; j++)` never runs: g_Scratch690 keeps the previous value (Q17) */ }
  cvslot[i] = 0x8da88[g_Scratch690];                    /* slot bases {0,6,12,18} (GENDAT3)          */
}
```
`Truck_LoadSpx @ 0x3ff4d`: file `"plane/" + "truck" + ('a' + (p13>>8)) + digits + ".spx"`; LZW-unpacks the bank
and for f < 0x90434: `Sprites_ReplaceFromBank(0x8dab8[0x9098c + f], f + 1, bank)`. Digits for type t:
t < 10 → one digit; t ≥ 10 → **two characters `'0'+(t/10)%10`, `'0'+t/10`**, i.e. always "11" (Q18: types
10..15 all load `truckX11.spx`; shipped data uses types 10..14 with letters b/c, e.g. M0 #41 type 14 → TRUCKC11).

### 7.3 Convoy_Update @ 0x3d105 (verified with capstone)
```c
0x9042c = g_TargetMarkX;
if (0x90568 == 0 || 0x9056c > 2 || 0x9056c < 0) { 0x90568 = 1; 0x9056c = 0; }
if ((p13 >> 8) == 2) 0x9056c = 0;  else { 0x9056c += 0x90568; if (0x9056c == 0 || 0x9056c == 2) 0x90568 = -0x90568; }
0x905a8 = 1 - 0x905a8;                                   /* half-rate phase                             */
for (i = n - 1; i >= 0; i--) {
  x = cvx[i]; y = cvy[i]; vx = cvvx[i]; slot = cvslot[i];   /* 0x909a8 0x909ac 0x909bc 0x909f4         */
  if (cvst[i] == 0) { g_TargetMarkX = x + vx; g_TargetMarkY = y; }
  d = vx;                                                    /* 0x909fc                                  */
  if (IsOnScreen(g_CamX, g_CamY, x, y)) {
    flip = (d < 0 && g_ConvoyTrain == 0 && (p13 >> 8) != 2);
    spr = 0x8dab8[slot + 0x9056c + flip*3];
    if (g_EscortMode) Sprite_Queue(x - g_CamX, y - g_CamY - Sprite_GetHeight(spr), 0x1ec + g_FrameParity);
    Sprite_Queue(x - g_CamX, (uint16)Sprite_GetY(spr) + (y - g_CamY) - Sprite_GetHeight(spr), spr);
    if (p13 != 2) {                                          /* whole word: hi 0, lo 2                   */
      if (Video_ReadPixel(x - g_CamX + g_ScrollFineX, max(y, p17) - g_CamY + 1 + g_ScrollFineY, g_BackPage) == 0)
           y = min(y + 1, p17 + 4);                          /* road below gone: sink (pixel probe)      */
      else if (Video_ReadPixel(x - g_CamX + g_ScrollFineX, y - g_CamY + g_ScrollFineY, g_BackPage) > 0)
           y = max(y - 1, p17);                              /* inside ground: climb                     */
    }
    if (cvst[i] > 0) { vx = 0; Sprite_Queue(x - g_CamX, cvsmk[i]/2 + (y - g_CamY), Rand(1) + 0x9e); }   /* fire */
  }
  if ((i & 1) == 0x905a8) {                                  /* half rate: logic + write-back            */
    if (cvst[i] > 0) {
      if (Rand(20) > 18) cvst[i]++;
      if (cvst[i] > 3) {                                     /* destroyed                                */
        if (p24 > 1000 && p06 == 0) { p24 %= 500; g_PickupX 0x909e8 = x; p14 = Div16(x); g_PickupY 0x909ec = y; }
        Explosion_Damage(x + 16, y + 16, 0, 0, 1000, 100);   /* 0x3b743: empty stub after it              */
        if (!g_EscortMode) { p06--; if (p06 < 1) { p06 = 0; p09 = 0; p10 = 0; } g_Score[g_AeroPlayer] += 250; }
        else { g_Score[g_AeroPlayer] -= 1000; Hud_PushMessage("Friendly Vehicle Destroyed!");
               if (n - 1 < g_EscortNeeded) { g_EscortNeeded = 0; Hud_PushMessage("Mission Failed!"); g_MissionBonus = 0; } }
        x = -999; } }
    if (x > 0) {
      x = (g_MapWidth*16 + x + vx) % (g_MapWidth*16);
      front = Map_GetTileAttr(Div16(x + Sign(d)*16) + 1, min(Div16(y) + p23, 63), 0);   /* 0x90578 */
      below = Map_GetTileAttr(Div16(x) + 1, Div16(y) + 1, 0);                           /* 0x9058c */
      if (g_EscortMode == 1) d = abs(d) * Sign(p03 - Div16(0x90538));   /* 0x90538 = last gun-ray hit x (Q19) */
      if (front == 0x80 || front == 0x7f || (below < 0x82 && p23 == 1)) {
        d = -d;                                              /* turn round                               */
        if ((p13 >> 8) == 1 || g_ConvoyTrain == 1) for (j = 0; j < 8; j++) cvvx[j] = d; }
      if (g_ShellCount < 10 && cvhp[i] > 0 && (g_GameMode & 1) == 0 && !g_EscortMode) Convoy_FireShell();  /* 0x3ea8f */
      if (g_ProjCount < 10 && cvhp[i] > 2 && Rand(50) == 1 && g_BaseStartX - 0x280 < x && x < g_BaseEndX + 0x280
          && !g_EscortMode) Convoy_LaunchMissile();          /* 0x17684 */
      if (p03 - 2 <= Div16(x) && Div16(x) <= p03 + 2) {     /* reached the destination                   */
        g_EscortNeeded--; Hud_PushMessage("Vehicle arrived"); x = -999; g_Score[g_AeroPlayer] += 2000; }
      if (below < 0x7f && (p13 >> 8) != 2) { y += cvst[i]; cvst[i] += Rand(1); }   /* falling (reuses cvst) */
      y = min(y, 0x3f0);
    }
    cvx[i] = x; cvy[i] = y; cvvx[i] = d;
    if (cvx[i] == -999) {
      if (g_ConvoyTrain == 1) g_ConvoyTrain = 2;
      if (i == 0x90a40) 0x90a40 = n - 1; else if (n - 1 == 0x90a40) 0x90a40 = i;
      remove i over {cvx, cvy, cvslot, cvvx, cvst, cvsmk, cvhp};   /* SwapInt(&a[n-1], &a[i])  */
    }
  }
}
if (((p13 >> 8) == 1 || g_ConvoyTrain > 0) && n > 1)
  for (i = 1; i < n; i++) { cvx[i] = cvx[0] + p18*i; if (g_ConvoyTrain == 2) cvst[i] = 5; }
if (g_ConvoyTrain == 2) cvst[0] = 5;
```
Notes: on-screen y changes are only stored on the object's half-rate frame. A falling vehicle (no ground)
accumulates `cvst` → becomes "hit" (> 0) and explodes after `cvst > 3`. p03 is the destination column for
escorted convoys (also the target column of other forms — escort missions use p03 for both).

`Convoy_FireShell @ 0x3ea8f` (uses g_LoopI = i): `if (cvy[i] - 0x78 < PY && Rand(30) < g_Mission && Rand(6) == 1
&& g_ShellCount < 10) EnemyShell_Add(cvx[i], cvy[i], Rand(10)+10, Sign(PX - cvx[i])*16,
min((PY - cvy[i])/4, -1) - Rand(4), size = stale)` — **ssize[n] is not written** (keeps whatever the slot held).
`Convoy_LaunchMissile @ 0x17684` (g_LoopI = i): appends a *player-array* projectile: kind 2, arm 0x1e, motor 1,
flight 9 (Projectile_Climb), detonate 0, flags 0, blastA = blastB = 2000, target 0/0, x = cvx[i],
y = cvy[i] - 8, frame 2, life 1000 (vx/vy not written: stale); `g_ProjCount = min(g_ProjCount+1, 0x15)`.

---------------------------------------------------------------------------------------------------

## 8. MP2 emplacements (classes 0x83/0x84/0x85; activated by Map_TriggerColumn, level.md §5) (verified)

Active object: tile col/row in 0x90668/0x90614 (0x83 flak), 0x9010c/0x90110 (0x84 SAM), 0x90268/0x9026c
(0x85 gun). Game_Run (GF 40) sets `g_TargetMark = (col*16, row*16)` and calls the draw then the fire routine
(fire guards: 0x84 none; 0x85 `0x907fc == 0 && g_ShellCount < 10 && (mode 0 or 2)`; 0x83 `0x90660 == 0 && 0x907fc == 0`).
Destruction: Explosion_Damage (5)-(7) (+3000 0x85, +1000 0x83, +2000 0x84, fire via Map_SecondaryExplosions).

### 8.1 Draw (0x3dea2 class 0x85, 0x3dfaa class 0x83, 0x3e0b2 class 0x84)
```c
wx = col*16 (0x905e4); wy = row*16 (0x905fc);
if (IsOnScreen(g_CamX, g_CamY, wx, wy)) {
  dv = Clamp(Div16(wy + 15 - PY), 1, 100);
  f  = Clamp((PX - wx - 8)/dv + 2, 0, 4);          /* aim frame 0..4; stored 0x902a0 / 0x906ac / 0x90108 */
  Sprite_Queue(wx - g_CamX - 16, wy - g_CamY, base + f);
}
/* 0x85: base = 0x1b2 + 5*0x90290, then 0x90290 = 0 (always, after the if)                               */
/* 0x83: base = 0x1b2 + 5*0x9064c, then 0x9064c = 0;   0x84: base = 0x1bc (no muzzle flag)              */
```
### 8.2 Gun_Fire @ 0x3e8b3 (class 0x85)
`if (row*16 - 0x78 < PY && Rand(1) != 0 && g_ShellCount < 10) { EnemyShell_Add(g_TargetMarkX, g_TargetMarkY - 16,
Rand(10)+10, Sign(PX - g_TargetMarkX)*32, min((PY - g_TargetMarkY)/6, -1) - Rand(4), 1);
0x90074 = 0; Sfx_Play(1, 14000, 0xc, g_TargetMarkX); 0x90080 = 0; Sfx_Play(3, 0x1194, 0x18, g_TargetMarkX); }`
(`min(v, -1)` written as `v < 0 ? v : -1`.) 0x90290 (muzzle flash) is never set here (always 0).
### 8.3 SAM_Fire @ 0x3ef72 (class 0x84)
`if (Rand(100) > 0x50 && g_EnemyMslCount < 4 && abs(PY - row*16) > 0xa0) EnemyMissile_Add(col*16, row*16 - 0x40, 2, 0x1e);`
(Rand(100) every frame the site is active.)
### 8.4 Flak_Fire @ 0x3ec5b (class 0x83) — airbursts near the player
```c
0x9062c = abs(col*16 - g_PlayerScrX - g_CamX) + abs(row*16 - g_PlayerScrY - g_CamY);
if (0x9062c < 0x280 && Rand(2) == 1) {
  bx = PX + Rand(100) - Rand(100);  by = Rand(0x8c) + g_CamY;              /* 0x90680 / 0x90684          */
  bc = Clamp(Div16(bx), 0, g_MapWidth - 1); br = Clamp(Div16(by), 0, 60);   /* 0x90694 / 0x9069c (MIN/MAX) */
  Sfx_Play(Rand(1)*0x17 + 4, 6000 - 0x9028c/200 + Rand(2000), 0x3f, bx);    /* Rand(2000) BEFORE Rand(1)  */
  0x9064c = 1;                                                              /* muzzle frame for §8.1      */
  VX/VY = view camera (g_ViewX/Y if either != 0);
  if (Map_GetTileAttr(bc, br, 0) < 0x7f) {
    if (BoxOverlap(bx, by, VX + g_PlayerScrX, VY + g_PlayerScrY, 8, 8)) { 0x907f8 = 2; Player_DamageSystems(); }
    Explosion_Damage(bx, by, 0, 0, 10, 10);  0x90660 = 10;                 /* 10-frame cooldown (GF 43)   */
  }
}
```
`0x9028c` here is whatever the last writer left (EnemyGround surface height or the GF 63 ground-probe flag).

---------------------------------------------------------------------------------------------------

## 9. Target-zone vehicles — TargetVehicle_Spawn @ 0x4424d / TargetVehicles_Update @ 0x44478 (verified)

Arrays (index 0..5, count g_TVCount 0x900b4): `tvx[]@0x8e400, tvy[]@0x8e418, tvst[]@0x8e3e8` (0 SAM
launcher, 1 gun, ≥ 2 dying). Spawn loop (game_flow §2.3): `for (k = 0; p03 && g_TVCount < 6 && g_GameMode == 0
&& k < 10; k++) TargetVehicle_Spawn();`
```c
TargetVehicle_Spawn: x = CLAMPR((p03 % 1000)*16 + Rand(0x1e0) - Rand(0x1e0), 0x1e0, g_MapWidth*16 - 0x1e0);
  tvx[n] = x - 8;  tvy[n] = g_MapVal[0x401 + Div16(tvx[n])] << 4;  tvst[n] = Rand(1);
  if (Map_GetTileAttr(Div16(tvx[n]), Div16(tvy[n]) + 1, 0) != 0x82) n++;   /* not on water: accept    */
```
```c
TargetVehicles_Update:
for (i = n - 1; i >= 0; i--) {
  if (IsOnScreen(g_CamX, g_CamY, tvx[i], tvy[i])) {
    Sprite_Queue(tvx[i] - g_CamX, tvy[i] - g_CamY, Rand(10)/10 + 0xd9);     /* 0xda one frame in 11      */
    p = Video_ReadPixel(tvx[i] - 0x904f0*16 + 16, tvy[i] - 0x904f4*16 + 12 /*, page arg not pushed*/);
    a = Map_GetTileAttr(Div16(tvx[i]) + 1, Div16(tvy[i]) + 1, 0);
    if (p > 0 && a > 0x7e) tvy[i]--;
    if (p == 0 || a < 0x7f) tvy[i] = min(tvy[i] + 1, 0x3e0);
    if (Rand(1000) > 0x3e3) {                                    /* ~1.6 % per frame                   */
      if (tvst[i] == 0) { TV_LaunchSAM(); TV_FireShell(); }      /* state 0 falls through to the gun  */
      else if (tvst[i] == 1) TV_FireShell(); }
  }
  if (tvst[i] > 1) {                                            /* dying: hop and sink               */
    tvy[i] += (tvst[i] - 8)*2;  tvx[i] = tvx[i] + Rand(2) - Rand(2);  tvst[i] = min(tvst[i] + 1, 16);
    if (tvy[i] > 0x3e0) tvx[i] = 0; }
  if (tvx[i] < 1) { remove i over {tvx, tvy} only; 0x900e0--; }  /* sic: Q6                             */
}
if (g_Mission < 7) g_TVCount = 0;                               /* sic: Q7                              */
```
`TV_LaunchSAM @ 0x447f3`: `if (abs(PY - tvy[i]) + abs(PX - tvx[i]) > 0x5a && g_EnemyMslCount < 4)
EnemyMissile_Add(tvx[i], tvy[i] - 32, Rand(2) + 1, 0xf);`
`TV_FireShell @ 0x448d7`: `if (g_ShellCount < 10) EnemyShell_Add(tvx[i], tvy[i], Rand(10)+10, Sign(PX - tvx[i])*16,
min((PY - tvy[i])/4, -1) - Rand(4), size not written);`
Explosion_Damage's vehicle loop (weapons.md §8.1 (1)) iterates `0x900e0`, which nothing ever sets (BSS 0,
only decremented here; verified by a byte scan for the address) — **these vehicles can never be destroyed**
(state ≥ 2 is unreachable) and they give no score (Q6).

---------------------------------------------------------------------------------------------------

## 10. Player-side moving objects

### 10.1 Flamer_Update @ 0x17c56 (was Turrets_Update) (verified)
Flame chain spawned by Player_Weapons' flamer branch (weapons.md §4.1). Arrays (index 0..10, count 0x90608):
`flx[]@0x9142c, fly[]@0x91458, flf[]@0x91500` (frame); direction 0x905f4/0x905f8 (px/2 per frame), power 0x90558.
```c
g_Scratch690 = 0;
for (i = 0; i < n; i++) {
  if (IsOnScreen(g_CamX, g_CamY, flx[i], fly[i])) Sprite_Queue(flx[i] - g_CamX, fly[i] - g_CamY, 0x8fc18[flf[i]]);
  if (flx[i] >= 0) { g_Scratch690 = 1; flx[i] += 0x905f4*2; fly[i] += 0x905f8*2; }
  if (++flf[i] > 5) { flx[i] = -1; flf[i] = 0; } }
m = n (0x9098c);  flx[m] = flx[0] + 0x905f4*m*2;  fly[m] = min(fly[0] + 0x905f8*2*m, 0x3e0);  flf[m] = 0;
if (fly[m] >= 0) {
  c = Clamp(Div16(flx[m]) + 1, 0, g_MapWidth - 1);  r = Clamp(Div16(fly[m]) + 1, 0, 63);   /* 0x9060c/0x90610 */
  g_TrigClass = Map_GetTileAttr(c, r, 0);
  if (g_TrigClass > 0x7e) { 0x907b8 = 1; Explosion_Damage(c*16, r*16, 0, 0, 0x90558*20 + 10, 0x90558*20 + 10); } }
for (j = 0; j < g_EnemyAirCount; j++)
  if (BoxOverlap(flx[m], fly[m], ax[j], ay[j], 0x20, 0x20)) {
    ahp[j] -= Rand(4);  if (ahp[j] < 0) { adm[j] -= ahp[j]; ahp[j] = 0; }
    if (adm[j] > 2 && aburn[j] == 0) { aburn[j] = 1; awx[j] = ax[j]; awy[j] = ay[j]; }
    Explosion_Damage(ax[j], ay[j], 0, 0, 10, 10); }
n++;  if (g_Scratch690 == 0 || n == 11) n = 0;
```
`0x8fc18[0..5]` = flame sprite ids (BSS, written at runtime by the player/flamer set-up; guess).

### 10.2 Commandos_Update @ 0x14b4a (was Flak_Update) (verified)
Soldiers landed by Weapon_DropCommando (weapons.md §5.6: x, y, vx = -2, anim 0). Arrays (index 0..9,
count 0x902e0): `cx[]@0x8ddc0, cy[]@0x8dd98, cvx_[]@0x8dd20, canim[]@0x8dd70, cfall[]@0x8dd48`.
```c
for (i = n - 1; i >= 0; i--) {
  pd = 1 (0x9098c); pu = 0 (0x907f8);
  if (IsOnScreen(g_CamX, g_CamY, cx[i], cy[i])) {
    if ((cvx_[i] > 0) + 0x1d7 < 0x1de) Sprite_Queue(cx[i] - g_CamX, cy[i] - g_CamY, canim[i] + 0x1da);  /* always */
    else Sprite_Queue(..., 0x1dd);
    canim[i] = Wrap(canim[i] + 1, 0, 2);
    pd = Video_ReadPixel(cx[i] - 0x904f0*16, cy[i] - 0x904f4*16 + 2, g_BackPage);   /* below feet  */
    pu = Video_ReadPixel(cx[i] - 0x904f0*16, cy[i] - 0x904f4*16,     g_BackPage);   /* at feet     */
  }
  a = (Div16(cy[i] + 0x11) < 0x40) ? Map_GetTileAttr(Div16(cx[i]), Div16(cy[i] + 0x11), 0)
                                   : Map_GetTileAttr(Div16(cx[i]), 0x3f, 0);           /* 0x90830 */
  g_Scratch690 = 1;
  if (pd > 0 && a > 0x7e && pu > 0) cy[i]--;                       /* climb slope                      */
  if (pd > 0 && a == 0x82) { if (cvx_[i] < 1) cvx_[i] = -cvx_[i]; else cx[i] = -999; }   /* water: turn, then drown */
  if (pd == 0 || a < 0x7f) {                                       /* falling                          */
    cy[i] = min(cy[i] + 1, 0x3ef);  if (++cfall[i] > 2) canim[i] = 99; }
  else { cfall[i] = 0;
    if ((Map_GetTileAttr(Div16(cx[i]), min(Div16(cy[i]) + 1, 0x3f), 3) & 0x20) == 0) cx[i] += cvx_[i];   /* walk */
    else { Explosion_Damage(cx[i], cy[i] + 16, 0, 0, 2000, 2000); cx[i] = -999; } }   /* demolition tile */
  if (g_TargetMarkX - 12 < cx[i] && cx[i] < g_TargetMarkX + 12) {   /* reached the target marker        */
    Explosion_Damage(cx[i], cy[i] + 16, 0, 0, 2000, 2000); cx[i] = -999; }
  if (cx[i] < 1) remove i over {cx, cy, cvx_} only (Q15b);
}
```
The draw test `(vx>0)+0x1d7 < 0x1de` is always true (walking frames 0x1da..0x1dc; `canim = 99` after a fall
draws sprite 0x1da+99 = 0x23d). Off-screen soldiers see `pd = 1, pu = 0`: they never climb or fall.

### 10.3 Building_Update @ 0x149ae — emplacement under construction (verified)
Started by Map_TriggerColumn (level.md §5.2: col 0x902c0, row 0x902c4, kind 0x902bc = Rand(1 or 2),
timer 0x902dc = Rand(100)+40).
```c
if (IsOnScreen(g_CamX, g_CamY, col << 4, row << 4)) Sprite_Queue(col*16 - g_CamX, row*16 - g_CamY + 15, 0x1e7);
if (--0x902dc == 0) {
  t = 0; for (k = 0; k < 256; k++) if (Byte_Get(g_MapVal, k) == kind + 0x83) { t = k; break; }  /* via g_LoopI = 999 */
  Map_SetTile(col, row, t);  col = 0;                    /* becomes a flak (0x83) / SAM (0x84) / gun (0x85) tile */
}
```
The new tile becomes a live object only when the MP2 trigger later reaches that column with a direct row
entry (level.md §5) — guess: in practice the built site is just a target tile (likely).

---------------------------------------------------------------------------------------------------

## 11. Airbase

### 11.1 Airbase_Update @ 0x2bc0b (verified; GF 39 guard: camera near the base and on ground/0x8ff08)
Ground line `GY = 0x3df - g_BaseYOff` (0x901ac). `0x8ff08` = "some airbase vehicle on screen" (cleared first).
`g_Speed` 0x90094 is a float compared as bits (`0x3f800000` = 1.0f, `0x40000000` = 2.0f, `0x3fffffff`).
```c
/* fuel truck x 0x901fc, sprite 0x72 */
if (IsOnScreen(g_CamX, g_CamY, 0x901fc, GY)) { Sprite_Queue(0x901fc - g_CamX, GY - g_CamY, 0x72); 0x8ff08 = 1; }
if (g_DamageLevel 0x900d8 == 0 && g_Speed == 0 && g_Fuel 0x905b8 < g_FuelBase 0x90378) {
  d = PX - 0x901fc;  0x901fc = Clamp(0x901fc + Sign(d)*min(abs(d), 4), max(g_CamX - 0x40, g_BaseStartX), min(g_CamX + 0x14a, g_BaseEndX - 0x20));
} else 0x901fc = min(0x901fc + 4, g_BaseEndX - 0x20);
if (0x901fc <= PX + 16 && PX <= 0x901fc && g_Speed == 0 && g_OnGround == 1) {          /* refuel        */
  0x901fc = PX + 16;  g_Fuel = min(g_Fuel + 1000, g_FuelBase + 0x90648*1000); }        /* fuel pods     */
/* mission-complete jeep x 0x903d8, sprite 0x79 (|0x8000 mirrored when the player is right of it) */
if (g_MissionResult != 0) {
  if (IsOnScreen(...0x903d8, GY)) { Sprite_Queue(0x903d8 - g_CamX, GY - g_CamY, (PX - 0x903d8 > 0 ? 0x8000 : 0) + 0x79); 0x8ff08 = 1; }
  if (*(int*)&g_Speed < 0x3f800000) {
    0x903d8 = Clamp(0x903d8 + Sign(PX - 0x903d8)*5, max(g_CamX - 0x40, g_BaseStartX), min(g_CamX + 0x14a, g_BaseEndX - 0x20));
    if (0x903d8 <= PX + 6 && PX - 6 <= 0x903d8 && g_EjectState == 0 && g_OnGround == 1) g_MissionResult = 2; } }
/* fire engine x 0x905d4, sprites 0x73/0x74 */
if (g_DamageLevel > 0 && 0x90600 == 0) { 0x90600 = 1; 0x905d4 = g_BaseEndX; }
if (IsOnScreen(...0x905d4, GY)) { Sprite_Queue(0x905d4 - g_CamX, GY - g_CamY, 0x73); Sprite_Queue(+0x20, 0x74); 0x8ff08 = 1; }
if (g_DamageLevel < 1 || *(int*)&g_Speed > 0x3fffffff) 0x905d4 = min(0x905d4 + 4, g_BaseEndX - 0x30);
else 0x905d4 = Clamp(0x905d4 + Sign(PX - 0x905d4 + 0x3c)*6, g_BaseStartX, min(g_CamX + 0x14a, g_BaseEndX - 0x30));
if (0x905d4 <= PX + 0x20) 0x905d4 += 4;
if (g_OnGround > 0 && g_DamageLevel > 0 && *(int*)&g_Speed < 0x3f800000 && IsOnScreen(...0x905d4, GY)) FireEngine_Spray();
/* rearm truck x 0x9082c, sprites 0x76/0x77/0x78 */
if (IsOnScreen(...0x9082c, GY)) { three sprites at +0, +0x10, +0x20; 0x8ff08 = 1; }
if (g_DamageLevel == 0 && g_Speed == 0 && 0x90828 == 0 && 0x90854 > 8) {
  d = PX - 0x9082c;  0x9082c = Clamp(0x9082c + Sign(d)*min(abs(d), 4), max(g_CamX - 0x154, g_BaseStartX), min(g_CamX + 0x14a, g_BaseEndX - 0x30));
  if (0x908f0 == 0 && PX - 0x20 <= 0x9082c && 0x9082c <= PX + 1 && g_Speed == 0) {     /* rearm + repair */
    0x8fa20[0..13] = 0; 0x90654 = 0; 0x90168 = 0x917e4; 0x907d8 = 0x917d4; 0x907dc = 0x917d8; 0x90670 = 1;
    g_Armour = 0x91834 + g_ArmourBonus; 0x905dc = 0x917e8; 0x8ff14 = 0x917ec;  if (0x907b4 == 1) 0x907b4 = 0;
    Sfx_Play(0x18, 4000, 0x3f, PX);  WeaponSelect_Screen();  if (0x90240 == 0) 0x907b4 = 1;
    0x900a4 = 1; 0x90854 = 0; if (0x90828 == 0) 0x90828 = -1; 0x90098 = 0x40; 0x900a8 = g_CamY;
    if (0x90154) Hud_DrawPanel();  0x8ffcc = -1;  if (0x90154 == 1) 0x9046c = 1;  if (g_FogActive) Pal_SaveNight(); }
} else { 0x908f0 = 0; 0x9082c = min(0x9082c + 2, g_BaseEndX - 0x40); }
```
(The `Clamp(v, lo, hi)` bounds above are themselves MIN/MAX macro results without Rand.)
`FireEngine_Spray @ 0x2c8a2` (was FUN_0002c8a2): `g_Scratch690 = 0; if (0x905d4 <= PX + 0x3c && PX - 0x24 <= 0x905d4
&& g_CamY <= GY && GY <= g_CamY + 0xb0) { g_Scratch690 = 1; for (k = 0; k < 6; k++) if (Rand(20) == 10)
g_DamageLevel = max(g_DamageLevel - 1, 0); }  Particle_Spawn((0x905d4 - 12) << 8, (0x3d6 - g_BaseYOff) << 8,
g_Scratch690*0x400 - 0x800, -Rand(0x100), 0x20, 0x10, 0x17);` (Rand(0x100) after the loop).

### 11.2 AirbaseCrew_Update @ 0x2c55b (was Balloons_Update) (verified)
Five crew: `crx[]@0x90c04, cry[]@0x90c18, crs[]@0x90bf0 (sprite 0x1d0..0x1d6), crvx[]@0x90bd0`. Game_Run
initialises only entries 0..3 (game_flow §2.3: x = base+16+Rand(...), y = 0x3df - g_BaseYOff, sprite 0x1d0/0x1d2,
vx -1/+1); **entry 4 is never initialised** (BSS / previous mission values; Q20).
```c
for (i = 0; i < 5; i++) {
  if (!IsOnScreen(g_CamX, g_CamY, crx[i], cry[i])) {
    if (crs[i] > 0x1d3) { crs[i] = Rand(1)*2 + 0x1d0; crvx[i] = (crs[i] == 0x1d2)*2 - 1; } }     /* revive */
  else Sprite_Queue(crx[i] - g_CamX, cry[i] - g_CamY, g_FrameParity*(crs[i] < 0x1d4) + crs[i]);
  crx[i] = Clamp(crx[i] + crvx[i], g_BaseStartX, g_BaseEndX);
  if (crx[i] < g_BaseStartX + 16 || g_BaseEndX - 16 < crx[i]) crvx[i] = abs(crvx[i]) * (1 - 2*(g_BaseEndX - 16 < crx[i]));
  if (crs[i] > 0x1d3) { crs[i] = min(crs[i] + 1, 0x1d6); crvx[i] = (crs[i] == 0x1d6) ? 0 : 4; }  /* squashed  */
  if (abs(crx[i] - g_CamX - g_PlayerScrX) < 0x20 && abs(cry[i] - g_CamY - g_PlayerScrY) < 0x20
      && crs[i] < 0x1d4 && *(int*)&g_Speed > 0x40000000) crs[i] = 0x1d4;                     /* run over  */
}
```

### 11.3 BaseRepair_Update @ 0x3dc4e (was FUN_0003dc4e) (verified)
Repair truck after the runway was bombed (GF 40: `if (0x904ac /*g_BaseHit*/ == 1 && !0x90498) { 0x90498 = 1;
0x90488 = g_BaseEndX; 0x904ac = 0; }`).
```c
if (IsOnScreen(g_CamX, g_CamY, 0x90488, GY)) Sprite_Queue(0x90488 - g_CamX, GY - g_CamY, 0x75);
0x90488 -= 4;
if (g_BaseStartX < 0x90488 && 0x90488 <= g_BaseEndX) {
  0x8fef0 = Div16(0x90488) + 1;  0x8fef8 = 0x3f - Div16(g_BaseYOff);
  if (Map_GetTileAttr(0x8fef0, 0x8fef8, 0) != 0x81) Map_SetTile(0x8fef0, 0x8fef8, g_RunwayFill 0x905f0); }
else 0x90498 = 0;
```

### 11.4 BaseHit_Losses @ 0x3dd5e (was FUN_0003dd5e) (verified)
`0x901c8` = number of base-store hits counted by Map_CraterAt (level.md). GF 57 calls it when nonzero:
```c
if ((g_GameMode & 1) == 0) {
  for (k = 1; k <= 0x901c8; k++) {
    m = -1;
    if (Rand(1) == 0) { p = Rand(0x90350 - 1); g_PlaneUsed[p]++; m = 0x11; }       /* AIRCRAFT IN HANGAR HIT ! */
    else { w = Rand(g_WeaponCount - 1); if (g_WeaponStock[w] > 1) s = g_WeaponStock[w];   /* else s stale (Q21) */
           g_WeaponStock[w] = max(g_WeaponStock[w] - Rand(s), 0); m = 0x10; }   /* WEAPON STORES HIT ! */
  }
  if (m != -1) Hud_PushMessage(HUDTEXT[m]);        /* m = last iteration's (0x90540)                     */
  0x901c8 = 0;
}
```
(In modes 1/3 0x901c8 is never reset.)

---------------------------------------------------------------------------------------------------

## 12. Enemy sprites — Enemy_LoadSpx @ 0x3f991, Enemy_ReplaceSprite @ 0x3fa76 (verified)

Called by Mission_LoadBriefing when the letter changes (`g_EnemySpxLoaded`). File `"plane/" + "enemy" +
('a' + g_EnemySpxLetter) + ".spx"` → LZW → bank 0x849f0. Frames: bank 0..15 → sprites 0x5a..0x69 (16 headings),
bank 16 → 0xdb (parked), bank 0x11..0x16 → sprites 0xc3..0xc8 (fireball/wreck). Enemy_ReplaceSprite =
`Sprites_ReplaceFromBank(0x9098c /*sprite*/, 0x90830 /*frame*/, bank)`.

---------------------------------------------------------------------------------------------------

## 13. Objects inside Player_DeathAndLanding 0x2780b (enemy part only; verified)

### 13.1 Big enemy bomber (0x906dc)
Started by GF 88: campaign only, `0x90760 += 0x8ffec - 0x9071c` (score progress) > 20000 →
`0x906dc = 1; bx 0x9073c = 1; by 0x90740 = -0x578; killed 0x90728 = 0; 0x90750 = 0; hp 0x9072c = 0x14;
0x90734 = 8; escort flag 0x90730 = 0; if (g_EnemyAirCount == 0) { 0x90a5c = 2; EnemyBomber_Spawn(); }`
(with p08 == 0 and no base this spawns one escort fighter at bx+0x40, §2.2).
Per frame (while 0x906dc):
```c
if (IsOnScreen(g_CamX, g_CamY, bx, by)) {
  if (!killed) Sprite_Queue(bx - g_CamX, by - g_CamY, 0x12e);
  else { Sprite_Queue(bx - g_CamX - 0x20, by - g_CamY, 0x12f); Particle_Spawn((bx-0x20)<<8, (by-0x10)<<8, 0,0,0, 0x20, Rand(1));
         Sprite_Queue(bx - g_CamX + 0x90714, by - g_CamY - Div16(0x90714), 0x130);
         Particle_Spawn((bx + 0x90714)<<8, ((by - Div16(0x90714))<<8) - 0x1000, 0,0,0, 0x20, Rand(1)); 0x90714++; } }
bx += 8 - killed*4;  by += killed*16;
if (g_BaseStartX - 0x140 < bx && g_ProjCount < 21 && g_FrameParity == 0 && bx < g_BaseEndX && !killed) {
  g_EnemyBombWeapon = 13; 0x909a8 = bx; 0x909ac = by; 0x909fc = 16; Enemy_DropBomb(); }       /* carpet bombing */
if (g_BaseEndX < bx) by += -2 + 2*(by < -0x640) + 4*(by < -0x708);                          /* climb out    */
if (killed == 1 && 0x90750 == 0) { 0x90750 = 1; 0x90714 = 0x20;
  for (k = 1; k < 5; k++) Explosion_Damage(k*16 + bx - 0x20, by + 16, 0, 0, 2000, 2000); }
if (by < -2000 || g_MapWidth*16 - 16 < bx) 0x906dc = 0;
if (by > 0x100) { Explosion_Damage(bx - 0x20, by, 0,0,2000,2000); Explosion_Damage(bx + 0x90714, by, 0,0,2000,2000); 0x906dc = 0; }
reticle §2.8 with LOCKID 2 (no 0x9c);  ... (rest of the block: player spec)
```
Damage: Explosion_Damage (4) and gun lock 2 (weapons.md); `killed` 0x90728 set when hp < 0. Note that
g_EnemyBombWeapon stays 13 for the rest of the mission (also used by later enemy aircraft) — Q22.

### 13.2 "Nessie" water event (0x90158) — 0x90114 = x of the last water explosion (Explosion_Particles)
Start (GF 87): `if (0x90114 > 0 && Rand(100) == 1 && 0x90158 == 0) { 0x90158 = 1; 0x900cc = 0; nx 0x900d0 = 0x90114;
frame 0x900c8 = 0; ndir 0x900bc = -1; ny 0x900d4 = 0x3df; }`. Per frame:
```c
if (IsOnScreen(g_CamX, g_CamY, nx, ny)) { Sprite_Queue(nx - g_CamX, ny - g_CamY, frame + 0x123);
  if (nx - 0x20 <= PX && PX <= nx + 0x20 && frame < 4 && Rand(100) == 1) { frame = 4; ndir = 1; } }   /* dives */
if (frame == 0 || frame == 3) { nx += ndir*2;
  if (Map_GetTileAttr(Clamp(Div16(nx) + ndir, 0, g_MapWidth), 0x3f, 0) != 0x82) { 0x900cc = 1; ndir = -ndir; } }
if (0x900cc || frame > 3) { frame += ndir; if (frame == 0 || frame == 3) { frame = Clamp(frame, 0, 3); 0x900cc = 0; }
  if (frame == 8) { 0x90158 = 0; 0x90120 = 0; } }
```
(Clamp upper bound `g_MapWidth`, not W-1.) Harmless decoration.

---------------------------------------------------------------------------------------------------

## 14. Particles, debris, bonuses, pickup

### 14.1 Particles (verified)
```c
typedef struct {          /* 0x92d20 + 32*i, i < 40; count g_ParticleCount 0x93228                    */
  int32 x8, y8;           /* +0x00 +0x04  world px << 8                                                */
  int32 vx8, vy8;         /* +0x08 +0x0c                                                               */
  int32 unused;           /* +0x10  5th Particle_Spawn arg, never read                                  */
  int32 life;             /* +0x14  frames                                                             */
  int32 anim;             /* +0x18  row of g_PartAnim                                                   */
  int32 frame;            /* +0x1c  0..7                                                               */
} Particle;
int16 g_PartAnim[][8] @0x8017c;   /* sprite ids; -1 = loop to column 0; 0 = end (particle removed)    */
```
Rows (verified dump): 0 {54,55,54,130..134}, 1 {54,55,-1}, 2 {175..179,0}, 3 {180..184,0}, 4..10 {238+2k,239+2k,-1},
11 {135,136,137,-1}, 12 {509..515,0}, 13 {254..259,-1}, 14 {260..263,-1}, 15 {260..263,277,276,275,0},
16 {264..268,0}, 17 {273,274,-1}, 18 {275,276,277,277,277,0}, 19 {286..290,-1}, 20 {299,300,301,0},
21 {269..272,-1}, 22 {404..407,-1}, 23 {125,126,127,-1}, **24 all zero**, 25 {449..452,0}.
`Particles_Clear @ 0x2653b`: count = 0. `Particles_Nop @ 0x26520`: empty.
`Particle_Spawn @ 0x26560 (x8, y8, vx8, vy8, a5, life, anim)`: if count < 40, append with frame 0.
`Particles_Update @ 0x2661e (camX, camY)`:
```c
for (i = 0; i < count; i++) {
  dead = 0;  X = p[i].x8 >> 8;  Y = p[i].y8 >> 8;               /* arithmetic shift                       */
  if (IsOnScreen(camX, camY, X, Y)) {                           /* frozen and not animated off-screen     */
    f = p[i].frame++;  if (f == 7) p[i].frame = 0;
    s = g_PartAnim[p[i].anim][f];
    if (s == -1) { p[i].frame = 0; s = g_PartAnim[p[i].anim][0]; }
    if (s == 0) dead = 1;
    else { p[i].x8 += p[i].vx8; p[i].y8 += p[i].vy8; Sprite_Queue(X - camX, Y - camY, s); }  /* old position */
  }
  if (p[i].life < 1) dead = 1;   p[i].life--;
  if (dead) { p[i] = p[--count]; i--; }                         /* copy last (8 dwords) into i, redo i    */
}
```
Particles have no gravity. Anim 24 (EnemyGround smoke) ends immediately (Q12).

### 14.2 Debris_Update @ 0x3b27c / Debris_Spawn @ 0x3b5c0 (player wreck, GF 24) (verified)
Reuse the player-projectile arrays (weapons.md §2.2) after the crash: Debris_Update first calls Debris_Spawn
once (`0x90628` latch, reset by the player spec). Spawn: `g_ProjCount = Rand(6) + 4; for i: x = wx 0x90a18 +
Rand(8) - Rand(8); y = wy 0x90a1c; vx = 0x90778 + Rand(2) - Rand(2); vy = (0x90778 == 0) ? 0 : -Rand(abs(0x90778)*2);
blastA (frame) = Rand(6);` (Rand order per piece: 8, 8, 2, 2, [abs*2], 6). Update, for i = n-1..0: draw
`blastA[i] + 0x3e`; `if (blastA[i] < 4) blastA[i] = (blastA[i] + 1) & 3;` x += vx, y += vy, vy = min(vy+1, 11);
`if (y > 1000) x = 0;` store; `blastA[i] = Clamp(blastA[i], 0, 7)`; `if (x == 0) remove i over {x, y, vx, vy, blastA}`.

### 14.3 Bonus crate (verified)
Globals: type `g_BonusType` 0x90890 (0 none, 1..11), x 0x90894, y 0x90898, chute frame 0x9088c.
`Bonus_Spawn @ 0x16f13` (callers: GF 75 with 0x909a8/0x909ac = (PX, g_CamY - 300); EnemyAir kill §2.4 with the
aircraft position):
```c
if (g_Score[0] < g_ExtraAircraftScore) {                         /* g_Score[0], not [g_AeroPlayer] (Q23) */
  if (g_BonusType == 0) { g_BonusType = Rand(10) + 1; 0x90894 = 0x909a8; 0x90898 = 0x909ac; }
  if (g_NextBonusScore <= g_Score[0]) {
    g_NextBonusScore = max(g_NextBonusScore + g_BonusScoreStep, g_Score[0] + 1000);
    g_BonusScoreStep = max(g_BonusScoreStep + 1000, g_MissionBonus);
    Hud_PushMessage("next bonus at  " + itoa(g_NextBonusScore)); }
} else { g_BonusType = 11; 0x90894 = 0x909a8; 0x90898 = 0x909ac;
         do g_ExtraAircraftScore <<= 1; while (g_ExtraAircraftScore < g_Score[g_AeroPlayer]); }
```
`Bonus_Update @ 0x17072`:
```c
if (IsOnScreen(g_CamX, g_CamY, bx, min(by, 0x3de))) {
  if (by < 0x3e0) Sprite_Queue(bx - g_CamX, by - g_CamY, 0x9088c + 0x31);            /* parachute      */
  Sprite_Queue(bx - g_CamX, min(by, 0x3de) - g_CamY, g_BonusSprites[g_BonusType]);
  if (BoxOverlap(bx, min(by, 0x3de), PX, PY, 0x20, 0x20)) Bonus_Award(); }
0x9088c = min(0x9088c + 1, 4);  by += 4 - (0x9088c == 4);  if (by > 0x51c) g_BonusType = 0;
```
`Bonus_Award @ 0x17214`: dispatch on g_BonusType, then message:
| type | handler (address, name) | effect |
|---|---|---|
| 0 | 0x17353 Bonus_FireExtinguisher | `if (g_DamageLevel < 1) 0x906a8++; else g_DamageLevel = g_DamageLevel - Rand(1) - 1` |
| 1 | 0x17399 Bonus_AmmoPod | `0x909e4 = min(0x909e4+1, 4); g_GunAmmo += 100` |
| 2 | 0x173e3 Bonus_FuelPod | `0x90648 = min(+1, 4); g_Fuel += 1000` |
| 3 | 0x17430 Bonus_Repair | clear 0x8fa20[0..13], 0x90654 = 0, restore stats from the plane record (as §11.1 rearm, but 0x8ff14 = 1 only if 0x917e8 == 1) |
| 4 | 0x174db Bonus_AutoEject | `0x90314 += 4; Hud_PushMessage("Green and Left to trigger")` |
| 5 | 0x1750b Bonus_ECM | `0x90710 = 1` |
| 6 | 0x17530 Bonus_Armour | `g_ArmourBonus = min(+1, 4); g_Armour = max(g_Armour + 1, 0)` |
| 7 | 0x17597 **Bonus_Saver** | `0x90888 = min(+1, 4)` (bonuses survive that many lost planes, Game_Run) |
| 8 | 0x175da Bonus_FirePower | `g_GunPowerUp 0x90544 = min(+1, 3)` |
| 9 | 0x1761d **Bonus_FirePower2** | `g_GunPowerUp2 0x90944 = 1` |
| 10 | 0x17642 **Bonus_ExtraLife** | `g_Lives++` |
| 11 | 0x17663 **Bonus_ExtraEject** | `g_PracticeEjects 0x9007c++` |
Message: `m = type + 0x2b; if (type == 10) { m = 0; "Extra Aircraft"; } if (type == 11) { m = 0; "Auto Eject"; }
if (m) Hud_PushMessage(HUDTEXT[m]);` then `g_BonusType = 0`. HUDTEXT 44..53 = FIRE EXTINGUISHER, AMMO POD, FUEL POD,
AIRCRAFT REPAIRED, AUTO EJECTOR, E.C.M POD, ARMOUR BONUS, BONUS SAVER, FIRE POWER, EXTRA AIRCRAFT; so the text
shown for type t is the name of handler t-1 (Q24). Type 0 is never spawned (Rand(10)+1 ≥ 1).

### 14.4 Pickup_Update @ 0x45859 (prize balloon / Aerolimits pickup; GF 83 starts it) (verified)
```c
if (g_GameMode == 0) { g_TargetMarkX = 0x9092c; g_TargetMarkY = 0x90930; }
if (IsOnScreen(g_CamX, g_CamY, 0x9092c, 0x90930)) {
  Sprite_Queue(0x9092c - g_CamX, 0x90930 - g_CamY, 0x90918 + 0xe0);
  if (BoxOverlap(PX, PY, 0x9092c, 0x90930, 0x20, 0x20)) {
    g_Score[g_AeroPlayer] += 0x90918 * 10000;  0x8ffec = 0;
    if (0x90918 == 2) PrizePlane_Award();  0x90918 = -1; } }
0x90930--;  if (0x90930 < -2000) 0x90918 = -1;                 /* rises 1 px/frame                         */
```
`PrizePlane_Award @ 0x15492` (was FUN_00015492): builds "<0x8b648>" + itoa(0x9084c+1) and passes it and
"rommel " to Hud_Nop (no output); `0x90540 = 1; 0x8f150 = 0x8f154 = (0x90af4 + 1)*0x5a; 0x9084c++` (index of
the next bonus-plane mission, game_flow GF 83). Score: +20000 (prize balloon, type 2), +10000 (Aerolimits, 1).

---------------------------------------------------------------------------------------------------

## 15. Small helpers (named) and dead code

| Address | Name | Status | Summary |
|---|---|---|---|
| 0x3cd9e | Heading_TurnToward | live | `TurnTab[dir][Oct(dx,dy)]` §1 |
| 0x389b0 | EnemyFire_Sfx | live | `if (!0x9018c && Rand(20)==1) { Sfx_Play(0x10,4000,0x3f,PX); 0x9018c=1; 0x900f0=8; } if (!0x9014c && Rand(10)==1) { Sfx_Play(0x1a,4000,0x3f,PX); 0x9014c=1; 0x900f0=8; }` (one-shot voice lines per mission; Rand only when the latch is 0) |
| 0x17944 | Enemy_DropBomb | live | §2.9 |
| 0x17684 | Convoy_LaunchMissile | live | §7.3 |
| 0x3ea8f | Convoy_FireShell | live | §7.3 |
| 0x3e8b3 / 0x3dea2 | Gun_Fire / Gun_Draw | live | §8 |
| 0x3ec5b / 0x3dfaa | Flak_Fire / Flak_Draw | live | §8 |
| 0x3ef72 / 0x3e0b2 | SAM_Fire / SAM_Draw | live | §8 |
| 0x447f3 / 0x448d7 | TV_LaunchSAM / TV_FireShell | live | §9 |
| 0x2c8a2 | FireEngine_Spray | live | §11.1 |
| 0x3dc4e / 0x3dd5e | BaseRepair_Update / BaseHit_Losses | live | §11 |
| 0x15492 | PrizePlane_Award | live | §14.4 |
| 0x177db | AgentSmoke_Update | live (via 0x15ce6) | smoke markers of the agent drop (level.md §8.7): for i < 0x90100: if on screen and Rand(1) → `life 0x8ec30[i]--`; every 10th call (`0x900f8`) remove entries with life < 1 over {0x8ec48 x, 0x8ec60 y, 0x8ec30 life} |
| 0x3b743 | Convoy_DeathStub | live | empty loop (`0x8fea4` counts 1..5); args ignored |
| 0x3b6e6 | Debrief_SignX | live (Mission_Debrief) | empty loop i<6, `0x90328 = Sign(0x90328) * PX` |
| 0x268be | Tracer_Stub | live | empty (all shell/gun tracer calls) |
| 0x1821c | Stub_Kind9 | live | empty (enemy bomb kind 9) |
| 0x14abe | (level: Map_SecondaryExplosions) | | |
| 0x18237 | OldProj_CycleFrame | dead | frame = (frame+1) & 3 style wrap 0..3 |
| 0x18274 | OldProj_FrameFromVY | dead | `frame = vy/8 - 3*(vx > 0)` |
| 0x1844f | OldProj_Brake | dead | `frame = Sign(vx)+6; vx -= Sign(vx)*min(|vx|,2); vy = speed` |
| 0x18663 | OldProj_AimMarker | dead | marker aim then 0x19236 |
| 0x18720 | OldProj_Combi | dead | `0x19178` or `0x1a7cb` by lock state |
| 0x18771 | OldProj_Cruise | dead | "Cruise Locked on" variant |
| 0x18bc8 | OldProj_SeaSkim | dead | sea-skimming variant |
| 0x18fca | OldProj_Climb | dead | climb then random base target |
| 0x190aa | OldProj_Proximity | dead | tile 3 below → detonate |
| 0x19153 | OldProj_AimAndSplit | dead | 0x19178 + 0x1968e |
| 0x19178 | OldProj_AimMissionTarget | dead | |
| 0x19236 | OldProj_Steer | dead | |
| 0x19574 | OldProj_SpeedFromV | dead | speed = min(2*max(|vx|,|vy|), 48) |
| 0x19669 | OldProj_Speed32 | dead | speed = 32 |
| 0x1968e | OldProj_Cluster | dead | splits into 6 bomblets |
| 0x19ab5 / 0x19ac2 | OldProj_HerculesDrop | dead (no caller) | cargo drop animation from 0x90520 |
| 0x19c6f | OldProj_Fall | dead | |
| 0x1a7cb | OldProj_HomeAir | dead | proximity vs aircraft / bomber / ground units |
| 0x40f5a / 0x40f67 | OldEnemyMissiles_AtConvoy | dead (no caller) | second missile array 0x90adc/0x90ae8/0x90ac4/0x90ad0 (count 0x906c0) homing on convoy vehicle 0 |
| 0x3e705 / 0x3e712 | OldFlak_Shell | dead (no caller) | shell variant of §8.2 with `Rand(20) < g_Mission && Rand(6)==1` |

---------------------------------------------------------------------------------------------------

## 16. Original bugs and quirks (recommendation; the user decides)

* **Q1** EnemyGround x = `Clamp(vy + dir*2 - 6, 0x280, g_BaseStartX - 0x140)` (the vy array instead of x;
  verified in asm). All gunships snap to x = 0x280 (or g_BaseStartX-0x140 when smaller) on their first update
  and only move vertically. Used by M0 #71/#98/#101. Keep (faithful); PORT note: intended `gx + dir*2 - 6`.
* **Q2** EnemyAir wrap-around near x=0 / x=W*16 teleports the aircraft relative to the player
  (`x += PX - W*16` / `x = PX + W*16 - x`). Keep.
* **Q3** EnemyAir respawn does not reset `abomb`, `aeject`, `astate`; a respawned aircraft whose previous
  pilot ejected dives forever (§2.5 step 14); missiles reset to 4, not g_EnemyMissiles. Keep.
* **Q4** EnemyAir gun tracer arguments divide by `abs(PY-y)` and `abs(PX-x)`: integer divide by zero (DOS4GW
  exception → crash) when the aircraft is exactly level or vertically aligned with the player inside 0x140.
  PORT: guard the division (result unused), keep both Rand(1) calls.
* **Q5** Enemy_DropBomb indexes its jump table with k0+1 (Weapon_Fire uses k0): enemy weapon kinds are
  shifted (ballistic bombs fire as rockets, rockets as guided, k0 9 → commando, k0 ≥ 10 nothing). Keep.
* **Q6** TargetVehicles removal decrements 0x900e0 (count used by Explosion_Damage) instead of 0x900b4; 0x900e0
  is never set, so these vehicles are indestructible and unscored. Keep (faithful); PORT note.
* **Q7** `if (g_Mission < 7) g_TVCount = 0` at the end of every TargetVehicles_Update: in missions 0..6 they
  are drawn/act for one frame only. Keep.
* **Q8** EnemyGround missile limit `< 10` vs the 4-entry missile arrays (0x8fab8..): entries 4..9 overwrite
  0x8fac8.. (my), 0x8fad8 (g_FlareLife), 0x8fae0 (g_FlareX)... Port: allocate the arrays contiguous exactly as
  in the image (mx[4], my[4], flare arrays after) to reproduce, or bound to 4 with a PORT note.
* **Q9** Missiles target the B52 only when it is ≥ 601 px away horizontally and within 599 vertically
  (inverted test). Keep.
* **Q10** EnemyMissiles/AI use stale globals (vx/vy, dx/dy) from the previous object. Keep globals.
* **Q11** Ground-unit missiles have life 3 → explode after ~2 frames. Keep.
* **Q12** EnemyGround damage smoke uses particle anim 24 (all zero) → removed on its first on-screen update.
  Keep (no visual).
* **Q13/Q14** Shell pixels go to absolute VRAM with an undefined colour; shell hit test uses cam X for y.
  See §5. Keep logic; render choice is a PORT decision.
* **Q15** Partial swaps on removal: shells don't swap `ssize`, pilots don't swap `epvy`, commandos don't swap
  anim/fall counters, target vehicles don't swap `tvst`. Keep (separate arrays).
* **Q16** Convoy vehicle types for i ≥ 8 come from p17 (convoy row) nibbles. Keep (max 9 vehicles).
* **Q17** More than 4 distinct convoy types: the lookup loop never runs, vehicle uses the previous slot. Keep.
* **Q18** Truck_LoadSpx two-digit names are built as "11" for every type ≥ 10 (wrong graphics, e.g. TRUCKC11
  instead of TRUCKC14; TRUCKA11 does not exist → fatal load error if ever used with letter a). Keep or fix
  with a PORT note (user decides).
* **Q19** Escorted convoy direction uses 0x90538 (last gun-ray hit x) — the convoy heads to p03 relative to
  where the player last shot. Keep.
* **Q20** Airbase crew entry 4 never initialised. PORT: zero-initialised BSS → x=y=0 off-screen sprite 0
  → revived only if its sprite > 0x1d3; keep zero.
* **Q21** BaseHit_Losses uses an uninitialised `s` when the stock ≤ 1. PORT: s = previous value (static
  local), initially garbage → use 0 with a note.
* **Q22** The big bomber permanently sets g_EnemyBombWeapon = 13. Keep.
* **Q23** Bonus_Spawn tests g_Score[0] (player 1) for thresholds, player g_AeroPlayer for the extra-aircraft
  doubling. Only matters in Aerolimits. Keep.
* **Q24** Bonus message index is type+43, i.e. the name of the *previous* handler (e.g. an ammo pod shows
  "FIRE EXTINGUISHER"). Possibly the handlers are the shifted ones; either way keep.
* **Q25** EnemyBomber_Spawn runs its body once even when asked for 0 aircraft. Keep.
* **Q26** Pilot spawn has no bound (arrays of 10). With ≤ 2 aircraft and one ejection per life this cannot
  overflow in practice (respawns keep aeject = 1). Keep.

## 17. Open questions

1. `0x8fc18[0..5]` (flamer sprites) — writer not found in this pass (guess: Player_Weapons / plane set-up).
2. Exact colour byte of the shell pixels (saved EBP of Game_Run's frame) — needs a DOSBox memory read; and
   whether those pixels are visible in the HUD panel at all.
3. Whether the built emplacement (Building_Update) ever becomes an active MP2 object (depends on MP2 data
   around the column).
4. Who sets 0x90070 semantics (plane record stat, used only by the enemy wander test) — player spec.
5. g_BonusSprites values after GENDAT2/constant overwrite (game_flow §2.1) determine what each bonus looks
   like; combined with Q24 the intended mapping is unclear.
6. The GF 87 Nessie start uses `Rand(100)` (js.c shows `Rand()` with the 100 pushed earlier; verified as
   Rand(100) by the push at 0x20b2x — likely).

## Corrections (phase 5 step D, checked against the disassembly while porting)

1. §8.2 Gun_Fire 0x3e8b3: the height test reads the **flak** row 0x90614 (`0x90614*16 - 0x78 < PY`, 0x3e8c9), not the
   gun's own row 0x9026c. With no flak site active (0x90614 stale or 0) the gun fires at any height.
2. §2.7 the tracer arguments: both divisions are evaluated before the two `Rand(1)` calls (y first, then x), so the
   #DE of Q4 happens before any Rand. The port keeps the fault (QUIRKS.md policy): it stops with a FatalError
   ("Divide by zero in EnemyAir_Update (tracer, Q4)") where the original takes a DOS/4GW exception; this needs the
   aircraft exactly level with (or exactly above) the player while it fires inside 0x140.
3. §13.1 / player.md §6.5: the big bomber's "flares" are enemy missiles: `mx/my[n] = (bx, by), mdir 4, mlife 0x1e,
   0x906fc++`, then `Flare_Release()` (the player's automatic flare), guarded by `0x90a00 in (199, 0x280)`,
   `0x906fc < 4`, not killed, `0x90734 > 0`, `Rand(6) > 3`.
4. §9 TargetVehicles_Update: the removal never touches g_TVCount 0x900b4 (only 0x900e0--, Q6): the dead entry
   (x 0) is swapped to the end and stays counted.
5. §12/§7.2 Truck_LoadSpx builds the name in 0x85b48 ("truck" + letter + digits + ".spx", dir "plane/") and copies
   frames to `0x8dab8[0x9098c + f]`, f < 0x90434; the slot base is passed in 0x9098c (the search counter of
   Enemy_SetupSpriteIds, which uses the same global as its loop index).
6. §3.2 the tile probe is `Map_GetTileAttr(Clamp(Div16(x+16), 0, W-1), Clamp(Div16(y+16), 0, 63), 0)` with both
   clamps as MIN/MAX macro pairs (no Rand), stored in 0x908b0.
