#include "host_int.h"

#include <stdio.h>

static SDL_Texture *texture;
static void (*frame_source)(u32 *);
static u32 frame[HOST_FRAME_W * HOST_FRAME_H];

bool host_video_init(int window_scale, bool fullscreen)
{
    if (window_scale < 1) window_scale = 3;
    if (!SDL_CreateWindowAndRenderer("JetStrike", HOST_FRAME_W * window_scale, HOST_FRAME_H * window_scale,
                                     SDL_WINDOW_RESIZABLE, &host_window, &host_renderer)) {
        fprintf(stderr, "window/renderer failed: %s\n", SDL_GetError());
        return false;
    }
    if (fullscreen) SDL_SetWindowFullscreen(host_window, true);
    /* 320x240 mode X has square pixels: a 4:3 letterboxed logical presentation. */
    SDL_SetRenderLogicalPresentation(host_renderer, HOST_FRAME_W, HOST_FRAME_H, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    texture = SDL_CreateTexture(host_renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                                HOST_FRAME_W, HOST_FRAME_H);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    return true;
}

void host_video_shutdown(void)
{
    if (texture) SDL_DestroyTexture(texture);
    if (host_renderer) SDL_DestroyRenderer(host_renderer);
    if (host_window) SDL_DestroyWindow(host_window);
    texture = NULL;
    host_renderer = NULL;
    host_window = NULL;
}

void host_video_event(const SDL_Event *ev)
{
    /* Alt+Enter toggles full screen (the key never reaches the game: host_input_event skips it). */
    if (ev->type == SDL_EVENT_KEY_DOWN && ev->key.key == SDLK_RETURN && (ev->key.mod & SDL_KMOD_ALT)
        && !ev->key.repeat && host_window)
        SDL_SetWindowFullscreen(host_window, !(SDL_GetWindowFlags(host_window) & SDL_WINDOW_FULLSCREEN));
}

void host_set_frame_source(void (*compose)(u32 *)) { frame_source = compose; }

/* Developer aid: JS_SNAPSHOT_DIR / JS_SNAPSHOT_MS (host.h). */
static void snapshot(void)
{
    static const char *dir;
    static bool checked;
    static Uint64 last_ns;
    static int n;
    static Uint64 interval_ns = 2 * SDL_NS_PER_SECOND;
    if (!checked) {
        dir = SDL_getenv("JS_SNAPSHOT_DIR");
        const char *ms = SDL_getenv("JS_SNAPSHOT_MS");
        if (ms && SDL_atoi(ms) > 0) interval_ns = (Uint64)SDL_atoi(ms) * SDL_NS_PER_MS;
        checked = true;
    }
    if (!dir || !*dir) return;
    Uint64 now = SDL_GetTicksNS();
    if (n && now - last_ns < interval_ns) return;
    last_ns = now;
    SDL_Surface *s = SDL_CreateSurfaceFrom(HOST_FRAME_W, HOST_FRAME_H, SDL_PIXELFORMAT_XRGB8888, frame,
                                           HOST_FRAME_W * 4);
    if (!s) return;
    char path[512];
    SDL_snprintf(path, sizeof path, "%s/snap%04d.png", dir, n++);
    if (!SDL_SavePNG(s, path)) fprintf(stderr, "snapshot %s: %s\n", path, SDL_GetError());
    SDL_DestroySurface(s);
}

void host_present(void)
{
    if (!frame_source) return;
    frame_source(frame);
    snapshot();
    if (!texture) return;
    SDL_UpdateTexture(texture, NULL, frame, HOST_FRAME_W * 4);
    SDL_SetRenderDrawColor(host_renderer, 0, 0, 0, 255);
    SDL_RenderClear(host_renderer);
    SDL_RenderTexture(host_renderer, texture, NULL, NULL);
    SDL_RenderPresent(host_renderer);
}
