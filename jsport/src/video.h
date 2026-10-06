#pragma once
/* Video subsystem (port/spec/video.md): the mode X model, palette, blitters, sprite bank and queue, level
 * renderer, text, primitives.
 *
 * VRAM: 4 planes x 64 KiB as one linear buffer, pixel index = byte offset * 4 + plane, so pixel x+1 is
 * always index + 1 (video.md §0). 96 bytes = 384 pixels per row. Writes outside the buffer are dropped
 * (they did not reach VRAM on the PC). Globals live in the data-segment image at their original addresses.
 *
 * ENH: the layout is a run-time value (PLAN.md §1). The game runs on the layout of its view (--view WxH, the
 * whole mission screen including the HUD); the intro and the 320x240 view use the original one. The model
 * stays linear: rows of view_w + 64 pixels (the 64 is the sprite wrap margin), the HUD at offset 0, two play
 * pages after it, the HUD save area after them, the whole buffer a power of two (the CRTC address wraps
 * there). At 320x240 every value below is the original one. */
#include "dseg.h"

typedef struct {
    int view_w, view_h;                         /* the mission screen, pixels (320 x 240) */
    int split;                                  /* first HUD line = playfield height (175 = 0xAF) */
    s32 stride;                                 /* VRAM row, pixels (384) */
    s32 row;                                    /* VRAM row, byte offsets (96) */
    s32 size;                                   /* VRAM, pixels (0x40000) */
    int tile_cols, tile_rows;                   /* tiles drawn per play page: the stride x the playfield (24 x 13) */
    s32 page_rows;                              /* rows per play page (256) */
    s32 page_a, page_b;                         /* play page bases, byte offsets (0x18C0, 0x78C0) */
    s32 save_row;                               /* HUD save area (radar, altimeter), absolute row (0x246) */
} VideoLayout;
extern VideoLayout vl;

#define VIEW_MIN_W 320                          /* --view limits (PLAN.md ground rules) */
#define VIEW_MAX_W 960
#define VIEW_MIN_H 240
#define VIEW_MAX_H 540
#define HUD_ROWS   0x42                         /* display.pax rows at the top of VRAM (65 of them shown) */

#define VRAM_SIZE  (vl.size)
#define VRAM_ROW   (vl.stride)                  /* pixels per row */
#define VRAM_ROWB  (vl.row)                     /* byte offsets per row */
#define PAGE_BYTES (240 * vl.row)               /* one 240-row picture page, in byte offsets (0x5A00) */
#define VIDX(pageofs, x, y) ((s32)(pageofs) * 4 + (s32)(y) * VRAM_ROW + (s32)(x))

extern u8 *vram;                                /* VRAM_SIZE bytes (allocated for the largest layout) */
extern u8 dac[768];                             /* the hardware DAC (changes only on uploads) */

typedef struct {
    u32 start;                                  /* CRTC start address (byte offset; wraps at VRAM_SIZE / 4) */
    u8  pan;                                    /* attribute 13h pel panning, pixels 0..3 */
    u16 split_rows;                             /* line compare: first HUD line; >= the screen height = off */
} Crtc;
extern Crtc crtc;

static inline void vput(s32 i, u8 c) { if ((u32)i < (u32)VRAM_SIZE) vram[i] = c; }
static inline u8 vget(s32 i) { return (u32)i < (u32)VRAM_SIZE ? vram[i] : 0; }

/* ENH: view setup. Video_CheckView returns NULL when w x h is a valid view, else the reason. Video_Init
 * allocates VRAM for that view and installs the frame source. Video_UseLayout(false) selects the original
 * 320x240 layout (the intro), true the layout of the view (the game, from Video_SetModeX on). */
const char *Video_CheckView(int w, int h);
void Video_UseLayout(bool view);

/* ---- Globals (video.md §1) */
#define g_SpriteColorOfs   D8(0x80008)
extern u8 *g_SmallFont;                          /* 0x80009 */
extern u8 *g_BigFont;                            /* 0x8000D */
#define g_SmallCharset     ((const char *)DSEG_PTR(D32(0x80011)))   /* -> 0x80BF0 */
#define g_BigCharset       ((const char *)DSEG_PTR(D32(0x80015)))   /* -> 0x80C1C */
#define g_DigitFont        DSEG(0x80019)
#define g_PicFullLoad      D8(0x8004B)          /* image value 1 */
#define g_FadeTargetColor  D8(0x8004F)
extern u8 *g_ParallaxBuf;                        /* 0x80050, malloc(0x280C8) */
#define PARALLAX_BYTES     0x280C8
#define g_VSyncWaits       DS32(0x80054)
extern u8 *g_TileData;                           /* 0x80058, malloc(0x10000) */
#define g_BackPage         DS32(0x8005C)        /* image value 0x18C0 */
#define g_LastFadeType     D32(0x8006C)
#define g_NightPalActive   DS32(0x80070)        /* = g_FogActive of game_flow.md */
#define g_Palette          DSEG(0x82ED8)        /* [768] 6-bit shadow palette */
/* g_TileWindow (0x831D8, [16][24]): ENH: moved out of the image (video.c tile_window, sized by the view) */
#define SPRITE_TAB_SIZE    0x800
extern u8 *g_SpriteTab[SPRITE_TAB_SIZE];         /* 0x83A54: 1-based sprite headers */
#define g_SpriteQueue      DSEG(0x81AD8)        /* 20-byte entries, no bound: entry 256 = g_Palette */
#define g_SpriteQueueCount DS32(0x84500)
#define g_LineColor        DS32(0x84504)
#define g_ShownPage        DS16(0x8452E)
extern u8 *g_TilePtrs[256];                      /* 0x845B8 */
#define g_FlashCounter     DS32(0x9031C)
#define g_NightLevel       DS32(0x90650)
#define g_NightLevelShown  DS32(0x9065C)
#define g_ScrollFineX      DS32(0x905C4)
#define g_ScrollFineY      DS32(0x905C8)
#define g_FlashTable       DS32A(0x90FE4)
#define g_ParallaxPal      DSEG(0x92AD4)
#define g_PalSaved         DSEG(0x933B4)
#define g_NightGreyTab     DS32A(0x8EC88)
#define g_NightMission     DS32(0x9035C)
#define g_CamX             DS32(0x903A8)
#define g_CamY             DS32(0x903AC)
/* ENH: playfield rows beyond the original 175 (0 at 320x240). The lowest camera (0x340) puts the map bottom at the
 * bottom of a 175-row playfield; a taller view lowers it by the extra rows so it never shows below the map, and the
 * player's screen-Y values tied to that camera (box, ground line) move down by the same amount. */
#define VIEW_EXTRA_ROWS    (vl.split - 175)
#define CAM_Y_MAX          (0x340 - VIEW_EXTRA_ROWS)
#define g_MapWidth         DS32(0x8FE90)
extern u8 *g_MapGrid;                            /* 0x849D0 (level.c) */

/* ---- Mode X, pages (§3, §4) */
void Video_Init(int view_w, int view_h);        /* PORT: installs the frame source (host); ENH: the view */
void Video_SetModeX(void);                      /* 0x10010 */
void Video_SetTextMode(void);                   /* 0x100c2 */
void Video_PutPixel(u32 x, s32 y, u8 col);      /* 0x100d0 */
int  Video_WaitVSync(void);                     /* 0x10108 */
void Video_SelectPlane(int plane);              /* 0x105d9 */
void Video_ClassicScreen(bool on);             /* ENH: show the 320x240 screen (HUD split at 175) while in a mission view */
void Video_SetSplitLine(s16 rows);              /* 0x105ef */
void Video_BlitLinearToPlanar(const u8 *src, u32 x, s32 y, u32 w, s32 h);   /* 0x1063a */
void Video_SetStartAndPan(u32 x, s32 y, s32 base);                          /* 0x106b0 */
int  Video_ReadPixel(s32 x, s32 y, s32 base);   /* 0x106fa */
void Video_FlipPage(void);                      /* 0x13a68 */
void Video_ShowPage(int p);                     /* 0x12e92 */
int  Video_GetPage(void);                       /* 0x12ef0 */
void Pic_LoadHudPanel(const char *name);        /* 0x130c8 */

/* ---- Blitters, sprites (§3, §5) */
void Sprite_Blit(u32 x, u32 y, const u8 *s, s32 pageofs);        /* 0x10128 */
void Sprite_BlitShift(u32 x, u32 y, const u8 *s, s32 pageofs);   /* 0x101c8 */
void Sprite_BlitMirror(s32 x, s32 y, const u8 *s, s32 pageofs);  /* 0x1026c */
int  Sprites_LoadSpx(const char *name);         /* 0x11306 */
void Sprites_ReplaceFromBank(int slot, int n, const u8 *bank);   /* 0x12f67 */
int  Sprites_CountInBank(const u8 *bank, int size);              /* 0x13773 */
void Sprite_ClearQueue(void);                   /* 0x10e70 */
int  Sprite_Mirror(int id);                     /* 0x1206e */
void Sprite_Queue(s32 x, s32 y, s32 id);        /* 0x10f11 */
void Sprite_DrawQueue(void);                    /* 0x1155e */
void Sprite_DrawNow(u32 x, u32 y, int id);      /* 0x1102d */
void Sprite_DrawNowShift(u32 x, u32 y, int id); /* 0x11066 */
u32  Sprite_GetX(int id);                       /* 0x11847 */
u32  Sprite_GetY(int id);                       /* 0x117f8 */
u32  Sprite_GetWidth(int id);                   /* 0x11895 */
u32  Sprite_GetHeight(int id);                  /* 0x118e7 */

/* ---- Level renderer (§3, §6) */
void Tiles_DrawColumns(const u8 *ids, u8 *const *tileptrs, int pagesel, int plane);           /* 0x10540 */
void Tiles_DrawColumnsParallax(const u8 *ids, u8 *const *tileptrs, int pagesel, int plane,
                               const u8 *par, s32 parofs, s32 px);                           /* 0x1040c */
void Level_DrawBackground(s32 col, s32 row);    /* 0x13842 */

/* ---- Palette (§7) */
void Pal_SetColor(int idx, int r, int g, int b);            /* 0x11145 */
void Pal_SetColorNoUpload(int idx, int r, int g, int b);    /* 0x1119e */
void Pal_SetColor12(int idx, u32 rgb);                      /* 0x111e6 */
u32  Pal_GetColor12(int idx);                               /* 0x1125c */
void Pal_Black(int first, int last);            /* 0x1195b */
void Pal_Upload(int first, int last);           /* 0x119b6 */
void Pal_UploadAll(void);                       /* 0x120ea */
void Pal_Fade(int first, int last, u32 type, int n);        /* 0x13b01 */
void Pal_CycleEffects(void);                    /* 0x45c09 */
void Pal_NightAltitude(void);                   /* 0x460ba */
void Pal_SaveNight(void);                       /* 0x46617 */
void Pal_Restore(void);                         /* 0x4699e */

/* ---- Text (§8) */
void Fonts_Load(void);                          /* 0x114da */
int  Font_DrawSmallGlyph(const u8 *g, int x, int y, int rows);              /* 0x12343 */
int  Font_DrawBigGlyph(const u8 *g, int x, int y, int w, int rows);         /* 0x123fa */
int  Font_GlyphWidth(const u8 *g, int rows);    /* 0x12970 */
void Text_DrawSmall(int x, int y, const char *s, int onPage);               /* 0x124e0 */
void Text_DrawBig(int x, int y, const char *s, int onPage);                 /* 0x126e9 */
int  Text_WidthSmall(const char *s);            /* 0x12bed */
int  Text_WidthBig(const char *s);              /* 0x12d8d */
int  Text_FitWidth(const char *s, int maxw);    /* 0x129ee */
void Text_DrawDigit(int x, int y, int d);       /* 0x12831 */
void Text_DrawNumber(int x, int y, int val, int n);                         /* 0x128f1 */

/* ---- Primitives (§9) */
void Video_SetLineColor(int c);                 /* 0x143b0 */
void Video_DrawLineColor(u32 x1, u32 y1, u32 x2, u32 y2);                  /* 0x13041 */
void Video_DrawLine(u32 x1, u32 y1, u32 x2, u32 y2, u8 col);                /* 0x31604 */
void Video_FillRect(int x1, int y1, int x2, int y2, u8 col);               /* 0x1334f */
void Video_CopyRect(int srcPage, int x1, int y1, int x2, int y2, int dstPage, int dx, int dy);   /* 0x11bee */
