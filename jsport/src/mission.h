#pragma once
/* The mission (game_flow.md §8, player.md, level.md, video.md §7.1, sound.md §5): globals shared by the frame
 * loop (frame.c), the player (player.c), the HUD (hud.c), the level objects (level.c) and the engine sound.
 * Everything lives in the data-segment image at the original address (dseg.h); names from player.md §2 /
 * level.md §1.4 / port/js_symbols.csv. Globals already named in game.h / video.h / platform.h are not
 * repeated (g_OnGround, g_CamX, g_Ctrl, ...). */
#include "dseg.h"
#include "game.h"

/* ---- player state (player.md §2.1) */
#define g_Speed            DF32(0x90094)        /* float 0..6 */
#define g_SpeedBits        DS32(0x90094)        /* the same float read as int (bit-pattern compares) */
#define g_Dir              DS32(0x90200)
#define g_DirHalf          DS32(0x901F0)
#define g_Throttle         DS32(0x8FFD4)
#define g_PlayerVX         DS32(0x90778)        /* = g_MenuDX */
#define g_PlayerVY         DS32(0x9077C)        /* = g_MenuDY */
#define g_TargetVY         DS32(0x90790)
#define g_VXError          DS32(0x8FF38)
#define g_LiftVY           DS32(0x8FF3C)
#define g_StallSink        DS32(0x90068)
#define g_StallLimit       DS32(0x90098)
#define g_StallTopY        DS32(0x900A8)
#define g_GearDown         DS32(0x8FF14)
#define g_GearLatch        DS32(0x8FF48)
#define g_HookDown         DS32(0x904FC)
#define g_FuelLeaks        DS32(0x90654)
#define g_EngineFire       DS32(0x900D8)
#define g_HeliVX           DS32(0x90550)
#define g_HeliLift         DS32(0x907E0)
#define g_BrakeDrag        DS32(0x90884)
#define g_ReverseThrust    DS32(0x90958)
#define g_RocketBoost      DS32(0x9085C)
#define g_RocketBoostTimer DS32(0x908A4)
#define g_CatapultTimer    DS32(0x90834)
#define g_CatapultCount    DS32(0x90828)
#define g_TaxiStopTimer    DS32(0x904B8)
#define g_Overloaded       DS32(0x902B4)
#define g_WheelsOk         DS32(0x90ABC)
#define g_OverRunway       DS32(0x9015C)
#define g_GroundAttr       DS32(0x90030)
#define g_HitAttr          DS32(0x8FFC4)
#define g_DeckAttr         DS32(0x90994)
#define g_AutoThrottle     DS32(0x9097C)        /* = g_TrainingMode of game.h */
#define g_GroundParked     DS32(0x90460)
#define g_UpHeld           DS32(0x90464)
#define g_DownHeld         DS32(0x904A0)
#define g_LastAction       DS32(0x9044C)
#define g_ActionThisFrame  DS32(0x90430)
#define g_KeyCode          DS32(0x90490)
#define g_DamageFlags      DS32A(0x8FA20)       /* [14] */
#define g_DamageLampsDirty DS32(0x90670)
#define g_DamageHits       DS32(0x907F8)
#define g_LastDamage       DS32(0x90540)
#define g_DmgLoop          DS32(0x8FEA4)        /* = g_ZoneHit of game.h */
#define g_EffThrottle      DS32(0x92B04)        /* = g_CDMissionTrack (shared temporary) */
#define g_SpeedTmp         DF32(0x92B08)
#define g_SpeedTmp2        DF32(0x92B10)
#define g_PlayerScrX       DS32(0x90214)
#define g_PlayerScrY       DS32(0x90218)
#define g_PlayerWX         DS32(0x90A18)        /* drawn world position (GF step 18) */
#define g_PlayerWY         DS32(0x90A1C)
#define g_PlayerScrXSave   DS32(0x90A50)
#define g_PlayerScrYSave   DS32(0x90A54)
#define g_PlayerSpriteId   DS32(0x90900)
#define g_IsGlider         DS32(0x90594)
#define g_GliderLift       DS32(0x90580)
#define g_FrameParity      DS32(0x905A0)
#define g_FrameCounter50   DS32(0x907EC)
#define g_EngineWarnCol    DS32(0x92B14)
#define g_EngineWarnDir    DS32(0x80178)
#define g_AimFrame         DS32(0x90774)
#define g_PrevDir          DS32(0x901C4)
#define g_PrevSpeed        DF32(0x90284)
#define g_SpeedDelta       DF32(0x90798)
#define g_RotateSpeed      DF32(0x8FE9C)
#define g_WingVapour       DS32(0x8FF2C)
#define g_FireHeldGearDown DS32(0x90438)
#define g_FireLatch        DS32(0x902D4)
#define g_LoopK            DS32(0x90830)        /* third shared loop counter (player.md Q5) */

/* ---- plane stats (player.md §2.3; copied by PlaneSelect_Screen) */
#define g_GearSilent       DS32(0x90060)        /* w62 */
#define g_NavLightBlink    DS32(0x90258)        /* w63 */
#define g_TwinEngine       DS32(0x90358)        /* w66 */
#define g_NeedsTow         DS32(0x9008C)        /* w67 */
#define g_BrakeStrength    DS32(0x90188)        /* w73 */
#define g_LandAttrLow      DS32(0x90450)        /* w74 */
#define g_EngineTickRate   DS32(0x908D0)        /* w78 */
#define g_StallSpeed       DF32(0x90090)        /* w80 */
#define g_Drag             DF32(0x90780)        /* w81 */
#define g_TopSpeed         DS32(0x903B0)        /* w82 */
#define g_Thrust           DF32(0x90674)        /* w83 */
#define g_MaxSinkAccel     DS32(0x907D8)        /* w84 */
#define g_WingAuthority    DS32(0x907DC)        /* w85 */
#define g_CruiseSpeed      DF32(0x907A0)        /* w86 */
#define g_EjectSeat        DS32(0x90954)        /* w87 */
#define g_TurnRate         DS32(0x90168)        /* w88 */
#define g_FixedGear        DS32(0x905DC)        /* w89 */
#define g_AirframeType     DS32(0x903D4)        /* w93 */
#define g_HasAfterburner   DS32(0x90A34)        /* w95 */
#define g_ReverseExhaustX  DS32(0x9090C)        /* w97 */
#define g_FlameLength      DS32(0x90A48)        /* w98 */
#define g_GearHeight       DS32(0x8FF0C)        /* w100 */
#define g_ArmourBase       DS32(0x91834)        /* w108 */
#define g_ProbeDX          DS32(0x901DC)        /* w109 */
#define g_ProbeDY          DS32(0x901E0)        /* w110 */
#define g_EngineKind       DS32(0x906D0)        /* w111 */
#define g_ParkAttitude     DS32(0x8FF20)        /* w113 */
#define g_Seaplane         DS32(0x90644)        /* w114 */
#define g_BurnerFrames     DS32(0x9006C)        /* w115 */
#define g_HookDX           DS32(0x8FFE4)        /* w116 */
#define g_HookDY           DS32(0x8FFE8)        /* w117 */
#define g_StallNoseDrop    DS32(0x90130)        /* w118 */
#define g_CrashBlast       DS32(0x90844)        /* w119 */

/* ---- flight tables (player.md §2.2), laid out contiguously as in the exe (Q3 reads past them) */
#define g_DirVX            DS32A(0x8E0D0)       /* e0d0[32] */
#define g_DirGunX          DS32A(0x8E150)       /* e150[32] */
#define g_DirLift          DS32A(0x8E210)       /* e210[32] */
#define g_DirGunY          DS32A(0x8E290)       /* e290[32] */
#define g_Dir16X           DS32A(0x8FE10)       /* fe10[16] */
#define g_Dir16Y           DS32A(0x8FE50)       /* fe50[16] */
#define g_MaxSpeedTab      ((float *)DSEG(0x863C8))   /* [10] */
#define g_PlayerDirSprite  DS32A(0x8FCB8)       /* 32 pairs (normal, gear down), heli at +16 pairs */
#define g_HeliMaxLeft      DS32A(0x8DEB8)
#define g_HeliMaxRight     DS32A(0x8E310)
#define g_HeliFrameBase    DS32A(0x80344)
#define g_HeliFrameKind    DS32A(0x80364)

/* ---- ejection / tow / views */
#define g_EjectScrX        DS32(0x90270)
#define g_EjectScrY        DS32(0x90274)
#define g_EjectCamX        DS32(0x9024C)
#define g_EjectCamY        DS32(0x90250)
#define g_ChuteVY          DS32(0x90968)
#define g_ChuteSwing       DS32(0x8FF28)
#define g_ChuteSwingDir    DS32(0x90794)
#define g_PilotX           DS32(0x90328)
#define g_ChuteFail        DS32(0x90298)
#define g_PendingWarnSfx   DS32(0x903FC)
#define g_WarnSfxDelay     DS32(0x90404)
#define g_TowX             DS32(0x8FF94)
#define g_TowY             DS32(0x8FF98)
#define g_TowVY            DS32(0x8FF78)
#define g_TowRope          DS32(0x8FF74)
#define g_TowTime          DS32(0x8FF90)
#define g_TowFrame         DS32(0x8FF88)
#define g_TowState         DS32(0x8FF8C)
#define g_ViewX            DS32(0x8FF68)
#define g_ViewY            DS32(0x8FF70)
#define g_ViewTarget       DS32(0x8FF10)
#define g_PrevCamX         DS32(0x90384)
#define g_PrevCamY         DS32(0x90374)
#define g_PickupX          DS32(0x909E8)

/* ---- level objects (level.md §1.4) */
#define g_MapWidthPx       DS32(0x903B4)
#define g_BaseYOff         DS32(0x901AC)
#define g_RunwayFill       DS32(0x905F0)
#define g_BaseIsCarrier    DS32(0x90818)
#define g_TrigCol          DS32(0x8FF18)
#define g_TrigCamCol       DS32(0x904DC)
#define g_TrigCamRow       DS32(0x904E0)
#define g_TrigClass        DS32(0x905BC)
#define g_TrigOff          DS32(0x8FEE4)
#define g_TrigRow          DS32(0x8FEEC)
#define g_Scratch690       DS32(0x90690)
#define g_RadarJammed      DS32(0x9047C)
#define g_MarkerX          DS32(0x9089C)
#define g_MarkerY          DS32(0x908A0)
#define g_TargetMarkX      DS32(0x90574)
#define g_TargetMarkY      DS32(0x9052C)
#define g_LockTarget       DS32(0x90454)
#define g_LauncherCol      DS32(0x902C0)
#define g_CrateX           DS32(0x90948)
#define g_CrateY           DS32(0x9094C)
#define g_CrateVY          DS32(0x9091C)
#define g_CrateSprite      DS32(0x90940)
#define g_AeroGatePostHit  DS32(0x90570)
#define g_BerthaCol        DS32(0x908E8)
#define g_BerthaRow        DS32(0x908EC)
#define g_BerthaW          DS32(0x908E4)
#define g_BerthaH          DS32(0x908DC)
#define g_BerthaDelay      DS32(0x908C8)
#define g_BerthaShellTimer DS32(0x908D8)
#define g_BerthaShellX     DS32(0x908F4)
#define g_StrafeCount      DS32(0x90738)
#define g_SmokeX           DS32A(0x8EC48)
#define g_SmokeY           DS32A(0x8EC60)
#define g_SmokeT           DS32A(0x8EC30)
#define g_SmokeCount       DS32(0x90100)
#define g_SecExpX          DS32A(0x8DFD0)
#define g_SecExpY          DS32A(0x8E050)
#define g_SecExpDmg        DS32A(0x8DF50)
#define g_SecExpCount      DS32(0x902FC)
#define g_ColArmourSum     DS32(0x933B0)
#define g_TargetsDone      DS32(0x90504)
#define g_AeroPadScore     DS32(0x903B8)
#define g_TargetTilesInit  DS32(0x902F4)
#define g_EscortMode       DS32(0x905C0)
#define g_EscortNeeded     DS32(0x909D8)
#define g_ParamNotObjective D16A(0x8F088)      /* u16, [i*2 + (mode == 3)] (game_flow Correction 3) */
#define g_MP_ReconCol      D16(0x9164A)        /* p01 */
#define g_MP_ReconRow      D16(0x9164C)        /* p02 */
#define g_MP_CeilingRow    D16(0x91670)        /* p20 */
#define g_MP_TargetMarker  D16(0x91672)        /* p21 */
#define g_KillTallyValue   DS32A(0x90EB0)
#define g_ProjCount        DS32(0x908BC)
#define g_ProjX            DS32A(0x92830)
#define g_ProjY            DS32A(0x92884)
#define g_ProjVX           DS32A(0x928D8)
#define g_ProjVY           DS32A(0x9292C)
#define g_ProjBlastA       DS32A(0x92720)
#define g_ProjKind         DS32A(0x91384)
#define g_TankerType       DS32(0x8FFB8)
#define g_EnemyAirX        DS32A(0x9264C)
#define g_EnemyAirY        DS32A(0x92660)
#define g_PhotoCount       DS32(0x902CC)
#define g_GunAmmo          DS32(0x909C4)
#define g_GunTrigger       DS32(0x90CA8)        /* = g_Ctrl[4] */
#define g_LightningCooldown DS32(0x8FFB0)
#define g_BoltX            DS32(0x8FFD8)
#define g_BoltY            DS32(0x8FFDC)

/* ---- HUD (player.md §10) */
#define g_HudMsgs          DSTR(0x8D3F0)       /* 10 x 100 */
#define g_HudMsgTime       DS32A(0x8031C)
#define g_DamageMsgs       DSTR(0x85C48)       /* stride 0x50 */

/* ---- engine sound (sound.md §1.5) */
#define g_EngineRev        DS32(0x90698)
#define g_EngineRevTarget  DS32(0x9066C)
#define g_SfxBusyTimer     DS32(0x900F0)
#define g_EngineSfxRequest DS32(0x900A4)
#define g_EngineSfxLastState DS32(0x907C8)

/* ---- player.c (player.md) */
void Player_Update(void);                /* 0x2d34e */
void Player_RotateA(void);               /* 0x323b9 */
void Player_RotateB(void);               /* 0x32476 */
void Player_PitchUp(void);               /* 0x324ed */
void Player_ThrottleUp(void);            /* 0x32585 */
void Player_PitchDown(void);             /* 0x32635 */
void Player_ThrottleDown(void);          /* 0x326d3 */
void Player_PullUp(void);                /* 0x16620 */
void Player_FullPower(void);             /* 0x15cb3 */
void Player_TakeoffAssist(void);         /* 0x1693b */
void Player_GearCollapseRoll(void);      /* 0x16d70 */
void Player_Ditching(void);              /* 0x15bb6 */
void Player_AutoThrottle(void);          /* 0x152a0 */
void Player_GliderUpdate(void);          /* 0x403c1 */
void Player_EngineFire(void);            /* 0x16b82 */
void Player_DamageSystems(void);         /* 0x3f2fe */
void Player_EjectUpdate(void);           /* 0x3c600 */
void Eject_SetPilotX(void);              /* 0x3b6e6 */
int  Player_DrawHeli(int x, int y, int heli, int dx, int f, int gear, int rotorAnim);   /* 0x29b29 */
void Player_AfterburnerFlame(void);      /* 0x156a2 */
void Player_DrawReverseThrust(void);     /* 0x1554b */
void Debris_Update(void);                /* 0x3b27c */
void Debris_Spawn(void);                 /* 0x3b5c0 */
void Player_BuildPlaneTables(int intAngles16);   /* inlined table loops (Mission_Setup, Player_Update) */
void Player_BuildHeliTables(void);       /* inlined */
void Player_BuildSpeedCaps(void);        /* inlined (0x863c8) */

/* ---- hud.c (player.md §10) */
void Hud_DrawPanel(void);                /* 0x25c8b */
void Hud_DrawRadarLine(int dx0, int y0, int wx1, int wx2, int col);   /* 0x26174 */
void Hud_DrawTargetArrow(int camX, int camY, int tx, int ty);         /* 0x267da */
void Hud_PushMessage(const char *s);     /* 0x26e03 */
void Hud_DrawMessages(void);             /* 0x26e5a */
void Hud_DrawBriefing(void);             /* 0x26b27 */
void Hud_UpdateRadar(void);              /* 0x2ec74 */
void Hud_LampBlinkA(void);               /* 0x3f727 */
void Hud_LampBlinkB(void);               /* 0x3f777 */
void Hud_Nop(const char *s);             /* 0x26cc6 */
void Hud_PlaneMessage(void);             /* 0x26ce1 */
void Hud_WeaponMessage(void);            /* 0x26d24 */

/* ---- level.c (level.md, mission part) */
void Tileset_Load(char *name);           /* 0x25c13 */
void Tileset_LoadTlx(char *name);        /* 0x133b3 */
void Parallax_Load(char *name);          /* 0x1351e */
int  IsOnScreen(int camX, int camY, int x, int y);      /* 0x10eb0 */
int  BoxOverlap(int x1, int y1, int x2, int y2, int w, int h);   /* 0x1172a */
void Map_TriggerColumn(void);            /* 0x3e1a3 */
void Map_CraterAt(int px, int py, int dmg);             /* 0x3b777 */
int  Map_DamageColumn(int x, int y, int tile, int mode, int uninit);   /* 0x3f195 (uninit: PORT, level.md §4.1) */
void Map_SecondaryExplosions(void);      /* 0x14abe */
void Mission_CheckComplete(void);        /* 0x3f02e */
void Mission_CheckObjectives(void);      /* 0x44a5d */
void Carrier_Update(void);               /* 0x15957 */
void Bertha_Update(void);                /* 0x1af0f */
void AgentDrop_Update(void);             /* 0x15387 */
void AgentDrop_Release(void);            /* 0x17be5 */
void Crate_Update(void);                 /* 0x4596f */
void Waypoint_Update(void);              /* 0x45584 */
void Runway_Update(void);                /* 0x15194 */
void BaseRadar_Update(void);             /* 0x16e38 */
void Runway_SetEndTargets(void);         /* 0x16dcd */
void Recon_PhotoCheck(void);             /* 0x400da (FUN_000400da) */
void Map_TriggerColumnAhead(void);       /* 0x3e66b */
int  Ray_Trace(int x, int y, int vx, int vy);   /* 0x4501e */
int  Ray_HitX(void);                     /* 0x450f8 */
int  Ray_HitY(void);                     /* 0x4511e */
void Level_DrawOverviewMap(int col, int row);   /* 0x14677 */
void Mission_CompleteScreen(void);       /* 0x4011f */

/* ---- weapons.c (weapons.md, enemies.md §10.1 / §14.1) */
void Particles_Clear(void);              /* 0x2653b */
void Particles_Nop(void);                /* 0x26520 */
void Particle_Spawn(int x, int y, int vx, int vy, int a5, int a6, int a7);   /* 0x26560 */
void Particles_Update(int camX, int camY);   /* 0x2661e */
void Bullets_Clear(void);                /* 0x2688f */
void Bullet_Add(int x8, int y8, int vx, int vy);   /* 0x268d9 */
void Bullets_Update(int camX, int camY); /* 0x2694b */
void Flare_Release(void);                /* 0x38a61 */
void Flares_Add(void);                   /* 0x346d3 */
void SupportAircraft_Flares(void);       /* flare block of 0x2780b */
void Weapon_Fire(void);                  /* 0x341e9 */
void Weapon_LaunchBallistic(void);       /* 0x3529c */
void Weapon_FireRocket(void);            /* 0x35532 */
void Weapon_FireGuided(void);            /* 0x356fe */
void Weapon_TakePhoto(void);             /* 0x348db */
void Weapon_DropTank(void);              /* 0x34f0d */
void Weapon_DropCommando(void);          /* 0x351e9 */
void Weapon_ArmJP233(void);              /* 0x34e13 */
void Weapon_ArmPorcupine(void);          /* 0x34dd0 */
void Weapon_FireGunPod(void);            /* 0x346f9 */
void Weapon_DispenseJP233(void);         /* 0x3494e */
void Weapon_DispensePorcupine(void);     /* 0x34b94 */
void Weapons_FrameDispensers(void);      /* 0x15ce6 */
void Player_Weapons(void);               /* 0x32846 */
void Flamer_Update(void);                /* 0x17c56 */
void Projectiles_Update(void);           /* 0x415e4 */
void Explosion_Damage(int x, int y, int vx, int vy, int a5, int a6);   /* 0x39b69 */

/* ---- sound.c (sound.md §5) */
void Engine_Sfx(void);                   /* 0x391e3 */
void Engine_SoundUpdate(void);           /* 0x39388 */
void Sfx_WarningTone(void);              /* 0x3f945 */
void Sfx_MissionEvent(void);             /* 0x14923 */
void Sfx_LaunchThump(void);              /* 0x3978d */

/* ---- frame.c (game_flow.md §8) */
void Mission_Run(void);                  /* the frame loop of Game_Run 0x1cc2f..0x20db3 */
void Fog_Refresh(void);                  /* 0x1adc9 (FUN_0001adc9) */

/* ---- game_flow.c */
void WeaponSelect_Screen(void);          /* 0x29d90 */
void WeaponSelect_DrawCounts(void);      /* 0x2ba3c */
void WeaponSelect_DrawInfo(void);        /* 0x2bb33 */

/* ---- stubs.c: subsystems of later steps (weapons, projectiles, enemies, particles, support aircraft).
 * Each is called at its original place; the comments in stubs.c list the Rand calls they will make. */
void TargetVehicle_Spawn(void);
void TargetVehicles_Update(void);
void Tanker_Update(void);                /* 0x27008 */
void SupportAircraft_Update(void);       /* 0x2780b */
void EnemyMissiles_Update(void);
void EnemyGround_Update(void);
void EnemyAir_Update(void);              /* 0x36ae3 */
void Building_Update(void);              /* 0x149ae */
void Convoy_Update(void);
void AirbaseCrew_Update(void);
void Alien_Update(void);                 /* 0x164d2 (FUN_000164d2) */
void Bonus_Update(void);
void Pickup_Update(void);
void BaseRepair_Update(void);
void Airbase_Update(void);               /* 0x2bc0b */
void SAM_Draw(void);
void SAM_Fire(void);
void Gun_Draw(void);
void Gun_Fire(void);
void Flak_Draw(void);
void Flak_Fire(void);
void EnemyPilots_Update(void);
void Commandos_Update(void);
void EnemyShells_Update(void);
void BaseHit_Losses(void);
void Bonus_Spawn(void);

