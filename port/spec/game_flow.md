# Game flow subsystem (JS.EXE): master loop, menus, briefing, plane/weapon select, mission frame loop, debrief, end game, save/load

Binary: `work/JS.bin` (flat, base 0x10000), decompile `port/decomp/js.c`. Symbols: `port/spec/game_flow_symbols.csv`.
Companion specs: `port/spec/platform.md` (main, input, waits, RNG, keys), `port/spec/video.md` (flip, palette,
lightning body, text). The intro program is in `port/spec/intro.md`.

Confidence tags: **verified** (read in the capstone disassembly or a clean decompile), **likely**, **guess**.
All functions use the Watcom stack convention; `__CHK(n)` is ignored. `Rand(n)` = `rand()%(n+1)`, no call for
n==0 (platform.md §7). "word"=16 bit, "dword"=32 bit; every global below is a signed int32 unless stated.
`g_LoopI` = 0x90ab8 and `g_LoopJ` = 0x9098c are *global* loop counters shared by almost every function
(keep them global in the port: several functions leave them at a value another reads).

Soft-float: Watcom helpers take `EDX:EAX op ECX:EBX` (double) or `EAX op EDX` (float), result in EAX /
EDX:EAX. `__FDS`/`__FSS` = first minus second, `__FDD`/`__FSD` = first / second. `__FDI4`/`__FSI4` = C cast
(truncate toward 0) (likely; same runtime as the game spec). Expressions below are written in evaluation
order with the C types used by the code: `(float)` means the value is rounded to float there, `(double)`
means it is widened. **Several "float < constant" tests are integer compares of the float bit pattern**
(e.g. `*(int*)&g_Speed < 0x40800000` = speed < 4.0f for non-negative speeds); they are written as such.

---------------------------------------------------------------------------------------------------

## 0. Overview and call graph (verified)

```
main 0x146f4 (platform.md §2.1)
 └ Game_Run 0x1ba0c ─── returns only after "Quit"
     ├ init once: constant tables, DATA/GENDAT3.DAX, Mem_AllocMapBuffers, DATA/GENDAT2.DAX,
     │            jetstrike.spx, DATA/MISC, DATA/MISC.Z                                   (§2.1)
     └ for (;;)                                                                           (§2.2)
         ├ if (g_AbortFlag != 2) MainMenu 0x469e7  (first call: CD_Check, Sound_Init;     (§3)
         │        every call: srand(time), load JETS.N/WEAPONS.DAT/HUDTEXT.DAT/L1L2/GENDATAD/GENDATA,
         │        MainMenu_Draw (miscon.pax zones) → SaveGame_LoadMenu | AeroOptions_Menu | quit)
         ├ quit → Pal_Fade out, return
         ├ while (g_Lives > 0 && g_Mission < 200 && g_AbortFlag == 0)   "attempt loop"    (§2.3)
         │    ├ Mission_Setup 0x212ac                                                     (§4)
         │    │    ├ Mission_LoadBriefing 0x2299a (data/M<mode> record, Story_ShowAsc 0x23f6b,
         │    │    │     jetlogo.pax briefing, F1..F10 → SaveGame_Write 0x3c367)           (§5)
         │    │    ├ PlaneSelect_Screen 0x245a9 (planech.pax)                              (§6)
         │    │    ├ Mission_ResetState 0x222f0 (rack loads, weight, fuel)                 (§7.3)
         │    │    └ tileset/parallax/map/HUD panel, flight tables
         │    ├ per-mission spawn set-up, CD_PlayTrack(random)                            (§8.1)
         │    └ frame loop (≈20 Hz)                                                       (§8.2)
         │          (Airbase_Update 0x2bc0b may call WeaponSelect_Screen 0x29d90 in flight, §7)
         ├ end of attempt: CD_Stop, final redraw + flip, Sound_StopAll, mission++ on success (§9.1)
         ├ Mission_Debrief 0x2c9cb (sarcasm text over the frozen view)                    (§9.2)
         ├ Aerolimits: next player / next random mission                                  (§9.3)
         └ EndGame_Screen 0x301b2 (endgameN.pax / gamedone.pax / AeroScores_Screen 0x45352 /
                 TrainingCredits_Screen 0x304e0)  or  g_AbortFlag = 2 (continue)          (§10)
```

Game modes `g_GameMode` 0x8ff7c: 0 campaign ("COMBAT", DATA/M0, 150 records), 1 training (M1, 10), 2 practice
(M2, 10), 3 Aerolimits/"AEROLYMPICS" 2..8-player hot-seat (M3, 20). Mission record size 0x1c2 = 450 bytes.

There is **no pause screen of its own and no promotion/rank system** in this game. Pause lives in
Player_Update (§8.4). "Lives" are *crashes left* ("YOU HAVE n CRASHES LEFT"): a crash without ejecting
ends the game (§9.2/§10).

Screen/menu functions (all in this spec): `MainMenu` 0x469e7, `MainMenu_Draw` 0x47ec2, `AeroOptions_Menu`
0x48111, `SaveGame_LoadMenu` 0x3c0fb (+`SaveGame_DrawLoadPrompt` 0x3c56c), `Mission_LoadBriefing` 0x2299a
(briefing screen), `Story_ShowAsc` 0x23f6b, `PlaneSelect_Screen` 0x245a9 (+`SelectScreen_LoadBg` 0x257c3,
`SelectScreen_DrawInfoBox` 0x258b6, `SelectScreen_ShowPlaneName` 0x25a5d, `Text_DrawCenteredAt` 0x25869),
`WeaponSelect_Screen` 0x29d90 (+`WeaponSelect_DrawCounts` 0x2ba3c, `WeaponSelect_DrawInfo` 0x2bb33),
`Mission_Debrief` 0x2c9cb, `EndGame_Screen` 0x301b2, `AeroScores_Screen` 0x45352, `TrainingCredits_Screen`
0x304e0. Menu hit zones: `Zone_Add` 0x11d02, `Zone_HitTest` 0x11dbc, `Zone_Clear` 0x11e64.

---------------------------------------------------------------------------------------------------

## 1. Key globals of the flow (C view)

```c
/* modes / progression */
int32 g_GameMode;        /* 0x8ff7c */
int32 g_Mission;         /* 0x903e0  record index in data/M<mode> */
int32 g_Lives;           /* 0x902f0  crashes left; 7 at start; -10 = game finished; -8 = self-destruct (Esc) */
int32 g_AbortFlag;       /* 0x90800  0 continue attempts, 1 end game, 2 "next mission without menu" */
int32 g_MissionResult;   /* 0x903e8  0 running, 1 objectives done (go home), 2 success (landed after 1) */
int32 g_MissionActive;   /* 0x90154  1 while a mission is set up; 0 forces PlaneSelect again (NEW PLANE) */
int32 g_BriefedMission;  /* 0x903f4  mission whose briefing was shown (-1 none) */
int32 g_QuitGame;        /* 0x80174  set by the Quit zone (platform name) */
int32 g_MenuRepeat;      /* 0x903ec  Game_Run calls MainMenu while > 0 (MainMenu sets 0) */
int32 g_MenuDone;        /* 0x9030c  menu loop exit; later 2 = "non-campaign game ended" */
int32 g_MenuChoice;      /* 0x90184  0..9 training mission, 10 practice, 11 campaign, 12 Aerolimits */
int32 g_StartMission;    /* 0x903a4  mission index chosen (training) or loaded */
int32 g_TrainingPicked;  /* 0x9021c  MainMenu-internal: 1 between pick and copy to g_Mission */
int32 g_TrainingMode;    /* 0x9097c  1 after a training pick; cleared by Game_Run when mode != 1 */
int32 g_PracticeEjects;  /* 0x9007c  3 in practice: auto-ejects left */
int32 g_AutoEjectOn;     /* 0x90078  1 = automatic ejection on fatal damage */
int32 g_FinalMission;    /* 0x90400  record param0 / 100: 1 = winning this mission ends the campaign */
/* Aerolimits */
int32 g_AeroPlayer;      /* 0x90254  0-based current player */
int32 g_AeroPlayers;     /* 0x902c8  2..8 */
int32 g_AeroRoundsSel;   /* 0x9033c  index 0..3 into g_AeroRoundsOptions 0x8d9f8 {3,5,10,15} */
int32 g_AeroRound;       /* 0x903cc  rounds played */
uint32 g_AeroMissionMask;/* 0x90390  bit n = M3 record n still unused; 0xffffe at menu */
int32 g_AeroUsedRacks[8];/* 0x8c3b8  per player, zeroed by MainMenu (read by PlaneSelect slot 4) */
int32 g_AeroPlaneSlot;   /* 0x90a20  slot 0..4 picked in Aerolimits (also the bonus multiplier) */
/* score (saved) */
int32 g_Score[8];        /* 0x8e330  per player */
int32 g_Kills, g_ArmourBonus, g_NextBonusScore, g_BonusScoreStep, g_ExtraAircraftScore; /* 0x904a8 0x909e0 0x902d8 0x902e4 0x90304 */
int32 g_MissionBonus;    /* 0x90408  (mission+1)*1000, 32000 in Aerolimits */
/* aircraft */
int32 g_PlaneSel;        /* 0x90a58  1-based plane number (0 = none chosen) */
int32 g_PlaneUsed[71];   /* 0x90ecc  airframes lost/used per plane, 9999 = alien (saved, low 16 bits) */
int32 g_PlaneLimit[60];  /* 0x9152c  DATA/L1L2 bytes, >= 200 = unlimited */
int32 g_PlaneFlown[40];  /* 0x91140  [plane-1]=1 once airborne this mission (cleared by LoadBriefing) */
int32 g_AlienPlaneIdx;   /* 0x90a04  0-based index of "Alien Superfighter" in JETS.N (59) */
int32 g_AlienAbduct;     /* 0x909c0  alien event state (>0x3f forces the alien plane) */
int32 g_PlaneStats[121]; /* 0x91684  MISC.Z record of the plane (Plane_SetupSprites) */
/* weapons */
int32 g_RackWeapon[2];   /* 0x8fbc8  weapon index on left/right rack */
int32 g_RackRounds[2];   /* 0x8fcb0  rounds loaded (multiple of per-rack) */
int32 g_RackMaxLoad[2];  /* 0x8fbc0  = stat104/105 (max weight per rack, lb) */
int32 g_RackPoints[2];   /* 0x8fc10  = stat101/102 (hard points per rack) */
int32 g_HardpointLoad;   /* 0x90500  = stat103 (max weight per hard point) */
int32 g_RackSel;         /* 0x90338  0/1 rack being edited (also used as a loop counter!) */
int32 g_LoadWeight;      /* 0x8feb8  total load weight */
int32 g_MaxLoadWeight;   /* 0x904ec */
int32 g_FuelBase;        /* 0x90378  = g_PlaneFuel 0x90388 (stat106) + drop tanks */
int32 g_Fuel;            /* 0x905b8 */
/* misc */
int32 g_StoryShown;      /* 0x92b1c  1 if Story_ShowAsc ran for the current briefing (enables saving) */
int32 g_FogPending;      /* 0x92b18  set by a fog warning, never cleared (quirk Q6) */
int32 g_FogActive;       /* 0x80070  fog palette effect for this mission */
int32 g_EndGameIndex;    /* 0x907f4  0..6 = endgameN.pax / CD track N+7 */
char  g_DebriefText[0x100];  /* 0x84f48 sarcasm line (also shown on endgameN.pax) */
char  g_SarcasmLines[28][0x50]; /* 0x863f0 DATA/SARCASM lines 0..27 (loaded lazily) */
```

---------------------------------------------------------------------------------------------------

## 2. Game_Run 0x1ba0c — `void Game_Run(void)` (verified)

### 2.1 One-time initialisation (in order)

1. `g_MapWidth`=0; `g_SarcasmLines[0][0]`=0 (forces Sarcasm_Load later); `0x849b8[0..30]` (bytes) = 0.
2. Constant tables (exact values, all int32):
   - pairs at 0x8d908/0x8d950 (stride 4, 18 entries interleaved as written): 0x8d908..0x8d94c =
     {0,0,-1,1,-1,1,0,-1,-1,-1,-1,1,1,1,1,1,1,0}, 0x8d950..0x8d994 = {1,1,1,1,1,1,1,1,1,0,-1,-1,1,1,0,1,-1,1}
     (written pairwise: 908=0/950=1, 90c=0/954=1, 910=-1/958=1, 914=1/95c=1, 918=-1/960=1, 91c=1/964=1,
     920=0/968=1, 924=-1/96c=1, 928=-1/970=1, 92c=-1/974=0, 930=-1/978=-1, 934=-1/97c=-1, 938=1/980=1,
     93c=1/984=1, 940=1/988=0, 944=1/98c=-1, 948=1/990=-1, 94c=0/994=1). (8-direction helper table, owner: play.)
   - 0x8d7e8=4, 0x8d7ec=8; 0x8da88..0x8da94 = {0,6,12,18}; 0x90cd8/0x90be4 = 28/23, 0x90cdc/0x90be8 = 29/20,
     0x90ce0/0x90bec = 29/16; 0x90cfc..0x90d10 = {0x18c,0x18d,0x1c6,0x1c7,0x1c8,0x1c9};
     0x90ce4..0x90cf4 = {0x197,0x198,0,0x199,0x19a}; `0x90cf8[i] = i+0x2e` for i=7..100;
     0x90cc4/0x90c84 pairs {0/0,1/2,11/2,14/2,15/0}; `0x90c2c[i] = i` for i=0..10.
     (Most are then overwritten by GENDAT3.)
3. `f = fopen("data/gendat3.dax","r")`, NULL → `FatalError("gendat3.dax"," not found.",1)`. Text, `fscanf("%d\n")`
   per number, in this order: 5 x (0x8d8c0[i], 0x8d8d4[i]); 18 x (0x8d818[i], 0x8d860[i]); 6 x 0x8d8a8[i];
   4 x `fgets(tmp,0x50)` (skipped lines "MISSIONS1/","BASE/"...); 4 x `g_AeroRoundsOptions[i]` (3,5,10,15);
   27 x `0x90b48[i]` i=1..27; 0x8d7e8=4, 0x8d7ec=8 again; 4 x `0x8da88[i]`; 3 x (0x90cd8[i], 0x90be4[i]);
   6 x `0x90cf8[i]` i=1..6; then `0x90cf8[i] = min(i+0x2e, 0x4a)` for i=7..100; 5 x `0x90ce4[i]`;
   5 x (0x90cc4[i], 0x90c84[i]); 8 x (0x8deb8[i], 0x8e310[i]); 7 x (0x8ded8[i], 0x8df00[i]);
   7 x { `0x90afc[i]`=0; `fgets(g_MusicTitles[i] (0x86148+i*0x50), 0x50)`; `fgets(0x86170+i*0x50, 0x50)`;
   `fscanf(&g_MusicTracks[i])` }. `strcpy(0x86378,"Negative G")`, `strcpy(0x863a0,"Adam F")` (title 4
   overrides). `0x90b34 = 0x90b30 + 1`. fclose.
4. `0x90308`=0, `0x90364`=6, `0x90524`=1, `0x9000c`=14, `0x90a2c`=0x14f (plane icon sprite base).
5. `Mem_AllocMapBuffers()`; `File_LoadWhole("data/","gendat2.dax",&0x849f0,20000)`; from the buffer (BE16):
   16 x {g_VehicleSprites[i*2] 0x8de38, 0x8de3c[i*2], g_VehicleHP[i]}, skip 1 word, 8 x 0x90e8c[i]
   (bonus-plane mission numbers), 10 x g_BonusSprites[i], 16 x 0x8fa78[i], 7 x g_RadarScales[i].
   Note the second loop's `for(..; p++, i<8; ..)` increments the pointer **before** each test, i.e. it
   skips one word before the first read and then reads every word (verified). 0x92560..0x9258c =
   {0xd0,200,0xd2,0xd3,0xd4,0xd5} / {0xd6,0xd7,0xe0,0xdc,0x4e,0x1fc} (pairs 560/578, 564/57c, ...).
6. `Sprites_LoadSpx("jetstrike.spx")` (second load, no-op after main), `File_LoadWhole("data/","misc",&g_MiscData,-1)`,
   `File_LoadWhole("data/","misc.z",&g_MiscZData,-1)`.
7. `0x90af4`=1, `0x9025c`=0, `0x906e8`=-1, `0x9081c`=0x69, `0x90468`=1, `g_EnemySpxLoaded`=-1.

### 2.2 Outer loop (one iteration = one mission sequence until debrief)

```c
for (;;) {
  if (0x907b4 == 1) 0x907b4 = 0;
  if (g_AbortFlag != 2) { g_MenuRepeat = 1; while (g_MenuRepeat > 0) MainMenu(); }
  if (g_QuitGame) { Pal_Fade(0, 0x100, 0, 0x20); return; }      /* fade out all, 32 steps */
  g_AbortFlag = 0;
  if (g_GameMode != 1) g_TrainingMode = 0;
  0x90a08 = -1; 0x90a0c = -1; 0x9037c = 0; 0x90998 = 0; 0x90398 = 0; 0x90528 = 0;
  while (g_Lives > 0 && g_Mission < 200 && g_AbortFlag == 0) { ... §2.3 ... }
  §9.1 end of attempt; §9.2 Mission_Debrief; §9.3 Aerolimits rotation; §10 end game / continue
}
```

### 2.3 Attempt loop body before the frame loop (verified)

```c
0x8ff94 = 0; 0x8ff8c = 0; 0x8ff90 = 0; 0x8ff78 = 0; 0x8ff74 = 0; g_FogActive = 0;
if (0x907b4 == 1) 0x907b4 = 0;
Video_SetSplitLine(400);                     /* split off during menus */
Mission_Setup();                             /* §4 */
0x8fef4 = 0;
if (g_FogActive) Pal_SaveNight();            /* 0x46617, video spec */
0x900b4 = 0; 0x8ff68 = 0; 0x8ff70 = 0; 0x90854 = 0; 0x900a4 = 1; 0x907c8 = -1;
for (g_LoopI = 0; g_MP_TargetX0 != 0 && 0x900b4 < 6 && g_GameMode == 0 && g_LoopI < 10; g_LoopI++)
    Enemy_InitPositions();                   /* 0x?: play spec */
0x8ffec = 0; 0x90288 = 0; 0x902b0 = 0; 0x909a0 = 0; 0x905ec = -1; 0x903fc = 0; 0x90360 = 0; 0x8fff0 = 0;
if (g_Mission < 31) g_AutoEjectOn = 1;
if (g_GameMode > 0) {
   if (g_GameMode == 1 || (g_GameMode == 2 && g_PracticeEjects > 0)) { g_AutoEjectOn = 1; g_Lives = 7; }
   if (g_GameMode == 3) { 0x8fff0 = 3200; 0x909b8 = 1; }          /* Aerolimits time budget */
}
Particles_Clear(); Particles_Nop(); 0x90318 = 0; Bullets_Clear();
0x9064c = 0; 0x90100 = 0; 0x907b0 = 0; 0x9041c = 0; 0x8ffd0 = 0; 0x8fff8 = 0; 0x90328 = 0;
0x8feb0 = Sign(0x8feb0);
0x9017c = 0x90180 + 8;  0x8ff10 = -1; 0x900b0 = 1; 0x900ac = 0; 0x90980 = 0; 0x90864 = 0; 0x90860 = 1;
/* windsock search: 0x90180 = runway left x (px), 0x901a8 = runway right x (px), 0x901ac = runway height (px) */
0x901e4 = 0;
for (g_LoopI = 0x901a8/16; g_LoopI >= 0x901a8/16 - 10; g_LoopI--)      /* "/16" = C division (toward 0) */
  for (g_LoopJ = 0; g_LoopJ > -5; g_LoopJ--)
    if (Map_GetTileAttr(g_LoopI, 0x3f - 0x901ac/16 + g_LoopJ, 0) == 0x8c) {
       0x901e4 = g_LoopI*16 - 16;  0x901e8 = g_LoopJ*16 - 0x901ac + 0x3ef;
       0x901d0 = 0x1e2; 0x901cc = 1;  g_LoopI = 0; g_LoopJ = -8; }     /* breaks both loops */
/* RNG: 1 + 4*2 calls, in this order */
0x90590 = 0x90180 + 16 + Rand(0x901a8 - 0x90180 - 0x80);
for (i = 0; i < 4; i++) {
   0x90c04[i] = 0x90180 + 16 + Rand(0x901a8 - 0x90180 - 0x80);   /* parked planes on the airbase */
   0x90c18[i] = 0x3df - 0x901ac;  0x90bf0[i] = 0x1d0;  0x90bd0[i] = -1;
   if (Rand(1) != 0) { 0x90bf0[i] = 0x1d2; 0x90bd0[i] = 1; }
}
0x9089c = 0; 0x90744 = 0; 0x9075c = 0;
/* CD track: Rand(13)+2 until 2..6 or 14..15 */
g_CDMissionTrack = 0;  while (g_CDMissionTrack < 2 || (6 < g_CDMissionTrack && g_CDMissionTrack < 14)) g_CDMissionTrack = Rand(13) + 2;
CD_PlayTrack(g_CDMissionTrack);
```
(`g_CDMissionTrack` 0x92b04 is reused as a temporary by the speed code in §8.3.)

---------------------------------------------------------------------------------------------------

## 3. Main menu

### 3.1 MainMenu 0x469e7 — `void MainMenu(void)` (verified)

1. CD/sound/srand block: platform.md §2.3 (first call only `CD_Check` with the three fatal messages; every
   call `g_SfxOn`, `Sound_Init` once, `g_CDMusicOn = cfg word +0`, **`srand(time(NULL))`**).
   Port: CD check → skip (or verify Game/MUSIC exists); keep srand here.
2. `0x90804`=0; `0x8c3b8[0..7]`=0; `0x90768`=0; `g_AeroRound`=0; `g_AeroGateX[0..4]`=0;
   `g_ExtraAircraftScore`=250000; `g_NextBonusScore`=10000; `g_BonusScoreStep`=12000; `g_PlaneUsed[0..70]`=0;
   `0x906ec`=0; `g_Score[0]`=0; `g_Score[1]` (0x8e334)=0; `g_Kills`=0; `0x90890`=0.
3. If `!g_TrainingPicked` (always true at entry): `Pal_SetColor(i,0,0,0)` for i<0x40 or i>0x7f;
   `Pal_UploadAll(1,0x140,0xf0,0x100)` (args as pushed).
4. `0x90494 = 0x10000`; `Video_ShowPage(0)`; `0x909f8 = -1`. First time only (`0x849e0 == 0`):
   `Fonts_Load(); 0x849e0 = 0x849c0; 0x849c0 = 0`.
5. **DATA/JETS.N** (text "r"): `fscanf("%d\n",&g_PlaneCount)`; for i = 0..g_PlaneCount (inclusive, 60 lines):
   `fgets(g_PlaneNames+i*0x28, 0x50, f)`; strip the last char (newline); if equal to "Alien Superfighter":
   `g_PlaneUsed[i] = 9999; g_AlienPlaneIdx = i`. NULL file → `FatalError("data/jets.n"," not found.",1)`.
6. `if (!0x849cc) File_LoadStub("jetsoundext.aaf",0,-1)` (empty function). `0x8f814`=0xde, `0x8fa1c`=0x40.
7. **DATA/WEAPONS.DAT** ("r"): `fscanf("%d\n",&g_WeaponCount)`; for i = 0..g_WeaponCount+1 (inclusive):
   name `fgets(g_WeaponNames+i*0x28,0x28)` strip last char; 6 x `fscanf("%d", &g_WeaponType[i*6+k])`;
   `%d` g_WeaponBlastA[i], g_WeaponBlastB[i], g_WeaponThrust[i], g_WeaponIcon[i], g_WeaponPerRack[i],
   g_WeaponWeight[i]; `%d\n` g_WeaponRackMult[i]; `fgets(g_WeaponDesc+i*0xa0,0xa0)` strip last; `%d`
   g_WeaponStock[i]; `%d\n` g_WeaponResupply[i]. (Note: the stock is reloaded on every menu entry, so a
   new game always starts with the file stock; a loaded save overwrites it afterwards.)
8. **DATA/HUDTEXT.DAT**: `while (!(f->flags & 0x10 /*EOF*/)) { fgets(g_HudText+i*0x50,0x50); strip last; i++; }`.
9. If `!g_TrainingPicked` (always): **DATA/L1L2** ("rb") 60 bytes → `g_PlaneLimit[i]` (zero-extended);
   **DATA/GENDATAD.DAX**: 16 x `fgets(g_DamageMsgs+i*0x50, 0x28)` (newline kept); **DATA/GENDATA.DAX** via
   `File_LoadWhole` into 0x849f0, byte-swap 0x66 words in place, then (owner: data/play specs, listed for
   order only): night flash colour list until 0xffff (0x23 = end of a group of 10), 3x3 words 0x92980,
   26 g_ParamNotObjective (stride 8), 26 0x8f08a (stride 8); 16 entries of direction tables
   `0x8fc30[i] = (int)(-cos(a)*8.0)`, `0x8fc70[i] = (int)(-sin(a)*6.0)` with `a = (float)(i * 0.39269908125)`
   (constant 0x3fd921fb4d12d84a = 3.14159265/8; a stored as float, widened for cos/sin), `0x8faf0[i] = 2*fc30[i]`,
   `0x8fb30[i*2] = 2*fc70[i]`, `0x8fb34[i*2] = 3`; 16 g_ProjSpriteBase; 6 words 0x8fc18 then overwritten by
   {0x36,0x37,0xaf,0xb0,0xb1,0x45}; `0x863c8[i] = (float)((double)(0x903b0*i)/10.8 + 1.0)` i<10
   (0x903b0 is 0 here at the first menu → all 1.0; Mission_Setup recomputes); player sprite direction table
   0x8fcb8/0x8fcbc (32 pairs: i=0..8 → i+1, mirrored `Sprite_Mirror(i+1)` at 16-i for i<8; i=0x11..0x18 → i-7,
   mirrored at 0x30-i; 0x8fdb8/0x8fdbc[i]=i+0x2d i<10; fixed 0x8fdcc..0x8fdf4 = 0x21..0x26 (stride 8),
   0x8fcbc=0x12, 0x8fcc4=0x13, 0x8fccc=0x14, 0x8fd2c=M(0x14), 0x8fd34=M(0x13), 0x8fd3c=M(0x12), 0x8fd44=M(0x15),
   0x8fd4c=M(0x15), 0x8fdb4=0x15, 0x8fdac=0x16 (M = Sprite_Mirror)). Then **`g_Lives = 7`**.
10. Reset: `g_MissionResult`=0, `g_AeroMissionMask`=0xffffe, `0x8f810`=0x30, `0x8fa18`=0xb0, `0x90310`=0,
    `0x80074[0]`=0 (loaded map name), `g_TilesetName[0]`=0, `0x902cc`=0, `0x901b8`=0, `g_EnemySetLoaded`=-1,
    `g_Mission`=0, `g_BriefedMission`=-1, `g_MissionActive`=0, `0x906a8`=0, `0x909e4`=0, `0x90648`=0,
    `g_AutoEjectOn`=0, `0x90710`=0, `g_ArmourBonus`=0, `0x90a5c`=0, `0x90708`=0, `0x906a4`=0, `g_EnemyBaseX`=0,
    `g_MenuRepeat`=0.
11. Menu loop (only if `!g_TrainingPicked`, always): `g_StartMission`=0; `0x9023c`=0; `MainMenu_Draw()`;
    cursor `col` 0x907d0 = 0, `row` 0x907d4 = 0, prev `0x907e8` = -1, `0x907f0` = 0.
    ```c
    while (g_MenuDone == 0) {
      Input_PollMenu();
      do { do {} while (g_Fire); } while (g_KeyDown[0x39] || g_KeyDown[0x1c]);   /* busy: port must pump */
      Input_PollMenu();
      while (!g_Fire && !g_KeyDown[0x39] && !g_KeyDown[0x1c]) {
        if (col != prevCol || row != prevRow) {
          if (prevCol != -1)        /* restore old highlight from page 1 copy */
            Video_CopyRect(0, 0,0xf0, 0x50,0x102, 0, colX[prevRow==2 ? 2 : prevCol], rowY[prevRow]);
          int c = (row == 2) ? 2 : col;
          Video_CopyRect(0, colX[c], rowY[row], colX[c]+0x50, rowY[row]+0x12, 0, 0,0xf0);  /* save */
          Sprite_DrawNow(colX[c], rowY[row], 0x1ca);                                        /* frame */
          prevCol = col; prevRow = row;
          while (Input_AnyKey(1)) ;                     /* busy wait for direction release */
        }
        Input_PollMenu();
        col = clamp(col + g_MenuDX, 0, 1);   row = clamp(row + g_MenuDY, 0, 7);
      }
      int c = (row == 2) ? 2 : col;
      g_ZoneHit /*0x8fea4*/ = Zone_HitTest(colX[c] + 8, rowY[row] + 4);
      if (g_ZoneHit == 5) { SaveGame_LoadMenu(); g_ZoneHit = 0; g_MenuChoice = 11; }
      if (g_ZoneHit == 4) { g_QuitGame = 1; g_MenuDone = 1; }
      if (g_ZoneHit == 3) { AeroOptions_Menu(); g_MenuChoice = 12; g_MenuDone = 1; }
      if (g_ZoneHit == 2) { g_MenuChoice = 10; g_MenuDone = 1; }
      if (g_ZoneHit > 5)  { g_MenuChoice = g_ZoneHit - 6; g_MenuDone = 1; }
      if (g_ZoneHit == 1 || g_ZoneHit == 2) { g_MenuChoice = 12 - g_ZoneHit; g_MenuDone = 1; }  /* 1 → 11 */
    }
    if (g_MenuChoice != 12) Video_CopyRect(0, 0,0xf0, 0x50,0x102, 0, colX[prevCol], rowY[prevRow]);
    0x901b0 = 0; g_GameMode = 0; g_PracticeEjects = 0;
    if (g_MenuChoice == 12) g_GameMode = 3;
    if (g_MenuChoice == 10) { g_GameMode = 2; g_PracticeEjects = 3; }
    if (g_MenuChoice < 10)  { g_GameMode = 1; g_StartMission = g_MenuChoice; g_TrainingPicked = 1; g_TrainingMode = 1; }
    ```
    `colX` = 0x90bc4 {4, 0x4a, 0x25}, `rowY` = 0x8da98 {0x34,0x47,0x59,0x79,0x8c,0x9f,0xb2,0xc5} (MainMenu_Draw).
    The vertical step is the raw `g_MenuDY` (-1/0/+1 per 4 retraces).
12. `g_AutoEjectOn`=0; `0x90170`=0; `if (g_TrainingPicked == 1) g_Mission = g_StartMission;` `g_TrainingPicked`=0;
    `g_AeroPlayer`=0.

Menu layout (zone id → label from MISCON.PAX): 1 COMBAT (campaign), 2 PRACTICE, 3 AEROLYMPICS, 4 QUIT,
5 LOAD GAME, TRAINING 6 LANDING, 7 RECON, 8 BOMBING, 9 NIGHT, 10 RESCUE, 11 FOG, 12 SPY DROP, 13 ATTACK,
14 SEA ATTACK, 15 DOGFIGHT (training mission = zone-6, M1 record 0..9). (likely: labels read from the
picture; ids verified.)

Quirks: **Q1** "Load game" with a missing file (`Save file not found!`) or after a load leaves
`g_MenuDone` as set by SaveGame_LoadMenu: on success `g_MenuDone=1` and choice 11 → campaign starts at the
loaded mission; on failure the menu continues (correct). **Q2** after `SaveGame_LoadMenu` the stale
`g_ZoneHit`=0 makes the following tests no-ops (fine).

### 3.2 MainMenu_Draw 0x47ec2 (verified)
`Pic_LoadPax("miscon.pax",0,0)`; `Pal_Fade(0,0x40,1,0x20)` (fade in colours 0..63); `Zone_Clear()`;
`g_MenuChoice`=11; `g_MenuDone`=0; `0x8fef0`=0x10; `0x8fef8`=0xb7;
zones: for i<5: `Zone_Add(i+1, (i%2)*0x45+5, (i/2)*0x13+0x34, (i%2)*0x45+0x48, (i/2)*0x13+0x40)`;
`Zone_Add(5, 5,0x46, 0x8e,0x59)`; for i<10: `Zone_Add(i+6, (i%2)*0x45+5, (i/2)*0x13+0x79, (i%2)*0x45+0x48, (i/2)*0x13+0x85)`.
(The zone added for i=4 has id 5 and covers x 5..0x48, y 0x5a..0x66 — the LOAD GAME button; the explicit
`Zone_Add(5,...)` above it is a second id-5 zone.) Cursor reset, `colX`/`rowY` tables set as in §3.1.

### 3.3 Zones (verified)
Table `g_Zones` 0x83358: int32[0x140], records of 5 ints {id, x1, y1, x2, y2}; id -1 = free.
- `Zone_Clear` 0x11e64: all 0x140 ints = -1.
- `Zone_Add(id,x1,y1,x2,y2)` 0x11d02: scans **int by int** (stride 1) for the first int == -1 (the `|| == id`
  term is redundant), FatalError("zonemap"," overflow!",7) if index > 0x140, writes the 5 ints there.
  Records are therefore packed (works because ids are never -1).
- `Zone_HitTest(x,y)` 0x11dbc: records in order (stride 5) until id == -1; first with
  `x1 <= x <= x2 && y1 <= y <= y2` wins; returns id or 0.

### 3.4 AeroOptions_Menu 0x48111 — `void AeroOptions_Menu(void)` (verified)
```c
Pal_Fade(0,0x100,0,0x20); Pic_LoadPax("aoset.pax",0,0); Pic_LoadPax("aoset.pax",1,0);
g_MenuDone = 0; row = 0; prevRow = -1; g_AeroPlayers = 2; g_AeroRoundsSel = 2; changed = 0;
Video_SetStartAndPan(0,10,0); Pal_Fade(0,0x100,1,0x20);
Text_DrawBig(160 - W("Games 10")/2, 0x7b, "Games 10"); ... "Players 2" at 0x8f; "Done" at 0xb7;
for (x = 0; x < 320; x++) for (y = 0x1e0; y < 0x1ef; y++) Video_PutPixel(x, y, 15);   /* highlight bar off-screen */
while (!g_MenuDone) {
  if (row != prevRow || changed) {
    /* un-highlight prevRow line: text = "Games "+itoa(rounds[sel]) (y 0x78) | "Players "+itoa(players) (0x8c) | "Done" (0xb4) */
    Video_CopyRect(1, 0,y, 320,y+15, 0, 0,y);  Text_DrawBig(160 - W/2, y+3, text);
    /* highlight row line */
    Video_CopyRect(0, 0,0x1e0, 320,0x1ef, 0, 0,y);  Text_DrawBig(160 - W/2, y+3, text);
    prevRow = row;
  }
  Input_PollMenu();
  if (g_MenuDY == -1 && row > 0) row--;  if (g_MenuDY == 1 && row < 2) row++;
  changed = 0;
  if (g_Fire) { prevRow = row;
     if (row == 1) { g_AeroPlayers = Wrap(g_AeroPlayers+1, 1, 8); changed = 1; }
     if (row == 0) { g_AeroRoundsSel = Wrap(g_AeroRoundsSel+1, 0, 3); changed = 1; }
     if (row == 2) g_MenuDone = 1; }
}
```
Only `g_Fire` (Space/joystick, not Enter) confirms here. `Wrap(v,lo,hi)` is the game's non-standard wrap
(platform/video spec): `Wrap(g_AeroPlayers+1,1,8)` with v=9 gives `9 % 8 = 1`, so players cycle 2..8,1,2
(likely; **Q3**: 1 player is selectable). Rounds cycle 3/5/10/15.

### 3.5 SaveGame_LoadMenu 0x3c0fb / SaveGame_Write 0x3c367 / SaveGame_DrawLoadPrompt 0x3c56c (verified)

File name `"js_save.000"` with the last char += slot (F1 → '1' ... F9 → '9', **F10 → ':' → "js_save.00:"**,
an invalid DOS name: **Q4** saving to F10 fails (fopen NULL → fwrite on NULL → crash in the original);
loading F10 says "Save file not found!"). Port: map F10 to "js_save.010" or keep the bug as "slot unusable".

Record (packed, little endian, 0x15 + 16 + 0x8e + 0x8e = 0x137 bytes):
```
+0  u8  g_Lives        +1 u8 g_Mission     +2 u8 g_Kills     +3 u8 0x90314   +4 u8 0x906a8
+5  u8  0x909e4 (ammo pods) +6 u8 g_AutoEjectOn  +7 u8 0x90710  +8 u8 g_ArmourBonus
+9  u8  0x90544        +10 u8 0x90944
+11 i32 g_ExtraAircraftScore  +15 i32 g_NextBonusScore  +19 i32 g_BonusScoreStep  +23 i32 g_Score[0]
+27 u16 g_PlaneUsed[0..70]   (low 16 bits of each int32)
+169 u16 g_WeaponStock[0..70]
```
Loading `fread(&int32, 1, 1)` only overwrites the **low byte** (upper bytes keep their old values: they
are 0 for all these after MainMenu, except `g_Lives` (7 → fine) and `g_Mission` (0)); u16 reads likewise
keep the upper 16 bits (0 after MainMenu, but `g_PlaneUsed[alien]` = 9999 → stays 9999 since its saved
low word is 9999). Mission ≥ 256 cannot be saved (max 149, fine). Port: read into the low byte/word, zero
the rest (identical result).

`SaveGame_LoadMenu`: `g_Mission = 0; SaveGame_DrawLoadPrompt()` (`Video_FillRect(0,0,320,10,0)`;
`Text_DrawSmall(0,0,"Press F1 to F10 to load game...",0)`); `slot = Input_GetFKey()` (busy loop, port must
pump); open "rb"; NULL → fill rect + "Save file not found!"; else read the record, then
`g_StartMission = g_Mission; 0x9023c = 1; g_MenuDone = 1; g_MenuChoice = 11` (campaign). No fclose on
the read path (**Q5**: file handle leak; harmless).
`SaveGame_Write(char slot)`: opens "wb" without NULL check, writes the record, fclose.

---------------------------------------------------------------------------------------------------

## 4. Mission_Setup 0x212ac — `void Mission_Setup(void)` (verified)

```c
Sound_StopAll(); g_StoryShown = 0; if (g_CDMusicOn) CD_Stop();
Weapons_ReturnUnused();                                   /* 0x22223, §7.4 */
if (g_GameMode == 3) g_PlaneSel = 0;
if (g_PlaneSel == 0) g_MissionActive = 0;
if (g_BriefedMission != g_Mission) {
   Mission_LoadBriefing();                                 /* §5 */
   if (g_GameMode < 3) { g_RackWeapon[0] = g_MissionDefWeapons[0]; g_RackWeapon[1] = g_MissionDefWeapons[1]; 0x8feb4 = 1; }
}
if (g_MissionActive == 0) { PlaneSelect_Screen(); 0x90828 = -1; g_TilesetName[0] = 0; g_MissionActive = 1; }
0x90654 = 0; 0x907d8 = stat84; 0x907dc = stat85; 0x90670 = 1; g_Armour = stat108 + g_ArmourBonus;
0x90168 = stat88; 0x905dc = stat89; 0x8ff14 = stat90; 0x9034c = 0; 0x906a0 = 8; g_FuelBase = g_PlaneFuel /*0x90388*/;
Mission_ResetState();                                     /* §7.3 */
if ((g_RackRounds[0] == 0 || g_RackRounds[1] != 0) && g_GameMode < 3) {   /* sic: "||" */
   if (g_RackRounds[0] == 0) g_RackWeapon[0] = g_MissionDefWeapons[2];
   if (g_RackRounds[1] == 0) g_RackWeapon[1] = g_MissionDefWeapons[3];
   Weapons_ReturnUnused(); Mission_ResetState();
}
if (g_GameMode == 3) {
   if (g_MP_TargetX0 == 0 || g_MP_TargetX0 > 1999) { g_RackWeapon[0] = g_RackWeapon[1] = 0; g_RackRounds[0] = g_RackRounds[1] = 0; }
   else { g_RackWeapon[0] = g_RackWeapon[1] = g_MissionDefWeapons[3];
          g_RackRounds[0] = g_RackRounds[1] = g_WeaponPerRack[g_MissionDefWeapons[3]]; }
}
if (g_MP_ConvoyCount && g_GameMode < 3) { Enemy_SetupSpriteIds(); g_MP_ConvoyCount = 0; }
if (strcmp(g_TilesetPending, g_TilesetName) && g_TilesetPending[0]) {
   Tileset_Load(g_TilesetPending); strcpy(g_TilesetName, g_TilesetPending);
   0x906dc = 0x90728 = 0x90750 = 0x90114 = 0x90158 = 0x90760 = 0x9071c = 0;
}
Parallax_Load(g_TilesetName);
if (!strcmp(g_MapName, g_LoadedMap /*0x80074*/) || g_MapName[0] == 0) strcpy(g_MapName, g_LoadedMap);
else { *g_MapGrid = 0; strcpy(g_LoadedMap, g_MapName); 0x90410 = 0; }
if (g_MapGrid == NULL || *g_MapGrid == 0) {      /* map not loaded yet */
   0x904ac = 0; if (!g_MapVal) g_MapVal = malloc(0x13b8); if (!0x849ec) 0x849ec = malloc(1000);
   load "map/<name>1.val" → g_MapVal; "map/<name6><NN>.mp2" → 0x849ec; "map/<name6><NN>.mxp" → Map_LoadMxp()
       (<name6> = first min(strlen,6) chars, NN = g_MapVariant as 2 decimal digits);
   0x8fea0 = 0x3f; 0x900c4 = Byte_Get(g_MapGrid,5); 0x90180 = 0x901a8 = 0x90818 = 0;
   /* find the runway: scan rows from 0x3f upwards, columns right→left, first tile attr 0x81 */
   while (0x90180 == 0) { for (x = g_MapWidth-1; x >= 0; x--) if (Map_GetTileAttr(x,row,0) == 0x81) {
         0x905f0 = Map_GetTile(x-1,row); 0x901a8 = x*16; g_CamX = 0x901a8 - 0xa0; 0x90180 = x;
         0x901ac = (0x3f - row)*16; break; }  row--; }
   if (Map_GetTileAttr(0x901a8/16, 0x3f, 0) == 0x82) 0x90818 = 1;           /* runway over water (carrier) */
   for (x = 0x90180; x >= 0; x--) if (Map_GetTileAttr(x, 0x3f - 0x901ac/16, 0) != 0x81) { 0x90180 = x*16; break; }
}
Pal_Fade(0,0x100,0,0x20);
reload "map/<name>1.val" → g_MapVal;  Map_ResetCounters();  Hud_DrawPanel();
for (i = 0; i < 10; i++) 0x863c8[i] = (float)((double)(0x903b0 * i) / 10.8 + 1.0);   /* max speed per throttle */
0x900a8 = 0x340 - 0x90064; 0x90098 = 0x40; 0x8ffd4 = 0;  g_CamX = 0x901a8 - 0x140;
if (g_IsHeli /*0x904cc*/) 0x8ffd4 = 9;
g_CamY = 0x900a8;
for (x = 0x90180/16 + 1; x <= 0x901a8/16 - 1; x++)     /* re-pave the runway (repair craters) */
   if (Map_GetTileAttr(x, 0x3f - 0x901ac/16, 0) != 0x81) Map_SetTile(x, 0x3f - 0x901ac/16, 0x905f0);
0x906a4 = 0; g_PlayerScrX = 0xa0; vy 0x9077c = 0; vx 0x90778 = 0; 0x90790 = 0; 0x8ff3c = 0; g_Speed 0x90094 = 0.0f;
g_Dir 0x90200 = 0; g_DirHalf 0x901f0 = 0; 0x8ff14 = 1; g_PlayerScrY = 0x9f - 0x8ff0c - 0x901ac;
0x90068 = 0; 0x90824 = 0; g_Crashed 0x907fc = 0; g_EjectState 0x9099c = 0; 0x908bc = 0; 0x90344 = 0;
g_OnGround 0x90440 = 1; 0x900d8 = 0; 0x904dc = -1; 0x90974 = 0; 0x908b8 = 0; 0x908b4 = 0;
if (!g_IsHeli) {
   for (i = 0; i < 16; i++) { float a = (float)(i * 3.14159265/8);       /* 0x3fd921fb4d12d84a, stored at 0x90208 */
      0x8fe10[i] = (int)(-cos((double)a) * 8.0);  0x8fe50[i] = (int)(-sin((double)a) * 3.0); }
   for (i = 0; i < 32; i++) { float a = (float)(i * 3.14159265/16);      /* 0x3fc921fb4d12d84a */
      0x8e150[i] = (int)(-(cos(a) * 16.0 + 0.5));  0x8e0d0[i] = (int)(-cos(a) * 8.0);
      0x8e290[i] = (int)(-sin(a) * 16.0);          0x8e210[i] = (int)(-sin(a) * 5.2); }  /* 0x4014cccccccccccd */
   0x8e190 = 16; g_Dir = 0; g_DirHalf = 0;
} else {
   for (i = 0; i < 7; i++) { float a = (float)(i * 1.0471975);          /* 0x3ff0c1523361e585 = 3.14159265/3 */
      0x8fe10[i] = (int)(-cos(a) * 8.0);  0x8fe50[i] = -3; }
   g_Dir = 6; g_DirHalf = 3; 0x907e0 = 5; 0x90550 = 0;
}
FUN_00015128();                                           /* play spec */
0x905d4 = 0x901fc = 0x9082c = 0x901a8; 0x90668 = -1; 0x90268 = 0; 0x90454 = -1; 0x90764 = 0; 0x8ffcc = -1;
if (0x90958) 0x90958 = 1;  0x90670 = 1;  0x8fa20[0..13] = 0;  0x90654 = 0;  0x90384 = g_CamX;  0x90628 = 0;
g_MissionActive = 1;
if ((g_FogPending || g_MP_Weather == 2 || 0x90768 == 1) && !g_NightMission && g_GameMode < 3) g_FogActive = 1;
0x9049c = 0; 0x904fc = 0; 0x903b4 = g_MapWidth*16; 0x904f8 = 0; 0x90298 = 0; 0x90664 = 0;
0x909e8 = g_MP_PickupCol*16; 0x8fed0 = 0; 0x9046c = 1;
```
Note in "cos/sin" lines `(int)` of a negative double truncates toward 0 (e.g. -7.39 → -7).

---------------------------------------------------------------------------------------------------

## 5. Briefing

### 5.1 Mission_LoadBriefing 0x2299a — `void Mission_LoadBriefing(void)` (verified)

Mission record (data/M<mode>, offset `g_Mission*0x1c2`):
```c
struct MissionRec {          /* 450 bytes */
  char  briefing[0x140];     /* +0x000 → g_BriefingText 0x92992 (0x92ad1 forced 0) */
  char  map[0x14];           /* +0x140 */
  char  tileset[0x14];       /* +0x154 */
  be16  params[30];          /* +0x168 → g_MissionParams 0x91648 (swapped to host order) */
  char  story[0x14];         /* +0x1a4 .asc name, [0x13]=0 */
  u8    defweapons[10];      /* +0x1b8 4 x BE16 → g_MissionDefWeapons[0..3]; last 2 bytes unused */
};
```
Order of operations:
1. Clear: `0x902c0 0x90190 0x90194 0x90198 0x9018c 0x9019c 0x9014c 0x90144` = 0; `g_PlaneFlown[0..39]` = 0;
   `0x908d8 0x908e8 0x902e0 0x8ffb8` = 0; `0x900a4` = 1; `0x90280 0x90948 0x90a40 0x90a44 0x90a10 0x90918` = 0.
2. Path "data/M" + ('0'+g_GameMode) built in 0x90368; fopen "rb" (NULL → FatalError(path," not found.",1));
   fseek, fread fields, fclose. `strcpy(g_MapName, map)`; `Str_TrimRight(g_MapName,0x13)`; trim tileset;
   if tileset[0]: `g_TilesetPending = tileset + ".tlx"`; trim story.
3. `g_MissionBonus = (g_Mission+1)*1000` (32000 if mode 3). Mission-parameter decoding (owner of param
   semantics: mission/play spec; order matters): `0x90148 = p29/1000; p29 %= 1000`; `0x905c0 = 0; 0x909d8 = 0`;
   `g_FinalMission = p0/100`; if `p_ConvoyCount > 999` {p %= 1000; 0x8ffe0 = 1} else 0x8ffe0 = 0;
   if `g_MP_ConvoyKills > 1000` { 0x905c0 = 1; 0x909d8 = min(kills%1000, convoyCount); kills = 0 };
   if p29 { 0x8deb0 = 0x8deb4 = p29 }; if `g_MP_ConvoyCol > 999`: n = col/1000 ground units placed at
   x = i*64 + g_MP_TargetX0*8, y = g_MapVal[x/16+0x400]*16 - 32, with 0x8d9c8=3, 0x8d8f8=-4, 0x8d8e8=8,
   0x8d9b8=4, 0x8d7d8=100, 0x8d9a8=4, 0x8d998=0 per unit; col %= 1000. Default weapons from bytes BE16.
   `g_NightMission = (g_MP_Weather == 1)`. Modes < 3: enemy base column/row (row > 0x3f → row >> 4,
   0x90410 = 0), `if (g_MP_EnemyAir) 0x90708 = 0`. Mode 3: `g_AeroGateX[i] = typesA[i]<<4`,
   `g_AeroGateY[i] = typesB[i]<<4` for i<4; params 15..22 = 0; `if (g_AeroGateX[0] > 0) convoyCount = 1`;
   `0x90334 = 0`. `0x905ac = 0; 0x90a30 = g_MP_PickupSprite; 0x90a38 = (0x90a30 == 0xca || 0x90a30 == 0xac)`.
   Enemy set `g_EnemySetIndex = p_EnemyAir/100 + 1`; modes < 3: p_EnemyAir %= 100; if the set differs from
   `g_EnemySetLoaded`: read 7 bytes at `index*7` of data/enemies → g_EnemySkill, g_EnemyGunDamage,
   g_EnemyMissiles, g_EnemyBombs, g_EnemyBombWeapon, g_EnemyBombRef, g_EnemySpxLetter; `Enemy_LoadSpx()` if
   the letter changed. `if (p0 == 2) g_Lives = 0;` (**sic**: a record with param0 == 2 kills the game —
   probably an unused "end" marker; keep).
4. Map variant: twice (for two trailing digits) `if last char of g_MapName is '0'..'9': g_MapVariant = digit;
   strip it` (the second strip overwrites the variant with the tens digit if present — likely names have one digit).
5. `0x8fa20[0..13]=0; 0x90654=0;` copy stats as in Mission_Setup (0x90168, 0x907d8, 0x907dc, 0x90670, g_Armour,
   0x905dc, 0x8ff14) — uses the *previous* plane's stats (overwritten later).
6. `Pal_Fade(0,0x100,0,0x20); Video_SetStartAndPan(0,0,0);`
   if `story[0] && g_AeroPlayer == 0`: `strcpy(g_StoryName, story); Story_ShowAsc();` (§5.2).
7. **Briefing screen**: `Pic_LoadPax("jetlogo.pax",0,0); Pal_Fade(0,0x100,1,0x20); 0x90300 = 1;
   g_HudMsgCount = 0; 0x909ec = 0x3e0;`
   - if `g_StoryShown && g_Mission != 0 && g_GameMode == 0`: centred `Text_DrawSmall` of
     "Press f1 to f10 to save the game..." at y 0x8c; `0x90348 = 0` (**sic**: also clears the ground-unit
     count set in step 3 — so missions with a story never get those ground units; likely bug, keep).
   - briefing text word-wrapped: `tmp = g_BriefingText` trimmed (0x13f); line counter 0x90434 = 2;
     `while (tmp[0]) { n = Text_FitWidth(tmp, 0x130); if (n < 1) {line = tmp; tmp[0]=0;} else {line = tmp[0..n]
     (n+1 chars, then line[n+1]... set: line[n+1-1+1]=0 i.e. keeps n+1 chars); tmp = tmp+n+1;}
     Text_DrawSmall(8, line_no*8 + 0x19, line, 0); line_no++; }` (the copy keeps chars 0..n, NUL at n+1).
   - `g_FogActive = 0; line_no++`. Title: modes < 3: `HUDTEXT[79] + "   " + itoa(g_Mission+1)`
     ("MISSION BRIEFING: MISSION   7"); mode 3: `"player " + itoa(g_AeroPlayer+1) + "  -  " + HUDTEXT[79] +
     "  " + itoa(g_AeroRound+1)`. Drawn centred at y 0x1c; underline: `for (x = 0x9e - W/2; x < W/2 + 0xa2; x++)
     Video_PutPixel(x, 0x24, 2)`.
   - `HUDTEXT[80] + "  " + itoa(g_MissionBonus) + " " + HUDTEXT[81]` ("MISSION BONUS  7000 POINTS") centred y 100.
   - **RNG: `Rand(20)`** (always called): `if ((r == 1 || g_MP_Weather == 2 || 0x90768 == 1) && !g_NightMission
     && g_GameMode < 3) g_FogActive = 1;` if set: centred HUDTEXT[82] "FOG WARNING !" at y 0x78,
     `g_FogPending = 1; g_FogActive = 0` (Mission_Setup turns it on again from g_FogPending).
   - `0x90470 = 5; WaitKey_Press();` (platform.md §4.3: confirm or F1..F10). Then scan `g_KeyDown[0x3b..0x44]`
     → slot 1..10 or 0. `if (g_StoryShown && g_Mission && g_GameMode == 0 && slot) SaveGame_Write(slot);`
     (the save is written *while continuing*; there is no separate save screen).
   - `if (g_MissionActive == 0) Pal_Fade(0,0x100,0,0x20);`
8. Campaign resupply, for every weapon i < g_WeaponCount with `g_WeaponStock[i] < 999`:
   `frac[i] += g_WeaponResupply[i]; g_WeaponStock[i] += frac[i]/10; frac[i] %= 10;` (C signed div/mod)
   — done in **every mode** at every briefing (frac = g_WeaponResupplyFrac 0x9194c).
9. `g_BriefedMission = g_Mission; g_MissionResult = 0;` steal missions: `if (2000 < X0 < 5000) { X0 -= 2000;
   if (X1 < X0) X1 = old X0 - 0x7c8; 0x90280 = 1; }` (old X0 = value before the subtraction, i.e. X1 = X0).
10. If the map name changed (vs g_LoadedMap) and non-empty: reset 0x90158 0x9037c 0x90998 0x90114, load
    .val (malloc 0x13b8), .mp2 (**malloc 2000** here vs 1000 in Mission_Setup), .mxp, Map_ResetCounters
    (g_LoadedMap is *not* updated here; Mission_Setup does it, then skips reloading because `*g_MapGrid != 0`).
11. Enemy air: modes < 3 and p_EnemyAir: `0x90a5c = min(p_EnemyAir,2); EnemyBomber_Spawn();`. Mode 3 with X0
    and no steal flag: `Map_SetTile(p_TargetsReq, x, 0x3e)` for x = X0-3..X0+3 and `Map_SetTile(p_EnemyAir, x,
    0x3f)` for x = X0-5..X0+5 (arguments as pushed: (col?,row?) — likely (row param, column loop var) order
    as written), then both params = 0; `0x90a5c = 0; 0x90708 = 0`.
12. Mode 3: if X1: `0x90948 = X1<<4; 0x9094c = AirKills<<4 (or -0x400 if 0); 0x9091c = ConvoyKills;
    0x90940 = PickupSprite; PickupSprite = 0`; if X0: `X1 = X0 + 5 + 5*steal; X0 = X0 - 5 - 5*steal`.
    Other modes: `0x902f4 = 0`; if X0 > 4999 count tiles `Map_FindTile((X0-5000)+j, x, g_MapVal[x+0xbd0]-1) >= 0`
    over x < width, j < X1 → 0x902f4 (targets to destroy).
13. `if (g_MP_Bertha) Map_StampBertha();` `0x9046c = 1; 0x9038c = 1;` pickup column ground height:
    `0x909ec = 0x3f; while (Map_GetTileAttr(col, 0x909ec, 0) > 0x7e) 0x909ec--; 0x909ec <<= 4;`
    `if (g_MP_PickupSprite > 500) 0x90a40 = g_MP_PickupCol - 1;`

### 5.2 Story_ShowAsc 0x23f6b — `void Story_ShowAsc(void)` (verified)
```c
g_StoryShown = 1;
f = fopen("data/" + g_StoryName, "r");  NULL → FatalError(g_StoryName, " not found.", 1);
fscanf(f, "%s", g_StoryName);  strcat → g_StoryName + ".pax";   /* first token = picture */
Pic_LoadPax(g_StoryName, 1, 0);                                   /* into page 1 */
fscanf("%d", &tx /*0x8fedc*/); fscanf("%d", &ty /*0x8fea0*/); fscanf("%d", &0x90540);   /* 3rd unused */
line_no = ty + 8;   text = "";                                    /* 0x85b48 */
while (!feof) fgets(text + strlen(text), 0x50, f);               /* whole rest, with newlines */
Text_DrawSmall(tx, (g_GameMode == 3)*0x28 + line_no + 0xf0, text, 0);   /* page-1 coordinates (+240) */
fclose(f);  Video_ShowPage(1);
Video_SetStartAndPan(0, g_GameMode == 3 ? 0xfa : 0xf0, 0);
if (g_GameMode == 3) Pal_SetColorNoUpload(0xff,0,0,0); else Pal_SetColorNoUpload(0xff,0x3f,0x3f,0x3f);
Pal_Fade(0,0x100,1,0x20);
0x90470 = 1; g_WaitCountInit = -500;  WaitKey_Release();          /* 0x90418 = -500 → timeout 1000 retraces */
Pal_Fade(0,0x100,0,0x20); Video_FillRect(0,0,0x140,0x1e0,0); Video_ShowPage(0);
if (g_GameMode == 3) Pal_SetColor(0xff,0x3f,0x3f,0x3f);
```
Text_DrawSmall handles embedded newlines (video spec). Timeout: WaitKey_Release counts from -500 to 500 →
1000 retraces ≈ 16.7 s (likely; platform §4.3 loop bounds).

---------------------------------------------------------------------------------------------------

## 6. PlaneSelect_Screen 0x245a9 — `int PlaneSelect_Screen(void)` (verified)

Screen `planech.pax`: 5 rows x 4 columns of plane slots per page; 20 planes per page (`g_SelPageSize`
0x90224 = 20), page `g_SelPage` 0x8fed8 (0..2 for 60 planes). Cursor in pixels (0x907d0 x, 0x907d4 y).

```c
if (g_CDMusicOn) CD_Stop();
0x907e4 = 0 /*forced alien*/; 0x85948[0] = 0; SelectScreen_LoadBg(0);   /* planech.pax, page sel = 0 */
0x900c0 = 6; g_InfoBoxW 0x90058 = 0x114; if (g_GameMode == 3) { 0x900c0 = 0x56; g_InfoBoxW = 0xc4; }
0x90350++;  0x8ff20 = 0; 0x90644 = 0; 0x902b0 = 0; g_PlaneSel = 0; g_SelPageSize = 20;
g_SelPage = min(g_SelPage, 1);     /* SelectScreen_LoadBg already set it to 0 */
cx = 0x24; cy = 0x26;  Video_SetStartAndPan(0,0,0); Pal_Upload(0xff,0x100);  faded = 0;
while (g_PlaneSel == 0) {
  0x901b4 = 0;  0x8004b = 0; 0x8004f = 6; Pal_Fade(0x40,0x80,0,0x10); 0x8004f = 0;   /* fade sprite colours */
  Pic_LoadPax("planech.pax",0,0);  if (!faded) { Pal_Fade(0,0x80,1,0x20); faded = 1; }
  Zone_Clear(); 0x8004b = 1;
  for (r = 0; r < 5; r++) for (c = 0; c < 4; c++) {
    k = c + r*4 + g_SelPage*20;                                   /* 0-based plane, also 0x9028c */
    Zone_Add(r*4+1+c, c*0x46+4, r*0x20+0x21, c*0x46+0x45, r*0x20+0x3e);
    if (k < min(g_PlaneCount, 60)) {
      if (g_PlaneUsed[k] < 9999 && ((g_GameMode & 1) == 0 || k < 4)) {    /* training: first 4 only */
        if (g_GameMode == 3) {
          if (c == 3) { if (g_AeroUsedRacks[g_AeroPlayer] == 0 && g_Mission > 0) Sprite_DrawNow(0x100, r*0x20+0x39, 0x207);  /* "?" */
                        else Sprite_DrawNow(0xf8, r*0x20+0x39, g_MissionDefWeapons[2] + 0x90a2c - 1); }
          else Sprite_DrawNow(c*0x47+0x23, r*0x20+0x39, g_MissionDefWeapons[r*4+c] + 0x90a2c - 1);
        } else Sprite_DrawNow(c*0x47+0x23, r*0x20+0x39, k + 0x90a2c);
        left = g_PlaneLimit[k] - g_PlaneUsed[k];
        text = (left <= 0) ? HUDTEXT[72] "TRASHED" : (left < 200) ? itoa(left) : HUDTEXT[87] "LOTS !";
        Text_DrawCenteredAt(0, c*0x47+0x18, r*0x20+0x49, text);
      } else if (g_PlaneUsed[k] > 9998) Text_DrawCenteredAt(0, c*0x47+0x18, r*0x20+0x49, " ");
    }
  }
  if (faded) { 0x8004f = 6; Pal_Fade(0x40,0x80,1,0x10); 0x8004f = 0; }
  if ((g_GameMode & 1) == 0) Zone_Add(0x17, 0x90,0xc0, 0x119,0xcd);     /* "next page" arrow */
  else { g_InfoText[0] = 0; SelectScreen_DrawInfoBox(0); }
  hl = -1;
  while (0x901b4 == 0) {                                               /* inner input loop, 15 Hz */
    z = Zone_HitTest(cx, cy);
    if (z > 0 && z < 0x15) { save rect under slot frame (Video_CopyRect to page-0 y 0xf0 area);
        hl = 0; Sprite_DrawNow(((cx-0x24)/0x47)*0x47+0x13, ((cy-0x26)/0x20)*0x20+0x35, 0x1c5); }
    if (z == 0x17) { save rect; hl = 1; Sprite_DrawNow(0xe4, 0xd4, 0x1cb); }
    if (g_ZoneHit > 0 && g_ZoneHit < 0x15) g_PlaneCand = g_ZoneHit + g_SelPage*20;   /* 0x9002c */
    g_PlaneCand = Clamp(g_PlaneCand, 0, 0x3d);
    if (g_GameMode == 3) { s = min(g_PlaneCand, 4); 0x90170 = 0; g_AeroPlaneSlot = s;
       if (s == 4) { if (!strcmp(g_PlaneNames[g_MissionDefWeapons[2]-1], "Glider") || g_AeroUsedRacks[g_AeroPlayer] || g_Mission < 1)
                       { g_PlaneCand = g_MissionDefWeapons[2]; g_AeroPlaneSlot = 3; }
                     else g_PlaneCand = Rand(g_PlaneCount - 1) + 1; }                 /* RNG */
       else if (s > 0) g_PlaneCand = g_MissionDefWeapons[s - 1]; }    /* (0x8e374 + s*4) = defweapons[s-1] */
    g_PlaneCand = Clamp(g_PlaneCand, 0, 0x3c);
    if (g_PlaneCand != 0x902b0 && (g_GameMode != 1 || g_PlaneCand < 5)) { SelectScreen_ShowPlaneName(); 0x902b0 = g_PlaneCand; }
    Input_PollMenu();
    if (hl == 0) restore slot rect; else if (hl == 1) restore arrow rect;  hl = -1;
    if (Zone_HitTest(cx,cy) == 0x17) { if (g_MenuDY == -1) cy = 0xa6; }
    else { cx = clamp(cx + g_MenuDX*0x48, 0x26, 0xfc);
           ny = cy + (g_GameMode != 3) * g_MenuDY * 0x20;   /* Aerolimits: column only */
           cy = min(ny, (g_GameMode & 1) == 0 ? 0x100 : 0x40); if (cy < 0x26) cy = 0x26; }
    if (cy > 0xbf) { if ((g_GameMode & 1) == 0) { cy = 0xc0; cx = 0xfc; } else cy = 0x26; }
    g_ZoneHit = Zone_HitTest(cx, cy);
    g_Confirm 0x905cc = (g_Fire || g_KeyDown[0x39] || g_KeyDown[0x1c]);
    if (((g_ZoneHit == 0x17 && g_Confirm) || g_Ctrl[1] /*TurnR*/) && (g_GameMode & 1) == 0) {
       g_SelPage++; m = min(g_PlaneCount-1, 0x3b); if (g_SelPage*20 > m) g_SelPage = 0;   /* (a-b != 0 && b <= a) */
       0x901b4 = 1; while (confirm held) Input_PollMenu(); }
    if (g_Ctrl[0] /*TurnL*/ && (g_GameMode & 1) == 0) { if (--g_SelPage == -1) g_SelPage = min(g_PlaneCount-1,0x3b)/20;
       0x901b4 = 1; while (g_Ctrl[1]) Input_PollMenu(); }               /* sic: waits on TurnR */
    if (g_ZoneHit > 0 && g_ZoneHit < 0x15) g_PlaneCand = g_ZoneHit + g_SelPage*20;
    if (g_GameMode == 3) { s = min(g_PlaneCand, 3); 0x90170 = 0; g_AeroPlaneSlot = s; if (s > 0) g_PlaneCand = g_MissionDefWeapons[s-1]; }
    if (g_AlienAbduct > 0x3f) { g_PlaneCand = g_AlienPlaneIdx + 1; 0x90170 = 0; 0x907e4 = 1; }
    if ((g_Confirm && g_PlaneCand > 0 && (g_SelPage*20 + g_ZoneHit == g_PlaneCand || g_GameMode == 3)) || g_AlienAbduct > 0x3f) {
       g_PlaneSel = min(g_PlaneCand, g_PlaneCount + 1);
       PlaneRecord_Load();                                             /* 0x25add, §6.1 */
       if ((g_PlaneUsed[g_PlaneSel-1] < g_PlaneLimit[g_PlaneSel-1] && (g_GameMode != 1 || g_PlaneSel < 5)) || 0x907e4) {
          0x901b4 = 1; if (g_AlienAbduct > 0x3f) g_AlienAbduct = -1;
          strcpy(g_InfoText, g_PlaneDesc /*0x8c4d8*/); SelectScreen_DrawInfoBox(0);
       } else { strcpy(g_InfoText, g_PlaneUsed[g_PlaneSel-1] == 9999 ? HUDTEXT[73] "DON'T BE SILLY"
                                                                       : HUDTEXT[74] "SORRY, THIS AIRCRAFT IS NOT AVAILABLE");
                SelectScreen_DrawInfoBox(0); g_PlaneSel = 0; }
    }
  }
}
if (0x9012c == 0) { FUN_0001adae() /*empty*/; 0x9012c = 1; }
0x90474 = g_PlaneSel; 0x90064 = 0; Plane_SetupSprites();             /* loads g_PlaneStats from MISC.Z */
0x909f8 = g_PlaneSel; 0x90594 = 0;
/* copy stats (index = (addr-0x91684)/4) */
0x90060=s62 0x90258=s63 0x90148=s64 0x90838=s65 0x90358=s66 0x9008c=s67 0x90508=s68 0x905a4=s69 0x90560=s70
0x90564=s71 0x90850=s72 0x90188=s73 0x90450=s74 0x8ff9c=s75 0x8fee0=s76 0x9045c=s77 0x908d0=s78
0x906b0=s79 (255 → 0)
g_StallSpeed 0x90090 = (float)((double)s80 / 100.0); 0x90780 = (float)((double)s81 / 100.0); 0x903b0 = s82 (top speed);
g_Thrust 0x90674 = (float)((double)s83 / 100.0); if (g_Thrust == 0 /*bits*/) 0x90594 = 1;   /* glider */
0x907d8 = s84; 0x907dc = s85; 0x907a0 = (float)((double)s86 / 100.0);
if (0x90594) { 0x90780 = (float)((double)0x90780 / 5.0); g_StallSpeed = g_StallSpeed / 100.0f; }
0x90954 = s87 (> 10 → -1); 0x90168=s88 0x905dc=s89 0x8ff14=s90 g_IsHeli 0x904cc=s91 0x90610=s92 0x903d4=s93
0x90558 = s94 % 10; 0x901f8 = s94 / 10; 0x90a34=s95 0x90958=s96 0x9090c=s97 0x90a48=s98 0x90a4c=s99
0x8ff0c=s100 g_RackPoints[0]=s101 g_RackPoints[1]=s102 g_HardpointLoad=s103 g_RackMaxLoad[0]=s104 g_RackMaxLoad[1]=s105
g_PlaneFuel 0x90388=s106 0x90788=s107 g_Armour=s108 0x901dc=s109 0x901e0=s110 0x906d0=s111 0x903c0=s112
0x8ff20=s113 0x90644=s114 0x9006c=s115 0x8ffe4=s116 0x8ffe8=s117 0x90130=s118 0x90844=s119 0x90070=s120
if (s111 != 0x906e8) { 0x90724 = s111; if (s111 % 10 == 3) 0x90724 = s111 - 3; }  return value = s111/10 (unused)
0x900a4 = 1; 0x8fa18 = 0xb0; 0x90150 = 1; 0x90300 = 0; 0x9046c = 1; if (g_AlienAbduct == 0x40) g_AlienAbduct = -1;
```
WeaponSelect_Screen is **not** called from here (the RE guide's tree is wrong): it is reached only through
`Airbase_Update` 0x2bc0b when the player taxis to the hangar (the mission starts on the runway), §7.

Quirks: **Q7** TurnL page-back waits on `g_Ctrl[1]` (TurnR) release instead of TurnL — holding TurnL flips
pages every 15 Hz tick. Keep. **Q8** `g_PlaneCand` clamp to 0x3d then 0x3c; `g_PlaneSel = min(cand,
g_PlaneCount+1)` allows plane 60 (alien) only through the abduction path because its slot shows " " and
`g_PlaneUsed = 9999` refuses it ("DON'T BE SILLY").

### 6.1 PlaneRecord_Load 0x25add (was "PilotRecord_Load": it reads the *aircraft* record) (verified)
Copies 0xdc bytes from `g_MiscData + (g_PlaneSel-1)*0xdc`: `+0` 20 chars → 0x8c3d8 (name), `+0x14` 20 →
0x8c458 (plane .spx/.hd base name, used by Plane_SetupSprites), `+0x28` 0x78 → g_PlaneDesc 0x8c4d8,
`+0xa0` 20 → 0x85848; then `0x8c558[i] = (0x85848[i]-1) ^ "Mixamatosis is Fun !"[i]` for i<21
(decoded "mission code"/password string, 0x8c56c = 0). Owner of the semantics: data spec.

### 6.2 Helpers (verified)
- `SelectScreen_LoadBg(int wep)` 0x257c3: `0x8004b = 1; Pic_LoadPax(wep ? "wepch.pax" : "planech.pax",0,0);
  0x8fa18 = 0x90308*0x40 + 0x100; 0x90300 = 0; 0x8f810 = 0x30; g_SelPage = 0;`
- `Text_DrawCenteredAt(page, x, y, s)` 0x25869: `Text_DrawSmall(x + 0x1d - Text_WidthSmall(s)/2, y, s, page)`.
- `SelectScreen_DrawInfoBox(int right)` 0x258b6: line counter 0x90434 = 3; colour arg 0x53 (0x5a in mode 3,
  which also forces `right = 1`); `Video_FillRect(right ? 0x53 : 0x13, 0x17, right ? 0x127 : 299, 0x33, 0, col)`;
  mode 3: `Text_DrawBig(0x14, 0x22, "player " + ('1'+g_AeroPlayer), 0)`; then word-wrap `g_InfoText` 0x84c48
  with width `g_InfoBoxW - 6` like the briefing, each line at `(right ? 0x53 : 0x13, line*6 + 7)`.
  Lines are cut with `strncpy(n+1)` and NUL at **[n]** (here: drops the break char).
- `SelectScreen_ShowPlaneName` 0x25a5d: if 0 < cand < 0x3d: `g_InfoText = g_PlaneNames[cand-1]`, or
  HUDTEXT[77] "STOLEN AIRCRAFT" if `g_PlaneUsed[cand-1] == 9999`; DrawInfoBox(0).

---------------------------------------------------------------------------------------------------

## 7. Weapons

### 7.1 Load formula (shared by Mission_ResetState and WeaponSelect_Screen) (verified)
For rack r (0/1) with weapon w = g_RackWeapon[r], per = g_WeaponPerRack[w], wt = g_WeaponWeight[w]:
```c
if (g_HardpointLoad < wt) rounds = 0;
else {
  a = (g_RackMaxLoad[r] / wt) * per;                      /* by rack weight limit      */
  b = ((g_HardpointLoad * g_RackPoints[r]) / wt) * per;   /* by hard points            */
  m = min(a, b);                                          /* "a < b ? a : b"           */
  m = min(m, g_WeaponRackMult[w] * g_RackPoints[r] * per);/* rack multiplier cap       */
  rounds = min(m, g_WeaponStock[w] * per);                /* stock (loads, not rounds) */
}
```
All C int (signed, truncating). `g_WeaponStock` counts *loads*; rounds = loads * per.

### 7.2 WeaponSelect_Screen 0x29d90 — `void WeaponSelect_Screen(void)` (verified)
Called only from `Airbase_Update` (0x2bc0b, in flight, when the plane stands at the hangar position
0x9082c with speed 0 and `0x908f0 == 0`); Airbase_Update first resets fuel/armour stats and plays Sfx 0x18.
```c
if (0x936f4) CD_Stop();  0x90140 = 0; Sound_StopAll(); 0x904f8 = 0; Weapons_ReturnUnused(); 0x9034c = 0;
0x90300 = 1; Pal_Fade(0,0x100,0,0x20); g_MissionActive = 1; SelectScreen_LoadBg(1);   /* wepch.pax */
Video_SetStartAndPan(0,0x14,0); Video_SetSplitLine(400); 0x906a0 = 8; 0x908f0 = 1; g_FuelBase = g_PlaneFuel;
g_MaxLoadWeight = ((g_RackMaxLoad[0] + g_RackMaxLoad[1]) * 2) / 3;
for (r = 0; r < 2; r++) g_RackRounds[r] = §7.1;           /* (stock not deducted yet) */
g_RackSel = 0; 0x900c0 = 0x4b; g_InfoBoxW = 0xce; col = row = 0; g_MenuDX = g_MenuDY = 0; g_MenuDone = 0; first = 0;
while (!g_MenuDone) {
  0x901b4 = 0;
  if (first) Pal_Fade(0x40,0x80,0,0x10);
  if (!first) { Pal_Fade(0,0x80,1,0x20); Pal_Upload(0xff,0x100); first++; }
  else { 0x8004b = 0; Pic_LoadPax("wepch.pax",0,0); 0x8004b = 1; }
  Video_CopyRect(0, 0x10,0xb5, 100,200, 0, 0,400);   Video_CopyRect(0, 0x10,0xd5, 100,0xe8, 0, 0xa0,400);  /* save rack boxes */
  for (r = 0; r < 4; r++) for (c = 0; c < 4; c++) { i = g_SelPage*g_SelPageSize + c + r*4;     /* page size: 20 on 1st pass (sic), 16 later */
     if (i > g_WeaponCount || i > 0x3b || g_WeaponIcon[i] < 2) Text_DrawCenteredAt(0, c*0x47+0x15, r*0x20+0x49, "          ");
     else { if (g_WeaponStock[i] > 0) Sprite_DrawNow(c*0x47+0x24, r*0x20+0x3d, g_WeaponIcon[i]);
            name = first 10 chars of g_WeaponNames[i]; Text_DrawCenteredAt(0, c*0x47+0x15, r*0x20+0x49 + (r != 0), name); } }
  Pal_Fade(0x40,0x80,1,0x10); Zone_Clear();
  for (r..4) for (c..4) Zone_Add(r*4+1+c, c, r, c, r);        /* zones in CELL units */
  0x9043c = 3; WeaponSelect_DrawCounts();
  /* stock preview for the current rack (stored in 0x8fefc / 0x8ff30, display only) */
  Zone_Add(0x17, 0,4, 3,4);  Zone_Add(0x19, 0,5, 2,5);  Zone_Add(0x18, 3,5, 3,5);   /* arrow / NEW PLANE / DONE */
  g_SelPageSize = 16;
  while (0x901b4 == 0) {
    z = Zone_HitTest(col, row);   /* never 0: every cell is covered */
    if (0 < z < 0x15) { save; Sprite_DrawNow(col*0x47+0x11, row*0x21+0x35 - (row != 0) - (row == 3), 0x1c5); hl = 1; }
    if (z == 0x17) { col = 3; row = 4; save; Sprite_DrawNow(0xe2,0xb7,0x1cb); hl = 2; }
    if (z == 0x19) { col = 2; row = 5; save; Sprite_DrawNow(0x9c,0xe2,0x1cb); hl = 3; }
    if (z == 0x18) { col = 3; row = 5; save; Sprite_DrawNow(0xe2,0xe2,0x1cb); hl = 4; }
    restore rack boxes; if (g_RackRounds[0] > 0) Sprite_DrawNow(0x26,0xbe,g_WeaponIcon[g_RackWeapon[0]]);
                        if (g_RackRounds[1] > 0) Sprite_DrawNow(0x26,0xde,g_WeaponIcon[g_RackWeapon[1]]);
    Sprite_DrawNow(0x48, g_RackSel*0x20 + 0xbe, 0x9d);           /* rack pointer */
    Input_PollMenu();  restore the highlight by hl;  hl = 0;
    col = clamp(col + g_MenuDX, 0, 3); row = clamp(row + g_MenuDY, 0, 5);
    if (g_SelPage == 3 && row == 3) row = g_MenuDY + 3;           /* last page has 3 rows */
    if (g_Confirm || g_Ctrl[0] || g_Ctrl[1] || g_Fire || Space || Enter) {
      g_ZoneHit = Zone_HitTest(col, row);
      if (g_Ctrl[2]) { col = 3; row = 0x9043c + 1; g_ZoneHit = 0; }   /* dead: g_Ctrl[2] is always 0 */
      if (0 < g_ZoneHit < 0x15 && (g_Fire || Space || Enter)) {
        if (g_Ctrl[3]) g_RackSel = 0;  if (g_Ctrl[5]) g_RackSel = 1;  /* dead (always 0) */
        i = g_SelPage*g_SelPageSize + g_ZoneHit;                       /* 1-based */
        if (g_WeaponIcon[i-1] > 1) {
          g_RackWeapon[g_RackSel] = min(i - 1, g_WeaponCount);
          if (g_HardpointLoad < g_WeaponWeight[g_RackWeapon[g_RackSel]]) g_RackRounds[g_RackSel] = 0;
          else g_RackRounds[g_RackSel] = §7.1 with stock' = max(0, stock + (other rack holds the same weapon ? +other_rounds/per : 0));
          WeaponSelect_DrawInfo();  g_RackSel = 1 - g_RackSel;  WeaponSelect_DrawCounts();
        }
      }
      if (g_ZoneHit == 0x15 || g_ZoneHit == 0x16) { g_RackSel = g_ZoneHit - 0x15; WeaponSelect_DrawCounts(); }  /* dead: no such zones */
      if (g_Ctrl[0]) { if (--g_SelPage < 0) g_SelPage = 3; 0x901b4 = 1; while (g_Ctrl[0]) Input_PollMenu(); col = 3; row = 4; }
      if (g_ZoneHit == 0x17 || g_Ctrl[1]) { if (++g_SelPage*g_SelPageSize > 0x3b) g_SelPage = 0; 0x901b4 = 1; while (g_Ctrl[1]) Input_PollMenu(); col = 3; row = 4; }
      if (g_ZoneHit == 0x18 && confirm) { g_MenuDone = 1; 0x901b4 = 1; }                 /* DONE */
      if (g_ZoneHit == 0x19 && confirm) { g_MissionActive = 0; g_MenuDone = 1; 0x901b4 = 1; }   /* NEW PLANE */
      while (confirm held) Input_PollMenu();
    }
  }
}
Mission_ResetState(); 0x90300 = 0; Pal_Fade(0,0x100,0,0x20); 0x8fa18 = 0xb0; 0x9046c = 1;
```
The weapon rack "sign" in the stock preview: `(other rack same weapon) ? -1 : 0` is used for the preview
(0x29ef9..) but `+1` (`== → 1`) for the actual load (0x2b6xx); verified as written — **Q9** inconsistent
preview (display only).
NEW PLANE: `g_MissionActive = 0` ends the frame loop (its condition needs `g_MissionActive == 1`), no life
lost (`g_OnGround` = 1, §9.1), and the next attempt runs PlaneSelect again (briefing skipped because
`g_BriefedMission == g_Mission`).

Quirk **Q10** `WeaponSelect_DrawCounts` 0x2ba3c draws: rack 0 loads `g_RackRounds[0]/per0` at (0x6d,0xba),
rack 1 loads at (0x6d,0xd9) (4 digits), rack 0 weight `wt0*rounds0/per0` at (0x74,200), rack 1 weight
`wt1*rounds1/per0` at (0x74,0xe7) (5 digits) — **divides by rack 0's per-rack count** (bug; keep or fix with
a PORT note). `Text_DrawNumber(x,y,v,digits,page)`.
`WeaponSelect_DrawInfo` 0x2bb33 (was WeaponSelect_DrawLine): `g_InfoText = g_WeaponDesc[i] + "  -  " +
itoa(g_WeaponStock[i]) + HUDTEXT[75] " AVAILABLE"` + (rack rounds == 0 && stock > 0 ? HUDTEXT[76]
" ---NOT LOADED---" : ""), `SelectScreen_DrawInfoBox()` (no argument pushed: uses whatever is on the stack
— **Q11**, effectively the left box in practice; guess).

### 7.3 Mission_ResetState 0x222f0 — `void Mission_ResetState(void)` (verified)
```c
if (g_CDMusicOn) CD_Stop();
g_FuelBase = g_PlaneFuel; g_LoadWeight = 0;
for (g_RackSel = 0; g_RackSel < 2; g_RackSel++) { w = g_RackWeapon[r];
  if (g_HardpointLoad < g_WeaponWeight[w]) g_RackRounds[r] = 0;
  else { n = §7.1 (min(...), stock*per); 0x8ff30 = n; g_RackRounds[r] = n;
         if (g_WeaponType[w*6] == 10) { g_RackRounds[r] = min(g_RackRounds[r], 0x90838 /*stat65*/);
                                        g_LoadWeight = g_WeaponWeight[w] * g_RackRounds[r]; }    /* sic: '=' */
         g_LoadWeight += g_WeaponWeight[w] * g_RackRounds[r] / per;
         g_WeaponStock[w] -= n / per; }                                    /* stock deducted by the *uncapped* n */
  if (g_WeaponType[w*6] == 5) { g_FuelBase += 0x90788 /*stat107*/; g_LoadWeight += 0x90788/4; g_RackRounds[r] = 1; }   /* drop tank */
  if (g_WeaponType[w*6] == 4) { g_LoadWeight = g_LoadWeight - g_WeaponWeight[w]*g_RackRounds[r]/per + 100; g_RackRounds[r] = 10; }  /* recon camera */
}
g_MaxLoadWeight = (int)((double)(g_RackMaxLoad[0] + g_RackMaxLoad[1]) / 1.5);
g_Fuel = g_FuelBase + 0x90648 * 1000;
0x909c4 = (g_GameMode < 3) ? 0x903c0 + 0x909e4 * 100 : 0;       /* gun ammo + ammo pods */
```
`/4` of 0x90788 is C signed division. Quirk **Q12** type-10 weapons: the stock is reduced by the uncapped
count and `g_LoadWeight` is assigned (not added) before the general add. Keep.

### 7.4 Weapons_ReturnUnused 0x22223 (verified)
`for (g_RackSel = 0; g_RackSel < 2; g_RackSel++) if (g_RackRounds[r] > 0) { 0x8ff44 = g_RackRounds[r] /
per(g_RackWeapon[r]); g_WeaponStock[g_RackWeapon[r]] += 0x8ff44; g_RackRounds[r] = 0; }`

---------------------------------------------------------------------------------------------------

## 8. The mission frame loop (Game_Run 0x1cc2f..0x20db3)

### 8.1 Loop condition (verified)
```c
while ( ( g_DeathTimer /*0x90824*/ < 0x20
          || (0x90344 > 0 && g_DeathTimer < 0x50) || g_PlayerScrY < 0x9d || g_EjectState > 0 )
        && g_EjectState < 99 && g_MissionResult < 2
        && g_MissionActive == 1
        && ( g_OnGround != 8 || 0x9041c > 7 || g_Speed != 0 /*bits*/ || g_EjectState != 0 ) )
```
So a mission attempt ends when: the death timer ran out (crash), the pilot was picked up/killed
(g_EjectState ≥ 99), the mission succeeded (result 2), NEW PLANE was chosen, or the plane ditched
(g_OnGround == 8) and stopped with 0x9041c ≤ 7.

### 8.2 Per-frame order (verified for the call order; every call listed with the condition that guards it)

One iteration = one game frame = `1 + g_VSyncWaits` retraces (platform.md §6). The **flip is the first
thing in the frame**: it shows the page drawn during the previous iteration; everything below draws into
the new back page. `Input_ReadControls` runs inside `Player_Update` (step 49), i.e. input is sampled near
the end of the frame and acts on the next frame.

```
 1  Video_FlipPage()                                                      @0x1ccb8
 2  if (0x8ff68 > 0 || 0x8ff70 > 0)  look-around: SwapInt(&g_CamX,&0x8ff68), (&g_CamY,&0x8ff70), (&0x90384,&0x8ffa4), (&0x90374,&0x8ff50)
 3  0x90574 = 0; 0x9052c = 0                      (target-arrow world target)
 4  if (g_EjectState) { 0x909b0 = g_CamX; g_CamX = 0x9024c; 0x909b4 = g_CamY; g_CamY = 0x90250;
                        if (oldCamX/32 == 0x9024c/32) 0x9024c += 2; }   (/32 = C division)
 5  horizontal wrap: if (g_CamX > g_MapPixW 0x903b4) { g_CamX &= 15; 0x8ff10 = -1; }
                     if (g_CamX < 1) { g_CamX = g_MapPixW - 16 + (g_CamX & 15); 0x8ff10 = -1; }
 6  g_CamY = Clamp(g_CamY, -2000, 0x340); camYc 0x9039c = min(g_CamY, 0x340); if (camYc < 0) camYc %= 0x40 (C rem)
 7  0x904f0 = Div16(g_CamX); 0x904f4 = Div16(camYc) (0x13810, same as Div16); 0x904c4 = (g_CamY + 0x800)/16
 8  0x90604 = g_ShakeX; g_ShakeX = g_CamX & 15; g_ShakeY = g_CamY & 15     (fine scroll for the flip)
 9  dx 0x907a4 = g_CamX - 0x90384; dy 0x907a8 = g_CamY - 0x90374; 0x9083c/0x90840 = dx/dy;
    if (abs(dx) > 16 || abs(dy) > 16) { dx = 0x907bc; dy = 0x9076c; }  (keep last frame's on jumps)
10  if (g_FogActive) { 0x90650 = Clamp((g_CamY + 0x96)/0x43 + 1, 0, 15); if (0x90650 != 0x9065c) Pal_NightAltitude(); }  0x460ba
11  if (0x8ffb0) 0x8ffb0--                         (lightning cooldown)
12  if (0x90318 > 0) FUN_1676a()                   (0x90318--)
13  Level_DrawBackground(0x904f0 + 1, 0x904f4 + 1)                       @0x1cf91  (video spec)
14  if ((0x904dc != 0x904f0 || 0x904e0 != 0x904c4 || 0x904dc == -1) && !g_EjectState) {
       if (abs(0x904f0 - 0x904dc) > 16) 0x904dc = 0x904f0 - dx;
       0x8ff18 = ((g_ShakeX + g_CamX + g_PlayerScrX)/16) % (g_MapWidth - 1);
       0x90690 = mp2[0x8ff18]; 0x90668 = 0x9010c = 0x90268 = 0x9047c = 0;
       if (0x90690 && !g_Crashed) FUN_3e1a3();                          (column objects from .mp2)
    }
15  0x907bc = dx; 0x9076c = dy; 0x90384 = g_CamX; 0x90374 = g_CamY; 0x904dc = 0x904f0; 0x904e0 = 0x904c4;
    if (g_EjectState) { g_CamX = 0x909b0; g_CamY = 0x909b4; }
16  Sprite_ClearQueue()                                                   @0x1d0e1
17  0x90034 = 0x90030; 0x90030 = 0; 0x8ffc4 = 0
18  player world pos 0x90a18/0x90a1c = (look ? 0x8ff68/0x8ff70 : g_CamX/g_CamY) + g_PlayerScrX/Y; save scr in 0x90a50/0x90a54;
    if (g_EjectState > 0) { g_PlayerScrX/Y = 0x90270/0x90274; g_CamX/Y = 0x9024c/0x90250; }
19  if (0x8fff8 > 0) 0x8fff8--
20  if (!g_FogActive) Video_NightStub()  (empty)
21  if (0x901e4) Windsock_Update FUN_16e38()
22  if (0x9089c && IsOnScreen(cam, 0x9089c, 0x908a0)) Sprite_Queue(0x9089c - camX, 0x908a0 + 15 - camY, 0x1e8)
23  if (0x8ffc0 > 0 && 0x8ff10 == -1 && 0x8ffb8 && !g_DirHalf) Sprite_Queue(scrX + 0x901dc, scrY + 0x901e0, 0xa9)
24  if (g_DeathTimer < 16) { if (IsOnScreen(...player world pos...)) {
        if (!0x903d4) queue the player sprite: id = dirTable[g_DirHalf][gear frame] + (0x16 if burner/hover) (0x8fcb8 table)
        else 0x9339c = Player_Collide(...) (hover type; temporarily forces g_DirHalf = 7 when turning on the ground)
        if (0x90a34 == 1 && throttle > 6) FUN_156a2()   (afterburner flame, FP + Rand)
        if (g_DirHalf == 0 || g_DirHalf == 16) FUN_1554b()  (wheels/smoke, Rand(2))
      } } else Debris_Update()
25  FUN_110a1 (empty); if (g_NightMission) Runway_Update(); FUN_110bc() returns 0 → its branch (Sfx + damage) is dead
26  0x9042c = 0x90454; 0x90454 = -1
27  if (0x8ffb8) Player_Ejection()
28  if (0x8ff94 || 0x90310 || g_MP_PickupCol || 0x906dc || 0x90158 || 0x90998 || 0x90528) Player_DeathAndLanding()
29  if (0x90608) Turrets_Update();  if (0x906fc) Helis_Update();  if (0x90348) EnemyGround_Update()
30  if (0x90708 == 0) { 0x906a4 += Rand(2); if ((g_CamY + g_PlayerScrY)/16 < g_MP_CeilingRow) 0x906a4++;
                        if (0x906a4 > 300 && (g_GameMode & 1) == 0) { 0x90a5c = Rand(2) + 1; EnemyBomber_Spawn(); 0x906a4 = 0; } }
    else EnemyAir_Update()
31  if (0x902c0) Building_Update();  if (0x905ac) Convoy_Update();  if (0x900b4 > 0) Ships_Update()
32  if (0x90180 - 0x140 < g_CamX < 0x901a8 + 0x260 && g_CamY > 0x330 - 0x901ac) Balloons_Update()
33  if (frameParity 0x905a0 == 0) FUN_15ce6()   (special weapons: FUN_177db / Weapon_FireSpecialA / B)
34  if (g_AlienAbduct > 0) FUN_164d2()           (alien abduction sequence)
35  RNG: Rand(9) then Rand(2) — arguments of FUN_110e1, an empty stub that returns 0x900fc unchanged.
    **Both Rand calls happen every frame** (keep for RNG fidelity).
36  Particles_Update(g_CamX, g_CamY)
37  if (0x90890) Bonus_Update();  if (0x90918 > 0) Pickup_Update();  if (0x90498) FUN_3dc4e()
38  if (g_OnGround == 1 && !0x8ff08 && !0x90414) FUN_16dcd()   (sets the runway end targets)
39  if (0x90180 - 0x140 < g_CamX < 0x901a8 + 0x260 && (g_OnGround == 1 || 0x8ff08 == 1)) Airbase_Update()   (→ WeaponSelect_Screen)
40  if (0x904ac == 1 && !0x90498) { 0x90498 = 1; 0x90488 = 0x901a8; 0x904ac = 0; }
    if (g_MP_TargetMarker) { 0x90574 = ((X1 - X0)/2 + X0)*16; 0x9052c = 0x3e0; }
    target objects from .mp2 (each: if active draw + AI else flush pending Map_SetTile):
      0x9010c: FUN_3e0b2(), FUN_3ef72() (Rand(100)…); 0x90268: FUN_3dea2(), FUN_3e8b3() (campaign only);
      0x90668: FUN_3dfaa(), FUN_3ec5b()
41  0x9089c second sprite 0x1e9 at +0x29;  if (0x90948) Crate_Update();  if (0x90754) Mines_Update();
    if (g_AeroGateX[0x90334]) Waypoint_Update();  if (0x902e0) Flak_Update()
42  Hud_DrawTargetArrow(g_CamX, g_CamY, 0x90574, 0x9052c)
43  if (0x90660) 0x90660--;  if (0x908bc && !g_Crashed && !g_EjectState) Projectiles_UpdateGuided()
44  frameParity 0x905a0 = 1 - 0x905a0;  if (0x901d8) 0x901d8--;  0x907ec = Wrap(0x907ec + 1, 0, 0x31)
45  if (g_EjectState) FUN_3c600() (parachute);  if (0x900d8) FUN_16b82() (engine damage: Rand(8),Rand(8),Rand(1),Rand(200))
    smoke/fire emitters (0x9037c, 0x90398, 0x905b0, 0x8ff2c, 0x9085c, 0x907b0, landing dust when g_OnGround==1, 0x90834) —
    each consumes Rand calls in the order listed in js.c 0x1db4c..0x1e0c1 and calls the empty stubs FUN_11105 /
    FUN_1112a; 0x9085c: Particle_Spawn(wx<<8, wy<<8, 0, -0x80, 0x10, 0x18)
46  if (g_Crashed || 0x900d8 > 3) burning-wreck loop: n = 1; for (;;) { lim = (Rand(7)+4 > 10 - 8*0x9045c) ? 10 - 8*0x9045c : Rand(7)+4;
        if (lim < n) break; Rand(5); Rand(16); Rand(16); Rand(16); Rand(16); FUN_11012 (stub); n++; }   (Rand order exact)
47  if (g_EjectState) { g_CamX = wx - 0x90a50; g_PlayerScrX = 0x90a50; g_CamY = min(wy - 0x90a54, 0x340); g_PlayerScrY = 0x90a54; }
48  LIGHTNING (video.md §7.1): if (g_NightMission && 0x8ffb0 == 0) { r = Rand(1000); if (r == 1) → flash }
    else-if / also: if (g_KeyDown[F3] && 0x8ffb0 == 0) → flash.  Exactly: night && cooldown==0 → Rand(1000) is consumed every frame.
    The flash draws lines into the back page **before** Sprite_DrawQueue (so sprites cover the bolt).
49a if (g_Ctrl[4] /*Fire, 0x90ca8*/ && 0x8ff14 == 0x905dc && !g_Crashed && !g_EjectState && !g_OnGround) Player_Weapons()
49b if (0x904f8) FUN_26fd2 (empty); if (0x905d8 && ...) FUN_26fb7 (empty); FUN_112e1 (empty)
50  Sprite_DrawQueue()                                                     @0x1e3fa
51  if (0x8ffa0) Targets_Update();  Bullets_Update(g_CamX, g_CamY);  FUN_26ae8 (empty)
52  if (g_HudMsgCount) Hud_DrawMessages()
53  steal mission: if (0x90280 && player column == X0 + 4) StealMission_Taken()
54  if (0x902fc) { FUN_14abe() (queued explosions → Explosion_Damage); 0x902fc = 0; }
55  recon photo: if (g_MP_ReconCol && |col - ReconCol| <= 1 && ReconRow*16 <= wy) { ReconCol = ReconRow = 0;
        Hud_PushMessage(g_GameMode == 3 ? "Mind Your Head!" : HUDTEXT[0]); FUN_400da(); }
56  FUN_11653() returns 0 (stub) → dead branch with Rand(9)
57  if (0x902ec && frameParity == 0) { 0x902ec--; Sfx_Play(0xe, 9000, 0x3f, wx); }
    if (0x8fff8 == 4) Sfx_Play(4, 25000, 0x3f, wx);  if (0x901c8) FUN_3dd5e() (resupply crate: Rand…)
58  0x902b4 = (0x907dc >= 2 && g_LoadWeight > g_MaxLoadWeight) ? 1 : 0        (overloaded)
59  if (0x90af4 && 0x908bc < 8) FUN_1679b (empty)
60  undo look-around swap (same 4 SwapInt as step 2)
61  if (0x9068c > 0) { 0x9068c = max(0x9068c - 0x888, 0); Pal_SetColor12(0x1a, 0x9068c); }   (flash fade)
62  0x90620 += (0x908d0 * throttle)/2; if (0x90258) 0x90238 ^= 1 (1-x); if (0x90620 > 19) { 0x90664 = 1 - 0x90664; 0x90620 = 0; if (0x90664) Sfx_Play(8,15000,0x2b,wx); }
63  ground probes: 0x905e8 = g_ShakeY (16 if g_CamY > 0x33f); col 0x8ff18 = wx/16 + 1 (wrap by width);
    0x909ac = g_CamY + g_PlayerScrY + 0x8ff0c*0x8ff14; row 0x8ff1c = min(0x909ac/16 + 1, 0x3f);
    if (col out of map || row < 11 || g_Crashed) clear 0x90000/4/8/0x90030/0x8ffc4/row
    else { 0x9028c = g_EjectState ? 1 : FUN_11678 (stub → 1); if (g_OnGround) { 0x90994 = attr(col,row+1,3), 0x9005c = attr(col-2,row,0) }
           0x90000 = attr(col,row-1), 0x90004 = attr(col,row)*0x9028c, 0x90008 = attr(col,row+1);
           sub = 0x909ac & 15; if (sub > 13) 0x90030 = 0x90008; if (sub < 3) { 0x8ffc4 = 0x90000; 0x90030 = 0x90004; } else 0x8ffc4 = 0x90004; }
64  if (frameParity == 1 && !g_Crashed) Hud_UpdateRadar()                 (radar every 2nd frame)
65  if (0x9046c == 1 && g_MissionActive == 1) { 0x90300 = 1; FUN_1adc9() (fog/night palette refresh); 0x9046c = 0; FUN_14923() (Rand(40)/Rand(10) ambient sfx) }
66  if (0x8ff34 && --0x8ff34 == 0 && 0x90190 == 1) Sfx_Play(0x17, 0x1004, 0x3f, wx)
67  FLIGHT PHYSICS block A (velocity) §8.3   0x1ea8c..0x1ed84
68  g_CamX += vx; horizontal screen drift (0x1ed84..0x1ee00) ; vertical move (§8.3 B)
69  ... landing/stall/crash detection, Mission_CheckComplete (§8.3 C)          0x1ee00..0x1f4c0
70  crash-site snapshot, ground-follow, ejection throttle decay, fuel/speed (§8.3 D)  0x1f4c0..0x20140
71  if (g_OnGround == 8) FUN_15bb6()  (ditching: skid, Rand(5), Rand(20)…)
72  if (g_TrainingMode && !0x8fa24) FUN_152a0()  (training instructor hints)
73  if (0x90068 > 0) { if (0x909a0) FUN_15cb3(); if (0x90130) nose-drop of g_Dir (8..23 → +1 else -1, wrap 63) }
74  if (0x9085c && --0x908a4 == 0) 0x9085c = 0
75  if (g_NextBonusScore <= g_Score[g_AeroPlayer] && !0x90890 && !g_OnGround) { 0x909a8 = wx; 0x909ac = g_CamY - 300; Bonus_Spawn(); }
76  if (!0x8ff94 || 0x8ff8c == -17) Player_Update()                      @0x2024f  (reads input; pause; eject key; Esc)
77  if (g_Ctrl[4] && 0x8ff8c && !g_OnGround && 0x8ff8c != -17) 0x8ff8c = -17
78  if (0x8ff14 - 0x905dc == 1 && !g_IsHeli && !0x8fa28) FUN_1693b()      (gear/flaps physics)
79  AUTO-EJECT: if ((0x900d8 > 3 || g_Crashed > 0) && !g_EjectState && g_AutoEjectOn == 1) {
        Hud_PushMessage("Autoejecting");
        g_NextBonusScore += g_BonusScoreStep; if (g_NextBonusScore < g_Score[p] + 1) g_NextBonusScore = g_Score[1+p-1] (0x8e334[p]);
        g_NextBonusScore += 1000; if (g_NextBonusScore < g_MissionBonus) g_NextBonusScore = g_MissionBonus;
        Hud_PushMessage("Next Bonus at " + itoa(g_NextBonusScore));
        g_EjectState = 1; 0x8ff8c = -17; 0x903fc = 0; 0x90270 = scrX; 0x90274 = scrY - 8; 0x9024c = camX; 0x90250 = camY;
        0x90968 = -16; 0x8fef4 = 16; g_PracticeEjects = max(g_PracticeEjects - 1, 0); 0x90328 = Sign(0x90328) * wx; g_AutoEjectOn = 0; }
80  controls (if !g_Crashed && !g_EjectState && 0x900d8 < 3): 0x901c4 = g_Dir; g_LastAction 0x9044c = g_KeyCode 0x90490;
    g_KeyCode = Input_GetKeyCode_Stub() (0);  throttle keys 1..0 (platform §3 hard-wired keys) unless 0x8fa24/0x8fa4c;
    if (0x8fa24 && throttle > 0) throttle--;
    if (0x909dc == 1 && !g_IsHeli) { ... } else { TurnL/TurnR → Player_ThrottleDown/Up; heli pitch Player_PitchDown/Up;
      heli hover drift 0x907e0; Up/Down → Player_RotateA/B (if !0x90460); 0x90464 = Up; 0x904a0 = Down }
    if (on runway && the new direction is not allowed) g_Dir = 0x901c4;  if (0x909a0 && !heli) FUN_16620();
    if (g_Dir changed && g_Speed > 5.0f(bits) && !heli) 0x8ff2c = 1   (wing-tip vapour)
81  Engine_SoundUpdate()
82  if (g_CamY < 0) { if (Rand(400000 /*0x61a80*/) == 0x144fd && !g_AlienAbduct) g_AlienAbduct = 0; }   (sic: sets 0 → no-op; the
    alien event is never triggered here; RNG call still consumed when above the top)
83  bonus plane: if (g_MissionResult == 1 && g_GameMode == 0 && !0x90918 && g_Mission == 0x90e8c[0x9084c]) { 0x90918 = 2;
        0x9092c = (0x901a8 - 0x90180)/2 + 0x90180; 0x90930 = 0x3e0; Hud_PushMessage(HUDTEXT[86] "PRIZE BALLOON LAUNCHED !"); }
    Aerolimits pickup: if (g_GameMode == 3 && 0x8fff0/2 < 0x8ffec && !0x90918 && 0x90910 > 0) { 0x90918 = 1; 0x9092c = 0x90910; 0x90930 = 0x3e0; 0x90910 = 0; }
84  g_DirHalf = g_Dir / 2; 0x90438 = 0; 0x902d4 = 0;
85  weapons (if !g_EjectState && !g_Crashed): 0x90438 = (g_Confirm && gear up and !heli);
    left fire request 0x8453c && conditions → { 0x8ff00 = 1; g_RackSel = 0; Weapon_Fire(); 0x8ff00 = 0; g_RackSel = 0; Weapon_Fire(); }
    right 0x84540 → same with g_RackSel = 1; clear latches; 0x90624 = 0; 0x9067c = (g_Confirm != 0)
86  0x905b4 -= Sign(0x905b4); keep the player between screen rows 0x50..0xa0 by moving g_CamY
87  if (0x90114 > 0 && Rand(100) == 1 && !0x90158) start ship/submarine event (0x90158 = 1, …, 0x900d4 = 0x3df)
88  campaign score-triggered bomber: if (mode 0 && !0x906dc && !0x90728) { 0x90760 += 0x8ffec - 0x9071c; 0x9071c = 0x8ffec;
        if (0x90760 > 20000) { 0x906dc = 1; ...; if (!0x90708) { 0x90a5c = 2; EnemyBomber_Spawn(); } } }
89  Aerolimits timer: if (mode 3) { if (!0x8ffd0) 0x8ffec = 0; if (g_MissionResult != 1) 0x903d0 = 0x8ffec; 0x8ffec = 0x903d0;
        if (0x8fff0 > 0 && 0x8fff0 - 550 <= 0x903d0 && (0x9002c = (0x8fff0 - 0x903d0)/50) < 11 && 0x9002c != 0x903dc) { FUN_1536c (empty); 0x903dc = 0x9002c; } }
90  0x908c8 -= Sign(0x908c8); 0x90738 -= Sign(0x90738); if (0x907ec == 1) 0x90288 = 0x8ffec
91  if (g_MP_Bertha && Rand(101) == 1 && 0x908c8 < 1) Bertha_Update()
92  if (0x908d8 && ++0x908d8 == 20) { 0x908d8 = 0; Explosion_Damage(0x908f4, 0x3ff - 0x901ac, 0, 0, 2000, 2000); }
93  if (0x8feb4) Hud_WeaponMessage();  if (0x909b8) Hud_PlaneMessage();  if (0x90280) StealMission_Update()
```
(The guards are copied from the decompile; numbers like "@0x1ccb8" are call sites. Bodies of the called
updaters belong to the play/enemy/player/video specs.)

### 8.3 Flight physics inlined in Game_Run (verified with capstone)

`g_Speed` 0x90094 float; `vx` 0x90778 and `vy` 0x9077c int (the same globals the platform spec calls
g_MenuDX/g_MenuDY); `g_DirHalf` 0x901f0 0..31; `throttle` 0x8ffd4 0..9; `g_OnGround` 0x90440;
`climb` 0x90068; `stallTimer`... names are tentative (player spec may rename).

**A. velocity (0x1ea8c)**, if `!g_Crashed`:
```c
if (!g_IsHeli) {
  t = (float)e0d0[d] * g_Speed;  0x8ff38 = (int)(t - (float)vx);                 /* __FSM, __FSS, __FSI4 */
  if (bits(g_Speed) >= 0x40800000 /*4.0*/ && throttle > 8)
       vx += Clamp(0x8ff38, -2*s85 + 0x902b4 + s115, 2*s85 - 0x902b4 - s115);   /* s85=0x907dc s115=0x9006c */
  else vx += Clamp(0x8ff38, -2*s85 + 0x902b4,        2*s85 - 0x902b4);
  vx = Clamp(vx, -16, 16);
  0x8ff3c = (int)((float)e210[d] * g_Speed);
  if (g_Speed < f907a0 /*FSC*/ && 0x8ff3c < 0) 0x8ff3c = (int)((float)0x8ff3c * g_Speed / f907a0);
  0x90790 = (g_OnGround == 0 && g_Speed < f907a0) ? 1 : 0;      /* sink when slow */
  0x90790 = Clamp(0x90790 + climb + 0x8ff3c, -16, 16);
  if (bits(g_Speed) >= 0x40800000 && throttle >= 8)
       vy += Clamp(0x90790 - vy, 0x902b4 - s85 + s115, 0x907d8);
  else vy += Clamp(0x90790 - vy, 0x902b4 - s85,        0x907d8);
} else { 0x90784 = 0x90550; vx += Sign(0x90550 - vx); vy = -16; }
```
else (crashed): `0x907e0 = max(0x907e0 - 2, 0); if (Rand(4) == 1) vx -= Sign(vx);` then vy toward ±16 by 2
(`+2` up to 16 for planes, `-2` down to -16 for helis).

**B. move (0x1ed84)**: `g_CamX += vx;` if `abs(vx) == 16 && bits(g_Speed) > 0x40a00000 (5.0)`:
`g_PlayerScrX = Clamp(g_PlayerScrX - Sign(vx), 0x20, 0x120)` else if `!0x904b8`: `g_PlayerScrX -= Sign(g_PlayerScrX - 0x28)`.
Planes: if `g_OnGround == 0 || vy < 0`: if `g_CamY < 0x340` `g_CamY += Clamp(vy,-16,16)` else
`{ t = vy; if (abs(vy) > 2) t = (int)(((double)(abs(vy)-2) * 60.0 / (double)g_PlayerScrY + 2.0) * (double)Sign(vy));
   g_PlayerScrY += Clamp(t, -14, 14); }` (0x90690 holds t). Helis: `v = ((0x907e0-5)*vy)/10`; if `g_OnGround == 0
|| v < 0`: same split on `g_CamY < 0x340` with `Clamp(v,-16,16)`.

**C. ground contact (0x1ee00..0x1f4c0)**: look-around fix (0x8ff94), on-runway sprite test (0x90abc =
0/1/2 from the direction sprite table and heli tilt), `g_OnGround = 0`; `0x9015c = -1` while over the
runway rectangle; **landing**: `g_OnGround = 1` when tile attr 0x90030 ∈ [0x81 - 0x90450, 0x90644 + 0x81]
and (gear down (0x8ff14 == 1) or (0x90644 == 1 && attr == 0x82 /*water, seaplane*/)) and (climb == 0 or
(climb < 5 && wy - 17 < 0x900a8)) and 0x90abc == 1 and (vy < 1 || speed < 3.0f(bits) || heli) and !crashed;
then if climb > 0: `climb = 0; FUN_16d70()` (Rand(5)==1 → undercarriage failure); **ditching**: attr in
0x80..0x8b, not landed, (gear up or 0x905dc == 1), climb == 0, (not over runway or attr == 0x81), 0x90abc == 1,
same vy/speed test, !crashed → `g_OnGround = 8; g_Speed = (float)((double)g_Speed - 0.02); clamp ≥ 0; if
(Rand(3) == 1) { 0x907f8 = 1; Player_DamageSystems(); }`. **crash**: (wy > 0x3df or (0x8ffc4 > 0x7e && not over
runway)) && !crashed && !landed → `g_Crashed = 1; 0x907f8 = 4; FUN_3f777(); FUN_3f727(); Player_DamageSystems();`
5 particles (`Rand(1)`, `Rand(16)` x2 per particle, exact order js.c 0x1f388..), `Explosion_Damage(wx, wy, vx, 0,
s119, s119)`. `if (g_OnGround == 1) Mission_CheckComplete(); else 0x907c0 = 0;`
Taxi-stop: `if (0x904b8 > 0) { g_Speed = (float)((double)g_Speed - 0.5); clamp ≥0; g_PlayerScrX--; if (--0x904b8 == 0
|| (g_Speed == 0 && abs(oldScrX - 0x95) < 16)) { g_PlayerScrX = 0x94; g_Speed = 0; 0x904b8 = 0; } }`.

**D. speed (0x1f4c0..0x20140)**: crash snapshot (0x908b8 = tile attr under the wreck, 0x90908 = attr at
(col, row?), 0x908b4 = g_DirHalf/4) used by the debrief; nose-to-ground follow on the runway; on-ground
throttle-down (`0x90460`); `if (g_EjectState && Rand(1) && --throttle < 0) throttle = 0;`
planes: `g_Fuel = max(g_Fuel - throttle - 100*0x90654, 0)`; if glider (0x90594) `Camera_Update()` (sic: the
glider model, owner player spec) else:
```c
thr = throttle - 2*0x900d8 - g_OnGround*(7 - 3*(0x8ff20 + (0x90994 >= 0xfd)));  thr = max(thr, 0);   /* in 0x92b04 */
a = (float)thr * g_Thrust;
a = (float)((double)a / ((double)(float)g_OnGround + 1.0));
a = (float)((double)a * ((double)(g_CamY + 0x800) / 2700.0));          /* thinner air higher up (camY smaller) */
a = (float)((double)a - (double)g_OnGround * 0.001 * (double)s85);
a = (float)((double)a - (double)((float)0x90884 + f90780 + (0x90958 == 2 ? 5.0f : 0)) * 0.0005);  /* drag: load + plane */
if (!g_OnGround) a = (float)((double)a + (double)e210[d] * 0.15);       /* gravity along the path */
a = (float)((double)a - (0.002 - (double)(float)0x8ff14 * 0.002));     /* gear-up bonus (0x8ff14: 1 = gear down) */
g_Speed = g_Speed + a;                                                  /* float add */
lim = (e210[d] > 0) ? 0x863c8[throttle] + 6.0f : 0x863c8[throttle];  if (bits(lim) > 0x40c00000) lim = 6.0f;
t2 = (float)0x9085c + g_Speed;  if (lim > t2) lim = t2;
g_Speed = (bits(lim) < 0) ? 0.0f : lim;
if (0x90834 > 0) { g_Speed += 3.0f; if (bits(g_Speed) > 0x40c00000) g_Speed = 3.0f; 0x90834--; }
```
(constants: 2700.0 = 0x40a5180000000000, 0.001 = 0x3f50624dd2f1a9fc, 0.0005 = 0x3f40624dd2f1a9fc, 0.15 =
0x3fc3333333333333, 0.002 = 0x3f60624dd2f1a9fc.) Helis: `0x900a8 = 0; d = max(8, s*100+throttle)...;
g_Fuel = max(g_Fuel - max(8, throttle + 100*0x90654), 0); climb = 0; g_Speed = (float)Clamp((int)((double)(abs(0x90550)*6) * 0.0625), 0, 6);
if (!0x90cb8 && !0x90cbc) 0x90550 -= Sign(0x90550) * frameParity;`
Out of fuel / engine dead (`g_Fuel == 0 || 0x8fa24 || 0x8fa4c`): colour 0x20 pulses (0x92b14 ± 4 between
0x20 and 0x3f, `Pal_SetColor(0x20, v, 0, 0)`), `throttle = 0; 0x907e0 = max(0x907e0 - 3, 4)`, Sfx 9 when
`0x907ec == 0 && !glider`, helis `g_Speed = clamp((float)((double)g_Speed - 1.0), 0, 6.0f)`.
Airborne (`g_OnGround == 0`): `g_PlaneFlown[g_PlaneSel-1] = 1; 0x902b0 = 1; 0x90828 = 0; 0x90958 = Sign(0x90958);`
stall entry: if `g_Speed < g_StallSpeed*6.0f` or (Sign(0x8ff38) == -Sign(vx) && abs(0x8ff38 - vx) > 15) and
!heli: `0x900a8 = min(0x900a8, wy); climb++ (max 0x90098/16); if (climb == 0x90098/16) 0x90098 = min(0x90098+1, 0xc0)`.
`if (g_Speed < f907a0*0.5 && !g_OnGround) g_Speed = max((float)((double)g_Speed - 0.01), 0)`;
stall recovery: if `!(g_Speed < g_StallSpeed*6.0f)` and not (signs differ && abs(0x8ff38 - vx) ≥ 16) and !heli:
`climb = max(climb-1,0); 0x90098 = max(0x90098 - 16, 0x40); 0x900a8 = 9999`.

### 8.4 Pause, abort, other in-flight keys (Player_Update, owner player spec; verified)
- Pause (`g_KeyPause`): `g_LastAction = 3; 0x90430 = 1; save 0x8ffec; Pal_Fade(0,0x100,2,0x10)` (half-out
  = dimmed), busy-wait until no key in `g_KeyDown[0..0x7f]` is down, then until one is down,
  `Pal_Fade(0,0x100,3,0x10)`, restore 0x8ffec (Aerolimits timer frozen). No text, no sound stop (CD keeps
  playing). Port: pump events in both waits.
- Esc (`g_KeyAbort`): `if (!g_OnGround) { 0x900d8 = 3; g_Lives = -8; }` — self-destruct: engine damage 3
  (auto-eject possible) and the run ends with lives -8 (§10: EndGame, then menu). On the ground Esc does nothing.

### 8.5 After the frame loop, inside the attempt loop (verified)
```c
if (g_MissionActive == 1) g_AbortFlag = 1;                 /* normal end → leave the attempt loop */
if (g_OnGround == 0 && g_AlienAbduct < 0x40) {             /* lost the aircraft in the air */
   0x90768 = 0; 0x901b8 = 0; 0x902cc = 0;
   if (s77 /*0x9045c*/ == 0) g_Lives--;
   if (g_GameMode == 0 && g_PlaneLimit[p-1] < 200
       && (g_PlaneUsed[p-1] += 3) >= g_PlaneLimit[p-1] && g_AlienPlaneIdx + 1 != p) g_PlaneSel = 0;   /* sic: +3 per loss */
   if (0x90888 == 0) { 0x90314 = 0x906a8 = 0x909e4 = 0x90648 = 0x90710 = 0; g_ArmourBonus = 0; g_MissionBonus = 0; 0x90544 = 0x90944 = 0; }
   else 0x90888--;                                           /* keep bonuses for n losses */
}
```
`g_PlaneUsed += 3` is inside the `&&` chain: it is only executed when `mode == 0 && limit < 200`
(short-circuit, verified). NEW PLANE (WeaponSelect) leaves `g_MissionActive = 0` → `g_AbortFlag` stays 0 →
the attempt loop repeats (no debrief).

---------------------------------------------------------------------------------------------------

## 9. End of a mission

### 9.1 Game_Run 0x20ee8.. (verified)
```c
if (g_CDMusicOn) CD_Stop();
g_HudMsgCount = 0;
Level_DrawBackground(0x904f0 + 1, 0x904f4 + 1); Sprite_DrawQueue(); Video_FlipPage();   /* last frame (sprites of the last loop) */
Sound_StopAll();
if (g_MissionResult == 2 && g_GameMode < 3 && g_Lives > -8) {
   for (i = 0; i < 60; i++) if (g_GameMode == 0 && g_PlaneLimit[i] < 200 && g_PlaneFlown[i] == 1) g_PlaneUsed[i]++;
   if (g_PlaneUsed[p-1] >= g_PlaneLimit[p-1]) g_PlaneSel = 0;
   g_Mission++; 0x9038c = 0;
}
if ((g_GameMode > 0 && ((g_Mission == 10 && g_GameMode != 3) || g_AeroRound == g_AeroRoundsOptions[g_AeroRoundsSel]))
    || g_Mission > 149 || (g_FinalMission == 1 && g_MissionResult == 2))
   g_Lives = -10;                                            /* game completed */
if (g_Lives > -999) Mission_Debrief();
if (g_FogActive) Pal_Restore();
```
Training: missions chain from the picked one to 9 (each success → next); practice 0..9; Aerolimits by rounds.
The campaign ends after record 149 or on a record flagged final (param0/100 == 1).

### 9.2 Mission_Debrief 0x2c9cb — `void Mission_Debrief(void)` (verified)
Draws only text, over the last game frame (no picture). The back page is toggled first so the text goes to
the **visible** page: `g_BackPage = (g_BackPage == 0x18c0) ? 0x78c0 : 0x18c0`. Text_DrawSmall writes to
`g_BackPage` (video spec) at screen coordinates shifted by `g_ShakeX/g_ShakeY` (the fine scroll).
RNG calls in order; `S(n)` = g_SarcasmLines[n]:
```c
if (g_SarcasmLines[0][0] == 0) Sarcasm_Load();       /* 28 x fgets(0x50) from data/sarcasm, newline kept */
g_EndGameIndex = 3; g_AbortFlag = 1; if (0x9049c == 1) g_EndGameIndex = 5;
txt = S(Rand(3));                                     /* 0..3 generic crash */
if (Rand(1) && (0x908b4 == 0 || 0x908b4 > 7)) { txt = S(4) "By the way, Pull Up !"; g_EndGameIndex = 2; }
if (0x908b8 == 0x82 || 0x90908 == 0x82) { txt = S(Rand(2) + 5); g_EndGameIndex = 1; }        /* water */
if (0x908b8 == 0x81) { g_EndGameIndex = 4; txt = S(Rand(3) + 8); }                           /* runway */
if (g_EjectState > 99) { g_EndGameIndex = 0; txt = S(12); if (Rand(1)) txt = S(13); }        /* pilot died */
if (g_EjectState == 99) { g_EndGameIndex = 6; g_AbortFlag = 0; txt = S(14);
   if (Rand(1) && 0x9034c > 4) txt = S(Rand(1) + 15);
   if (Rand(1) && 0x908b8 == 0x82) txt = S(17);
   if (Rand(1)) txt = S(18); }
if (g_OnGround > 1 && !g_EjectState) { g_AbortFlag = 0; txt = S(Rand(1) + 19); }             /* ditched */
if (g_Lives == 0 && g_EjectState == 99) { g_AbortFlag = 0; g_EndGameIndex = 6; txt = S(21) "You're fired !"; }
if (g_MissionResult == 2) { g_AbortFlag = 0; txt = S(0x90a10*3 + Rand(2) + 22); }          /* 22..24 good, 25..27 poor */
else if (g_GameMode == 3) { txt = HUDTEXT[Rand(1) + 58]; g_AbortFlag = 0; }                  /* "MISSION FAILED, NO POINTS !" / "NIL POINT !" */
if (g_AlienAbduct > 0x3f) { txt = HUDTEXT[60] "NO ONE WILL BELIEVE YOU !"; g_AbortFlag = 0; g_PlaneSel = 0; }
if (txt[strlen-1] != ' ') strcat(txt, " ");           /* txt = g_DebriefText 0x84f48 (newline from fgets kept!) */
if (g_EjectState > 0) g_OnGround = 0;
0x90354 = 0x28;
draw " " + txt word-wrapped at 0xf0 px, lines at y = line*10 + 0x2c + g_ShakeY, centred +g_ShakeX (page arg 1);
   (a line is split with strncpy(n) without terminator: the tail of the previous line may remain — Q13)
if (g_EjectState == 99 && g_GameMode == 0) centred "YOU HAVE" + itoa(g_Lives) + " CRASHES LEFT" at y 0x190 (0x28*8+0x50) + g_ShakeY
if (g_MissionResult == 2) {
   line2 = "";
   if (g_MissionBonus < 1) line1 = HUDTEXT[71] "NO BONUS";
   else { mult = (g_GameMode == 3) ? (g_MissionBonus -= 0x903d0*10 + 0x903b8, g_AeroPlaneSlot) : s77 + 1;   /* 0x90904 */
          line1 = HUDTEXT[69] "BONUS" + itoa(g_MissionBonus) + " PTS";
          g_Score[g_AeroPlayer] += g_MissionBonus * mult;
          if (mult > 1) line2 = " x" + itoa(mult) + HUDTEXT[70] " FOR BRAVERY"; }
   draw line1 centred at y 0x190 + g_ShakeY; if (line2[0]) draw line2 at 0x192 using line1's width (sic) }
if (g_GameMode == 3) { "PLAYER 1" + " SCORE" + itoa(g_Score[0]) at y 0x1a6; "PLAYER 1" + " SCORE" + itoa(g_Score[1]) at y 0x1b2 }
```
Notes: y 0x190.. are page-row coordinates below the 240 visible lines?? — 0x28*8+0x50 = 400: these lines
are drawn at page row 400+g_ShakeY, i.e. **in the HUD panel area** (the split screen shows VRAM offset 0 below
row 175; Text_DrawSmall's y is relative to g_BackPage). (likely; verify against the video spec's Text_DrawSmall
origin.) **Q14**: both Aerolimits score lines say "PLAYER 1" (HUDTEXT[61]); the second should be HUDTEXT[62].
Keep or fix (PORT note). **Q15**: the bonus multiplier `s77+1` = "bravery" factor per aircraft (MISC.Z stat 77);
in Aerolimits the slot number (0..4) multiplies — slot 0 gives **0 points**.
No sarcasm in the original file beyond line 27 is used (file has 30 lines; lines 28/29 unused).

Sarcasm index table (DATA/SARCASM): 0-3 crash, 4 pull up, 5-7 water, 8-11 runway, 12-13 pilot killed,
14 ejected, 15-16 ejected with kills>4, 17 ejected into water, 18 generic eject, 19-20 ditched/bad
landing, 21 fired (last crash), 22-24 success, 25-27 success with 0x90a10 = 1 ("poor").

### 9.3 Aerolimits rotation (Game_Run 0x21064, verified)
```c
if (g_GameMode == 3 && g_Lives > -8) {
   g_AbortFlag = 0; if (g_Lives > 1) g_Lives = 7;
   g_AeroPlayer = Wrap(g_AeroPlayer + 1, 0, g_AeroPlayers - 1);
   0x90910 = (0x90180 - 500) + Rand(1200);     /* RNG */
   g_BriefedMission = -1; 0x9038c = 0;
   if (g_AeroPlayer == 0) {
      while (!Bit_Test(g_Mission, g_AeroMissionMask) && g_AeroMissionMask != 0) g_Mission = Rand(20);   /* test first! */
      Bit_Clear(g_Mission, &g_AeroMissionMask);
      if (++g_AeroRound == g_AeroRoundsOptions[g_AeroRoundsSel]) g_Lives = -10;
   }
}
```
Round 1 is always M3 record 0 (MainMenu sets g_Mission = 0, bit 0 is clear in 0xffffe); each later round
draws records 1..19 without repetition (Rand(20) yields 0..20; 0 and 20 rejected while the mask is
non-zero). With 15 rounds after 19 records the mask is never exhausted. **Q16**: `Wrap(p+1,0,n-1)` with the
game's Wrap: `v > lo ? v % (hi-lo+1) : (v+lo) % ...` = `(p+1) % n` — fine.

---------------------------------------------------------------------------------------------------

## 10. End game / continue (Game_Run 0x2115b.., verified)

```c
if (g_AbortFlag == 1 || (g_Lives < 1 && g_Lives > -999)) EndGame_Screen();
else g_AbortFlag = 2;                                       /* next mission, no menu */
0x90470 = 1; WaitKey_Release();                             /* 8.3 s timeout, platform §4.3 */
Pal_Fade(0,0x100,0,0x20);  Video_FillRect(0,0,0x140,0xf0,0);
for (i = 0; i < 0xff; i++) if (i < 0x40 && i > 0x60) Pal_SetColor(i,0,0,0);   /* dead loop (Q17) */
if (g_AbortFlag == 1 || g_Lives < 0) {
   0x9038c = 0; if (g_GameMode != 0) g_MenuDone = 2;
   0x90180 = 0; 0x901a8 = 0; g_AbortFlag = 0;
   if (g_MenuDone != 2) g_Lives = 7;                        /* campaign */
}
```
→ next outer iteration shows the menu unless `g_AbortFlag == 2`.

### 10.1 EndGame_Screen 0x301b2 — `void EndGame_Screen(void)` (verified)
```c
0x90470 = 0;
if (g_Lives == -10) {                     /* finished */
  if (0x907b4 == 1) 0x907b4 = 0;
  if (g_GameMode == 0) { Pal_Fade(0,0x100,0,0x20); Pic_LoadPax("gamedone.pax",0,0); Video_SetStartAndPan(0,0,0);
                         Video_SetSplitLine(400); Pal_Fade(0,0x100,1,0x20); }
  else if (g_GameMode == 3) AeroScores_Screen();
  else { x = 0x90180 + (0x901a8 - 0x90180)/2;  Level_DrawBackground(x/16 - 9, 0x35);
         s = HUDTEXT[65] "FINAL SCORE" + itoa(g_Score[0]);  Text_DrawSmall(160 - W(s)/2, 0x78, s, 0);
         TrainingCredits_Screen(); }      /* the picture replaces it immediately (Q18) */
} else {                                  /* crash / last life / Esc */
  if (0x907b4 == 1) 0x907b4 = 0;
  name = "endgame" + ('0' + g_EndGameIndex) + ".pax";
  Pal_Fade(0,0x100,0,0x20); Video_SetSplitLine(400); Video_SetStartAndPan(0,0x14,0); Pic_LoadPax(name,0,0);
  text = g_DebriefText; y = 0x0c;
  word-wrap at 0xf0, each line centred at x = g_ShakeX + 160 - W/2, y += 10;
  if (g_CDMusicOn) { 0x936f4 = 1; CD_PlayTrack(g_EndGameIndex + 7); }
  Pal_Fade(0,0x100,1,0x20);
}
```
Note: with g_Lives == -8 (Esc) and g_AbortFlag == 0 (e.g. ejected) the condition `g_Lives < 1` still shows
the crash picture. `0x936f4` makes WeaponSelect stop the CD (never reached after this).

### 10.2 AeroScores_Screen 0x45352 (verified)
`Pal_Fade out; Pic_LoadPax("aoscores.pax",0,0); Video_SetStartAndPan(0,0,0); Video_SetSplitLine(400);`
`best = 0; bestScore = 0;` for i < g_AeroPlayers: `if (bestScore < g_Score[i]) { best = i; bestScore = g_Score[i]; }`
line = `"player " + itoa(i+1) + " scored 00000000"` with the score's digits overwriting the tail
(`strcpy(line + strlen(line) - strlen(score), score)`), `Text_DrawBig(160 - W/2, i*13 + 0x56, line)`;
then `"player " + itoa(best+1) + " wins !!"` at y 0xbe; fade in. Ties → lowest player number; all-zero →
player 1 wins. Scores beyond `g_Score[1]` (players 3..8) live at 0x8e338.. (int array of 8, likely).

### 10.3 TrainingCredits_Screen 0x304e0 (verified)
`Pal_Fade out; g_StoryName = (mode 1) "traincrt.pax" / (mode 2) "practcrt.pax"; Pic_LoadPax(g_StoryName,0,0);
Video_SetSplitLine(400); Video_SetStartAndPan(0,0,0); Pal_Fade(0,0x40,1,0x20)`.

---------------------------------------------------------------------------------------------------

## 11. CD audio, credits, misc placement (verified)

- CD check: MainMenu first call only (§3.1). Intro plays CD track 2 itself (intro.md).
- Mission music: §2.3, random {2,3,4,5,6,14,15}, `CD_Stop` in Mission_Setup, Mission_ResetState,
  PlaneSelect_Screen (if on), after the frame loop, WeaponSelect (only if 0x936f4). End game jingle track
  `g_EndGameIndex + 7` (7..13). No music in menus/briefing.
- Credits on exit: `main` after Game_Run returns (platform.md §2.1 item 11): text mode printf.
- `Sound_StopAll` at Mission_Setup start, WeaponSelect start and after the frame loop.

---------------------------------------------------------------------------------------------------

## 12. Bugs and quirks (decision list)

| # | Where | Behaviour | Recommendation |
|---|---|---|---|
| Q1/Q2 | MainMenu | load-game zone bookkeeping | keep |
| Q3 | AeroOptions | players cycle 2..8 then 1 | keep (1-player Aerolimits works) |
| Q4 | SaveGame | F10 → "js_save.00:" invalid, write crashes | PORT: use "js_save.010" (or refuse) |
| Q5 | SaveGame_LoadMenu | no fclose | fix silently |
| Q6 | Mission_LoadBriefing | g_FogPending never cleared: after the first fog warning every later day mission is foggy | keep (faithful) — or fix with PORT note; user decides |
| Q7 | PlaneSelect | TurnL page-back waits on TurnR | keep |
| Q9 | WeaponSelect | stock preview sign differs from the load | keep (display only) |
| Q10 | WeaponSelect_DrawCounts | rack-1 weight divided by rack-0 per-rack | keep or fix (PORT note) |
| Q11 | WeaponSelect_DrawInfo | DrawInfoBox called without argument | port: pass 0 |
| Q12 | Mission_ResetState | type-10 weapon weight assigned, stock by uncapped count | keep |
| Q13 | Debrief/briefing wrap | strncpy without NUL in Debrief | port: terminate (identical output when lines shrink? verify) |
| Q14 | Debrief | Aerolimits shows "PLAYER 1" twice | fix with PORT note (user decides) |
| Q15 | Debrief | Aerolimits slot 0 multiplier 0 | keep |
| Q17 | Game_Run | dead palette clear loop | keep (no-op) |
| Q18 | EndGame_Screen | training final score overwritten by the credits picture | keep |
| Q19 | LoadBriefing | "Press f1..f10 to save" also zeroes 0x90348 (ground units) | keep |
| Q20 | Frame loop | stubs FUN_110e1/FUN_11105/FUN_1112a/FUN_11012 consume Rand every frame | keep the Rand calls |
| Q21 | Frame loop | alien abduction trigger at camY<0 sets 0x909c0 = 0 (no-op), Rand(400000) consumed | keep |
| Q22 | Frame loop | float speed compared as int bits (negative values would misbehave; speed is clamped ≥ 0) | keep exactly |
| Q23 | Aerolimits | Aero first round always record 0 | keep |
| Q24 | Frame loop | busy waits (menu fire release, AnyKey, pause, GetFKey) | port: pump events |

---------------------------------------------------------------------------------------------------

## 13. Open questions

1. Debrief/score text y (0x190..0x1b2): confirm with the video spec whether `Text_DrawSmall` y is page-row
   based (then the bonus lines land in the HUD panel region below the split line, which is visible).
2. `__FDI4`/`__FSI4` rounding mode: assumed truncation (C cast); confirm in the runtime (`0x494c8`, `0x4a220`).
3. Story_ShowAsc timeout: `g_WaitCountInit = -500` → 1000 retraces assumed (platform §4.3 shows `<= 499` / `< 500`).
4. Map_SetTile argument order in Mission_LoadBriefing step 11 (Aerolimits pylons) — as pushed; the map spec
   should confirm (col,row,tile).
5. AeroOptions `Wrap(players+1, 1, 8)` exact result for v = 9 depends on the Wrap body (platform/video).
6. Meaning of stat 77 (0x9045c): "bravery" bonus multiplier minus 1 *and* "no life lost" flag (§8.5) and
   the burning-wreck particle count — same global, so likely a "cheap/expendable aircraft" flag; check MISC.Z.
7. 0x90a10 (debrief success category "poor") is cleared in LoadBriefing; who sets it (objectives spec)?
```

> **Correction (player spec):** §8.3 flight physics has errors (drift is 4*Sign re-centring on 0xa0; catapult limit 3.0 above speed 6.0; thrust from w83 0x90674). `port/spec/player.md` §3.2 is authoritative.

## Corrections (phase 5 step A, checked against the disassembly while porting)

1. **Extensions are written over the last 4 characters, not appended** (§5.1, §5.2, §6.1 / Plane_SetupSprites):
   `*(u32 *)(name + strlen(name) - 4) = ".pax"` etc. The first token of a story file is `COMBATCOMBI.ABK` ->
   `COMBATCOMBI.pax` (DOS opens `GFX/COMBATCO.PAX`); the MISC plane base name `Hawk.Abk` -> `plane/Hawk.hd` and
   `Hawk.spx` (Plane_LoadSpx 0x264a2: `lea edi, [eax+0x85b44]; movsd; movsb`); the record's tileset name gets
   `.tlx` over its own extension. With `strcat` every story picture would fail ("...ABK.pax not found").
2. §3.1 step 4: `0x849c0` is 0 in the image, so `0x849e0` stays 0 and **Fonts_Load runs on every MainMenu
   entry** (two `File_LoadWhole(.., -1)` allocations leaked each time), not only the first time.
3. §3.1 step 9 (GENDATA.DAX): `0x92980` is a **u16** table (`[i + 3*j]`, word stores at 0x47359); the two
   26-entry tables at `0x8f088` (g_ParamNotObjective) and `0x8f08a` are **u16 with a 4-byte stride**
   (`shl edx,2; mov word [edx+0x8f088]`), not stride 8.
4. §6 PlaneSelect inner loop: the vertical move uses `g_MenuDY` (0x9077c) itself as the temporary:
   `g_MenuDY = cy + (mode != 3) * g_MenuDY * 0x20; cy = (mode & 1) ? 0x40 : 0x100; if (g_MenuDY < cy) cy = g_MenuDY;`
   and the x clamp has minimum 0x26, so the start column 0x24 becomes 0x26 after the first poll. The arrow
   highlight saves `Video_CopyRect(0, 0xe4,0xd4, 0xe4+W,0xd4+H, 0, 0,0xf0)` (W/H = Sprite_GetWidth/Height(0x1cb)).
5. §6.2 SelectScreen_DrawInfoBox: a line is also taken whole when `Text_WidthSmall(g_InfoText) <= g_InfoBoxW - 6`;
   in Aerolimits the lines are drawn at x **0x5a** (the colour variable), not 0x53; `Video_FillRect` gets colour 0
   (0x53/0x5a is pushed as an extra, unused 6th argument).
6. §10.1 EndGame_Screen (crash branch): the text starts at absolute VRAM row 12 but the display starts at row 20
   (`Video_SetStartAndPan(0,0x14,0)`), so the first line - i.e. every shipped sarcasm text, all of which fit on one
   line - is above the visible window: the end-game pictures show no text. Original behaviour, kept.
7. §3.4 AeroOptions_Menu has no release wait: a direction held for more than one 15 Hz poll moves the cursor
   again (a 0.2 s tap moves it two rows).
8. §8.2 step names (phase 5 step B): step 29 is `Flamer_Update` (0x90608), `EnemyMissiles_Update` (0x906fc),
   `EnemyGround_Update` (0x90348); step 32 is `AirbaseCrew_Update` (not balloons); step 33 `Weapons_FrameDispensers`;
   step 34 `FUN_000164d2` (alien); step 37 `Bonus_Update`, `Pickup_Update`, `BaseRepair_Update`; step 38
   `Runway_SetEndTargets`; step 40 objects 0x84 = SAM_Draw/SAM_Fire, 0x85 = Gun_Draw/Gun_Fire (also needs
   `g_ShellCount < 10 && (mode & 1) == 0`), 0x83 = Flak_Draw/Flak_Fire; step 41 `EnemyPilots_Update` (0x90754),
   `Commandos_Update` (0x902e0); step 43 `Projectiles_Update` (g_ProjCount != 0); step 51 `EnemyShells_Update`
   (g_ShellCount 0x8ffa0) before Bullets_Update.
9. §8.2 step 45 also **starts the support aircraft** (0x1db4c..): if 0x9037c (B52 target) != 0 and no B52 yet:
   `0x90998 = 1, 0x90970 = 0x9095c = 0, 0x90978 = 8, 0x90984 = 0x3d34, 0x90988 = -0x800`, then Rand(9), Rand(5);
   if 0x90398 (drop zone) != 0 and no Fat Albert: `0x90528 = 1` + its start state, Rand(9), Rand(5); and
   **every frame with no tanker, no enemy aircraft and p26 == 0 it starts the tanker** (`0x8ffb8 = 1`, x =
   g_BaseEndX, y = 0x3cb - g_BaseYOff, door 4). Then: 0x905b0 (gun smoke) -> Rand(3); wing vapour (0x8ff2c, no
   fire, not crashed) -> Rand(3) and clears it; rocket boost particle; 0x907b0 contrail -> Rand(3); landed on
   water (0x90034 == 0x82): 4 x {Rand(3), Rand(3), Rand(8)}; first landed frame (0x90414 == 0): Sfx 7 if attr
   0x81 and not a heli, then 6 x {Rand(3), Rand(8), Rand(8)}; catapult: 6 x {Rand(3), Rand(8), Rand(8)};
   `0x90414 = g_OnGround`.
10. §7.2 WeaponSelect_Screen: the hovered zone is stored in **0x8fea8** (g_ZoneHit 0x8fea4 is the confirmed one);
   when Zone_HitTest returns 0 the cursor steps right (`Wrap(x+1,0,3)`) and reads a 4x8 table at 0x8dca0
   (`[x*8 + y]`); in the confirm branch the Wrap result is discarded (endless loop if the cell had no zone - every
   cell has one). The cell highlight is saved/restored with `Video_CopyRect(0, x, y, x+W, y+H, 0, 0, 0x104)` /
   `(0, 0, 0x104, W, 0x104+H, 0, x, y)` where its y uses **`0x90830 != 0`** while the drawn cursor uses
   `g_CursorY != 0` (kept). The weapon-confirm keys are `g_Fire || g_KeyDown[0x39] || g_KeyDown[0x1c]`; the outer
   test adds `g_FireOrConfirm || g_Ctrl[0] || g_Ctrl[1]`.
