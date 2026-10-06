#include "host_int.h"

#include <stdio.h>

static SDL_Texture *texture;
static void (*frame_source)(u32 *, int *, int *);
static u32 frame[HOST_FRAME_MAX_W * HOST_FRAME_MAX_H];
static int frame_w = HOST_FRAME_W, frame_h = HOST_FRAME_H;     /* size of the last composed frame */

/* Texture and logical presentation for a w x h frame. Mode X has square pixels. ENH: the frame is scaled by the
 * largest whole factor that fits the window (pillar/letterboxed; smaller windows scale it down), so a 320x240
 * front-end frame in a mission-sized window is centred with borders and stays sharp. */
static void set_frame_size(int w, int h)
{
    if (texture) SDL_DestroyTexture(texture);
    texture = SDL_CreateTexture(host_renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    if (texture) SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    SDL_SetRenderLogicalPresentation(host_renderer, w, h, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
}

bool host_video_init(int view_w, int view_h, int window_scale, bool fullscreen)
{
    if (window_scale < 1) window_scale = 3;
    /* ENH: the window is the view times the scale, lowered until it fits the desktop (never below 1). */
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable) && usable.w > 0 && usable.h > 0)
        while (window_scale > 1 && (view_w * window_scale > usable.w || view_h * window_scale > usable.h - 40))
            window_scale--;                         /* 40: the title bar */
    if (!SDL_CreateWindowAndRenderer("JetStrike Enhanced", view_w * window_scale, view_h * window_scale,
                                     SDL_WINDOW_RESIZABLE, &host_window, &host_renderer)) {
        fprintf(stderr, "window/renderer failed: %s\n", SDL_GetError());
        return false;
    }
    if (fullscreen) SDL_SetWindowFullscreen(host_window, true);
    set_frame_size(frame_w, frame_h);
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

void host_set_frame_source(void (*compose)(u32 *, int *, int *)) { frame_source = compose; }

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
    SDL_Surface *s = SDL_CreateSurfaceFrom(frame_w, frame_h, SDL_PIXELFORMAT_XRGB8888, frame, frame_w * 4);
    if (!s) return;
    char path[512];
    SDL_snprintf(path, sizeof path, "%s/snap%04d.png", dir, n++);
    if (!SDL_SavePNG(s, path)) fprintf(stderr, "snapshot %s: %s\n", path, SDL_GetError());
    SDL_DestroySurface(s);
}

/* Developer aid: JS_SNAP_AT="t1,t2,..." (ascending seconds on the JS_KEYS clock, use with JS_VCLOCK=1) saves the
 * first presented frame at or after each time as dir/at_NNN.png (dir = JS_SNAPSHOT_DIR) and appends
 * "NNN <time> <FNV-1a of the RGB pixels>" to dir/snap.txt: the reference data of tools/snapcheck. */
static void snapshot_at(void)
{
    static const char *spec;
    static bool checked;
    static int n;
    if (!checked) {
        const char *d = SDL_getenv("JS_SNAPSHOT_DIR");
        spec = (d && *d) ? SDL_getenv("JS_SNAP_AT") : NULL;
        checked = true;
    }
    if (!spec || !*spec) return;
    char *end;
    double at = SDL_strtod(spec, &end);
    if (end == spec) { spec = NULL; return; }
    if (host_script_seconds() < at) return;
    uint32_t h = 2166136261u;
    for (int i = 0; i < frame_w * frame_h; i++) {
        uint32_t px = frame[i];
        for (int k = 0; k < 3; k++) { h ^= (px >> (8 * k)) & 0xFF; h *= 16777619u; }
    }
    const char *dir = SDL_getenv("JS_SNAPSHOT_DIR");
    char path[512];
    SDL_Surface *s = SDL_CreateSurfaceFrom(frame_w, frame_h, SDL_PIXELFORMAT_XRGB8888, frame, frame_w * 4);
    SDL_snprintf(path, sizeof path, "%s/at_%03d.png", dir, n);
    if (s) { SDL_SavePNG(s, path); SDL_DestroySurface(s); }
    SDL_snprintf(path, sizeof path, "%s/snap.txt", dir);
    FILE *f = fopen(path, "a");
    if (f) { fprintf(f, "%03d %.2f %08x\n", n, at, h); fclose(f); }
    n++;
    spec = (*end == ',') ? end + 1 : NULL;
}

void host_present(void)
{
    if (!frame_source) return;
    int w = HOST_FRAME_W, h = HOST_FRAME_H;
    frame_source(frame, &w, &h);
    bool resized = w != frame_w || h != frame_h;
    frame_w = w;
    frame_h = h;
    snapshot();
    snapshot_at();
    if (!host_renderer) return;
    if (resized) set_frame_size(w, h);
    if (!texture) return;
    SDL_UpdateTexture(texture, NULL, frame, frame_w * 4);
    SDL_SetRenderDrawColor(host_renderer, 0, 0, 0, 255);
    SDL_RenderClear(host_renderer);
    SDL_RenderTexture(host_renderer, texture, NULL, NULL);
    SDL_RenderPresent(host_renderer);
}
