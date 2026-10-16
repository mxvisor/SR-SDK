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

#ifndef GAME_RUNTIME_H
#define GAME_RUNTIME_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "Game_defs.h"     /* CCALL */
#include "Game_file.h"

/* DOS file handles: one table for both the INT 21h path (Game_runtime.c) and
 * the host-side C library replacements (the C library layer, CLIB.c), so a handle from
 * either side means the same file to the other -- the game reads fileno()
 * out of a FILE and hands it to filelength(). */
#define GAME_MAX_HANDLES 64

#ifdef __cplusplus
extern "C" {
#endif

int Game_ResolveDataPath(const char *dos_path, char *host_path, size_t host_path_size);
/* The game directory (GAME_ENV("DATA_DIR"), else the current directory), absolute;
 * NULL when it has no game data.  Valid before Game_RuntimeInit. */
const char *Game_DataRoot(void);
/* GAME_ENV("WRITE_ROOT"), else the game directory (gamefiles/ in the current directory
 * when the game data is not found); Game_RuntimeInit calls it, and so may main
 * before it. */
int Game_SelectWriteRoot(void);
const char *Game_WriteRoot(void);
/* The write root is the game directory: no overlay, one copy of each file. */
int Game_WriteRootIsDataRoot(void);
int Game_GameDirWritePath(const char *dos_path, char *host_path, size_t host_path_size, void **new_in);
void Game_GameDirAdded(void *new_in, const char *host_path);
int Game_AttachHandle(game_file *file);
game_file *Game_HandleFile(int handle);
/* A path on the CD drive: *on_cd = 1 and the file opened (NULL, errno set,
 * when it is missing or `write` is asked for -- the CD is read-only).
 * Otherwise *on_cd = 0. */
game_file *Game_OpenCdFile(const char *dos_path, int write, int *on_cd);
/* Opens dos_path as the game's fopen does (the C library layer, CLIB.c), with `kind` 'r',
 * 'w' or 'a' and `update` the '+': the CD, the write root or the game
 * directory.  NULL on failure; errno is EACCES for a write to the CD. */
game_file *Game_OpenGameFile(const char *dos_path, char kind, int update);
void Game_ReleaseHandle(int handle);
int Game_TraceFileEnabled(void);
void Game_DumpGuestMemory(void);

int key_available(void);
uint8_t dos_blocking_getch(void);
uint8_t dos_scan_to_ascii(uint8_t scan_code);
int Game_RuntimeInit(void);
void Game_RuntimeDeinit(void);
void Game_RequestPresent(void);
void Game_DrainEventKeys(void);
/* Reprogram channel 0 as a guest `out 43h/40h` sequence would (Game_sound.c's
 * AIL timers). */
void Game_ProgramPit(uint32_t divisor);
/* Call a guest timer callback as AIL's ISR does; 0 if it could not run now
 * (guest IF clear, or inside another guest ISR). */
int Game_CallGuestTimerCallback(uint32_t target, uint32_t user);
/* Guest IF set and no guest ISR running: a hardware interrupt could be taken. */
int Game_GuestInterruptsEnabled(void);
/* Host address of a flat address in obj2 (the game's data, 0x90000-0x110000). */
void *Game_Obj2Pointer(uint32_t flat);

#ifdef __cplusplus
}
#endif

#endif
