#include "host_int.h"

#include <stdio.h>

static void (*kbd_handler)(u8);
static SDL_Gamepad *gamepad;
static float mouse_x, mouse_y;
static bool held[0x200];                 /* set-1 code | GREY, keys whose make code was sent */

void host_set_kbd_handler(void (*handler)(u8)) { kbd_handler = handler; }

/* Set-1 make code of an SDL key; GREY = sent with an E0 prefix. 0 = not reported. */
#define GREY 0x100
static u16 set1_scan(SDL_Scancode sc)
{
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) {
        static const u8 letter_scan[26] = { 0x1E,0x30,0x2E,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,
                                            0x31,0x18,0x19,0x10,0x13,0x1F,0x14,0x16,0x2F,0x11,0x2D,0x15,0x2C };
        return letter_scan[sc - SDL_SCANCODE_A];
    }
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_0) return (u16)(0x02 + (sc - SDL_SCANCODE_1));
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F10) return (u16)(0x3B + (sc - SDL_SCANCODE_F1));
    switch (sc) {
    case SDL_SCANCODE_F11:          return 0x57;
    case SDL_SCANCODE_F12:          return 0x58;
    case SDL_SCANCODE_ESCAPE:       return 0x01;
    case SDL_SCANCODE_MINUS:        return 0x0C;
    case SDL_SCANCODE_EQUALS:       return 0x0D;
    case SDL_SCANCODE_BACKSPACE:    return 0x0E;
    case SDL_SCANCODE_TAB:          return 0x0F;
    case SDL_SCANCODE_LEFTBRACKET:  return 0x1A;
    case SDL_SCANCODE_RIGHTBRACKET: return 0x1B;
    case SDL_SCANCODE_RETURN:       return 0x1C;
    case SDL_SCANCODE_KP_ENTER:     return GREY | 0x1C;
    case SDL_SCANCODE_LCTRL:        return 0x1D;
    case SDL_SCANCODE_RCTRL:        return GREY | 0x1D;
    case SDL_SCANCODE_SEMICOLON:    return 0x27;
    case SDL_SCANCODE_APOSTROPHE:   return 0x28;
    case SDL_SCANCODE_GRAVE:        return 0x29;
    case SDL_SCANCODE_LSHIFT:       return 0x2A;
    case SDL_SCANCODE_BACKSLASH:    return 0x2B;
    case SDL_SCANCODE_COMMA:        return 0x33;
    case SDL_SCANCODE_PERIOD:       return 0x34;
    case SDL_SCANCODE_SLASH:        return 0x35;
    case SDL_SCANCODE_KP_DIVIDE:    return GREY | 0x35;
    case SDL_SCANCODE_RSHIFT:       return 0x36;
    case SDL_SCANCODE_KP_MULTIPLY:  return 0x37;
    case SDL_SCANCODE_LALT:         return 0x38;
    case SDL_SCANCODE_RALT:         return GREY | 0x38;
    case SDL_SCANCODE_SPACE:        return 0x39;
    case SDL_SCANCODE_CAPSLOCK:     return 0x3A;
    case SDL_SCANCODE_NUMLOCKCLEAR: return 0x45;
    case SDL_SCANCODE_SCROLLLOCK:   return 0x46;
    case SDL_SCANCODE_KP_7:         return 0x47;
    case SDL_SCANCODE_KP_8:         return 0x48;
    case SDL_SCANCODE_KP_9:         return 0x49;
    case SDL_SCANCODE_KP_MINUS:     return 0x4A;
    case SDL_SCANCODE_KP_4:         return 0x4B;
    case SDL_SCANCODE_KP_5:         return 0x4C;
    case SDL_SCANCODE_KP_6:         return 0x4D;
    case SDL_SCANCODE_KP_PLUS:      return 0x4E;
    case SDL_SCANCODE_KP_1:         return 0x4F;
    case SDL_SCANCODE_KP_2:         return 0x50;
    case SDL_SCANCODE_KP_3:         return 0x51;
    case SDL_SCANCODE_KP_0:         return 0x52;
    case SDL_SCANCODE_KP_PERIOD:    return 0x53;
    case SDL_SCANCODE_HOME:         return GREY | 0x47;
    case SDL_SCANCODE_UP:           return GREY | 0x48;
    case SDL_SCANCODE_PAGEUP:       return GREY | 0x49;
    case SDL_SCANCODE_LEFT:         return GREY | 0x4B;
    case SDL_SCANCODE_RIGHT:        return GREY | 0x4D;
    case SDL_SCANCODE_END:          return GREY | 0x4F;
    case SDL_SCANCODE_DOWN:         return GREY | 0x50;
    case SDL_SCANCODE_PAGEDOWN:     return GREY | 0x51;
    case SDL_SCANCODE_INSERT:       return GREY | 0x52;
    case SDL_SCANCODE_DELETE:       return GREY | 0x53;
    default:                        return 0;
    }
}

static void send_key(u16 code, bool down)
{
    held[code & 0x1FF] = down;
    if (!kbd_handler) return;
    if (code & GREY) kbd_handler(0xE0);
    kbd_handler((u8)(down ? (code & 0x7F) : ((code & 0x7F) | 0x80)));
}

static void release_all(void)
{
    for (u16 c = 0; c < 0x200; c++)
        if (held[c]) send_key(c, false);
}

void host_input_event(const SDL_Event *ev)
{
    switch (ev->type) {
    case SDL_EVENT_KEY_DOWN:
        if (ev->key.repeat) break;                       /* platform.md §3.3: repeats change nothing */
        if (ev->key.key == SDLK_RETURN && (ev->key.mod & SDL_KMOD_ALT)) break;   /* full screen toggle */
        { u16 c = set1_scan(ev->key.scancode); if (c) send_key(c, true); }
        break;
    case SDL_EVENT_KEY_UP:
        { u16 c = set1_scan(ev->key.scancode); if (c && held[c]) send_key(c, false); }
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        release_all();
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (host_renderer) {
            SDL_Event e = *ev;
            SDL_ConvertEventToRenderCoordinates(host_renderer, &e);
            mouse_x = e.motion.x;
            mouse_y = e.motion.y;
        }
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
        if (!gamepad) gamepad = SDL_OpenGamepad(ev->gdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        if (gamepad && SDL_GetGamepadID(gamepad) == ev->gdevice.which) {
            SDL_CloseGamepad(gamepad);
            gamepad = NULL;
        }
        break;
    default:
        break;
    }
}

void host_input_shutdown(void)
{
    if (gamepad) SDL_CloseGamepad(gamepad);
    gamepad = NULL;
}

#define TAP_SECONDS 0.2

/* JS_KEYS="<seconds>:<xx>[+<xx>...][p|r],...": hex set-1 codes (e0xx = grey key), pressed in order and
 * released in reverse after TAP_SECONDS (so a per-frame poll sees them); p = press only (held),
 * r = release only. */
void host_input_script(void)
{
    static const char *spec;
    static bool checked;
    static u16 pending[8];                       /* keys of a tap, released after TAP_SECONDS */
    static int npending;
    static double release_at;
    if (!checked) { spec = SDL_getenv("JS_KEYS"); checked = true; }
    if (npending) {
        /* The game polls g_KeyDown once per frame, so a tap is held for a few frames. */
        if (host_seconds() < release_at) return;
        for (int i = npending - 1; i >= 0; i--) send_key(pending[i], false);
        npending = 0;
    }
    if (!spec || !*spec) return;
    char *end;
    double at = SDL_strtod(spec, &end);
    if (end == spec || *end != ':') { fprintf(stderr, "JS_KEYS: bad entry at \"%s\"\n", spec); spec = NULL; return; }
    if (host_seconds() < at) return;
    const char *p = end + 1;
    u16 keys[8];
    int n = 0;
    bool press = true, release = true;
    while (n < 8) {
        unsigned long v = SDL_strtoul(p, &end, 16);
        keys[n++] = (u16)((v >> 8) == 0xE0 ? (GREY | (v & 0x7F)) : (v & 0x7F));
        p = end;
        if (*p == 'p') { release = false; p++; }
        else if (*p == 'r') { press = false; p++; }
        if (*p != '+') break;
        p++;
    }
    if (press)
        for (int i = 0; i < n; i++) send_key(keys[i], true);
    if (press && release) {
        for (int i = 0; i < n; i++) pending[i] = keys[i];
        npending = n;
        release_at = host_seconds() + TAP_SECONDS;
    } else if (release) {
        for (int i = n - 1; i >= 0; i--) send_key(keys[i], false);
    }
    spec = *p == ',' ? p + 1 : NULL;
}

void host_mouse_read(s16 *x, s16 *y, u8 *buttons)
{
    SDL_MouseButtonFlags b = SDL_GetMouseState(NULL, NULL);
    if (x) *x = (s16)SDL_clamp(mouse_x, 0.0f, HOST_FRAME_W - 1.0f);
    if (y) *y = (s16)SDL_clamp(mouse_y, 0.0f, HOST_FRAME_H - 1.0f);
    if (buttons) *buttons = (u8)(((b & SDL_BUTTON_LMASK) ? 1 : 0) | ((b & SDL_BUTTON_RMASK) ? 2 : 0));
}

bool host_joy_read(s16 *x, s16 *y, u8 *buttons)
{
    if (!gamepad) return false;
    s16 ax = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
    s16 ay = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY);
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT))  ax = -32768;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) ax = 32767;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP))    ay = -32768;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN))  ay = 32767;
    u8 b = 0;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH)) b |= 1;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_EAST))  b |= 2;
    if (x) *x = ax;
    if (y) *y = ay;
    if (buttons) *buttons = b;
    return true;
}
