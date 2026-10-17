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

#ifndef _GAME_DOSFILE_H_INCLUDED_
#define _GAME_DOSFILE_H_INCLUDED_

#include <stdint.h>

/* The file functions of INT 21h for a DOS4GW game, on the game directory,
 * the disc and the handle table of Game_gamedir.c (Game_vfs.h). */

#ifdef __cplusplus
extern "C" {
#endif

/* The registers of a software interrupt, as SR's X86_InterruptProcedure
 * hands them over: pushad order, then the flags. */
typedef struct {
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t ignored_esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t eflags;
} Game_DosRegs;

/* The default DTA, no search in progress. */
void Game_DosFileInit(void);
void Game_DosFileDeinit(void);
/* AH = 1Ah (set DTA), 2Fh (get DTA), 3Ch (create), 3Dh (open), 3Eh (close),
 * 3Fh (read), 40h (write), 42h (seek), 4Eh (find first), 4Fh (find next):
 * done, with CF and AX as DOS leaves them, and 1 returned.  Any other AH: 0,
 * the registers untouched.  `pointer` turns a guest address (DS:EDX, the DTA)
 * into a host pointer, as the game's runtime does. */
int Game_DosFileCall(Game_DosRegs *regs, void *(*pointer)(uint32_t address));

#ifdef __cplusplus
}
#endif

#endif
