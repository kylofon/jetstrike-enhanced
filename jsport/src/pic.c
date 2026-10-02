/* Pictures: port/formats/gfx.md §2, port/spec/video.md. */
#include "pic.h"

#include <string.h>

#include "files.h"
#include "lzw.h"
#include "platform.h"
#include "video.h"

/* 0x11a1a Pic_LoadPax */
int Pic_LoadPax(const char *name, u8 page, char applyPal)
{
    if (g_PicFullLoad == 1) Pal_Black(0, 0x40);
    int packed = File_LoadWhole("gfx/", name, (void **)&g_PackBuf, 0);
    s32 size = (s32)LZW_PackedSize(g_PackBuf);
    LZW_Unpack(g_PackBuf, (u32)packed, g_PicBuf);       /* PORT: 82000-byte g_PicBuf holds every PAX */
    if (g_PicFullLoad == 1) {
        /* map mask 0x0F: 0x5a00 bytes in all four planes */
        for (s32 i = 0; i < PAGE_BYTES * 4; i++) vput((s32)page * PAGE_BYTES * 4 + i, 0);
    }
    Video_BlitLinearToPlanar(g_PicBuf, 0, (s32)page * 240 + 20, 320, size / 320);
    int r = 0;
    if (g_PicFullLoad == 1) {
        char pal[100];
        strcpy(pal, name);
        size_t n = strlen(pal);
        memcpy(pal + n - 4, ".pal", 5);                 /* the last 4 characters + the terminator */
        File_LoadWhole("gfx/", pal, (void **)&g_PackBuf, 0);
        for (int i = 0; i < 0x40; i++) SwapByte(&g_PackBuf[i * 3], &g_PackBuf[i * 3 + 2]);
        for (int i = 0; i < 0xc0; i++) g_Palette[i] = (u8)(g_PackBuf[i] >> 2);
        r = 0xc0;
        if (applyPal) Pal_Upload(0, 0x40);
    }
    return r;
}
