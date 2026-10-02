/* Video subsystem: port/spec/video.md (§0 hardware model, §3 low-level routines, §10 port design). */
#include "video.h"

#include <string.h>

#include "host.h"

u8 vram[VRAM_SIZE];
u8 g_Palette[768];
u8 dac[768];
Crtc crtc = { 0, 0, 240 };
u8 g_PicFullLoad;
s16 g_ShownPage;

/* PORT: what the monitor shows at a retrace (video.md §10): 240 lines from the CRTC start with pel
 * panning, from the line-compare line on VRAM offset 0 without panning (attribute mode 0x61). The CRTC
 * address counter wraps at 64 KiB (= 0x40000 pixels). */
static void compose(u32 *out)
{
    u32 rgb[256];
    for (int i = 0; i < 256; i++) {
        u32 r = dac[3 * i] & 63, g = dac[3 * i + 1] & 63, b = dac[3 * i + 2] & 63;
        rgb[i] = (r << 2 | r >> 4) << 16 | (g << 2 | g >> 4) << 8 | (b << 2 | b >> 4);
    }
    for (int r = 0; r < HOST_FRAME_H; r++) {
        s32 base = r < crtc.split_rows ? (s32)crtc.start * 4 + r * VRAM_ROW + crtc.pan
                                       : (r - crtc.split_rows) * VRAM_ROW;
        for (int c = 0; c < HOST_FRAME_W; c++) out[r * HOST_FRAME_W + c] = rgb[vram[(base + c) & (VRAM_SIZE - 1)]];
    }
}

void Video_Init(void) { host_set_frame_source(compose); }

/* 0x10010 Video_SetModeX: mode 13h, DAC black, unchained 320x240 60 Hz, 96-byte rows, all VRAM cleared. */
void Video_SetModeX(void)
{
    memset(dac, 0, sizeof dac);
    memset(vram, 0, sizeof vram);
    crtc.start = 0;
    crtc.pan = 0;
    crtc.split_rows = 240;                      /* CRTC 18h from mode 13h: never matches */
}

/* 0x100c2 Video_SetTextMode: INT 10h on the way out; nothing to do. */
void Video_SetTextMode(void) {}

/* 0x100d0 Video_PutPixel: absolute VRAM coordinates. */
void Video_PutPixel(u32 x, s32 y, u8 col) { vput(VIDX(0, x, y), col); }

/* 0x10108 Video_WaitVSync: wait for the start of the next retrace (the port presents the frame). */
int Video_WaitVSync(void)
{
    host_wait_vretrace();
    return 0;
}

/* 0x105ef Video_SetSplitLine: line compare = rows*2 - 1 (10 bits; double scan). */
void Video_SetSplitLine(s16 rows)
{
    u16 lc = (u16)((rows * 2 - 1) & 0x3FF);
    crtc.split_rows = lc < 480 ? (u16)((lc + 1) / 2) : 240;
}

/* 0x1063a Video_BlitLinearToPlanar: plane by plane from plane x&3; the source advances 4 bytes per
 * destination byte and is not re-aligned per row. Absolute rows (no page argument). */
void Video_BlitLinearToPlanar(const u8 *src, u32 x, s32 y, u32 w, s32 h)
{
    u32 w4 = w >> 2;
    for (u32 p = 0; p < 4; p++)
        for (s32 row = 0; row < h; row++)
            for (u32 c = 0; c < w4; c++)
                vput((y + row) * VRAM_ROW + (s32)x + (s32)(4 * c + p), src[p + 4 * ((u32)row * w4 + c)]);
}

/* 0x106b0 Video_SetStartAndPan: CRTC start = y*96 + (x>>2) + base, one retrace wait, then pan. */
void Video_SetStartAndPan(u32 x, s32 y, s32 base)
{
    crtc.start = (u16)(y * 96 + (s32)(x >> 2) + base);
    host_wait_vretrace();                       /* PORT: the start address latches at the retrace */
    crtc.pan = (u8)(x & 3);
}

/* 0x12e92 Video_ShowPage: CRTC start = p * 0x5a00 (pan unchanged), no wait. */
void Video_ShowPage(int p)
{
    crtc.start = (u16)(p * PAGE_BYTES);
    g_ShownPage = (s16)p;
}

/* 0x12ef0 Video_GetPage */
int Video_GetPage(void) { return g_ShownPage; }

/* 0x1195b Pal_Black: DAC entries [first, last) := 0; g_Palette unchanged. */
void Pal_Black(int first, int last)
{
    for (int i = first; i < last; i++) dac[3 * i] = dac[3 * i + 1] = dac[3 * i + 2] = 0;
}

/* 0x119b6 Pal_Upload: DAC [first, last) := g_Palette. */
void Pal_Upload(int first, int last)
{
    for (int i = first; i < last; i++) {
        dac[3 * i] = g_Palette[3 * i];
        dac[3 * i + 1] = g_Palette[3 * i + 1];
        dac[3 * i + 2] = g_Palette[3 * i + 2];
    }
}

/* 0x120ea Pal_UploadAll */
void Pal_UploadAll(void) { Pal_Upload(0, 256); }
