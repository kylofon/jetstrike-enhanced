#pragma once
/* Video subsystem (port/spec/video.md): the mode X model and the low-level routines.
 *
 * VRAM: 4 planes x 64 KiB as one linear buffer, pixel index = byte offset * 4 + plane, so pixel x+1 is
 * always index + 1 (video.md §0). 96 bytes = 384 pixels per row. Writes outside the buffer are dropped
 * (they did not reach VRAM on the PC). */
#include "types.h"

#define VRAM_SIZE  0x40000
#define VRAM_ROW   384                          /* pixels per row (96 bytes x 4 planes) */
#define PAGE_BYTES 0x5A00                       /* one 240-row picture page, in byte offsets */
#define VIDX(pageofs, x, y) ((s32)(pageofs) * 4 + (s32)(y) * VRAM_ROW + (s32)(x))

extern u8 vram[VRAM_SIZE];
extern u8 g_Palette[768];                       /* 0x82ED8: 6-bit shadow palette */
extern u8 dac[768];                             /* the hardware DAC (changes only on uploads) */

typedef struct {
    u16 start;                                  /* CRTC start address (byte offset) */
    u8  pan;                                    /* attribute 13h pel panning, pixels 0..3 */
    u16 split_rows;                             /* line compare: first HUD line; 240 = off */
} Crtc;
extern Crtc crtc;

extern u8  g_PicFullLoad;                       /* 0x8004B */
extern s16 g_ShownPage;                         /* 0x8452E */

static inline void vput(s32 i, u8 c) { if ((u32)i < VRAM_SIZE) vram[i] = c; }
static inline u8 vget(s32 i) { return (u32)i < VRAM_SIZE ? vram[i] : 0; }

void Video_Init(void);                          /* PORT: installs the frame source (host) */
void Video_SetModeX(void);                      /* 0x10010 */
void Video_SetTextMode(void);                   /* 0x100c2 */
void Video_PutPixel(u32 x, s32 y, u8 col);      /* 0x100d0 */
int  Video_WaitVSync(void);                     /* 0x10108 */
void Video_SetSplitLine(s16 rows);              /* 0x105ef */
void Video_BlitLinearToPlanar(const u8 *src, u32 x, s32 y, u32 w, s32 h);   /* 0x1063a */
void Video_SetStartAndPan(u32 x, s32 y, s32 base);                          /* 0x106b0 */
void Video_ShowPage(int p);                     /* 0x12e92 */
int  Video_GetPage(void);                       /* 0x12ef0 */
void Pal_Black(int first, int last);            /* 0x1195b */
void Pal_Upload(int first, int last);           /* 0x119b6 */
void Pal_UploadAll(void);                       /* 0x120ea */
