/* Data-segment images: a minimal LE loader (same logic as tools/lefile.py: page map, internal fixups; only
 * the sites inside the loaded object are applied) for object 5 of JS_CDROM.EXE and object 6 of
 * INTRO/INTRO.EXE (the intro's data, intro.c). */
#include "dseg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"

u8 g_dseg[DSEG_SIZE + DSEG_SLACK];
u8 *g_dsegp = g_dseg;
u8 g_iseg[ISEG_SIZE + ISEG_SLACK];
u8 *g_isegp = g_iseg;

static const u8 *exe;
static size_t exe_len;
static const char *exe_name;

static u32 rd32(size_t o) { return o + 4 <= exe_len ? (u32)exe[o] | (u32)exe[o + 1] << 8 | (u32)exe[o + 2] << 16 | (u32)exe[o + 3] << 24 : 0; }
static u16 rd16(size_t o) { return o + 2 <= exe_len ? (u16)(exe[o] | exe[o + 1] << 8) : 0; }
static u8  rd8(size_t o)  { return o < exe_len ? exe[o] : 0; }

_Noreturn static void bad(const char *why)
{
    host_fatal_code(1, "%s: %s\n\nThe port reads the data segment of the original executable "
                       "(Game/%s, CD version 1994).", exe_name, why, exe_name);
}

static u8 *put_dst;
static u32 put_base, put_size;

static void put(u32 site, int size, u32 val)
{
    if (site < put_base || site + (u32)size > put_base + put_size) return;
    for (int i = 0; i < size; i++) put_dst[site - put_base + (u32)i] = (u8)(val >> (8 * i));
}

/* Loads LE object `obj` (1-based) of the game-relative executable `name` into dst (base..base+size, zero
 * filled first: the BSS part), page map and the internal fixups whose sites lie inside the object. The exe
 * must have `nobj` objects and the object must sit at `base` with virtual size `size`. Fatal error otherwise. */
static void le_load_object(const char *name, u32 nobj_expect, u32 obj, u32 base, u32 size, u8 *dst)
{
    exe_name = name;
    char *path = host_game_path(name, false);
    if (!path) bad("not found in the game folder");
    FILE *f = fopen(path, "rb");
    host_free(path);
    if (!f) bad("cannot be opened");
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *buf = malloc((size_t)(n > 0 ? n : 1));
    if (!buf || fread(buf, (size_t)n, 1, f) != 1) { fclose(f); bad("read error"); }
    fclose(f);
    exe = buf;
    exe_len = (size_t)n;

    if (exe_len < 0x40 || exe[0] != 'M' || exe[1] != 'Z') bad("not an MZ executable");
    size_t h = rd32(0x3C);
    if (rd8(h) != 'L' || rd8(h + 1) != 'E') bad("no LE header");
    u32 num_pages = rd32(h + 0x14), page_size = rd32(h + 0x28), last_page = rd32(h + 0x2C);
    u32 obj_tab = rd32(h + 0x40), nobj = rd32(h + 0x44), pmap = rd32(h + 0x48);
    size_t fix_page_tab = h + rd32(h + 0x68), fix_rec_tab = h + rd32(h + 0x6C);
    u32 data_pages = rd32(h + 0x80);
    if (nobj != nobj_expect || page_size != 0x1000 || obj < 1 || obj > nobj || nobj > 8)
        bad("unexpected object layout (not the CD version?)");
    u32 obase[9], opidx[9], opages[9], ovsize[9];
    for (u32 i = 0; i < nobj; i++) {
        size_t o = h + obj_tab + 24 * i;
        ovsize[i + 1] = rd32(o); obase[i + 1] = rd32(o + 4); opidx[i + 1] = rd32(o + 12); opages[i + 1] = rd32(o + 16);
    }
    if (obase[obj] != base || ovsize[obj] != size) bad("data object is not at the expected address / size");

    put_dst = dst;
    put_base = base;
    put_size = size;
    memset(dst, 0, size);
    for (u32 k = 0; k < opages[obj]; k++) {
        u32 page = opidx[obj] + k;                               /* 1-based */
        size_t pm = h + pmap + 4 * (page - 1);
        u32 num = (u32)rd8(pm) << 16 | (u32)rd8(pm + 1) << 8 | rd8(pm + 2);
        if (rd8(pm + 3) != 0) bad("iterated/invalid data page");
        u32 psize = page == num_pages ? last_page : page_size;
        size_t off = data_pages + (size_t)(num - 1) * page_size;
        if (off + psize > exe_len) bad("truncated");
        u32 va = base + k * page_size;
        u32 room = size - (va - base);
        memcpy(&dst[va - base], exe + off, psize < room ? psize : room);
    }

    /* Fixups of the object's pages (lefile.py _page_fixups). */
    for (u32 k = 0; k < opages[obj]; k++) {
        u32 page = opidx[obj] + k;
        u32 page_va = base + k * page_size;
        size_t p = fix_rec_tab + rd32(fix_page_tab + 4 * (page - 1));
        size_t end = fix_rec_tab + rd32(fix_page_tab + 4 * page);
        while (p < end) {
            u8 src = rd8(p), flg = rd8(p + 1);
            p += 2;
            int stype = src & 0x0F, cnt = 1;
            s16 one = 0;
            bool list = (src & 0x10) != 0;
            if (list) cnt = rd8(p++);
            else { one = (s16)rd16(p); p += 2; }
            if (flg & 3) bad("import fixup in the data object");
            u32 tobj;
            if (flg & 0x40) { tobj = rd16(p); p += 2; } else tobj = rd8(p++);
            u32 toff = 0;
            if (stype != 2) {
                if (flg & 0x10) { toff = rd32(p); p += 4; } else { toff = rd16(p); p += 2; }
            }
            if (tobj < 1 || tobj > nobj) bad("fixup target object");
            u32 target = obase[tobj] + toff;
            for (int i = 0; i < cnt; i++) {
                s16 so = list ? (s16)rd16(p + 2 * (size_t)i) : one;
                u32 site = page_va + (u32)(s32)so;
                switch (stype) {
                case 7: put(site, 4, target); break;                     /* off32 */
                case 5: put(site, 2, target); break;                     /* off16 */
                case 8: put(site, 4, target - (site + 4)); break;        /* rel32 */
                case 2: put(site, 2, 0); break;                          /* sel16: flat */
                case 6: put(site, 4, target); put(site + 4, 2, 0); break;
                case 3: put(site, 2, target); put(site + 2, 2, 0); break;
                case 0: put(site, 1, target); break;
                default: bad("unknown fixup type");
                }
            }
            if (list) p += 2 * (size_t)cnt;
        }
    }
    free(buf);
    exe = NULL;
}

void Dseg_Load(void)
{
    le_load_object("JS_CDROM.EXE", 5, 5, DSEG_BASE, DSEG_SIZE, g_dseg);
    /* Sanity: the small-font charset pointer 0x80011 -> 0x80BF0 "ABC..." (a fixup in the data object). */
    if (D32(0x80011) != 0x80BF0 || memcmp(DSEG(0x80BF0), "ABCDEFGHIJ", 10) != 0)
        bad("data segment check failed (not the CD version?)");
}

void Iseg_Load(void)
{
    le_load_object("INTRO/INTRO.EXE", 6, 6, ISEG_BASE, ISEG_SIZE, g_iseg);
    /* Sanity: the glyph-set pointer 0x607b5 -> 0x60260 "ABC..." (a fixup in the data object). */
    if (I32(0x607B5) != 0x60260 || memcmp(ISEG(0x60260), "ABCDEFGHIJ", 10) != 0)
        bad("data segment check failed (not the CD version?)");
}
