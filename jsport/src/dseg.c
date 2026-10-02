/* Data-segment image: a minimal LE loader for object 5 of JS_CDROM.EXE (same logic as tools/lefile.py:
 * page map, internal fixups; only the sites inside the data object are applied). */
#include "dseg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"

u8 g_dseg[DSEG_SIZE + DSEG_SLACK];
u8 *g_dsegp = g_dseg;

static const u8 *exe;
static size_t exe_len;

static u32 rd32(size_t o) { return o + 4 <= exe_len ? (u32)exe[o] | (u32)exe[o + 1] << 8 | (u32)exe[o + 2] << 16 | (u32)exe[o + 3] << 24 : 0; }
static u16 rd16(size_t o) { return o + 2 <= exe_len ? (u16)(exe[o] | exe[o + 1] << 8) : 0; }
static u8  rd8(size_t o)  { return o < exe_len ? exe[o] : 0; }

_Noreturn static void bad(const char *why)
{
    host_fatal_code(1, "JS_CDROM.EXE: %s\n\nThe port reads the data segment of the original executable "
                       "(Game/JS_CDROM.EXE, CD version 1994).", why);
}

static void put(u32 site, int size, u32 val)
{
    if (site < DSEG_BASE || site + (u32)size > DSEG_BASE + DSEG_SIZE) return;
    for (int i = 0; i < size; i++) g_dseg[site - DSEG_BASE + (u32)i] = (u8)(val >> (8 * i));
}

void Dseg_Load(void)
{
    char *path = host_game_path("JS_CDROM.EXE", false);
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
    if (nobj != 5 || page_size != 0x1000) bad("unexpected object layout (not the CD version?)");
    u32 obase[6], opidx[6], opages[6], ovsize[6];
    for (u32 i = 0; i < nobj; i++) {
        size_t o = h + obj_tab + 24 * i;
        ovsize[i + 1] = rd32(o); obase[i + 1] = rd32(o + 4); opidx[i + 1] = rd32(o + 12); opages[i + 1] = rd32(o + 16);
    }
    if (obase[5] != DSEG_BASE || ovsize[5] != DSEG_SIZE) bad("data object is not at 0x80000 / 0x14750 bytes");

    memset(g_dseg, 0, sizeof g_dseg);
    for (u32 k = 0; k < opages[5]; k++) {
        u32 page = opidx[5] + k;                                 /* 1-based */
        size_t pm = h + pmap + 4 * (page - 1);
        u32 num = (u32)rd8(pm) << 16 | (u32)rd8(pm + 1) << 8 | rd8(pm + 2);
        if (rd8(pm + 3) != 0) bad("iterated/invalid data page");
        u32 size = page == num_pages ? last_page : page_size;
        size_t off = data_pages + (size_t)(num - 1) * page_size;
        if (off + size > exe_len) bad("truncated");
        u32 va = DSEG_BASE + k * page_size;
        u32 room = DSEG_SIZE - (va - DSEG_BASE);
        memcpy(&g_dseg[va - DSEG_BASE], exe + off, size < room ? size : room);
    }

    /* Fixups of the data object's pages (lefile.py _page_fixups). */
    int applied = 0;
    for (u32 k = 0; k < opages[5]; k++) {
        u32 page = opidx[5] + k;
        u32 page_va = DSEG_BASE + k * page_size;
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
            if (tobj < 1 || tobj > 5) bad("fixup target object");
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
                applied++;
            }
            if (list) p += 2 * (size_t)cnt;
        }
    }
    /* Sanity: the small-font charset pointer 0x80011 -> 0x80BF0 "ABC..." (a fixup in the data object). */
    if (D32(0x80011) != 0x80BF0 || memcmp(DSEG(0x80BF0), "ABCDEFGHIJ", 10) != 0)
        bad("data segment check failed (not the CD version?)");
    free(buf);
    exe = NULL;
    (void)applied;
}
