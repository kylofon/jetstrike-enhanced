#pragma once
/* Host services on top of SDL3: window/present, the virtual VGA retrace clock, keyboard (set-1 byte
 * stream for the game's own INT 9 handler), mouse, gamepad, audio device (Sound Blaster mixer stream +
 * CD music stream), case-insensitive game file lookup and fatal errors. No game logic lives here.
 *
 *   host.c        init / shutdown, event pump, file lookup, fatal
 *   host_timer.c  59.94 Hz retrace clock (host_wait_vretrace)
 *   host_video.c  window, 320x240 presentation, snapshots
 *   host_input.c  keyboard -> set-1 scancodes, scripted keys, mouse, gamepad
 *   host_audio.c  SDL audio device, mixer stream (pull callback), music stream, WAV dump */
#include "types.h"

bool host_init(const char *game_dir, int window_scale, bool fullscreen, bool open_window);
void host_shutdown(void);

/* Handles window events, scripted keys and gamepad hot-plug. Every busy-wait loop of the original must
 * call it (host_wait_vretrace does). */
void host_pump(void);

/* ---- Timing: the virtual vertical retrace of mode X 320x240 (25.175 MHz / 800 / 525 = 59.94 Hz).
 * host_wait_vretrace presents the current frame (host_set_frame_source), then sleeps until the start of
 * the next retrace, i.e. the first retrace boundary after "now" (an overrun frame waits for the next
 * boundary, like the port 3DAh loops). Returns the retrace count since start-up. */
#define HOST_RETRACE_NUM 16800000000ull  /* period = 800*525/25.175e6 s = 16800000000 / 1007 ns */
#define HOST_RETRACE_DEN 1007ull
uint64_t host_wait_vretrace(void);
uint64_t host_retrace_count(void);       /* retraces since start-up (no wait) */
bool host_in_vretrace(void);             /* port 3DAh bit 3 (first ~1.4 ms of each period) */
/* One step of a busy loop on ISR-updated memory: pumps events, presents the frame when a retrace boundary
 * has passed since the last present, sleeps ~0.5 ms. */
void host_idle(void);

/* ---- Video: the frame source fills a 320x240 XRGB8888 image (the VGA model, video.c). */
#define HOST_FRAME_W 320
#define HOST_FRAME_H 240
void host_set_frame_source(void (*compose)(u32 *xrgb));
void host_present(void);                 /* compose + show + snapshot (called by host_wait_vretrace) */

/* ---- Keyboard: the game's INT 9 handler receives the set-1 bytes the keyboard controller would deliver:
 * sc / sc|80h, grey keys with an E0 prefix. Key repeats are not delivered (platform.md §3.3: equivalent).
 * When the window loses focus, break codes are sent for every key still held. */
void host_set_kbd_handler(void (*handler)(u8 byte));

/* ---- Mouse (unused by JS_CDROM.EXE; kept for the launcher / menus of later phases). */
void host_mouse_read(s16 *x, s16 *y, u8 *buttons);

/* ---- Gamepad (replaces port 201h): first connected gamepad. Axes -32768..32767, buttons bit0 = A
 * (fire 1), bit1 = B (fire 2). Returns false if none is connected. */
bool host_joy_read(s16 *x, s16 *y, u8 *buttons);

/* ---- Audio. One SDL playback device, two streams mixed by SDL:
 *   mixer stream: unsigned 8-bit mono at `rate` Hz, pulled from the audio thread through render(out, n)
 *                 (the Sound Blaster DMA ring, sound.c); game code changes mixer state between
 *                 host_audio_lock / host_audio_unlock;
 *   music stream: a WAV file (CD track), played once. */
bool host_audio_open(int rate, void (*render)(u8 *out, int n));
void host_audio_close(void);
void host_audio_lock(void);
void host_audio_unlock(void);
bool host_music_play(const char *path);  /* false if the file cannot be loaded (music stays silent) */
void host_music_stop(void);

/* ---- Game files: case-insensitive lookup of a relative path ('/' or '\\' separated, each component
 * matched case-insensitively) inside the game directory. Returns a malloc'd path (free with host_free)
 * or NULL. With create = true a missing last component is returned as given (new files). */
char *host_game_path(const char *rel, bool create);
void  host_free(void *p);

/* ---- Errors: message box (unless headless), stderr, shutdown, exit(code). */
_Noreturn void host_fatal_code(int code, const char *fmt, ...);

/* ---- Developer aids (environment variables), see PORTING.md:
 *   JS_SNAPSHOT_DIR=dir   save presented frames as snapNNNN.png, one every JS_SNAPSHOT_MS (default 2000)
 *   JS_KEYS="<sec>:<xx>[+<xx>...][p|r],..."  scripted set-1 keys (hex; grey keys e0xx), p = press only,
 *                         r = release only
 *   JS_QUIT_AFTER=sec     exit(0) after that many seconds (headless runs)
 *   JS_AUDIO_DUMP=file    write the mixer stream (u8 mono) to a WAV file */
double host_seconds(void);
