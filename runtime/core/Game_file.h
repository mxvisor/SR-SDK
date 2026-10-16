/**
 *
 *  Copyright (C) 2026 mxvisor
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy of
 *  this software and associated documentation files (the "Software"), to deal in
 *  the Software without restriction, including without limitation the rights to
 *  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *  of the Software, and to permit persons to whom the Software is furnished to do
 *  so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *
 */

#ifndef _GAME_FILE_H_INCLUDED_
#define _GAME_FILE_H_INCLUDED_

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* An open DOS file: a host file (the game directory and the write root), or
 * a file on the CD (Game_cdrom.c), which is read out of the image sector by
 * sector.  The calls mirror the stdio ones the file I/O replacements need. */
typedef struct game_file game_file;

#ifdef __cplusplus
extern "C" {
#endif

game_file *game_file_host(FILE *file);
game_file *game_file_cd(uint32_t lba, uint32_t size);

size_t game_fread(void *buffer, size_t length, game_file *file);
size_t game_fwrite(const void *buffer, size_t length, game_file *file);
int game_fseek(game_file *file, long offset, int origin);
long game_ftell(game_file *file);
int game_fgetc(game_file *file);
int game_fungetc(int character, game_file *file);
int game_fputc(int character, game_file *file);
/* Cuts or extends the file to its current position, as DOS's zero-byte
 * write does; -1 on a CD file or a failure. */
int game_ftruncate(game_file *file);
int game_feof(game_file *file);
int game_ferror(game_file *file);
void game_clearerr(game_file *file);
void game_setbuf(game_file *file, int buffered);
int game_fclose(game_file *file);

#ifdef __cplusplus
}
#endif

#endif
