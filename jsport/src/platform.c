/* Platform subsystem: port/spec/platform.md. */
#include "platform.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "files.h"
#include "game.h"
#include "host.h"
#include "sound.h"
#include "video.h"

u8 *g_PackBuf;
u8 *g_PicBuf;

/* 0x10c1d FatalError */
_Noreturn void FatalError(const char *a, const char *b, int code)
{
    CD_Stop();
    if (g_SfxOn) Sound_Shutdown();
    /* Video_SetTextMode: no-op. PORT: the message goes to stderr and a message box. */
    host_fatal_code(code, "%s%s", a, b);
}

/* ---------------------------------------------------------------- main */

/* 0x146f4 main */
int js_main(void)
{
    /* PORT: step 1, the 2 MB malloc probe (and its "2.5 megabytes" message), is dropped. */
    g_PackBuf = malloc(85000);
    g_PicBuf = malloc(82000);
    if (!g_PackBuf || !g_PicBuf)
        FatalError(DSTR(0x80EAF), DSTR(0x80E95), 2);    /* "Memory Error while allocating memory for" */
    File_SetBufferCapacity((uintptr_t)g_PackBuf, 85000);
    Kbd_Install();
    Sprites_LoadSpx(DSTR(0x80ED8));                     /* "jetstrike.spx" */
    Zone_Clear();
    Sprite_ClearQueue();
    FILE *cfg = Platform_Fopen(DSTR(0x80EE6), DSTR(0x80C77));      /* "js.cfg", "rb" */
    if (!cfg) FatalError(DSTR(0x80F10), DSTR(0x80EED), 1);
    if (fread(&g_Config, 0x48, 1, cfg) != 1) { /* the file has 0x3c bytes: 0 items, not checked */ }
    fclose(cfg);
    g_JoyEnabled = g_Config.joy_enabled;
    if (g_Config.joy_enabled) {
        g_JoyLeftThr = g_Config.joy_left;
        g_JoyRightThr = g_Config.joy_right;
        g_JoyUpThr = g_Config.joy_up;
        g_JoyDownThr = g_Config.joy_down;
        Joystick_Poll();
    }
    g_DetailParallax = g_Config.detail;
    Video_SetModeX();
    for (int i = 0; i < 0x300; i++) g_Palette[i] = 0;
    Pal_Upload(0, 0x100);
    /* PORT: Video_BenchmarkSpeed 0x143d3 is not run; a fast machine gets 2 extra retraces per frame
     * (platform.md §6.3/6.4: logic at 19.98 Hz). */
    g_VSyncWaits = 2;
    Game_Run();
    Sound_Shutdown();
    Kbd_Restore();
    Video_SetTextMode();
    fputs(DSTR(0x80F32), stdout);                       /* the credits */
    fputs(DSTR(0x80F59), stdout);
    fputs(DSTR(0x80F9C), stdout);
    fputs(DSTR(0x80FC7), stdout);
    fputs(DSTR(0x80FF3), stdout);
    return 0;
}

/* ---------------------------------------------------------------- keyboard */

/* The ISR indexes g_KeyDown with the full 16-bit CFG word (a value > 0xff reads the image after the array). */
static u16 key(u16 code) { return D8(0x83958u + code); }

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

/* ---------------------------------------------------------------- joystick */

static u8 joy_buttons;

/* 0x327c0 Joystick_Poll. PORT: the port-201h RC timing loop is replaced by the first SDL gamepad: the counts
 * are placed just outside / between the JS.CFG calibration thresholds, so Joystick_ReadDirs gives the same
 * direction bits a calibrated stick would (half deflection = the threshold, as CONFIG calibrates). */
void Joystick_Poll(void)
{
    s16 x = 0, y = 0;
    u8 b = 0;
    host_joy_read(&x, &y, &b);
    joy_buttons = b;
    u32 l = g_JoyLeftThr, r = g_JoyRightThr, u = g_JoyUpThr, d = g_JoyDownThr;
    g_JoyX = x < -16384 ? (l ? l - 1 : 0) : x > 16384 ? r + 1 : (l + r) / 2;
    g_JoyY = y < -16384 ? (u ? u - 1 : 0) : y > 16384 ? d + 1 : (u + d) / 2;
}

u32 Joystick_GetX(void) { return g_JoyX; }       /* 0x327fb */
u32 Joystick_GetY(void) { return g_JoyY; }       /* 0x32805 */

/* 0x3280f Joystick_GetButtons: (~in 201h) >> 4, bit0 = button 1 (gamepad south), bit1 = button 2 (east). */
u32 Joystick_GetButtons(void) { return joy_buttons; }

/* 0x1456d Joystick_ReadDirs */
u8 Joystick_ReadDirs(u32 *buttons)
{
    Joystick_Poll();
    int x = (int)g_JoyX, y = (int)g_JoyY;
    u8 d = (u8)(x < (int)g_JoyLeftThr);
    if (x > (int)g_JoyRightThr) d |= 4;
    if (y < (int)g_JoyUpThr) d |= 2;
    if (y > (int)g_JoyDownThr) d |= 8;
    *buttons = Joystick_GetButtons();
    return d;
}

/* PORT: with the joystick off in JS.CFG (the shipped file) a connected gamepad still works: direction bits
 * and buttons straight from the pad (half deflection), as a calibrated joystick would give them. */
static bool gamepad_dirs(u32 *dirs, u32 *buttons)
{
    s16 x, y;
    u8 b;
    if (!host_joy_read(&x, &y, &b)) return false;
    u32 d = 0;
    if (x < -16384) d |= 1;
    if (x > 16384) d |= 4;
    if (y < -16384) d |= 2;
    if (y > 16384) d |= 8;
    *dirs = d;
    *buttons = b;
    return true;
}

/* ---------------------------------------------------------------- input */

u16 Key_Up(int unused)    { (void)unused; return g_KeyUp; }      /* 0x122a3 */
u16 Key_Left(int unused)  { (void)unused; return g_KeyLeft; }    /* 0x122cb */
u16 Key_Right(int unused) { (void)unused; return g_KeyRight; }   /* 0x122f3 */
u16 Key_Down(int unused)  { (void)unused; return g_KeyDn; }      /* 0x1231b */

/* 0x11eab Input_GetBits (no callers) */
int Input_GetBits(void) { return ((g_KeyTurnR * 2 | g_KeyTurnL) * 2 | g_KeyPause) * 2 | g_Fire; }

/* 0x11f20 Input_AnyKey */
int Input_AnyKey(int unused)
{
    (void)unused;
    return (g_KeyTurnR || g_KeyTurnL || g_KeyPause) | (g_KeyLeft || g_KeyRight || g_KeyUp || g_KeyDn);
}

/* 0x3ceae Input_ReadControls */
void Input_ReadControls(void)
{
    g_AnyInput = 0;
    u32 btn;
    if (g_JoyEnabled == 0) {
        g_JoyDirs = 0;
        btn = 0;
        u32 d;
        if (gamepad_dirs(&d, &btn)) g_JoyDirs = d;   /* PORT, see gamepad_dirs */
    } else {
        g_JoyDirs = Joystick_ReadDirs(&btn);
    }
    g_Fire = (g_KeyGuns != 0 || (btn & 1) == 1);
    int up    = (Key_Up(1) != 0 || (g_JoyDirs & 2));
    int left  = (Key_Left(1) != 0 || (g_JoyDirs & 1));
    int right = (Key_Right(1) != 0 || (g_JoyDirs & 4));
    int down  = (Key_Down(1) != 0 || (g_JoyDirs & 8));
    g_CtrlRaw[7] = -up;
    g_CtrlRaw[8] = -left;
    g_CtrlRaw[9] = -right;
    g_CtrlRaw[10] = -down;
    g_CtrlRaw[4] = -(s32)g_Fire;
    g_CtrlRaw[6] = -(s32)g_KeyPause;
    g_CtrlRaw[0] = -(s32)g_KeyTurnL;
    g_CtrlRaw[1] = -(s32)g_KeyTurnR;
    g_CtrlRaw[2] = 0;
    g_CtrlRaw[3] = 0;
    g_CtrlRaw[5] = 0;
    g_AnyInput = 0;
    for (g_LoopI = 0; g_LoopI < 11; g_LoopI++) {
        g_Ctrl[g_LoopI] = g_CtrlRaw[g_LoopI];
        g_AnyInput |= g_Ctrl[g_LoopI];
    }
    g_AnyInput |= (g_Fire || g_KeyDown[0x39] || g_KeyDown[0x1c]);
}

/* 0x1acf5 Input_PollMenu: menus run at 59.94/4 = 15 Hz. */
void Input_PollMenu(void)
{
    Input_ReadControls();
    g_FireOrConfirm = (g_Fire || g_KeyDown[0x39] || g_KeyDown[0x1c]);
    g_MenuDX = g_Ctrl[8] - g_Ctrl[9];
    g_MenuDY = g_Ctrl[7] - g_Ctrl[10];
    Video_WaitVSync();
    Video_WaitVSync();
    Video_WaitVSync();
    Video_WaitVSync();
}

void Platform_Spin(void) { host_idle(); }

/* 0x3c5b0 Input_GetFKey: busy loop until one of F1..F10 is down (scanned upward); returns 1..10.
 * PORT: the loop pumps events (platform.md Q9). */
int Input_GetFKey(void)
{
    for (;;) {
        for (int sc = 0x3b; sc < 0x45; sc++)
            if (g_KeyDown[sc]) return sc - 0x3a;
        Platform_Spin();
    }
}

/* 0x1169d Input_GetKeyCode_Stub: the Amiga key-code reader, left empty. */
int Input_GetKeyCode_Stub(void) { return 0; }

static bool confirm(void) { return g_Fire || g_KeyDown[0x39] || g_KeyDown[0x1c]; }

/* 0x45144 WaitKey_Release: wait for confirm (timeout shared with the release wait: 500 - init retraces). */
void WaitKey_Release(void)
{
    g_SavedPage = Video_GetPage();
    g_WaitCount = g_WaitCountInit;
    Input_ReadControls();
    while (!confirm() && g_WaitCount <= 499) {
        g_WaitCount++;
        Video_WaitVSync();
        Input_ReadControls();
    }
    Input_ReadControls();
    while (confirm() && g_Lives > -999 && g_WaitCount < 500) {
        Input_ReadControls();
        g_WaitCount++;
        Video_WaitVSync();
    }
    g_WaitCountInit = 0;
}

static int any_fkey(void)
{
    int fk = 0;
    for (int sc = 0x3b; sc < 0x45; sc++) fk |= g_KeyDown[sc];
    return fk;
}

/* 0x45233 WaitKey_Press: no timeout; the release wait ends early when an F-key is down (save slot). */
int WaitKey_Press(void)
{
    g_SavedPage = Video_GetPage();
    g_WaitCount = g_WaitCountInit;
    Input_ReadControls();
    while (!confirm()) {
        Input_ReadControls();
        Video_WaitVSync();
    }
    Input_ReadControls();
    int fk = any_fkey();
    while (confirm() && g_Lives > -999 && !fk) {
        Input_ReadControls();
        Video_WaitVSync();
        fk |= any_fkey();
    }
    g_WaitCountInit = 0;
    return 0x44;                                  /* the last loop index; callers ignore it */
}

/* ---------------------------------------------------------------- random numbers */

/* 0x489c6 Watcom rand() */
int rand_js(void)
{
    g_RandSeed = g_RandSeed * 0x41C64E6Du + 0x3039u;
    return (int)((g_RandSeed >> 16) & 0x7FFF);
}

/* 0x489e8 srand */
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

/* ---------------------------------------------------------------- helpers */

int Clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }      /* 0x10e11 */

/* 0x1178a Wrap: not a true wrap (platform.md Q3). */
int Wrap(int v, int lo, int hi)
{
    if (v < lo || v > hi) {
        int m = hi - lo + 1;
        v = (v > lo) ? imod_js(v, m, "Wrap") : imod_js(v + lo, m, "Wrap");
    }
    return v;
}

int Sign(int v) { return v < 0 ? -1 : v > 0 ? 1 : 0; }                          /* 0x13a25 */
int Div16(int v) { return v / 16; }                                             /* 0x137de */
int Bit_Test(int bit, u32 v) { return (v & (1u << bit)) == (1u << bit); }       /* 0x14331 */
void Bit_Clear(int bit, u32 *p) { *p &= ~(1u << bit); }                         /* 0x14382 */

/* 0x10da7 SwapInt */
void SwapInt(s32 *a, s32 *b)
{
    s32 t = *a;
    *a = *b;
    *b = t;
}

/* 0x10ddc SwapByte */
void SwapByte(u8 *a, u8 *b)
{
    u8 t = *a;
    *a = *b;
    *b = t;
}

/* 0x10c6b Str_TrimRight. PORT: i is tested first (the original reads s[-1] once, harmless; platform Q10). */
void Str_TrimRight(char *s, int i)
{
    while (i >= 0 && (s[i] == ' ' || s[i] == 0)) i--;
    s[i + 1] = 0;
}

/* 0x12288 File_LoadStub: empty (called with "jetsoundext.aaf"). */
void File_LoadStub(const char *name, int a, int b) { (void)name; (void)a; (void)b; }

char *itoa_js(int v, char *buf)
{
    sprintf(buf, "%d", v);
    return buf;
}

int idiv_js(int a, int b, const char *where)
{
    if (b == 0 || (a == INT32_MIN && b == -1))
        FatalError("Divide by zero in ", where, 1);   /* PORT: #DE in the original */
    return a / b;
}

int imod_js(int a, int b, const char *where)
{
    if (b == 0 || (a == INT32_MIN && b == -1))
        FatalError("Divide by zero in ", where, 1);
    return a % b;
}
