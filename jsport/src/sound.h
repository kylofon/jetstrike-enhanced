#pragma once
/* Sound (port/spec/sound.md): the Sound Blaster 4-channel 8-bit mixer, sound effects, CD music.
 * Only the SB path is ported (GUS: optional, sound.md §6). */
#include "types.h"

#pragma pack(push, 1)
typedef struct {                 /* 0x803d4, 4 x 21 bytes */
    const u8 *data;              /* +0x00 unsigned 8-bit samples; NULL = idle */
    u32 len;                     /* +0x04 */
    u32 pos;                     /* +0x08 */
    u32 loop;                    /* +0x0c 1 = loop */
    u8  shift;                   /* +0x10 volume shift 0..6 */
    u32 step;                    /* +0x11 1 or 2 */
} MixChan;
#pragma pack(pop)

#define SFX_SLICES   35
#define AAF_SIZE     0x48a7a
#define SB_RATE_DESIGNED 19920   /* Sound_Init's request (default, PLAN.md decision 6) */
#define SB_RATE_AS_CODED 3906    /* time constant 0 = 3906.25 Hz, what the original programs (Q1) */

extern MixChan g_MixChan[4];
extern u32 g_SoundDevice;        /* 0x80430: 0 none, 1 SB */
extern const u32 g_SfxLens[SFX_SLICES];   /* 0x80434 */
extern u32 g_SfxChanRR;          /* 0x80546 */
extern u8 *g_SampleData;         /* 0x93388 */
extern int g_SBRate;             /* PORT: mixer output rate (option --sb-rate) */

void Mixer_SetChannel(int ch, const u8 *data, u32 len, u32 loop, u8 shift, u32 rateParam);  /* 0x309ab */
void Mixer_SetRate(int ch, u32 rateParam);                                                   /* 0x30ffa */
void Mixer_Render(u8 *out, int n);   /* PORT: Timer_ISR_SBMixer 0x310ba body, per output byte */

void Sound_Init(void);           /* 0x31e24 */
void Sound_Shutdown(void);       /* 0x32119 */
void Sound_StopAll(void);        /* 0x321e5 */
void Sfx_Play(int id, int freq, int vol, int x);                 /* 0x315bc */
void Sfx_PlayChannel(int slice, int f, int vol, int loop);       /* 0x31a53 */
void Sfx_PlayVoice(int ch, int slice, int f, int vol, int loop); /* 0x31c51 */
void Sound_SetFreq(int ch, int f);                               /* 0x322c4 */
void Sound_SetVolume(int ch, int v);                             /* 0x3235d */

/* CD audio (sound.md §7.3): Game/MUSIC/TRACKnn.WAV, played once; a missing file is silence. */
void CD_PlayTrack(int track);    /* 0x30136: only if g_CDMusicOn */
void CD_Stop(void);              /* 0x30176: only if g_CDMusicOn */
