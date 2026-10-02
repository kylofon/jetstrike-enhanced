#pragma once
/* Full-screen pictures (port/formats/gfx.md §2). */
#include "types.h"

/* 0x11a1a Pic_LoadPax(name, page, applyPal): gfx/<name> (LZW) to VRAM page `page` at page row 20;
 * with g_PicFullLoad == 1 also clears the page and loads gfx/<name>.pal into g_Palette 0..63 (uploaded
 * if applyPal). */
int Pic_LoadPax(const char *name, u8 page, char applyPal);
