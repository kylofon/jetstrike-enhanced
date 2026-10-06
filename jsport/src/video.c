/* Video subsystem: port/spec/video.md (§0 hardware model, §3 low-level routines, §4 pages, §5 sprites,
 * §6 level renderer, §7 palette, §8 text, §9 primitives, §10 port design). */
#include "video.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"
#include "host.h"
#include "lzw.h"
#include "platform.h"

u8 *vram;
u8 dac[768];
Crtc crtc = { 0, 0, 240 };
VideoLayout vl;

u8 *g_SmallFont;
u8 *g_BigFont;
u8 *g_ParallaxBuf;
u8 *g_TileData;
u8 *g_SpriteTab[SPRITE_TAB_SIZE];
u8 *g_TilePtrs[256];
static u8 *g_SpriteBank;                        /* 0x8450C: the unpacked JETSTRIK.SPX */

/* ENH: the view and its layout (video.h). */
static VideoLayout layout_classic, layout_view;
static bool mission_view;                       /* the screen is the view (from Pic_LoadHudPanel), else 320x240 */
static bool classic_screen;                     /* ENH: a front-end style screen in a mission (Video_ClassicScreen): 320x240 */
/* ENH: g_TileWindow (0x831D8, [16][24], 13 rows drawn), out of the image: vl.tile_rows x vl.tile_cols ids. */
static u8 tile_window[((VIEW_MAX_H - 65 + 46) / 16) * ((VIEW_MAX_W + 64) / 16)];

/* The VRAM layout of a w x h view. Play page: the 16-row tile margin above the playfield, up to 15 rows of fine
 * scroll, the playfield, rounded up to whole tile rows (13 at 320x240), then 48 rows for sprites below it. */
static VideoLayout layout_for(int w, int h)
{
    VideoLayout l;
    l.view_w = w;
    l.view_h = h;
    l.split = h - 65;
    l.stride = w + 64;
    l.row = l.stride / 4;
    l.tile_cols = l.stride / 16;
    l.tile_rows = (16 + 15 + l.split + 15) / 16;
    l.page_rows = l.tile_rows * 16 + 48;
    l.page_a = HUD_ROWS * l.row;
    l.page_b = (HUD_ROWS + l.page_rows) * l.row;
    l.save_row = HUD_ROWS + 2 * l.page_rows + 4;
    s32 rows = l.save_row + 0x24;               /* the save area: rows 0x246..0x269 at 320x240 */
    if (rows < 0x40000 / 384) rows = 0x40000 / 384;    /* the front end keeps the 682 rows of the original */
    l.size = 0x40000;
    while (l.size < rows * l.stride) l.size <<= 1;
    return l;
}

const char *Video_CheckView(int w, int h)
{
    if (w < VIEW_MIN_W || w > VIEW_MAX_W) return "width must be 320..960";
    if (w % 16) return "width must be a multiple of 16";
    if (h < VIEW_MIN_H || h > VIEW_MAX_H) return "height must be 240..540";
    return NULL;
}

void Video_ClassicScreen(bool on) { classic_screen = on; }

void Video_UseLayout(bool view) { vl = view ? layout_view : layout_classic; }

/* PORT: what the monitor shows at a retrace (video.md §10): 240 lines from the CRTC start with pel
 * panning, from the line-compare line on VRAM offset 0 without panning (attribute mode 0x61). The CRTC
 * address counter wraps at 64 KiB (= 0x40000 pixels). ENH: in a mission the screen is the view (the CRTC
 * wraps at VRAM_SIZE); elsewhere it stays 320x240. */
static void compose(u32 *out, int *w, int *h)
{
    u32 rgb[256];
    for (int i = 0; i < 256; i++) {
        u32 r = dac[3 * i] & 63, g = dac[3 * i + 1] & 63, b = dac[3 * i + 2] & 63;
        rgb[i] = (r << 2 | r >> 4) << 16 | (g << 2 | g >> 4) << 8 | (b << 2 | b >> 4);
    }
    bool wide = mission_view && !classic_screen;
    int sw = wide ? vl.view_w : 320, sh = wide ? vl.view_h : 240;
    int split = classic_screen ? 175 : crtc.split_rows;     /* the original HUD split */
    u32 mask = (u32)VRAM_SIZE - 1;
    int hud_x = wide ? (sw - 320) / 2 : 0;      /* ENH: the 320-wide HUD panel is centred, colour 0 at its sides */
    *w = sw;
    *h = sh;
    for (int r = 0; r < sh; r++) {
        if (r >= split && hud_x > 0) {
            u32 base = (u32)((r - split) * VRAM_ROW);
            for (int c = 0; c < sw; c++)
                out[r * sw + c] = rgb[(c >= hud_x && c < hud_x + 320) ? vram[(base + (u32)(c - hud_x)) & mask] : 0];
            continue;
        }
        u32 base = r < split ? crtc.start * 4 + (u32)(r * VRAM_ROW) + crtc.pan
                                       : (u32)((r - split) * VRAM_ROW);
        for (int c = 0; c < sw; c++) out[r * sw + c] = rgb[vram[(base + (u32)c) & mask]];
    }
}

void Video_Init(int view_w, int view_h)
{
    layout_classic = layout_for(320, 240);
    layout_view = layout_for(view_w, view_h);
    s32 n = layout_view.size > layout_classic.size ? layout_view.size : layout_classic.size;
    vram = calloc((size_t)n, 1);
    if (!vram) host_fatal_code(2, "out of memory (VRAM)");
    Video_UseLayout(false);
    host_set_frame_source(compose);
}

/* ================================================================ §3 low-level routines */

/* 0x10010 Video_SetModeX: mode 13h, DAC black, unchained 320x240 60 Hz, 96-byte rows, all VRAM cleared.
 * ENH: switches to the layout of the view (called once, right after the data-segment image is loaded) and
 * moves g_BackPage to its page A (the image value 0x18C0 is page A of the 320x240 layout). */
void Video_SetModeX(void)
{
    Video_UseLayout(true);
    memset(dac, 0, sizeof dac);
    memset(vram, 0, (size_t)VRAM_SIZE);
    crtc.start = 0;
    crtc.pan = 0;
    crtc.split_rows = 240;                      /* CRTC 18h from mode 13h: never matches */
    mission_view = false;
    g_BackPage = vl.page_a;
}

/* 0x100c2 Video_SetTextMode: INT 10h on the way out; nothing to do. */
void Video_SetTextMode(void) {}

/* 0x100d0 Video_PutPixel: absolute VRAM coordinates, byte y*96 + (x>>2) (x unsigned), plane x&3.
 * PORT: addresses outside the 64 KiB window are dropped (they were not VRAM on the PC). */
void Video_PutPixel(u32 x, s32 y, u8 col)
{
    int64_t off = (int64_t)y * VRAM_ROWB + (x >> 2);
    if (off >= 0 && off < VRAM_SIZE / 4) vram[off * 4 + (x & 3)] = col;
}

/* 0x10108 Video_WaitVSync: wait for the start of the next retrace (the port presents the frame). */
int Video_WaitVSync(void)
{
    host_wait_vretrace();
    return 0;
}

/* 0x10128 Sprite_Blit: hotspot guard (unsigned), 0x40 transparent, no clipping. */
void Sprite_Blit(u32 x, u32 y, const u8 *s, s32 pageofs)
{
    if (!s) return;                             /* PORT: an empty sprite slot (bogus pointer in the original) */
    u32 hx = (u32)(s[0] | s[1] << 8), hy = (u32)(s[2] | s[3] << 8);
    if (x < hx || y < hy) return;
    s32 dx = (s32)(x - hx), dy = (s32)(y - hy);
    int w4 = s[4], h = s[6];
    const u8 *src = s + 8;
    for (int p = 0; p < 4; p++)
        for (int r = 0; r < h; r++)
            for (int c = 0; c < w4; c++) {
                u8 v = *src++;
                if (v != 0x40) vput(VIDX(pageofs, dx + 4 * c + p, dy + r), v);
            }
}

/* 0x101c8 Sprite_BlitShift: as Sprite_Blit, stores pixel + 0x20. */
void Sprite_BlitShift(u32 x, u32 y, const u8 *s, s32 pageofs)
{
    if (!s) return;
    u32 hx = (u32)(s[0] | s[1] << 8), hy = (u32)(s[2] | s[3] << 8);
    if (x < hx || y < hy) return;
    s32 dx = (s32)(x - hx), dy = (s32)(y - hy);
    int w4 = s[4], h = s[6];
    const u8 *src = s + 8;
    for (int p = 0; p < 4; p++)
        for (int r = 0; r < h; r++)
            for (int c = 0; c < w4; c++) {
                u8 v = *src++;
                if (v != 0x40) vput(VIDX(pageofs, dx + 4 * c + p, dy + r), (u8)(v + 0x20));
            }
}

/* 0x1026c Sprite_BlitMirror: horizontal flip, no hotspot guard (video.md Q3). */
void Sprite_BlitMirror(s32 x, s32 y, const u8 *s, s32 pageofs)
{
    if (!s) return;
    s32 hx = s[0] | s[1] << 8, hy = s[2] | s[3] << 8;
    s32 w4full = s[4] | s[5] << 8;
    int w4 = s[4], h = s[6];
    s32 left = x - (w4full * 4 - hx), top = y - hy;
    const u8 *planes = s + 8;
    for (int p = 0; p < 4; p++)
        for (int r = 0; r < h; r++)
            for (int c = 0; c < w4; c++) {
                u8 v = planes[(3 - p) * h * w4 + r * w4 + (w4 - 1 - c)];
                if (v != 0x40) vput(VIDX(pageofs, left + 4 * c + p, top + r), v);
            }
}

/* Tile pixel (x, y) of a 16x16 tile: tile[(x&3)*64 + y*4 + (x>>2)] (video.md §3 Tiles_DrawColumns). */
static u8 tile_px(const u8 *t, int x, int y) { return t ? t[(x & 3) * 64 + y * 4 + (x >> 2)] : 0; }

/* Base of play page pagesel (0 = A, 8 = B: the original's 0x18C0 + pagesel * 0xC00). */
static s32 play_page(int pagesel) { return vl.page_a + pagesel / 8 * (vl.page_b - vl.page_a); }

/* 0x10540 Tiles_DrawColumns: 24 x 13 tiles, one plane, into 0x18C0 + pagesel*0xC00. ENH: vl.tile_cols x
 * vl.tile_rows (the whole stride, the playfield rows). */
void Tiles_DrawColumns(const u8 *ids, u8 *const *tileptrs, int pagesel, int plane)
{
    s32 base = play_page(pagesel);
    for (int k = 0; k < vl.tile_cols * vl.tile_rows; k++) {
        int tx = k % vl.tile_cols, ty = k / vl.tile_cols;
        const u8 *t = tileptrs[ids[k]];
        for (int r = 0; r < 16; r++)
            for (int j = 0; j < 4; j++)
                vput(VIDX(base, 16 * tx + 4 * j + plane, 16 * ty + r), tile_px(t, 4 * j + plane, r));
    }
}

/* PORT: backdrop reads outside the 0x280C8-byte buffer read 0 (video.md Q4: margins instead of heap). */
static u8 par_get(const u8 *par, s32 i) { return (par && i >= 0 && i < PARALLAX_BYTES) ? par[i] : 0; }

/* ENH: the backdrop pixel at line y, column x of the 320x512 picture. At 320 wide it stays linear (Q4: x past
 * 319 reads the next line); a wider view wraps x at 320 on the same line. At 240 high reads outside the buffer
 * read 0 (as above); a taller view repeats the top and bottom lines (it can show rows the original never did). */
static u8 par_px(const u8 *par, s32 y, s32 x)
{
    if (vl.view_w != 320) x = (x % 320 + 320) % 320;
    if (vl.view_h != 240) y = y < 0 ? 0 : y > 511 ? 511 : y;
    return par_get(par, y * 320 + x);
}

/* 0x1040c Tiles_DrawColumnsParallax (+ 0x104f8 Tiles_ParallaxRow): 22 x 13 tiles (2 ids skipped per row),
 * tile pixel 0x80 shows the backdrop byte (linear, no wrap at 320). ENH: vl.tile_cols - 2 x vl.tile_rows; arg 7
 * (unused in the original) is px, so the backdrop line is (parofs - px) / 320 and the column px + c (par_px). */
void Tiles_DrawColumnsParallax(const u8 *ids, u8 *const *tileptrs, int pagesel, int plane,
                               const u8 *par, s32 parofs, s32 px)
{
    s32 base = play_page(pagesel), py = (parofs - px) / 320;
    for (int ty = 0; ty < vl.tile_rows; ty++)
        for (int tx = 0; tx < vl.tile_cols - 2; tx++) {
            const u8 *t = tileptrs[ids[ty * vl.tile_cols + tx]];
            for (int r = 0; r < 16; r++)
                for (int j = 0; j < 4; j++) {
                    int c = 16 * tx + 4 * j + plane, row = 16 * ty + r;
                    u8 v = tile_px(t, 4 * j + plane, r);
                    if (v == 0x80) v = par_px(par, py + row, px + c);
                    vput(VIDX(base, c, row), v);
                }
        }
}

/* 0x105d9 Video_SelectPlane: map mask; implicit in the port. */
void Video_SelectPlane(int plane) { (void)plane; }

/* 0x105ef Video_SetSplitLine: line compare = rows*2 - 1 (10 bits; double scan). ENH: only the front end calls
 * it (400 = no split); the screen goes back to 320x240. The mission split is set by Pic_LoadHudPanel. */
void Video_SetSplitLine(s16 rows)
{
    u16 lc = (u16)((rows * 2 - 1) & 0x3FF);
    crtc.split_rows = lc < 480 ? (u16)((lc + 1) / 2) : 240;
    mission_view = false;
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
    crtc.start = (u32)(y * VRAM_ROWB + (s32)(x >> 2) + base);     /* ENH: no u16 cut (compose wraps) */
    host_wait_vretrace();                       /* PORT: the start address latches at the retrace */
    crtc.pan = (u8)(x & 3);
}

/* 0x106fa Video_ReadPixel: collision probe on the drawn page. */
int Video_ReadPixel(s32 x, s32 y, s32 base)
{
    u8 v = vget(VIDX(base, x, y));
    return (v >= 0x80 && v <= 0xC0) ? v - 0x80 : 0;
}

/* ================================================================ §4 pages */

/* 0x13a68 Video_FlipPage: shows the back page (1 + g_VSyncWaits retraces), swaps pages. */
void Video_FlipPage(void)
{
    Video_SetStartAndPan((u32)g_ScrollFineX, g_ScrollFineY, g_BackPage + 16 * VRAM_ROWB);    /* + 0x600 */
    for (int i = 0; i < g_VSyncWaits; i++) Video_WaitVSync();
    if (g_FlashCounter != 0) Pal_CycleEffects();
    g_BackPage = (g_BackPage == vl.page_a) ? vl.page_b : vl.page_a;
}

/* 0x12e92 Video_ShowPage: CRTC start = p * 0x5a00 (pan unchanged), no wait. */
void Video_ShowPage(int p)
{
    crtc.start = (u32)(p * PAGE_BYTES);
    g_ShownPage = (s16)p;
}

/* 0x12ef0 Video_GetPage */
int Video_GetPage(void) { return g_ShownPage; }

/* 0x130c8 Pic_LoadHudPanel(name): all VRAM cleared, gfx/<name> (LZW, 320x66) at offset 0, its .pal into
 * g_Palette 0..63 (no upload), split at 0xAF, CRTC start 0x1EC0. ENH: the split is the playfield height of
 * the view, the start the top of page A + 16 rows, and the screen becomes the view. */
void Pic_LoadHudPanel(const char *name)
{
    char path[100];
    strcpy(path, DSTR(0x80CC3));                /* "gfx/" */
    strcat(path, name);
    memset(vram, 0, (size_t)VRAM_SIZE);
    FILE *f = Platform_Fopen(path, DSTR(0x80C77));
    if (!f) FatalError(path, DSTR(0x80C8C), 1);
    u8 *pic = malloc(0x5280);
    if (!pic) FatalError(DSTR(0x80D52), path, 7);
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *packed = malloc((size_t)(len > 0 ? len : 1));
    if (!packed) FatalError(DSTR(0x80D52), path, 7);
    if (fread(packed, (size_t)len, 1, f) != 1) { /* not checked */ }
    if (LZW_PackedSize(packed) > 0x5280)
        FatalError(path, ": picture larger than its 0x5280-byte buffer (the original would overwrite memory)", 7);
    LZW_Unpack(packed, (u32)len, pic);
    free(packed);
    fclose(f);
    Video_BlitLinearToPlanar(pic, 0, 0, 320, 0x42);
    size_t n = strlen(path);
    memcpy(path + n - 4, DSTR(0x80CC8), 5);     /* ".pal" */
    f = Platform_Fopen(path, DSTR(0x80C77));
    if (!f) FatalError(path, DSTR(0x80CEB), 1);
    memset(pic, 0, 0xc0);
    if (fread(pic, 0xc0, 1, f) != 1) { /* not checked */ }
    fclose(f);
    memcpy(path + n - 4, DSTR(0x80D81), 5);     /* ".pax" again */
    for (int i = 0; i < 0x40; i++) SwapByte(&pic[i * 3], &pic[i * 3 + 2]);
    for (int i = 0; i < 0xc0; i++) g_Palette[i] = (u8)(pic[i] >> 2);
    crtc.split_rows = (u16)vl.split;            /* Video_SetSplitLine(0xAF) */
    mission_view = true;
    crtc.start = (u32)(vl.page_a + 16 * VRAM_ROWB);                     /* 0x1EC0 */
    free(pic);
}

/* ================================================================ §5 sprite bank and queue */

/* 0x11306 Sprites_LoadSpx: first call unpacks data/<name> into the bank (pixels + g_SpriteColorOfs) and
 * fills g_SpriteTab[1..]; later calls load data/jetsprit.pal into colours 64..95 and 96..127 (no upload). */
int Sprites_LoadSpx(const char *name)
{
    int r = 0;
    if (g_SpriteBank == NULL) {
        int packed = File_LoadWhole(DSTR(0x80C49), name, (void **)&g_PackBuf, 0);
        s32 size = (s32)LZW_PackedSize(g_PackBuf);
        g_SpriteBank = malloc((size_t)(size > 0 ? size : 1));
        if (!g_SpriteBank) FatalError(DSTR(0x80C52), DSTR(0x80C4F), 2);
        LZW_Unpack(g_PackBuf, (u32)packed, g_SpriteBank);
        s32 o = 0;
        int k = 0;
        while (o < size) {
            if (k + 1 < SPRITE_TAB_SIZE) g_SpriteTab[k + 1] = g_SpriteBank + o;
            u32 w4 = (u32)(g_SpriteBank[o + 4] | g_SpriteBank[o + 5] << 8);
            u32 h = (u32)(g_SpriteBank[o + 6] | g_SpriteBank[o + 7] << 8);
            o += 8;
            s32 n = (s32)(w4 * 4 * h);
            for (s32 i = 0; i < n && o < size; i++, o++) g_SpriteBank[o] = (u8)(g_SpriteBank[o] + g_SpriteColorOfs);
            k++;
        }
        r = o;
    } else {
        FILE *f = Platform_Fopen(DSTR(0x80C7A), DSTR(0x80C77));      /* "data/jetsprit.pal", "rb" */
        if (!f) FatalError(DSTR(0x80C98), DSTR(0x80C8C), 1);
        u8 buf[0x60] = { 0 };
        if (fread(buf, 0x60, 1, f) != 1) { /* not checked */ }
        fclose(f);
        for (int i = 0; i < 0x60; i++) {
            g_Palette[0xC0 + i] = buf[i];
            g_Palette[0x120 + i] = buf[i];
        }
        r = 0x5f;
    }
    return r;
}

/* 0x12f67 Sprites_ReplaceFromBank(slot, n, bank): the n-th entry (1-based) of an unpacked bank is copied
 * over sprite slot `slot` (header unchanged, pixels + g_SpriteColorOfs). The slot must be large enough. */
void Sprites_ReplaceFromBank(int slot, int n, const u8 *bank)
{
    for (; n > 1; n--) bank += (u32)(bank[6] | bank[7] << 8) * (u32)(bank[4] | bank[5] << 8) * 4 + 8;
    u16 len = (u16)((bank[4] | bank[5] << 8) * 4 * (bank[6] | bank[7] << 8) + 8);
    if (slot < 1 || slot >= SPRITE_TAB_SIZE || !g_SpriteTab[slot]) return;   /* PORT: no such slot */
    u8 *dst = g_SpriteTab[slot];
    for (u16 i = 0; i < len; i++) dst[i] = i < 8 ? bank[i] : (u8)(bank[i] + g_SpriteColorOfs);
}

/* 0x13773 Sprites_CountInBank */
int Sprites_CountInBank(const u8 *bank, int size)
{
    int n = 0;
    for (int o = 0; o < size; o += (int)((u32)(bank[o + 6] | bank[o + 7] << 8) * (u32)(bank[o + 4] | bank[o + 5] << 8) * 4 + 8))
        n++;
    return n;
}

/* 0x10e70 Sprite_ClearQueue */
void Sprite_ClearQueue(void) { g_SpriteQueueCount = 0; }

/* 0x1206e Sprite_Mirror */
int Sprite_Mirror(int id) { return id + 0x8000; }

/* Queue entry n field (0 mirror, 4 wrap, 8 x, 12 y, 16 sprite). PORT: the sprite pointer is kept as the
 * sprite table index (the image cannot hold a C pointer); writes past the image are dropped. */
static void q_put(s32 n, int field, s32 v)
{
    u32 off = 0x1AD8u + (u32)n * 20u + (u32)field;
    if (off + 4 <= sizeof g_dseg) memcpy(&g_dseg[off], &v, 4);
}

static s32 q_get(s32 n, int field)
{
    u32 off = 0x1AD8u + (u32)n * 20u + (u32)field;
    s32 v = 0;
    if (off + 4 <= sizeof g_dseg) memcpy(&v, &g_dseg[off], 4);
    return v;
}

/* 0x10f11 Sprite_Queue (no limit: entry 256 overwrites g_Palette, video.md Q11, kept) */
void Sprite_Queue(s32 x, s32 y, s32 id)
{
    if (id == 0) return;
    s32 n = g_SpriteQueueCount;
    s32 qx = x + g_ScrollFineX;
    q_put(n, 8, qx);
    q_put(n, 12, y + 16 + g_ScrollFineY);
    if (id < 0x8001) { q_put(n, 0, 0); q_put(n, 16, id); }
    else { q_put(n, 0, 1); q_put(n, 16, id - 0x8000); }
    u32 hx = Sprite_GetX(id);
    q_put(n, 4, (qx - (s32)(hx & 0xFFFF)) < 0);
    g_SpriteQueueCount++;
}

static const u8 *spr_ptr(s32 idx) { return (idx > 0 && idx < SPRITE_TAB_SIZE) ? g_SpriteTab[idx] : NULL; }

/* 0x1155e Sprite_DrawQueue: last queued first; wrapped entries at x + 384 (ENH: + the row). */
void Sprite_DrawQueue(void)
{
    for (s32 i = g_SpriteQueueCount - 1; i > -1; i--) {
        s32 xx = q_get(i, 8) + (q_get(i, 4) ? VRAM_ROW : 0);
        const u8 *s = spr_ptr(q_get(i, 16));
        if (q_get(i, 0) == 0) Sprite_Blit((u32)xx, (u32)q_get(i, 12), s, g_BackPage);
        else Sprite_BlitMirror(xx, q_get(i, 12), s, g_BackPage);
    }
}

/* 0x1102d Sprite_DrawNow: absolute VRAM (HUD, menus). */
void Sprite_DrawNow(u32 x, u32 y, int id) { Sprite_Blit(x, y, spr_ptr(id), 0); }

/* 0x11066 Sprite_DrawNowShift */
void Sprite_DrawNowShift(u32 x, u32 y, int id) { Sprite_BlitShift(x, y, spr_ptr(id), 0); }

static u32 hdr16(int id, int off)
{
    if (id == 0 || id > 0x212) return 0;
    const u8 *s = spr_ptr(id);
    return s ? (u32)(s[off] | s[off + 1] << 8) : 0;
}

u32 Sprite_GetX(int id) { return hdr16(id, 0); }            /* 0x11847 */
u32 Sprite_GetY(int id) { return hdr16(id, 2); }            /* 0x117f8 */
u32 Sprite_GetWidth(int id) { return hdr16(id, 4) * 4; }    /* 0x11895 */
u32 Sprite_GetHeight(int id) { return hdr16(id, 6); }       /* 0x118e7 */

/* ================================================================ §6 level renderer */

static double dbits(uint64_t u)
{
    double d;
    memcpy(&d, &u, sizeof d);
    return d;
}

/* 0x13842 Level_DrawBackground(col, row): 24x16 tile window from the map, backdrop origin, 4 planes. ENH: the
 * window is vl.tile_cols x vl.tile_rows (rows 13..15 of the original were never drawn). */
void Level_DrawBackground(s32 col, s32 row)
{
    const u8 *grid = g_MapGrid + 4;
    if (row < 2) row = 2;
    for (int j = 0; j < vl.tile_rows; j++)
        for (int i = 0; i < vl.tile_cols; i++) {
            s32 k = g_MapWidth * (row + j - 1) + imod_js(col + i, g_MapWidth, "Level_DrawBackground");
            /* PORT: at the bottom of the map (row 54) the window reaches rows 64..68, past the 0xfa04-byte grid
             * (heap bytes in the original); those rows lie under the HUD split (row 64 is the 13th tile row,
             * rows 65+ are never drawn), so they read as tile 0 here. */
            tile_window[j * vl.tile_cols + i] = (k >= 0 && k < 0xfa00) ? grid[k] : 0;
        }
    s32 px = (s32)((double)g_CamX / dbits(0x4010AAAAAA9F36A3ull)) % 320 - g_ScrollFineX;
    /* ENH: view: the backdrop line at the bottom of the view is the original's (from the camera of the original
     * view with the same bottom edge); the extra rows show the lines above it */
    s32 py = (s32)((double)(g_CamY + VIEW_EXTRA_ROWS + 0x7D8) / 9.25 - (double)g_ScrollFineY) - VIEW_EXTRA_ROWS;
    int pagesel = (g_BackPage == vl.page_a) ? 0 : 8;
    for (int plane = 0; plane < 4; plane++) {
        Video_SelectPlane(plane);
        if (g_DetailParallax)
            Tiles_DrawColumnsParallax(tile_window, g_TilePtrs, pagesel, plane, g_ParallaxBuf, py * 320 + px, px);
        else
            Tiles_DrawColumns(tile_window, g_TilePtrs, pagesel, plane);
    }
}

/* ================================================================ §7 palette */

/* 0x11145 Pal_SetColor */
void Pal_SetColor(int idx, int r, int g, int b)
{
    g_Palette[idx * 3] = (u8)r;
    g_Palette[idx * 3 + 1] = (u8)g;
    g_Palette[idx * 3 + 2] = (u8)b;
    Pal_Upload(idx, idx + 1);
}

/* 0x1119e Pal_SetColorNoUpload */
void Pal_SetColorNoUpload(int idx, int r, int g, int b)
{
    g_Palette[idx * 3] = (u8)r;
    g_Palette[idx * 3 + 1] = (u8)g;
    g_Palette[idx * 3 + 2] = (u8)b;
}

/* 0x111e6 Pal_SetColor12 */
void Pal_SetColor12(int idx, u32 rgb)
{
    g_Palette[idx * 3] = (u8)(((rgb >> 8) & 15) << 2);
    g_Palette[idx * 3 + 1] = (u8)(((rgb >> 4) & 15) << 2);
    g_Palette[idx * 3 + 2] = (u8)((rgb & 15) << 2);
    Pal_Upload(idx, idx + 1);
}

/* 0x1125c Pal_GetColor12: packs the 6-bit values into overlapping nibbles (video.md Q8). */
u32 Pal_GetColor12(int idx)
{
    return (u32)g_Palette[idx * 3 + 2] + ((u32)g_Palette[idx * 3 + 1] + (u32)g_Palette[idx * 3] * 16) * 16;
}

/* 0x1195b Pal_Black: DAC entries [first, last) := 0; g_Palette unchanged. */
void Pal_Black(int first, int last)
{
    for (int i = first; i < last; i++) dac[3 * i] = dac[3 * i + 1] = dac[3 * i + 2] = 0;
}

/* 0x119b6 Pal_Upload: DAC [first, last) := g_Palette. */
void Pal_Upload(int first, int last)
{
    for (int i = first; i < last; i++) {
        if (i < 0 || i > 255) continue;
        dac[3 * i] = g_Palette[3 * i];
        dac[3 * i + 1] = g_Palette[3 * i + 1];
        dac[3 * i + 2] = g_Palette[3 * i + 2];
    }
}

/* 0x120ea Pal_UploadAll (callers push 4 ignored arguments) */
void Pal_UploadAll(void) { Pal_Upload(0, 256); }

/* 0x13b01 Pal_Fade(first, last, type, n): 0 out, 1 in, 2 half out, 3 half in; one retrace per step;
 * a repeated type is ignored (video.md Q10); g_Palette is always restored, the DAC is left as faded. */
void Pal_Fade(int first, int last, u32 type, int n)
{
    if (type == g_LastFadeType) return;
    g_LastFadeType = type;
    u8 saved[768];
    memcpy(saved, g_Palette, 768);
    int s0 = 0, s1 = 0, N = n;
    if (type == 0 || type == 1) { s0 = 0; s1 = n; }
    else if (type == 2) { N = 2 * n; s0 = 0; s1 = N / 2; }
    else if (type == 3) { N = 2 * n; s0 = N / 2; s1 = N; }
    for (int s = s0; s < s1 && type <= 3; s++) {
        double k;
        if (type == 0) k = (1.0 / n) * (double)(n - s);
        else if (type == 1) k = (1.0 / n) * (double)s;
        else if (type == 2) k = (1.0 / N) * (double)(N - s);
        else k = (1.0 / N) * (double)s;
        for (int i = 3 * first; i < 3 * last; i++) {
            if (g_FadeTargetColor == 0) {
                g_Palette[i] = (u8)(u32)((double)(u32)saved[i] * k);
            } else {
                u32 t = saved[3 * g_FadeTargetColor + i % 3];
                g_Palette[i] = (u8)(u32)((double)(s32)(saved[i] - t) * k + (double)t);
            }
        }
        Pal_Upload(first, last);
        Video_WaitVSync();
    }
    memcpy(g_Palette, saved, 768);
    if (type & 1) Pal_Upload(first, last);
}

/* 0x45c09 Pal_CycleEffects: night flashes (from Video_FlipPage when g_FlashCounter != 0). */
void Pal_CycleEffects(void)
{
    if (g_NightMission == 0) return;
    s32 c = ++g_FlashCounter;
    if (g_DetailParallax == 0) {
        Pal_SetColor12(0x40, (u32)g_FlashTable[c]);
        if (g_FlashTable[c] == 0) g_FlashCounter = 0;
    } else if (c <= 99) {
        g_FlashCounter = 0x65;
    } else {
        for (int i = 0; i < 16; i++) {
            int v[3];
            for (int comp = 0; comp < 3; comp++) {
                int p = g_ParallaxPal[3 * i + comp];
                if (c <= 104) v[comp] = (int)((double)(63 - p) * 0.25 * (double)(c - 100) + (double)p);
                else v[comp] = (int)(63.0 - (double)(63 - p) * 0.25 * (double)(c - 104));
            }
            Pal_SetColor(0xC0 + i, v[0], v[1], v[2]);
        }
        if (g_FlashCounter == 0x6C) g_FlashCounter = 0;
    }
}

/* 0x460ba Pal_NightAltitude */
void Pal_NightAltitude(void)
{
    const double c21 = dbits(0x3FA8618618618619ull), c31 = dbits(0x3FA0842108421084ull);
    g_NightLevelShown = g_NightLevel;
    for (int i = 0; i < 16; i++) {
        int v[3];
        for (int comp = 0; comp < 3; comp++) {
            int p = g_PalSaved[0x0C0 + 3 * i + comp];
            double t = (double)(63 - p) * c21;
            t = t * (double)g_NightLevel;
            v[comp] = (int)((double)p + t);
        }
        Pal_SetColor(0x40 + i, v[0], v[1], v[2]);
        for (int comp = 0; comp < 3; comp++) {
            int p = g_PalSaved[0x180 + 3 * i + comp];
            double t = (double)(63 - p) * c21;
            t = t * (double)g_NightLevel;
            v[comp] = (int)((double)p + t);
        }
        Pal_SetColor(0x80 + i, v[0], v[1], v[2]);
        int l2 = Clamp((g_CamY + 150) / 33 + 1, 0, 31);
        for (int comp = 0; comp < 3; comp++) {
            int p = g_PalSaved[0x240 + 3 * i + comp];
            double t = (double)(63 - p) * c31;
            t = t * (double)l2;
            v[comp] = (int)((double)p + t);
        }
        Pal_SetColor(0xC0 + i, v[0], v[1], v[2]);
    }
}

/* 0x46617 Pal_SaveNight */
void Pal_SaveNight(void)
{
    memcpy(g_PalSaved, g_Palette, 768);
    Pal_SetColor(0xFE, 0x30, 0, 0);
    /* g_NightGreyTab: written, never read (video.md §7). Kept for the image contents. */
    for (int j = 1; j < 16; j++)
        for (int i = 0; i < 16; i++) {
            float f = (i == 7) ? 0.0f : 0.3f;
            if (j < 4) f = (float)((double)f + (double)(5 - j) * 0.2);
            double d = 1.0 + (double)(15 - j) / 12.0 + (double)f;
            int c = i + (int)(0.5 + (double)(13 - i) / d);
            g_NightGreyTab[j * 16 + i] = c * 0x111;
        }
    for (int i = 0; i < 16; i++) g_NightGreyTab[i] = (s32)(Pal_GetColor12(i) & 0xFFFF);
    g_NightLevelShown = -2;
}

/* 0x4699e Pal_Restore: no upload. */
void Pal_Restore(void) { memcpy(g_Palette, g_PalSaved, 768); }

/* ================================================================ §8 text */

/* 0x114da Fonts_Load */
void Fonts_Load(void)
{
    File_LoadWhole(DSTR(0x80CB2), DSTR(0x80CA5), (void **)&g_SmallFont, -1);   /* misc/smallfnt.raw */
    File_LoadWhole(DSTR(0x80CB2), DSTR(0x80CB8), (void **)&g_BigFont, -1);     /* misc/bigfnt.raw */
    Pal_SetColor(0xff, 0x3f, 0x3f, 0x3f);
    Pal_Upload(0xff, 0x100);
}

/* 0x12343 Font_DrawSmallGlyph: colour 255, or 254 with the night/fog palette. */
int Font_DrawSmallGlyph(const u8 *g, int x, int y, int rows)
{
    int maxc = 0;
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < 16; c++)
            if (g[r * 16 + c] != 0) {
                if (maxc < c) maxc = c;
                Video_PutPixel((u32)(x + c), y + r, g_NightPalActive == 0 ? 0xFF : 0xFE);
            }
    return maxc;
}

/* 0x123fa Font_DrawBigGlyph: byte 1 -> 255, byte 5 -> 0 (shadow). */
int Font_DrawBigGlyph(const u8 *g, int x, int y, int w, int rows)
{
    int maxc = 0;
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < w; c++) {
            u8 v = g[r * w + c];
            if (v == 1) Video_PutPixel((u32)(x + c), y + r, 0xFF);
            if (v == 5) Video_PutPixel((u32)(x + c), y + r, 0);
            if (v != 0 && maxc < c) maxc = c;
        }
    return maxc;
}

/* 0x12970 Font_GlyphWidth: rightmost non-zero column of 16. (Every glyph of both charsets lies inside its
 * font file, so the reads stay in the buffer.) */
int Font_GlyphWidth(const u8 *g, int rows)
{
    int maxc = 0;
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < 16; c++)
            if (g[r * 16 + c] != 0 && maxc < c) maxc = c;
    return maxc;
}

static int upper(u8 ch) { return (ch > 0x60 && ch < 0x7b) ? ch - 0x20 : ch; }

static int charset_index(const char *set, int ch)
{
    int i = 0;
    while (set[i] != (char)ch && set[i] != 0) i++;
    return set[i] ? i : -1;
}

/* Width of one small-font character as Text_WidthSmall / Text_FitWidth count it (without the +2). */
static int small_glyph_w(int ch, int i)
{
    const u8 *f = g_SmallFont;
    if (ch < 0x2c) {
        if (ch == 0x20) return 2;
        goto letter;
    }
    if (ch < 0x2d) return Font_GlyphWidth(f + i * 0x50 + 0x10, 3);
    if (ch < 0x2e) return Font_GlyphWidth(f + i * 0x50 - 0x60, 1);
    if (ch == 0x2e) return Font_GlyphWidth(f + i * 0x50 - 0x10, 1);
letter:
    if (i < 0x11) {
        if (ch == 0x51) return Font_GlyphWidth(f + i * 0x50, 6);
        if (ch < 0x51) return Font_GlyphWidth(f + i * 0x50, 5);
        return 0;
    }
    return Font_GlyphWidth(f + i * 0x50 + 0x10, 5);
}

/* 0x124e0 Text_DrawSmall(x, y, s, onPage): absolute VRAM rows, + g_BackPage/96 if onPage. */
void Text_DrawSmall(int x, int y, const char *s, int onPage)
{
    int cx = x;
    y += (onPage * g_BackPage) / VRAM_ROWB;
    const u8 *f = g_SmallFont;
    for (; *s; s++) {
        int ch = upper((u8)*s);
        if (ch == 10) { y += 8; cx = x; continue; }
        int i = charset_index(g_SmallCharset, ch);
        if (i < 0) { cx += 2; continue; }
        if (ch < 0x2c) {
            if (ch == 0x20) cx += 2;
            else goto letter;
        } else if (ch < 0x2d) {
            cx += Font_DrawSmallGlyph(f + i * 0x50 + 0x10, cx, y + 3, 3);
        } else if (ch < 0x2e) {
            cx += Font_DrawSmallGlyph(f + i * 0x50 - 0x60, cx, y + 2, 1);
        } else if (ch == 0x2e) {
            cx += Font_DrawSmallGlyph(f + i * 0x50 - 0x10, cx, y + 4, 1);
        } else {
        letter:
            if (i < 0x11) {
                if (ch == 0x51) cx += Font_DrawSmallGlyph(f + i * 0x50, cx, y, 6);
                else if (ch < 0x51) cx += Font_DrawSmallGlyph(f + i * 0x50, cx, y, 5);
            } else {
                cx += Font_DrawSmallGlyph(f + i * 0x50 + 0x10, cx, y, 5);
            }
        }
        cx += 2;
    }
}

/* 0x126e9 Text_DrawBig: \n = 12 rows, unknown = 4 px, no gap between glyphs. */
void Text_DrawBig(int x, int y, const char *s, int onPage)
{
    int cx = x;
    y += (onPage * g_BackPage) / VRAM_ROWB;
    for (; *s; s++) {
        int ch = upper((u8)*s);
        if (ch == 10) { y += 0xc; cx = x; continue; }
        int i = charset_index(g_BigCharset, ch);
        if (i < 0) cx += 4;
        else if (i < 0x10) cx += Font_DrawBigGlyph(g_BigFont + i * 0x90, cx, y, 0x10, 9);
        else if (i < 0x11) cx += Font_DrawBigGlyph(g_BigFont + i * 0x90, cx, y, 0x10, 10);
        else cx += Font_DrawBigGlyph(g_BigFont + i * 0x90 + 0x10, cx, y, 0x10, 9);
    }
}

/* 0x12bed Text_WidthSmall */
int Text_WidthSmall(const char *s)
{
    int w = 0;
    for (; *s; s++) {
        int ch = upper((u8)*s);
        int i = charset_index(g_SmallCharset, ch);
        if (i >= 0) w += small_glyph_w(ch, i);
        w += 2;
    }
    return w;
}

/* 0x12d8d Text_WidthBig */
int Text_WidthBig(const char *s)
{
    int w = 0;
    for (; *s; s++) {
        int ch = upper((u8)*s);
        int i = charset_index(g_BigCharset, ch);
        if (i < 0) w += 4;
        else if (i < 0x10) w += Font_GlyphWidth(g_BigFont + i * 0x90, 9);
        else if (i < 0x11) w += Font_GlyphWidth(g_BigFont + i * 0x90, 10);
        else w += Font_GlyphWidth(g_BigFont + i * 0x90 + 0x10, 9);
    }
    return w;
}

/* 0x129ee Text_FitWidth: index of the last space before the width reaches maxw (0 if it fits / none). */
int Text_FitWidth(const char *s, int maxw)
{
    int i = 0, w = 0;
    for (;;) {
        if (s[i] == 0 || maxw <= w) {
            while (i > 0 && s[i] != ' ' && s[i] != 0) i--;
            return w < maxw ? 0 : i;
        }
        int ch = upper((u8)s[i]);
        i++;
        int k = charset_index(g_SmallCharset, ch);
        if (k >= 0) w += small_glyph_w(ch, k);
        w += 2;
    }
}

/* 0x12831 Text_DrawDigit: 3x5 digits from the image table 0x80019, colour 255, no background. */
void Text_DrawDigit(int x, int y, int d)
{
    for (int r = 0; r < 5; r++) {
        u8 b = D8(0x80019u + (u32)(d * 5 + r));
        if (b & 4) Video_PutPixel((u32)x, y + r, 0xFF);
        if (b & 2) Video_PutPixel((u32)(x + 1), y + r, 0xFF);
        if (b & 1) Video_PutPixel((u32)(x + 2), y + r, 0xFF);
    }
}

/* 0x128f1 Text_DrawNumber: n digits right-aligned, leading zeros (negative values: video.md Q9). */
void Text_DrawNumber(int x, int y, int val, int n)
{
    int px = x + n * 4;
    for (int i = 0; i < n; i++) {
        Text_DrawDigit(px, y, (u8)(val % 10));
        val /= 10;
        px -= 4;
    }
}

/* ================================================================ §9 primitives */

void Video_SetLineColor(int c) { g_LineColor = c; }                                   /* 0x143b0 */
void Video_DrawLineColor(u32 x1, u32 y1, u32 x2, u32 y2) { Video_DrawLine(x1, y1, x2, y2, (u8)g_LineColor); }   /* 0x13041 */

/* 0x31604 Video_DrawLine: the original DDA with its quirks (video.md Q1). */
void Video_DrawLine(u32 x1, u32 y1, u32 x2, u32 y2, u8 col)
{
    if (x1 == x2) {
        s32 lo = (s32)y1 < (s32)y2 ? (s32)y1 : (s32)y2, hi = (s32)y1 < (s32)y2 ? (s32)y2 : (s32)y1;
        for (s32 y = lo; y <= hi; y++) Video_PutPixel(x2, y, col);
        return;
    }
    if (y1 == y2) {
        s32 lo = (s32)x1 < (s32)x2 ? (s32)x1 : (s32)x2, hi = (s32)x1 < (s32)x2 ? (s32)x2 : (s32)x1;
        for (s32 x = lo; x <= hi; x++) Video_PutPixel((u32)x, (s32)y1, col);
        return;
    }
    u32 xs, ys, xe, ye;
    if (y2 < y1) { xs = x2; ys = y2; xe = x1; ye = y1; } else { xs = x1; ys = y1; xe = x2; ye = y2; }
    u32 dy = ye - ys, acc = 0x800, x = xs, y = ys, step;
    u16 cnt;
#define ADD_CARRY(a, s) ((a) + (s) < (a))
    if (xe < xs) {
        u32 dx = xs - xe;
        if (dx >= dy) {
            uint64_t num = (uint64_t)(dy - 1) << 32 | (u32)(-(s32)dy);
            step = (u32)(num / dx);
            cnt = (u16)(dx + 1);
            for (;;) {
                Video_PutPixel(x, (s32)y, col);
                if (--cnt == 0) break;
                if (ADD_CARRY(acc, step)) y++;
                acc += step;
                x--;
            }
        } else {
            step = (u16)(((u32)(dx & 0xFFFF) << 16) / (u16)dy);
            cnt = (u16)dy;
            for (;;) {
                Video_PutPixel(x, (s32)y, col);
                if (--cnt == 0) break;
                if (ADD_CARRY(acc, step)) x--;
                acc += step;
                y++;
            }
        }
    } else {
        u32 dx = xe - xs;
        if (dx >= dy) {
            u16 q = (u16)((((u32)((dy - 1) & 0xFFFF)) << 16 | ((u32)(-(s32)dy) & 0xFFFF)) / (u16)dx);
            step = 0xFFFF0000u | q;
            cnt = (u16)(dx + 1);
            for (;;) {
                Video_PutPixel(x, (s32)y, col);
                if (--cnt == 0) break;
                if (ADD_CARRY(acc, step)) y++;
                acc += step;
                x++;
            }
        } else {
            step = (u16)(((u32)(dx & 0xFFFF) << 16) / (u16)dy);
            cnt = (u16)(dy + 1);
            for (;;) {
                Video_PutPixel(x, (s32)y, col);
                if (--cnt == 0) break;
                if (ADD_CARRY(acc, step)) x++;
                acc += step;
                y++;
            }
        }
    }
#undef ADD_CARRY
}

/* 0x1334f Video_FillRect: inclusive, absolute VRAM. */
void Video_FillRect(int x1, int y1, int x2, int y2, u8 col)
{
    for (int y = y1; y <= y2; y++)
        for (int x = x1; x <= x2; x++) Video_PutPixel((u32)x, y, col);
}

/* 0x11bee Video_CopyRect: latch copy of whole 4-pixel groups, rows = (char)(y2 - y1) (loop until 0). */
void Video_CopyRect(int srcPage, int x1, int y1, int x2, int y2, int dstPage, int dx, int dy)
{
    s8 rows = (s8)(y2 - y1);
    s32 src = y1 * VRAM_ROWB + (x1 >> 2) + srcPage * PAGE_BYTES;
    s32 srcEnd = y1 * VRAM_ROWB + (x2 >> 2) + srcPage * PAGE_BYTES;
    s32 dst = dy * VRAM_ROWB + (dx >> 2) + dstPage * PAGE_BYTES;
    for (; rows != 0; rows--) {
        s32 d = dst;
        for (s32 sb = src; sb <= srcEnd; sb++, d++)
            for (int p = 0; p < 4; p++) vput(d * 4 + p, vget(sb * 4 + p));
        src += VRAM_ROWB;
        srcEnd += VRAM_ROWB;
        dst += VRAM_ROWB;
    }
}
