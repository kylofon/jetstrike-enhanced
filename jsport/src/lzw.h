#pragma once
/* LZW unpacker, LE object 2 of JS_CDROM.EXE (0x50000), FORMATS.md / tools/jsunpack.py. */
#include "types.h"

/* Unpacked size stored in a packed file (u32 LE at offset 0). */
u32 LZW_PackedSize(const u8 *packed);

/* 0x50000 LZW_Unpack: unpacks `packed` (srclen bytes, starting with the u32 size) into dst, which must
 * hold LZW_PackedSize(packed) bytes. Returns the number of bytes written.
 * PORT: bytes past srclen read as 0 and the output is clipped at the stored size (the asm stops
 * after the string that reaches it). */
u32 LZW_Unpack(const u8 *packed, u32 srclen, u8 *dst);
