#pragma once
/* Platform subsystem (port/spec/platform.md): main, fatal errors, JS.CFG, keyboard ISR, input (keys, menu
 * polling, waits, joystick), RNG, small helpers. Globals live in the data-segment image (dseg.h) at their
 * original addresses. */
#include <stdio.h>

#include "dseg.h"

/* 0x10c1d FatalError: CD_Stop, Sound_Shutdown (if sfx on), text mode, printf("%s%s\n", a, b), exit(code). */
_Noreturn void FatalError(const char *a, const char *b, int code);

/* JS.CFG (platform.md §3.1): 60 bytes on disk, the game freads 0x48 into g_Config 0x936b4. */
#pragma pack(push, 1)
typedef struct {
    u16 cd_music;        /* +0x00 */
    u16 sfx;             /* +0x02 */
    u16 key[16];         /* +0x04 */
    u16 joy_enabled;     /* +0x24 */
    u16 joy_left;        /* +0x26 */
    u16 joy_up;          /* +0x28 */
    u16 joy_right;       /* +0x2a */
    u16 joy_down;        /* +0x2c */
    u16 detail;          /* +0x2e */
    u16 key_guns;        /* +0x30 */
    u16 key_fire_left;   /* +0x32 */
    u16 key_fire_right;  /* +0x34 */
    u16 key_look;        /* +0x36 */
    u16 key_target;      /* +0x38 */
    u16 sb_irq;          /* +0x3a */
    u8  tail[0x0c];      /* +0x3c..0x47: read by the 0x48-byte fread, stay 0 (file is 0x3c bytes) */
} JsCfg;
#pragma pack(pop)

#define g_Config        (*(JsCfg *)DSEG(0x936B4))
#define g_CDMusicOn     D32(0x936F8)             /* = g_Config + 0x44 (overlaps the 0x48-byte read) */
#define g_SfxOn         D16(0x936FC)

/* ---- Keyboard (0x10760 Kbd_ISR, 0x10a16 Kbd_Install). */
#define g_KeyDown          ((u8 *)DSEG(0x83958))     /* [256]; the ISR indexes it with 16-bit CFG words */
#define g_KeyTarget        D16(0x8453A)
#define g_FireLeftReq      D16(0x8453C)
#define g_KeyLook          D16(0x8453E)
#define g_FireRightReq     D16(0x84540)
#define g_KeyAbort         D16(0x84542)
#define g_KeyBriefing      D16(0x84544)
#define g_KeyUnusedA       D16(0x84546)
#define g_KeyEject         D16(0x84548)
#define g_KeyPause         D16(0x8454A)
#define g_KeyAutoThrottle  D16(0x8454C)
#define g_KeyDn            D16(0x8454E)
#define g_KeyTurnL         D16(0x84550)
#define g_KeyLeft          D16(0x84552)
#define g_KeyUnusedD       D16(0x84554)
#define g_KeyHover         D16(0x84556)
#define g_KeyGear          D16(0x84558)
#define g_KeyGuns          D16(0x8455A)
#define g_Fire             D16(0x8455C)
#define g_KeyUnusedL       D16(0x8455E)
#define g_KeyUp            D16(0x84560)
#define g_KeyTurnR         D16(0x84562)
#define g_KeyRight         D16(0x84564)
#define g_AltHeld          D32(0x80060)
#define g_CtrlHeld         D32(0x80064)
void Kbd_ISR(u8 sc);                             /* 0x10760 */
void Kbd_Install(void);                          /* 0x10a16 */
void Kbd_Restore(void);                          /* 0x10a88 */

/* ---- Joystick (§4.1) */
#define g_JoyDownThr       D16(0x8452C)
#define g_JoyLeftThr       D16(0x84530)
#define g_JoyUpThr         D16(0x84532)
#define g_JoyRightThr      D16(0x84534)
#define g_DetailParallax   D16(0x84536)
#define g_JoyEnabled       D16(0x84538)
#define g_JoyX             D32(0x8054C)
#define g_JoyY             D32(0x80550)
#define g_JoyDirs          D32(0x90264)
void Joystick_Poll(void);                        /* 0x327c0 */
u32  Joystick_GetX(void);                        /* 0x327fb */
u32  Joystick_GetY(void);                        /* 0x32805 */
u32  Joystick_GetButtons(void);                  /* 0x3280f */
u8   Joystick_ReadDirs(u32 *buttons);            /* 0x1456d */

/* ---- Input (§3.4, §4.2, §4.3) */
#define g_CtrlRaw          DS32A(0x90C58)        /* [11] 0/-1 */
#define g_Ctrl             DS32A(0x90C98)        /* [11] */
#define g_AnyInput         DS32(0x909C8)
#define g_FireOrConfirm    DS32(0x905CC)
#define g_MenuDX           DS32(0x90778)
#define g_MenuDY           DS32(0x9077C)
#define g_WaitCount        DS32(0x90444)
#define g_WaitCountInit    DS32(0x90418)
#define g_SavedPage        DS32(0x90128)
#define g_LoopI            DS32(0x90AB8)         /* the shared global loop counter */
#define g_LoopJ            DS32(0x9098C)         /* second shared counter (also g_ProjSprite) */
u16  Key_Up(int unused);                         /* 0x122a3 */
u16  Key_Left(int unused);                       /* 0x122cb */
u16  Key_Right(int unused);                      /* 0x122f3 */
u16  Key_Down(int unused);                       /* 0x1231b */
int  Input_GetBits(void);                        /* 0x11eab (no callers) */
int  Input_AnyKey(int unused);                   /* 0x11f20 */
void Input_ReadControls(void);                   /* 0x3ceae */
void Input_PollMenu(void);                       /* 0x1acf5 */
int  Input_GetFKey(void);                        /* 0x3c5b0 */
int  Input_GetKeyCode_Stub(void);                /* 0x1169d */
void WaitKey_Release(void);                      /* 0x45144 */
int  WaitKey_Press(void);                        /* 0x45233 */
/* PORT: one step of a busy loop on ISR-updated memory (platform.md Q9): pump events, present when a
 * retrace has passed, sleep briefly. */
void Platform_Spin(void);

/* ---- Random numbers (§7): Watcom rand LCG, seed 0x807bc = 1 (image). */
#define g_RandSeed         D32(0x807BC)
int  rand_js(void);                              /* 0x489c6 */
void srand_js(u32 seed);                         /* 0x489e8 */
int  Rand(int n);                                /* 0x10d65 */

/* ---- Small helpers (§7.1) */
int  Clamp(int v, int lo, int hi);               /* 0x10e11 */
int  Wrap(int v, int lo, int hi);                /* 0x1178a */
int  Sign(int v);                                /* 0x13a25 */
int  Div16(int v);                               /* 0x137de */
int  Bit_Test(int bit, u32 v);                   /* 0x14331 */
void Bit_Clear(int bit, u32 *p);                 /* 0x14382 */
void SwapInt(s32 *a, s32 *b);                    /* 0x10da7 */
void SwapByte(u8 *a, u8 *b);                     /* 0x10ddc */
void Str_TrimRight(char *s, int i);              /* 0x10c6b */
void File_LoadStub(const char *name, int a, int b);   /* 0x12288 */
/* Watcom itoa(v, buf, 10). */
char *itoa_js(int v, char *buf);
/* PORT: C signed division / remainder that stops like the original's #DE instead of crashing the host. */
int  idiv_js(int a, int b, const char *where);
int  imod_js(int a, int b, const char *where);

/* ---- Buffers allocated by main 0x146f4 step 2 (platform.md §2.2). */
extern u8 *g_PackBuf;            /* 0x84510, 85000 bytes: packed (LZW) files */
extern u8 *g_PicBuf;             /* 0x84508, 82000 bytes: unpacked pictures */

/* 0x146f4 main (the original's; the host's main() is in main.c). */
int js_main(void);
