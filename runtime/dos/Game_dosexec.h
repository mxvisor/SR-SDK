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

#ifndef _GAME_DOSEXEC_H_INCLUDED_
#define _GAME_DOSEXEC_H_INCLUDED_

#include <stdint.h>
#include "Game_dosregs.h"

/* INT 21h AH=4Bh: the other programs of a DOS game, modules of the same binary
 * (exe_modules= of game.cfg; Game_dosexec.c). */

#ifdef __cplusplus
extern "C" {
#endif

/* AH=4Bh: runs the program, returns when it has ended; 1 (the registers set) */
int Game_DosExec(Game_DosRegs *regs);
/* AH=4Ch of the program running: back to its Game_DosExec; returns if none runs */
void Game_DosExecExit(uint8_t status);
/* AH=4Dh: the exit code of the program that ran last */
uint8_t Game_DosExecCode(void);

#ifdef __cplusplus
}
#endif

#endif
