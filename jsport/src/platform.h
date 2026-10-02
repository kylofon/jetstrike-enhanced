#pragma once
/* Platform subsystem (port/spec/platform.md): fatal errors, JS.CFG, keyboard ISR, RNG, small helpers. */
#include "types.h"

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

extern JsCfg g_Config;           /* 0x936b4 */
extern u32 g_CDMusicOn;          /* 0x936f8 */
extern u16 g_SfxOn;              /* 0x936fc */

/* main 0x146f4 step 5: fopen("js.cfg"), fread 0x48 bytes; missing -> FatalError(.., 1). */
void Cfg_Load(void);

/* ---- Keyboard (0x10760 Kbd_ISR, 0x10a16 Kbd_Install). */
extern u8  g_KeyDown[256];       /* 0x83958 */
extern u16 g_KeyEject, g_KeyHover, g_KeyUnusedA, g_KeyGear, g_KeyUnusedL, g_KeyUnusedD, g_KeyPause,
           g_KeyAutoThrottle, g_KeyBriefing, g_KeyAbort, g_KeyUp, g_KeyDn, g_KeyLeft, g_KeyRight,
           g_KeyTurnL, g_KeyTurnR, g_KeyGuns, g_KeyLook, g_KeyTarget;
extern u32 g_AltHeld, g_CtrlHeld;            /* 0x80060 / 0x80064 */
extern u16 g_FireLeftReq, g_FireRightReq;    /* 0x8453c / 0x84540 */
void Kbd_ISR(u8 sc);
void Kbd_Install(void);
void Kbd_Restore(void);

/* ---- Random numbers (§7): Watcom rand LCG, seed 0x807bc = 1. */
extern u32 g_RandSeed;
int  rand_js(void);
void srand_js(u32 seed);
int  Rand(int n);

/* ---- Buffers allocated by main 0x146f4 step 2 (platform.md §2.2). */
extern u8 *g_PackBuf;            /* 0x84510, 85000 bytes: packed (LZW) files */
extern u8 *g_PicBuf;             /* 0x84508, 82000 bytes: unpacked pictures */
void Platform_AllocBuffers(void);

/* Small helpers (§7.1). */
void SwapByte(u8 *a, u8 *b);     /* 0x10ddc */
