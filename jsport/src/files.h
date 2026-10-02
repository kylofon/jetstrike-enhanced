#pragma once
/* File access (platform.md §8): DOS 8.3 truncation + case-insensitive open, File_LoadWhole. */
#include <stdio.h>

#include "types.h"

/* fopen of a game-relative DOS path ('/' or '\\'): every component is truncated to 8.3 as DOS does
 * ("jetstrike.spx" -> "JETSTRIK.SPX") and matched case-insensitively. New files are created with the
 * truncated name in upper case. */
FILE *Platform_Fopen(const char *path, const char *mode);
/* The 8.3-truncated, upper-case form of a path (for messages and new files). */
void  Dos_TruncatePath(const char *path, char *out, size_t n);

/* 0x12114 File_LoadWhole(dir, name, &buf, size): size >= 1 fixed length, -1 always malloc, 0 file size;
 * allocates if *buf is NULL. Returns the length. Missing file -> FatalError(path, " not found", 1). */
int File_LoadWhole(const char *dir, const char *name, void **buf, int size);
/* PORT: the capacity of a caller's buffer, for the overflow check File_LoadWhole cannot do (0 = unknown). */
void File_SetBufferCapacity(uintptr_t buf, size_t cap);
