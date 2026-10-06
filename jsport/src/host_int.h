#pragma once
/* Shared between the host_*.c files only. */
#include <SDL3/SDL.h>

#include "host.h"

extern SDL_Window *host_window;          /* NULL when running without a window (--lzw-dump) */
extern SDL_Renderer *host_renderer;
extern Uint64 host_start_ns;

bool host_video_init(int window_scale, bool fullscreen);
void host_video_shutdown(void);
void host_video_event(const SDL_Event *ev);

void host_input_event(const SDL_Event *ev);
void host_input_script(void);            /* JS_KEYS */
void host_input_shutdown(void);
