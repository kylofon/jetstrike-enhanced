# Player subsystem (JS.EXE): flight model, controls, helicopter/VTOL/glider modes, damage, ejection, support aircraft, HUD

Binary `work/JS.bin` (flat, base 0x10000), decompile `port/decomp/js.c`. Symbols: `port/spec/player_symbols.csv`.
Companion specs (not repeated here): `platform.md` (Rand, Clamp/Sign/Wrap, keys, `Input_ReadControls`, g_Ctrl),
`video.md` (Sprite_Queue/DrawNow, Text_*, Video_* primitives, Hud_DrawRadarLine), `sound.md` (Sfx_Play,
Engine_*), `weapons.md` (gun, racks, projectiles, flares, Explosion_Damage, Bonus/Particle helpers), `level.md`
(map, tile attributes, camera wrap, follow views §6.2), `game_flow.md` (frame-loop order §8.2, mission set-up,
plane select stat copy §6, flight-table init §4).

Confidence: **verified** = read instruction by instruction in the capstone disassembly (all floating point
in this file is verified that way unless marked); **likely** = read from the Ghidra decompile, checked at the
points that matter; **guess** = interpretation of purpose.

## 0. Conventions

* All globals are int32 unless stated. `PX = g_CamX + g_PlayerScrX`, `PY = g_CamY + g_PlayerScrY` (world
  pixels, y down, ground at the bottom of the 1024-px map). `/` on ints = C division (truncates toward 0;
  the binary uses the `sar/sbb` bias idiom). `>>` only where the binary uses a plain `sar`.
* **Floats.** Watcom helpers: `__FSA/__FSS/__FSM/__FSD` = IEEE single add/sub/mul/div of `EAX op EDX` (x87 or
  emulated, both correctly rounded to float); `__FDA/__FDS/__FDM/__FDD` = double ops on `EDX:EAX op ECX:EBX`;
  `__I4FS` int→float, `__I4D` int→double, `__FSFD` float→double, `__FDFS` double→float (round to nearest),
  `__FSI4` float→int and `__FDI4` double→int **truncate toward zero** (verified: 0x4a25c shifts the
  magnitude right and negates; 0x494c8 never adds the rounding bit). `__FSC(a,b)` / `__FDC(a,b)` return
  sign(a-b) (verified by emulating 0x49bb4). A port written with C `float`/`double` (SSE, FLT_EVAL_METHOD 0)
  and `(int)` casts reproduces the results bit-exactly as long as **every expression is evaluated in the
  order and type given here** (each `(float)` below is a real rounding point).
* **Bit-pattern compares.** Several tests compare the float's bits with an int (`cmp dword [g_Speed],
  0x40800000`). Written `bits(x) < 0x40800000`. For x ≥ +0 this equals `x < 4.0f`; for negative floats the
  order is reversed (any negative float < 0x40800000 as signed int). Keep them as integer compares.
* Rand(n) = `rand()%(n+1)` (platform.md). **Every Rand call is listed in order.** C short-circuit decides
  whether a Rand inside a condition runs; conditions are written in source order.
* Frame = one Game_Run iteration (game_flow §8.2 step numbers are quoted as "GF step n").

---------------------------------------------------------------------------------------------------

## 1. Overview and call tree

The RE guide's picture ("Player_Update = flight model") is wrong: the flight model is **inlined in
Game_Run** (0x1ea8c..0x2014d, GF steps 67-73) and in the control block (GF step 80). `Player_Update`
0x2d34e only handles keys (pause, eject, gear, hover switch, autothrottle, follow camera, tow start).
Four functions had misleading names (renamed, see the CSV):

| Address | Old name | New name | What it really is |
|---|---|---|---|
| 0x403c1 | Camera_Update / Player_HoverUpdate | **Player_GliderUpdate** | speed + ridge lift of a glider (thrust stat 0); not hover, not camera |
| 0x29b29 | Player_Collide | **Player_DrawHeli** | queues body + rotor sprites of a helicopter/VTOL from the .HD table; no collision |
| 0x27008 | Player_Ejection | **Tanker_Update** | air-to-air refuelling tanker (refuel drogue, probe, fuel transfer) |
| 0x2780b | Player_DeathAndLanding | **SupportAircraft_Update** | glider tow plane, Fat Albert, B52, ship, campaign bomber + lock-on, player flare shells, ground pickup/winch |
| 0x15957 | Player_LandingCheck | **Carrier_Update** | carrier catapult launch countdown and taxi-to-catapult (called from Mission_CheckComplete) |
| 0x3c600 | FUN_0003c600 | **Player_EjectUpdate** | ejected pilot / parachute |

```
Game_Run frame (game_flow §8.2) — player-owned pieces, in frame order:
 step 24  player sprite (§9.1): Sprite_Queue or Player_DrawHeli 0x29b29; Player_AfterburnerFlame 0x156a2;
          Player_DrawReverseThrust 0x1554b
 step 27  Tanker_Update 0x27008                 (if g_TankerType 0x8ffb8)
 step 28  SupportAircraft_Update 0x2780b        (if any support object)
 step 42  Hud_DrawTargetArrow 0x267da
 step 45  Player_EjectUpdate 0x3c600 (if g_EjectState); Player_EngineFire 0x16b82 (if g_EngineFire)
 step 52  Hud_DrawMessages 0x26e5a
 step 63  ground probes (game_flow) → g_GroundAttr 0x90030, g_HitAttr 0x8ffc4, g_DeckAttr 0x90994
 step 64  Hud_UpdateRadar 0x2ec74 (odd frames)
 step 67  §3.1 velocity        0x1ea8c
 step 68  §3.2 move            0x1ed84
 step 69  §3.3 ground contact / landing / ditching / crash 0x1efbb → Player_DamageSystems, Mission_CheckComplete
          (→ Carrier_Update 0x15957)
 step 70  §3.4 parked attitude, fuel, speed (thrust/drag/gravity, Player_GliderUpdate 0x403c1), engine-out,
          stall entry/exit
 step 71  Player_Ditching 0x15bb6 (g_OnGround == 8)
 step 72  Player_AutoThrottle 0x152a0 (if g_AutoThrottle 0x9097c && !throttle-jam)
 step 73  stall nose-drop; Player_FullPower 0x15cb3
 step 76  Player_Update 0x2d34e (§5)
 step 78  Player_TakeoffAssist 0x1693b
 step 79  auto-eject (game_flow; §7.1 here)
 step 80  controls (§4): Player_Throttle*/Pitch*/Rotate*, Player_PullUp 0x16620
 step 84  g_DirHalf = g_Dir / 2
 step 86  vertical dead band of the camera (§8)
```

---------------------------------------------------------------------------------------------------

## 2. Data

### 2.1 Player state globals (all int32 unless noted)

| Address | Name | Meaning | Conf |
|---|---|---|---|
| 0x90094 | g_Speed | **float**, airspeed 0..6 (ground speed on the runway) | verified |
| 0x90200 | g_Dir | heading 0..63 (planes: 0 = nose left, 16 = up, 32 = right, 48 = down; helicopter: tilt 0..13) | verified |
| 0x901f0 | g_DirHalf | g_Dir/2 (index into the 32-entry tables); set at GF step 84 and by a few routines | verified |
| 0x8ffd4 | g_Throttle | 0..9 | verified |
| 0x90778 | g_PlayerVX | px/frame, -16..16 (shared with the menu g_MenuDX) | verified |
| 0x9077c | g_PlayerVY | px/frame, -16..16, + = down (shared with g_MenuDY) | verified |
| 0x90790 | g_TargetVY | wanted vy this frame | verified |
| 0x8ff38 | g_VXError | `(int)(e0d0[d]*speed - vx)` (also the stall slip test) | verified |
| 0x8ff3c | g_LiftVY | `(int)(e210[d]*speed)` lift component | verified |
| 0x90068 | g_StallSink | stall counter: added to the wanted vy; 0 = flying | verified |
| 0x90098 | g_StallLimit | 16× the max stall sink, 0x40..0xc0 | verified |
| 0x900a8 | g_StallTopY | highest PY reached since the stall began (9999 = none) | verified |
| 0x90440 | g_OnGround | 0 airborne, 1 landed (runway/deck/strip), 8 ditched | verified |
| 0x907fc | g_Crashed | 1 after a crash | verified |
| 0x90824 | g_DeathTimer | frames since the crash (loop ends at 0x20) | likely |
| 0x9099c | g_EjectState | 0 none; 1..29 parachute sequence; 99 safe on the ground/rescued; 100 splat | likely |
| 0x8ff14 | g_GearDown | 1 = undercarriage down | verified |
| 0x904fc | g_HookDown | tailhook down (carrier planes) | likely |
| 0x905b8 | g_Fuel | fuel units (plane select: stat 106 + pods) | verified |
| 0x90654 | g_FuelLeaks | number of fuel-leak hits (100 units per leak per frame) | verified |
| 0x900d8 | g_EngineFire | engine fire level: 1..2 smoke, 3 out of control, >3 dead | likely |
| 0x904cc | g_IsHeli | 1 = helicopter physics (stat 91, or VTOL in hover mode) | verified |
| 0x90550 | g_HeliVX | helicopter wanted horizontal speed (also hover entry speed) | verified |
| 0x907e0 | g_HeliLift | helicopter collective 0..10 (5 = hover) | verified |
| 0x90884 | g_BrakeDrag | wheel-brake drag added on the ground at throttle 0 | verified |
| 0x90958 | g_ReverseThrust | 0 none (plane can't); 1 armed; 2 reverse thrust on (adds 5 to drag) | likely |
| 0x9085c | g_RocketBoost | boost frames active flag (adds to the speed cap); timer 0x908a4 | likely |
| 0x90834 | g_CatapultTimer | frames of +3.0 speed after a catapult launch | verified |
| 0x904b8 | g_TaxiStopTimer | frames of forced deceleration/screen drift (set by Airbase_Update) | likely |
| 0x902b4 | g_Overloaded | 1 when load > max (GF step 58); reduces acceleration limits | verified |
| 0x90abc | g_WheelsOk | 0/1/2: sprite has gear and the attitude allows a landing (§3.3) | verified |
| 0x9015c | g_OverRunway | -1 while PX/PY inside the base rectangle, else 0 | verified |
| 0x90030 / 0x8ffc4 / 0x90994 | g_GroundAttr / g_HitAttr / g_DeckAttr | tile attributes under the plane (GF step 63) | verified (GF) |
| 0x9097c | g_AutoThrottle | autothrottle on | verified |
| 0x90460 | g_GroundParked | 1 while parked slow on the ground (blocks rotation) | verified |
| 0x90464 / 0x904a0 | g_UpHeld / g_DownHeld | g_Ctrl[7]/[10] of the previous frame | verified |
| 0x9044c / 0x90430 | g_LastAction / g_ActionThisFrame | key debounce (platform §4.2; see quirk Q4) | verified |
| 0x8fa20[14] | g_DamageFlags | per damaged system (index = message id, 13 = wing/rotor), drawn as HUD lamps | verified |
| 0x90670 | g_DamageLampsDirty | redraw the 14 lamps | verified |
| 0x92b04 / 0x92b08 / 0x92b10 | g_EffThrottle / g_SpeedTmp / g_SpeedTmp2 | scratch of §3.4 | verified |

### 2.2 Flight tables (built in Mission_Setup, game_flow §4, and rebuilt by Player_Update §5.6)

Plane tables, `a = (float)(i * 0.1963495375)` (0x3fc921fb4d12d84a, i.e. i·π/16 rounded to float), i = 0..31:
```c
e150[i] /*0x8e150*/ = (int)-(cos((double)a)*16.0 + 0.5);   /* gun / flame x */
e0d0[i] /*0x8e0d0*/ = (int)(-cos((double)a) * 8.0);        /* horizontal speed per unit speed */
e290[i] /*0x8e290*/ = (int)(-sin((double)a) * 16.0);       /* gun / flame y */
e210[i] /*0x8e210*/ = (int)(-sin((double)a) * 5.2);        /* lift per unit speed (5.2 = 0x4014cccccccccccd) */
fe10[j] /*0x8fe10*/ = (int)(-cos((double)b) * 8.0);  fe50[j] /*0x8fe50*/ = (int)(-sin((double)b) * 3.0);
       b = (float)(j * 0.392699075) (0x3fd921fb4d12d84a), j = 0..15     (16-direction table, g_Dir/4)
0x8e190 = 16  (= e150[16], i.e. the -cos(π) entry rewritten to 16; it is `e150 + 0x40`)
```
`0x8e190 = 16` overwrites `e150[16]` (address 0x8e150 + 16·4). Helicopter tables (g_IsHeli): `b = (float)(j *
1.047197533333333)` (0x3ff0c1523361e585), j = 0..6: `fe10[j] = (int)(-cos(b)*8.0)`, `fe50[j] = -3`.
`0x863c8[t]` (float, t = throttle 0..9) = `(float)((double)(stat82 * t) / 10.8 + 1.0)`: the speed cap per
throttle notch.

Constant tables from GENDAT3.DAX (game_flow §2.1): heli speed limits per tilt `g_HeliMaxLeft` 0x8deb8[8] =
{16,12,6,2,2,4,4,2}, `g_HeliMaxRight` 0x8e310[8] = {4,4,2,2,6,12,16,2} (index g_Dir/2). Initialised data:
heli frame tables `g_HeliFrameBase` 0x80344[8] = {7,5,3,0,4,6,12,17}, `g_HeliFrameKind` 0x80364[8] =
{5,1,1,3,1,1,5,1}; HUD message timers 0x8031c[10] (0 initially).

### 2.3 Plane stats (MISC.Z words 62..120) → destination globals

PlaneSelect_Screen copies them (game_flow §6, which lists the copy order; float conversions there). Names
and uses (this resolves the open fields of `port/formats/data.md`):

| w | Global | Name | Use (reader) | Conf |
|---|---|---|---|---|
| 62 | 0x90060 | g_GearSilent | gear key plays Sfx 0x13 only when 0 (Player_Update) | likely |
| 63 | 0x90258 | g_NavLightBlink | if set, 0x90238 toggles every frame → alternate (+0x16) sprite frame (GF step 62) | likely |
| 64 | 0x90148 | g_RandomEventChance | FUN_14923 ambient event (game_flow) | verified (GF) |
| 65 | 0x90838 | g_SpecialRackMax | cap on type-10 weapon rounds (Mission_ResetState) | verified (GF) |
| 66 | 0x90358 | g_TwinEngine | afterburner flame drawn twice, offset ±e290/5 (0x156a2) | likely |
| 67 | 0x9008c | g_NeedsTow | glider: TurnR/Right on the ground at speed 0 calls the tow plane (§5.1) | verified |
| 68-72 | 0x90508 0x905a4 0x90560 0x90564 0x90850 | gun parameters | weapons.md | verified (W) |
| 73 | 0x90188 | g_BrakeStrength | `g_BrakeDrag = 3*w73*(fuel>0) + 10` (ThrottleDown) | verified |
| 74 | 0x90450 | g_LandAttrLow | lowest landable attribute = 0x81 - w74 (grass strips) | verified |
| 75 | 0x8ff9c | — | written only (never read) | verified |
| 76 | 0x8fee0 | g_HasWinch | ground pickup uses the winch (SupportAircraft_Update §6.6) | likely |
| 77 | 0x9045c | g_PlaneClass | score/kill multiplier, damage spread; ≠0 = no life lost (game_flow) | verified |
| 78 | 0x908d0 | g_EngineTickRate | engine-sound counter `0x90620 += w78*throttle/2` (Rotate*, GF step 62) | verified |
| 79 | 0x906b0 | g_GunWeapon | built-in gun weapon (weapons.md) | verified (W) |
| 80 | 0x90090 | g_StallSpeed | float w/100; stall when `speed < g_StallSpeed*6.0f`; gear key only below that ×6 | verified |
| 81 | 0x90780 | g_Drag | float w/100 (glider: /5 again) | verified |
| 82 | 0x903b0 | g_TopSpeed | builds 0x863c8 | verified |
| 83 | 0x90674 | g_Thrust | float w/100; 0 → glider (0x90594 = 1) | verified |
| 84 | 0x907d8 | g_MaxSinkAccel | upper clamp of the per-frame vy change | verified |
| 85 | 0x907dc | g_WingAuthority | vx accel clamp ±2·w85, climb accel; 1 ↔ 0 by "WING HOLED" damage | verified |
| 86 | 0x907a0 | g_CruiseSpeed | float w/100; below it lift is reduced and the plane sinks | verified |
| 87 | 0x90954 | g_EjectSeat | ejection impulse (vy -= 8·w87); w87 > 10 → -1 = no parachute | likely |
| 88 | 0x90168 | g_TurnRate | g_Dir step per frame (Rotate*); "CONTROL HIT" toggles it 1 ↔ 0 / n → 1 | verified |
| 89 | 0x905dc | g_FixedGear | 1 = gear cannot retract; weapons fire only when `g_GearDown == w89` | verified |
| 90 | 0x8ff14 | g_GearDown (initial) | gear state at mission start | verified |
| 91 | 0x904cc | g_IsHeli | helicopter | verified |
| 92 | 0x90610 | — | overwritten every frame by Turrets_Update (temp); stat unused | verified |
| 93 | 0x903d4 | g_AirframeType | 0 normal sprite; ≠0 drawn with Player_DrawHeli (.HD); 2 = VTOL (hover switch) | verified |
| 94 | 0x90558 / 0x901f8 | gun power / gun type | weapons.md | verified (W) |
| 95 | 0x90a34 | g_HasAfterburner | flame when throttle > 6 (GF step 24) | verified |
| 96 | 0x90958 | g_ReverseThrust | initial reverse-thrust capability (§4.2) | likely |
| 97 | 0x9090c | g_ReverseExhaustX | x offset of the reverse-thrust sprite | likely |
| 98 | 0x90a48 | g_FlameLength | afterburner flame offset factor | verified |
| 99 | 0x90a4c | — | written only | verified |
| 100 | 0x8ff0c | g_GearHeight | y offset of the wheels: ground probe row and parked screen y | verified |
| 101-105 | racks / loads | | game_flow §7 | verified (GF) |
| 106 | 0x90388 | g_FuelCapacity | game_flow | verified (GF) |
| 107 | 0x90788 | g_PodCapacity | weapons | verified (W) |
| 108 | 0x91834 | g_ArmourBase | armour = w108 + bonus | verified |
| 109, 110 | 0x901dc, 0x901e0 | g_ProbeDX, g_ProbeDY | **refuelling probe** offset (Tanker_Update, sprite 0xa9); not the ejection seat | verified |
| 111 | 0x906d0 | g_EngineKind | sound.md; ==2 turns "WING HOLED" into "ROTOR DAMAGED" | verified |
| 112 | 0x903c0 | g_AmmoBase | gun ammo | verified (GF) |
| 113 | 0x8ff20 | g_ParkAttitude | nose-up attitude on the ground: parked g_Dir = 4·w113 (left) or 32-4·w113 | verified |
| 114 | 0x90644 | g_Seaplane | landable attributes up to 0x81+w114; with 1 water (0x82) is landable with gear up | verified |
| 115 | 0x9006c | g_BurnerFrames | +0x16 sprite at low power; reduces the high-power accel clamps | verified |
| 116, 117 | 0x8ffe4, 0x8ffe8 | g_HookDX, g_HookDY | tailhook present (≠0) and sprite offset | likely |
| 118 | 0x90130 | g_StallNoseDrop | nose drops while stalled | verified |
| 119 | 0x90844 | g_CrashBlast | Explosion_Damage radius at the crash | verified |
| 120 | 0x90070 | g_Conspicuity | enemy aircraft attack chance `Rand(10)*w120 > 2` (EnemyAir_Update) | likely |

---------------------------------------------------------------------------------------------------

## 3. Flight model (Game_Run, verified)

`d = g_DirHalf`. Runs every frame after the ground probes (GF step 63).

### 3.1 Velocity (0x1ea8c)

```c
if (!g_Crashed) {
  if (!g_IsHeli) {
    float t = (float)e0d0[d] * g_Speed;                        /* __I4FS, __FSM */
    g_VXError = (int)(t - (float)g_PlayerVX);                  /* __FSS, __FSI4 */
    if (bits(g_Speed) >= 0x40800000 && g_Throttle > 8)
         g_PlayerVX += Clamp(g_VXError, -2*w85 + g_Overloaded + w115,  2*w85 - g_Overloaded - w115);
    else g_PlayerVX += Clamp(g_VXError, -2*w85 + g_Overloaded,        2*w85 - g_Overloaded);
    g_PlayerVX = Clamp(g_PlayerVX, -16, 16);
    g_LiftVY = (int)((float)e210[d] * g_Speed);
    if (g_Speed < g_CruiseSpeed /*FSC*/ && g_LiftVY < 0)
        g_LiftVY = (int)(((float)g_LiftVY * g_Speed) / g_CruiseSpeed);   /* float mul then float div */
    g_TargetVY = (g_OnGround == 0 && g_Speed < g_CruiseSpeed) ? 1 : 0;    /* (OnGround tested first) */
    g_TargetVY = Clamp(g_TargetVY + (g_StallSink + g_LiftVY), -16, 16);
    if (bits(g_Speed) >= 0x40800000 && g_Throttle >= 8)                  /* note: >= 8 here, > 8 above */
         g_PlayerVY += Clamp(g_TargetVY - g_PlayerVY, g_Overloaded - w85 + w115, w84);
    else g_PlayerVY += Clamp(g_TargetVY - g_PlayerVY, g_Overloaded - w85,        w84);
  } else {
    0x90784 = g_HeliVX;
    g_PlayerVX += Sign(0x90784 - g_PlayerVX);
    g_PlayerVY = -16;                         /* overwritten by the lift model in §3.2 */
  }
} else {                                       /* crashed: wreck falls */
  g_HeliLift = (g_HeliLift - 2 < 0) ? 0 : g_HeliLift - 2;
  if (Rand(4) == 1) g_PlayerVX -= Sign(g_PlayerVX);
  if (!g_IsHeli) g_PlayerVY = (g_PlayerVY + 2 < 16) ? g_PlayerVY + 2 : 16;
  else           g_PlayerVY = (g_PlayerVY - 2 < -16) ? -16 : g_PlayerVY - 2;
}
```
(`w85` etc. = the stat globals of §2.3.) The second Clamp argument pair is (lo, hi).

### 3.2 Move (0x1ed84)

```c
g_CamX += g_PlayerVX;
if (abs(g_PlayerVX) == 16 && bits(g_Speed) > 0x40a00000 /*5.0*/)
    g_PlayerScrX = Clamp(g_PlayerScrX - 4*Sign(g_PlayerVX), 0x20, 0x120);   /* fast: plane drifts back */
else if (g_TaxiStopTimer == 0)
    g_PlayerScrX -= 4*Sign(g_PlayerScrX - 0xa0);                             /* re-centre at x 160 */
if (!g_IsHeli) {
  if (g_OnGround == 0 || g_PlayerVY < 0) {
    if (g_CamY < 0x340) g_CamY += Clamp(g_PlayerVY, -16, 16);
    else {                                         /* bottom of the map: the sprite moves */
      int t = g_PlayerVY;                          /* stored in 0x90690 */
      if (abs(g_PlayerVY) > 2)
        t = (int)((((double)(abs(g_PlayerVY) - 2) * 60.0) / (double)g_PlayerScrY + 2.0) * (double)Sign(g_PlayerVY));
      g_PlayerScrY += Clamp(t, -14, 14);
    }
  }
} else {
  int v = ((g_HeliLift - 5) * g_PlayerVY) / 10;    /* = -(lift-5)*16/10: 5 hovers, >5 climbs */
  if (g_OnGround == 0 || v < 0) {
    if (g_CamY < 0x340) g_CamY += Clamp(v, -16, 16);
    else                g_PlayerScrY += Clamp(v, -16, 16);
  }
}
```
(game_flow §8.3 B gives `Sign(vx)` and 0x28 for the drift; the binary multiplies by 4 (`shl eax,2`) and
centres on 0xa0 — this file is authoritative.)

### 3.3 Ground contact (0x1efbb..0x1f4d9)

```c
/* glider tow plane: keep it at most 0x60 px ahead */
if (g_TowX > 0 && (PX - 0x50 > g_TowX || g_OnGround == 0) && g_TowFrame /*0x8ff88*/ > -17) g_TowX = PX - 0x60;
```
It tests **0x8ff88** (the tow-plane sprite frame, 0..2, always > -17), not the tow state 0x8ff8c, so the
condition reduces to the first two terms. Quirk Q1.

```c
/* wheels/attitude test */
int k = (g_IsHeli*16 + d) * 2;                                 /* pair index into 0x8fcb8 */
g_WheelsOk = (dirSprite[k] != dirSprite[k+1]                   /* 0x8fcb8 / 0x8fcbc: gear-down frame differs */
              && (fe50[d/2] <= 0 || g_IsHeli == 1)) ? 1 : 0;   /* fe50 index = g_DirHalf/2 (sar idiom) */
if (g_IsHeli == 1) g_WheelsOk = (abs(g_HeliVX) > 12) ? 2 : 1;
g_OnGround = 0;
g_OverRunway = (PX > g_BaseStartX 0x90180 && PX < g_BaseEndX 0x901a8 && PY > 0x3e1 - g_BaseYOff 0x901ac) ? -1 : 0;

/* landing */
if (0x81 - w74 <= g_GroundAttr && g_GroundAttr <= w114 + 0x81
    && (g_GearDown == 1 || (w114 == 1 && g_GroundAttr == 0x82))
    && (g_StallSink == 0 || (g_StallSink < 5 && PY - 17 < g_StallTopY))
    && g_WheelsOk == 1
    && (g_PlayerVY <= 0 || bits(g_Speed) < 0x40400000 /*3.0*/ || g_IsHeli == 1)
    && g_Crashed == 0)
  g_OnGround = 1;
if (g_OnGround == 1 && g_StallSink > 0) { g_StallSink = 0; Player_GearCollapseRoll(); /*0x16d70*/ g_StallSink = 0; }

/* ditching */
if (0x80 <= g_GroundAttr && g_GroundAttr < 0x8c && g_OnGround == 0
    && (g_GearDown == 0 || w89 == 1) && g_StallSink == 0
    && (g_OverRunway == 0 || g_GroundAttr == 0x81)
    && g_WheelsOk == 1
    && (g_PlayerVY <= 0 || bits(g_Speed) < 0x40400000 || g_IsHeli == 1)
    && g_Crashed == 0) {
  g_OnGround = 8;
  g_Speed = (float)((double)g_Speed + -0.02);  if (bits(g_Speed) < 0) g_Speed = 0;
  if (Rand(3) == 1) { g_DamageHits 0x907f8 = 1; Player_DamageSystems(); }
}

/* crash */
if ((PY >= 0x3e0 || (g_HitAttr > 0x7e && g_OverRunway == 0)) && g_Crashed == 0 && g_OnGround == 0) {
  g_Crashed = 1; g_DamageHits = 4;
  Hud_LampBlinkB 0x3f777(); Hud_LampBlinkA 0x3f727(); Player_DamageSystems();
  for (g_LoopI = 0; g_LoopI < 5; g_LoopI++) {
     int life = Rand(1);                        /* arg 7 */
     int y = ((Rand(16) + PY) << 8) - 0x800;    /* evaluated before x */
     int x = ((Rand(16) + PX) << 8) - 0x800;
     Particle_Spawn(x, y, 0, 0, 0, 0x1f4, life);
  }
  Explosion_Damage(PX, PY, g_PlayerVX, 0, w119, w119);
}
if (g_OnGround == 1) Mission_CheckComplete(); else 0x907c0 = 0;
/* taxi stop (set by Airbase_Update) */
if (g_TaxiStopTimer > 0) {
  int old = g_PlayerScrX;
  g_Speed = (float)((double)g_Speed + -0.5); if (bits(g_Speed) < 0) g_Speed = 0;
  g_PlayerScrX--;
  if (--g_TaxiStopTimer == 0 || (bits(g_Speed) == 0 && abs(old - 0x95) < 16)) {
    g_PlayerScrX = 0x94; g_Speed = 0; g_TaxiStopTimer = 0; }
}
```
The `abs(old - 0x95)` uses the value before the decrement (`old-1 - 0x94`). Particle argument order is as
pushed: Particle_Spawn(x, y, 0, 0, 0, 0x1f4, life) with the Rand calls in the order life, y, x (verified).

### 3.4 Attitude on the ground, fuel and speed (0x1f4d9..0x1fd41)

```c
if (g_Crashed == 1 && g_CrashAttr 0x908b8 == 0) {                /* debrief snapshot */
  g_CrashAttr = g_GroundAttr;
  g_CrashTileAttr 0x90908 = Map_GetTileAttr(Clamp((PX + g_ShakeX)/16, 0, g_MapWidth-1), 0x3f, 0);
  g_CrashDir 0x908b4 = d / 4;
}
if (g_OnGround >= 1 && (d > 0x1d || d == 0x11 || d == 0x12) && !g_IsHeli && bits(g_Speed) < 0x40800000) {
  int r = (d == 0x11 || d == 0x12);                              /* facing right */
  g_Dir = r*32 - 2*w113 + (1-r)*4*w113;  g_DirHalf = g_Dir / 2;   /* snap to the parked attitude */
  g_LoopI = g_MapVal[0x400 + PX/16] * 16;                        /* ground top, Byte_Get */
  g_PlayerScrY = (g_OnGround == 1) ? g_LoopI - w100 - g_CamY : g_LoopI - g_CamY;
}
if (g_OnGround != 0 && bits(g_Speed) == 0 && g_ReverseThrust > 0) g_ReverseThrust = 1;
g_GroundParked = 0;
if (g_OnGround == 1 && bits(g_Speed) < 0x40000000 /*2.0*/ && !g_IsHeli) {
  g_Dir = (d < 5 || d > 0x1d) ? 0 : 0x20;
  g_Dir -= 2*w113;
  if (d < 4) g_Dir += 4*w113;
  g_GroundParked = 1; g_StallSink = 0;
}
if (g_EjectState && Rand(1) != 0) { if (--g_Throttle < 0) g_Throttle = 0; }      /* Rand only if ejected */
if (!g_IsHeli) {
  g_Fuel = g_Fuel - g_Throttle - 100*g_FuelLeaks;  if (g_Fuel < 0) g_Fuel = 0;
  if (g_IsGlider 0x90594 == 1) Player_GliderUpdate();                            /* §3.6 */
  else {
    g_EffThrottle = g_Throttle - 2*g_EngineFire
                  - g_OnGround * (7 - 3*(w113 + (g_DeckAttr > 0xfc ? 1 : 0)));   /* carrier deck: +1 */
    if (g_EffThrottle < 0) g_EffThrottle = 0;
    float a = (float)g_EffThrottle * g_Thrust;                                   /* FSM */
    a = (float)((double)a / ((double)(float)g_OnGround + 1.0));
    a = (float)((double)a * ((double)(g_CamY + 0x800) / 2700.0));                /* thinner air higher up */
    a = (float)((double)a - ((double)g_OnGround * 0.001) * (double)w85);         /* rolling friction */
    if (g_ReverseThrust == 2)
         a = (float)((double)a - (double)(((float)g_BrakeDrag + g_Drag) + 5.0f) * 0.0005);
    else a = (float)((double)a - (double)((float)g_BrakeDrag + g_Drag) * 0.0005);
    if (g_OnGround == 0) a = (float)((double)a + (double)e210[d] * 0.15);       /* gravity along the path */
    a = (float)((double)a - (0.002 - (double)(float)g_GearDown * 0.002));        /* gear UP costs 0.002 (sic) */
    g_Speed = g_Speed + a;                                                       /* FSA */
    float lim = (e210[d] > 0) ? 0x863c8[g_Throttle] + 6.0f : 0x863c8[g_Throttle];   /* diving: +6 */
    if (bits(lim) > 0x40c00000) lim = 6.0f;
    float t2 = g_Speed + (float)g_RocketBoost;
    if (lim > t2 /*FSC*/) lim = t2;
    g_Speed = (bits(lim) < 0) ? 0.0f : lim;
    if (g_CatapultTimer > 0) {
      g_Speed = g_Speed + 3.0f;
      if (bits(g_Speed) > 0x40c00000) g_Speed = 3.0f;          /* sic: 3.0, not 6.0 — quirk Q2 */
      g_CatapultTimer--;
    }
  }
} else {                                                        /* helicopter */
  g_StallTopY = 0;
  int burn = (g_FuelLeaks*100 + g_Throttle < 8) ? 8 : g_Throttle + g_FuelLeaks*100;
  g_Fuel = (g_Fuel - burn < 0) ? 0 : g_Fuel - burn;              /* burn evaluated twice, same value */
  g_StallSink = 0;
  g_Speed = (float)Clamp((int)((double)(abs(g_HeliVX)*6) * 0.0625), 0, 6);
  if (g_Ctrl[8] == 0 && g_Ctrl[9] == 0) g_HeliVX -= Sign(g_HeliVX) * g_FrameParity;   /* drift to 0 */
}
```
Notes (verified): the drag constants are 0.0005 = 0x3f40624dd2f1a9fc, 0.001 = 0x3f50624dd2f1a9fc, 0.002 =
0x3f60624dd2f1a9fc, 0.15 = 0x3fc3333333333333, 2700.0 = 0x40a5180000000000. `g_BrakeDrag` is an int
converted with `__I4FS`; `+ 5.0f` and `+ g_Drag` are float adds (FSA). In the gear term, `(float)g_GearDown`
is widened to double and multiplied by 0.002 in double.
"Thrust" here is w83 (0x90674), not `0x90094`. g_FrameParity = 0x905a0.

**Engine out** (0x1fd41): if `g_Fuel == 0 || g_DamageFlags[1] /*0x8fa24 engine fail*/ || g_DamageFlags[11]
/*0x8fa4c throttle jam*/`:
```c
g_EngineWarnCol 0x92b14 += 4*Sign(g_EngineWarnDir 0x80178);       /* initial dir 1 */
g_EngineWarnCol = min(max(g_EngineWarnCol, 0x20), 0x3f);           /* (<=0x20 → 0x20, >=0x3f → 0x3f) */
if (g_EngineWarnCol == 0x20) g_EngineWarnDir = 1;  if (g_EngineWarnCol == 0x3f) g_EngineWarnDir = -1;
Pal_SetColor(0x20, g_EngineWarnCol & 0xff, 0, 0);                  /* red warning lamp pulses */
g_Throttle = 0;
g_HeliLift -= 3; if (g_HeliLift < 4) g_HeliLift = 4;
if (g_FrameCounter50 0x907ec == 0 && g_IsGlider == 0) Sfx_Play(9, 10000, 0x3f, PX);
if (g_IsHeli == 1) { g_Speed = (float)((double)g_Speed + -1.0); if (bits>0x40c00000) g_Speed = 6.0f; if (bits<0) g_Speed = 0; }
```
Exact clamp of the colour: `v = (col <= 0x20) ? 0x20 : col; v = (v >= 0x3f) ? 0x3f : v` (verified).

### 3.5 Stall (0x1fec4..0x2013f)

```c
if (g_OnGround == 0) {
  g_PlaneFlown[g_PlaneIdx 0x90a58] (0x9113c+4*i) = 1;  0x902b0 = 1 (has taken off);  g_CatapultCount 0x90828 = 0;
  g_ReverseThrust = Sign(g_ReverseThrust);
  int stall = (g_Speed < g_StallSpeed * 6.0f)                                   /* FSM then FSC */
           || (Sign(g_VXError) == -Sign(g_PlayerVX) && abs(g_VXError - g_PlayerVX) > 15);
  /* evaluation order: FSC; if not below: Sign(vx), then Sign(VXError), then abs */
  if (stall && g_IsHeli == 0) {
    if (PY < g_StallTopY) g_StallTopY = PY;
    g_StallSink++;
    if (g_StallLimit/16 < g_StallSink) g_StallSink = g_StallLimit/16;
    if (g_StallLimit/16 == g_StallSink && ++g_StallLimit > 0xc0) g_StallLimit = 0xc0;
  }
}
if ((double)g_Speed < (double)g_CruiseSpeed * 0.5 && g_OnGround == 0) {           /* FDC */
  g_Speed = (float)((double)g_Speed + -0.01);  if (bits(g_Speed) < 0) g_Speed = 0;
}
if (!(g_Speed < g_StallSpeed * 6.0f)                                             /* FSC jl → skip */
    && (Sign(g_VXError) == Sign(g_PlayerVX) || abs(g_VXError - g_PlayerVX) < 16)
    && g_IsHeli == 0) {
  if (--g_StallSink < 0) g_StallSink = 0;
  g_StallLimit -= 16; if (g_StallLimit < 0x40) g_StallLimit = 0x40;
  g_StallTopY = 9999;
}
```
`/16` here is the `sbb` bias idiom = C division. The "slip" test (`g_VXError` opposite to the motion and
more than 15 px/frame off) stalls a plane that is flown backwards after a sharp turn. There is **no "Negative
G" message** in the flight code: "Negative G" (0x86378) is a music credit string from GENDAT3 (game_flow §2.1).

After §3.5 (GF steps 71..73, verified):
```c
if (g_OnGround == 8) Player_Ditching();                         /* §3.8 */
if (g_AutoThrottle && !g_DamageFlags[1]) Player_AutoThrottle(); /* §3.9 */
if (g_StallSink > 0) {
  if (0x909a0) Player_FullPower();                              /* 0x15cb3: Hud_PushMessage("FULL POWER STUPID !"); g_Throttle = 9 */
  if (w118) { if (d < 8 || d > 0x18) g_Dir--;  if (d > 7 && d < 0x18) g_Dir++;  if (g_Dir < 0) g_Dir = 0x3f; }
}
if (g_RocketBoost && --g_RocketBoostTimer 0x908a4 == 0) g_RocketBoost = 0;
```
(0x909a0 = "backseat driver" training flag, game_flow.) The nose-drop uses `d` (= g_DirHalf) as it was at
GF step 84 of the previous frame and can push g_Dir to 64 (`g_Dir++` at 63 is not wrapped here; the next
Rotate/`% 0x40` or GF step 84 `g_Dir/2 = 32` reads e0d0[32] = **out of bounds** (0x8e150 = e150[0])). Quirk Q3.

### 3.6 Player_GliderUpdate 0x403c1 — `void(void)` (verified)

Called instead of the thrust model when `g_IsGlider` (w83 == 0). Speed (the expression is evaluated up to
4 times by a min/max macro; all evaluations are identical and side-effect free):
```c
double E = (double)(g_Speed / (float)(g_OnGround + 1))                  /* float divide, then widen */
         - ((double)g_OnGround * 0.001) * (double)w85
         - (double)(((float)g_BrakeDrag + g_Drag) + (float)(g_ReverseThrust == 2 ? 5 : 0)) * 0.002
         + ((double)e210[d] * 0.1) * (double)(g_OnGround == 0)
         + -0.002
         - (double)g_GearDown * 0.02
         + (double)g_RocketBoost;
double r = (E > 6.0) ? 6.0 : E;   if (r < 0) r = 0.0;     /* sign test of the high dword */
g_Speed = (float)r;
```
(0.1 = 0x3fb999999999999a, 0.02 = 0x3f947ae147ae147b, -0.002 = 0xbf60624dd2f1a9fc.) Note the glider uses
0.002 for the drag (planes 0.0005) and 0.1 for gravity (planes 0.15), and **no** thrust, altitude or speed-cap
table.

Ridge lift:
```c
int col = PX / 16;
g_LoopI = g_MapVal[0x400 + (g_MapWidth + col) % 1000];        /* three Byte_Get calls: col, col-1, col+2 */
g_LoopI = g_MapVal[0x400 + (col + g_MapWidth - 1) % 1000];    /* only the LAST value survives */
g_LoopI = g_MapVal[0x400 + (col + g_MapWidth + 2) % 1000];    /* h = ground top row two columns ahead */
int h = g_LoopI;
if (abs(PY/16 - h) < 16 && g_LoopJ 0x9098c < h && h < g_LoopK 0x90830 && h < 0x3c && g_OnGround == 0
    && abs(e0d0[d]) > 2 && (g_TowState == 0 || g_TowState == -17) && g_OverRunway == 0) {
  int l = (abs(PY)/16 - h - 16) / 4;                          /* C divisions */
  g_GliderLift 0x90580 = (l + g_GliderLift > 8) ? 8 : g_GliderLift + l;
}
if (Map_GetTileAttr(PX/16, h, 3) & 0x40) g_PlayerScrY -= 2;   /* table-3 bit 0x40: thermal/updraft tile */
if (g_GliderLift < -3) g_PlayerScrY -= 4; else g_PlayerScrY += g_GliderLift;
g_GliderLift -= Sign(g_GliderLift);
```
Quirk Q5: the bounds `0x9098c < h < 0x90830` are the leftover values of two **global loop counters**
(g_LoopJ, 0x90830) — whatever the last loop that used them this frame left there (e.g. Plane_SetupSprites
leaves both at the sprite count + 1, Enemy_LoadSpx leaves 0x90830 = 0x17, Turrets/projectile loops reuse
0x9098c). Keep them global and shared in the port. Quirk Q6: `% 1000` instead of `% g_MapWidth`.

### 3.7 Player_TakeoffAssist 0x1693b (GF step 78, if `g_GearDown - w89 == 1 && !g_IsHeli && !g_DamageFlags[2]`) — verified

```c
g_SpeedDelta 0x90798 = g_Speed - g_PrevSpeed 0x90284;  g_PrevSpeed = g_Speed;      /* FSS */
g_RotateSpeed 0x8fe9c = g_CruiseSpeed - g_StallSpeed;                                 /* FSS */
g_RotateSpeed = (float)((double)g_RotateSpeed / 1.5 + (double)g_StallSpeed);
if (g_FireHeldGearDown 0x90438 && !g_EjectState && g_Fuel > 0) {
  if (g_WheelsOk == 1 && (double)g_SpeedDelta < 0.02 && g_Speed < g_RotateSpeed + -1.0f
      && !g_DamageFlags[1] && !g_DamageFlags[11]) g_Throttle = min(g_Throttle + 1, 9);
  if (g_WheelsOk == 1 && (double)g_SpeedDelta > -0.02 && g_Speed < g_RotateSpeed
      && !g_DamageFlags[1] && !g_DamageFlags[11] && g_Throttle > 0) g_Throttle--;
  if (g_OnGround == 1 && g_Throttle > 0) g_Throttle--;
  if (g_OnGround == 0) {
    if ((g_Dir < 0x11 && g_Dir > 4) || (g_Dir > 0x1c && g_Dir < 0x1b)) g_Dir -= g_TurnRate;   /* 2nd test never true */
    if ((g_Dir > 0x10 && g_Dir < 0x1d) || g_Dir > 0x2e) { g_Dir += g_TurnRate; g_Dir %= 0x40; }
  }
}
```
(0x90438 = Fire held with the gear down, set at GF step 85 of the previous frame.) Quirk: the second
throttle rule lowers the throttle whenever the plane is slower than the rotate speed and not decelerating,
so the net effect on the runway is to hold the speed near `g_RotateSpeed - 1`; airborne with gear down it
levels the nose toward 4/0x2e. Keep.

### 3.8 Player_Ditching 0x15bb6 (g_OnGround == 8) — verified

```c
if ((double)g_Speed < 0.1) g_DitchStopped 0x9041c++;
if (Rand(5) == 1 && !g_EjectState) Hud_PushMessage(HUDTEXT[9] "OUCH !");
if (Rand(20) == 1 && (double)g_Speed >= (double)g_CruiseSpeed * 0.5) {
  g_PlayerScrY -= Rand(4);
  if (Rand(2) == 1) { int a = Rand(2); int b = Rand(2); g_DirHalf = g_DirHalf + a - b; }
}
```
(Rand(5) is consumed even while ejected — the `&&` tests Rand first.)

### 3.9 Player_AutoThrottle 0x152a0 — verified

```c
if ((g_GearDown == 0 || g_Ctrl[4] == 0) && g_OnGround == 0 && g_Fuel > 0) {
  if ((double)g_Speed < (double)g_CruiseSpeed + 0.5 && g_Throttle < 9) { g_Throttle++; 0x8ffd0 = 1; }
  if (g_Speed > g_CruiseSpeed + 1.0f /*FSA,FSC*/ && g_Throttle > 5) g_Throttle--;
}
```

### 3.10 Player_GearCollapseRoll 0x16d70 — verified
`if (Rand(5) == 1) { g_GearDown = 0; g_DamageFlags[10] (0x8fa48) = 1; Hud_PushMessage(HUDTEXT[38] "UNDERCARRIAGE FAILED !"); g_DamageLampsDirty = 1; }`
Called when the plane touches down while still stalling (hard landing).

### 3.11 Player_PullUp 0x16620 (GF step 80, if 0x909a0 && !heli) — verified
```c
if (PY > 0) {
  0x90598 = Sign(fe10[g_Dir/4]); 0x9059c = Sign(fe50[g_Dir/4]);
  if (0x901bc /* ground-proximity warning */) {
    Hud_PushMessage(HUDTEXT[15] "PULL UP STUPID");  g_Throttle = 9;
    if (g_Dir/4 < 4 || g_Dir/4 > 12) g_Dir += g_TurnRate;
    if (g_Dir/4 > 4 && g_Dir/4 < 13) g_Dir -= g_TurnRate;
  }
  g_Dir = (g_Dir + 0x40) % 0x40;
}
```

### 3.12 Player_EngineFire 0x16b82 (GF step 45, if g_EngineFire) — verified (decompile, ints only)
```c
int a = Rand(8), b = Rand(8);  0x90774 = a - b;                 /* engine shake */
if (Rand(1) != 0) 0x900fc = 0;
int s = Sign(g_EjectState);
if (Rand(200) == s*0x50 + 1000) g_EngineFire++;                 /* never true (Rand(200) <= 200): dead */
if (g_EngineFire == 3) {                                         /* out of control: spiral */
  0x8ff80 = g_SpinPhase 0x900ac + 0x18;
  g_SpinPhase += g_SpinStep 0x900b0;  if (g_SpinPhase == -2 || g_SpinPhase == 2) g_SpinStep = -g_SpinStep;
  int e = 0x8ff80 - g_DirHalf;
  g_Dir += Sign(e) * min(abs(e), 4);  g_Dir = (g_Dir + 0x40) & 0x3f;  g_DirHalf = g_Dir / 2;
  if (g_DirHalf > 0x15 && g_DirHalf < 0x1b && !g_Crashed) {
    Sprite_Queue(0x90a18 - g_CamX, 0x90a1c - g_CamY, ((g_DirHalf - 0x16)/2)*2 + 0x116 + g_FrameParity);
    0x900fc = 0; }
}
if (g_EngineFire > 3 && g_DeathTimer == 0) { 0x9049c = 1; g_Throttle = 0; g_DeathTimer = 0xf; g_Crashed = 1; g_StallSink = 16; }
```
(0x90a18/0x90a1c = the player's drawn world position, GF step 18.)

---------------------------------------------------------------------------------------------------

## 4. Controls (Game_Run GF step 80, 0x20430..0x207c2; verified from the decompile, ints only)

```c
if (!g_Crashed && !g_EjectState && g_EngineFire < 3) {
  g_PrevDir 0x901c4 = g_Dir;
  g_LastAction = g_KeyCode 0x90490;  g_KeyCode = Input_GetKeyCode() /*0x1169d stub → 0*/;
  if (!g_DamageFlags[1] && !g_DamageFlags[11])
     for (g_KeyScan 0x90470 = 2; g_KeyScan < 12; g_KeyScan++) if (g_KeyDown[g_KeyScan]) g_Throttle = g_KeyScan - 2;   /* keys 1..9,0 */
  if (g_DamageFlags[1] && g_Throttle > 0) g_Throttle--;                    /* engine failed: spool down */
  if (0x909dc == 1 && !g_IsHeli) {                                          /* alternative mode (unused?) */
     if (g_Ctrl[2] == 0) { 0x90774 = 0; g_Dir = (g_Dir + 0x40) % 0x40; 0x90620 += (w78*g_Throttle)/2; }
     if (g_Ctrl[0] && !g_Ctrl[1] && !g_Ctrl[2]) Player_ThrottleDown();
     if (g_Ctrl[1] && !g_Ctrl[0] && !g_Ctrl[2]) Player_ThrottleUp();
  } else {
     if (g_Ctrl[0] && !g_Ctrl[1] && !g_Ctrl[2]) Player_ThrottleDown();      /* LShift */
     if (g_Ctrl[1] && !g_Ctrl[0] && !g_Ctrl[2]) Player_ThrottleUp();        /* RShift */
     if (g_Ctrl[8] && !g_Ctrl[2] && (g_IsHeli == 1 || 0x90468 == 1)) Player_PitchDown();   /* Left  */
     if (g_Ctrl[9] && !g_Ctrl[2] && (g_IsHeli == 1 || 0x90468 == 1)) Player_PitchUp();     /* Right */
     if (!g_Ctrl[7] && !g_Ctrl[10] && g_IsHeli == 1 && !g_Ctrl[2]) {         /* heli: collective returns */
        if (bits(g_Speed) < 0x40800000) g_HeliLift += Sign(4 - g_HeliLift); else g_HeliLift = 5; }
     if (g_Ctrl[7]  && !g_GroundParked && !g_Ctrl[2]) Player_RotateA();     /* Up   */
     if (g_Ctrl[10] && !g_GroundParked && !g_Ctrl[2]) Player_RotateB();     /* Down */
     g_UpHeld = g_Ctrl[7];  g_DownHeld = g_Ctrl[10];
  }
  if (g_OnGround == 1 && (dirSprite[(g_Dir/2)*2] == dirSprite[(g_Dir/2)*2+1] || fe50[g_Dir/4] > 0) && !g_IsHeli)
     g_Dir = g_PrevDir;                                   /* no wheel frame / nose too high: undo the turn */
  if (0x909a0 && !g_IsHeli) Player_PullUp();
  if (g_PrevDir != g_Dir && bits(g_Speed) > 0x40a00000 && !g_IsHeli) g_WingVapour 0x8ff2c = 1;
}
```
0x90468 is set to 1 at game start (game_flow §2.1) and never cleared: Left/Right always act as throttle for
planes. g_Ctrl[2] is always 0 (platform Q5). 0x909dc: writer not found in the frame loop (guess: unused
alternative control scheme).

### 4.1 Player_RotateA 0x323b9 / Player_RotateB 0x32476 (verified)
```c
void Player_RotateA(void) {             /* Up: nose anticlockwise */
  if (!g_IsHeli) { g_Dir -= g_TurnRate; g_Dir = (g_Dir + 0x40) % 0x40; 0x90620 += (w78*g_Throttle)/2; }
  else if (g_Fuel > 0 && !g_DamageFlags[0] /*0x8fa20 engine fire*/ && !g_DamageFlags[11]) {
    g_HeliLift += abs(g_HeliVX)/5 + 1;  if (g_HeliLift > 10) g_HeliLift = 10; }
}
void Player_RotateB(void) {             /* Down */
  if (!g_IsHeli) { g_Dir += g_TurnRate; g_Dir = (g_Dir + 0x40) % 0x40; 0x90620 += (w78*g_Throttle)/2; }
  else if (g_HeliLift > 0) g_HeliLift--;
}
```

### 4.2 Player_ThrottleUp 0x32585 / Player_ThrottleDown 0x326d3 / Player_PitchUp 0x324ed / Player_PitchDown 0x32635 (verified)
```c
void Player_ThrottleUp(void) {
  if (!g_IsHeli) {
    g_ReverseThrust = Sign(g_ReverseThrust);
    if (!g_DamageFlags[1] && !g_DamageFlags[11] && g_Fuel > 0) { g_Throttle += 2; if (g_Throttle > 9) g_Throttle = 9; 0x8ffd0 = 1; }
  } else { g_Dir = Clamp(g_Dir + 2, 0, 13); if (g_Dir == 13 && g_Throttle < 9) g_Throttle++; }
}
void Player_ThrottleDown(void) {
  if (!g_IsHeli) {
    if (!g_DamageFlags[1] && !g_DamageFlags[11] && (g_Throttle -= 2) < 0) g_Throttle = 0;   /* -=2 only if both flags clear */
    if (g_Throttle == 0 && g_OnGround == 1) {
      if (g_ReverseThrust == 0) g_BrakeDrag = w73*3*(g_Fuel > 0) + 10;       /* wheel brakes */
      else if (bits(g_Speed) > 0) g_ReverseThrust = 2;                        /* reverse thrust on */
    }
  } else { g_Dir = Clamp(g_Dir - 2, 0, 13); if (g_Dir == 0 && g_Throttle < 9) g_Throttle++; }
}
void Player_PitchUp(void) {
  if (!g_IsHeli) Player_ThrottleUp();
  else { g_HeliVX += 2; g_HeliVX = Clamp(g_HeliVX, -g_HeliMaxLeft[g_Dir/2], g_HeliMaxRight[g_Dir/2]);
         if (g_HeliVX == 0 && g_Throttle > 4) g_Throttle--; }
}
void Player_PitchDown(void) {
  g_BrakeDrag = 0;                                           /* releasing: brakes off every call */
  if (!g_IsHeli) Player_ThrottleDown();
  else { g_HeliVX = Clamp(g_HeliVX - 2, -g_HeliMaxLeft[g_Dir/2], g_HeliMaxRight[g_Dir/2]);
         if (g_HeliVX == 0 && g_Throttle > 4) g_Throttle--; }
}
```
Quirk Q7: PitchDown clears the brakes and then ThrottleDown sets them again at throttle 0, so with Left held
the brake drag stays; once applied it is only cleared by the next PitchDown/Left press — LShift (TurnL) does
not clear it. Keep.

---------------------------------------------------------------------------------------------------

## 5. Player_Update 0x2d34e — `void Player_Update(void)` (keys; verified unless noted)

Called at GF step 76 when `!g_TowX || g_TowState == -17`. In this order:

### 5.1 Input and tow start
```c
g_PrevTurnL 0x90814 = g_Ctrl[0];  0x90820 = g_Ctrl[2];
Input_ReadControls();
g_Confirm 0x905cc = g_Fire;
if (w67 > 0 && (g_Ctrl[1] || g_Ctrl[9]) && g_OnGround == 1 && bits(g_Speed) == 0 && g_TowX 0x8ff94 == 0) {
  g_DirHalf = 0; g_TowVY 0x8ff78 = 0; g_TowRope 0x8ff74 = 0; g_TowTime 0x8ff90 = 0;
  g_TowX = g_CamX + 0x140; g_TowY 0x8ff98 = PY; g_TowFrame 0x8ff88 = 2; g_TowState 0x8ff8c = 0;
}
g_ViewXPrev 0x8ffa4 = g_ViewX; g_ViewYPrev 0x8ff50 = g_ViewY; g_ViewX = g_ViewY = 0;   /* level.md §6.2 */
g_ActionThisFrame = 0;
```
### 5.2 Autothrottle, rocket boost, briefing, pause, abort
```c
if (g_KeyAutoThrottle 0x8454c && g_LastAction != 7) {
  int n = -g_AutoThrottle; g_AutoThrottle = n + 1;              /* 0→1, 1→0 */
  Hud_PushMessage(HUDTEXT[n + 22]);                             /* 22 "AUTOTHROTTLE ON" / 21 "OFF" */
  g_LastAction = 7; g_ActionThisFrame = 1;
}
if (g_Ctrl[2] == 0 || g_Ctrl[8] == 0 || g_Ctrl[0]) 0x9079c = 0;          /* g_Ctrl[2] is always 0 → always */
else if (g_RocketBoost == 0 && g_RocketCount 0x90314 > 0) {               /* DEAD (platform Q5) */
  Hud_PushMessage("Rocket Boost !"); 0x907b0 = 0; g_RocketCount--; g_RocketBoost = 10; g_RocketBoostTimer = 30;
} else if (++0x9079c > 3) 0x907b0 = 1 - 0x907b0;
if (g_KeyBriefing 0x84544 == 0) 0x90084 = 0;
else if (++0x90084 > 8) { Hud_DrawBriefing(); /*0x26b27*/ 0x8f150 = 0x3c; }
if (g_KeyPause) { ... game_flow §8.4 ... }
if (g_KeyAbort && g_OnGround == 0) { g_EngineFire = 3; g_Lives = -8; }
```
### 5.3 Look around and follow camera
Exactly as level.md §6.2 (Looking around / Backspace target cycling, messages "Follow Aborted", "Following
Enemy Aircraft", "Following Ground Force", "Following Fat Albert", "Following B52", "Ready to follow weapon"
built in 0x84a48 and pushed once). The six follow cases and the hold-last-view case follow (level.md table).
Note the follow test for projectiles uses `g_ProjKind[g_ProjCount]` (0x91384 + 4·0x908bc) — the slot **one
past the last projectile**, not the followed one (quirk Q8, keep).

### 5.4 Eject key (E, 0x84548) — the real ejection
```c
if (g_KeyEject && g_EjectState == 0) {
  g_EjectState = 1; 0x903fc = 0;
  g_EjectScrX 0x90270 = g_PlayerScrX; g_EjectScrY 0x90274 = g_PlayerScrY - 2;
  g_EjectCamX 0x9024c = g_CamX; g_EjectCamY 0x90250 = g_CamY;
  g_ChuteVY 0x90968 = (g_PlayerVY > 0 ? g_PlayerVY : 0) - 8*w87;  g_ChuteVY = Clamp(g_ChuteVY, -16, 16);
  0x8ffcc = -1;
  g_ChuteSwing 0x8ff28 = Rand(23);  g_ChuteSwingDir 0x90794 = 16;  0x8fef4 = 16;
  g_PilotX 0x90328 = Sign(g_PilotX) * PX;
  if (w87 != 0) Eject_SetPilotX 0x3b6e6();     /* same assignment again; leaves g_LoopI = 6 */
  g_TowState = -17;                             /* releases a tow */
  g_ChuteFail 0x90298 = 0;
  if (Rand(100) == 1) { Hud_PushMessage(HUDTEXT[23] "*** PARACHUTE FAILURE ***"); g_ChuteFail = 1; }
  else Hud_PushMessage(HUDTEXT[24] "YOU'RE OUT O' HERE !");
  if ((double)g_Speed < 0.1 && g_OnGround == 1) g_EjectState = 99;      /* standing still: just climb out */
}
```
(The auto-eject at GF step 79 is game_flow's; it uses -16 for g_ChuteVY and scrY-8.)

### 5.5 Gear key (U, 0x84558)
```c
if (g_KeyGear && (g_OnGround != 0 || !(g_Speed > g_StallSpeed * 6.0f))   /* FSM, FSC jle */
    && g_LastAction != 4 && !g_DamageFlags[10] && w89 == 0) {
  g_LastAction = 4; g_ActionThisFrame = 1;
  if (!g_GearLatch 0x8ff48) {
    g_GearDown = 1 - g_GearDown;
    if (w62 == 0) Sfx_Play(0x13, 0x1194, 0x1e, PX);
    Hud_PushMessage(HUDTEXT[g_GearDown + 35]);             /* 35 "UNDERCARRIAGE UP", 36 "...DOWN" */
    if (w116 != 0 && g_CarrierMission 0x90818 == 1) { g_HookDown = g_GearDown; Hud_PushMessage(HUDTEXT[g_GearDown + 56]); }  /* TAILHOOK UP/DOWN */
    g_GearLatch = 1;
  }
} else g_GearLatch = 0;
```
(The gear is lowered/raised only below `6 × stall speed` when airborne — "too fast for gear" is silent.)

### 5.6 Hover/agile switch (Enter, 0x84556) — VTOL (w93 == 2)
```c
if (!g_KeyHover || w93 != 2 || g_LastAction == 5) g_HoverLatch 0x903bc = 0;
else {
  g_LastAction = 5; g_ActionThisFrame = 1;
  if (!g_HoverLatch) {
    if (!g_IsHeli) {
      if (d < 3 || d > 0x1d || (d > 0xd && d < 0x13)) {             /* roughly level */
        Hud_PushMessage(HUDTEXT[8] "HOVER MODE"); g_IsHeli = 1;
        g_Dir = (d < 0xe || d > 0x12) ? 0 : 13;                      /* tilt full left / full right */
        g_HeliVX = e0d0[d] * 2;
        Build_SpeedCaps();                                           /* 0x863c8[i], §2.2 formula, i<10 */
      }
      g_HeliLift = 4;
    } else if (g_Dir/2 == 0 || g_Dir/2 == 6) {                       /* full tilt */
      Hud_PushMessage(HUDTEXT[11] "AGILE MODE");
      if (g_Throttle < 4) g_Throttle = 4;
      g_Dir = (g_Dir/2 == 6) << 5;  g_IsHeli = 0;
      Build_SpeedCaps(); g_HeliLift = 0;
    }
    if (!g_IsHeli) Build_PlaneTables_Bugged();  else Build_HeliTables();
  }
  g_HoverLatch = 1;
}
```
`Build_PlaneTables_Bugged` = the §2.2 plane tables, **except** the 16-entry loop stores the angle as an
**int**: `0x90248 = (int)(0.392699075 * (double)j)` (`__FDI4`) and then `fe10[j] = (int)(-cos((double)0x90248)*8.0)`,
`fe50[j] = (int)(-sin(...)*3.0)` — so after an agile switch fe10/fe50 hold cos/sin of 0,0,0,1,1,1,2,2,2,3,3,
3,4,4,5,5 radians (verified at 0x2e19b). The 32-entry part uses the float angle correctly. Quirk Q9 (affects
gun/flame/pull-up directions after an in-flight switch). `Build_HeliTables` = §2.2 heli loop (7 entries).

### 5.7 Up-release on the ground (VTOL take-off/landing switch and parking)
```c
if (!g_KeyUp && g_UpHeld && !g_IsHeli && g_OnGround == 1 && (double)g_Speed < 0.1
    && g_PlayerVX == 0 && (!g_IsHeli || !w89) && g_LastAction != 6) {
  if (w93 == 2) {
    if (!g_IsHeli) {                                     /* always true here */
      g_IsHeli = 1; Build_SpeedCaps();
      0x902d0 = g_DirHalf / 2; g_Dir = 6; g_DirHalf = 3; g_HeliVX = 0; g_HeliLift = 0; g_Speed = 0;
    } else if (g_Dir/2 != 3) { /* dead: g_IsHeli is 0 here */ }
    if (!g_IsHeli) Build_PlaneTables(); else Build_HeliTables();    /* here the 16-entry loop is correct (float) */
  } else if (g_DirHalf < 8) g_Dir = 0x20 - 4*w113;
  else                      g_Dir = 4*w113;
}
```
The outer condition already requires `g_IsHeli == 0`, so the "back to plane" branch (it resets throttle,
speed, vy, sets g_Dir = 0x902d0 ? 0 : 0x20 and computes three unused locals) is unreachable. Document only.
### 5.8 End
`g_LastAction *= g_ActionThisFrame;`

---------------------------------------------------------------------------------------------------

## 6. SupportAircraft_Update 0x2780b (was Player_DeathAndLanding) — likely (floats verified)

Called at GF step 28 if any of `g_TowX 0x8ff94, 0x90310, g_MP_PickupCol, 0x906dc, 0x90158, 0x90998, 0x90528`.
Sections run in this order; each is independent.

### 6.1 Glider tow plane (g_TowX)
```c
if (g_TowX) {
  if (!IsOnScreen(g_CamX, g_CamY, g_TowX, g_TowY)) { if (g_TowState == -17) g_TowX = 0; }
  else {
    Sprite_Queue(g_TowX - g_CamX, g_TowY - g_CamY, g_TowFrame + 0x1de);
    if (g_TowState > -17 && (g_TowX < PX - 0x50 || g_OnGround == 0)) {
      for (g_LoopI = 0; g_LoopI < 6; g_LoopI++) {                       /* rope sag: -1,0,1,2,2,1 */
        g_LoopJ = g_LoopI - 2*(g_LoopI == 5) - (g_LoopI > 3) - 1;
        int len = (PX - g_TowX < g_TowRope) ? PX - g_TowX : g_TowRope;
        int x = (int)((double)(g_TowX - g_CamX) + (double)(g_LoopI*16) * ((double)len / 96.0));
        Sprite_Queue(x, g_LoopJ + (g_TowY - g_CamY), 0x1e1);
      }
      if (g_TowRope < 0x60) {                                            /* rope still paying out: hook on the ground */
        int len = (PX - g_TowX < g_TowRope) ? PX - g_TowX : g_TowRope;
        Sprite_Queue((int)((double)(g_TowX - g_CamX) + (double)(len*0x60) / 96.0),
                     0x3df - g_BaseYOff - g_CamY, 0x1d2 + g_FrameParity);
      }
    }
  }
  if (g_TowState < -16 || (PX - 0x5c <= g_TowX && g_TowState >= 0 && g_OnGround != 0 && g_TowTime < 1 && g_TowRope != 0x60)) {
    if (g_TowX > 0) {
      if (g_OnGround == 1) { if (bits(g_Speed) == 0) { g_TowX -= 2; g_TowFrame = 1; } g_TowState = 0; g_TowY = 0x3dc - g_BaseYOff; }
      else { g_TowY -= 16; g_TowX -= 6; g_TowFrame = min(g_TowFrame + 1, 2); if (g_TowY < -0x800) g_TowX = 0; }
    }
  } else {                                                               /* towing */
    g_Speed = (float)abs(g_TowState / 8);
    g_DirHalf = 0; g_Dir = 0;
    if (g_TowTime > 30) { g_PlayerScrY += g_TowVY; g_TowVY = (g_TowVY < -7) ? -8 : g_TowVY - 1; }
    if (g_TowRope == 0x60) { g_TowTime++; g_TowState -= g_FrameParity; if (g_TowState < -16) g_TowState = -16; }
    g_TowX = PX - 0x60;  g_TowRope = min(g_TowRope + 2, 0x60);  g_TowY = PY;
    if (g_TowY < -400) g_TowState = -17;                                 /* auto release high up */
    if (g_TowState < -2 && g_TowFrame > 0) g_TowFrame--;
  }
}
```
Fire releases the tow (GF step 77 sets g_TowState = -17).

### 6.2 Fat Albert (0x90528; transport that drops paratroops)
Position 0x90520/0x904c0, state 0x904b0 (0 flying, 1 hit), frame 0x90518 (0 level .. 2 climbing), drop
counter 0x904d8, door 0x904b4, vertical speed 0x904bc. Per frame (ints):
```c
if (IsOnScreen(cam, X, Y)) {
  if (!0x904b0) {
    if (0x904b4 > 0 && 0x90518 == 0) { Sprite_Queue(X-cx-0x24, Y-cy+0xe+0x904b4, 0x199); Sprite_Queue(X-cx-0xd, Y-cy+0xe+0x904b4, 0x19a); }
    if (0x904d8) Sprite_Queue(X-cx+0xc, Y-cy+0x11, 0x1a4 - 0x904d8);
    Sprite_Queue(X-cx, Y-cy, 0x90518 + 0x131);
    /* propeller: two Sprite_GetX/GetY(...) calls (video spec) then
       Sprite_Queue(0x90cd8[f] + (X - w), (Y-cy - h) + 0x90be4[f], 0x90518*2 + 0x19b + g_FrameParity) */
  } else {                                     /* burning wreck: same pattern as 6.3/6.5 with 0x134/0x135, offset 0x90548++ */
    Sprite_Queue(X-cx-0x20, Y-cy, 0x134);  Particle_Spawn((X-0x20)*256, (Y-0x10)*256, 0,0,0,0x20, Rand(1));
    Sprite_Queue(X-cx+o, Y-cy - o/16, 0x135); Particle_Spawn((X+o)*256, (Y - o/16)*256 - 0x1000, 0,0,0,0x20, Rand(1)); 0x90548++;
  }
}
X += 0x9050c*4 - 8;  Y += 0x9050c*16;                       /* 0x9050c = shot down (1) */
if (X < 0x90398 + 0xa0 && 0x904d8 < 3 && X > 3000) 0x904d8++;
if (0x90398 - 0x2d0 < X && g_ProjCount < 0x15 && X < 0x90398 + 0x40 && 0x9050c == 0 && (Rand(10) == 1 || g_ProjCount == 0)) {
  g_EnemyBombWeapon = 0x35; 0x909a8 = X + 0x20; 0x909ac = Y + 9; 0x909fc = 0; EnemyBomber_Drop 0x17944(); }
if (Y > 500) { if (++0x9051c > 0x20) 0x90518 = min(0x90518+1, 2); if (0x9051c > 0x10 && g_FrameParity == 0 && 0x904b4 > 0) 0x904b4--; }
if (X < 3000) { 0x90518 = min(0x90518+1, 2); 0x904d8 = max(0x904d8-1, 0); 0x90398 = 0; if (Y < -0x800) 0x90528 = 0; }
if (0x90518 < 1) 0x904bc = 0;
else { Y += 0x904bc; 0x904bc = max(0x904bc - 1, -6*0x90518); if (Y < 0 && X > 3000) 0x90518--; }
```
(`max(v-1, -6f)`: the binary keeps `v-1` when `-6f <= v-1`.) Rand(10) runs only if the preceding terms
hold (short-circuit). The Particle_Spawn Rand(1) is evaluated before the call (last argument).

### 6.3 B52 (0x90998), position 0x90984/0x90988, hit 0x90970, offset 0x9095c, target x 0x9037c
```c
if (IsOnScreen) {
  if (!hit) { Sprite_Queue(X-cx, Y-cy, 0x138);
              if (Rand(1) != 0 && 0x90978 < 5) { int a = Rand(4); int b = Rand(8); int c = Rand(4);   /* see note */
                Particle_Spawn((X+0x20)*256, (Y+0x10)*256, 0, ...); } }
  else { wreck: 0x139 / 0x198 with Particle_Spawn(..., Rand(1)) twice, 0x9095c++ }
}
X += hit*6 - 12; Y += hit*16;
if (0x9037c < X && Y < -400) Y += 8 - 4*(-600 < 0x90998);
if (Y > 0x100) { Explosion_Damage(X-0x20, Y,0,0,2000,2000); Explosion_Damage(X-0x9095c, Y,0,0,2000,2000); 0x90998 = 0; 0x9037c = 0; }
0x9096c = Wrap(0x9096c + 1, 0, 3);
if (0x9037c - 0x2d0 < X && 0x9096c == 0 && g_ProjCount < 0x15 && X < 0x9037c + 0x50 && !hit) {
   g_EnemyBombWeapon = 0xd; 0x909a8 = X; 0x909ac = Y; 0x909fc = 0; EnemyBomber_Drop(); }   /* carpet bombing */
if (X < 0x9037c - 0x140 && !hit) Y += -4 - 16*(X < 0x640);
if (X < 0 || Y < -0x800) { 0x90998 = 0; 0x9037c = 0; }
```
Contrail particle: Ghidra mangled the arguments; the Rand order is `Rand(1)` (gate), then `Rand(4)`, `Rand(8)`,
`Rand(4)` (likely; check 0x2836x when porting). (The `-600 < 0x90998` compare uses the flag, sic.)

### 6.4 Ship / submarine (0x90158): position 0x900d0/0x900d4, frame 0x900c8 (0..3 surfacing, 4..8 sinking), dir 0x900bc, turning 0x900cc
```c
if (IsOnScreen(...) && (Sprite_Queue(X-cx, Y-cy, 0x900c8 + 0x123), X - 0x20 <= PX) && PX <= X + 0x20
    && 0x900c8 < 4 && Rand(100) == 1) { 0x900c8 = 4; 0x900bc = 1; }               /* dives under the player */
if (0x900c8 == 0 || 0x900c8 == 3) {
  X += 0x900bc*2;
  if (Map_GetTileAttr(Clamp(X/16 + 0x900bc, 0, g_MapWidth), 0x3f, 0) != 0x82) { 0x900cc = 1; 0x900bc = -0x900bc; }
}
if (0x900cc || 0x900c8 > 3) {
  0x900c8 += 0x900bc;
  if (0x900c8 == 0 || 0x900c8 == 3) { 0x900c8 = Clamp(0x900c8, 0, 3); 0x900cc = 0; }
  if (0x900c8 == 8) { 0x90158 = 0; 0x90120 = 0; }
}
```
### 6.5 Campaign bomber with lock-on (0x906dc): position 0x9073c/0x90740, hit 0x90728, offset 0x90714, flares left 0x90734
Draw (0x12e, or wreck 0x12f/0x130 like 6.2); `X += 8 - 4*hit; Y += 16*hit`; bomb run between the base ends:
`if (g_BaseStartX - 0x140 < X && g_ProjCount < 0x15 && g_FrameParity == 0 && X < g_BaseEndX && !hit)
{ g_EnemyBombWeapon = 0xd; 0x909a8 = X; 0x909ac = Y; 0x909fc = 0x10; EnemyBomber_Drop(); }`; climb after
the base `if (g_BaseEndX < X) Y += -2 + 2*(Y < -0x640) + 4*(Y < -0x708)`; on the hit frame (`hit == 1 &&
!0x90750`): `0x90750 = 1; 0x90714 = 0x20; for (k = 1; k < 5; k++) Explosion_Damage(16k + X - 0x20, Y + 16,
0,0,2000,2000)`. End: `if (Y < -2000 || X > W*16 - 16) 0x906dc = 0; if (Y > 0x100) { two Explosion_Damage as
6.3; 0x906dc = 0; }`.
**Lock-on cue** (player's missile aims at it): `0x90a24 = Sign(X - cx - scrX); 0x90a28 = Sign(Y - cy - scrY);
aim = (fe10[g_DirHalf/2], fe50[g_DirHalf/2])` (heli: `aimY = 3; aimX = (g_DirHalf < 6) ? (g_DirHalf/2 == 2 ? 0 : -16) : 16`);
`dist 0x90a00 = |dy| + |dx|`. If `Sign(aimX) == 0x90a24 && Sign(aimY) == 0x90a28`: `range = max(32*max(thrust[rack0],
thrust[rack1]), 0x4b0)` (g_WeaponThrust of g_RackWeapon 0x8fbc8/0x8fbcc); if `|dx|+|dy| < range` { `0x903f0 =
0x9b; 0x90454 = 2;` draw sprite 0x9b at the target clamped to x 8..0x138, y `(8 - 0x23*g_FogRows 0x90308)`..0xa8 }.
**Bomber flares**: `if (dist < 0x280 && dist > 199 && g_FlareCount 0x906fc < 4 && !hit && 0x90734 > 0 && Rand(6) > 3)
{ 0x90734--; flare[n] = (X, Y, life 4, 0x1e); 0x906fc++; Flares_Update(); }` (weapons.md flares).

### 6.6 Player flare shells (0x90310 count; arrays x 0x8fae0, y 0x8fae8, vx 0x8fbb0, vy 0x8fbb8, life 0x8fad8)
```c
for (i = count-1; i >= 0; i--) {         /* while ((g_LoopI = n-1) >= 0) with n reloaded from the swap */
  if (IsOnScreen) Sprite_Queue(x-cx, y-cy, 0xa0);
  x += vx;  y = min(y + vy, 0x3f0);
  vy = (Rand(1) + vy < 9) ? vy + Rand(1) : 8;          /* two Rand(1) calls when < 9 (the first only tests) */
  life--;
  for (j = 0; j < g_FlareCount; j++) if (BoxOverlap(flareX[j], flareY[j], x, y, 0x40, 0x40)) flareLife[j] = min(flareLife[j], 1);
  if (life < 1) { swap slot i with slot count-1 (5 arrays, SwapInt); count--; }
}
```
(Uses the loop counter 0x90724 for j — it is **0x90724 = the engine-sound stat copy** (game_flow §6 sets it from
w111); this loop clobbers it. Quirk Q10, keep.)

### 6.7 Ground pickup / agent rescue (if `g_MP_PickupCol && g_MP_PickupSprite < 1000 && g_CamY > 0`)
Object at 0x909e8/0x909ec, anim 0x90a3c, walking 0x90a38, winch state 0x8feb0 (0 off, 1 lowering, 2 hauling,
3 aborted), winch length 0x8fed0, walk target 0x90a44, picked flag 0x90328 (> 0 = pilot position for an ejected
pilot). The routine (decompile lines 13110-13280) does: draw the person (sprite `g_MP_PickupSprite % 500 +
min(0x90a3c, 3·0x90a38)` or `0x90a3c + 0xd6`), settle it on the terrain by **reading back-page pixels**
(`Video_ReadPixel` below/at its feet: 0 below → fall 1 px (max y 1000), non-0 at its position → rise 1 px),
convoy-carried variant (sprite 500..999: rides vehicle 0x90a40, killed when x < 0 → "AGENT KILLED !",
g_MissionBonus = 0), helicopter winch (w76: sprite 0xab at scrY + 0x8fed0, lowered by Sign toward the person
up to 0x30, hauled up when hooked, "PICKUP COMPLETE !" at length < 4), landing pickup (`g_OnGround == 1 &&
sprite < 500 && (bits(speed) < 0x3f000000 /*0.5*/ || !heli) && outside the base`, or winch touching, or
sprite > 500 near the convoy) → within ±16 px "THANKS BUD !" and the person boards (0x90328 = 1,
g_MP_PickupCol = 0, sprite %= 500), otherwise the person walks toward the plane (`x += Sign(PX - x) *
0x8ff30`). Animation tail: `if (walking || sprite == 0xcd) { if (a < 2) { a = 1 - a; if (Rand(5) == 1) a = 2; }
else if (a < 3) { if (Rand(8) == 0) a = 0; } else a = Clamp(7 - a, 3, 4); }`. Drowning: on water (attr 0x82)
sprites 0xca/0xac become 0xcd. Port this section line by line from the decompile (likely; no floats).

---------------------------------------------------------------------------------------------------

## 7. Ejection, parachute, damage

### 7.1 Player_EjectUpdate 0x3c600 (GF step 45, if g_EjectState) — likely
Sprite table `g_ChuteSprite` 0x90cf8[1..6] = {396,397,454,455,456,457} (GENDAT3), 0x90ce4 = {407,408,0,409,410}.
```c
int spr = g_ChuteSprite[g_EjectState];
if (spr == 0x35) { spr = 0x90ce4[g_ChuteSwayIdx 0x90864 / 2] + 0x35; 0x90864 += 0x90860; if (0x90864 == 0 || 0x90864 == 9) 0x90860 = -0x90860; }
if (g_ChuteVY >= 0 && g_EjectState < 4) {                  /* tumbling seat */
  spr = 0x90964/2 + 0x18e; 0x90964 = Wrap(0x90964 + 1, 0, 7);
  if (!0x90980) { Particle_Spawn(PX*256, PY*256, 0x200, g_ChuteVY << 8, 0x80, 0x14, 0x16); 0x90980 = 1; }
}
Sprite_Queue(g_PlayerScrX, g_PlayerScrY, spr);
if (g_PilotX) {                                            /* pilot marker drifts toward the base */
  Sprite_Queue(g_PilotX - g_EjectCamX, g_PlayerScrY, spr);
  g_MP_PickupCol = max(g_PilotX/16, 1);
  g_PilotX -= (g_ChuteVY < 0) ? 3 : 1;  if (g_PilotX < 1) g_PilotX = 1;  0x909e8 = g_PilotX;
}
if (g_ChuteVY < 0) Stub_11105(g_EjectCamX + scrX + 16, g_EjectCamY + scrY + 18, 0x45, 0x900fc, Rand(2));   /* Rand consumed */
g_ChuteSwing = Clamp(g_ChuteSwing + g_ChuteSwingDir, 0, 0x17);
if (g_ChuteSwing > 0x16 || g_ChuteSwing < 1 || Rand(2) == 1) g_ChuteSwingDir = -g_ChuteSwingDir;
0x8ffcc = -1;
g_EjectCamY += g_ChuteVY;  g_ChuteVY += g_FrameParity;  g_ChuteVY = min(g_ChuteVY, 16 - 12*(g_EjectState == 7));
if (g_ChuteVY < 4 && g_EjectState < 3) g_EjectState = 3 - g_EjectState;
if (g_ChuteVY > 3 && PY > 800) {                            /* low: open the chute, advance the sequence */
  g_EjectState++;
  int cap = min(100 - 95*(w87 == -1), 0x90974 + 7);
  if (g_EjectState > cap) g_EjectState = cap;
  if (g_ChuteFail == 1) { Hud_PushMessage(HUDTEXT[37] "HAD YOU WORRIED"); g_ChuteFail = 0; }
}
g_OverRunway = (g_BaseStartX < g_EjectScrX + g_EjectCamX && g_EjectScrX + g_EjectCamX < g_BaseEndX);
g_EjectAttr 0x90228 = Map_GetTileAttr(Clamp(PX/16 + 1, 0, g_MapWidth), max(PY/16, 0), 0);
if (g_ChuteVY < 0 && g_EjectAttr > 0x7e && !g_OverRunway) { Sprite_Queue(scrX, scrY, 0x4b); g_EjectState = 100; g_ChuteVY = 0; }  /* hit terrain going up */
if (g_EjectCamY > 0x340) {                                  /* landing phase */
  int water = (Map_GetTileAttr(PX/16, 0x3f, 0) == 0x82);  0x8ff44 = water*4;  0x90244 = 0x9f + 4*water;
  if (water) g_CrashAttr = 0x82;
  g_EjectCamY = 0x340;  g_EjectScrY += g_ChuteVY;
  if (g_EjectScrY >= 0x90244 && g_EjectState < 7 && 0x8ff44 < g_EjectState) { Sprite_Queue(scrX, 0x90244, 0x4b); g_EjectState = 100; }   /* splat */
  if (g_EjectScrY >= 0x90244 && g_EjectState > 6) { g_EjectState = max(g_EjectState, 0x18); 0x90974 = max(0x90974, 0x1b); g_ChuteVY = 4; g_EjectScrY = 0x90244; }
  if (g_EjectState == 0x1d && 0x90974 < 0x33) { g_EjectState = 0x1c; 0x90974++; }
  if (0x90974 > 0x1d) g_EjectState = 99;
  if (g_EjectScrY >= 0x90244 && g_EjectState <= 0x8ff44) g_EjectState = 99;
  if (g_EjectState == 99 && g_PilotX > 0) { /* pilot marker becomes the pickup: 0x909ec = 0x3e1 (0x3e5 + sprite 0xcd if drowning on water),
                                                inside the base: g_MP_PickupCol = 0x90a44 = 0x90a3c = 0; g_PilotX = 0 */ }
}
if (w87 == -1 && g_EjectState < 99) g_EjectState = 2;       /* no parachute */
```
g_EjectState ≥ 99 ends the mission attempt (game_flow §8.1); 100 = killed (debrief sarcasm 12/13).

### 7.2 Player_DamageSystems 0x3f2fe — `void(void)`; input g_DamageHits 0x907f8 (verified)
Callers: ditching, crash, enemy hits (0x19e3e, 0x1a263), Explosion_Damage near the player (0x38643, 0x3ef42),
FUN_110bc stub branch (dead).
```c
g_ViewTarget 0x8ff10 = -1;  0x8ffcc = -1;
g_DamageHits = Rand((w77 + 1) * g_DamageHits) + 1 + w77;
if (g_OnGround == 8) g_DamageHits = Rand(g_DamageHits) + 1;
for (g_DmgLoop 0x8fea4 = 1; g_DmgLoop <= g_DamageHits; g_DmgLoop++) {
  if (--g_Armour < 0) {
    if (g_Armour == -1) Sfx_Play(0xf, 12000, 0x20, PX);
    if (!g_EjectState) g_DamageCount 0x9034c++;
    int m = (g_OnGround == 8 || w77 == 10) ? 0 : 1;
    int s = (Rand(m*w77 + 13) < 15) ? Rand(m*w77 + 13) : 14;     /* 2nd Rand only when the 1st < 15 */
    g_LastDamage 0x90540 = s;  if (s == 13 && w111 == 2) g_LastDamage = 15;
    switch (g_LastDamage) { ... table below ... }
    g_DamageFlags[min(g_LastDamage, 13)] = 1;  g_DamageLampsDirty = 1;
    if (!g_EjectState) Hud_PushMessage(g_DamageMsgs + g_LastDamage*0x50);    /* GENDATAD, newline kept */
  }
}
```
| id | Message (GENDATAD) | Handler | Effect |
|---|---|---|---|
| 0 | ENGINE FIRE | 0x3f5c8 | `g_EngineFire++; if (g_FireExtinguishers 0x906a8 > 0) { g_EngineFire -= Rand(2)+1; clamp ≥0; 0x906a8--; } if (g_EngineFire > 0 && !g_OnGround && !g_Crashed) 0x903fc = 0xb;` |
| 1 | ENGINE FAIL | 0x3f645 | `g_Throttle = 0; if airborne && !crashed 0x903fc = 0xc` (flag 1 blocks throttle) |
| 2 | CONTROL HIT | 0x3f688 | `g_TurnRate = (g_TurnRate != 1)` (1 → 0 = no turning; n → 1) |
| 3 | WEAPON 1 FAIL | 0x3f6c2 | `g_RackRounds[0] 0x8fcb0 = 0` |
| 4 | WEAPON 2 FAIL | 0x3f6e7 | `g_RackRounds[1] 0x8fcb4 = 0` |
| 5 | DISPLAY 1 FAIL | 0x3f70c | nothing (lamp only) |
| 6 | DISPLAY 2 FAIL | 0x3f727 Hud_LampBlinkA | draws sprite (shift bank) at panel `(GetX(0x8c)+0x51, GetY(0x8c)+5)` (video) |
| 7 | DISPLAY 3 FAIL | 0x3f777 Hud_LampBlinkB | same with 0xa8 / +0xdd |
| 8 | ALL DISPLAY FAIL | 0x3f7c9 | A + B; `g_DamageFlags[5]=[6]=[7]=1` (radar/speed displays dead: flag 7 = 0x8fa3c jams the radar, flag 5 = 0x8fa34 kills the speed bar §10.3) |
| 9 | FUEL LEAK | 0x3f80c | `g_FuelLeaks++` |
| 10 | U-C FAILURE | 0x3f82d | `if (w89 == 1) g_GearDown = 0` (only fixed-gear planes lose the wheels) |
| 11 | THROTTLE JAM | 0x3f85b | `if (Rand(10) == 1) g_Throttle = Rand(8)` |
| 12 | GUN FAIL | 0x3f894 | `if (Rand(10) == 1) g_GunAmmo 0x909c4 = 0` |
| 13 | WING HOLED | 0x3f8c8 | `if (w85 == 1) { w85 = 0; if airborne && !crashed 0x903fc = 0xd; } else w85 = 1` |
| 14 | EXPLOSION | 0x3f920 | `g_EngineFire = 3` (out of control) |
| 15 | ROTOR DAMAGED | 0x3f8c8 | same as 13 (engine kind 2) |
0x903fc = sarcasm/debrief cause id (game_flow). Quirk Q11: with `m*w77 + 13 = 13` the first Rand returns
0..13 (< 15 always) so a second Rand picks the system, i.e. EXPLOSION (14) is reachable only for class ≥ 2.

---------------------------------------------------------------------------------------------------

## 8. Camera follow inside Game_Run (player-owned lines; verified)

* Horizontal: `g_CamX += g_PlayerVX` and the screen-x drift (§3.2). Wrap: level.md §6.1.
* Vertical: §3.2 (camera moves while `g_CamY < 0x340`, else the sprite), then GF step 86 (0x20ac8):
  `0x905b4 -= Sign(0x905b4); if (g_PlayerScrY < 0x50) { g_CamY += g_PlayerScrY - 0x50; g_PlayerScrY = 0x50; }
  if (g_PlayerScrY > 0xa0) { g_CamY += g_PlayerScrY - 0xa0; g_PlayerScrY = 0xa0; }`, and GF step 6 clamps
  g_CamY to [-2000, 0x340].
* Parked: §3.4 sets `g_PlayerScrY = groundTop*16 - w100 - g_CamY`.
* Ejection: g_EjectCamX/Y + g_EjectScrX/Y replace the camera while g_EjectState (GF steps 4, 18, 47).
* Carrier taxi: Carrier_Update nudges g_CamX by ±4 (§9.3). Follow views / look-around: level.md §6.2.

---------------------------------------------------------------------------------------------------

## 9. Player drawing, carrier, tanker

### 9.1 Player sprite (GF step 24, 0x1d3a0..0x1d4ee; verified)
```c
if (g_DeathTimer < 16) {
  if (IsOnScreen(g_CamX, g_CamY, 0x90a18, 0x90a1c)) {
    if (w93 == 0) {
      0x8ff24 = 0;
      if ((w115 && (bits(g_Speed) < 0x40800000 || g_Throttle < 8)) || 0x90664 == 1 || 0x90238 == 1) 0x8ff24 = 0x16;
      int g = g_GearDown - w89;  int id = dirSprite[g_DirHalf*2 + (g < 0 ? 0 : g)];
      g_PlayerSpriteId 0x90900 = id + 0x8ff24;
      Sprite_Queue(0x90a18 - g_CamX, 0x90a1c - g_CamY, g_PlayerSpriteId);
    } else {
      int save = g_DirHalf;
      if ((g_Ctrl[7] || g_Ctrl[10] || (g_Ctrl[2] && g_Ctrl[5])) && g_DirHalf == 3 && g_PlayerVX == 0 && g_OnGround == 0 && g_IsHeli == 1)
         g_DirHalf = 7;                                              /* climbing/sinking frame */
      int g = g_GearDown - w89;
      0x9339c = Player_DrawHeli(0x90a18 - g_CamX, 0x90a1c - g_CamY, g_IsHeli, 0x907a4 /*camera dx*/, g_DirHalf, g < 0 ? 0 : g, g_FrameParity);
      g_DirHalf = save;
    }
    if (w95 == 1 && g_Throttle > 6) Player_AfterburnerFlame();     /* 0x156a2 */
    if (g_DirHalf == 0 || g_DirHalf == 16) Player_DrawReverseThrust(); /* 0x1554b */
  }
} else Debris_Update();
```
`dirSprite` = 0x8fcb8 pairs (normal, gear-down), built in MainMenu (game_flow §2.1).

**Player_DrawHeli 0x29b29** `int (int x, int y, int heli, int dx, int f, int gear, int rotorAnim)` (verified):
```c
int32 *hd = g_HeliHD 0x84a34;                  /* .HD: 100 records of 4 int32 (byte-swapped BE32) */
if (heli == 1) {
  int kind = g_HeliFrameKind[f]; int base = g_HeliFrameBase[f]; f = base;
  if (kind != 1 && kind > 2) {
    if (kind < 4) f = base + 1 + Sign(dx);                                   /* kind 3: lean left/none/right */
    else if (kind == 5) {
      if (dx < 0) f = (-dx == g_HeliMaxLeft[g_Dir/2]) ? base : base + 1;
      else { f = base + 2; if (dx > 0) f = (dx == g_HeliMaxRight[g_Dir/2]) ? base + 4 : base + 3; }
    }
  }
  if (gear == 1 && w89 == 0) f += 0x12;                                      /* gear-down frames */
} else {                                                                     /* VTOL in plane mode */
  if (gear - w89 == 1 && (f < 3 || f > 0x1d || (f > 0xd && f < 0x13))) f += 0x20;
  f += 0x24;
}
int a, b;  if (hd[f*4] < 1) { a = hd[f*4+2] & 0xffff; b = hd[f*4]; } else { a = hd[f*4]; b = hd[f*4+2]; }
a &= 0xffff;  int r = (-b) & 0xffff;  short ox, oy;              /* ox, oy uninitialised when r == 0 */
if (r) { r += rotorAnim; ox = ((short*)hd)[7]; oy = ((short*)hd)[6]; }  /* record 0 dword 3: hi = dx, lo = dy */
if (hd[f*4] < 0) { Sprite_Queue(x, y, a); Sprite_Queue(ox + x, oy + y, r); }
else             { Sprite_Queue(ox + x, oy + y, r); Sprite_Queue(x, y, a); }
return f;
```
.HD record (after the per-dword byte swap): `{ int32 body; int32 unused; int32 negRotor; int32 offs; }` —
`body` = 1-based frame of the plane bank (bit 15 = mirrored; negative = draw rotor on top), `negRotor` =
minus the rotor frame (bit 15 mirror), `offs` = (dx<<16)|(dy&0xffff). **Quirk Q12:** the rotor offset is
always taken from **record 0** (`hd + 0xc/0xe`), so the per-frame offsets in the files are ignored (e.g.
APACHE: dx = -1, dy = -4 for every frame). Keep for faithfulness. If the rotor id is 0, Sprite_Queue gets id 0
at an uninitialised position — the port must treat id 0 as "draw nothing" (check video.md) or skip it.

**Player_AfterburnerFlame 0x156a2** (verified):
```c
0x902ac = 0;  g_LoopJ = g_Throttle - 6;
for (g_LoopI = 0; g_LoopI < 3; g_LoopI++) {
  0x90278 = (int)(((double)(e150[d] * w98) + (double)(g_LoopJ * (e150[d] * g_LoopI)) * 0.8) / 15.0);
  0x9027c = (int)(((double)(e290[d] * w98) + (double)(g_LoopJ * (e290[d] * g_LoopI)) * 0.8) / 15.0);
  int bx = (0x90a18 - g_CamX) - 0x90278, by = (0x90a1c - g_CamY) - 0x9027c;
  if (w66 == 0) Sprite_Queue(bx, by, Rand(1)*2 + (0xa4 - g_LoopI/2));
  else { Sprite_Queue(e290[d]/5 + bx, by, Rand(1)*2 + 0xa4); Sprite_Queue(bx - e290[d]/5, by, Rand(1)*2 + 0xa4); }
}
0x90920 = Wrap(0x90920 + 1, 0, 2);
```
(`d` = g_DirHalf; the products are int, widened with `__I4D`; 0.8 = 0x3fe999999999999a, 15.0.)

**Player_DrawReverseThrust 0x1554b** (verified ints):
```c
if (g_ReverseThrust == 2 && g_HookDown == 0) {
  int r = (g_DirHalf/2 == 8);  0x90278 = r ? -w97 : w97;  0x902ac = r;
  int y = 0x90a1c - g_CamY;  Sprite_Queue(0x90a18 - g_CamX + 0x90278, Rand(2) + y - 1, r + 0xa1);
}
if (w116 > 0 && g_HookDown == 1 && g_IsHeli == 0) {
  int r = (g_DirHalf/2 == 8);  0x90278 = r ? -w116 : w116;  0x902ac = r;
  Sprite_Queue(0x90a18 - g_CamX + 0x90278, 0x90a1c - g_CamY + w117, r + 0xa9);
}
```

### 9.2 Hud_DrawBriefing 0x26b27 (B held > 8 frames; likely)
Copies g_BriefingText, appends `itoa(0x8dab8[0x918e0[i]])` for each convoy vehicle (debug leftover), then
word-wraps at 0xa0 px (`Text_FitWidth`) and draws each line centred `Text_DrawSmall(0xa0 - w/2 + g_ShakeX,
row*8 + 10 + g_ShakeY, line, 1)`, rows from 7.

### 9.3 Carrier_Update 0x15957 (was Player_LandingCheck; called by Mission_CheckComplete; verified ints)
```c
if (0x902b0 == 1 && p00 /*g_MissionParams[0]*/ != 0) { p00 = 0; 0x907c0 = 0; }   /* "land back" objective done */
if (4*w113 == g_Dir && g_OnGround == 1 && g_CatapultTimer == 0 && g_BaseStartX + 0x7e < PX && PX < g_BaseStartX + 0x86
    && g_DeckAttr > 0xfc && w116 != 0 && g_Throttle == 0) {
  if (g_CatapultCount 0x90828 == 0) g_CatapultCount = 10;
  else { g_CatapultCount--; build "STANDBY TO LAUNCH" + itoa(count) in 0x85448; Stub_26f9c(it) /* empty: never shown */;
         if (g_CatapultCount == 0) { g_CatapultTimer = 10; g_Throttle = 9; 0x8ffcc = 1; 0x90698 = 0x5a; } }
} else g_CatapultCount = 0;
if (g_DeckAttr > 0xfc && g_TaxiStopTimer == 0 && !g_IsHeli && w116 != 0 && g_Throttle > 0 && g_ArmourBase + g_ArmourBonus == g_Armour) {
  if (g_HookDown == 1) { g_HookDown = 0; Hud_PushMessage(HUDTEXT[7] "TAIL HOOK UP"); }
  if (PX > g_BaseStartX + 0x82) { g_Dir = g_DirHalf = 0; g_CamX -= 4; }
  if (PX < g_BaseStartX + 0x82) { g_Dir = g_DirHalf = 0; g_CamX += 4; }
}
```
Quirk Q13: the countdown text goes to an empty stub (0x26f9c), so "STANDBY TO LAUNCH n" is never displayed.
Quirk: the auto-taxi only runs while the armour is undamaged.

### 9.4 Tanker_Update 0x27008 (was Player_Ejection; GF step 27 if g_TankerType 0x8ffb8) — likely
Tanker at 0x8ff64/0x8ff6c, hit 0x8ffbc, frame 0x8ffc8 (0..2), drogue 0x8ffc0 (0..16), timer 0x8ff60, vy 0x8ff54,
door 0x8ff58. Off-screen: arrow sprite 0x1e6 at the clamped screen position (x 8..0x134, y 8..0xa8).
On-screen: door sprites 0x199/0x19a (if 0x8ff58 > 0 && frame 0), body `0x131 + type`, propeller like §6.2,
drogue hose: `for (i = 0; i < 5; i++) Sprite_Queue(i*hose + X-cx, Y-cy+7, 0x136); Sprite_Queue(5*hose + X-cx,
Y-cy+7, 0x137)`. Wreck: as §6.2 but the offset counter incremented is **0x90548** (Fat Albert's) while
0x8fff4 is read (quirk Q14). Motion: `X += 4*hit - 8; Y += 2*(X > 0x4b0 && Y <= -0x1f5) + 16*hit`.
Refuelling when the player is within ±0x140/±0xa0, frame 0, not hit, `Y < 0`, `g_DirHalf == 0` (flying left)
and `w109 != 0`: the tanker steers `X += Clamp(Sign(e)*abs(e), -16, 4)` toward `PX + w109 - 0x60`, `Y` by
`Sign*min(|e|,4)` toward `PY + w110 - 7`, the hose extends `min(hose+1, 16)`; at full hose with the probe
(`PX + w109`) in `(X+0x5c, X+100)` and (`PY + w110`) in `(Y+3, Y+0xb)`: **`g_Fuel = min(0x90378 + 1000*0x90648,
g_Fuel + 100)`** (max fuel incl. fuel pods). Otherwise the hose retracts by 1. Departure: `if (Y > -500) {
if (++0x8ff60 > 0x40) frame = min(frame+1,2); if (0x8ff60 > 0x10 && parity == 0 && door > 0) door--; }`;
`if (X < g_BaseStartX - 800) { frame = min(frame+1, 2); if (Y < -0x800) { Y = -0x800; X = W*16 - 0x140; frame = 0; } }`;
climb like §6.2 with 0x8ff54. The probe sprite 0xa9 at `(scrX + w109, scrY + w110)` is drawn at GF step 23
while the hose is out and the follow view is off and `g_DirHalf == 0`.

---------------------------------------------------------------------------------------------------

## 10. HUD (display.pax panel below the split line; drawing primitives in video.md)

### 10.1 Hud_DrawPanel 0x25c8b (Mission_Setup, Airbase return 0x2c4e5; likely, coordinates verified)
```c
g_MapPixW2 0x90394 = g_MapWidth << 4;
Pic_LoadHudPanel("display.pax");
if (g_RackRounds[0] > 0) Sprite_DrawNowShift(0x5e, 0x16, g_WeaponIcon[g_RackWeapon[0]]);
if (g_RackRounds[1] > 0) Sprite_DrawNowShift(0x5e, 0x2a, g_WeaponIcon[g_RackWeapon[1]]);
for (i = 0; i < g_Lives && i < 10; i++) Sprite_DrawNowShift(0x122, Sprite_GetHeight(0x1fb)*i + 10, 0x1fb);   /* lives */
for (i = 0; i < 14; i++) Sprite_DrawNowShift((i%7)*8 + 0xc, (i/7)*7 + 0x2c, g_DamageFlags[i] + 0x8a);   /* lamps */
Video_SetLineColor(0x16);                                   /* terrain profile in the radar */
x0 = 0; y0 = 0x19;
for (i = 0; i < g_MapWidth; i++) {
  int x = (i*0x3e)/g_MapWidth; int h = (g_MapVal[0xbd0 + i] * 0xd) >> 6;
  if (x0 < x) { Video_DrawLineColor(x0 + 0xdc, y0 + 0x11, x0 + 0xdc, 0x1d); y0 = 0x19; x0 = x; }
  y0 = min(y0, h);
}
g_DamageLampsDirty = 0;
if (g_GameMode != 3) { Text_DrawNumber(0x54, 0x20, g_RackRounds[0], 3); Text_DrawNumber(0x54, 0x32, g_RackRounds[1], 3); Text_DrawNumber(0x54, 0xb, g_GunAmmo, 4); }
Hud_DrawRadarLine(0x3a, 0x28, g_BaseStartX/16, g_BaseEndX/16, 1);                       /* base */
if (g_MP_PickupCol && g_MP_PickupSprite < 500) Hud_DrawRadarLine(3, 0x2d, col, col, 3);
if (g_MP_ReconCol) Hud_DrawRadarLine(3, 0x2d, g_MP_ReconCol, g_MP_ReconCol, 3);
if (g_MP_TargetX0 && g_MP_TargetX0 < 5000 && g_MP_TargetX1) Hud_DrawRadarLine(3, 0x2d, X0, g_MissionParams[4 - (0x90280 != 0)], 3);
if (g_MP_ConvoyCol) Hud_DrawRadarLine(3, 0x2d, g_MP_ConvoyCol, g_MP_ConvoyCol, 3);
/* invalidate gauge caches */ 0x90160 = 0x900f4 = 0x902a8 = 0x905ec = 0x902a4 = -1; 0x90414 = 1; g_OnGround = 1; 0x908e0 = -999;
if (0x902f8 == 1) { 0x9040c = (g_BaseStartX*0x3e0)/W16; 0x901d4 = (g_BaseEndX*0x3e0)/W16; Video_FillRect(0, 0x40, 0x7e, 0x59, 0); 0x902f8 = 0; }
Video_CopyRect(0, 0xdc, 5, 0x11a, 0x1e, 0, 0, 0x246);      /* save radar background */
Video_CopyRect(0, 0x95, 0x14, 0x9f, 0x37, 0, 0x40, 0x246); /* save altimeter background */
if (g_GameMode > 0) { 0x9040c = 0; 0x901d4 = 0x3e; }
Pal_Fade(0, 0x100, 1, 0x20);  Video_SetLineColor(1);
```
Quirk: Hud_DrawPanel sets `g_OnGround = 1` (harmless: called before the plane starts on the runway, and
after the hangar).

### 10.2 Messages (verified)
`Hud_PushMessage(str)` 0x26e03: `if (g_HudMsgCount < 10) { strcpy(g_HudMsgs 0x8d3f0 + n*100, str);
g_HudMsgTime 0x8031c[n] = 0x28; n++; }` (no length check; strings ≤ 80). `Hud_DrawMessages` 0x26e5a
(GF step 52, if count):
```c
int row = 0;
for (g_LoopI = 0; g_LoopI < g_HudMsgCount; g_LoopI++, row++) {
  strcpy(0x85448, msg[g_LoopI]);
  Text_DrawSmall(0xa0 - Text_WidthSmall(0x85448)/2 + g_ShakeX, row*8 + 0x2e + g_ShakeY, 0x85448, 1);
  if (--time[g_LoopI] == 0) {                      /* expire: shift the rest up */
    for (k = g_LoopI; k < g_HudMsgCount - 1; k++) { strcpy(msg[k], msg[k+1]); time[k] = time[k+1]; }
    g_HudMsgCount--; g_LoopI--;
  }
}
```
Quirk: `row` is not decremented on removal, so in the frame a message expires the following messages are
drawn one row lower (one-frame jump). Keep. Messages last 40 frames. Hud_Nop 0x26cc6, Hud_PlaneMessage
0x26ce1 (`Hud_Nop(HUDTEXT[61 + player]); 0x909b8 = 0`) and Hud_WeaponMessage 0x26d24 (builds "name LOADED"
/ "NO LEFT AMMO" per rack, passes it to Hud_Nop) **display nothing** (Hud_Nop is empty) — keep as no-ops.

### 10.3 Hud_UpdateRadar 0x2ec74 (odd frames when not crashed; verified for floats, rest likely)
```c
if (!g_DamageFlags[7] && !g_RadarJam 0x9047c) { 0x90480 = 0; Video_CopyRect(0, 0, 0x246, 0x3e, 0x25f, 0, 0xdc, 5); }  /* clear radar */
else if (!0x90480 && !g_DamageFlags[7]) { Text_DrawSmall(0xfd - Text_WidthSmall("Jammed")/2, 0x10, "Jammed", 0); 0x90480 = 1; }
W16 = g_MapWidth << 4;
if (!g_DamageFlags[5]) {
  int len = (int)(g_Speed * 8.0f);                                   /* FSM, FSI4 */
  if (len != 0x900f4) {                                              /* speed bar, rows 8..10 */
    for (i = 0; i < len; i++) col = (i < 0x2e) ? i/3 + 0x21 : (i == 0x2e ? 0x27 : 0x30), x = (i == 0x2e) ? 0x3a : i + 0xc,
        Video_PutPixel(x, 8..10, col);
    for (i = len; i < 0x30; i++) Video_PutPixel(i + 0xc, 8..10, 0);
    0x900f4 = len;
  }
  if (abs(g_Fuel - 0x905ec) > 0x32) { Video_FillRect(0x17, 0x17, 0x2d, 0x1c, 0); Text_DrawNumber(0x17, 0x17, g_Fuel, 5); 0x905ec = g_Fuel; }
} else if (Rand(10) == 1 && 0x9068c == 0) 0x9068c = 0xfff;          /* display 1 dead: random screen flash */
if (!g_DamageFlags[7] && !g_RadarJam) {
  rx = (g_CamX*0x3e)/W16;  ry = ((g_CamY + 0x800)*0x19)/0xc08;
  if (rx != 0x90160 || ry != 0x90164) { 0x9040c = min(0x9040c, max(rx-8, 0)); 0x901d4 = max(0x901d4, min(rx+8, 0x3e)); 0x90160 = rx; 0x90164 = ry; }  /* explored range */
  Video_PutPixel(rx + 0xdd, ry + 5, 0x1f);                           /* player */
  gate/crate: if (0x90948 > 0 || g_AeroGateX[0x90334] > 0) { gx = (max(0x90948, gateX)*0x3e)/W16; gy = ((gateY ? gateY : 0x9094c) + 0x7e8)*0x19/0xc08;
      if (gx == rx && gy == ry) 0x908e0 = -1;  if (gx > 0) PutPixel(gx + 0xdd, gy + 5, 0x1f); }
  enemy aircraft i < 0x90708: PutPixel(x*0x3e/W16 + 0xdd, ((y + 0x7e8)*0x19)/0xc08 + 5, 0x1e) if x' > 0
  ground enemies i < 0x90348 (0x8d9d8/0x8d9e8): PutPixel(x*0x3e/W16 + 0xdd, ((y + 0x800)*0x19)/0xbd9 + 4, 0x1e) if x' > 0
  bomber 0x906dc: 2 px colour 0x1e;  B52 0x90998 (x scale **0x3f**, sic), Fat Albert, tanker: 2 px colour 0x1f
}
Pal_SetColor(0x1f, p, p, p);   p = 0x8038c (init 45);  0x8038c += 3*0x80390 (init 1);  if (0x8038c > 0x3d || < 0x23) 0x80390 = -0x80390;
Pal_SetColor(0x1e, q, 0xc, 0xc); q = 0x80384 (init 45);  0x80384 += 3*0x80390;            if (q out of range) 0x80388 = -0x80388 (sic, unused)
Video_CopyRect(0, 0x40, 0x246, 0x4a, 0x269, 0, 0x95, 0x14);         /* restore altimeter */
int ay = (int)((double)max(PY, 0) / 29.1764) + 0x14;                 /* 0x403d2d288ce703b0 */
for (x = 0x96; x < 0x9f; x++) Video_PutPixel(x, ay, 1);
if (g_Score[p] != 0x902a8) { Video_FillRect(0xab, 0x28, 0xd4, 0x2d, 0xe); Text_DrawNumber(0xac, 0x28, g_Score[p], 8); 0x902a8 = g_Score[p]; }
if (g_Kills != 0x902a4) {                                            /* kill tally: medals 0x94+k worth 1,5,10,25,50,75,100 */
  left = g_Kills; px = 0; Video_FillRect(0xa6, 0x38, 0xd4, 0x3c, 0xe);
  for (k = 6; left > 0 && px < 0x34 && k >= 0; k--)
    for (; g_KillTallyValue[k] < left && px < 0x34; px += 7) { Sprite_DrawNowShift(px + 0xa4, 0x38, k + 0x94); left -= g_KillTallyValue[k]; }
  0x902a4 = g_Kills;
}
if (g_DamageLampsDirty) { redraw the 14 lamps as in 10.1; g_DamageLampsDirty = 0; }
```
`g_KillTallyValue` = 0x90eb0 (GENDAT2, was named g_RadarScales). Note the strict `<` (quirk Q21): a medal
is drawn only while its value is **less than** the remaining count, so 1 kill shows nothing, 5 kills show four
"1" medals, 6 kills one "5" medal; at most 7 medals (px < 0x34). Port exactly. Speed bar quirk: pixels for i == 0x2e go to x 0x3a
(= 0x2e + 0xc) with colour 0x27 (the "max" tick).

### 10.4 Hud_DrawTargetArrow 0x267da (GF step 42; verified)
`(camX, camY, tx, ty)`: if `abs(camX - tx) + abs(camY - ty) < 0x4b0 && tx != 0`: `Sprite_Queue(Clamp(tx - camX,
5, 0x138), Clamp(ty - camY, 5, 0xa4), 0xa7)` (clamps done as two ifs in this order: <5, >max).

### 10.5 Scoring (writers of g_Score[g_AeroPlayer] 0x8e330; likely)
| Event | Code | Points |
|---|---|---|
| enemy aircraft destroyed | EnemyAir_Update 0x36ae3 | `(w77+1)*1000`, g_Kills++, then `Bonus_Spawn` if `(!0x90890 && Rand(2)==1) || g_ExtraAircraftScore < score` |
| target building 1 / 2 / 3 destroyed | Explosion_Damage 0x39b69 | +3000 / +1000 / +2000 |
| ground hit (per explosion tile) | Explosion_Damage | +100 |
| own runway cratered | Map_CraterAt 0x3b777 | -1000 (min 0) |
| convoy vehicle killed | Convoy_Update 0x3d105 | +250 (enemy) / -1000 + "Friendly Vehicle Destroyed !" (escort) |
| escorted vehicle arrived | Convoy_Update | +2000 |
| prize balloon / pickup | Pickup_Update 0x45859 | `+10000 * 0x90918` |
| mission complete | Mission_CompleteScreen 0x4011f | `+(w77+1) * g_MissionBonus` |
| debrief | Mission_Debrief 0x2c9cb | `+g_MissionBonus * mult` (game_flow §9) |
Bonus crates appear when `g_NextBonusScore <= score` (GF step 75); auto-eject advances g_NextBonusScore (GF step 79).

---------------------------------------------------------------------------------------------------

## 11. Quirks and bugs (recommendation in brackets; the user decides)

| # | Where | Quirk | Rec. |
|---|---|---|---|
| Q1 | §3.3 tow | rope-limit test reads 0x8ff88 (sprite frame) instead of the tow state | keep |
| Q2 | §3.4 | catapult: speed > 6 is set to **3.0**, not 6.0 | keep |
| Q3 | §3.5 | stall nose-drop lets g_Dir reach 64 → g_DirHalf 32 indexes past the 32-entry tables (reads e150[0] etc.) | keep (emulate the adjacent-table read: lay the tables out contiguously 0x8e0d0,+0x80 e150,+0x100 …) or fix with PORT note |
| Q4 | §5 | g_LastAction is reset to 0 every frame at GF step 80 (g_KeyCode stub = 0), so the "7"/"5" debounces never hold: holding Tab toggles the autothrottle every frame; gear and hover have their own latches | keep |
| Q5 | §3.6 | glider ridge-lift bounds come from leftover global loop counters | keep (shared globals) |
| Q6 | §3.6 | ground column wrapped `% 1000` | keep |
| Q7 | §4.2 | brakes stay on until the next PitchDown | keep |
| Q8 | §5.3 | follow-projectile end test reads the slot after the last projectile | keep |
| Q9 | §5.6 | agile switch rebuilds fe10/fe50 from **integer** radians | keep |
| Q10 | §6.6 | flare loop uses 0x90724 (engine-sound copy) as its counter | keep |
| Q11 | §7.2 | damage roll: EXPLOSION needs class ≥ 2 | keep |
| Q12 | §9.1 | .HD rotor offset always from record 0 | keep |
| Q13 | §9.3 | "STANDBY TO LAUNCH n" sent to an empty stub | keep (or show it, PORT note) |
| Q14 | §9.4 | tanker wreck increments 0x90548 but reads 0x8fff4 | keep |
| Q15 | §3.12 | engine-fire escalation `Rand(200) == 1000±80` never true | keep |
| Q16 | §10.3 | B52 radar x scale 0x3f; second colour pulse flips the wrong variable (harmless, values equal) | keep |
| Q17 | §5.2 | Rocket Boost branch dead (g_Ctrl[2] always 0) | keep |
| Q18 | §3.4 | gear term: `a -= 0.002 - gear*0.002` penalises gear **up** (sic; drag should be gear down) | keep |
| Q19 | §10.2 | expiring message shifts the following ones one row down for a frame | keep |
| Q20 | §5.7 | "back to plane" branch unreachable | keep |
| Q21 | §10.3 | kill tally uses `value < left` (1 kill shows no medal) | keep (or fix, PORT note) |

## 12. Open questions

1. 0x909dc (alternative control scheme) and 0x909a0 ("backseat driver") writers — confirm in the training
   options (game_flow) whether 0x909dc is ever 1.
2. Typical values of g_LoopJ 0x9098c / 0x90830 when Player_GliderUpdate runs (needs a trace of the frame's
   earlier loops; the glider is only in a few missions).
3. B52 contrail Particle_Spawn arguments (§6.3) — Ghidra garbled them; disassemble 0x2836x before porting.
4. SupportAircraft_Update §6.7 (pickup) was ported from the decompile only; re-check the Video_ReadPixel
   coordinates (g_ShakeX/Y offsets) against video.md's back-page layout.
5. Exact semantics of 0x903fc (debrief cause ids 0xb/0xc/0xd) — game_flow §9 owns the sarcasm table.
6. Sprite id 0 handling in Sprite_Queue (Player_DrawHeli with no rotor) — video.md.

## Corrections (phase 5 step B, checked against the disassembly while porting)

1. §3.3 `g_OverRunway`: the height test is `PY <= 0x3e1 - g_BaseYOff` (0x1f0f6 `jle`), not `>`: the plane counts as
   over the runway while it is *above* (or level with) the runway surface.
2. §5.5 gear key: the speed test applies **on the ground**, not in the air (0x2da9d: `g_OnGround == 0` skips it):
   `g_KeyGear && (g_OnGround == 0 || g_Speed > g_StallSpeed * 6.0f) && g_LastAction != 4 && !g_DamageFlags[10] &&
   w89 == 0`. So the gear always works in the air, and on the ground it can only be raised/lowered above 6x stall
   speed (`__FSC` + `jle` skips at speed <= 6x stall).
3. §5.2 rocket boost: the third test is `g_Ctrl[1]` (0x90c9c, RShift), not `g_Ctrl[0]` (dead code either way).
4. §5.1/§5.3 not listed: `Player_Update` also counts `0x90854++` while Down is held parked (`g_KeyDn && g_OnGround
   == 1 && speed bits == 0 && g_GameMode != 3`), else clears 0x90854 and 0x90140 (read by Airbase_Update).
5. §5.3 Backspace cycling: the 0..49 (projectile) case sets the B52 view and its message without testing that the
   B52 exists (js.c 14670); the next frame's view code then drops it (`0x90998 == 0` -> -1).
6. §9.1 / GF step 45 smoke emitters: the 0x11105 stub (Stub_FrameD) returns 0, so each emitter call also stores
   `0x900fc = 0`.
