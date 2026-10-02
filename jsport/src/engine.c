/* Engine and mission sounds: port/spec/sound.md §5 (Engine_Sfx, Engine_SoundUpdate, pitch functions, warning
 * sequence) and the small sound triggers of the frame loop (Sfx_MissionEvent, Sfx_LaunchThump). */
#include "mission.h"

#include <stdlib.h>

#include "platform.h"
#include "sound.h"
#include "video.h"

#define g_CrashTimer g_AlienAbduct              /* 0x909c0 (sound.md name) */

/* 0x391e3 Engine_Sfx: (re)starts the engine loop on channel 0 */
void Engine_Sfx(void)
{
    if (g_Crashed == 0 && g_CrashTimer < 1) {
        if (DS32(0x8FEF4) == 0 && g_IsGlider == 0) {
            DS32(0x90724) = (g_EngineKind % 10 == 3) ? g_EngineKind - 3 : g_EngineKind;
            u32 c = (u32)(DS32(0x90724) + 1);
            if (c < 2) {
                if (c == 1) Sfx_PlayVoice(0, 0x1c, 0x9c4, 0x10, 1);
            } else if (c < 3) {
                Sfx_PlayVoice(0, 0x1e, 0x9c4, 0x10, 1);
            } else if (c < 4) {
                Sfx_PlayVoice(0, 0x1f, 0x9c4, 0x10, 1);
            } else if (c == 0xc) {
                Sfx_PlayVoice(0, 0x1e, 0x9c4, 0x10, 1);
            }
        }
        g_EngineSfxRequest = 0;
    } else if (g_Crashed != 0) {
        Sound_SetVolume(0, 0);                   /* GUS only; on SB the engine keeps running */
    }
    if (g_CrashTimer > 0) {
        int v = g_CrashTimer < 0x40 ? g_CrashTimer : 0x3f;
        if (v < 0) v = 0;
        Sfx_PlayVoice(1, 1, g_CrashTimer * 0xf, v, 1);
    }
}

/* 0x397c5 Engine_DeathWobble */
static void Engine_DeathWobble(void)
{
    int v = Rand(1) + DS32(0x8FEF4);
    if (v > 0xc) v = 0xc;
    if (v < 4) v = 4;
    DS32(0x8FEF4) = v;
}

/* 0x39817 Engine_SetPitch (classes 0, 3) */
static void Engine_SetPitch(void)
{
    int e = g_EngineRev, x = e - 0x50 < 0 ? 0 : e - 0x50;
    if (g_EngineKind % 10 == 4) Sound_SetFreq(0, e * 0x32 + 5000 + x * 0x19);    /* dead (sound.md §5.4) */
    else Sound_SetFreq(0, e * 0x5a + 4000 + x * 0x19);
    double d = 16.0 + (double)e * 0.4;
    DS32(0x8FF40) = (s32)(d > 32.0 ? 32.0 : d);
}

/* 0x39976 Engine_Nop */
static void Engine_Nop(void) {}

/* 0x39991 Engine_Pitch1 (class 1) */
static void Engine_Pitch1(void)
{
    double d = 16.0 + (double)g_EngineRev * 0.2;
    DS32(0x8FF40) = (s32)(d > 48.0 ? 48.0 : d);
    int x = g_EngineRev - 0x50 < 0 ? 0 : g_EngineRev - 0x50;
    Sound_SetFreq(0, (x * 100 + g_EngineRev * 0x50 + 8000) / 2);
}

/* 0x39a94 Engine_Pitch2 (class 2) */
static void Engine_Pitch2(void)
{
    Sound_SetFreq(0, g_EngineRev * 0x28 + 6000);
    double d = 18.0 + (double)g_EngineRev * 0.2;
    DS32(0x8FF40) = (s32)(d > 32.0 ? 32.0 : d);
}

/* 0x3f945 Sfx_WarningTone */
void Sfx_WarningTone(void)
{
    Sfx_Play(10, 8000, 0x3f, g_CamX + g_PlayerScrX);
    g_WarnSfxDelay = 0x18;
    g_SfxBusyTimer = 0x18;
}

/* 0x39388 Engine_SoundUpdate (GF step 81) */
void Engine_SoundUpdate(void)
{
    if (g_SfxBusyTimer > 0) g_SfxBusyTimer--;
    s32 *spd = &DS32(0x90720);
    g_EngineRevTarget = *spd / 2 + g_Throttle * 10 + (DS32(0x907A8) > 0) * g_WingVapour * -5;
    int s = *spd + g_DirLift[g_DirHalf];
    int d = abs(*spd) < 0xb ? Sign(*spd) * abs(*spd) : Sign(*spd) * 10;
    if (g_DirLift[g_DirHalf] == 0) s += d;
    int lo = (int)(g_Speed * 10.0f + -30.0f);    /* __FSM, __FSA, __FSI4 */
    if (lo < 0) lo = 0;
    int hi = (int)(((float)g_DirLift[g_DirHalf] + g_Speed) * 10.0f);
    if (hi < 0) hi = 0;
    *spd = Clamp(s, lo, hi);
    if (g_IsHeli == 1) g_EngineRevTarget = g_Throttle * 10;
    if (g_EngineRev != g_EngineRevTarget) {
        int st = abs(g_EngineRevTarget - g_EngineRev) / 0xf;
        if (st < 1) st = 1;
        g_EngineRev += Sign(g_EngineRevTarget - g_EngineRev) * st;
    }
    if (g_FrameParity != 0) {
        if (DS32(0x8FEF4) == 0) {
            if (g_CrashTimer < 1) {
                switch (g_EngineKind % 10) {
                case 0: Engine_SetPitch(); break;
                case 1: Engine_Pitch1(); break;
                case 2: Engine_Pitch2(); break;
                case 3: Engine_SetPitch(); break;
                default: break;
                }
            } else {
                Engine_Nop();
            }
        } else {
            Engine_DeathWobble();
        }
    }
    if (g_PendingWarnSfx > 0) {
        if (g_WarnSfxDelay == 0) {
            Sfx_WarningTone();
        } else if (--g_WarnSfxDelay == 0) {
            Sfx_Play(g_PendingWarnSfx, 8000, 0x3f, g_CamX + g_PlayerScrX);
            g_SfxBusyTimer = 0;
            g_PendingWarnSfx = 0;
        }
    }
    if (g_Crashed > 0) {                         /* the wreck tumbles (non-sound part, js.c 20702) */
        if (g_PlayerVX != 0) {
            if (Rand(3) == 3 && g_PlayerScrY > 0x9c) {
                g_PlayerVY = -Rand(abs(g_PlayerVX));
                if (g_IsHeli == 0) {
                    int a = Rand(1);
                    int b = Rand(1);
                    DS32(0x90204) = a * 4 + b * -4;
                } else {
                    DS32(0x90204) = Rand(2) - 1;
                }
            }
            if (g_IsHeli == 0) {
                g_Dir = (g_Dir + 0x40 + DS32(0x90204)) % 0x40;
            } else {
                if (g_Dir > 0xd) g_Dir = 0xd;
                if (g_Dir < 0) g_Dir = 0;
                if (g_HeliLift > 0) g_HeliLift--;
            }
        }
        g_DeathTimer++;
    }
    if (g_CrashTimer > 0 || g_EngineSfxRequest == 1 || g_Crashed != g_EngineSfxLastState) {
        Engine_Sfx();
        g_EngineSfxRequest = 0;
        g_EngineSfxLastState = g_Crashed;
    }
}

/* 0x14923 Sfx_MissionEvent (FUN_00014923, GF step 65) */
void Sfx_MissionEvent(void)
{
    if ((DS32(0x90148) < 1 || Rand(0x28) != 1) && (Rand(10) == 1 || g_Mission == 0) && DS32(0x90190) == 0) {
        DS32(0x90190) = 1;
        DS32(0x8FF34) = 0x28;
        Sfx_Play(0x21, 0x1004, 0x3a, g_CamX + g_PlayerScrX);
    }
}

/* 0x3978d Sfx_LaunchThump (FUN_0003978d, GF step 62) */
void Sfx_LaunchThump(void)
{
    Sfx_Play(8, 15000, 0x2b, g_CamX + g_PlayerScrX);
}
