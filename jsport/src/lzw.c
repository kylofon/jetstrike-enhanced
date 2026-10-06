/* 0x50000 LZW_Unpack (+ LZW_Init 0x50020, LZW_ReadCode 0x50090, LZW_Decode 0x501e0): hand-written asm,
 * ported from tools/jsunpack.py, which reproduces it.
 *
 * Packed file: u32 LE unpacked size, then an MSB-first bit stream of 9..12-bit codes.
 *  - dictionary entry = (prefix code, char); 0..255 are the bytes; nxt (last used entry) starts at 0xFF;
 *  - code == (1 << bits) - 1 is an escape: bits += 1 (and if nxt == that code - 1, nxt += 1), read again;
 *  - a new entry skips the index (1 << bits) - 1; the dictionary stops growing at 0xFFF;
 *  - output lags one code: each step reads a code, adds an entry, then writes the string of the previous
 *    code; it stops once `size` bytes are written (the last code read is never written). */
#include "lzw.h"

#define DICT 0x1000

typedef struct {
    const u8 *d;
    u32 len, pos;
    u32 acc;
    int n;
} Bits;

static u32 get_bits(Bits *b, int bits)
{
    while (b->n < bits) {
        u8 v = b->pos < b->len ? b->d[b->pos] : 0;
        b->pos++;
        b->acc = (b->acc << 8) | v;
        b->n += 8;
    }
    b->n -= bits;
    u32 v = (b->acc >> b->n) & ((1u << bits) - 1);
    b->acc &= (1u << b->n) - 1;
    return v;
}

typedef struct {
    Bits br;
    int bits;
    u32 nxt;
    u16 prefix[DICT];
    u8 chr[DICT];
} Lzw;

static u32 read_code(Lzw *z)
{
    for (;;) {
        u32 c = get_bits(&z->br, z->bits);
        u32 esc = (1u << z->bits) - 1;
        if (c != esc) return c;
        z->bits++;
        if (esc - 1 == z->nxt) z->nxt++;
        if (z->bits > 24) return 0;             /* PORT: corrupt stream guard (never in game data) */
    }
}

static void add_entry(Lzw *z, u32 pfx, u8 ch)
{
    if (z->nxt >= 0xFFF) return;
    z->nxt++;
    if (z->nxt == (1u << z->bits) - 1) {
        if (z->nxt == 0xFFF) return;
        z->nxt++;
    }
    z->prefix[z->nxt] = (u16)pfx;
    z->chr[z->nxt] = ch;
}

static u8 root(const Lzw *z, u32 c)
{
    int guard = DICT;
    while (c >= 0x100 && guard--) c = z->prefix[c & (DICT - 1)];
    return (u8)c;
}

u32 LZW_PackedSize(const u8 *packed)
{
    return (u32)packed[0] | (u32)packed[1] << 8 | (u32)packed[2] << 16 | (u32)packed[3] << 24;
}

u32 LZW_Unpack(const u8 *packed, u32 srclen, u8 *dst)
{
    static Lzw z;
    if (srclen < 4) return 0;
    u32 size = LZW_PackedSize(packed);
    for (u32 i = 0; i < DICT; i++) { z.prefix[i] = 0; z.chr[i] = i < 0x100 ? (u8)i : 0; }
    z.bits = 9;
    z.nxt = 0xFF;
    z.br = (Bits){ packed, srclen, 4, 0, 0 };

    u32 out = 0;
    u32 old = read_code(&z);
    u8 stack[DICT];
    while (out < size) {
        u32 c = read_code(&z);
        add_entry(&z, old, c > z.nxt ? root(&z, old) : root(&z, c));
        int sp = 0;
        u32 x = old;
        for (;;) {
            stack[sp++] = z.chr[x & (DICT - 1)];
            if (x < 0x100 || sp == DICT) break;
            x = z.prefix[x & (DICT - 1)];
        }
        while (sp > 0) {
            u8 v = stack[--sp];
            if (out < size) dst[out] = v;
            out++;
        }
        old = c;
    }
    return out < size ? out : size;
}
