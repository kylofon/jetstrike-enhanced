#include "host_int.h"

#include <stdio.h>
#include <string.h>

static SDL_AudioDeviceID device;
static SDL_AudioStream *mix_stream;      /* u8 mono at the Sound Blaster rate, pulled */
static SDL_AudioStream *music_stream;    /* the current CD track */
static Uint8 *music_buf;
static void (*mix_render)(u8 *, int);
static int mix_rate;

/* JS_AUDIO_DUMP: the mixer output as an 8-bit mono WAV (written from the audio thread). */
static FILE *dump_f;
static u32 dump_bytes;

static void put_le(u8 *p, u32 v, int n) { for (int i = 0; i < n; i++) p[i] = (u8)(v >> (8 * i)); }

static void dump_header(void)
{
    u8 h[44];
    memcpy(h, "RIFF", 4);              put_le(h + 4, 36 + dump_bytes, 4);
    memcpy(h + 8, "WAVEfmt ", 8);      put_le(h + 16, 16, 4);
    put_le(h + 20, 1, 2);              put_le(h + 22, 1, 2);
    put_le(h + 24, (u32)mix_rate, 4);  put_le(h + 28, (u32)mix_rate, 4);
    put_le(h + 32, 1, 2);              put_le(h + 34, 8, 2);
    memcpy(h + 36, "data", 4);         put_le(h + 40, dump_bytes, 4);
    fseek(dump_f, 0, SEEK_SET);
    fwrite(h, 1, sizeof h, dump_f);
    fseek(dump_f, 0, SEEK_END);
}

static void SDLCALL mix_callback(void *userdata, SDL_AudioStream *stream, int additional, int total)
{
    u8 buf[1024];
    while (additional > 0) {
        int n = SDL_min(additional, (int)sizeof buf);
        mix_render(buf, n);
        if (dump_f) { fwrite(buf, 1, (size_t)n, dump_f); dump_bytes += (u32)n; }
        SDL_PutAudioStreamData(stream, buf, n);
        additional -= n;
    }
}

bool host_audio_open(int rate, void (*render)(u8 *out, int n))
{
    host_audio_close();
    if (!SDL_WasInit(SDL_INIT_AUDIO)) return false;
    device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
    if (!device) {
        fprintf(stderr, "audio unavailable: %s\n", SDL_GetError());
        return false;
    }
    mix_render = render;
    mix_rate = rate;
    SDL_AudioSpec spec = { SDL_AUDIO_U8, 1, rate };
    mix_stream = SDL_CreateAudioStream(&spec, NULL);
    if (!mix_stream || !SDL_BindAudioStream(device, mix_stream)) {
        fprintf(stderr, "audio stream: %s\n", SDL_GetError());
        host_audio_close();
        return false;
    }
    const char *dump = SDL_getenv("JS_AUDIO_DUMP");
    if (dump && *dump) {
        dump_f = fopen(dump, "wb");
        if (dump_f) dump_header(); else fprintf(stderr, "cannot write %s\n", dump);
    }
    SDL_SetAudioStreamGetCallback(mix_stream, mix_callback, NULL);
    return true;
}

void host_audio_close(void)
{
    host_music_stop();
    if (mix_stream) SDL_DestroyAudioStream(mix_stream);
    mix_stream = NULL;
    if (device) SDL_CloseAudioDevice(device);
    device = 0;
    if (dump_f) { dump_header(); fclose(dump_f); dump_f = NULL; }
}

void host_audio_lock(void) { if (mix_stream) SDL_LockAudioStream(mix_stream); }
void host_audio_unlock(void) { if (mix_stream) SDL_UnlockAudioStream(mix_stream); }

bool host_music_play(const char *path)
{
    host_music_stop();
    if (!device || !path) return false;
    SDL_AudioSpec spec;
    Uint32 len = 0;
    if (!SDL_LoadWAV(path, &spec, &music_buf, &len)) {
        fprintf(stderr, "music %s: %s\n", path, SDL_GetError());
        return false;
    }
    music_stream = SDL_CreateAudioStream(&spec, NULL);
    if (!music_stream || !SDL_BindAudioStream(device, music_stream)) {
        host_music_stop();
        return false;
    }
    SDL_PutAudioStreamData(music_stream, music_buf, (int)len);
    SDL_FlushAudioStream(music_stream);                  /* played once, then silence */
    return true;
}

void host_music_stop(void)
{
    if (music_stream) SDL_DestroyAudioStream(music_stream);
    music_stream = NULL;
    SDL_free(music_buf);
    music_buf = NULL;
}
