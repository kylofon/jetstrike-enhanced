/* JetStrike SDL3 port: entry point.
 *
 * usage: jsenh [--game-dir DIR] [--view WxH] [--scale N] [--fullscreen] [--sb-rate 19920|3906] [--no-intro]
 *              [--lzw-dump OUTDIR]
 *   --game-dir    folder with the original game files (default: "Game" in the working directory)
 *   --view        ENH: the mission screen including the HUD (PLAN.md): width a multiple of 16 in 320..960,
 *                 height 240..540; 320x240 is the original (default for now). Front end and intro stay 320x240.
 *   --scale       initial window size: the view times N (default 3)
 *   --fullscreen  start in full screen (Alt+Enter switches)
 *   --sb-rate     Sound Blaster mixer rate: 3906 (what the original programs, default) or 19920 (the designed rate);
 *                 the intro's mixer: 40000 (designed) or 3906
 *   --no-intro    PORT: skip the intro (INTRO.EXE, which JS.BAT runs before the game)
 *   --lzw-dump    developer check: unpack every PAX/SPX/TLX/MXP/DX0/DX1 file of the game folder with the
 *                 port's LZW into OUTDIR (as <DIR>_<NAME>.bin) and exit (tools/lzw_check.py compares them)
 */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dseg.h"
#include "host.h"
#include "intro.h"
#include "lzw.h"
#include "platform.h"
#include "sound.h"
#include "video.h"

/* ENH: the view without --view. PLAN.md decision 1 makes 640x360 the default once the wide renderer is in. */
#define VIEW_DEFAULT_W 640
#define VIEW_DEFAULT_H 360

static int usage(const char *prog)
{
    fprintf(stderr, "usage: %s [--game-dir DIR] [--view WxH] [--scale N] [--fullscreen] [--sb-rate 19920|3906] "
                    "[--no-intro] [--lzw-dump OUTDIR]\n", prog);
    return 2;
}

static bool has_packed_ext(const char *name)
{
    static const char *const exts[] = { ".PAX", ".SPX", ".TLX", ".MXP", ".DX0", ".DX1" };
    size_t n = strlen(name);
    if (n < 4) return false;
    for (size_t i = 0; i < SDL_arraysize(exts); i++)
        if (SDL_strcasecmp(name + n - 4, exts[i]) == 0) return true;
    return false;
}

/* Developer check (PORTING.md "Verification"). */
static int lzw_dump(const char *game_dir, const char *out_dir)
{
    SDL_CreateDirectory(out_dir);
    int count = 0, files = 0, failed = 0;
    char **subdirs = SDL_GlobDirectory(game_dir, NULL, 0, &count);
    for (int i = 0; subdirs && i < count; i++) {
        char *dir = NULL;
        SDL_asprintf(&dir, "%s/%s", game_dir, subdirs[i]);
        SDL_PathInfo info;
        if (!SDL_GetPathInfo(dir, &info) || info.type != SDL_PATHTYPE_DIRECTORY) { SDL_free(dir); continue; }
        int n = 0;
        char **entries = SDL_GlobDirectory(dir, NULL, 0, &n);
        for (int k = 0; entries && k < n; k++) {
            if (!has_packed_ext(entries[k])) continue;
            char *path = NULL, *out = NULL;
            SDL_asprintf(&path, "%s/%s", dir, entries[k]);
            SDL_asprintf(&out, "%s/%s_%s.bin", out_dir, subdirs[i], entries[k]);
            size_t len = 0;
            u8 *src = SDL_LoadFile(path, &len);
            if (src && len >= 4) {
                u32 size = LZW_PackedSize(src);
                u8 *dst = malloc(size ? size : 1);
                u32 got = dst ? LZW_Unpack(src, (u32)len, dst) : 0;
                if (!dst || !SDL_SaveFile(out, dst, got)) failed++;
                files++;
                free(dst);
            } else {
                failed++;
            }
            SDL_free(src);
            SDL_free(path);
            SDL_free(out);
        }
        SDL_free(entries);
        SDL_free(dir);
    }
    SDL_free(subdirs);
    printf("lzw-dump: %d files unpacked to %s, %d failed\n", files, out_dir, failed);
    return failed ? 1 : 0;
}

int main(int argc, char **argv)
{
    const char *dir = "Game", *dump = NULL;
    int scale = 3, view_w = VIEW_DEFAULT_W, view_h = VIEW_DEFAULT_H;
    bool fullscreen = false, intro = true;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--game-dir") && v) { dir = v; i++; }
        else if (!strcmp(a, "--view") && v) {
            char x, end;
            const char *err = (sscanf(v, "%d%c%d%c", &view_w, &x, &view_h, &end) == 3 && (x == 'x' || x == 'X'))
                              ? Video_CheckView(view_w, view_h) : "expected WxH, e.g. 640x360";
            if (err) {
                fprintf(stderr, "%s: --view %s: %s\n", argv[0], v, err);
                return 2;
            }
            i++;
        }
        else if (!strcmp(a, "--scale") && v) { scale = atoi(v); i++; }
        else if (!strcmp(a, "--fullscreen")) fullscreen = true;
        else if (!strcmp(a, "--sb-rate") && v) {
            g_SBRate = atoi(v) == SB_RATE_AS_CODED ? SB_RATE_AS_CODED : SB_RATE_DESIGNED;
            i++;
        }
        else if (!strcmp(a, "--no-intro")) intro = false;
        else if (!strcmp(a, "--lzw-dump") && v) { dump = v; i++; }
        else return usage(argv[0]);
    }

    if (dump) {
        if (!host_init(dir, view_w, view_h, scale, false, false)) return 1;
        int rc = lzw_dump(dir, dump);
        host_shutdown();
        return rc;
    }

    if (!host_init(dir, view_w, view_h, scale, fullscreen, true)) return 1;
    Video_Init(view_w, view_h);
    if (intro) Intro_Run();                     /* JS.BAT: cd intro / intro / cd .. / js_cdrom */
    Dseg_Load();
    int rc = js_main();                         /* 0x146f4 main */
    host_shutdown();
    return rc;
}
