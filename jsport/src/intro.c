/* The intro, INTRO.EXE (port/spec/intro.md; formats and sound: port/formats/sound_intro.md §2,
 * port/spec/sound.md §9). Addresses are INTRO.EXE ones (work/INTRO.bin, base 0x10000), marked "INTRO".
 *
 * INTRO.EXE has its own copies of the mode-X, Sound Blaster and MSCDEX code; they are ported here as static
 * functions on top of the port's VGA model (video.h: vram / dac / crtc) and audio device (host.h). To keep
 * them apart from the game's routines of the same name (video.c, sound.c) every INTRO function that is not
 * already called Intro_* carries an I_ prefix, and the intro's globals an ig_ prefix (instead of g_).
 *
 * Non-pointer globals live in the intro's own data image (g_iseg: LE object 6 of INTRO.EXE, dseg.h) at their
 * original addresses: the palette, sprite and tile tables, credit strings and widths, CITY.PTT, the key
 * flags. Buffers are C pointers.
 *
 * Every original bug is kept (QUIRKS.md): only Esc and D (on release) leave the intro, the FRAME7 over-read
 * (the 40 bytes land in the next heap block, emulated), the 4-pixel shift of sprite 1, the 3-line credit's
 * width, particle 0 of the smoke never drawn, the play-once samples, the city lights tested against plane 0. */
#include "intro.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dseg.h"
#include "files.h"
#include "host.h"
#include "sound.h"
#include "video.h"

/* ---------------------------------------------------------------- globals (intro.md §2) */

#define ig_SoundAlive         IS32(0x60364)    /* Intro_SoundInit sets 1, Intro_SoundShutdown 0 */
#define ig_CreditIndex        IS32(0x60368)    /* 0..10 */
#define ig_PalSource          ISEG(0x6036C)    /* 0x240 bytes, converted in place by Intro_Main */
#define ig_LogoShown          IS32(0x60701)
#define ig_FrameOverlayActive IS32(0x60705)
#define ig_SoundDevice        IS32(0x60709)    /* 0 none, 1 SB (2 GUS: not ported) */
#define ig_SampleLens         IS32A(0x6070D)   /* [8] INTRO.SAM slice lengths */
#define ig_CreditStrings      IS32A(0x6072D)   /* [11] flat pointers into the image */
#define ig_CreditWidths       IS32A(0x6075D)   /* [11][2] width of line 1, of line 2 */
#define ig_GlyphSetPtr        I32(0x607B5)     /* -> 0x60260 "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!?" */
#define ig_SpriteSize         IS32A(0x60B50)   /* [97][2] {w, h} of sprite n at [n-1] */
#define ig_CityPoints         IS32A(0x60E98)   /* [405][3] {x, y, colour} */
#define ig_Palette            ISEG(0x65C18)    /* [0x300] 6-bit master palette */
#define ig_PaletteWork        ISEG(0x65F18)    /* [0x300] faded copy, uploaded */
#define ig_TileMap            IS32A(0x66818)   /* [16][24] tile index per cell */
#define ig_KeyReleased        IS32A(0x66E18)   /* [128] set on break, cleared on make */
#define ig_Scene              IS32(0x67018)    /* 1..7 */
#define ig_SpriteOffs         IS32A(0x67044)   /* [98] offs[0] = 0; sprite n is drawn from offs[n-1] */
#define ig_MouseEnabled       IS32(0x671F4)    /* never set */

#define CITY_POINTS 0x195
#define SAM_SIZE    0x3F404

static u8 *ig_RawBuf;           /* 0x671ec INTRO.RAW */
static u8 *ig_BigFont;          /* 0x671fc BIGFNT2.RAW + BIGFNT1.RAW */
static u8 *ig_SmallFontBuf;     /* 0x671f8 SMALLFNT.RAW, never drawn */
static u8 *ig_TilBuf;           /* 0x671e8 INTRO.TIL */
static u8 *ig_Char1Buf;         /* 0x67040 CHAR1.RAW, never drawn */
static u8 *ig_AosetBuf;         /* 0x671f0 only for the dead menu */
static u8 *ig_FrameBuf[8];      /* 0x67020 FRAME1..8.RAW */
static u8 *ig_TilePtrs[0x180];  /* 0x66218 */
static u8 *ig_SamBuf;           /* 0x67210 INTRO.SAM, unsigned */
static u8 *s_Frame78Block;      /* PORT: the FRAME7 + FRAME8 heap blocks (see Intro_Main) */

/* The intro's rand(): INTRO.EXE has its own Watcom C runtime, never seeded (seed 1), so the intro is the
 * same every run and the game's own sequence is not touched. */
static u32 s_RandSeed = 1;

/* INTRO 0x16123 rand */
static int I_rand(void)
{
    s_RandSeed = s_RandSeed * 0x41C64E6Du + 0x3039u;
    return (int)((s_RandSeed >> 16) & 0x7FFF);
}

/* ---------------------------------------------------------------- keyboard */

/* INTRO 0x10010 Kbd_ISR: g_KeyReleased[sc & 0x7f] = 1 on a break code, 0 on a make code (an E0 prefix byte
 * sets [0x60], unused). */
static void I_Kbd_ISR(u8 sc)
{
    ig_KeyReleased[sc & 0x7F] = sc < 0x80 ? 0 : 1;
}

/* INTRO 0x142c1 Intro_ReadKey (body 0x142ce): -1 Esc or D released, 1 Up, 2 Down, 4 Right, 3 Left, 5 Enter,
 * else 0; the flag found is cleared. The mouse branch needs g_MouseEnabled, never set (not ported). */
static int Intro_ReadKey(void)
{
    s32 *k = ig_KeyReleased;
    if (k[0x01]) { k[0x01] = 0; return -1; }
    if (k[0x20]) { k[0x20] = 0; return -1; }
    if (k[0x48]) { k[0x48] = 0; return 1; }
    if (k[0x50]) { k[0x50] = 0; return 2; }
    if (k[0x4D]) { k[0x4D] = 0; return 4; }
    if (k[0x4B]) { k[0x4B] = 0; return 3; }
    if (k[0x1C]) { k[0x1C] = 0; return 5; }
    return 0;
}

/* ---------------------------------------------------------------- video (intro.md §3) */

/* INTRO 0x20008 Video_SetModeX: mode 13h (DAC black), unchained 320x240 60 Hz, 96-byte rows, 256 KiB cleared.
 * Mode 13h leaves the CRTC start at 0 and the graphics controller's read map select at plane 0. */
static void I_Video_SetModeX(void)
{
    memset(dac, 0, sizeof dac);
    Video_UseLayout(false);                             /* ENH: the intro keeps the 320x240 layout */
    memset(vram, 0, (size_t)VRAM_SIZE);
    crtc.start = 0;
    crtc.pan = 0;
    crtc.split_rows = 240;
}

/* INTRO 0x205c4 Video_FlipScroll(x, showPage1): CRTC start = (showPage1 ? 0x5a00 : 0) + x/4, wait for the end
 * of the current retrace and the start of the next, then pel panning (x & 3) * 2 (= x & 3 pixels).
 * PORT: start and panning both apply to the frame displayed after this retrace (the start latches at the
 * retrace, the panning is written inside it); host_wait_vretrace presents exactly that frame. */
static void I_Video_FlipScroll(s32 x, s32 showPage1)
{
    crtc.start = (u16)((showPage1 ? PAGE_BYTES : 0) + (u16)((u16)x >> 2));
    crtc.pan = (u8)(x & 3);
    host_wait_vretrace();
}

/* INTRO 0x20254 Pal_Set(pal, first, last, waitVsync): (waits for the start of a retrace,) then uploads
 * (last - first + 1) colours from pal[0] (not pal[first]) starting at DAC entry first.
 * PORT: the upload falls inside the retrace, so the frame presented at it already has the new colours. */
static void I_Pal_Set(const u8 *pal, int first, int last, int waitVsync)
{
    u16 n = (u16)((u16)(last - first) + 1);
    for (u32 i = 0; i < (u32)n * 3; i++) {
        u32 d = (u32)first * 3 + i;
        if (d < sizeof dac) dac[d] = pal[i];
    }
    if (waitVsync) host_wait_vretrace();
}

/* INTRO 0x149a6 Pal_Fade(dir) (body 0x149b3): dir 0 fades in, work = pal * k / 25 for k = 1..25; else fades
 * out, k = 25..0; one retrace each. */
static void I_Pal_Fade(int dir)
{
    int k = 25;
    if (dir == 0) {
        k = 1;
        do {
            for (int i = 0; i < 0x300; i++) ig_PaletteWork[i] = (u8)(((int)ig_Palette[i] * k) / 25);
            I_Pal_Set(ig_PaletteWork, 0, 0xFF, 1);
            k++;
        } while (k < 26);
    } else {
        do {
            for (int i = 0; i < 0x300; i++) ig_PaletteWork[i] = (u8)(((int)ig_Palette[i] * k) / 25);
            I_Pal_Set(ig_PaletteWork, 0, 0xFF, 1);
            k--;
        } while (k > -1);
    }
}

/* INTRO 0x200b8 Video_PlotUnder(x, y, col, pageOfs): writes col at (x, y) if the pixel read is 0 or
 * 0x20..0x24. The read goes through the graphics controller's read map select, which the intro never sets
 * (mode 13h leaves plane 0): the byte tested is the plane-0 pixel of the 4-pixel group, (x & ~3, y). */
static void I_Video_PlotUnder(u32 x, s32 y, s32 col, s32 pageOfs)
{
    s32 byte = y * 96 + pageOfs + (s32)(x >> 2);
    u8 cur = vget(byte * 4);
    if (cur == 0 || (cur >= 0x20 && cur <= 0x24)) vput(byte * 4 + (s32)(x & 3), (u8)col);
}

/* INTRO 0x202c0 Spr_Blit(src, x, y, w, h, skip, pad, pageOfs): chunky source -> planar page, colour 0
 * transparent; row r starts at src + skip + r * (w + skip + pad). */
static void I_Spr_Blit(const u8 *src, s32 x, s32 y, s32 w, s32 h, s32 skip, s32 pad, s32 pageOfs)
{
    for (s32 r = 0; r < h; r++) {
        const u8 *row = src + skip + r * (w + skip + pad);
        for (s32 c = 0; c < w; c++) {
            u8 v = row[c];
            if (v != 0) vput(VIDX(pageOfs, x + c, y + r), v);
        }
    }
}

/* INTRO 0x100ce Spr_BlitClip(src, x, y, w, h, 0, 0, pageOfs) (body 0x100db): drawn only if x + w > 0,
 * x < 0x150 and y >= 0; clipped to columns 0..0x15f; no bottom clip. */
static void I_Spr_BlitClip(const u8 *src, s32 x, s32 y, s32 w, s32 h, s32 pageOfs)
{
    if (x + w > 0 && x < 0x150 && y > -1) {
        if (x < 0) I_Spr_Blit(src, 0, y, w + x, h, -x, 0, pageOfs);
        else if (x + w < 0x160) I_Spr_Blit(src, x, y, w, h, 0, 0, pageOfs);
        else I_Spr_Blit(src, x, y, 0x160 - x, h, 0, w - (0x160 - x), pageOfs);
    }
}

/* INTRO 0x20614 Tile_DrawBand(map, ptrs, band, plane, pageOfs): band b = page rows b*32..b*32+31 (band 7:
 * one tile row, 224..239); tile k of the band (24 per row) from map[k]; plane p's 16 rows x 4 bytes. */
static void I_Tile_DrawBand(const s32 *map, u8 *const *ptrs, int band, int plane, s32 pageOfs)
{
    s32 base = band * 0xC00 + pageOfs;
    int n = band == 7 ? 0x18 : 0x30;
    for (int k = 0; k < n; k++) {
        const u8 *t = ptrs[map[k]] + plane * 0x40;
        s32 b = base + (k / 24) * 16 * 96 + (k % 24) * 4;
        for (int r = 0; r < 16; r++)
            for (int c = 0; c < 4; c++) vput((b + r * 96 + c) * 4 + plane, t[r * 4 + c]);
    }
}

/* INTRO 0x20ba3 Tile_ScrollMapLeft(map): each of the 16 rows of 24 entries rotates left by one. */
static void I_Tile_ScrollMapLeft(s32 *map)
{
    for (int r = 0; r < 16; r++) {
        s32 *row = map + r * 24, first = row[0];
        memmove(row, row + 1, 23 * sizeof *row);
        row[23] = first;
    }
}

/* All bands of the background: the 32 Tile_DrawBand calls of Intro_Main, map + band * 48 each. */
static void draw_bands(int nbands, s32 pageOfs)
{
    for (int band = 0; band < nbands; band++)
        for (int plane = 0; plane < 4; plane++)
            I_Tile_DrawBand(ig_TileMap + band * 48, ig_TilePtrs, band, plane, pageOfs);
}

/* INTRO 0x13ff1 Intro_DrawCredits(scroll, pageOfs) (body 0x13ffe): message g_CreditIndex in the big font,
 * line 1 centred with width 1, every following line with width 2 (intro.md Q3), 12 px apart; y 100, or
 * 150 while the logo / overlay is up or in scene 7. */
static void Intro_DrawCredits(s32 scroll, s32 pageOfs)
{
    char s[52];
    const s32 *wid = ig_CreditWidths + ig_CreditIndex * 2;
    s32 x = scroll + (0x140 - wid[0]) / 2;
    s32 y = (ig_LogoShown == 0 && ig_FrameOverlayActive == 0 && ig_Scene != 7) ? 100 : 0x96;
    const char *src = (const char *)ISEG_PTR((u32)ig_CreditStrings[ig_CreditIndex]);
    snprintf(s, sizeof s, "%s", src ? src : "");
    const char *glyphs = (const char *)ISEG_PTR(ig_GlyphSetPtr);
    for (u32 i = 0; i < strlen(s); i++) {
        if (s[i] == '\\') {
            x = scroll + (0x140 - wid[1]) / 2;
            y += 12;
            continue;
        }
        int k = 0;
        while (glyphs[k] != s[i] && glyphs[k] != '\0') k++;
        if (s[i] == ' ') {
            x += 11;
        } else {
            I_Spr_Blit(ig_BigFont + k * 0x90 + (k < 0x11 ? 0x10 : 0x20), x, y, 16, 9, 0, 0, pageOfs);
            x += s[i] == 'I' ? 7 : 11;
            if (s[i] == 'W') x += 1;
        }
    }
}

/* ---------------------------------------------------------------- sound (sound.md §9) */

/* INTRO's 4-channel mixer (the SB driver object, 0x50000): same code as the game's (sound.c). */
static MixChan s_Chan[4];
static bool s_DmaOn;            /* PORT: SB_StartDMA .. SB_Stop */

/* INTRO 0x5042f Mixer_SetChannel(ch, ptr, len, loop, volshift, rate): step = (u16)(40000 / rate) & 15. */
static void I_Mixer_SetChannel(int ch, const u8 *data, u32 len, u32 loop, u8 shift, u32 rateParam)
{
    host_audio_lock();
    MixChan *c = &s_Chan[(ch == 0 || ch == 1 || ch == 2) ? ch : 3];
    c->data = data;
    c->len = len;
    c->pos = 0;
    c->loop = loop;
    c->shift = shift;
    c->step = (u32)(u16)(40000u / (u16)rateParam) & 0xF;
    host_audio_unlock();
}

/* INTRO 0x5058a Mixer_Update, per output byte (the formula of sound.c Mixer_Render).
 * PORT: the original refills the DMA ring by polling, 4 times per frame from Intro_Main; the SDL stream
 * pulls the bytes on the audio thread instead, so the polls are gone. After SB_Stop: silence. */
static void I_Mixer_Render(u8 *out, int n)
{
    for (int i = 0; i < n; i++) {
        u8 v = 0x80;
        if (s_DmaOn)
            for (int ch = 0; ch < 4; ch++) {
                MixChan *c = &s_Chan[ch];
                if (c->data) {
                    v = (u8)(v + (u8)(((u16)c->data[c->pos] << c->shift) >> 8));
                    c->pos += c->step;
                }
                if (c->len <= c->pos) {
                    if (c->loop == 1) c->pos = 0;
                    else { c->data = NULL; c->pos = 0; c->len = 0; }
                }
            }
        out[i] = v;
    }
}

/* INTRO 0x101c7 Intro_PlaySample(n) (body 0x101d4): INTRO.SAM slice n; 0, 2, 4 on channel 1, 1, 3 on
 * channel 2, one-shot, shift 6; 6 on channel 0, looped, shift 4; rate 12000 (step 3). 5 and 7: nothing. */
static void Intro_PlaySample(int n)
{
    if (ig_SoundDevice == 0 || ig_SoundDevice >= 2) return;    /* GUS path not ported */
    s32 off = 0;
    for (int i = 0; i < n; i++) off += ig_SampleLens[i];
    if (!ig_SamBuf) return;     /* PORT: after Intro_SoundShutdown the original passes NULL + off to a
                                 * stopped DMA (inaudible); never happens, the samples play once */
    const u8 *p = ig_SamBuf + off;
    u32 len = (u32)ig_SampleLens[n];
    switch (n) {
    case 0: I_Mixer_SetChannel(1, p, len, 0, 6, 12000); break;
    case 1: I_Mixer_SetChannel(2, p, len, 0, 6, 12000); break;
    case 2: I_Mixer_SetChannel(1, p, len, 0, 6, 12000); break;
    case 3: I_Mixer_SetChannel(2, p, len, 0, 6, 12000); break;
    case 4: I_Mixer_SetChannel(1, p, len, 0, 6, 12000); break;
    case 6: I_Mixer_SetChannel(0, p, len, 1, 4, 12000); break;
    default: break;
    }
}

/* INTRO 0x10464 Intro_SoundInit (body 0x10471): GUS if present, else SB at 40000 Hz: INTRO.SAM loaded,
 * made unsigned, the loop bed (sample 6) started.
 * PORT: no GUS; SB_Detect, the DOS DMA buffer and the IRQ hook are the SDL device (the mixer at 40000 Hz,
 * or 3906 with --sb-rate 3906: the time constant 0 the original actually programs, sound.md Q1). A device
 * that cannot be opened = no card (silent). */
static void Intro_SoundInit(void)
{
    ig_SoundAlive = 1;
    int rate = g_SBRate == SB_RATE_AS_CODED ? SB_RATE_AS_CODED : 40000;
    s_DmaOn = false;
    memset(s_Chan, 0, sizeof s_Chan);
    if (!host_audio_open(rate, I_Mixer_Render)) return;
    ig_SoundDevice = 1;
    ig_SamBuf = malloc(SAM_SIZE);
    FILE *f = Platform_Fopen("intro\\intro.sam", "rb");
    if (!f || !ig_SamBuf)
        /* PORT: the original freads from a NULL FILE (a crash); stop with a message instead. */
        host_fatal_code(1, "intro\\intro.sam not found");
    if (fread(ig_SamBuf, SAM_SIZE, 1, f) != 1) { /* not checked by the original */ }
    fclose(f);
    for (u32 i = 0; i < SAM_SIZE; i++) ig_SamBuf[i] = (u8)(ig_SamBuf[i] - 0x80);
    Intro_PlaySample(6);
    host_audio_lock();
    s_DmaOn = true;                                     /* SB_StartDMA */
    host_audio_unlock();
}

/* INTRO 0x106cb Intro_SoundShutdown (body 0x106d8): once; SB: stop the DMA, wait for it, unhook, free the
 * samples (later Intro_PlaySample calls are silent). PORT: the device stays open for the CD music. */
static void Intro_SoundShutdown(void)
{
    if (ig_SoundAlive == 0) return;
    if (ig_SoundDevice == 1 && ig_SamBuf) {
        host_audio_lock();
        s_DmaOn = false;
        memset(s_Chan, 0, sizeof s_Chan);
        host_audio_unlock();
        free(ig_SamBuf);
        ig_SamBuf = NULL;
    }
    ig_SoundAlive = 0;
}

/* INTRO 0x1553a CD_InitAndPlayTrack2: MSCDEX install / version checks (printf on failure, no fatal), disc
 * info, track table, then track 2 from its start to the start of track 3 (once).
 * PORT: the ripped track Game/MUSIC/TRACK02.WAV (sound.md §7.3); missing = silent. Not gated by JS.CFG: the
 * intro never reads it. */
static void I_CD_InitAndPlayTrack2(void)
{
    char *path = host_game_path("MUSIC/TRACK02.WAV", false);
    if (path) host_music_play(path);
    host_free(path);
}

/* INTRO 0x15511 CD_Stop */
static void I_CD_Stop(void) { host_music_stop(); }

/* ---------------------------------------------------------------- Intro_Main */

/* PORT: fopen("<name>", "rb") with INTRO as the current directory (JS.BAT: cd intro). A missing file makes
 * the original fread from a NULL FILE (a crash); the port stops with a message. */
static FILE *intro_open(const char *name)
{
    char path[64];
    snprintf(path, sizeof path, "intro\\%s", name);
    FILE *f = Platform_Fopen(path, "rb");
    if (!f) host_fatal_code(1, "%s not found", path);
    return f;
}

static void intro_read(const char *name, void *buf, size_t n)
{
    FILE *f = intro_open(name);
    if (fread(buf, 1, n, f) != n) { /* short reads (CHAR1.RAW) are not checked by the original */ }
    fclose(f);
}

/* Draw(n, x, y) of intro.md §4.3: sprite n (1-based) from raw + offs[n-1], w x h of size[n-1]. */
static void draw_sprite(s32 n, s32 x, s32 y, s32 pageOfs)
{
    I_Spr_BlitClip(ig_RawBuf + ig_SpriteOffs[n - 1], x, y, ig_SpriteSize[(n - 1) * 2], ig_SpriteSize[(n - 1) * 2 + 1],
                   pageOfs);
}

/* The logo (sprite 66), centred, 12 px above the middle. */
static void draw_logo(s32 scroll, s32 pageOfs)
{
    I_Spr_BlitClip(ig_RawBuf + ig_SpriteOffs[65], (0x140 - ig_SpriteSize[65 * 2]) / 2 + scroll,
                   (0xF0 - ig_SpriteSize[65 * 2 + 1]) / 2 - 12, ig_SpriteSize[65 * 2], ig_SpriteSize[65 * 2 + 1], pageOfs);
}

/* The smoke particles of scenes 5 and 6 (local_798 of Intro_Main: y at [p+1], age [p+101], sprite [p+201],
 * x [p+301]). */
typedef struct { s32 x[100], y[100], spr[100], age[100]; u32 n; } Smoke;

/* Inlined in Intro_Main (scenes 5 and 6, non-step frames): particles n-1 .. 1 drawn (particle 0 never, Q5). */
static void smoke_draw(const Smoke *s, s32 scroll, s32 pageOfs)
{
    for (s32 j = (s32)s->n - 1; j > 0; j--) draw_sprite(s->spr[j], scroll + s->x[j], s->y[j], pageOfs);
}

/* Inlined in Intro_Main (scenes 5 and 6, step frames): draw, drift up-left, age (+4 per step once off is
 * set), grow (1/3, up to sprite 70), recycle with the last live particle (swap, n - 1). */
static void smoke_update(Smoke *s, u32 off, s32 scroll, s32 pageOfs)
{
    for (s32 j = (s32)s->n - 1; j > 0; j--) {
        draw_sprite(s->spr[j], scroll + s->x[j], s->y[j], pageOfs);
        s->x[j]--;
        s->y[j]--;
        s->age[j] += (s32)(off * 3 + 1);
        if (I_rand() % 3 == 1 && s->spr[j] < 0x46) s->spr[j]++;
        if (0x50 - (s32)s->n < s->age[j]) {
            u32 l = s->n - 1;
            s32 t;
            t = s->x[j]; s->x[j] = s->x[l]; s->x[l] = t;
            t = s->y[j]; s->y[j] = s->y[l]; s->y[l] = t;
            t = s->spr[j]; s->spr[j] = s->spr[l]; s->spr[l] = t;
            t = s->age[j]; s->age[j] = s->age[l]; s->age[l] = t;
            s->n--;
        }
    }
}

static void smoke_add(Smoke *s, s32 x, s32 y, s32 spr)
{
    if ((s32)s->n < 0x3C && x < 0x140) {
        s->x[s->n] = x;
        s->y[s->n] = y;
        s->spr[s->n] = spr;
        s->age[s->n] = 0;
        s->n++;
    }
}

/* INTRO 0x107bb main -> 0x107c8 Intro_Main: load, mode X, the scene machine (intro.md §4) until Esc or D is
 * released, then sound off, CD stop, fade out. Returns 1 (0 when a buffer cannot be allocated).
 * The post-loop code at 0x13475 (Intro_ExitWaitKey, the PLAYERS / GAMES / ABORT menu) and Intro_Cleanup
 * 0x13f03 are only reachable from inside that block: after the loop Intro_Main returns (0x1344c -> 0x13fe6),
 * so they are dead code and not ported. */
static int Intro_Main(void)
{
    s32 exitFlag = 0;           /* local_10 */
    s32 credOn = 1;             /* local_c */
    s32 emit = 0;               /* local_b4 */
    Smoke smoke;                /* local_798.. */
    smoke.n = 0;                /* local_d8 */
    s32 scroll = 0;             /* local_fc */
    s32 c3 = 0, c10 = 0, cCred = 0;     /* local_100, local_104, local_108 */
    s32 pageOfs = 0;            /* local_124 */
    /* Never initialised in the original (stack): cStep is reset before its first use, cOvl too; the
     * play-once flags are assumed 0 (the samples play on the first cycle). */
    s32 cStep = 0, cOvl = 0;    /* local_10c, local_110 */
    s32 played1 = 0, played2 = 0, played3 = 0, played4 = 0, played5 = 0;  /* local_dc, e0, e4, e8, 134 */
    u32 off = 0;                /* local_d0 */
    s32 vxA = 0, vyA = 0, vxB = 0, vyB = 0;     /* local_60, 64, 68, 6c */
    s32 l5c = 0;
    s32 ex = 0, ey = 0, smokeSpr = 0, vx6 = 0, tx = 0, ty = 0;   /* local_b8, bc, cc, d4, c0, c4 */
    s32 l74 = 0, l78 = 0, l7c = 0, l80 = 0, l84 = 0, l88 = 0, l8c = 0, l90 = 0, l94 = 0, l98 = 0;  /* dead */
    (void)l5c; (void)l7c; (void)l80; (void)l8c;

    Intro_SoundInit();
    for (int i = 0; i < 0x80; i++) ig_KeyReleased[i] = 0;
    for (int i = 0; i < 0x60; i++) ig_PalSource[i] = (u8)(ig_PalSource[i] << 2);
    for (int i = 0x60; i < 0x240; i++) ig_PalSource[i] = (u8)(ig_PalSource[i] >> 2);
    for (int i = 0; i < 0x300; i++) ig_PaletteWork[i] = 0;

    static const u32 frameSize[8] = { 0x1A40, 0x3200, 0x6B80, 0xBCC0, 0xE100, 0xFDC0, 0x10F18, 0x12C00 };
    ig_RawBuf = malloc(0x36320);
    ig_BigFont = malloc(0x1650);
    ig_SmallFontBuf = malloc(0xC90);
    ig_TilBuf = malloc(0x18000);
    ig_Char1Buf = calloc(1, 0x33F8);
    ig_AosetBuf = malloc(0x12C00);
    for (int k = 0; k < 6; k++) ig_FrameBuf[k] = malloc(frameSize[k]);
    /* FRAME7's buffer is 0x10f18 bytes but 0x10f40 (320 x 217) are read into it and drawn from it (Q1): the
     * 40 extra bytes go to the next heap block, FRAME8's (malloc'ed right after it: 4-byte Watcom heap tag,
     * then its data), and are overwritten there by FRAME8's own read; the draw then reads them back from
     * the tag and FRAME8's first row. PORT: one block holds both, laid out like that heap (all those bytes
     * are 0 in the shipped files, so the tag size assumed does not change the picture). */
    s_Frame78Block = calloc(1, 0x10F18 + 4 + 0x12C00);
    ig_FrameBuf[6] = s_Frame78Block;
    ig_FrameBuf[7] = s_Frame78Block ? s_Frame78Block + 0x10F18 + 4 : NULL;
    if (!ig_RawBuf || !ig_BigFont || !ig_SmallFontBuf || !ig_TilBuf || !ig_Char1Buf) {
        printf("%s", ISTR(0x60011));                    /* "Not enough memory!\n" */
        return 0;
    }
    for (int k = 0; k < 8; k++)
        if (!ig_FrameBuf[k]) host_fatal_code(1, "INTRO: out of memory");   /* PORT: unchecked in the original */

    intro_read(ISTR(0x60025), ig_RawBuf, 0x36320);      /* intro.raw */
    intro_read(ISTR(0x6002F), ig_BigFont, 0xC80);       /* bigfnt2.raw */
    intro_read(ISTR(0x6003B), ig_BigFont + 0xC80, 0x9D0);   /* bigfnt1.raw */
    intro_read(ISTR(0x60047), ig_SmallFontBuf, 0xC90);  /* smallfnt.raw */
    intro_read(ISTR(0x60054), ig_TilBuf, 0x18000);      /* intro.til */
    intro_read(ISTR(0x6005E), ig_Char1Buf, 0x33F8);     /* char1.raw (576 bytes) */
    intro_read(ISTR(0x60068), ig_CityPoints, 0x12FC);   /* city.ptt */
    static const u32 frameName[8] = { 0x60071, 0x6007C, 0x60087, 0x60092, 0x6009D, 0x600A8, 0x600B3, 0x600BE };
    for (int k = 0; k < 8; k++)                         /* frame1..8.raw; FRAME7 with 0x10f40 bytes */
        intro_read(ISTR(frameName[k]), ig_FrameBuf[k], k == 6 ? 0x10F40 : frameSize[k]);

    for (int i = 0; i < 0x180; i++) ig_TilePtrs[i] = ig_TilBuf + i * 0x100;
    /* Sprite table (intro.md §2, Q2): w = BE16 * 16, h = BE16; header zeroed, pixels masked to 0..31. */
    s32 pos = 0;
    ig_SpriteOffs[0] = 0;
    for (int k = 1; k < 0x62; k++) {
        u8 *r = ig_RawBuf + pos;
        s32 w16 = r[0] << 8 | r[1], h = r[2] << 8 | r[3];
        ig_SpriteSize[(k - 1) * 2] = w16 << 4;
        ig_SpriteSize[(k - 1) * 2 + 1] = h;
        for (s32 i = pos + 4; i < w16 * 16 * h + pos; i++) ig_RawBuf[i] &= 0x1F;
        r[0] = r[1] = r[2] = r[3] = 0;
        pos += w16 * 16 * h;
        ig_SpriteOffs[k] = pos + 4;
    }

    host_set_kbd_handler(I_Kbd_ISR);                    /* _dos_setvect(9, Kbd_ISR) */
    I_Video_SetModeX();
    memcpy(ig_Palette, ig_PalSource, 0x240);
    for (int i = 0; i < 0x300; i++) ig_PaletteWork[i] = 0;
    I_Pal_Set(ig_PaletteWork, 0, 0xFF, 0);

    ig_Scene = 1;
    s32 xA = -96, yA = 100, xB = -0x78, yB = 0x90;      /* local_2c, 30, 34, 38 */
    s32 sprA = 1, sprB = 1, ovlStep = 1;                /* local_3c, 40, 44 */
    s32 bobA = 0, bobB = 0, wait = 0, played0 = 0;      /* local_f4, 4c, 58, 128 */
    ig_LogoShown = 0;
    for (int i = 0; i < 0x180; i++) ig_TileMap[i] = i;

    const s32 cityY = 0x9E;                             /* local_120 */
    draw_bands(7, pageOfs);                             /* bands 0..6 (band 7 is not drawn here) */
    for (int i = 0; i < CITY_POINTS; i++) ig_CityPoints[i * 3 + 1] -= 2;
    for (int i = 0; i < CITY_POINTS; i++)
        if (ig_CityPoints[i * 3] < 0x170)
            I_Video_PlotUnder((u32)(scroll + ig_CityPoints[i * 3]), cityY + ig_CityPoints[i * 3 + 1],
                              ig_CityPoints[i * 3 + 2], pageOfs);
    I_Pal_Fade(0);
    Intro_PlaySample(6);
    I_CD_InitAndPlayTrack2();

    while (exitFlag == 0) {
        I_Video_FlipScroll(scroll, pageOfs == 0);       /* shows the page drawn last frame (Q4: page 1 first) */
        /* Mixer_Update (the four polls per frame): PORT, the SDL stream pulls the mixer. */
        c3++; c10++; cCred++; cStep++;
        if (c3 == 3) {
            c3 = 0;
            if (scroll < 15) scroll++;
            else { I_Tile_ScrollMapLeft(ig_TileMap); scroll = 0; }
        }
        if (c10 == 10) {
            c10 = 0;
            for (int i = 0; i < CITY_POINTS; i++) ig_CityPoints[i * 3]--;
            for (int i = 0; i < CITY_POINTS; i++) if (ig_CityPoints[i * 3] < 0) ig_CityPoints[i * 3] = 0x17F;
        }
        draw_bands(8, pageOfs);
        for (int i = 0; i < CITY_POINTS; i++)
            if (ig_CityPoints[i * 3] < 0x170)
                I_Video_PlotUnder((u32)(scroll + ig_CityPoints[i * 3]), cityY + ig_CityPoints[i * 3 + 1],
                                  ig_CityPoints[i * 3 + 2], pageOfs);
        if (ig_LogoShown != 0 && ig_FrameOverlayActive == 0) draw_logo(scroll, pageOfs);

        /* FRAME overlay (intro.md §4.4) */
        if (ig_FrameOverlayActive != 0) {
            if (cOvl < 3) cOvl++;
            else {
                cOvl = 0;
                ovlStep++;
                if (9 < ovlStep) ig_FrameOverlayActive = 0;
            }
            switch (ovlStep) {
            case 1: I_Spr_BlitClip(ig_FrameBuf[0], scroll, 0x6F, 0x140, 0x15, pageOfs); break;
            case 2: I_Spr_BlitClip(ig_FrameBuf[1], scroll, 0x67, 0x140, 0x28, pageOfs); break;
            case 3: I_Spr_BlitClip(ig_FrameBuf[2], scroll, 0x4C, 0x140, 0x56, pageOfs); break;
            case 4: I_Spr_BlitClip(ig_FrameBuf[3], scroll, 0x2F, 0x140, 0x97, pageOfs); break;
            case 5: I_Spr_BlitClip(ig_FrameBuf[4], scroll, 0x18, 0x140, 0xB4, pageOfs); break;
            case 6:
                draw_logo(scroll, pageOfs);
                I_Spr_BlitClip(ig_FrameBuf[5], scroll, 0x0C, 0x140, 0xCB, pageOfs);
                break;
            case 7:
                draw_logo(scroll, pageOfs);
                I_Spr_BlitClip(ig_FrameBuf[6], scroll, 6, 0x140, 0xD9, pageOfs);    /* 0x10f40 bytes (Q1) */
                break;
            case 8:
                draw_logo(scroll, pageOfs);
                I_Spr_BlitClip(ig_FrameBuf[7], scroll, 0, 0x140, 0xF0, pageOfs);
                break;
            default:
                draw_logo(scroll, pageOfs);
                ig_LogoShown = 1;
                Intro_SoundShutdown();
                break;
            }
        }

        /* Scenes (intro.md §4.3) */
        switch (ig_Scene) {
        case 1: {                                       /* two jets fly in */
            int r1 = I_rand(), r2 = I_rand();
            if (xA < ((r2 % 10) * (r1 % 0x16)) / 10 + 100) xA += 2;
            else if (played0 == 0) { Intro_PlaySample(0); played0 = 1; }
            r1 = I_rand(); r2 = I_rand();
            if (xB < ((r2 % 10) * (r1 % 0x16)) / 10 + 0x56) xB += 2;
            yA -= bobA;
            yB -= bobB;
            if (yA == 100) bobA = 0;
            if (yB == 0x90) bobB = 0;
            if (yA == 0x61) bobA = -1;
            if (yB == 0x8D) bobB = -1;
            if (I_rand() % 0x16 == 1 && yA == 100) bobA = 1;
            if (I_rand() % 0x16 == 1 && yB == 0x90) bobB = 1;
            draw_sprite(1, scroll + xA, yA, pageOfs);
            draw_sprite(1, scroll + xB, yB, pageOfs);
            if (99 < xA && 0x55 < xB) wait++;
            if (0x96 < wait) {
                ig_Scene = 2;
                vxA = 1; vyA = 1; wait = 0; vxB = 1; vyB = 1; l5c = 0; cStep = 0;
            }
            break;
        }
        case 2:                                         /* pull up and away */
            if (played1 == 0) { Intro_PlaySample(1); played1 = 1; }
            if (cStep < 3 && wait == 0) {
                draw_sprite(sprA, scroll + xA, yA, pageOfs);
                draw_sprite(sprB, scroll + xB, yB, pageOfs);
            } else {
                cStep = 0;
                xA -= vxA; yA -= vyA; xB -= vxB; yB -= vyB;
                sprA = sprA + 1 < 0x16 ? sprA + 1 : 0x15;
                if (4 < sprA) sprB = sprB + 1 < 0x16 ? sprB + 1 : 0x15;
                if (4 < sprA) { vxA++; vxB++; }
                if (vyA < 5) vyA++;
                if (4 < sprA && vyB < 5) vyB++;
                if (sprA < 0x14) {
                    draw_sprite(sprA, scroll + xA, yA, pageOfs);
                    draw_sprite(sprB, scroll + xB, yB, pageOfs);
                } else {
                    wait++;
                    if (0x32 < wait) {
                        sprA = 0x17; xA = -96; yA = 100; vyA = 0; ig_Scene = 3; l5c = 0; wait = 0;
                    }
                    cStep = 0;
                }
            }
            break;
        case 3:                                         /* one jet banks towards the viewer */
            if (played2 == 0) { played2 = 1; Intro_PlaySample(2); }
            if (cStep < 1 && wait == 0) {
                draw_sprite(sprA, scroll + xA, yA, pageOfs);
            } else {
                cStep = 0;
                xA += 3;
                if (0x1E < sprA && 0x50 < xA) yA--;
                l5c = xA % 5 != 1;
                if (l5c == 0 && 0 < xA) sprA = sprA + 1 < 0x23 ? sprA + 1 : 0x22;
                if (300 < xA && played3 == 0) { played3 = 1; Intro_PlaySample(3); }
                if (xA < 0x141) draw_sprite(sprA, scroll + xA, yA, pageOfs);
                else {
                    wait++;
                    if (0x50 < wait) { ig_Scene = 4; cStep = 0; wait = 0; xA = -96; yA = 100; sprA = 1; }
                }
            }
            break;
        case 4:                                         /* fast pass */
            if (played4 == 0 && 200 < xA) { Intro_PlaySample(4); played4 = 1; }
            if (cStep < 1 && wait == 0) {
                draw_sprite(sprA, scroll + xA, yA, pageOfs);
            } else {
                cStep = 0;
                xA += 4;
                if (xA < 0x141) draw_sprite(sprA, scroll + xA, yA, pageOfs);
                else {
                    wait++;
                    if (0x1E < wait) {
                        ig_Scene = 5; wait = 0; sprA = 0x33; xA = 0; yA = 100; smoke.n = 0;
                        for (int i = 0; i < 100; i++) { smoke.age[i] = 0; smoke.y[i] = 0; smoke.spr[i] = 0; }
                    }
                }
            }
            break;
        case 5:                                         /* small jet with a smoke trail */
            if (played5 == 0) { Intro_PlaySample(5); played5 = 1; }
            if (cStep < 1 && wait == 0) {
                draw_sprite(sprA, scroll + xA, yA, pageOfs);
                if (emit != 0 && 0 < (s32)smoke.n) smoke_draw(&smoke, scroll, pageOfs);
            } else {
                cStep = 0;
                xA += 4;
                if (100 < xA && emit == 0) { emit = 1; ey = yA + 6; smokeSpr = 0; ex = xA; }
                if (emit != 0)
                    for (int k = 0; k < 4; k++) { ex += 4; smoke_add(&smoke, ex, ey, smokeSpr + 0x44); }
                off = 0x140 < xA;
                if (xA < 0x191) {
                    draw_sprite(sprA, scroll + xA, yA, pageOfs);
                    if (emit != 0 && 0 < (s32)smoke.n) smoke_update(&smoke, off, scroll, pageOfs);
                } else {
                    wait++;
                    if (0x32 < wait) {
                        ig_Scene = 6; wait = 0; xA = 0; yA = 0xBF; vx6 = 0x10; vyA = 0; sprA = 0x23;
                        tx = 0x18; ty = 0x20; smoke.n = 0; emit = 0;
                    }
                    cStep = 0;
                }
            }
            break;
        case 6:                                         /* a jet climbs out of the city */
            if (cStep < 2 && wait == 0) {
                draw_sprite(sprA, scroll + xA, yA, pageOfs);
                if (0 < (s32)smoke.n) smoke_draw(&smoke, scroll, pageOfs);
            } else {
                cStep = 0;
                s32 nx = xA + vx6;
                vx6 = vx6 - 1 < 0 ? 0 : vx6 - 1;
                s32 ny = yA + vyA;
                vyA = ny < 0x96 ? vyA - 2 : vyA - 1;
                s32 spr = sprA + 1;
                if (spr == 0x32) {                      /* -> scene 7: FRAME overlay, credit timer restarted */
                    wait = 0; ovlStep = 1; ig_LogoShown = 0; cCred = 0; ig_Scene = 7; ig_FrameOverlayActive = 1;
                    cOvl = 0;
                    l78 = xB; l84 = 4; l88 = 2; l8c = 2; l90 = 0; l94 = 0; l98 = 0;
                    xA = -0x40; yA = 0x50;
                    l7c = ny; l74 = nx; sprA = spr; yB = ny;
                } else {
                    sprA = spr; yA = ny; xA = nx;
                    draw_sprite(spr, scroll + nx, ny, pageOfs);
                }
                s32 dx = (xA + tx) - ex, dy = (yA + ty) - ey;
                smokeSpr = (sprA - 0x22) / 3 < 4 ? (sprA - 0x22) / 3 : 3;
                s32 x0 = ex, y0 = ey;
                for (int k = 1; k < 5; k++) {
                    ex = x0 + (dx * k) / 4;
                    ey = y0 + (dy * k) / 4;
                    smoke_add(&smoke, ex, ey, smokeSpr + 0x44);
                }
                if (0 < (s32)smoke.n) smoke_update(&smoke, off, scroll, pageOfs);
            }
            break;
        case 7:                                         /* the jet crosses with the logo explosion */
            if (cStep < 2 && wait == 0) {
                draw_sprite(sprA, scroll + xA, yA, pageOfs);
            } else {
                cStep = 0;
                /* dead bookkeeping (never drawn) */
                if (l74 != 0) {
                    l94 = l94 + 1 < 0xD ? l94 + 1 : 0xC;
                    l74 += l84;
                    l7c += l8c;
                    if (l74 < -0x20) l74 = 0;
                }
                if (l78 != 0) {
                    l78 += l88; l88++;
                    l80 += l90; l90++;
                    l98 = l98 + 1 < 0xD ? l98 + 1 : 0xC;
                    if (0x140 < l78) l78 = 0;
                }
                xA += 3;
                if (I_rand() % 3 == 1) {
                    if (sprA < 0x32) sprA = 0x32;
                    else if (sprA < 0x41) sprA++;
                }
                if (wait == 0) draw_sprite(sprA, scroll + xA, yA, pageOfs);
                if (0x140 < xA && (wait++, 0x32 < wait)) {
                    ig_Scene = 1; wait = 0; xA = -96; yA = 100; xB = -0x78; yB = 0x90;
                    sprA = 1; sprB = 1; bobA = 0; bobB = 0;
                }
            }
            break;
        default:
            break;
        }

        /* Credits (intro.md §4.5): 100 frames shown (the counter also counts at the top), 41 blank */
        if (credOn == 0) {
            if (0x28 < cCred) {
                if (10 < ++ig_CreditIndex) ig_CreditIndex = 0;
                cCred = 0;
                credOn = 1;
            }
        } else if (cCred < 200) {
            Intro_DrawCredits(scroll, pageOfs);
            cCred++;
        } else {
            cCred = 0;
            credOn = 0;
        }
        pageOfs = pageOfs == 0 ? PAGE_BYTES : 0;
        if (Intro_ReadKey() == -1) exitFlag = 1;        /* only Esc / D released (Q8: kept) */
    }
    if (exitFlag != 0) {
        Intro_SoundShutdown();
        I_CD_Stop();
    }
    I_Pal_Fade(1);
    return 1;
}

void Intro_Run(void)
{
    Iseg_Load();
    Intro_Main();
    /* PORT: the end of the INTRO.EXE process (the original returns from main without Intro_Cleanup: no text
     * mode, INT 9 not restored by the code; DOS/4GW frees the memory): the keyboard hook goes, the audio
     * device closes (the samples and the CD are already stopped), the buffers are freed. The game's main
     * sets up its own mode X, keyboard and sound. */
    host_set_kbd_handler(NULL);
    host_audio_close();
    free(ig_SamBuf);
    free(ig_RawBuf);
    free(ig_BigFont);
    free(ig_SmallFontBuf);
    free(ig_TilBuf);
    free(ig_Char1Buf);
    free(ig_AosetBuf);
    for (int k = 0; k < 6; k++) free(ig_FrameBuf[k]);
    free(s_Frame78Block);
    ig_SamBuf = ig_RawBuf = ig_BigFont = ig_SmallFontBuf = ig_TilBuf = ig_Char1Buf = ig_AosetBuf = s_Frame78Block = NULL;
    memset(ig_FrameBuf, 0, sizeof ig_FrameBuf);
}
