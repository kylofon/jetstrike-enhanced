#pragma once
/* Game flow (port/spec/game_flow.md): master loop, menus, briefing, plane select, debrief, end game,
 * save/load. Globals live in the data-segment image at their original addresses (dseg.h); unnamed ones are
 * accessed in place as DS32(0x9xxxx) with the spec's address. */
#include "dseg.h"

/* ---- modes / progression (game_flow.md §1) */
#define g_GameMode          DS32(0x8FF7C)
#define g_Mission           DS32(0x903E0)
#define g_Lives             DS32(0x902F0)
#define g_AbortFlag         DS32(0x90800)
#define g_MissionResult     DS32(0x903E8)
#define g_MissionActive     DS32(0x90154)
#define g_BriefedMission    DS32(0x903F4)
#define g_QuitGame          DS32(0x80174)
#define g_MenuRepeat        DS32(0x903EC)
#define g_MenuDone          DS32(0x9030C)
#define g_MenuChoice        DS32(0x90184)
#define g_StartMission      DS32(0x903A4)
#define g_TrainingPicked    DS32(0x9021C)
#define g_TrainingMode      DS32(0x9097C)       /* g_AutoThrottle in the symbol table */
#define g_PracticeEjects    DS32(0x9007C)
#define g_AutoEjectOn       DS32(0x90078)
#define g_FinalMission      DS32(0x90400)
#define g_CDCheckDone       D16(0x8078E)
#define g_SoundInitDone     D16(0x8078C)
/* Aerolimits */
#define g_AeroPlayer        DS32(0x90254)
#define g_AeroPlayers       DS32(0x902C8)
#define g_AeroRoundsSel     DS32(0x9033C)
#define g_AeroRound         DS32(0x903CC)
#define g_AeroMissionMask   D32(0x90390)
#define g_AeroUsedRacks     DS32A(0x8C3B8)
#define g_AeroPlaneSlot     DS32(0x90A20)
#define g_AeroRoundsOptions DS32A(0x8D9F8)
#define g_AeroGateX         DS32A(0x91484)
#define g_AeroGateY         DS32A(0x91498)
#define g_AeroGateIdx       DS32(0x90334)
/* score */
#define g_Score             DS32A(0x8E330)
#define g_Kills             DS32(0x904A8)
#define g_ArmourBonus       DS32(0x909E0)
#define g_NextBonusScore    DS32(0x902D8)
#define g_BonusScoreStep    DS32(0x902E4)
#define g_ExtraAircraftScore DS32(0x90304)
#define g_MissionBonus      DS32(0x90408)
#define g_RocketCount       DS32(0x90314)
#define g_GunPowerUp        DS32(0x90544)
#define g_GunPowerUp2       DS32(0x90944)
/* aircraft */
#define g_PlaneSel          DS32(0x90A58)
#define g_PlaneUsed         DS32A(0x90ECC)      /* [71] */
#define g_PlaneLimit        DS32A(0x9152C)      /* [60] */
#define g_PlaneFlown        DS32A(0x91140)      /* [40] */
#define g_AlienPlaneIdx     DS32(0x90A04)
#define g_AlienAbduct       DS32(0x909C0)
#define g_PlaneStats        DS32A(0x91684)      /* [121] MISC.Z record */
#define g_PlaneCount        DS32(0x90330)
#define g_PlaneNames        DSTR(0x86CB0)       /* stride 0x28 */
#define g_PlaneCand         DS32(0x9002C)
#define g_SelPage           DS32(0x8FED8)
#define g_SelPageSize       DS32(0x90224)
#define g_CursorX           DS32(0x907D0)
#define g_CursorY           DS32(0x907D4)
#define g_ZoneHit           DS32(0x8FEA4)
#define g_InfoBoxW          DS32(0x90058)
#define g_InfoText          DSTR(0x84C48)
#define g_PlaneDesc         DSTR(0x8C4D8)
#define g_IsHeli            DS32(0x904CC)
#define g_PlaneClass        DS32(0x9045C)       /* stat 77 */
#define g_Armour            DS32(0x909D0)
/* weapons */
#define g_WeaponCount       DS32(0x902E8)
#define g_WeaponNames       DSTR(0x8C8D8)       /* stride 0x28 */
#define g_WeaponDesc        DSTR(0x877C8)       /* stride 0xa0 */
#define g_WeaponType        DS32A(0x8F168)      /* [i*6+k] */
#define g_WeaponBlastA      DS32A(0x921F4)
#define g_WeaponBlastB      DS32A(0x91CF4)
#define g_WeaponThrust      DS32A(0x920D8)
#define g_WeaponIcon        DS32A(0x92310)
#define g_WeaponPerRack     DS32A(0x91FBC)
#define g_WeaponWeight      DS32A(0x9242C)
#define g_WeaponRackMult    DS32A(0x91E10)
#define g_WeaponStock       DS32A(0x91B84)
#define g_WeaponResupply    DS32A(0x91A68)
#define g_WeaponResupplyFrac DS32A(0x9194C)
#define g_RackWeapon        DS32A(0x8FBC8)      /* [2] */
#define g_RackRounds        DS32A(0x8FCB0)      /* [2] (g_RackCount) */
#define g_RackMaxLoad       DS32A(0x8FBC0)
#define g_RackPoints        DS32A(0x8FC10)
#define g_HardpointLoad     DS32(0x90500)
#define g_RackSel           DS32(0x90338)       /* g_FireRack */
#define g_LoadWeight        DS32(0x8FEB8)
#define g_MaxLoadWeight     DS32(0x904EC)
#define g_PlaneFuel         DS32(0x90388)
#define g_FuelBase          DS32(0x90378)
#define g_Fuel              DS32(0x905B8)
/* mission record */
#define g_MissionParams     D16A(0x91648)       /* [30] host order after the swap */
#define g_MP_TargetX0       D16(0x9164E)
#define g_MP_TargetX1       D16(0x91650)
#define g_MP_AirKills       D16(0x91652)
#define g_MP_ConvoyKills    D16(0x91654)
#define g_MP_TargetsReq     D16(0x91656)
#define g_MP_EnemyAir       D16(0x91658)
#define g_MP_ConvoyCount    D16(0x9165A)
#define g_MP_ConvoyCol      D16(0x9165C)
#define g_MP_PickupCol      D16(0x91664)
#define g_MP_ConvoyTypesA   D16A(0x91666)       /* read with stride 2 words */
#define g_MP_ConvoyTypesB   D16A(0x91668)
#define g_MP_Bertha         D16(0x9166E)
#define g_MP_PickupSprite   D16(0x91678)
#define g_MP_Weather        D16(0x9167A)
#define g_MP_EnemyBaseCol   D16(0x9167C)
#define g_MP_EnemyBaseRow   D16(0x9167E)
#define g_MP_Misc29         D16(0x91682)
#define g_MissionDefWeapons DS32A(0x8E378)      /* [4]; read up to [19] by Aerolimits PlaneSelect */
#define g_BriefingText      DSTR(0x92992)
#define g_MapName           DSTR(0x85148)
#define g_MapLoaded         DSTR(0x80074)
#define g_TilesetName       DSTR(0x84B48)
#define g_TilesetPending    DSTR(0x84E48)
#define g_MapVariant        DS32(0x8FFFC)
#define g_StoryName         DSTR(0x85648)
#define g_StoryShown        DS32(0x92B1C)
#define g_FogPending        DS32(0x92B18)       /* g_FogSticky */
#define g_FogActive         DS32(0x80070)
#define g_SpecialHit        DS32(0x90768)
#define g_EnemySetIndex     DS32(0x906D4)
#define g_EnemySetLoaded    DS32(0x90700)
#define g_EnemySpxLetter    DS32(0x906E4)
#define g_EnemySpxLoaded    DS32(0x906BC)
#define g_EnemyBaseX        DS32(0x906F4)
#define g_EnemyBaseRow      DS32(0x9070C)
#define g_EnemyAirCount     DS32(0x90708)
#define g_EnemyGroundCount  DS32(0x90348)
#define g_HudMsgCount       DS32(0x904D0)
#define g_PickupY           DS32(0x909EC)
#define g_AgentDropPending  DS32(0x90280)
#define g_BaseStartX        DS32(0x90180)
#define g_BaseEndX          DS32(0x901A8)
#define g_EndGameIndex      DS32(0x907F4)
#define g_DebriefText       DSTR(0x84F48)
#define g_SarcasmLines      DSTR(0x863F0)       /* 28 x 0x50 */
#define g_HudText           DSTR(0x8A428)       /* stride 0x50 */
#define HUDTEXT(n)          (g_HudText + (n) * 0x50)
#define g_CDEndgameLatch    D32(0x936F4)
/* end-of-mission state (owned by the frame loop, read by the debrief) */
#define g_OnGround          DS32(0x90440)
#define g_EjectState        DS32(0x9099C)
#define g_Crashed           DS32(0x907FC)
#define g_DeathTimer        DS32(0x90824)
#define g_CrashAttr         DS32(0x908B8)
#define g_CrashTileAttr     DS32(0x90908)
#define g_CrashDir          DS32(0x908B4)
/* scratch strings of the original */
#define s_Tmp85048          DSTR(0x85048)
#define s_Line85248         DSTR(0x85248)
#define s_Num85548          DSTR(0x85548)
#define s_Tmp85B48          DSTR(0x85B48)

/* ---- buffers (C pointers, see dseg.h) */
extern u8 *g_MiscData;           /* 0x90324: DATA/MISC, 0xdc bytes per plane */
extern u8 *g_MiscZData;          /* 0x9032C: DATA/MISC.Z, 300 bytes per plane */
extern u8 *g_HeliHD;             /* 0x84A34 */

/* ---- functions */
void Game_Run(void);                     /* 0x1ba0c */
void MainMenu(void);                     /* 0x469e7 */
void MainMenu_Draw(void);                /* 0x47ec2 */
void AeroOptions_Menu(void);             /* 0x48111 */
void Zone_Add(int id, int x1, int y1, int x2, int y2);   /* 0x11d02 */
int  Zone_HitTest(int x, int y);         /* 0x11dbc */
void Zone_Clear(void);                   /* 0x11e64 */
void SaveGame_LoadMenu(void);            /* 0x3c0fb */
void SaveGame_Write(char slot);          /* 0x3c367 */
void SaveGame_DrawLoadPrompt(void);      /* 0x3c56c */
void Mission_Setup(void);                /* 0x212ac */
void Weapons_ReturnUnused(void);         /* 0x22223 */
void Mission_ResetState(void);           /* 0x222f0 */
void Mission_LoadBriefing(void);         /* 0x2299a */
void Story_ShowAsc(void);                /* 0x23f6b */
int  PlaneSelect_Screen(void);           /* 0x245a9 */
void SelectScreen_LoadBg(int wep);       /* 0x257c3 */
void Text_DrawCenteredAt(int page, int x, int y, const char *s);   /* 0x25869 */
void SelectScreen_DrawInfoBox(int right);/* 0x258b6 */
void SelectScreen_ShowPlaneName(void);   /* 0x25a5d */
void PlaneRecord_Load(void);             /* 0x25add */
void Plane_SetupSprites(void);           /* 0x26256 */
void Plane_LoadSpx(void);                /* 0x26473 */
void Plane_ReplaceSprite(void);          /* 0x264eb */
void Mission_Debrief(void);              /* 0x2c9cb */
void Sarcasm_Load(void);                 /* 0x2d2b8 */
void EndGame_Screen(void);               /* 0x301b2 */
void TrainingCredits_Screen(void);       /* 0x304e0 */
void AeroScores_Screen(void);            /* 0x45352 */
void Stub_PlaneSelectOnce(void);         /* 0x1adae */
