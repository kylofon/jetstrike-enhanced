/* TODO(phase 5, later steps): subsystems the mission frame loop calls that are not ported yet (weapons.md,
 * enemies.md, the particle system, the support aircraft of player.md §6). Each stub sits at its original call
 * site in the frame loop (frame.c) so the order of everything around it is right.
 *
 * RNG: the original functions consume Rand() calls; until they are ported the random sequence of a mission
 * diverges from the original as soon as one of them would have run. "Rand: n" below is the number of Rand
 * call sites in the function body (js.c), "via" lists callees that also call Rand. */
#include "mission.h"

/* TargetVehicle_Spawn (attempt start, campaign with a target column, up to 10 calls). Rand: 9 per call. */
void TargetVehicle_Spawn(void) {}
/* TargetVehicles_Update (GF step 31, g_TVCount > 0). Rand: 4; via TV_FireShell (2), TV_LaunchSAM (1). */
void TargetVehicles_Update(void) {}
/* 0x27008 Tanker_Update (GF step 27, g_TankerType != 0 - set every frame when no enemy aircraft and no
 * enemy base, see frame.c step 45). Rand: 2 (Particle_Spawn arguments of the wreck). */
void Tanker_Update(void) {}
/* 0x2780b SupportAircraft_Update (GF step 28: tow plane, Fat Albert, B52, ship, campaign bomber, player
 * flares, ground pickup). Rand: 17; via Flare_Release (1), Explosion_Damage. Only the flare block (0x28e6d,
 * weapons.md §7.2, Rand(1) x 1..2 per flare) is ported (weapons.c); it sits after the big-bomber part and before
 * the pickup part of the original, whose Rand calls are still missing. */
void SupportAircraft_Update(void)
{
    /* TODO(support aircraft): tow plane, Fat Albert, B52, ship, big enemy bomber (incl. its missile launch, which
     * calls Flare_Release), then: */
    SupportAircraft_Flares();
    /* TODO: ground pickup */
}
/* EnemyMissiles_Update (GF step 29, g_EnemyMslCount). Rand: 4; via Player_DamageSystems, Explosion_Damage. */
void EnemyMissiles_Update(void) {}
/* EnemyGround_Update (GF step 29, g_EnemyGroundCount). Rand: 7; via Explosion_Damage. */
void EnemyGround_Update(void) {}
/* 0x36ae3 EnemyAir_Update (GF step 30 when g_EnemyAirCount != 0). Rand: 31; via Sfx_RandomAmbient (2),
 * Flare_Release (1), Bonus_Spawn (1), Player_DamageSystems, Explosion_Damage. */
void EnemyAir_Update(void) {}
/* 0x149ae Building_Update (GF step 31, g_LauncherCol). Rand: 0. */
void Building_Update(void) {}
/* Convoy_Update (GF step 31, g_ConvoyCount). Rand: 4; via Convoy_FireShell (4), Explosion_Damage. */
void Convoy_Update(void) {}
/* AirbaseCrew_Update (GF step 32: near the base and low). Rand: 1 - runs every frame at the airbase, so the
 * RNG sequence diverges from the start of every mission until it is ported. */
void AirbaseCrew_Update(void) {}
/* 0x164d2 FUN_000164d2 alien abduction (GF step 34, g_AlienAbduct > 0; never started, Q21). Rand: 0. */
void Alien_Update(void) {}
/* Bonus_Update (GF step 37, g_BonusType). Rand: 0. */
void Bonus_Update(void) {}
/* Pickup_Update (GF step 37, 0x90918 > 0: prize balloon / Aerolimits pickup). Rand: 0. */
void Pickup_Update(void) {}
/* BaseRepair_Update (GF step 37, 0x90498). Rand: 0. */
void BaseRepair_Update(void) {}
/* 0x2bc0b Airbase_Update (GF step 39: refuel/rearm at the hangar -> WeaponSelect_Screen). Rand: 0; via
 * FireEngine_Spray (2). PORT (debug): frame.c opens WeaponSelect_Screen with F12 while parked instead. */
void Airbase_Update(void) {}
/* MP2 object 0x84 (GF step 40): SAM_Draw (Rand: 0), SAM_Fire (Rand: 1). */
void SAM_Draw(void) {}
void SAM_Fire(void) {}
/* MP2 object 0x85: Gun_Draw (Rand: 0), Gun_Fire (Rand: 3). */
void Gun_Draw(void) {}
void Gun_Fire(void) {}
/* MP2 object 0x83: Flak_Draw (Rand: 0), Flak_Fire (Rand: 6; via Player_DamageSystems, Explosion_Damage). */
void Flak_Draw(void) {}
void Flak_Fire(void) {}
/* EnemyPilots_Update (GF step 41, 0x90754). Rand: 1. */
void EnemyPilots_Update(void) {}
/* Commandos_Update (GF step 41, 0x902e0). Rand: 0; via Explosion_Damage. */
void Commandos_Update(void) {}
/* EnemyShells_Update (GF step 51, g_ShellCount). Rand: 0; via Player_DamageSystems, Explosion_Damage. */
void EnemyShells_Update(void) {}
/* BaseHit_Losses (GF step 57, 0x901c8: runway cratered). Rand: 4. */
void BaseHit_Losses(void) {}
/* Bonus_Spawn (GF step 75 and auto-eject bonus). Rand: 1. */
void Bonus_Spawn(void) {}
