/* File access: port/spec/platform.md §8. */
#include "files.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"
#include "platform.h"

/* One component: base = first 8 chars before the first '.', extension = first 3 chars after it;
 * characters after a second dot are dropped. */
static size_t truncate_component(const char *s, size_t len, char *out)
{
    size_t o = 0, i = 0, b = 0;
    while (i < len && s[i] != '.') { if (b < 8) { out[o++] = (char)toupper((unsigned char)s[i]); b++; } i++; }
    if (i < len) {
        size_t e = 0;
        i++;
        out[o++] = '.';
        while (i < len && s[i] != '.' && e < 3) { out[o++] = (char)toupper((unsigned char)s[i]); e++; i++; }
        if (e == 0) o--;                                /* "NAME." -> "NAME" */
    }
    return o;
}

void Dos_TruncatePath(const char *path, char *out, size_t n)
{
    size_t o = 0;
    const char *p = path;
    while (*p && o + 14 < n) {
        if (*p == '/' || *p == '\\') { out[o++] = '/'; p++; continue; }
        size_t len = strcspn(p, "/\\");
        o += truncate_component(p, len, out + o);
        p += len;
    }
    out[o] = 0;
}

FILE *Platform_Fopen(const char *path, const char *mode)
{
    char dos[260];
    Dos_TruncatePath(path, dos, sizeof dos);
    bool create = strchr(mode, 'w') || strchr(mode, 'a');
    char *real = host_game_path(dos, create);
    if (!real) return NULL;
    FILE *f = fopen(real, mode);
    host_free(real);
    return f;
}

/* PORT: buffer capacities for the overflow assert (the original has no bounds check). */
static struct { uintptr_t buf; size_t cap; } caps[8];

void File_SetBufferCapacity(uintptr_t buf, size_t cap)
{
    for (int i = 0; i < 8; i++)
        if (caps[i].buf == buf || !caps[i].buf) { caps[i].buf = buf; caps[i].cap = cap; return; }
}

static size_t capacity(uintptr_t buf)
{
    for (int i = 0; i < 8; i++) if (caps[i].buf == buf) return caps[i].cap;
    return 0;
}

/* 0x12114 File_LoadWhole */
int File_LoadWhole(const char *dir, const char *name, void **buf, int size)
{
    char path[108];
    snprintf(path, sizeof path, "%s%s", dir, name);
    FILE *f = Platform_Fopen(path, "rb");
    if (!f) FatalError(path, " not found", 1);
    int len;
    if (size >= 1) {
        len = size;
    } else {
        fseek(f, 0, SEEK_END);
        len = (int)ftell(f);
        fseek(f, 0, SEEK_SET);
        if (size == -1) {
            *buf = malloc((size_t)len);           /* leaks a buffer passed in, as the original */
            if (!*buf) FatalError("Error allocating memory. While trying to load", path, 6);
        }
    }
    if (*buf == NULL) *buf = malloc((size_t)len);
    if (*buf == NULL) FatalError("Error allocating memory. While trying to load", path, 6);
    size_t cap = capacity((uintptr_t)*buf);
    if (cap && (size_t)len > cap) {
        /* PORT: the original overflows the buffer here (no shipped file does). */
        FatalError(path, ": file larger than its load buffer (the original would overwrite memory)", 6);
    }
    if (fread(*buf, (size_t)len, 1, f) != 1) { /* short read: not checked by the original */ }
    fclose(f);
    return len;
}
