#include "host_int.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

SDL_Window *host_window;
SDL_Renderer *host_renderer;
Uint64 host_start_ns;

static char *game_dir;
static double quit_after = -1;

bool host_init(const char *dir, int window_scale, bool fullscreen, bool open_window)
{
    SDL_InitFlags flags = open_window ? (SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD) : 0;
    if (!SDL_Init(flags)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    game_dir = SDL_strdup(dir);
    host_start_ns = SDL_GetTicksNS();
    const char *q = SDL_getenv("JS_QUIT_AFTER");
    if (q && *q) quit_after = SDL_atof(q);
    if (open_window && !host_video_init(window_scale, fullscreen)) return false;
    return true;
}

void host_shutdown(void)
{
    host_audio_close();
    host_input_shutdown();
    host_video_shutdown();
    SDL_free(game_dir);
    game_dir = NULL;
    SDL_Quit();
}

double host_seconds(void) { return (double)(SDL_GetTicksNS() - host_start_ns) / 1e9; }

void host_pump(void)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_EVENT_QUIT:
            host_shutdown();
            exit(0);
        default:
            host_video_event(&ev);
            host_input_event(&ev);
            break;
        }
    }
    host_input_script();
    if (quit_after >= 0 && host_seconds() >= quit_after) {
        host_shutdown();
        exit(0);
    }
}

/* ---------------------------------------------------------------- files */

/* Case-insensitive match of one path component inside dir; returns a malloc'd "dir/entry" or NULL. */
static char *match_component(const char *dir, const char *name)
{
    char *direct = NULL;
    SDL_asprintf(&direct, "%s/%s", dir, name);
    if (SDL_GetPathInfo(direct, NULL)) return direct;
    SDL_free(direct);
    int count = 0;
    char **entries = SDL_GlobDirectory(dir, NULL, 0, &count);
    char *found = NULL;
    for (int i = 0; entries && i < count; i++)
        if (SDL_strcasecmp(entries[i], name) == 0) {
            SDL_asprintf(&found, "%s/%s", dir, entries[i]);
            break;
        }
    SDL_free(entries);
    return found;
}

char *host_game_path(const char *rel, bool create)
{
    char *cur = SDL_strdup(game_dir ? game_dir : ".");
    const char *p = rel;
    while (*p) {
        while (*p == '/' || *p == '\\') p++;
        if (!*p) break;
        size_t n = strcspn(p, "/\\");
        char comp[256];
        SDL_strlcpy(comp, p, SDL_min(n + 1, sizeof comp));
        p += n;
        bool last = strspn(p, "/\\") == strlen(p);
        char *next = match_component(cur, comp);
        if (!next) {
            if (!(create && last)) { SDL_free(cur); return NULL; }
            SDL_asprintf(&next, "%s/%s", cur, comp);
        }
        SDL_free(cur);
        cur = next;
    }
    return cur;
}

void host_free(void *p) { SDL_free(p); }

_Noreturn void host_fatal_code(int code, const char *fmt, ...)
{
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    fprintf(stderr, "%s\n", msg);
    const char *drv = SDL_GetCurrentVideoDriver();
    if (host_window && !(drv && SDL_strcmp(drv, "dummy") == 0))
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "JetStrike", msg, host_window);
    host_shutdown();
    exit(code);
}
