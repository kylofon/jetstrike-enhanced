/* Sound: port/spec/sound.md (§2 SB driver, §3 game API, §7 CD audio, §10 port design). */
#include "sound.h"

#include <stdio.h>
#include <stdlib.h>

#include "files.h"
#include "host.h"
#include "platform.h"

MixChan g_MixChan[4] = { { NULL, 0, 0, 0, 6, 0 }, { NULL, 0, 0, 0, 6, 0 },
                         { NULL, 0, 0, 0, 6, 0 }, { NULL, 0, 0, 0, 6, 0 } };
u32 g_SoundDevice;
const u32 g_SfxLens[SFX_SLICES] = {
    5054, 4186, 834, 3696, 8020, 18990, 2216, 2160, 3580, 13866, 7942, 8256, 5850, 4390, 5324, 46, 6398,
    16394, 4976, 4850, 46, 5904, 3174, 46, 46, 4984, 2688, 5240, 15904, 10880, 14070, 6292, 11660, 48766, 40466,
};
u32 g_SfxChanRR = 1;
u8 *g_SampleData;
int g_SBRate = SB_RATE_AS_CODED;       /* ENH: what the original plays (the 19920 Hz path sounds wrong) */

static MixChan *chan(int ch) { return &g_MixChan[(ch == 0 || ch == 1 || ch == 2) ? ch : 3]; }

/* The step formula of 0x309ab / 0x30ffa: (u16)(40000 / (u16)rateParam) & 15. */
static u32 rate_step(u32 rateParam, const char *fn)
{
    u16 d = (u16)rateParam;
    if (d == 0) {
        /* PORT: 16-bit divide by zero (#DE) in the original (frequency > 20000); the port stops with a
         * message instead of a host crash (QUIRKS.md). */
        char msg[112];
        snprintf(msg, sizeof msg, " (rate parameter %u): the original stops with a DOS/4GW exception", (unsigned)rateParam);
        host_audio_unlock();
        FatalError(fn, msg, 1);
    }
    return (u32)(u16)(40000u / d) & 0xf;
}

/* 0x309ab Mixer_SetChannel (ch 0/1/2, anything else = 3) */
void Mixer_SetChannel(int ch, const u8 *data, u32 len, u32 loop, u8 shift, u32 rateParam)
{
    host_audio_lock();
    MixChan *c = chan(ch);
    c->data = data;
    c->len = len;
    c->pos = 0;
    c->loop = loop;
    c->shift = shift;
    c->step = rate_step(rateParam, "Divide by zero in Mixer_SetChannel");
    host_audio_unlock();
}

/* 0x30ffa Mixer_SetRate */
void Mixer_SetRate(int ch, u32 rateParam)
{
    host_audio_lock();
    chan(ch)->step = rate_step(rateParam, "Divide by zero in Mixer_SetRate");
    host_audio_unlock();
}

/* Timer_ISR_SBMixer 0x310ba, mono path (sound.md §2.13 / §2.14), one output byte at a time.
 * PORT: the SDL stream pulls bytes on demand instead of 120-byte chunks at 50 Hz (game-side changes
 * apply at any sample boundary instead of the next chunk); the DMA ring and its throttle are gone.
 * Runs on the audio thread with the stream locked. */
void Mixer_Render(u8 *out, int n)
{
    for (int i = 0; i < n; i++) {
        u8 v = 0x80;
        for (int ch = 0; ch < 4; ch++) {
            MixChan *c = &g_MixChan[ch];
            if (c->data) {
                v = (u8)(v + (u8)(((u16)c->data[c->pos] << c->shift) >> 8));   /* 8-bit wrap, no clipping */
                c->pos += c->step;
            }
            if (c->len <= c->pos) {                     /* also true for an idle channel */
                if (c->loop == 1) c->pos = 0;
                else { c->data = NULL; c->pos = 0; c->len = 0; }
            }
        }
        out[i] = v;
    }
}

/* VolToShift (inlined in Sfx_PlayChannel / Sfx_PlayVoice, sound.md §3.5) */
static u8 vol_to_shift(int v)
{
    if (v <= 1) return 0;
    if (v == 2) return 1;
    if (v <= 4) return 2;
    if (v <= 8) return 3;
    if (v <= 16) return 4;
    if (v <= 32) return 5;
    return 6;
}

static u32 slice_offset(int slice)
{
    u32 off = 0;
    for (int i = 0; i < slice; i++) off += g_SfxLens[i];
    return off;
}

/* 0x31e24 Sound_Init, SB path. PORT: no detection; an SDL device that cannot be opened runs silent
 * (sound.md §8); the DMA ring, IRQ and timer ISR are replaced by the SDL stream. */
void Sound_Init(void)
{
    g_SoundDevice = 1;
    g_SampleData = malloc(AAF_SIZE);
    FILE *f = Platform_Fopen("misc\\jetsound.aaf", "rb");
    if (!f || !g_SampleData) {
        /* PORT: the original freads from a NULL FILE (crash); stop with a message instead. */
        FatalError("misc\\jetsound.aaf", " not found", 1);
    }
    if (fread(g_SampleData, AAF_SIZE, 1, f) != 1) { /* not checked by the original */ }
    fclose(f);
    for (u32 i = 0; i < AAF_SIZE; i++) g_SampleData[i] += 0x80;    /* signed -> unsigned */
    host_audio_open(g_SBRate, Mixer_Render);
}

/* 0x32119 Sound_Shutdown */
void Sound_Shutdown(void)
{
    if (g_SoundDevice == 1 && g_SampleData) {
        host_audio_close();
        for (int ch = 0; ch < 4; ch++) g_MixChan[ch].data = NULL;
        free(g_SampleData);
        g_SampleData = NULL;
    }
}

/* 0x321e5 Sound_StopAll. PORT: the channels are simply cleared (Q11: the original first mixes one byte
 * of sample[0] >> 7 per channel, inaudible). */
void Sound_StopAll(void)
{
    if (g_SoundDevice != 1) return;
    host_audio_lock();
    for (int ch = 0; ch < 4; ch++) {
        MixChan *c = &g_MixChan[ch];
        c->data = NULL; c->len = 0; c->pos = 0; c->loop = 0; c->shift = 1; c->step = 8;
    }
    host_audio_unlock();
}

/* 0x315bc Sfx_Play: x is ignored (no panning). */
void Sfx_Play(int id, int freq, int vol, int x)
{
    static int trace = -1;                       /* PORT (developer aid): JS_SFX_TRACE=1 logs every call */
    if (trace < 0) trace = getenv("JS_SFX_TRACE") != NULL;
    if (trace) { printf("sfx %d freq %d vol %d\n", id, freq, vol); fflush(stdout); }
    if (g_SfxOn) Sfx_PlayChannel(id - 1, freq / 2, vol, 0);
}

/* 0x31a53 Sfx_PlayChannel, SB path (round-robin channels 2, 3, 1, ...; the loop argument is ignored). */
void Sfx_PlayChannel(int slice, int f, int vol, int loop)
{
    if (g_SoundDevice != 1) return;
    u32 off = slice_offset(slice);
    if (++g_SfxChanRR > 3) g_SfxChanRR = 1;
    u8 shift = vol_to_shift(vol);
    if (f < 8000) f = 8000;
    Mixer_SetChannel((int)g_SfxChanRR, g_SampleData + off, g_SfxLens[slice] - 1, 0, shift,
                     (u32)((20000 / f) * 20000));
}

/* 0x31c51 Sfx_PlayVoice, SB path: fixed channel, no halving, no g_SfxOn check. */
void Sfx_PlayVoice(int ch, int slice, int f, int vol, int loop)
{
    if (g_SoundDevice != 1) return;
    u32 off = slice_offset(slice);
    if (f < 8000) f = 8000;
    Mixer_SetChannel(ch, g_SampleData + off, g_SfxLens[slice] - 1, (u32)loop, vol_to_shift(vol),
                     (u32)((20000 / f) * 20000));
}

/* 0x322c4 Sound_SetFreq, SB path */
void Sound_SetFreq(int ch, int f)
{
    if (g_SoundDevice != 1) return;
    if (f < 8000) f = 8000;
    Mixer_SetRate(ch, (u32)((20000 / f) * 20000));
}

/* 0x3235d Sound_SetVolume: GUS only, a no-op on SB (Q8). */
void Sound_SetVolume(int ch, int v) {}

/* ---------------------------------------------------------------- CD audio */

/* 0x30136 CD_PlayTrack. PORT: the ripped track instead of MSCDEX (sound.md §7.3). */
void CD_PlayTrack(int track)
{
    if (!g_CDMusicOn) return;
    CD_Stop();
    char name[32];
    snprintf(name, sizeof name, "MUSIC/TRACK%02d.WAV", track);
    char *path = host_game_path(name, false);
    if (path) host_music_play(path);
    host_free(path);
}

/* 0x30176 CD_Stop */
void CD_Stop(void)
{
    if (g_CDMusicOn) host_music_stop();
}
