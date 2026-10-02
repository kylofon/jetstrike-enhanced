/* JetStrike SDL3 port: entry point.
 *
 * usage: jsport [--game-dir DIR] [--scale N] [--fullscreen] [--sb-rate 19920|3906] [--lzw-dump OUTDIR]
 *   --game-dir    folder with the original game files (default: "Game" in the working directory)
 *   --scale       initial window size: 320x240 times N (default 3)
 *   --fullscreen  start in full screen (Alt+Enter switches)
 *   --sb-rate     Sound Blaster mixer rate: 19920 (designed, default) or 3906 (what the original programs)
 *   --lzw-dump    developer check: unpack every PAX/SPX/TLX/MXP/DX0/DX1 file of the game folder with the
 *                 port's LZW into OUTDIR (as <DIR>_<NAME>.bin) and exit (tools/lzw_check.py compares them)
 */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game_main.h"
#include "host.h"
#include "lzw.h"
#include "sound.h"
#include "video.h"

static int usage(const char *prog)
{
    fprintf(stderr, "usage: %s [--game-dir DIR] [--scale N] [--fullscreen] [--sb-rate 19920|3906] "
                    "[--lzw-dump OUTDIR]\n", prog);
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
    int scale = 3;
    bool fullscreen = false;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--game-dir") && v) { dir = v; i++; }
        else if (!strcmp(a, "--scale") && v) { scale = atoi(v); i++; }
        else if (!strcmp(a, "--fullscreen")) fullscreen = true;
        else if (!strcmp(a, "--sb-rate") && v) {
            g_SBRate = atoi(v) == SB_RATE_AS_CODED ? SB_RATE_AS_CODED : SB_RATE_DESIGNED;
            i++;
        }
        else if (!strcmp(a, "--lzw-dump") && v) { dump = v; i++; }
        else return usage(argv[0]);
    }

    if (dump) {
        if (!host_init(dir, scale, false, false)) return 1;
        int rc = lzw_dump(dir, dump);
        host_shutdown();
        return rc;
    }

    if (!host_init(dir, scale, fullscreen, true)) return 1;
    Video_Init();
    int rc = game_main();
    host_shutdown();
    return rc;
}
