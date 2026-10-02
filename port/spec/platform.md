# Platform subsystem (JS_CDROM.EXE): startup/shutdown, JS.CFG, keyboard, input, joystick, timing, RNG, files, fatal errors

Binary: `work/JS.bin` (flat, base 0x10000), decompile `port/decomp/js.c`. Symbols for this spec:
`port/spec/platform_symbols.csv`. Confidence tags: **verified** (read in the disassembly, capstone),
**likely**, **guess**. Addresses are linear. "word" = 16 bit, "dword" = 32 bit. All C functions use the
Watcom stack convention (args pushed right to left, caller pops); `__CHK(n)` is the stack probe, ignore.

Scope boundary: video primitives (`Video_SetModeX`, `Pal_*`, sprite/tile blitters) belong to the video
spec, `Player_Update` 0x2d34e and `Game_Run` 0x1ba0c to the gameplay/flow specs. They are referenced here only
where they consume platform state (key slots, `g_Fire`, `g_VSyncWaits`).

---------------------------------------------------------------------------------------------------

## 1. Port design summary (read this first)

| Original mechanism | Port |
|---|---|
| DOS/4GW flat memory, `malloc` 2 MB probe | plain `malloc`; drop the probe (keep the message text unused) |
| INT 9 ISR 0x10760 writing `g_KeyDown[]` + remapped key words | SDL key events -> translate SDL scancode to **PC set-1 scancode** -> call a C copy of the ISR body (`Kbd_OnScancode`) for every press/release. Keep `g_KeyDown[256]`, the 21 key words and the two edge latches exactly. |
| `in 0x3DA` busy waits (`Video_WaitVSync`, `Video_SetStartAndPan`, `Video_CountVSync`) | a **virtual retrace clock** at 59.94 Hz (mode X 320x240 = 25.175 MHz / 800 / 525). `WaitVSync()` = present the current front page, pump SDL events, sleep until the next retrace tick. |
| `Video_BenchmarkSpeed` -> `g_VSyncWaits` 0/1/2 | **do not run it**; set `g_VSyncWaits = 2` (what every fast machine and DOSBox at normal cycles get). Optionally expose a setting (0/1/2). |
| Game logic speed | one logic tick per `Video_FlipPage` = `1 + g_VSyncWaits` retraces = **3 retraces = 19.98 Hz** (≈20 fps). All physics is per-frame, no delta time. |
| Joystick port 0x201 RC timing | SDL gamepad/joystick: synthesise the 4 direction bits + button bits directly (`Joystick_ReadDirs` contract), ignore calibration values except as an optional deadzone. |
| INT 8 timer (PIT 50 Hz) for the SB mixer | belongs to the sound spec; the port uses an SDL audio callback. Nothing in game logic reads a tick counter. |
| `time()`/`srand`, Watcom `rand` LCG | reimplement the LCG bit-exactly (§7); `srand(time(NULL))` at the same place (MainMenu entry), with an optional fixed seed for testing. |
| DOS 8.3 file names | `File_Open` wrapper: truncate every path component to 8.3, case-insensitive lookup (§8). |
| `FatalError` -> text mode + printf + exit | SDL message box (or stderr) with the same two strings, then clean SDL shutdown, exit code = 3rd arg. |

---------------------------------------------------------------------------------------------------

## 2. Startup and shutdown

### 2.1 `main` 0x146f4 — `void main(void)` (verified)

Order of operations:

1. `p = malloc(0x1f4000)` (2,048,000 bytes). If NULL: `printf` three lines
   ("To play JetStrike you will need at least 2.5 megabytes\n", "of free extended memory...\n\n",
   "Please remove any disk-caches or RAM disks...\n") and `exit()` (exit code = garbage/unspecified: the
   call pushes no argument; **quirk**, port: exit(1)). Else `free(p)`. The text says 2.5 MB but the probe is 2.0 MB.
2. `g_PackBuf` (0x84510) `= malloc(85000)`, `g_PicBuf` (0x84508) `= malloc(82000)`. If either is NULL:
   `FatalError("Memory Error while allocating memory for", " packed and picture data.", 2)`.
3. `Kbd_Install()` (§3.2).
4. `Sprites_LoadSpx("jetstrike.spx")` (file `data/jetstrike.spx` -> DOS opens `DATA\JETSTRIK.SPX`),
   `Zone_Clear()`, `Sprite_ClearQueue()`.
5. `g_CfgFile` (0x8fe94) `= fopen("js.cfg","rb")`; NULL -> `FatalError("Jetstrike!\n\nError reading js.cfg\n",
   "\nPlease run config program first.\n", 1)`. `fread(&g_Config, 0x48, 1, f)`; `fclose(f)`.
   The file is only 0x3c bytes, so fread reads 60 bytes (returns 0 items, not checked); bytes
   0x3c..0x47 of `g_Config` keep their BSS value 0. Note `g_CDMusicOn` 0x936f8 = `g_Config+0x44` and
   `g_SfxOn` 0x936fc = `g_Config+0x48` lie in or just after this block: `g_Config+0x44` is therefore
   0 after loading and is overwritten by MainMenu (§2.3).
6. `g_JoyEnabled` (0x84538, word) `= cfg.joy_enabled` (+0x24). If nonzero: copy the four calibration
   thresholds (§4.1) into 0x84530/0x84534/0x84532/0x8452c and call `Joystick_Poll()` once (result
   unused, a warm-up).
7. `g_DetailParallax` (0x84536, word) `= cfg.detail` (+0x2e).
8. `Video_SetModeX()`; `g_Palette[0..0x2ff] = 0`; `Pal_Upload(0, 0x100)`.
9. `Video_BenchmarkSpeed()` (§6.3) -> `g_VSyncWaits`.
10. `Game_Run()` (returns only when the main-menu "Quit" zone set `g_QuitGame` 0x80174 = 1; Game_Run then
    does `Pal_Fade(0x100, 0, 0x20)` style fade-out and returns — argument order per the video spec).
11. `Sound_Shutdown()` (unconditional; it checks `g_SoundDevice` itself), `Kbd_Restore()`,
    `Video_SetTextMode()`, then the credits: "Thank you for playing JetStrike - PC\n\n",
    "JetStrike - PC has been a conversion by Team Hoi Games in 1994.\n\n\n",
    "Main PC Coding done by Martijn Pieterse.\n\n", "Additional PC Coding done by Peter Schaap.\n", "\n\n\n".
    `main` returns (exit code = whatever EAX holds; port: 0).

Not done on exit: **INT 8 is never restored** (`Timer_Restore` 0x10ba0 and its duplicate 0x10bad have no
callers), so after a session with Sound Blaster the PIT stays at 50 Hz and the protected-mode INT 8
vector still points into the (terminated) program. Harmless under DOS/4GW in practice
(it restores vectors on exit). Port: irrelevant.

### 2.2 Memory buffers allocated by platform code (verified)

| Global | Size | Allocated in | Use |
|---|---|---|---|
| 0x84510 `g_PackBuf` | 85000 | main | packed (LZW) file buffer, passed to `File_LoadWhole` with size 0 (no bounds check!) |
| 0x84508 `g_PicBuf` | 82000 | main | unpacked picture buffer (video spec) |
| 0x849f0 `g_MapBuf` | 150000 | `Mem_AllocMapBuffers` 0x1aeaa | map data (level spec) |
| `g_MapGrid` | 0xfa04 | `Mem_AllocMapBuffers` | map grid, first dword set to 0 |

`Mem_AllocMapBuffers` 0x1aeaa (verified): `g_MapBuf = malloc(150000); g_MapGrid = malloc(0xfa04); *(uint32*)g_MapGrid = 0;`
No NULL checks (a NULL would crash on the store). Called once from Game_Run init.

### 2.3 Per-MainMenu platform work (MainMenu 0x469e7, verified)

On the **first** call only (`g_CDChecked` 0x8078e == 0): `CD_Check()` -> 1: `FatalError("MSCDEX Not found, unable to use CD-ROM.\n","\n",5)`;
2: `FatalError("Old version of MSCDEX found, unable to use CD-ROM.\n","\n",5)`; 3: `FatalError("Wrong CD.\nConsider buying the original!\n","\n\n",6)`;
(case 1 falls through into the case-2 call in the code, but FatalError never returns). Then `g_CDChecked = 1`.
Every call: `g_SfxOn = cfg.sfx` (+2); if `g_SfxOn && !g_SoundInited` (0x8078c): `Sound_Init()`, and if
`g_SoundDevice == 0` -> `FatalError("No supported soundcard found...\n", "\n\nReturning to DOS\n\nTo solve this, turn off the sound fx option with the config program...\n\n", 3)`; `g_SoundInited = 1`.
`g_CDMusicOn = cfg.cd_music` (word +0 zero-extended). Then **`srand(time(NULL))`** (§7) — on every MainMenu entry.

Port: CD check -> skip (or check that the audio tracks exist); keep the srand placement.

---------------------------------------------------------------------------------------------------

## 3. JS.CFG and the keyboard

### 3.1 JS.CFG layout (verified against CONFIG.EXE fread/fwrite order and the shipped file)

60 bytes, 30 little-endian words, read into `g_Config` 0x936b4 (game reads 0x48 bytes). CONFIG.EXE
(`work/CONFIG.bin`, functions 0x11d29 load / 0x11ff1 save) reads/writes word by word in this order.

```c
#pragma pack(1)
typedef struct {            /* g_Config @ 0x936b4 */
  uint16 cd_music;          /* +0x00  1 = CD music on   (Y/N question)                -> g_CDMusicOn */
  uint16 sfx;               /* +0x02  1 = sound fx on                                  -> g_SfxOn     */
  uint16 key[16];           /* +0x04..+0x22  set-1 scancodes, see table               */
  uint16 joy_enabled;       /* +0x24                                                   -> g_JoyEnabled */
  uint16 joy_left;          /* +0x26  (UL.x + C.x)/2   -> 0x84530 g_JoyLeftThr  */
  uint16 joy_up;            /* +0x28  (UL.y + C.y)/2   -> 0x84532 g_JoyUpThr    */
  uint16 joy_right;         /* +0x2a  (LR.x + C.x)/2   -> 0x84534 g_JoyRightThr */
  uint16 joy_down;          /* +0x2c  (LR.y + C.y)/2   -> 0x8452c g_JoyDownThr  */
  uint16 detail;            /* +0x2e  parallax/detail ("recommended only with VESA local bus") -> g_DetailParallax */
  uint16 key_guns;          /* +0x30 */
  uint16 key_fire_left;     /* +0x32 */
  uint16 key_fire_right;    /* +0x34 */
  uint16 key_look;          /* +0x36 */
  uint16 key_target;        /* +0x38 */
  uint16 sb_irq;            /* +0x3a  -> g_SBIrq 0x936ee */
} JsCfg;                    /* 0x3c bytes on disk */
```

Calibration (CONFIG 0x10d54): UL = stick upper-left, LR = lower-right, C = released (centre); each
threshold is the midpoint between the extreme and the centre (unsigned 16-bit arithmetic, `/2` truncating).

Key slots. "Slot" = the word global the ISR writes (0 or 1). Default = shipped `Game/JS.CFG`.
Meaning from the game's use of the slot (CONFIG action strings in brackets).

| CFG off | default | slot | meaning / reader | conf. |
|---|---|---|---|---|
| +0x04 | 0x12 E | 0x84548 `g_KeyEject` | eject (Player_Update 0x2d34e: `if key && !ejecting`) | verified |
| +0x06 | 0x1c Enter | 0x84556 `g_KeyHover` | agile/hover mode toggle (only if aircraft type 0x903d4==2) | verified |
| +0x08 | 0x1e A | 0x84546 `g_KeyUnusedA` | **never read** (written by the ISR only) | verified |
| +0x0a | 0x16 U | 0x84558 `g_KeyGear` | undercarriage up/down (HUD 0x23/0x24) | verified |
| +0x0c | 0x26 L | 0x8455e `g_KeyUnusedL` | **never read** ([Lock out weapons] not implemented) | verified |
| +0x0e | 0x20 D | 0x84554 `g_KeyUnusedD` | **never read** ([Toggle smoke] not implemented) | verified |
| +0x10 | 0x19 P | 0x8454a `g_KeyPause` | pause (Player_Update); also bit 1 of `Input_GetBits`, `g_CtrlRaw[6]` | verified |
| +0x12 | 0x0f Tab | 0x8454c `g_KeyAutoThrottle` | autothrottle toggle (HUD 0x16 "ON"/0x15 "OFF") | verified |
| +0x14 | 0x30 B | 0x84544 `g_KeyBriefing` | mission briefing overlay while held > 8 frames | verified |
| +0x16 | 0x01 Esc | 0x84542 `g_KeyAbort` | [Self-destruct]: `g_Lives = -8; 0x900d8 = 3` when 0x90440 == 0 | verified |
| +0x18 | 0x48 Up | 0x84560 `g_KeyUp` | [Aircraft rotate anti-clockwise / heli up] | verified |
| +0x1a | 0x50 Down | 0x8454e `g_KeyDn` | [rotate clockwise / heli down] | verified |
| +0x1c | 0x4b Left | 0x84552 `g_KeyLeft` | [throttle down / heli left] | verified |
| +0x1e | 0x4d Right | 0x84564 `g_KeyRight` | [throttle up / heli right] | verified |
| +0x20 | 0x2a LShift | 0x84550 `g_KeyTurnL` | [helicopter turn left] | likely |
| +0x22 | 0x36 RShift | 0x84562 `g_KeyTurnR` | [helicopter turn right] | likely |
| +0x30 | 0x39 Space | 0x8455a `g_KeyGuns` | [fire guns] -> `g_Fire` | verified |
| +0x32 | 0x38 Alt | latch 0x8453c `g_FireLeftReq` | [fire left weapon], edge-triggered | verified |
| +0x34 | 0x1d Ctrl | latch 0x84540 `g_FireRightReq` | [fire right weapon], edge-triggered | verified |
| +0x36 | 0x37 KP* | 0x8453e `g_KeyLook` | [look around] | verified |
| +0x38 | 0x0e Backspace | 0x8453a `g_KeyTarget` | [change target]: cycles the follow camera (B52 / Fat Albert / ground force / enemy / weapon) | verified |

(The old name `g_KeySlots` for 0x8453a was wrong: it is only the change-target slot; renamed `g_KeyTarget`.)

Hard-wired keys (not remappable), read directly from `g_KeyDown`:
- `g_KeyDown[0x39]` Space (0x83991) and `g_KeyDown[0x1c]` Enter (0x83974): "confirm" in all menus/waits,
  OR-ed with `g_Fire`. (So remapping fire off Space still leaves Space as confirm.)
- `g_KeyDown[0x02..0x0b]` (keys 1..0): Game_Run (around 0x20470) sets the throttle `0x8ffd4 = scancode-2`
  (0..9) when `0x8fa24 == 0 && 0x8fa4c == 0`. Scanned upward, so the highest held key wins.
- `g_KeyDown[0x3d]` F3 (0x83995): Game_Run 0x1e1c2: forces a lightning flash (night effect) whenever
  `0x8ffb0 == 0` — a leftover debug/cheat key. Keep (faithful).
- F1..F10 (0x3b..0x44): `Input_GetFKey` (save/load slot select) and `WaitKey_Press` (skip).
- Pause wait: any of `g_KeyDown[0..0x7f]`.

### 3.2 `Kbd_Install` 0x10a16 / `Kbd_Restore` 0x10a88 (verified)

Install: `for i in 0..255: g_KeyDown[i] = 0;` save old INT 9 (`_dos_getvect(9)` -> offset 0x84526,
selector 0x8452a); `_dos_setvect(9, Kbd_ISR)`. Restore: `_dos_setvect(9, saved)`. The ISR never chains to
the BIOS handler: no BIOS key buffer, Ctrl-Alt-Del / Ctrl-C / Pause are swallowed. FatalError does **not**
call Kbd_Restore (DOS/4GW restores vectors at exit).

Port: Install = zero the array + start delivering SDL events; Restore = no-op.

### 3.3 `Kbd_ISR` 0x10760 (verified) — port as `void Kbd_OnScancode(uint8 sc)`

```c
uint8 g_KeyDown[256];   /* 0x83958, only [0..0x7f] ever written */
void Kbd_OnScancode(uint8 sc) {           /* sc = byte read from port 0x60 */
  /* hardware ack: in 0x61, out |0x80, out &0x7f; EOI 0x20->0x20 (drop in port) */
  if (sc != 0xE0) g_KeyDown[sc & 0x7f] = (sc < 0x80);   /* 1 = make, 0 = break */
  /* recomputed after EVERY scancode, in this order (each slot is a uint16 = 0/1): */
  g_KeyEject   = g_KeyDown[cfg.key[0]];   g_KeyHover = g_KeyDown[cfg.key[1]];
  g_KeyUnusedA = g_KeyDown[cfg.key[2]];   g_KeyGear  = g_KeyDown[cfg.key[3]];
  g_KeyUnusedL = g_KeyDown[cfg.key[4]];   g_KeyUnusedD = g_KeyDown[cfg.key[5]];
  g_KeyPause   = g_KeyDown[cfg.key[6]];   g_KeyAutoThrottle = g_KeyDown[cfg.key[7]];
  g_KeyBriefing= g_KeyDown[cfg.key[8]];   g_KeyAbort = g_KeyDown[cfg.key[9]];
  g_KeyUp      = g_KeyDown[cfg.key[10]];  g_KeyDn    = g_KeyDown[cfg.key[11]];
  g_KeyLeft    = g_KeyDown[cfg.key[12]];  g_KeyRight = g_KeyDown[cfg.key[13]];
  g_KeyTurnL   = g_KeyDown[cfg.key[14]];  g_KeyTurnR = g_KeyDown[cfg.key[15]];
  g_KeyGuns    = g_KeyDown[cfg.key_guns];
  if (!g_KeyDown[cfg.key_fire_left]) g_AltHeld = 0;                     /* 0x80060 dword */
  else if (g_AltHeld == 0 && g_FireLeftReq == 0) { g_FireLeftReq = 1; g_AltHeld = 1; }
  if (!g_KeyDown[cfg.key_fire_right]) g_CtrlHeld = 0;                   /* 0x80064 dword */
  else if (g_CtrlHeld == 0 && g_FireRightReq == 0) { g_FireRightReq = 1; g_CtrlHeld = 1; }
  g_KeyLook   = g_KeyDown[cfg.key_look];
  g_KeyTarget = g_KeyDown[cfg.key_target];
}
```

Indexing uses the full 16-bit CFG word (`movzx eax, word [cfg]`), so a CFG value > 0xff would read past
the array (CONFIG never writes such values). The fire latches are cleared by the consumer (Game_Run 0x1fxxx:
`if (g_FireLeftReq) g_FireLeftReq = 0;` after firing); one press = one request; a new request needs a release
**and** the previous request consumed (if the key is pressed while a request is still pending, `g_AltHeld`
stays 0, so the request is set later on the next scancode event of any key — subtle, keep).

Extended keys (quirk, keep): the 0xE0 prefix byte is ignored but the following byte is processed as a
normal scancode, so grey arrows = keypad arrows, Right Ctrl = Ctrl (0x1d), Right Alt = Alt (0x38),
KP Enter = Enter (0x1c), KP / = '/' (0x35). The keyboard's fake shifts (E0 2A / E0 AA around grey keys
with NumLock on) set/clear `g_KeyDown[0x2a]` = LShift = helicopter turn left.

**Port mapping** (SDL3): on `SDL_EVENT_KEY_DOWN` (ignore `repeat` events: the PC typematic repeat sends
extra make codes, but they don't change any state since make is idempotent... except the latches, which only
fire on 0->1 of `g_AltHeld` — so ignoring repeats is exactly equivalent) call
`Kbd_OnScancode(set1(sc))`, on KEY_UP `Kbd_OnScancode(set1(sc) | 0x80)`. Set-1 table (the subset needed:
everything CONFIG.EXE can assign): Esc 01, 1..0 02..0B, - 0C, = 0D, Backspace 0E, Tab 0F, Q..P 10..19,
[ 1A, ] 1B, Enter/KP Enter 1C, Ctrl (L/R) 1D, A..L 1E..26, ; 27, ' 28, ` 29, LShift 2A, \ 2B, Z..M 2C..32,
, 33, . 34, / and KP/ 35, RShift 36, KP* 37, Alt (L/R) 38, Space 39, CapsLock 3A, F1..F10 3B..44,
NumLock 45, ScrollLock 46, KP7/Home 47, KP8/Up 48, KP9/PgUp 49, KP- 4A, KP4/Left 4B, KP5 4C,
KP6/Right 4D, KP+ 4E, KP1/End 4F, KP2/Down 50, KP3/PgDn 51, KP0/Ins 52, KP./Del 53, F11 57, F12 58.
Arrow keys, Home/End/PgUp/PgDn/Ins/Del of the grey block map to the same codes as the keypad (because of
the E0 behaviour above). On focus loss, release all keys (send break codes for every set entry).

### 3.4 Key slot accessors (verified)

`Key_Up` 0x122a3 / `Key_Left` 0x122cb / `Key_Right` 0x122f3 / `Key_Down` 0x1231b:
`uint16 f(void)` return `g_KeyUp` / `g_KeyLeft` / `g_KeyRight` / `g_KeyDn` (callers push an ignored arg 1).

`Input_GetBits` 0x11eab (no callers; 0x11eb8 is a no-prologue duplicate): returns
`((g_KeyTurnR*2 | g_KeyTurnL)*2 | g_KeyPause)*2 | g_Fire`.

`Input_AnyKey` 0x11f20, called by MainMenu: `return (g_KeyTurnR||g_KeyTurnL||g_KeyPause) | (g_KeyLeft||g_KeyRight||g_KeyUp||g_KeyDn);` (0/1).

---------------------------------------------------------------------------------------------------

## 4. Input

### 4.1 Joystick (verified)

`Joystick_Poll` 0x327c0 (asm): `out 0x201, al` (fires the one-shots), then loop: `al = in 0x201`; while
`al & 3`: `if (al&1) cx++; if (al&2) bx++;` (16-bit counters). Stores `g_JoyX` 0x8054c = cx, `g_JoyY`
0x80550 = bx (dwords, zero-extended). No timeout: with no joystick the bits read 1 forever? (Real game ports
return 0xFF when nothing is plugged in -> **infinite loop** if JS.CFG enables a joystick that is absent;
quirk, port: N/A.) The counts depend on CPU speed (they are what CONFIG calibrated against).

`Joystick_GetX` 0x327fb / `Joystick_GetY` 0x32805: return g_JoyX / g_JoyY.
`Joystick_GetButtons` 0x3280f: `return (uint8)(~in(0x201)) >> 4;` -> bit0 = button 1, bit1 = button 2 (A/B of joystick A), bits 2,3 = joystick B.
0x32824 `Joystick_WaitButton(mask)` (no callers in JS; same code in CONFIG): wait until `buttons & mask == 0`, then until `!= 0`.

`Joystick_ReadDirs` 0x1456d — `uint8 Joystick_ReadDirs(uint32 *buttons)`:
```c
Joystick_Poll(); int x = g_JoyX, y = g_JoyY;
uint8 d = (x < (int)g_JoyLeftThr);          /* bit0 left  */
if (x > (int)g_JoyRightThr) d |= 4;          /* bit2 right */
if (y < (int)g_JoyUpThr)    d |= 2;          /* bit1 up    */
if (y > (int)g_JoyDownThr)  d |= 8;          /* bit3 down  */
*buttons = Joystick_GetButtons(); return d;
```
Thresholds are uint16 globals compared as signed int (no sign issues, values < 0x8000).

Port: `d` from a gamepad stick/D-pad with a deadzone (e.g. |axis| > 50%), `*buttons` bit0 = south button
(fire), bit1 = east button (unused by the game). Only if `g_JoyEnabled` (keep the CFG flag or make
gamepad always active — both are invisible to game logic beyond these bits).

### 4.2 `Input_ReadControls` 0x3ceae — `void Input_ReadControls(void)` (verified)

Called every game frame from Player_Update, and from every menu/wait loop.

```c
g_AnyInput = 0;                                                  /* 0x909c8 dword */
uint32 btn;
if (g_JoyEnabled == 0) { g_JoyDirs = 0; btn = 0; }               /* 0x90264 */
else g_JoyDirs = Joystick_ReadDirs(&btn);
g_Fire = (g_KeyGuns != 0 || (btn & 1) == 1);                     /* 0x8455c word, 0/1 */
int up    = (Key_Up()    != 0 || (g_JoyDirs & 2));
int left  = (Key_Left()  != 0 || (g_JoyDirs & 1));
int right = (Key_Right() != 0 || (g_JoyDirs & 4));
int down  = (Key_Down()  != 0 || (g_JoyDirs & 8));
/* g_CtrlRaw: int32[11] @ 0x90c58, values 0 or -1 */
g_CtrlRaw[7] = -up;  g_CtrlRaw[8] = -left;  g_CtrlRaw[9] = -right;  g_CtrlRaw[10] = -down;
g_CtrlRaw[4] = -g_Fire;  g_CtrlRaw[6] = -g_KeyPause;
g_CtrlRaw[0] = -g_KeyTurnL;  g_CtrlRaw[1] = -g_KeyTurnR;
g_CtrlRaw[2] = 0; g_CtrlRaw[3] = 0; g_CtrlRaw[5] = 0;
g_AnyInput = 0;
for (g_LoopI = 0; g_LoopI < 11; g_LoopI++) {                    /* g_LoopI = 0x90ab8, a shared global loop counter! */
  g_Ctrl[g_LoopI] = g_CtrlRaw[g_LoopI];                          /* g_Ctrl int32[11] @ 0x90c98 */
  g_AnyInput |= g_Ctrl[g_LoopI];
}
g_AnyInput |= (g_Fire || g_KeyDown[0x39] || g_KeyDown[0x1c]);
```
Note the writes go through the shared global loop index 0x90ab8 (also used by most object loops): it is
left at 11 on return. Keep that side effect (callers might depend on it; Player_Update reassigns it before use).

`g_Ctrl` index meaning (used by Player_Update / menus): 0 TurnL, 1 TurnR, 2 (always 0), 3 (0), 4 Fire,
5 (0), 6 Pause, 7 Up, 8 Left, 9 Right, 10 Down. **Quirk:** Player_Update's rocket-boost test reads
`g_Ctrl[2]` (0x90ca0), which is always 0, so the "Rocket Boost" branch is dead (report to the player spec).
Player_Update copies `g_Ctrl[0]` -> 0x90814 and `g_Ctrl[2]` -> 0x90820 **before** calling
Input_ReadControls (previous-frame values for edge detection of TurnL and the change-target key).

Action debounce used by Player_Update (for the port of that spec): `g_LastAction` 0x9044c holds the id of
the action handled last frame (3 pause, 4 gear, 5 hover, 7 autothrottle); each handler sets
`g_ActionThisFrame` 0x90430 = 1; at the end of the frame `g_LastAction *= g_ActionThisFrame` (0x2e0xx,
js.c line 15277), so holding a key does not repeat the toggle.

### 4.3 Menus and waits (verified)

`Input_PollMenu` 0x1acf5 (12 callers, menu loops):
```c
Input_ReadControls();
g_FireOrConfirm = (g_Fire || g_KeyDown[0x39] || g_KeyDown[0x1c]);   /* 0x905cc */
g_MenuDX = g_Ctrl[8] - g_Ctrl[9];   /* 0x90778 = right - left  (values -1/0/1) */
g_MenuDY = g_Ctrl[7] - g_Ctrl[10];  /* 0x9077c = down - up */
Video_WaitVSync(); x4                /* menus therefore run at 59.94/4 = 15 Hz */
```
(-left - -right = right-left, since g_Ctrl values are 0/-1.)

`WaitKey_Release` 0x45144 (callers Game_Run, Story_ShowAsc) — really "wait for confirm with timeout, then release":
```c
g_SavedPage = Video_GetPage();             /* 0x90128 */
g_WaitCount = g_WaitCountInit;             /* 0x90444 = 0x90418 */
Input_ReadControls();
while (!(g_Fire || KeyDown[0x39] || KeyDown[0x1c]) && g_WaitCount <= 499) { g_WaitCount++; Video_WaitVSync(); Input_ReadControls(); }
Input_ReadControls();
while ((g_Fire || KeyDown[0x39] || KeyDown[0x1c]) && g_Lives > -999 && g_WaitCount < 500) { Input_ReadControls(); g_WaitCount++; Video_WaitVSync(); }
g_WaitCountInit = 0;
```
Timeout 500 retraces = 8.34 s total (shared between both loops).

`WaitKey_Press` 0x45233 (caller Mission_LoadBriefing): no timeout:
```c
g_SavedPage = Video_GetPage(); g_WaitCount = g_WaitCountInit;
Input_ReadControls();
while (!(g_Fire || KeyDown[0x39] || KeyDown[0x1c])) { Input_ReadControls(); Video_WaitVSync(); }
Input_ReadControls();
fk = any of g_KeyDown[0x3b..0x44];
while ((g_Fire || KeyDown[0x39] || KeyDown[0x1c]) && g_Lives > -999 && !fk) { Input_ReadControls(); Video_WaitVSync(); fk |= any F1..F10; }
g_WaitCountInit = 0; return 0x44 (garbage, last loop index; callers ignore);
```

`Input_GetFKey` 0x3c5b0 (caller SaveGame_LoadMenu): busy-loop forever until one of `g_KeyDown[0x3b..0x44]`
is set, scanning upward; returns `scancode - 0x3a` (F1 = 1 .. F10 = 10). No vsync wait inside: **the port
must pump events inside this loop** (`Video_WaitVSync`-style yield) or it will hang.

Same for the pause loop in Player_Update (busy loops on `g_KeyDown`) and anything else that spins on
ISR-updated memory: in the port, every such spin loop must call `Platform_Pump()` (process SDL events,
present, sleep ~1 ms). List of spin loops on key state: Player_Update pause (2 loops), Input_GetFKey,
FUN_00011fc3/0x11fd0 (dead debug screen, `while (g_Fire)` then `for(;;)`).

### 4.4 Cheat / stub key code path (verified)

`FUN_0001169d` -> `Input_GetKeyCode_Stub` 0x1169d: `return 0;` (an Amiga "last key code" reader left empty).
Game_Run stores its result in `g_KeyCode` 0x90490 every frame (after copying the old value to `g_LastAction`).
`CheatKeys` 0x167b6 / `CheatKeys_b` 0x167c3 compare `g_KeyCode` against a table at 0x8456c..0x845b4 (all BSS zeros)
and would toggle/abort things — but **they have no callers** (dead code). Do not port.
`FUN_00012288` 0x12288 (`File_LoadStub`): empty; called as `File_LoadStub("jetsoundext.aaf", 0, -1)` from MainMenu — no effect.

---------------------------------------------------------------------------------------------------

## 5. Fatal errors

`FatalError` 0x10c1d — `void FatalError(const char *a, const char *b, int code)` (verified):
`CD_Stop(); if (g_SfxOn) Sound_Shutdown(); Video_SetTextMode(); printf("%s%s\n", a, b); exit(code);`
(tail `jmp exit` with `code` pushed). Kbd_Restore is not called.

Callers and codes (29 call sites): js.cfg missing 1, file not found (File_LoadWhole) 1, alloc failure in
File_LoadWhole 6, memory for pack/picture 2, sprite memory 2, MSCDEX 5, wrong CD 6, no sound card 3,
plus Game_Run data files ("gendat3.dax"," not found.",1 etc).

Port: `Platform_Fatal(a,b,code)`: stop audio, `SDL_ShowSimpleMessageBox(ERROR, "JetStrike", a+b)`, fprintf
stderr, `SDL_Quit()`, `exit(code)`.

---------------------------------------------------------------------------------------------------

## 6. Timing

### 6.1 Retrace primitives (verified)

- `Video_WaitVSync` 0x10108 `int (int unused)`: `while (in(0x3da) & 8); while (!(in(0x3da) & 8)); return 0;`
  = wait for the **start of the next** vertical retrace (if called inside a retrace, waits for the next one).
- `Video_SetStartAndPan` 0x106b0 (asm) `(uint x, int y, int pageofs)`: CRTC 0x0C/0x0D = `y*0x60 + (x>>2) + pageofs`,
  then `cli`, the same two-loop wait as WaitVSync, `sti`, then attribute reg 0x33 (written via 0x3c0) = `(x&3)*2`.
  So every call costs one retrace wait.
- `Video_CountVSync` 0x31a19 `int (void)`: wait while in retrace, then count loop iterations until the next
  retrace starts; returns the count (a CPU-speed-dependent measure of the remaining display time).

The display mode (video spec): 25.175 MHz dot clock, 800 clocks/line, 525 lines -> **59.94 Hz** retrace.

### 6.2 `Video_FlipPage` 0x13a68 (verified; video spec owns the drawing part)

```c
Video_SetStartAndPan(g_ShakeX, g_ShakeY, g_BackPage + 0x600);   /* waits 1 retrace */
for (i = 0; i < g_VSyncWaits; i++) Video_WaitVSync(0);          /* g_VSyncWaits 0x80054 int32 */
if (g_PalCycleOn /*0x9031c*/) Pal_CycleEffects();
g_BackPage = (g_BackPage == 0x18c0) ? 0x78c0 : 0x18c0;
```
(A dead `cmp g_SoundDevice,1` sits at 0x13a9d with no branch.)

### 6.3 `Video_BenchmarkSpeed` 0x143d3 — `int Video_BenchmarkSpeed(void)` (verified)

```c
int crossings = 0;                        /* local, compared unsigned for the switch */
Video_WaitVSync(0);
int prev = Video_CountVSync();            /* a full frame's worth of count */
for (int i = 1; i < 14; i++) {
  Video_WaitVSync(0);
  for (int j = 0; j < i; j++) {
    if (g_DetailParallax == 0) Tiles_ClearColumns(0,0,8,0) x4;     /* 0x3197c, asm, draws garbage into VRAM page 0x18c0 (palette is all black) */
    else                       Tiles_ParallaxBench(0,0,8,0,0,0) x4; /* 0x3184c */
  }
  int c = Video_CountVSync();
  if (prev < c) crossings++;              /* signed compare */
  prev = c;
}
g_VSyncWaits = (crossings == 0) ? 2 : (crossings == 1) ? 1 : 0;
return 13;                                  /* last loop value; unused */
```
Meaning: iteration i does 4i tile-column draws right after a retrace. While the work fits in one frame the
remaining-time count shrinks every iteration; each time the work spills over a retrace the count jumps
up, which is counted. `crossings` ≈ number of frames the heaviest batch (52 draws) needs. A machine fast
enough to do 52 draws within one frame gets `g_VSyncWaits = 2`; slow machines get fewer extra waits and
spend the time computing instead.

### 6.4 Resulting game speed (answer)

In-mission loop (Game_Run ~0x1ccbd): one `Video_FlipPage()` per iteration, one Player_Update / object
update per iteration, no other waits. If the frame's work finishes within one retrace period, the first
wait (inside SetStartAndPan) aligns to a retrace, so a frame lasts exactly `1 + g_VSyncWaits` retraces.

**On a fast machine (and DOSBox): `g_VSyncWaits = 2` -> 3 retraces per frame -> 59.94 / 3 = 19.98 Hz
game logic and display rate.** On slow machines (W=1 or 0) the game runs at most at 30 / 60 fps but
in practice at whatever the CPU manages (the benchmark is meant to approximate a constant ~20 fps).
There is no other speed regulation: all movement is per frame. Menus (`Input_PollMenu`) run at 15 Hz,
WaitKey loops and palette fades at 59.94 Hz per step.

Port: virtual retrace clock `T_retrace = 1/59.94 s`; `WaitVSync()` advances to the next retrace boundary
(sleep + present); `FlipPage` = present the finished back page then wait `1 + 2` retraces -> exact
original pacing; if a frame overruns, catch up by not sleeping (same behaviour as hardware: the next
wait completes at the next boundary). Provide an optional "60 fps smooth" mode only as a deliberate
deviation. Do NOT drive logic from the 50 Hz audio timer.

---------------------------------------------------------------------------------------------------

## 7. Random numbers (verified)

Watcom runtime: seed dword `g_RandSeed` 0x807bc, **initial value 1** (data section).
```c
int rand(void)   { g_RandSeed = g_RandSeed * 0x41C64E6D + 0x3039;  /* uint32 wrap */
                   return (g_RandSeed >> 16) & 0x7FFF; }            /* 0..32767 */
void srand(unsigned s) { g_RandSeed = s; }
```
`__get_rand_seed_ptr` 0x489c0 returns 0x807bc (never NULL in this single-threaded build).

`srand` has exactly one caller: MainMenu, `srand(time(NULL))` on every MainMenu entry (§2.3). Watcom
`time()` = seconds since 1970-01-01 local time (TZ default EST5EDT if unset, irrelevant for randomness).
Everything before the first MainMenu (intro, loading) uses seed 1.

`Rand` 0x10d65 — `int Rand(int n)` (verified):
```c
if (n == 0) return 0;               /* no rand() call: the sequence does not advance! */
return rand() % (n + 1);             /* cdq; idiv: signed C remainder */
```
Result 0..n for n > 0. Quirks: `n == 0` does not consume a random number (must be preserved for RNG
sequence fidelity); `n == -1` divides by zero (crash, #DE); `n < -1` gives `rand() % (n+1)` = a value
in `0..-n-2` (remainder takes the dividend's sign, which is >= 0). `rand()` is called only from Rand; Rand has 363 call sites.

Port: implement exactly; `n == -1` -> return 0 with a debug assert (PORT note: original would crash).

### 7.1 Small helpers (verified)

| Addr | Signature | Body |
|---|---|---|
| 0x10e11 `Clamp` | `int (int v, int lo, int hi)` | `v<lo ? lo : v>hi ? hi : v` (signed) |
| 0x1178a `Wrap` | `int (int v, int lo, int hi)` | if `v<lo || v>hi`: `v = (v>lo) ? v % (hi-lo+1) : (v+lo) % (hi-lo+1)` (signed idiv); return v. **Quirk:** not a true wrap — e.g. `Wrap(-1,0,2) = -1`, `Wrap(10,5,7) = 1` (out of range). Keep. |
| 0x13a25 `Sign` | `int (int v)` | -1 / 0 / 1 |
| 0x137de `Div16` | `int (int v)` | `v / 16` truncating toward zero (`sar` with bias 15 for negatives) |
| 0x14331 `Bit_Test` | `int (int bit, uint32 v)` | `(v & (1<<bit)) == (1<<bit)` |
| 0x14382 `Bit_Clear` | `void (int bit, uint32 *p)` | `*p &= ~(1<<bit)` |
| 0x10da7 `SwapInt` / 0x10ddc `SwapByte` | `(T*,T*)` | swap |
| 0x10c6b `Str_TrimRight` | `void (char *s, int i)` | `while ((s[i]==' '||s[i]==0) && i>=0) i--; s[i+1]=0;` (reads s[-1] when i reaches -1 is avoided only because `i>=0` is tested last: it **reads s[-1]** before the test — harmless read, port: test i first) |

---------------------------------------------------------------------------------------------------

## 8. File access

### 8.1 `File_LoadWhole` 0x12114 — `int File_LoadWhole(const char *dir, const char *name, void **buf, int size)` (verified)

```c
char path[108]; strcpy(path, dir); strcat(path, name);    /* no length check */
FILE *f = fopen(path, "rb");
if (!f) FatalError(path, " not found", 1);
int len;
if (size >= 1) len = size;                                  /* caller-given size */
else if (size == -1) {                                      /* always allocate */
  fseek(f,0,SEEK_END); len = ftell(f); fseek(f,0,SEEK_SET);
  *buf = malloc(len);
  if (!*buf) FatalError("Error allocating memory. While trying to load", path, 6);
} else {                                                    /* size 0 (or < -1): file size */
  fseek(f,0,SEEK_END); len = ftell(f); fseek(f,0,SEEK_SET);
}
if (*buf == NULL) *buf = malloc(len);                       /* allocate if caller passed NULL */
if (*buf == NULL) FatalError("Error allocating memory. While trying to load", path, 6);
fread(*buf, len, 1, f); fclose(f);
return len;
```
Quirks: with size 0 and an existing buffer (e.g. `g_PackBuf`, 85000 bytes) there is no bounds check —
a file larger than the buffer overflows it (no shipped file does; port: assert). `size == -1` leaks the
old buffer if one was passed. Note that FatalError prints `a` then `b`, so the "not found" message reads
"data/xxx not found". 22 callers (sprites, fonts, pictures, tilesets, parallax, maps, misc.z, ...).

Other platform-level opens use `fopen` directly (js.cfg; gendat3.dax with fscanf; jets.n, weapons.dat,
hudtext.dat, l1l2, gendatad.dax, gendata.dax in MainMenu; misc/jetsound.aaf; js_save.000 save files —
see the files/save specs).

### 8.2 Name building and 8.3 truncation (verified from strings + directory listing)

Paths are built with `strcpy/strcat` (or sprintf) from a directory prefix with forward slashes
(`data/`, `gfx/`, `map/`, `misc/`, `plane/`) and a name, sometimes plus an extension (`.pax`, `.pal`,
`.tlx`, `.mxp`, `.mp2`, `.spx`, `.hd`, `1.val`). DOS (through DOS/4GW's INT 21h) accepts `/` and
**silently truncates each component to 8.3 and upper-cases it**: `jetstrike.spx` -> `JETSTRIK.SPX`,
`Trainingicons.tlx` -> `TRAINING.TLX`, `jetsoundext.aaf` -> `JETSOUND.AAF` (never opened: stub).
Truncation rules the port must apply per path component: split at the **first** '.'; base = first 8
chars; extension = first 3 chars after the dot (characters after a second dot are dropped);
case-insensitive match against the real directory (scan the directory once and cache).

Port: `FILE *Platform_Fopen(const char *path, const char *mode)` implementing that, used by all file code;
writes (save games) use the same truncated name in upper case.

---------------------------------------------------------------------------------------------------

## 9. Other DOS/4GW / DPMI use (verified)

- `_dos_getvect/_dos_setvect` (INT 21h 35h/25h via DOS/4GW) for INT 9 (keyboard) and the SB IRQ vector.
- `Timer_Install` 0x10abb (called from Sound_Init): DPMI 0x204 get PM vector 8 -> saved 0x84490 (sel) /
  0x84480 (offset); DPMI 0x205 set vector 8 to `Timer_ISR_SBMixer` 0x310ba; PIT: `out 0x43,0x36; out 0x40,0x37; out 0x40,0x5d`
  -> divisor 0x5d37 = 23863 -> 1193182/23863 = **50.00 Hz**. Restore functions 0x10ba0/0x10bad (set the
  saved vector, divisor 0 = 18.2 Hz) are never called.
- Sound_Shutdown: DPMI 0x101 (free DOS memory block for the DMA buffer).
- `int386`/`int386x` 0x4894b/0x4a2d2 for INT 31h and MSCDEX (INT 2Fh, CD spec).
- Direct port I/O: 0x60/0x61/0x20 (kbd), 0x201 (joystick), 0x3c0/0x3c4/0x3ce/0x3d4/0x3da (VGA), 0x40/0x43 (PIT).
None of it needs emulating; all is replaced by SDL.

---------------------------------------------------------------------------------------------------

## 10. Bugs and quirks (decision list)

| # | Quirk | Recommendation |
|---|---|---|
| Q1 | Frame rate depends on the startup benchmark (20 fps fast / up to 30-60 fps "slow" with less waiting). | Fix at W=2 (19.98 Hz). PORT note. |
| Q2 | `Rand(0)` returns 0 without advancing the RNG; `Rand(-1)` crashes. | Keep the first; guard the second (PORT note). |
| Q3 | `Wrap` is not a real modulo wrap (negative results, wrong range when lo != 0). | Keep (gameplay depends on it). |
| Q4 | Slots A (+0x08), L (+0x0c), D (+0x0e) are never read: "Lock out weapons" and "smoke" keys do nothing. | Keep. |
| Q5 | `g_Ctrl[2]` is always 0, so Player_Update's rocket-boost branch is dead. | Keep (report in the player spec). |
| Q6 | E0-prefixed keys alias the main keys; fake shift E0 2A presses "turn left". | Keep the aliasing (grey arrows must work); the fake-shift artefact does not occur with SDL. |
| Q7 | F3 forces a lightning flash in night missions (debug key). | Keep. |
| Q8 | Number keys 1..0 set throttle 0..9 directly. | Keep (feature). |
| Q9 | Busy loops on key memory (pause, Input_GetFKey) rely on the ISR. | Port must pump events in those loops. |
| Q10 | `File_LoadWhole` size 0 into a fixed buffer has no bounds check; `Str_TrimRight` reads s[-1]. | Add asserts / reorder test (PORT note). |
| Q11 | FatalError doesn't restore the keyboard; INT 8 never restored on exit; main's "2.5 MB" message vs 2 MB probe; exit code undefined on the memory-probe path. | Irrelevant to port / exit(1). |
| Q12 | `srand(time())` on every MainMenu entry; RNG uses seed 1 before the first menu. | Keep; add an optional fixed-seed command-line switch for testing. |
| Q13 | Joystick poll has no timeout. | N/A in port. |
| Q14 | JS.CFG is 60 bytes but 72 are requested; `g_Config+0x3c..0x47` stay 0. | Port: read 60 bytes, zero the rest. |

## 11. Open questions

1. Exact frame period when the per-frame work exceeds one retrace on the original hardware is irrelevant
   for the port (W=2 assumed); confirm with a DOSBox capture that the in-mission rate is ≈20 fps (expected).
2. Player_Update's B-key "briefing" uses a hold counter `> 8` frames; at 20 fps that is 0.45 s — plausible,
   verify in DOSBox.
3. The fire-latch consumer (`g_FireLeftReq`/`g_FireRightReq` cleared at Game_Run js.c lines 9589-9621) is
   in the weapons/player spec; the condition that both are 0 before a new request is accepted should be
   checked there.
4. Whether any data file relies on the "second dot" truncation rule (none seen; `jetsoundext.aaf` is a stub).
