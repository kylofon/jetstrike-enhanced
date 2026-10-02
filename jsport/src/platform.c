/* Platform subsystem: port/spec/platform.md. */
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"
#include "host.h"
#include "sound.h"

JsCfg g_Config;
u32 g_CDMusicOn;
u16 g_SfxOn;

/* 0x10c1d FatalError */
_Noreturn void FatalError(const char *a, const char *b, int code)
{
    CD_Stop();
    if (g_SfxOn) Sound_Shutdown();
    /* Video_SetTextMode: no-op. PORT: the message goes to stderr and a message box. */
    host_fatal_code(code, "%s%s", a, b);
}

/* main 0x146f4, step 5 (the part that reads the file) */
void Cfg_Load(void)
{
    FILE *f = Platform_Fopen("js.cfg", "rb");
    if (!f) FatalError("Jetstrike!\n\nError reading js.cfg\n", "\nPlease run config program first.\n", 1);
    memset(&g_Config, 0, sizeof g_Config);
    if (fread(&g_Config, 0x48, 1, f) != 1) { /* the file has 0x3c bytes: fread returns 0, not checked */ }
    fclose(f);
}

/* ---------------------------------------------------------------- keyboard */

u8  g_KeyDown[256];
u16 g_KeyEject, g_KeyHover, g_KeyUnusedA, g_KeyGear, g_KeyUnusedL, g_KeyUnusedD, g_KeyPause,
    g_KeyAutoThrottle, g_KeyBriefing, g_KeyAbort, g_KeyUp, g_KeyDn, g_KeyLeft, g_KeyRight,
    g_KeyTurnL, g_KeyTurnR, g_KeyGuns, g_KeyLook, g_KeyTarget;
u32 g_AltHeld, g_CtrlHeld;
u16 g_FireLeftReq, g_FireRightReq;

/* The ISR indexes g_KeyDown with the full 16-bit CFG word; CONFIG never writes values > 0xff.
 * PORT: an index past the array reads 0 instead of the bytes after g_KeyDown. */
static u16 key(u16 code) { return code < 256 ? g_KeyDown[code] : 0; }

/* 0x10760 Kbd_ISR: one byte from port 60h. */
void Kbd_ISR(u8 sc)
{
    if (sc != 0xE0) g_KeyDown[sc & 0x7f] = (sc < 0x80);
    g_KeyEject        = key(g_Config.key[0]);
    g_KeyHover        = key(g_Config.key[1]);
    g_KeyUnusedA      = key(g_Config.key[2]);
    g_KeyGear         = key(g_Config.key[3]);
    g_KeyUnusedL      = key(g_Config.key[4]);
    g_KeyUnusedD      = key(g_Config.key[5]);
    g_KeyPause        = key(g_Config.key[6]);
    g_KeyAutoThrottle = key(g_Config.key[7]);
    g_KeyBriefing     = key(g_Config.key[8]);
    g_KeyAbort        = key(g_Config.key[9]);
    g_KeyUp           = key(g_Config.key[10]);
    g_KeyDn           = key(g_Config.key[11]);
    g_KeyLeft         = key(g_Config.key[12]);
    g_KeyRight        = key(g_Config.key[13]);
    g_KeyTurnL        = key(g_Config.key[14]);
    g_KeyTurnR        = key(g_Config.key[15]);
    g_KeyGuns         = key(g_Config.key_guns);
    if (!key(g_Config.key_fire_left)) g_AltHeld = 0;
    else if (g_AltHeld == 0 && g_FireLeftReq == 0) { g_FireLeftReq = 1; g_AltHeld = 1; }
    if (!key(g_Config.key_fire_right)) g_CtrlHeld = 0;
    else if (g_CtrlHeld == 0 && g_FireRightReq == 0) { g_FireRightReq = 1; g_CtrlHeld = 1; }
    g_KeyLook   = key(g_Config.key_look);
    g_KeyTarget = key(g_Config.key_target);
}

/* 0x10a16 Kbd_Install */
void Kbd_Install(void)
{
    for (int i = 0; i < 256; i++) g_KeyDown[i] = 0;
    host_set_kbd_handler(Kbd_ISR);
}

/* 0x10a88 Kbd_Restore: the vector restore has no SDL equivalent. */
void Kbd_Restore(void) {}

/* ---------------------------------------------------------------- random numbers */

u32 g_RandSeed = 1;

/* Watcom rand() */
int rand_js(void)
{
    g_RandSeed = g_RandSeed * 0x41C64E6Du + 0x3039u;
    return (int)((g_RandSeed >> 16) & 0x7FFF);
}

void srand_js(u32 seed) { g_RandSeed = seed; }

/* 0x10d65 Rand */
int Rand(int n)
{
    if (n == 0) return 0;                       /* no rand() call: the sequence does not advance */
    int r = rand_js();
    if (n == -1) {
        /* PORT: the original divides by zero here (#DE, DOS/4GW exception dump); the port stops with a
         * message instead of a host crash (QUIRKS.md). */
        FatalError("Divide by zero in Rand(-1)", " (the original stops with a DOS/4GW exception)", 1);
    }
    return r % (n + 1);
}

/* ---------------------------------------------------------------- buffers, helpers */

u8 *g_PackBuf;
u8 *g_PicBuf;

/* main 0x146f4 step 2 */
void Platform_AllocBuffers(void)
{
    g_PackBuf = malloc(85000);
    g_PicBuf = malloc(82000);
    if (!g_PackBuf || !g_PicBuf)
        FatalError("Memory Error while allocating memory for", " packed and picture data.", 2);
    File_SetBufferCapacity((uintptr_t)g_PackBuf, 85000);
}

/* 0x10ddc SwapByte */
void SwapByte(u8 *a, u8 *b)
{
    u8 t = *a;
    *a = *b;
    *b = t;
}
