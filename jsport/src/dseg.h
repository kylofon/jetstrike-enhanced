#pragma once
/* The data-segment image (PORTING.md "Data-segment image"): LE object 5 of JS_CDROM.EXE (0x80000-0x94750),
 * loaded at start-up from the game's own executable with its internal fixups applied, exactly as
 * tools/lefile.py builds work/JS.bin. Every non-pointer global of the original lives here at its original
 * address, so initial values, string constants, tables and the layout (out-of-bounds reads and writes into
 * the neighbouring globals) are those of the exe.
 *
 *   DSEG(a)          byte pointer to address a
 *   D8/D16/D32(a)    unsigned lvalues, DS8/DS16/DS32 signed, DF32 float
 *   DSTR(a)          char pointer
 *   DSEG_PTR(a)      the C pointer for a 32-bit flat address stored in the image (strings, tables), or NULL
 *
 * Pointer-valued globals (malloc'd buffers, the sprite table) stay C pointers outside the image (the image
 * would hold 32-bit DOS/4GW addresses). Accesses are unaligned-safe on x86/x64 (-fno-strict-aliasing). */
#include "types.h"

#define DSEG_BASE 0x80000u
#define DSEG_SIZE 0x14750u
#define DSEG_SLACK 0x1000u                      /* PORT: zero bytes after the image (BSS/heap beyond) */

extern u8 g_dseg[DSEG_SIZE + DSEG_SLACK];
/* All accesses go through this pointer (= g_dseg): the compiler then cannot see that two globals share one
 * array, which would only produce bogus -Wrestrict / bounds warnings. */
extern u8 *g_dsegp;

#define DSEG(a)  (&g_dsegp[(u32)(a) - DSEG_BASE])
#define D8(a)    (*(u8 *)DSEG(a))
#define DS8(a)   (*(s8 *)DSEG(a))
#define D16(a)   (*(u16 *)DSEG(a))
#define DS16(a)  (*(s16 *)DSEG(a))
#define D32(a)   (*(u32 *)DSEG(a))
#define DS32(a)  (*(s32 *)DSEG(a))
#define DF32(a)  (*(float *)DSEG(a))
#define DSTR(a)  ((char *)DSEG(a))
#define DS32A(a) ((s32 *)DSEG(a))               /* int32 array */
#define D16A(a)  ((u16 *)DSEG(a))

/* A flat address stored in the image -> C pointer into the image (NULL if outside it). */
static inline u8 *DSEG_PTR(u32 flat)
{
    return (flat >= DSEG_BASE && flat < DSEG_BASE + DSEG_SIZE) ? DSEG(flat) : NULL;
}

/* Loads the image from <game dir>/JS_CDROM.EXE; fatal error if the exe is missing or not the expected one. */
void Dseg_Load(void);
