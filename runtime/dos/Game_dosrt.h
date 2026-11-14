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

#ifndef _GAME_DOSRT_H_INCLUDED_
#define _GAME_DOSRT_H_INCLUDED_

#include <stdint.h>

/* The frame of a DOS4GW game's runtime: start and end, the I/O ports and the
 * segment-override accesses of the translated code (X86_InPortProcedure,
 * X86_OutPortProcedure, X86_ReadMemProcedure, X86_WriteMemProcedure).
 *
 * A port goes to the game's GAME_DOS_PORT_IN(port, &value) or
 * GAME_DOS_PORT_OUT(port, value) first, when Game_hooks.h defines them
 * (nonzero: served), then to the PIT (40h, 43h), the keyboard (60h, 64h) and
 * the VGA (3C7h-3C9h, 3DAh); any other reads 0.
 *
 * A segment-override access with an offset below 100h goes to the PSP or to
 * the latest DOS block (Game_dosrt.c), one above to the DOS address itself
 * (Game_dosmem.h: the memory and the VGA window at A0000h); past the window
 * nothing is there.
 *
 * Game_DumpGuestMemory (called when the game fails to open its error log)
 * calls GAME_DUMP_GUEST_MEMORY() when Game_hooks.h defines it. */

#ifdef __cplusplus
extern "C" {
#endif

/* The game directory, the DOS memory with the first first_free bytes from the
 * PSP on taken (Game_DosMemInit), the VGA window as Game_FrameBuffer and
 * Game_ScreenWindow, the vectors (Game_DosIntInit, all to the stub of
 * llasm/Game-llasm.llasm), the DOS files, the keyboard, the PIT and the audio
 * device.  0 on success. */
int Game_DosRuntimeInit(uint32_t first_free);
void Game_DosRuntimeDeinit(void);

/* Any thread: asks the flip thread to show a frame (Game_RequestFlip). */
void Game_DosRequestPresent(void);
/* The milliseconds since the last Game_DosRequestPresent */
uint32_t Game_DosSincePresent(void);

#ifdef __cplusplus
}
#endif

#endif
